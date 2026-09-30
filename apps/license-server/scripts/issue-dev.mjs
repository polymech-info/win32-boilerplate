#!/usr/bin/env node
/**
 * Dev helper: read this machine's fingerprint via pm-image, POST /v1/license/issue,
 * write license-dev.json or license-dev.dat. Requires server running (npm start) and
 * a built dist/pm-image.exe on Windows (or set PM_IMAGE_EXE / MACHINE_HASH).
 *
 * Usage:
 *   npm run issue-dev
 *   npm run issue-dev -- --dat
 *   npm run issue-dev -- --email other@example.com --out ./my.lic.json
 *
 * Env: LICENSE_SERVER_URL, LICENSE_API_KEY, PORT, DEV_USER_EMAIL, PM_IMAGE_EXE,
 *      MACHINE_HASH (skip pm-image if set), ISSUE_FORMAT=dat|json
 */
import { writeFileSync, readFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { config } from "dotenv";

const __dirname = dirname(fileURLToPath(import.meta.url));
const pkgRoot = join(__dirname, "..");
config({ path: join(pkgRoot, ".env") });

/** packages/media/cpp (three levels up from scripts/) */
const cppRoot = join(__dirname, "..", "..", "..");
const defaultPmImage = join(cppRoot, "dist", "pm-image.exe");

function parseArgs(argv) {
  let format = process.env.ISSUE_FORMAT === "dat" ? "dat" : "json";
  let out = null;
  let email = process.env.DEV_USER_EMAIL || null;
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--dat" || a === "--format=dat") format = "dat";
    else if (a === "--json" || a === "--format=json") format = "json";
    else if (a === "--email" && argv[i + 1]) {
      email = argv[++i];
    } else if (a === "--out" && argv[i + 1]) {
      out = argv[++i];
    } else if (a.startsWith("--out=")) {
      out = a.slice(6);
    }
  }
  if (!out) {
    out = format === "dat" ? join(pkgRoot, "license-dev.dat") : join(pkgRoot, "license-dev.json");
  }
  return { format, out, email };
}

function loadSeedEmail() {
  const p = join(pkgRoot, "seed", "users.json");
  const j = JSON.parse(readFileSync(p, "utf8"));
  const u = j.users?.[0]?.email;
  if (!u) throw new Error(`No users in ${p}`);
  return u;
}

function getMachineHash() {
  const fromEnv = process.env.MACHINE_HASH?.trim();
  if (fromEnv) {
    if (!/^[0-9a-f]{64}$/i.test(fromEnv)) {
      throw new Error("MACHINE_HASH must be 64 hex chars");
    }
    return fromEnv.toLowerCase();
  }
  if (process.platform !== "win32") {
    throw new Error(
      "On non-Windows set MACHINE_HASH to a 64-hex fingerprint, or run pm-image on Windows."
    );
  }
  const exe = process.env.PM_IMAGE_EXE || defaultPmImage;
  if (!existsSync(exe)) {
    throw new Error(
      `pm-image not found at ${exe}. Build the app or set PM_IMAGE_EXE.`
    );
  }
  const r = spawnSync(exe, ["--no-mcp", "license", "fingerprint"], {
    encoding: "utf8",
    windowsHide: true,
  });
  if (r.status !== 0) {
    throw new Error(r.stderr || r.stdout || `pm-image exited ${r.status}`);
  }
  const line = r.stdout.trim().split(/\r?\n/).pop()?.trim() ?? "";
  if (!/^[0-9a-f]{64}$/i.test(line)) {
    throw new Error(`Bad fingerprint output: ${line.slice(0, 80)}`);
  }
  return line.toLowerCase();
}

async function main() {
  const { format, out, email: emailArg } = parseArgs(process.argv.slice(2));
  const user_email = emailArg || loadSeedEmail();

  const port = process.env.PORT || 8787;
  const base =
    process.env.LICENSE_SERVER_URL?.replace(/\/$/, "") ||
    `http://127.0.0.1:${port}`;
  const apiKey = process.env.LICENSE_API_KEY || "dev-api-key";

  const machine_hash = getMachineHash();
  const body = JSON.stringify({
    user_email,
    machine_hash,
    ...(format === "dat" ? { format: "dat" } : {}),
  });

  const url = `${base}/v1/license/issue`;
  const res = await fetch(url, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${apiKey}`,
      "Content-Type": "application/json",
    },
    body,
  });

  if (!res.ok) {
    const t = await res.text();
    throw new Error(`POST ${url} -> ${res.status}: ${t.slice(0, 500)}`);
  }

  if (format === "dat") {
    const buf = Buffer.from(await res.arrayBuffer());
    writeFileSync(out, buf);
    console.log(`Wrote ${buf.length} bytes -> ${out}`);
  } else {
    const text = await res.text();
    writeFileSync(out, text, "utf8");
    console.log(`Wrote JSON -> ${out}`);
  }
  console.log(`user_email=${user_email} machine_hash=${machine_hash}`);
  console.log(`Import: pm-image license import "${out}"`);
}

main().catch((e) => {
  console.error(e.message || e);
  process.exit(1);
});
