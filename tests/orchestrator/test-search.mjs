/**
 * orchestrator/test-search.mjs
 *
 * CLI integration tests for the `pm-image search` subcommand.
 *
 * Covers:
 *   1.  Name search — literal (case-insensitive default)
 *   2.  Name search — case-sensitive vs case-insensitive
 *   3.  --type=image  filter
 *   4.  --include / --exclude globs
 *   5.  --exclude-dir
 *   6.  --no-recursive
 *   7.  --dry-run
 *   8.  --json output shape
 *   9.  Grep mode (--grep)
 *   10. Context lines (-C N)
 *   11. Regex mode (--regex)
 *   12. --max result limit
 *
 * Fixture root: ./tests  (tests/unit/*.cpp, tests/assets/*.png, tests/functional/*.cpp)
 *
 * Run:
 *   npm run test:search
 *
 * Env:
 *   PM_IMAGE / MEDIA_IMG_EXE  — override binary path (default: dist/pm-image[.exe])
 */

import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { mediaExePath } from './media-presets.js';
import { createAssert } from './test-commons.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const EXE        = mediaExePath(__dirname);
const TESTS_DIR  = resolve(__dirname, '..', 'tests');
const ASSETS_DIR = resolve(TESTS_DIR, 'assets');
const UNIT_DIR   = resolve(TESTS_DIR, 'unit');

const stats = createAssert();
const { assert } = stats;

// ── Helpers ───────────────────────────────────────────────────────────────────

/**
 * Run `pm-image --no-gui search <args…>` synchronously.
 * @param {...string} args
 * @returns {{ stdout: string, stderr: string, status: number }}
 */
function search(...args) {
  const r = spawnSync(EXE, ['--no-gui', 'search', ...args], {
    encoding: 'utf8',
    timeout: 15_000,
  });
  return {
    stdout: r.stdout ?? '',
    stderr: r.stderr ?? '',
    status: r.status ?? -1,
  };
}

function section(label) {
  console.log(`\n── ${label} ──`);
}

function lines(str) {
  return str.trim().split('\n').filter(Boolean);
}

function tryParseJson(str) {
  try { return JSON.parse(str); } catch { return null; }
}

// ── Guards ────────────────────────────────────────────────────────────────────

if (!existsSync(EXE)) {
  console.error(`[test:search] Binary not found: ${EXE}\nRun: npm run build:cpp`);
  process.exit(1);
}
if (!existsSync(TESTS_DIR)) {
  console.error(`[test:search] Tests directory not found: ${TESTS_DIR}`);
  process.exit(1);
}

// ── 1. Name search — literal ──────────────────────────────────────────────────

section('1. Name search — literal');

{
  const r = search(ASSETS_DIR, '-q', 'checker');
  assert(r.status === 0,                   'name/literal: "checker" exits 0');
  assert(r.stdout.includes('checker'),     'name/literal: "checker" → path in stdout');
}

{
  const r = search(ASSETS_DIR, '-q', 'tiny');
  assert(r.status === 0,                   'name/literal: "tiny" exits 0');
  assert(lines(r.stdout).length >= 2,      `name/literal: "tiny" → ≥2 results (got ${lines(r.stdout).length})`);
}

{
  const r = search(TESTS_DIR, '-q', '__nonexistent_xyz_9999__');
  assert(r.status === 1,                   'name/literal: no-match exits 1');
  assert(r.stdout.trim() === '',           'name/literal: no-match → empty stdout');
}

// ── 2. Case sensitivity ───────────────────────────────────────────────────────

section('2. Case sensitivity');

{
  // Default is case-insensitive — uppercase query should hit lowercase filenames.
  const r = search(ASSETS_DIR, '-q', 'CHECKER');
  assert(r.status === 0,                   'case: "CHECKER" case-insensitive exits 0');
  assert(r.stdout.toLowerCase().includes('checker'), 'case: case-insensitive → found');
}

{
  // --case-sensitive: uppercase query vs lowercase filename → no match.
  const r = search(ASSETS_DIR, '-q', 'CHECKER', '--case-sensitive');
  assert(r.status === 1,                   'case: "CHECKER" --case-sensitive exits 1');
}

// ── 3. --type=image ───────────────────────────────────────────────────────────

section('3. --type=image');

{
  const r = search(TESTS_DIR, '-q', 'tiny', '--type', 'image', '--json');
  assert(r.status === 0,                   'type=image: exits 0');
  const arr = tryParseJson(r.stdout);
  assert(Array.isArray(arr),               'type=image: --json → valid JSON array');
  assert(arr !== null && arr.length >= 1,  'type=image: ≥1 result');

  const imageExts = /\.(jpg|jpeg|png|gif|bmp|webp|tiff?|avif|heic|arw|cr[23]|nef|nrw|dng|orf|rw2|raf|pef)$/i;
  const allImages = arr?.every((m) => imageExts.test(m.path));
  assert(allImages === true,               'type=image: every result is an image file');
}

{
  // .cpp files must not appear in type=image results.
  const r = search(TESTS_DIR, '-q', 'test', '--type', 'image', '--json');
  const arr = tryParseJson(r.stdout) ?? [];
  assert(!arr.some((m) => m.path.endsWith('.cpp')), 'type=image: no .cpp files in results');
}

// ── 4. --include / --exclude globs ────────────────────────────────────────────

section('4. --include / --exclude globs');

{
  const r = search(TESTS_DIR, '-q', 'test', '--include', '*.cpp', '--json');
  assert(r.status === 0,                   'include *.cpp: exits 0');
  const arr = tryParseJson(r.stdout) ?? [];
  assert(arr.length >= 1,                  'include *.cpp: ≥1 result');
  assert(arr.every((m) => m.path.endsWith('.cpp')), 'include *.cpp: all paths end in .cpp');
}

{
  // Exclude PNGs from assets — no .png should appear.
  const r = search(ASSETS_DIR, '-q', 'tiny', '--exclude', '*.png', '--json');
  const arr = tryParseJson(r.stdout) ?? [];
  assert(!arr.some((m) => m.path.endsWith('.png')), 'exclude *.png: no .png in results');
}

// ── 5. --exclude-dir ──────────────────────────────────────────────────────────

section('5. --exclude-dir');

{
  // Exclude "unit" → test_*.cpp from tests/unit should not appear.
  const r = search(TESTS_DIR, '-q', 'test', '--include', '*.cpp',
                   '--exclude-dir', 'unit', '--json');
  const arr = tryParseJson(r.stdout) ?? [];
  const hasUnit = arr.some((m) => m.path.replace(/\\/g, '/').includes('/unit/'));
  assert(!hasUnit,                         'exclude-dir unit: no /unit/ paths in results');
}

// ── 6. --no-recursive ─────────────────────────────────────────────────────────

section('6. --no-recursive');

{
  const rRec  = search(TESTS_DIR, '-q', 'test', '--include', '*.cpp');
  const rFlat = search(TESTS_DIR, '-q', 'test', '--include', '*.cpp', '--no-recursive');
  const cRec  = lines(rRec.stdout).length;
  const cFlat = lines(rFlat.stdout).length;
  assert(cFlat <= cRec,                    `no-recursive: flat count (${cFlat}) ≤ recursive (${cRec})`);
}

// ── 7. --dry-run ──────────────────────────────────────────────────────────────

section('7. --dry-run');

{
  const r = search(TESTS_DIR, '-q', 'checker', '--dry-run');
  assert(r.status === 0,                   'dry-run: exits 0');
  assert(r.stdout.trim() === '',           'dry-run: no stdout (matches not printed)');
  assert(r.stderr.includes('Visiting'),    'dry-run: stderr mentions "Visiting"');
}

// ── 8. --json output shape ────────────────────────────────────────────────────

section('8. --json output shape');

{
  const r = search(ASSETS_DIR, '-q', 'square', '--json');
  assert(r.status === 0,                   'json: exits 0');
  const arr = tryParseJson(r.stdout);
  assert(Array.isArray(arr),               'json: stdout is a JSON array');
  assert(arr !== null && arr.length >= 1,  `json: ≥1 match (got ${arr?.length ?? 0})`);

  const first = arr?.[0];
  assert(typeof first?.path   === 'string', 'json[0]: has .path string');
  assert(typeof first?.source === 'string', 'json[0]: has .source string');
  assert(typeof first?.score  === 'number', 'json[0]: has .score number');
}

// ── 9. Grep mode ──────────────────────────────────────────────────────────────

section('9. Grep mode (--grep)');

{
  // Search for "#include" in .cpp files — every C++ file has at least one.
  const r = search(UNIT_DIR, '-q', '#include', '--grep', '--include', '*.cpp');
  assert(r.status === 0,                   'grep: "#include" exits 0');
  assert(r.stdout.includes('#include'),    'grep: preview contains "#include"');
  // Output format: path:line:col: preview
  assert(/:\d+:\d+:/.test(r.stdout),      'grep: output contains :line:col: numbers');
}

{
  const r = search(UNIT_DIR, '-q', '__grep_nope_xyz_9999__', '--grep', '--include', '*.cpp');
  assert(r.status === 1,                   'grep: no-match exits 1');
}

{
  // --names-only: just file paths, no :line:col: decoration.
  const r = search(UNIT_DIR, '-q', '#include', '--grep', '--names-only', '--include', '*.cpp');
  assert(r.status === 0,                   'grep --names-only: exits 0');
  const ls = lines(r.stdout);
  assert(ls.length >= 1,                   'grep --names-only: ≥1 result line');
  // On Windows paths start C:\…; a bare drive colon like C: is fine.
  const hasLineCol = ls.some((l) => /^[^:]+:\d+:\d+:/.test(l));
  assert(!hasLineCol,                      'grep --names-only: no :line:col decoration in output');
}

{
  // --json in grep mode
  const r = search(UNIT_DIR, '-q', '#include', '--grep', '--include', '*.cpp', '--json');
  assert(r.status === 0,                   'grep --json: exits 0');
  const arr = tryParseJson(r.stdout);
  assert(Array.isArray(arr) && arr.length >= 1, 'grep --json: valid non-empty array');
  const first = arr?.[0];
  assert(typeof first?.line === 'number' && first.line >= 1, 'grep --json: .line ≥1');
  assert(typeof first?.preview === 'string',                 'grep --json: has .preview');
  assert(first?.source === 'content',                        'grep --json: .source = "content"');
}

// ── 10. Context lines (-C) ────────────────────────────────────────────────────

section('10. Context lines (-C)');

{
  const r = search(UNIT_DIR, '-q', '#include', '--grep', '--include', '*.cpp', '-C', '2', '--json');
  assert(r.status === 0,                   'context: exits 0');
  const arr = tryParseJson(r.stdout) ?? [];
  assert(arr.length >= 1,                  'context: has results');
  // At least some matches (not the very first line of each file) should have context.
  const hasCtx = arr.some(
    (m) => (m.context_before?.length ?? 0) > 0 || (m.context_after?.length ?? 0) > 0,
  );
  assert(hasCtx,                           'context -C 2: ≥1 match has context_before or context_after');
}

// ── 11. Regex mode (--regex) ──────────────────────────────────────────────────

section('11. Regex mode (--regex)');

{
  // Filenames with digit sequences (e.g. "128x128", "64", "256")
  const r = search(ASSETS_DIR, '-q', '\\d+', '--regex');
  assert(r.status === 0,                   'regex: "\\d+" exits 0');
  assert(lines(r.stdout).length >= 1,      'regex: "\\d+" → ≥1 result');
}

{
  // Anchored: only filenames that START with "checker"
  const r = search(ASSETS_DIR, '-q', '^checker', '--regex');
  assert(r.status === 0,                   'regex: "^checker" exits 0');
  const ls = lines(r.stdout);
  assert(ls.every((p) => /checker/i.test(p)), 'regex: "^checker" → all results contain checker');
}

{
  // Invalid regex pattern → exit 1
  const r = search(TESTS_DIR, '-q', '[invalid(regex', '--regex');
  assert(r.status === 1,                   'regex: invalid pattern exits 1');
  assert(
    r.stderr.toLowerCase().includes('invalid') ||
    r.stderr.toLowerCase().includes('regex')   ||
    r.stderr.toLowerCase().includes('error'),
    'regex: stderr mentions invalid/regex/error',
  );
}

// ── 12. --max limit ───────────────────────────────────────────────────────────

section('12. --max limit');

{
  const rAll = search(TESTS_DIR, '-q', 'test', '--include', '*.cpp');
  const rMax = search(TESTS_DIR, '-q', 'test', '--include', '*.cpp', '--max', '1');
  const cAll = lines(rAll.stdout).length;
  const cMax = lines(rMax.stdout).length;
  assert(cMax <= 1,           `max=1: got ${cMax} results (expected ≤1)`);
  assert(cAll >= cMax,        `max: unrestricted (${cAll}) ≥ limited (${cMax})`);
}

// ── Summary ───────────────────────────────────────────────────────────────────

console.log(`\nDone.  Passed: ${stats.passed}  Failed: ${stats.failed}\n`);
process.exit(stats.failed > 0 ? 1 : 0);
