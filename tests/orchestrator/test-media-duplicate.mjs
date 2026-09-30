/**
 * Duplicates integration tests (CLI `media-img duplicates` / pm-image.exe).
 *
 * Reuses orchestrator helpers: media-presets, test-commons, reports.
 *
 * Run (from packages/media/cpp, after build:release):
 *   npm run test:duplicates              — all suites below
 *   npm run test:duplicates:size-name    — by-size fixture only
 *   npm run test:duplicates:fingerprint  — resized-pair dHash fixture only
 *   npm run test:duplicates:meta         — meta sidecar JSON via LLM (--meta-compare-json-llm); skips if no API key
 *
 * On failure, full stderr/stdout is printed (not only the first 500 chars). For meta LLM, read the last
 * `duplicates:` lines: API errors, or exit 1 with zero groups when every pair scored below
 * --meta-json-min-sim (see pm_image_cmd_dup.cpp: success but `dr.groups.empty()` → exit 1).
 */
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { defaultAssetsDir, mediaExePath } from './media-presets.js';
import { createAssert } from './test-commons.js';
import { buildMetricsBundle, createMetricsCollector, fileByteSize, renderMarkdownReport } from './reports.js';
import {
  appendMarkdownChapter,
  getChapterTitle,
  getNpmScriptLabel,
  isAccumulateMode,
  TEST_REPORT_ALL_PATH,
} from './test-report-accumulate.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const EXE = mediaExePath(__dirname);
/** Disable embedded MCP for duplicate-suite spawns (default MCP is on in the binary). */
const pmSpawnEnv = { ...process.env, PM_IMAGE_MCP: '0' };
const assetsDir = resolve(defaultAssetsDir(__dirname));
const TEST_REPORT_PATH = join(__dirname, '..', 'tests', 'test-report-last-duplicate.md');
/** Per-suite detailed CLI outputs (same payload as `DuplicatesResult::report`, MD + JSON). */
const DUPLICATE_REPORTS_DIR = join(__dirname, '..', 'tests', 'duplicate-reports');
/** Filenames use `-detail` so a hand-placed e.g. `meta.json` in this folder is not clobbered by the meta suite. */
function detailReportPaths(suite) {
  const n = String(suite);
  return {
    md: join(DUPLICATE_REPORTS_DIR, `${n}-detail.md`),
    json: join(DUPLICATE_REPORTS_DIR, `${n}-detail.json`),
  };
}

const argv = new Set(process.argv.slice(2));
const runSizeName = argv.has('--size-name') || argv.size === 0;
const runFingerprint = argv.has('--fingerprint') || argv.size === 0;
const runMeta = argv.has('--meta') || argv.size === 0;

const stats = createAssert();
const { assert } = stats;

// ── by-name-size (same byte size, two folders) ─────────────────────────────
const PAIR_BASENAME = 'DSC06829_800.JPG';
const EXPECTED_SIZE_BYTES = 97412;
const FIXTURE_SIZE = join('duplicate', 'by-name-size');

// ── fingerprint (full + width-1200 resize; dHash needs Hamming > 0) ─────────
const FIXTURE_FP = join('duplicate', 'fingerprint');
const FP_FULL = 'DSC07015.JPG';
const FP_RESIZED = 'DSC07015_1200.JPG';
/** Empirical: this pair’s Hamming distance is 28–29; 32 gives a small margin. */
const FP_MAX_HAMMING = 32;

// ── meta (sidecar JSON fields compared by LLM; same workshop scene, two angles; EXIF off) ─
const FIXTURE_META = join('duplicate', 'meta');
const META_A = 'DSC07036.JPG';
const META_B = 'DSC07121.JPG';

function loadEnvFromPackage() {
  const envPath = join(__dirname, '..', '.env');
  if (!existsSync(envPath)) return;
  for (const raw of readFileSync(envPath, 'utf8').split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const k = line.slice(0, eq).trim();
    let v = line.slice(eq + 1).trim();
    if ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith("'") && v.endsWith("'"))) v = v.slice(1, -1);
    if (!process.env[k]) process.env[k] = v;
  }
}
loadEnvFromPackage();

/**
 * @param {string} pathAbs
 * @param {string[]} list
 */
function listContainsPathNormalized(pathAbs, list) {
  const a = resolve(pathAbs).toLowerCase();
  return list.some((p) => resolve(String(p)).toLowerCase() === a);
}

/**
 * @param {string} pathAbs
 */
function assertNonEmptyFile(pathAbs) {
  const n = fileByteSize(pathAbs);
  assert(
    n != null && n > 0,
    `expected non-empty file: ${pathAbs} (size ${n})`,
  );
}

function parseDuplicatesJson(r) {
  let j;
  try {
    j = JSON.parse((r.stdout || '').trim() || '{}');
  } catch (e) {
    return { err: (e && e.message) || String(e) };
  }
  return { j };
}

/** Max chars per stream when logging failed CLI runs (override with MEDIA_DUP_TEST_IO_MAX). */
function maxIoLogChars() {
  const raw = process.env.MEDIA_DUP_TEST_IO_MAX;
  if (raw === undefined || raw === '') return 96 * 1024;
  const n = Number.parseInt(String(raw), 10);
  return Number.isFinite(n) && n > 0 ? n : 96 * 1024;
}

/**
 * Print full stderr/stdout so failures are diagnosable (assert() in test-commons does not throw).
 * @param {string} title
 * @param {import('node:child_process').SpawnSyncReturns<string>} r
 * @param {string[]} [hints]
 */
function logSpawnFailure(title, r, hints = []) {
  const cap = maxIoLogChars();
  const trim = (s) => (s && s.length > cap ? `${s.slice(0, cap)}\n… [truncated ${s.length - cap} chars; set MEDIA_DUP_TEST_IO_MAX]` : s || '');
  console.error(`\n  ══ ${title} ══`);
  console.error(`  exit: ${r.status == null ? 'null' : r.status}${r.signal != null ? `  signal: ${r.signal}` : ''}`);
  for (const h of hints) console.error(`  · ${h}`);
  const err = trim(r.stderr);
  const out = trim(r.stdout);
  if (err) {
    console.error('  --- stderr ---');
    console.error(err);
  } else {
    console.error('  (stderr empty)');
  }
  if (out) {
    console.error('  --- stdout ---');
    console.error(out);
  }
  console.error('');
}

const META_DUP_LLM_HINTS = [
  'pm-image returns exit 1 if `find_duplicates` set an error, OR if the run succeeded but `groups` is empty (see src/cli/pm_image_cmd_dup.cpp: `return dr.groups.empty() ? 1 : 0`).',
  'Hard failure: stderr contains `duplicates: ...` (often `meta JSON LLM:   API error:` on the first pair) — network, OpenRouter, bad/expired key, model id, rate limit, or non-JSON model output (src/core/duplicates.cpp).',
  'If stderr shows `response parse error` / `invalid literal` on a backtick, the model returned markdown-fenced or non-JSON text though HTTP succeeded — the worker expects a raw JSON object with similarity_0_10. Use a stricter model or ensure json_object mode is honored.',
  'Soft failure: all pair requests completed but no group met `--min-group` and similarity ≥ `--meta-json-min-sim` (default 7) — look for `finished with 0 group(s)` and per-pair `not linked`.',
  'LLM key: `fill_chat_llm_api_key_from_app_settings` in src/cli (settings.json / providers).',
];

function suiteByNameAndSize() {
  const fixtureDir = join(assetsDir, FIXTURE_SIZE);
  const pathRoot = join(fixtureDir, PAIR_BASENAME);
  const pathOther = join(fixtureDir, 'other', PAIR_BASENAME);

  console.log('\n── CLI: duplicates — by size (same basename, two folders) ──\n');

  if (!existsSync(fixtureDir)) {
    console.log(`  ⏭ skipped: missing fixture directory\n    ${fixtureDir}\n`);
    return;
  }
  if (!existsSync(pathRoot) || !existsSync(pathOther)) {
    console.log(`  ⏭ skipped: need ${PAIR_BASENAME} at fixture root and under other/\n`);
    return;
  }

  const szA = fileByteSize(pathRoot);
  const szB = fileByteSize(pathOther);
  assert(
    szA != null && szA === EXPECTED_SIZE_BYTES,
    `root ${PAIR_BASENAME} is ${EXPECTED_SIZE_BYTES} B (got ${szA})`,
  );
  assert(
    szB != null && szB === EXPECTED_SIZE_BYTES,
    `other/${PAIR_BASENAME} is ${EXPECTED_SIZE_BYTES} B (got ${szB})`,
  );
  assert(szA === szB, 'fixture pair: same byte size');

  const rep = detailReportPaths('size');
  const r = spawnSync(
    EXE,
    [
      'duplicates',
      fixtureDir,
      '--by',
      'size',
      '--json',
      '--report-md',
      rep.md,
      '--report-json',
      rep.json,
    ],
    { env: pmSpawnEnv, encoding: 'utf8', timeout: 120_000, maxBuffer: 20 * 1024 * 1024, windowsHide: true },
  );
  if (r.status !== 0) {
    logSpawnFailure('duplicates --by size', r, [
      'Exit 1 with no `duplicates:` error usually means `groups` was empty (pm_image_cmd_dup).',
    ]);
  }
  assert(
    r.status === 0,
    'duplicates --by size --json: expected exit 0 (full I/O above when non-zero)',
  );
  if (r.status !== 0) return;
  assertNonEmptyFile(rep.md);
  assertNonEmptyFile(rep.json);

  const parsed = parseDuplicatesJson(r);
  if (parsed.err) {
    assert(false, `duplicates JSON parse: ${parsed.err} stdout: ${(r.stdout || '').slice(0, 200)}`);
    return;
  }
  const j = parsed.j;
  const groups = j.groups;
  assert(Array.isArray(groups), 'JSON has groups[]');
  if (!Array.isArray(groups)) return;

  const key = String(EXPECTED_SIZE_BYTES);
  const match = groups.find(
    (g) =>
      g && String(g.key) === key && Array.isArray(g.paths) && g.paths.length >= 2,
  );
  assert(match != null, `JSON includes a size group with key "${key}" and ≥2 paths (got ${groups.length} group(s))`);
  if (match) {
    const paths = match.paths;
    const okA = listContainsPathNormalized(pathRoot, paths);
    const okB = listContainsPathNormalized(pathOther, paths);
    assert(okA, `size group includes root: ${pathRoot}`);
    assert(okB, `size group includes other/: ${pathOther}`);
  }
}

function suiteFingerprintResizedPair() {
  const fixtureDir = join(assetsDir, FIXTURE_FP);
  const pathFull = join(fixtureDir, FP_FULL);
  const pathResized = join(fixtureDir, FP_RESIZED);

  console.log('\n── CLI: duplicates — by fingerprint (original + width-1200) ──\n');

  if (!existsSync(fixtureDir)) {
    console.log(`  ⏭ skipped: missing fixture directory\n    ${fixtureDir}\n`);
    return;
  }
  if (!existsSync(pathFull) || !existsSync(pathResized)) {
    console.log(`  ⏭ skipped: need ${FP_FULL} and ${FP_RESIZED}\n`);
    return;
  }

  const szF = fileByteSize(pathFull);
  const szR = fileByteSize(pathResized);
  assert(szF != null && szF > 0, `${FP_FULL} readable (size ${szF})`);
  assert(szR != null && szR > 0, `${FP_RESIZED} readable (size ${szR})`);
  assert(
    szF !== szR,
    'fingerprint fixture: file sizes differ (resized copy) — size-mode would not group them',
  );

  // Exact dHash (max-hamming 0) does not match across this resize; need tolerance.
  const r0 = spawnSync(
    EXE,
    ['duplicates', fixtureDir, '--by', 'fingerprint', '--max-hamming', '0', '--json'],
    { env: pmSpawnEnv, encoding: 'utf8', timeout: 120_000, maxBuffer: 20 * 1024 * 1024, windowsHide: true },
  );
  const j0 = parseDuplicatesJson(r0).j;
  const noExactDup =
    r0.status === 1 && Array.isArray(j0?.groups) && j0.groups.length === 0;
  assert(
    noExactDup,
    'fingerprint --max-hamming 0 yields no duplicate group (hashes differ after resize)',
  );

  const rep = detailReportPaths('fingerprint');
  const r = spawnSync(
    EXE,
    [
      'duplicates',
      fixtureDir,
      '--by',
      'fingerprint',
      '--max-hamming',
      String(FP_MAX_HAMMING),
      '--json',
      '--report-md',
      rep.md,
      '--report-json',
      rep.json,
    ],
    { env: pmSpawnEnv, encoding: 'utf8', timeout: 120_000, maxBuffer: 20 * 1024 * 1024, windowsHide: true },
  );
  if (r.status !== 0) {
    logSpawnFailure(`duplicates --by fingerprint --max-hamming ${FP_MAX_HAMMING}`, r, [
      'Empty groups → exit 1. Otherwise check stderr for `duplicates:`.',
    ]);
  }
  assert(
    r.status === 0,
    'duplicates fingerprint: expected exit 0 (full I/O above when non-zero)',
  );
  if (r.status !== 0) return;
  assertNonEmptyFile(rep.md);
  assertNonEmptyFile(rep.json);

  const parsed = parseDuplicatesJson(r);
  if (parsed.err) {
    assert(false, `duplicates JSON parse: ${parsed.err}`);
    return;
  }
  const j = parsed.j;
  const groups = j.groups;
  assert(Array.isArray(groups) && groups.length >= 1, 'JSON: ≥1 fingerprint group');
  if (!Array.isArray(groups) || groups.length < 1) return;

  const withBoth = groups.find(
    (g) =>
      g &&
      (g.method === 'fingerprint' || !g.method) &&
      Array.isArray(g.paths) &&
      g.paths.length >= 2 &&
      listContainsPathNormalized(pathFull, g.paths) &&
      listContainsPathNormalized(pathResized, g.paths),
  );
  assert(
    withBoth != null,
    `one group contains both ${basename(pathFull)} and ${basename(pathResized)} (max_hamming=${FP_MAX_HAMMING})`,
  );
}

function suiteMetaJsonLlmCompare() {
  const fixtureDir = join(assetsDir, FIXTURE_META);
  const pathA = join(fixtureDir, META_A);
  const pathB = join(fixtureDir, META_B);
  const jsonA = join(fixtureDir, 'DSC07036.json');
  const jsonB = join(fixtureDir, 'DSC07121.json');

  console.log(
    '\n── CLI: duplicates — by meta + --meta-compare-json-llm (default chat / OpenRouter stack) ──\n',
  );

  if (!existsSync(fixtureDir)) {
    console.log(`  ⏭ skipped: missing fixture directory\n    ${fixtureDir}\n`);
    return;
  }
  if (!existsSync(pathA) || !existsSync(pathB)) {
    console.log(`  ⏭ skipped: need ${META_A} and ${META_B}\n`);
    return;
  }
  if (!existsSync(jsonA) || !existsSync(jsonB)) {
    console.log('  ⏭ skipped: need DSC07036.json and DSC07121.json\n');
    return;
  }

  const rep = detailReportPaths('meta');
  const r = spawnSync(
    EXE,
    [
      'duplicates',
      fixtureDir,
      '--by',
      'meta',
      '--no-exif',
      '--no-md',
      '--meta-compare-json-llm',
      '--meta-json-min-sim',
      '7',
      '--json',
      '--report-md',
      rep.md,
      '--report-json',
      rep.json,
    ],
    { env: pmSpawnEnv, encoding: 'utf8', timeout: 180_000, maxBuffer: 20 * 1024 * 1024, windowsHide: true },
  );
  const errText = `${r.stderr || ''} ${r.stdout || ''}`;
  if (r.status !== 0 && /no API key/i.test(errText)) {
    console.log(
      '  ⏭ skipped: no LLM API key (env, .env, or app settings chat provider on Windows — same resolution as `llm agent`)\n',
    );
    return;
  }
  if (r.status !== 0) {
    logSpawnFailure('duplicates --by meta --meta-compare-json-llm', r, META_DUP_LLM_HINTS);
  }
  assert(
    r.status === 0,
    'duplicates --meta-compare-json-llm: expected exit 0 (full stderr/stdout above when non-zero)',
  );
  if (r.status !== 0) return;
  assertNonEmptyFile(rep.md);
  assertNonEmptyFile(rep.json);

  const parsed = parseDuplicatesJson(r);
  if (parsed.err) {
    assert(false, `duplicates JSON parse: ${parsed.err}`);
    return;
  }
  const j = parsed.j;
  const groups = j.groups;
  assert(Array.isArray(groups) && groups.length >= 1, 'JSON: ≥1 meta group');
  if (!Array.isArray(groups) || groups.length < 1) return;

  const withBoth = groups.find(
    (g) =>
      g &&
      (g.method === 'meta' || !g.method) &&
      Array.isArray(g.paths) &&
      g.paths.length >= 2 &&
      listContainsPathNormalized(pathA, g.paths) &&
      listContainsPathNormalized(pathB, g.paths),
  );
  assert(
    withBoth != null,
    `one meta group contains both ${META_A} and ${META_B} (LLM JSON sidecar similarity ≥ min)`,
  );

  const dm = j.duplicate_map;
  assert(dm && typeof dm === 'object', 'stdout JSON has duplicate_map');
  assert(String(dm.mode) === 'meta', `duplicate_map.mode is meta (got ${dm.mode})`);
  const byPath = dm.by_path;
  assert(byPath && typeof byPath === 'object', 'duplicate_map.by_path object');
  const aNorm = resolve(pathA).toLowerCase();
  const keys = Object.keys(byPath);
  const hitA = keys.find((k) => resolve(String(k)).toLowerCase() === aNorm);
  const entryA = hitA != null ? byPath[hitA] : null;
  assert(
    entryA && typeof entryA === 'object',
    'duplicate_map has entry for first image path',
  );
  const peers = entryA.peers;
  assert(Array.isArray(peers) && peers.length >= 1, 'duplicate_map entry has peers[]');
  const toB = peers.find(
    (p) =>
      p
      && listContainsPathNormalized(pathB, [p.path]),
  );
  assert(
    toB && toB.pairwise && toB.pairwise.method === 'meta cmp:json',
    'peer pairwise.method is meta cmp:json',
  );
  assert(
    toB.pairwise.match_basis != null
      && String(toB.pairwise.match_basis).includes('llm'),
    'peer match_basis names LLM sidecar field compare',
  );

  let repObj;
  try {
    repObj = JSON.parse(readFileSync(rep.json, 'utf8'));
  } catch (e) {
    assert(false, `report JSON parse: ${(e && e.message) || e}`);
    return;
  }
  const mjc = repObj.meta_json_compare;
  assert(
    mjc && typeof mjc === 'object',
    'detailed report has meta_json_compare',
  );
  const pairs = mjc.pairs;
  assert(
    Array.isArray(pairs) && pairs.length >= 1,
    'meta_json_compare.pairs has ≥1 LLM comparison',
  );
  const one = pairs[0];
  assert(
    typeof one.similarity_0_10 === 'number' && one.similarity_0_10 >= 0,
    'pair log includes similarity_0_10',
  );
}

function main() {
  const wallStart = performance.now();
  const startedAtIso = new Date().toISOString();
  const metricsCollector = createMetricsCollector();

  if (!existsSync(EXE)) {
    console.error(`Binary not found: ${EXE}\n  Build: npm run build:release`);
    process.exit(1);
  }

  mkdirSync(DUPLICATE_REPORTS_DIR, { recursive: true });

  console.log(`\nmedia-img duplicate tests\n  binary: ${EXE}\n  assets: ${assetsDir}\n`);
  if (runSizeName && runFingerprint && runMeta) {
    console.log('  mode: all suites\n');
  } else {
    const p = [];
    if (runSizeName) p.push('size-name');
    if (runFingerprint) p.push('fingerprint');
    if (runMeta) p.push('meta');
    console.log(`  mode: ${p.length ? p.join(' + ') : 'none'}\n`);
  }

  const notes = [];
  try {
    if (runSizeName) {
      suiteByNameAndSize();
      notes.push('by-name-size: tests/assets/duplicate/by-name-size');
    }
    if (runFingerprint) {
      suiteFingerprintResizedPair();
      notes.push(
        `fingerprint: tests/assets/duplicate/fingerprint (${FP_FULL} + ${FP_RESIZED}, --max-hamming ${FP_MAX_HAMMING})`,
      );
    }
    if (runMeta) {
      suiteMetaJsonLlmCompare();
      notes.push(
        `meta: tests/assets/duplicate/meta (${META_A} + ${META_B}, --meta-compare-json-llm; skipped if no API key)`,
      );
    }
    if (runSizeName || runFingerprint || runMeta) {
      notes.push(
        'detailed duplicate reports: tests/duplicate-reports/{size|fingerprint|meta}-detail.{md,json} (per suite that ran; `-detail` avoids clobbering a hand-placed meta.json here)',
      );
    }
  } catch (e) {
    console.error(e);
    process.exit(1);
  }

  const wallMs = performance.now() - wallStart;
  const finishedAtIso = new Date().toISOString();
  const failed = stats.failed;
  const passed = stats.passed;
  const ok = failed === 0;
  const metrics = buildMetricsBundle(metricsCollector, startedAtIso, finishedAtIso);

  const allThree = runSizeName && runFingerprint && runMeta;
  const onlySize = runSizeName && !runFingerprint && !runMeta;
  const onlyFp = !runSizeName && runFingerprint && !runMeta;
  const onlyMeta = !runSizeName && !runFingerprint && runMeta;
  const modeLabel = allThree
    ? 'duplicates: all'
    : onlySize
      ? 'duplicates: size-name'
      : onlyFp
        ? 'duplicates: fingerprint'
        : onlyMeta
          ? 'duplicates: meta'
          : 'duplicates: custom';
  const argvStr = process.argv.slice(2).join(' ') || '(default all)';

  mkdirSync(dirname(TEST_REPORT_PATH), { recursive: true });
  const md = renderMarkdownReport({
    ok,
    passed,
    failed,
    meta: {
      cwd: process.cwd(),
      displayName: modeLabel,
      testName: 'test-media-duplicate',
      wallClockMs: wallMs,
      writtenAt: finishedAtIso,
      argv: argvStr,
      exe: EXE,
      assetsDir: assetsDir,
    },
    metrics,
    integrationNotes: notes,
  });
  const acc = isAccumulateMode() && getChapterTitle();
  const footer = acc
    ? `\n---\n*Per-run: \`tests/test-report-last-duplicate.md\` — this run is also a **chapter** in \`tests/test-report-all.md\` (from \`npm run test:all\`).*\n`
    : `\n---\n*Artifact: tests/test-report-last-duplicate.md — overwritten each run.*\n`;
  const full = `${md}${footer}`;
  writeFileSync(TEST_REPORT_PATH, full, 'utf8');
  if (acc) {
    try {
      appendMarkdownChapter(getChapterTitle(), full, { npmScript: getNpmScriptLabel() || undefined });
      console.log(`  (aggregated report chapter: ${TEST_REPORT_ALL_PATH})`);
    } catch (e) {
      console.error('Failed to append chapter to test-report-all.md:', e);
    }
  }

  console.log(`\nPassed: ${passed}  Failed: ${failed}  Wall: ${wallMs.toFixed(0)} ms`);
  console.log(`Report: ${TEST_REPORT_PATH}`);
  if (runSizeName || runFingerprint || runMeta) {
    console.log(`Detail: ${DUPLICATE_REPORTS_DIR} (*-detail.md / *-detail.json per suite)\n`);
  } else {
    console.log('');
  }

  process.exit(failed > 0 ? 1 : 0);
}

main();
