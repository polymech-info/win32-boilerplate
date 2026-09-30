#!/usr/bin/env node
/**
 * Pixlwiz **service posts**: `pm-image service posts create` (POST /api/posts + uploads + /api/pictures).
 *
 * Uses every `*.png` / `*.jpg` / `*.jpeg` / `*.webp` under `tests/assets/service/posts/` (you supply files; `build-fixtures.mjs` does not write there).
 * Runs with **`--dump-raw-http`** and writes **`tests/test-service-posts-responses.json`** (exact API response bodies per step).
 *
 * Requires: built `dist/pm-image`, `pm-image login`, `SERVER_URL` in `packages/media/cpp/.env`.
 *
 *   npm run test:service:posts
 *
 * Skip: SKIP_SERVICE_POSTS_TEST=1
 */

import { spawnSync } from "node:child_process";
import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { basename, dirname, extname, join } from "node:path";
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
const POSTS_DIR = join(root, "tests", "assets", "service", "posts");
const POSTS_RESPONSES_JSON = join(root, "tests", "test-service-posts-responses.json");

const IMG_EXT = new Set([".png", ".jpg", ".jpeg", ".webp"]);

function stripInlineCommentUnquoted(v) {
  if (!v) return v;
  const q = v[0];
  if (q === '"' || q === "'") return v;
  const hash = v.indexOf("#");
  if (hash < 0) return v.trimEnd();
  return v.slice(0, hash).trimEnd();
}

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

function logPosts(...args) {
  console.error("[test:service:posts]", ...args);
}

function pmImgArgs(...argv) {
  return ["--no-gui", "--no-mcp", ...argv];
}

function listPostFixtures() {
  if (!existsSync(POSTS_DIR)) return [];
  return readdirSync(POSTS_DIR)
    .filter((name) => IMG_EXT.has(extname(name).toLowerCase()))
    .map((name) => join(POSTS_DIR, name))
    .sort();
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
      displayName: "service-posts (Pixlwiz create)",
      testName: "service-posts-create",
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

const UUID_RE = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

async function suitePostsCreate(rep, stats) {
  const { assert } = stats;
  rep.beginSuite("service posts create");
  console.log("\n-- Service: posts create --\n");

  const files = listPostFixtures();
  if (files.length === 0) {
    rep.notes.push("No images under tests/assets/service/posts/");
    console.error(
      "Add one or more images under tests/assets/service/posts/ (not generated by build-fixtures). Supported: .png .jpg .jpeg .webp",
    );
    assert(false, "post fixtures present");
    rep.endSuite();
    return;
  }

  logPosts("fixture files:", files.length, files.map((f) => basename(f)).join(", "));

  const serverUrl = (process.env.SERVER_URL || "").trim();
  if (!serverUrl) {
    logPosts("SERVER_URL missing; set in packages/media/cpp/.env");
    assert(false, "SERVER_URL set");
    rep.endSuite();
    return;
  }
  console.log("  SERVER_URL:", serverUrl);

  const t0 = performance.now();
  const r = spawnSync(EXE, pmImgArgs("service", "posts", "create", "--dump-raw-http", ...files), {
    encoding: "utf8",
    env: { ...process.env },
    timeout: 300_000,
  });
  const ms = performance.now() - t0;
  rep.step("service posts create", ms, `exit ${r.status ?? -1}`);

  console.log("  exit:", r.status, "  (" + Math.round(ms) + " ms)");
  console.log(
    "  stderr has [service-posts]:",
    (r.stderr || "").includes("[service-posts]") ? "yes" : "no",
  );
  if (r.stderr && r.stderr.length > 500) console.log("  stderr tail:\n", (r.stderr || "").slice(-400));

  assert(r.status === 0, `posts create exit 0 (got ${r.status}) stderr: ${(r.stderr || "").slice(0, 800)}`);
  const line = (r.stdout || "").trim().split(/\r?\n/).filter(Boolean).pop();
  assert(line, "stdout has JSON line");
  const j = JSON.parse(line);
  assert(typeof j.post_id === "string" && UUID_RE.test(j.post_id), "post_id is UUID");
  assert(Array.isArray(j.picture_ids), "picture_ids is array");
  assert(j.picture_ids.length === files.length, `picture_ids length ${j.picture_ids.length} === files ${files.length}`);
  for (const id of j.picture_ids) {
    assert(typeof id === "string" && UUID_RE.test(id), "each picture id is UUID");
  }

  assert(Array.isArray(j.http_raw_steps), "dump-raw-http: http_raw_steps array");
  const n = files.length;
  assert(j.http_raw_steps.length === 1 + 2 * n, `http_raw_steps length 1+2n = ${1 + 2 * n}, got ${j.http_raw_steps.length}`);
  assert(j.http_raw_steps[0].label === "POST /api/posts", "first step is create post");
  assert(j.http_raw_steps[0].http_status >= 200 && j.http_raw_steps[0].http_status < 300, "create post 2xx");
  const createRaw = JSON.parse(j.http_raw_steps[0].body_raw);
  assert(createRaw.success === true && createRaw.post && createRaw.post.id === j.post_id, "create post raw body matches post_id");

  for (let k = 0; k < n; k++) {
    const up = j.http_raw_steps[1 + 2 * k];
    const pic = j.http_raw_steps[2 + 2 * k];
    assert(typeof up.body_raw === "string" && up.body_raw.includes("url"), "upload step has raw JSON with url");
    assert(up.label.includes("/api/images"), "upload step label");
    const upJ = JSON.parse(up.body_raw);
    assert(typeof upJ.url === "string" && upJ.url.length > 0, "upload raw url");
    assert(pic.label.startsWith("POST /api/pictures"), "picture step label");
    const picJ = JSON.parse(pic.body_raw);
    assert(picJ.id === j.picture_ids[k], `picture raw id matches picture_ids[${k}]`);
  }

  mkdirSync(dirname(POSTS_RESPONSES_JSON), { recursive: true });
  writeFileSync(
    POSTS_RESPONSES_JSON,
    JSON.stringify(
      {
        generatedAt: new Date().toISOString(),
        summary: { post_id: j.post_id, picture_ids: j.picture_ids, fixtureCount: n },
        http_raw_steps: j.http_raw_steps,
      },
      null,
      2,
    ),
    "utf8",
  );
  console.log("  wrote", POSTS_RESPONSES_JSON);
  console.log("  post_id:", j.post_id, "pictures:", j.picture_ids.length);

  rep.endSuite();
}

async function run() {
  const envInfo = loadEnvFromPackage();
  if (envInfo.envPath) {
    logPosts(".env path:", envInfo.envPath, "exists:", existsSync(envInfo.envPath));
    logPosts(".env applied keys (missing/empty only):", envInfo.keys.join(", ") || "(none)");
  }

  const stats = createAssert();
  const rep = createTestReport();
  const metricsCollector = createMetricsCollector();
  const startedAtIso = new Date().toISOString();
  const wallStart = performance.now();
  let exitCode = 0;

  if (process.env.SKIP_SERVICE_POSTS_TEST === "1" || process.env.SKIP_SERVICE_POSTS_TEST === "true") {
    logPosts("skip (SKIP_SERVICE_POSTS_TEST)");
    process.exit(0);
  }

  if (!existsSync(EXE)) {
    console.error("test:service:posts: binary missing:", EXE);
    process.exit(1);
  }

  try {
    await suitePostsCreate(rep, stats);
    exitCode = stats.failed > 0 ? 1 : 0;
    console.log(`\nDone. Passed: ${stats.passed}  Failed: ${stats.failed}\n`);
  } catch (e) {
    console.error(e);
    rep.meta = rep.meta || {};
    rep.meta.uncaughtError = String(e?.stack || e);
    exitCode = 1;
  } finally {
    rep.finalize(stats, performance.now() - wallStart, EXE, POSTS_DIR);
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
