#!/usr/bin/env node
/**
 * `npm run test:zitadel` — exercises `pm-image login --probe` (+ optional `--decode-jwt`
 * when ZITADEL_TEST_ACCESS_TOKEN is set). Loads `ref/pm-pics/.env` for VITE_* / ZITADEL_* defaults.
 *
 * Set SKIP_ZITADEL_TEST=1 to skip (offline / no binary).
 */
import { spawnSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { mediaExePath } from "./media-presets.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "../..");

function zitadelCliPath() {
  const fromEnv = process.env.PM_IMAGE || process.env.MEDIA_IMG_EXE;
  if (fromEnv && fromEnv.trim()) return resolve(fromEnv.trim());
  const candidates = process.platform === "win32"
    ? [
        resolve(root, "dist", "win-x64", "pm-image-cli.exe"),
        resolve(root, "dist", "win-x64", "pm-image.exe"),
        mediaExePath(resolve(root, "orchestrator")),
      ]
    : [mediaExePath(resolve(root, "orchestrator"))];
  return candidates.find((p) => existsSync(p)) || candidates[0];
}

function loadDotEnvFile(path) {
  if (!existsSync(path)) return;
  const text = readFileSync(path, "utf8");
  for (const line of text.split(/\r?\n/)) {
    const t = line.trim();
    if (!t || t.startsWith("#")) continue;
    const eq = t.indexOf("=");
    if (eq <= 0) continue;
    const key = t.slice(0, eq).trim();
    let val = t.slice(eq + 1).trim();
    if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith("'") && val.endsWith("'")))
      val = val.slice(1, -1);
    if (key && val && process.env[key] === undefined) process.env[key] = val;
  }
}

if (process.env.SKIP_ZITADEL_TEST === "1" || process.env.SKIP_ZITADEL_TEST === "true") {
  console.log("test:zitadel: skip (SKIP_ZITADEL_TEST)");
  process.exit(0);
}

const exe = zitadelCliPath();
if (!existsSync(exe)) {
  console.error("test:zitadel: binary missing:", exe, "(run npm run buildf first)");
  process.exit(1);
}

loadDotEnvFile(resolve(root, "ref", "pm-pics", ".env"));

const env = { ...process.env, PM_IMAGE_MCP: "0" };
const r1 = spawnSync(exe, ["--no-gui", "--no-mcp", "login", "--probe"], { encoding: "utf8", env, stdio: ["ignore", "pipe", "pipe"] });
if (r1.status !== 0) {
  console.error("test:zitadel: login --probe failed", r1.status);
  if (r1.stderr) console.error(r1.stderr);
  if (r1.stdout) console.error(r1.stdout);
  process.exit(1);
}
console.log("test:zitadel: login --probe ok");
if (r1.stderr && r1.stderr.includes("[zitadel-login]"))
  console.log("test:zitadel: stderr contains [zitadel-login] verbose prefix");

const tok = env.ZITADEL_TEST_ACCESS_TOKEN?.trim();
if (tok) {
  const r2 = spawnSync(exe, ["--no-gui", "--no-mcp", "login", "--decode-jwt", tok], {
    encoding: "utf8",
    env,
    stdio: ["ignore", "pipe", "pipe"],
  });
  if (r2.status !== 0) {
    console.error("test:zitadel: login --decode-jwt failed", r2.status);
    if (r2.stderr) console.error(r2.stderr);
    process.exit(1);
  }
  if (!r2.stderr.includes("sub:")) {
    console.error("test:zitadel: expected decode-jwt stderr to mention sub");
    process.exit(1);
  }
  console.log("test:zitadel: login --decode-jwt ok (ZITADEL_TEST_ACCESS_TOKEN)");
} else {
  console.log("test:zitadel: skip decode-jwt (set ZITADEL_TEST_ACCESS_TOKEN to exercise)");
}

process.exit(0);
