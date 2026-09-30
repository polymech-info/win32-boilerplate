#!/usr/bin/env node
/**
 * Pixlwiz / Polymech **service** integration: `pm-image service upload --dump-raw-http` (multipart /api/images).
 *
 * Requires:
 *   - Built `dist/pm-image(.exe)` or `dist-osx/pm-image` (darwin)
 *   - Prior `pm-image login` so zitadel-oauth.json exists (macOS: beside PixelWiz `settings.json`)
 *   - `SERVER_URL` in `packages/media/cpp/.env` or in the environment (e.g. https://pixlwiz.com)
 *
 * Service PNG fixtures under `tests/assets/service/` are created automatically from
 * `tests/assets/build-fixtures.mjs` when missing (same as `npm run generate:assets`).
 *
 * Run:
 *   npm run test:service
 *   npm run test:service -- --upload-only   (same suite; flag reserved for future splits)
 *
 * On success writes **tests/test-service-upload-responses.json** (stdout lines with exact **raw_body** from `/api/images?forward=vfs&original=true`).
 *
 * Skip (CI / offline):
 *   SKIP_SERVICE_TEST=1
 */

import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { mediaExePath } from "./media-presets.js";
import { createAssert } from "./test-commons.js";
import { buildMetricsBundle, createMetricsCollector, renderMarkdownReport } from "./reports.js";
import {
  appendMarkdownChapter,
  getChapterTitle,
  getNpmScriptLabel,
  isAccumulateMode,
  TEST_REPORT_ALL_PATH,
} from "./test-report-accumulate.mjs";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = join(__dirname, "..");
const TEST_REPORT_PATH = join(root, "tests", "test-report-last.md");
const EXE = mediaExePath(__dirname);
const SERVICE_DIR = join(root, "tests", "assets", "service");
const BUILD_FIXTURES_SCRIPT = join(root, "tests", "assets", "build-fixtures.mjs");
/** Written on each successful run: exact stdout objects (includes raw_body when using --dump-raw-http). */
const UPLOAD_RESPONSES_JSON = join(root, "tests", "test-service-upload-responses.json");

/** Ensures `upload-a.png` / `upload-b.png` exist (same generator as `npm run generate:assets`). */
function ensureServiceFixtures() {
  const a = join(SERVICE_DIR, "upload-a.png");
  const b = join(SERVICE_DIR, "upload-b.png");
  if (existsSync(a) && existsSync(b)) return true;
  console.log("[test:service] missing service PNGs — running tests/assets/build-fixtures.mjs …");
  const r = spawnSync(process.execPath, [BUILD_FIXTURES_SCRIPT], {
    encoding: "utf8",
    cwd: root,
    stdio: "inherit",
  });
  return r.status === 0 && existsSync(a) && existsSync(b);
}

const uploadOnly = process.argv.includes("--upload-only");
void uploadOnly;

function stripInlineCommentUnquoted(v) {
  if (!v) return v;
  const q = v[0];
  if (q === '"' || q === "'") return v;
  const hash = v.indexOf("#");
  if (hash < 0) return v.trimEnd();
  return v.slice(0, hash).trimEnd();
}

/** Load `packages/media/cpp/.env` into `process.env` (fills missing or empty vars). */
function loadEnvFromPackage() {
  const envPath = join(root, ".env");
  if (!existsSync(envPath)) return { envPath, loaded: 0, keys: [] };
  let loaded = 0;
  const keys = [];
  for (const raw of readFileSync(envPath, "utf8").split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith("#")) continue;
    const eq = line.indexOf("=");
    if (eq < 0) continue;
    const k = line.slice(0, eq).trim();
    let v = line.slice(eq + 1).trim();
    v = stripInlineCommentUnquoted(v);
    if ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith("'") && v.endsWith("'"))) v = v.slice(1, -1);
    const cur = process.env[k];
    if (cur === undefined || cur === "") {
      process.env[k] = v;
      loaded++;
      keys.push(k);
    }
  }
  return { envPath, loaded, keys };
}

function pmImgArgs(...argv) {
  return ["--no-gui", "--no-mcp", ...argv];
}

function createTestReport() {
  return {
    meta: {},
    notes: [],
    suites: [],
    images: [],
    _cur: null,
    beginSuite(name) {
      if (this._cur) this.endSuite();
      this._cur = { name, steps: [], t0: performance.now() };
    },
    endSuite() {
      if (!this._cur) return;
      this._cur.totalMs = performance.now() - this._cur.t0;
      this.suites.push(this._cur);
      this._cur = null;
    },
    step(label, ms, detail = "") {
      if (!this._cur) return;
      this._cur.steps.push({ label, ms: Math.round(ms * 100) / 100, detail: detail || "" });
    },
    finalize(stats, wallMs, exe, assetsDir, extra = {}) {
      this.meta = {
        generatedAt: new Date().toISOString(),
        node: process.version,
        argv: process.argv.slice(2),
        exe,
        assetsDir: assetsDir || "",
        passed: stats.passed,
        failed: stats.failed,
        wallClockMs: Math.round(wallMs * 100) / 100,
        ...extra,
      };
      if (this._cur) this.endSuite();
    },
  };
}

function writeTestReportFile(rep, metricsCollector, startedAtIso) {
  mkdirSync(dirname(TEST_REPORT_PATH), { recursive: true });
  const finishedAtIso = new Date().toISOString();
  const metrics = buildMetricsBundle(metricsCollector, startedAtIso, finishedAtIso);
  const failed = rep.meta.failed ?? 0;
  const ok = failed === 0 && !rep.meta.abortReason && !rep.meta.uncaughtError;
  let md = renderMarkdownReport({
    ok,
    passed: rep.meta.passed,
    failed,
    abortReason: rep.meta.abortReason,
    uncaughtError: rep.meta.uncaughtError,
    error: rep.meta.uncaughtError,
    meta: {
      cwd: process.cwd(),
      displayName: "service-api (Pixlwiz upload)",
      testName: "service-upload",
      writtenAt: finishedAtIso,
      generatedAt: rep.meta.generatedAt,
      exe: rep.meta.exe,
      assetsDir: rep.meta.assetsDir,
      argv: Array.isArray(rep.meta.argv) ? rep.meta.argv.join(" ") : String(rep.meta.argv ?? ""),
      wallClockMs: rep.meta.wallClockMs,
    },
    metrics,
    images: rep.images,
    mediaSuites: rep.suites,
    integrationNotes: rep.notes,
  });
  const acc = isAccumulateMode() && getChapterTitle();
  if (acc) {
    md += `\n---\n\n*Per-run: \`tests/test-report-last.md\` — this run is also a **chapter** in \`tests/test-report-all.md\` (from \`npm run test:all\`).*\n`;
  } else {
    md += `\n---\n\n*Artifact: \`tests/test-report-last.md\` — overwritten on each test run.*\n`;
  }
  writeFileSync(TEST_REPORT_PATH, md, "utf8");
  if (acc) {
    try {
      appendMarkdownChapter(getChapterTitle(), md, { npmScript: getNpmScriptLabel() || undefined });
      console.log(`  (aggregated report chapter: ${TEST_REPORT_ALL_PATH})`);
    } catch (e) {
      console.error("Failed to append chapter to test-report-all.md:", e);
    }
  }
}

async function suiteServiceUpload(rep, stats) {
  const { assert } = stats;
  rep.beginSuite("service upload (Pixlwiz /api/images)");
  console.log("\n-- Service: upload --\n");

  const a = join(SERVICE_DIR, "upload-a.png");
  const b = join(SERVICE_DIR, "upload-b.png");
  if (!ensureServiceFixtures()) {
    console.error("Missing fixtures under tests/assets/service/ (build-fixtures.mjs failed)");
    assert(false, "service fixtures present");
    rep.endSuite();
    return;
  }

  const serverUrl = (process.env.SERVER_URL || "").trim();
  if (!serverUrl) {
    logService("SERVER_URL missing after .env load; set SERVER_URL in packages/media/cpp/.env or export it.");
    console.error("SERVER_URL not set (add to packages/media/cpp/.env)");
    assert(false, "SERVER_URL set");
    rep.endSuite();
    return;
  }
  console.log("  SERVER_URL:", serverUrl);

  const t0 = performance.now();
  const r = spawnSync(EXE, pmImgArgs("service", "upload", "--dump-raw-http", a, b), {
    encoding: "utf8",
    env: { ...process.env },
    timeout: 180_000,
  });
  const ms = performance.now() - t0;
  rep.step("service upload two files", ms, `exit ${r.status ?? -1}`);

  console.log("  exit:", r.status, "  (" + Math.round(ms) + " ms)");
  console.log(
    "  stderr has [service-upload]:",
    (r.stderr || "").includes("[service-upload]") ? "yes" : "no",
  );
  if (r.stderr && r.stderr.length > 400) console.log("  stderr tail:\n", (r.stderr || "").slice(-500));

  assert(r.status === 0, `service upload exit 0 (got ${r.status}) stderr: ${(r.stderr || "").slice(0, 600)}`);
  const out = r.stdout || "";
  assert(out.includes('"url"'), "stdout JSON lines include url field");
  assert(out.includes("http://") || out.includes("https://"), "stdout contains absolute URL");
  const lines = out
    .trim()
    .split(/\r?\n/)
    .filter(Boolean);
  assert(lines.length >= 2, `expected >=2 JSON lines, got ${lines.length}`);
  const parsedLines = [];
  for (const line of lines) {
    const j = JSON.parse(line);
    assert(typeof j.url === "string" && j.url.length > 0, "each line has non-empty url");
    assert(typeof j.path === "string", "each line has path");
    assert(j.http_status >= 200 && j.http_status < 300, "dump-raw-http: http_status 2xx");
    assert(typeof j.raw_body === "string" && j.raw_body.length > 0, "dump-raw-http: raw_body string");
    const raw = JSON.parse(j.raw_body);
    assert(raw.url === j.url, "raw_body JSON url matches line url");
    assert(raw.meta !== null && typeof raw.meta === "object", "/api/images (vfs) response has meta object");
    for (const k of ["width", "height", "format", "size", "filename"]) {
      assert(Object.prototype.hasOwnProperty.call(raw, k), `raw_body has ${k} (pm-pics VFS upload JSON)`);
    }
    parsedLines.push(j);
  }
  console.log("  JSON lines:", lines.length, "ok (raw_body round-trip)");

  mkdirSync(dirname(UPLOAD_RESPONSES_JSON), { recursive: true });
  writeFileSync(
    UPLOAD_RESPONSES_JSON,
    JSON.stringify(
      {
        generatedAt: new Date().toISOString(),
        endpoint: "POST /api/images?forward=vfs&original=true",
        lines: parsedLines,
      },
      null,
      2,
    ),
    "utf8",
  );
  console.log("  wrote", UPLOAD_RESPONSES_JSON);

  rep.endSuite();
}

function logService(...args) {
  console.error("[test:service]", ...args);
}

async function run() {
  const envInfo = loadEnvFromPackage();
  if (envInfo.envPath) {
    logService(".env path:", envInfo.envPath, "exists:", existsSync(envInfo.envPath));
    logService(".env applied", envInfo.loaded, "key(s) into process.env (only missing/empty):", envInfo.keys.join(", ") || "(none)");
  }
  const stats = createAssert();
  const rep = createTestReport();
  const metricsCollector = createMetricsCollector();
  const startedAtIso = new Date().toISOString();
  const wallStart = performance.now();
  let exitCode = 0;

  if (process.env.SKIP_SERVICE_TEST === "1" || process.env.SKIP_SERVICE_TEST === "true") {
    logService("skip (SKIP_SERVICE_TEST)");
    process.exit(0);
  }

  if (!existsSync(EXE)) {
    console.error("test:service: binary missing:", EXE);
    process.exit(1);
  }

  try {
    await suiteServiceUpload(rep, stats);
    exitCode = stats.failed > 0 ? 1 : 0;
    console.log(`\nDone. Passed: ${stats.passed}  Failed: ${stats.failed}\n`);
  } catch (e) {
    console.error(e);
    rep.meta = rep.meta || {};
    rep.meta.uncaughtError = String(e?.stack || e);
    exitCode = 1;
  } finally {
    rep.finalize(stats, performance.now() - wallStart, EXE, SERVICE_DIR);
    try {
      writeTestReportFile(rep, metricsCollector, startedAtIso);
      console.log("Test report written:", TEST_REPORT_PATH);
    } catch (e) {
      console.error("Failed to write test report:", e);
    }
  }
  process.exit(exitCode);
}

run();
