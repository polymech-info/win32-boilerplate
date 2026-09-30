#!/usr/bin/env node
/**
 * Smoke-test `pm-image status` (trial read-only API). Windows + built dist only.
 */
import { spawnSync } from "node:child_process";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const exe = resolve(root, "dist", "pm-image.exe");

if (process.platform !== "win32") {
  console.log("test:trial-protection: skip (Windows only)");
  process.exit(0);
}

const r = spawnSync(exe, ["--no-mcp", "status", "--json"], { encoding: "utf8" });
if (r.status !== 0) {
  console.error(r.stderr || r.stdout || `exit ${r.status}`);
  process.exit(1);
}

let j;
try {
  j = JSON.parse(r.stdout.trim());
} catch {
  console.error("Invalid JSON from status --json:\n", r.stdout);
  process.exit(1);
}

const required = [
  "godmode",
  "state",
  "remaining_seconds",
  "install_time",
  "expiry_time",
  "last_run_time",
  "now",
  "trial_id",
];
for (const k of required) {
  if (!(k in j)) {
    console.error("Missing JSON key:", k);
    process.exit(1);
  }
}

const okStates = new Set([
  "godmode",
  "pending_first_run",
  "active",
  "expired",
  "invalid",
]);
if (!okStates.has(j.state)) {
  console.error("Unexpected state:", j.state);
  process.exit(1);
}

console.log("test:trial-protection: ok (state=%s)", j.state);
process.exit(0);
