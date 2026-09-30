/**
 * Sample Hono license issuer for pm-image (development only).
 * Uses libsodium Ed25519 — must match src/win/license_pub_key.hpp + LICENSE_ISSUER_SECRET_HEX.
 */
import { serve } from "@hono/node-server";
import { Hono } from "hono";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { buildLicenseDatBlob } from "./license-dat.js";

const require = createRequire(import.meta.url);
const sodium = require("libsodium-wrappers");

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, "..");
const seedPath = join(root, "seed", "users.json");
const dataDir = join(root, "data");
const machinesPath = join(dataDir, "machines.json");

const PORT = Number(process.env.PORT || 8787);
const API_KEY = process.env.LICENSE_API_KEY || "dev-api-key";
const MAX_MACHINES = Number(process.env.MAX_MACHINES_PER_USER || 3);
const SECRET_HEX = process.env.LICENSE_ISSUER_SECRET_HEX || "";
const DAT_MIN_NOISE = Number(process.env.LICENSE_DAT_MIN_NOISE_BYTES || 8192);

await sodium.ready;

function loadSecretKey() {
  if (!SECRET_HEX || SECRET_HEX.length !== 128) {
    console.error(
      "Set LICENSE_ISSUER_SECRET_HEX (64-byte / 128 hex chars). Run: npm run gen-keys"
    );
    process.exit(1);
  }
  const sk = Buffer.from(SECRET_HEX, "hex");
  if (sk.length !== 64) {
    console.error("LICENSE_ISSUER_SECRET_HEX must decode to 64 bytes");
    process.exit(1);
  }
  return sk;
}

const secretKey = loadSecretKey();

async function loadJson(path, fallback) {
  try {
    const raw = await readFile(path, "utf8");
    return JSON.parse(raw);
  } catch {
    return fallback;
  }
}

async function saveMachines(obj) {
  await mkdir(dataDir, { recursive: true });
  await writeFile(machinesPath, JSON.stringify(obj, null, 2), "utf8");
}

const app = new Hono();

app.get("/health", (c) => c.json({ ok: true, service: "pm-image-license-server" }));

/** Seed users (read-only reference). */
app.get("/v1/seed-users", async (c) => {
  const seed = await loadJson(seedPath, { users: [] });
  const safe = (seed.users || []).map((u) => ({
    email: u.email,
    max_machines: u.max_machines ?? MAX_MACHINES,
  }));
  return c.json({ users: safe });
});

/**
 * Issue a license for one machine.
 * Header: Authorization: Bearer <LICENSE_API_KEY>
 * Body: { "user_email", "machine_hash", "format"?: "json" | "dat" }
 * - Default / "json": JSON envelope (legacy license.json).
 * - "dat": binary license.dat (random noise + same envelope JSON inside; see docs/license-server.md).
 */
app.post("/v1/license/issue", async (c) => {
  const auth = c.req.header("authorization") || "";
  const token = auth.replace(/^Bearer\s+/i, "").trim();
  if (token !== API_KEY) {
    return c.json({ error: "unauthorized" }, 401);
  }

  let body;
  try {
    body = await c.req.json();
  } catch {
    return c.json({ error: "invalid json" }, 400);
  }
  const user_email = body.user_email;
  const machine_hash = body.machine_hash;
  const format = body.format === "dat" ? "dat" : "json";
  if (!user_email || !machine_hash) {
    return c.json({ error: "user_email and machine_hash required" }, 400);
  }
  if (!/^[0-9a-f]{64}$/i.test(machine_hash)) {
    return c.json({ error: "machine_hash must be 64 hex chars" }, 400);
  }

  const seed = await loadJson(seedPath, { users: [] });
  const user = (seed.users || []).find(
    (u) => u.email.toLowerCase() === String(user_email).toLowerCase()
  );
  if (!user) {
    return c.json({ error: "unknown user_email (see seed/users.json)" }, 403);
  }

  const maxMach = user.max_machines ?? MAX_MACHINES;
  const machinesData = await loadJson(machinesPath, {});
  const key = user_email.toLowerCase();
  const list = Array.isArray(machinesData[key]) ? machinesData[key] : [];
  const mh = machine_hash.toLowerCase();
  if (!list.includes(mh)) {
    if (list.length >= maxMach) {
      return c.json(
        {
          error: "machine limit reached",
          max_machines: maxMach,
          hint: "Raise max_machines or clear data/machines.json for dev",
        },
        403
      );
    }
    list.push(mh);
    machinesData[key] = list;
    await saveMachines(machinesData);
  }

  const inner = {
    v: 1,
    license_id: `lic-${Date.now()}`,
    machine_hash: mh,
    user_email: user.email,
    issued_at: Math.floor(Date.now() / 1000),
    expiry: null,
  };
  const msg = Buffer.from(JSON.stringify(inner), "utf8");
  const sig = Buffer.from(sodium.crypto_sign_detached(msg, secretKey));

  const file = {
    v: 1,
    payload_hex: msg.toString("hex"),
    signature_hex: sig.toString("hex"),
  };

  if (format === "dat") {
    const blob = buildLicenseDatBlob(file, { minNoiseBytes: DAT_MIN_NOISE });
    return new Response(blob, {
      status: 200,
      headers: {
        "Content-Type": "application/octet-stream",
        "Content-Disposition": 'attachment; filename="license.dat"',
      },
    });
  }

  return c.json(file);
});

serve({ fetch: app.fetch, port: PORT }, (info) => {
  console.log(`license-server listening on http://127.0.0.1:${info.port}`);
  console.log(`  POST /v1/license/issue  Bearer ${API_KEY}`);
  console.log(`  Body optional: "format": "dat" → binary license.dat (noise + JSON envelope)`);
  console.log(`  MAX_MACHINES_PER_USER=${MAX_MACHINES} (server default; per-user override in seed/users.json)`);
  console.log(`  LICENSE_DAT_MIN_NOISE_BYTES=${DAT_MIN_NOISE} (format=dat only)`);
});
