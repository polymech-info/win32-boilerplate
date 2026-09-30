#!/usr/bin/env node
/**
 * App-use / app-inspect CLI tests for Win32 computer-use helpers.
 *
 * Invoked via:
 *   npm run test:app-use
 */

import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { createAssert } from './test-commons.js';
import { buildMetricsBundle, createMetricsCollector, renderMarkdownReport } from './reports.js';
import {
  appendMarkdownChapter,
  getChapterTitle,
  getNpmScriptLabel,
  isAccumulateMode,
  TEST_REPORT_ALL_PATH,
} from './test-report-accumulate.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = resolve(__dirname, '..', '..');
const TEST_REPORT_PATH = join(PACKAGE_ROOT, 'tests', 'test-report-last.md');

function cliExePath() {
  const env = process.env.PM_IMAGE_CLI || process.env.MEDIA_IMG_CLI_EXE;
  if (env && env.trim()) return resolve(env.trim());
  const name = process.platform === 'win32' ? 'pm-image-cli.exe' : 'pm-image-cli';
  const winX64 = resolve(PACKAGE_ROOT, 'dist', 'win-x64', name);
  if (existsSync(winX64)) return winX64;
  return resolve(PACKAGE_ROOT, 'dist', name);
}

function createTestReport() {
  return {
    meta: {},
    notes: [],
    suites: [],
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
    step(label, ms, detail = '') {
      if (!this._cur) return;
      this._cur.steps.push({ label, ms: Math.round(ms * 100) / 100, detail: detail || '' });
    },
    finalize(stats, wallMs, exe, extra = {}) {
      if (this._cur) this.endSuite();
      this.meta = {
        generatedAt: new Date().toISOString(),
        node: process.version,
        argv: process.argv.slice(2),
        exe,
        passed: stats.passed,
        failed: stats.failed,
        wallClockMs: Math.round(wallMs * 100) / 100,
        ...extra,
      };
    },
  };
}

function writeTestReportFile(rep, metricsCollector, startedAtIso) {
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
      displayName: 'app-use',
      testName: 'app-use',
      writtenAt: finishedAtIso,
      generatedAt: rep.meta.generatedAt,
      exe: rep.meta.exe,
      argv: Array.isArray(rep.meta.argv) ? rep.meta.argv.join(' ') : String(rep.meta.argv ?? ''),
      wallClockMs: rep.meta.wallClockMs,
      workDir: rep.meta.workDir,
    },
    metrics,
    images: [],
    mediaSuites: rep.suites,
    integrationNotes: rep.notes,
  });
  const acc = isAccumulateMode() && getChapterTitle();
  if (acc) {
    md += '\n---\n\n*Per-run: `tests/test-report-last.md` - this run is also a chapter in `tests/test-report-all.md`.*\n';
  } else {
    md += '\n---\n\n*Artifact: `tests/test-report-last.md` - overwritten on each test run.*\n';
  }
  writeFileSync(TEST_REPORT_PATH, md, 'utf8');
  if (acc) {
    appendMarkdownChapter(getChapterTitle(), md, { npmScript: getNpmScriptLabel() || undefined });
    console.log(`  (aggregated report chapter: ${TEST_REPORT_ALL_PATH})`);
  }
}

function runCli(exe, args, timeout = 30_000, env = undefined) {
  const t0 = performance.now();
  const r = spawnSync(exe, args, {
    cwd: PACKAGE_ROOT,
    encoding: 'utf8',
    timeout,
    env,
  });
  return {
    status: r.status ?? -1,
    stdout: r.stdout || '',
    stderr: r.stderr || '',
    ms: performance.now() - t0,
  };
}

function runCliStreaming(exe, args, timeout = 180_000, label = 'agent', cwd = PACKAGE_ROOT, env = undefined) {
  const t0 = performance.now();
  console.log(`\n--- ${label} command ---`);
  console.log(`cwd=${cwd}`);
  console.log(`${JSON.stringify(exe)} ${args.map((arg) => JSON.stringify(arg)).join(' ')}`);
  console.log(`--- ${label} live output ---`);

  return new Promise((resolve) => {
    const child = spawn(exe, args, {
      cwd,
      stdio: ['ignore', 'pipe', 'pipe'],
      windowsHide: false,
      env,
    });
    let stdout = '';
    let stderr = '';
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      try {
        child.kill();
      } catch {
        /* ignore kill errors */
      }
    }, timeout);

    child.stdout.on('data', (chunk) => {
      const text = chunk.toString('utf8');
      stdout += text;
      process.stdout.write(text);
    });
    child.stderr.on('data', (chunk) => {
      const text = chunk.toString('utf8');
      stderr += text;
      process.stderr.write(text);
    });
    child.on('error', (err) => {
      clearTimeout(timer);
      stderr += String(err?.stack || err);
      console.log(`\n--- end ${label} live output ---\n`);
      resolve({
        status: -1,
        stdout,
        stderr,
        ms: performance.now() - t0,
        timedOut,
      });
    });
    child.on('close', (code) => {
      clearTimeout(timer);
      console.log(`\n--- end ${label} live output ---\n`);
      resolve({
        status: timedOut ? -1 : (code ?? -1),
        stdout,
        stderr,
        ms: performance.now() - t0,
        timedOut,
      });
    });
  });
}

function truncateText(text, max = 500) {
  const s = String(text || '');
  return s.length <= max ? s : `${s.slice(0, max)}...`;
}

function parseJson(stdout) {
  const text = String(stdout || '').trim();
  const start = text.indexOf('{');
  if (start < 0) throw new Error(`no JSON object in stdout: ${text.slice(0, 200)}`);
  return JSON.parse(text.slice(start));
}

function cleanAgentArtifacts(files) {
  for (const f of files) {
    try { rmSync(join(PACKAGE_ROOT, f), { force: true }); } catch { /* ignore */ }
  }
}

function readAgentToolCalls(logPath) {
  if (!existsSync(logPath)) return [];
  const events = JSON.parse(readFileSync(logPath, 'utf8')).events || [];
  return events.filter((ev) => ev.kind === 'tool_call').map((ev) => ev.tool);
}

function readAgentResult(logPath) {
  if (!existsSync(logPath)) return null;
  return JSON.parse(readFileSync(logPath, 'utf8')).result || null;
}

function llmAgentDryRunHasConfiguredKey(exe, workDir) {
  const logPath = join(workDir, 'llm-agent-dry-run.json');
  const r = runCli(
    exe,
    ['--cwd', workDir, 'llm', 'agent', '--prompt', 'ping', '--single-turn', '--dry-run', '--log', logPath],
    30_000,
  );
  if (existsSync(logPath)) {
    try {
      const doc = JSON.parse(readFileSync(logPath, 'utf8'));
      return doc?.provider?.api_key_configured === true;
    } catch {
      /* fall through to stdout/stderr heuristic */
    }
  }
  const combined = `${r.stdout}\n${r.stderr}`;
  if (/\|\s*api_key\s*\|\s*set\s*\|/i.test(combined)) return true;
  if (/\|\s*api_key\s*\|\s*\*\*missing\*\*\s*\|/i.test(combined)) return false;
  return false;
}

function chromeExePath() {
  const env = process.env.CHROME_EXE || process.env.GOOGLE_CHROME_EXE;
  if (env && env.trim() && existsSync(env.trim())) return resolve(env.trim());
  const candidates = [
    join(process.env.PROGRAMFILES || 'C:\\Program Files', 'Google', 'Chrome', 'Application', 'chrome.exe'),
    join(process.env['PROGRAMFILES(X86)'] || 'C:\\Program Files (x86)', 'Google', 'Chrome', 'Application', 'chrome.exe'),
    join(process.env.LOCALAPPDATA || '', 'Google', 'Chrome', 'Application', 'chrome.exe'),
  ];
  for (const candidate of candidates) {
    if (candidate && existsSync(candidate)) return candidate;
  }
  const where = spawnSync('where.exe', ['chrome.exe'], { encoding: 'utf8', timeout: 5000 });
  if ((where.status ?? -1) === 0) {
    const first = String(where.stdout || '')
      .split(/\r?\n/)
      .map((line) => line.trim())
      .find((line) => line && existsSync(line));
    if (first) return first;
  }
  return '';
}

function paintExePath() {
  const env = process.env.MSPAINT_EXE || process.env.PAINT_EXE;
  if (env && env.trim() && existsSync(env.trim())) return resolve(env.trim());
  const candidates = [
    join(process.env.SYSTEMROOT || 'C:\\Windows', 'System32', 'mspaint.exe'),
    join(process.env.SYSTEMROOT || 'C:\\Windows', 'mspaint.exe'),
  ];
  for (const candidate of candidates) {
    if (existsSync(candidate)) return candidate;
  }
  const where = spawnSync('where.exe', ['mspaint.exe'], { encoding: 'utf8', timeout: 5000 });
  if ((where.status ?? -1) === 0) {
    const first = String(where.stdout || '')
      .split(/\r?\n/)
      .map((line) => line.trim())
      .find((line) => line && existsSync(line));
    if (first) return first;
  }
  return '';
}

function paintCanvasRect(windowRect) {
  // Win11 Paint layout: ~30 px title + ~110 px ribbon, ~50 px status bar at bottom.
  // Use generous insets so the drag stays inside the actual canvas regardless of DPI.
  const left = windowRect.x + 16;
  const top = windowRect.y + 150;
  const right = windowRect.x + windowRect.w - 16;
  const bottom = windowRect.y + windowRect.h - 60;
  const w = Math.max(0, right - left);
  const h = Math.max(0, bottom - top);
  return { x: left, y: top, w, h };
}

function findPaintWindow(inspectDoc) {
  const wins = Array.isArray(inspectDoc?.windows) ? inspectDoc.windows : [];
  return wins.find((w) => /(^|[^a-z])paint([^a-z]|$)/i.test(String(w?.title || ''))) || wins[0] || null;
}

function wordpadExePath() {
  const env = process.env.WORDPAD_EXE;
  if (env && env.trim() && existsSync(env.trim())) return resolve(env.trim());
  const candidates = [
    join(process.env.PROGRAMFILES || 'C:\\Program Files', 'Windows NT', 'Accessories', 'wordpad.exe'),
    join(process.env['PROGRAMFILES(X86)'] || 'C:\\Program Files (x86)', 'Windows NT', 'Accessories', 'wordpad.exe'),
    join(process.env.SYSTEMROOT || 'C:\\Windows', 'System32', 'write.exe'),
  ];
  for (const c of candidates) if (existsSync(c)) return c;
  const where = spawnSync('where.exe', ['wordpad.exe'], { encoding: 'utf8', timeout: 5000 });
  if ((where.status ?? -1) === 0) {
    const first = String(where.stdout || '')
      .split(/\r?\n/)
      .map((s) => s.trim())
      .find((s) => s && existsSync(s));
    if (first) return first;
  }
  return '';
}

function parseTicTacToeBoard(text) {
  // Only count tokens on the board lines — separated by "|" and/or newlines.
  // This avoids matching X/O/. that appear in unrelated text.
  const s = String(text || '');
  const tokens = [];
  for (let i = 0; i < s.length; ++i) {
    const c = s[i];
    if (c === 'X' || c === 'O' || c === '.') tokens.push(c);
    if (tokens.length === 9) break;
  }
  return tokens;
}

/// Find the WordPad document RichEdit element in a JSON dump and return its value.
/// The font-name combobox is also a RICHEDIT50W with class "RICHEDIT50W", so we
/// also require the element to look like the document body: a value that contains
/// at least one "|" (board separator) OR a large rect (>= 200x200).
function findWordpadDocumentValue(inspectDoc) {
  const wins = Array.isArray(inspectDoc?.windows) ? inspectDoc.windows : [];
  const candidates = [];
  for (const w of wins) {
    for (const e of (w.elements || [])) {
      const cls = String(e.className || '').toUpperCase();
      const ct = String(e.controlType || '');
      const val = String(e.value || '');
      const w1 = e.rect?.w ?? 0;
      const h1 = e.rect?.h ?? 0;
      const isRichEdit = cls.includes('RICHEDIT');
      const isDocLike = (ct === 'Edit' || ct === 'Document' || ct === 'Text') && (w1 >= 200 && h1 >= 200);
      if (!isRichEdit && !isDocLike) continue;
      const hasBoardChars = /\|/.test(val) || /[XO.]/.test(val);
      if (hasBoardChars) candidates.push({ val, area: w1 * h1 });
    }
  }
  if (candidates.length === 0) return '';
  // Pick the element with the largest rect (the document body, not the font combo).
  candidates.sort((a, b) => b.area - a.area);
  return candidates[0].val;
}

/// Read the WordPad body via the JSON dump path. The RichEdit document
/// surfaces its full text through ValuePattern OR TextPattern (read_value
/// falls back from one to the other in app_inspect.cpp).
function readWordpadDocumentText(exe) {
  const r = runCli(exe, ['assistant', 'app-inspect', 'dump', '--title', 'WordPad', '--limit', '200', '--json', '--text-max-chars', '4000'], 15_000);
  if (r.status !== 0) return { text: '', dump: r };
  const doc = parseJson(r.stdout);
  return { text: findWordpadDocumentValue(doc), dump: r, doc };
}

function detectTicTacToeWinner(cells) {
  if (!Array.isArray(cells) || cells.length !== 9) return 'unknown';
  const lines = [
    [0, 1, 2], [3, 4, 5], [6, 7, 8],
    [0, 3, 6], [1, 4, 7], [2, 5, 8],
    [0, 4, 8], [2, 4, 6],
  ];
  for (const [a, b, c] of lines) {
    if (cells[a] !== '.' && cells[a] === cells[b] && cells[b] === cells[c]) return cells[a];
  }
  return cells.every((c) => c !== '.') ? 'draw' : 'ongoing';
}

function libreOfficeExePath() {
  const env = process.env.LIBREOFFICE_EXE || process.env.SOFFICE_EXE;
  if (env && env.trim() && existsSync(env.trim())) return resolve(env.trim());
  const candidates = [
    'C:\\Program Files\\LibreOffice\\program\\soffice.exe',
    'C:\\Program Files (x86)\\LibreOffice\\program\\soffice.exe',
  ];
  for (const candidate of candidates) {
    if (existsSync(candidate)) return candidate;
  }
  const where = spawnSync('where.exe', ['soffice.exe'], { encoding: 'utf8', timeout: 5000 });
  if ((where.status ?? -1) === 0) {
    const first = String(where.stdout || '')
      .split(/\r?\n/)
      .map((line) => line.trim())
      .find((line) => line && existsSync(line));
    if (first) return first;
  }
  return '';
}

async function suiteInspect(rep, assert, exe, workDir) {
  console.log('\n-- App Inspect: dump + screenshot --\n');
  rep.beginSuite('App inspect dump and screenshot');

  const dump = runCli(exe, ['assistant', 'app-inspect', 'dump', '--foreground', '--json']);
  const doc = parseJson(dump.stdout);
  rep.step('foreground app dump', dump.ms, `exit=${dump.status}; windows=${doc.windowCount}`);
  assert(dump.status === 0, `app-inspect dump exits 0 (got ${dump.status}); stderr: ${dump.stderr.slice(0, 300)}`);
  assert(doc.ok === true, 'app-inspect dump reports ok=true');
  assert(Array.isArray(doc.windows), 'app-inspect dump returns windows array');
  assert(doc.windows.length >= 1, 'app-inspect dump sees the foreground window');

  const shot = join(workDir, 'foreground-crop.jpg');
  const shotRun = runCli(exe, [
    'assistant',
    'app-inspect',
    'screenshot',
    '--rect',
    '0,0,160,120',
    '--output',
    shot,
    '--json',
  ]);
  const shotDoc = parseJson(shotRun.stdout);
  rep.step('screen rect jpeg', shotRun.ms, `exit=${shotRun.status}; output=${shot}`);
  assert(shotRun.status === 0, `app-inspect screenshot exits 0 (got ${shotRun.status}); stderr: ${shotRun.stderr.slice(0, 300)}`);
  assert(shotDoc.ok === true, 'app-inspect screenshot reports ok=true');
  assert(existsSync(shot) && statSync(shot).size > 256, 'app-inspect screenshot writes a non-empty JPEG');

  const cursor = runCli(exe, [
    'assistant',
    'app-use',
    'cursor-position',
    '--api-width',
    '1024',
    '--api-height',
    '768',
    '--json',
  ]);
  const cursorDoc = parseJson(cursor.stdout);
  rep.step('cursor position scaled', cursor.ms, `exit=${cursor.status}; x=${cursorDoc.x}; y=${cursorDoc.y}`);
  assert(cursor.status === 0, `app-use cursor-position exits 0 (got ${cursor.status}); stderr: ${cursor.stderr.slice(0, 300)}`);
  assert(cursorDoc.ok === true, 'app-use cursor-position reports ok=true');
  assert(Number.isFinite(cursorDoc.x) && Number.isFinite(cursorDoc.y), 'app-use cursor-position returns screen coordinates');
  assert(Number.isFinite(cursorDoc.apiX) && Number.isFinite(cursorDoc.apiY), 'app-use cursor-position returns scaled API coordinates');

  rep.endSuite();
}

async function suiteNotepadWrite(rep, assert, exe, workDir) {
  console.log('\n-- App Use: Notepad write/save --\n');
  rep.beginSuite('App use Notepad write/save');

  const file = join(workDir, 'app-use-notepad.txt');
  const marker = `app-use marker ${Date.now()}`;
  writeFileSync(file, '', 'utf8');

  const open = runCli(exe, [
    'assistant',
    'app-use',
    'open-app',
    '--exe',
    'notepad.exe',
    '--args',
    file,
    '--wait-ms',
    '3000',
    '--x',
    '40',
    '--y',
    '40',
    '--width',
    '720',
    '--height',
    '420',
    '--json',
  ]);
  const openDoc = parseJson(open.stdout);
  rep.step('open notepad', open.ms, `exit=${open.status}; pid=${openDoc.pid}; hwnd=${openDoc.hwnd}`);
  assert(open.status === 0, `app-use open-app exits 0 (got ${open.status}); stderr: ${open.stderr.slice(0, 300)}`);
  assert(openDoc.ok === true && openDoc.pid > 0, 'app-use open-app returns a process id');

  const type = runCli(exe, ['assistant', 'app-use', 'type', '--text', marker, '--json']);
  rep.step('type marker', type.ms, `exit=${type.status}`);
  assert(type.status === 0, `app-use type exits 0 (got ${type.status}); stderr: ${type.stderr.slice(0, 300)}`);

  const save = runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'ctrl+s', '--json']);
  rep.step('save marker', save.ms, `exit=${save.status}`);
  assert(save.status === 0, `app-use hotkey ctrl+s exits 0 (got ${save.status}); stderr: ${save.stderr.slice(0, 300)}`);

  await new Promise((resolveDelay) => setTimeout(resolveDelay, 600));
  const saved = readFileSync(file, 'utf8');
  assert(saved.includes(marker), 'app-use type + ctrl+s writes marker to the Notepad file');

  const close = runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  rep.step('close notepad', close.ms, `exit=${close.status}`);
  assert(close.status === 0, `app-use hotkey alt+f4 exits 0 (got ${close.status}); stderr: ${close.stderr.slice(0, 300)}`);

  rep.endSuite();
}

async function suiteBatchNotepad(rep, assert, exe, workDir) {
  console.log('\n-- App Use: batch Notepad write/save --\n');
  rep.beginSuite('App use batch Notepad write/save');

  const file = join(workDir, 'app-use-batch-notepad.txt');
  const marker = `app-use batch marker ${Date.now()}`;
  const batchPath = join(workDir, 'app-use.batch.json');
  writeFileSync(file, '', 'utf8');
  writeFileSync(
    batchPath,
    `${JSON.stringify(
      {
        steps: [
          {
            action: 'open-app',
            exe: 'notepad.exe',
            args: file,
            waitMs: 3000,
            x: 70,
            y: 70,
            width: 720,
            height: 420,
            delayMs: 150,
          },
          { action: 'type', text: marker, delayMs: 50 },
          { action: 'hotkey', keys: 'ctrl+s', delayMs: 600 },
          { action: 'hotkey', keys: 'alt+f4', delayMs: 50 },
        ],
      },
      null,
      2,
    )}\n`,
    'utf8',
  );

  const r = runCli(exe, ['assistant', 'app-use', 'batch', '--file', batchPath, '--json'], 45_000);
  const doc = parseJson(r.stdout);
  rep.step('batch notepad write/save', r.ms, `exit=${r.status}; steps=${doc.steps?.length ?? 0}`);
  assert(r.status === 0, `app-use batch exits 0 (got ${r.status}); stderr: ${r.stderr.slice(0, 300)}`);
  assert(doc.ok === true, 'app-use batch reports ok=true');
  assert(Array.isArray(doc.steps) && doc.steps.length === 4, 'app-use batch reports all four steps');
  assert(doc.steps.every((step) => step.ok === true), 'app-use batch marks every step ok');

  const saved = readFileSync(file, 'utf8');
  assert(saved.includes(marker), 'app-use batch writes and saves marker to the Notepad file');

  rep.endSuite();
}

async function suiteChromeAmazon(rep, assert, exe, workDir) {
  console.log('\n-- App Use: open amazon.es in Chrome --\n');
  rep.beginSuite('App use Chrome amazon.es');

  const chrome = chromeExePath();
  if (!chrome) {
    rep.notes.push('Skipped Chrome amazon.es app-use test: chrome.exe was not found.');
    rep.step('chrome amazon.es skipped', 0, 'chrome.exe not found');
    rep.endSuite();
    return;
  }

  const url = 'https://www.amazon.es/';
  const before = runCli(exe, ['assistant', 'app-inspect', 'dump', '--process', 'chrome.exe', '--limit', '20', '--json'], 15_000);
  let beforeDoc = null;
  if (before.status === 0) {
    try {
      beforeDoc = parseJson(before.stdout);
    } catch {
      beforeDoc = null;
    }
  }
  const existingChromeTitle = (beforeDoc?.windows || []).map((w) => String(w.title || '')).find(Boolean);

  if (existingChromeTitle) {
    const batchPath = join(workDir, 'app-use-chrome-amazon.batch.json');
    writeFileSync(
      batchPath,
      `${JSON.stringify(
        {
          steps: [
            { action: 'activate', title: existingChromeTitle, delayMs: 250 },
            { action: 'hotkey', keys: 'ctrl+l', delayMs: 150 },
            { action: 'type', text: url, delayMs: 100 },
            { action: 'hotkey', keys: 'enter', delayMs: 2500 },
          ],
        },
        null,
        2,
      )}\n`,
      'utf8',
    );
    const nav = runCli(exe, ['assistant', 'app-use', 'batch', '--file', batchPath, '--json', '--default-delay-ms', '50'], 30_000);
    const navDoc = parseJson(nav.stdout);
    rep.step('navigate existing chrome amazon.es', nav.ms, `exit=${nav.status}; title=${truncateText(existingChromeTitle, 180)}`);
    assert(nav.status === 0, `app-use batch navigate existing Chrome exits 0 (got ${nav.status}); stderr: ${truncateText(nav.stderr, 300)}`);
    assert(navDoc.ok === true, 'app-use batch navigate existing Chrome reports ok=true');
    assert(Array.isArray(navDoc.steps) && navDoc.steps.every((step) => step.ok === true), 'app-use batch navigate existing Chrome marks every step ok');
  } else {
    const chromeArgs = [
      '--new-window',
      url,
    ].join(' ');

    const open = runCli(exe, [
      'assistant',
      'app-use',
      'open-app',
      '--exe',
      chrome,
      '--args',
      chromeArgs,
      '--wait-ms',
      '5000',
      '--x',
      '120',
      '--y',
      '120',
      '--width',
      '1100',
      '--height',
      '760',
      '--json',
    ], 30_000);
    const openDoc = parseJson(open.stdout);
    rep.step('open chrome amazon.es', open.ms, `exit=${open.status}; pid=${openDoc.pid}; hwnd=${openDoc.hwnd}`);
    assert(open.status === 0, `app-use open-app Chrome exits 0 (got ${open.status}); stderr: ${truncateText(open.stderr, 300)}`);
    assert(openDoc.ok === true && openDoc.pid > 0, 'app-use open-app returns a Chrome process id');
  }

  let inspectDoc = null;
  let inspectRun = null;
  const deadline = Date.now() + 20_000;
  while (Date.now() < deadline) {
    inspectRun = runCli(exe, ['assistant', 'app-inspect', 'dump', '--process', 'chrome.exe', '--limit', '80', '--json'], 15_000);
    if (inspectRun.status === 0) {
      inspectDoc = parseJson(inspectRun.stdout);
      const titles = (inspectDoc.windows || []).map((w) => String(w.title || ''));
      if (titles.some((title) => /amazon/i.test(title))) break;
    }
    await new Promise((resolveDelay) => setTimeout(resolveDelay, 1000));
  }

  const titles = (inspectDoc?.windows || []).map((w) => String(w.title || '')).filter(Boolean);
  rep.step('inspect chrome amazon.es', inspectRun?.ms ?? 0, `exit=${inspectRun?.status}; titles=${truncateText(titles.join(' | '), 300)}`);
  assert(inspectRun?.status === 0, `app-inspect Chrome exits 0 (got ${inspectRun?.status}); stderr: ${truncateText(inspectRun?.stderr, 300)}`);
  assert(inspectDoc?.ok === true, 'app-inspect Chrome reports ok=true');
  assert(titles.some((title) => /amazon/i.test(title)), `Chrome has an Amazon window title (titles=${JSON.stringify(titles)})`);

  const shot = join(workDir, 'chrome-amazon-window.jpg');
  const shotRun = runCli(exe, [
    'assistant',
    'app-inspect',
    'screenshot',
    '--process',
    'chrome.exe',
    '--title',
    'Amazon',
    '--output',
    shot,
    '--json',
  ], 30_000);
  const shotDoc = parseJson(shotRun.stdout);
  rep.step('chrome amazon.es screenshot', shotRun.ms, `exit=${shotRun.status}; output=${shot}`);
  assert(shotRun.status === 0, `app-inspect Chrome screenshot exits 0 (got ${shotRun.status}); stderr: ${truncateText(shotRun.stderr, 300)}`);
  assert(shotDoc.ok === true, 'app-inspect Chrome screenshot reports ok=true');
  assert(existsSync(shot) && statSync(shot).size > 1024, 'app-inspect Chrome screenshot writes a non-empty JPEG');

  const close = runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  rep.step('close chrome amazon.es', close.ms, `exit=${close.status}`);

  rep.endSuite();
}

function findCalculatorResultText(doc) {
  const texts = [];
  for (const w of doc?.windows || []) {
    for (const e of w.elements || []) {
      for (const key of ['name', 'value']) {
        const text = String(e?.[key] || '').trim();
        if (text) texts.push(text);
      }
    }
  }
  return texts.find((text) => /\b100\b/.test(text) || /display\s+is\s+100/i.test(text)) || '';
}

async function suiteCalculatorResize(rep, assert, exe, workDir) {
  console.log('\n-- App Use: Calculator resize drag and 10*10 --\n');
  rep.beginSuite('App use Calculator resize drag');

  const open = runCli(exe, [
    'assistant',
    'app-use',
    'open-app',
    '--exe',
    'calc.exe',
    '--wait-ms',
    '4000',
    '--x',
    '160',
    '--y',
    '120',
    '--width',
    '380',
    '--height',
    '560',
    '--json',
  ], 30_000);
  const openDoc = parseJson(open.stdout);
  rep.step('open calculator', open.ms, `exit=${open.status}; pid=${openDoc.pid}; hwnd=${openDoc.hwnd}`);
  assert(open.status === 0, `app-use open-app Calculator exits 0 (got ${open.status}); stderr: ${truncateText(open.stderr, 300)}`);
  assert(openDoc.ok === true && openDoc.pid > 0, 'app-use open-app returns a Calculator process id');

  const before = runCli(exe, ['assistant', 'app-inspect', 'dump', '--foreground', '--limit', '100', '--json'], 15_000);
  const beforeDoc = parseJson(before.stdout);
  const beforeWin = beforeDoc.windows?.[0];
  const beforeRect = beforeWin?.rect;
  rep.step('inspect calculator before resize', before.ms, `exit=${before.status}; rect=${JSON.stringify(beforeRect)}`);
  assert(before.status === 0, `app-inspect Calculator before resize exits 0 (got ${before.status}); stderr: ${truncateText(before.stderr, 300)}`);
  assert(beforeDoc.ok === true && beforeRect?.w > 0 && beforeRect?.h > 0, 'app-inspect Calculator before resize returns a window rect');

  const batchPath = join(workDir, 'app-use-calculator-resize.batch.json');
  writeFileSync(
    batchPath,
    `${JSON.stringify(
      {
        steps: [
          { action: 'activate', title: beforeWin?.title || 'Calculator', delayMs: 250 },
          {
            action: 'drag',
            x: Math.max(0, beforeRect.x + beforeRect.w - 2),
            y: Math.max(0, beforeRect.y + beforeRect.h - 2),
            dx: 200,
            dy: 200,
            steps: 24,
            durationMs: 450,
            delayMs: 500,
          },
          { action: 'type', text: '10*10', delayMs: 150 },
          { action: 'hotkey', keys: 'enter', delayMs: 800 },
        ],
      },
      null,
      2,
    )}\n`,
    'utf8',
  );

  const batch = runCli(exe, ['assistant', 'app-use', 'batch', '--file', batchPath, '--json', '--default-delay-ms', '50'], 45_000);
  const batchDoc = parseJson(batch.stdout);
  rep.step('resize calculator and enter 10*10', batch.ms, `exit=${batch.status}; steps=${batchDoc.steps?.length ?? 0}`);
  assert(batch.status === 0, `app-use Calculator batch exits 0 (got ${batch.status}); stderr: ${truncateText(batch.stderr, 300)}`);
  assert(batchDoc.ok === true, 'app-use Calculator batch reports ok=true');
  assert(Array.isArray(batchDoc.steps) && batchDoc.steps.every((step) => step.ok === true), 'app-use Calculator batch marks every step ok');

  const after = runCli(exe, ['assistant', 'app-inspect', 'dump', '--foreground', '--limit', '200', '--json'], 15_000);
  const afterDoc = parseJson(after.stdout);
  const afterRect = afterDoc.windows?.[0]?.rect;
  const resultText = findCalculatorResultText(afterDoc);
  rep.step('inspect calculator result', after.ms, `exit=${after.status}; rect=${JSON.stringify(afterRect)}; result=${JSON.stringify(resultText)}`);
  assert(after.status === 0, `app-inspect Calculator result exits 0 (got ${after.status}); stderr: ${truncateText(after.stderr, 300)}`);
  assert(afterDoc.ok === true, 'app-inspect Calculator result reports ok=true');
  assert(afterRect?.w >= beforeRect.w + 120 || afterRect?.h >= beforeRect.h + 120,
    `Calculator window grew after 200px drag (before=${JSON.stringify(beforeRect)}, after=${JSON.stringify(afterRect)})`);
  assert(Boolean(resultText), 'Calculator UIA result includes 100 after entering 10*10=');

  const close = runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  rep.step('close calculator', close.ms, `exit=${close.status}`);

  rep.endSuite();
}

async function suitePaintTicTacToe(rep, assert, exe, workDir) {
  console.log('\n-- App Use: MS Paint tic-tac-toe grid --\n');
  rep.beginSuite('App use Paint tic-tac-toe');

  const mspaint = paintExePath();
  if (!mspaint) {
    rep.notes.push('Skipped Paint tic-tac-toe test: mspaint.exe was not found.');
    rep.step('paint tictactoe skipped', 0, 'mspaint.exe not found');
    rep.endSuite();
    return;
  }

  const open = runCli(exe, [
    'assistant', 'app-use', 'open-app',
    '--exe', mspaint,
    '--wait-ms', '6000',
    '--x', '120', '--y', '80',
    '--width', '960', '--height', '760',
    '--json',
  ], 30_000);
  const openDoc = parseJson(open.stdout);
  rep.step('open mspaint', open.ms, `exit=${open.status}; pid=${openDoc.pid}; hwnd=${openDoc.hwnd}`);
  assert(open.status === 0, `app-use open-app mspaint exits 0 (got ${open.status}); stderr: ${truncateText(open.stderr, 300)}`);
  assert(openDoc.ok === true && openDoc.pid > 0, 'app-use open-app returns a Paint process id');

  // Paint may take a beat to render the ribbon after the window appears.
  await new Promise((r) => setTimeout(r, 1200));

  const before = runCli(exe, ['assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json'], 15_000);
  const beforeDoc = parseJson(before.stdout);
  const paintWin = findPaintWindow(beforeDoc);
  const winRect = paintWin?.rect;
  rep.step('inspect paint window', before.ms, `exit=${before.status}; title=${JSON.stringify(paintWin?.title)}; rect=${JSON.stringify(winRect)}`);
  assert(before.status === 0, `app-inspect Paint exits 0 (got ${before.status}); stderr: ${truncateText(before.stderr, 300)}`);
  assert(beforeDoc.ok === true && winRect?.w > 200 && winRect?.h > 200, `app-inspect returns a Paint window rect (rect=${JSON.stringify(winRect)})`);

  const beforeShot = join(workDir, 'paint-canvas-before.jpg');
  const beforeShotRun = runCli(exe, [
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint',
    '--output', beforeShot,
    '--json',
  ], 15_000);
  rep.step('screenshot paint before', beforeShotRun.ms, `exit=${beforeShotRun.status}`);
  assert(beforeShotRun.status === 0, `app-inspect Paint before screenshot exits 0 (got ${beforeShotRun.status})`);
  assert(existsSync(beforeShot) && statSync(beforeShot).size > 1024, 'paint before-screenshot is written');
  const beforeSize = statSync(beforeShot).size;

  const canvas = paintCanvasRect(winRect);
  assert(canvas.w > 200 && canvas.h > 200, `derived Paint canvas rect is sane (canvas=${JSON.stringify(canvas)})`);

  const v1x = canvas.x + Math.round(canvas.w / 3);
  const v2x = canvas.x + Math.round((2 * canvas.w) / 3);
  const h1y = canvas.y + Math.round(canvas.h / 3);
  const h2y = canvas.y + Math.round((2 * canvas.h) / 3);
  const padX = Math.max(20, Math.round(canvas.w * 0.06));
  const padY = Math.max(20, Math.round(canvas.h * 0.06));
  const dragSteps = 30;
  const dragDur = 500;

  const batchPath = join(workDir, 'app-use-paint-tictactoe.batch.json');
  writeFileSync(
    batchPath,
    `${JSON.stringify(
      {
        steps: [
          { action: 'activate', title: paintWin?.title || 'Paint', delayMs: 400 },
          { action: 'drag', x: v1x, y: canvas.y + padY, dx: 0, dy: canvas.h - 2 * padY, steps: dragSteps, durationMs: dragDur, delayMs: 350 },
          { action: 'drag', x: v2x, y: canvas.y + padY, dx: 0, dy: canvas.h - 2 * padY, steps: dragSteps, durationMs: dragDur, delayMs: 350 },
          { action: 'drag', x: canvas.x + padX, y: h1y, dx: canvas.w - 2 * padX, dy: 0, steps: dragSteps, durationMs: dragDur, delayMs: 350 },
          { action: 'drag', x: canvas.x + padX, y: h2y, dx: canvas.w - 2 * padX, dy: 0, steps: dragSteps, durationMs: dragDur, delayMs: 350 },
        ],
      },
      null,
      2,
    )}\n`,
    'utf8',
  );

  const batch = runCli(exe, ['assistant', 'app-use', 'batch', '--file', batchPath, '--json', '--default-delay-ms', '60'], 60_000);
  const batchDoc = parseJson(batch.stdout);
  rep.step('draw tic-tac-toe grid', batch.ms, `exit=${batch.status}; steps=${batchDoc.steps?.length ?? 0}`);
  assert(batch.status === 0, `app-use Paint batch exits 0 (got ${batch.status}); stderr: ${truncateText(batch.stderr, 300)}`);
  assert(batchDoc.ok === true, 'app-use Paint batch reports ok=true');
  assert(Array.isArray(batchDoc.steps) && batchDoc.steps.length === 5, 'app-use Paint batch reports activate + 4 drag steps');
  assert(batchDoc.steps.every((step) => step.ok === true), 'app-use Paint batch marks every step ok');

  await new Promise((r) => setTimeout(r, 600));

  const afterShot = join(workDir, 'paint-canvas-after.jpg');
  const afterShotRun = runCli(exe, [
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint',
    '--output', afterShot,
    '--json',
  ], 15_000);
  rep.step('screenshot paint after', afterShotRun.ms, `exit=${afterShotRun.status}`);
  assert(afterShotRun.status === 0, `app-inspect Paint after screenshot exits 0 (got ${afterShotRun.status})`);
  assert(existsSync(afterShot) && statSync(afterShot).size > beforeSize,
    `paint after-screenshot grows after drawing (before=${beforeSize}B, after=${existsSync(afterShot) ? statSync(afterShot).size : 0}B)`);

  // Best-effort cleanup: close Paint and dismiss the "Save changes?" prompt.
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  await new Promise((r) => setTimeout(r, 600));
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'n', '--json']);

  rep.endSuite();
}

async function suiteLlmAgentPaintTicTacToe(rep, assert, exe, workDir) {
  console.log('\n-- LLM Agent: open Paint and draw a tic-tac-toe grid --\n');
  rep.beginSuite('LLM agent Paint tic-tac-toe');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped LLM agent Paint test: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('llm agent paint skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped LLM agent Paint test: no configured API key.');
    rep.step('llm agent paint skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }
  const mspaint = paintExePath();
  if (!mspaint) {
    rep.notes.push('Skipped LLM agent Paint test: mspaint.exe not found.');
    rep.step('llm agent paint skipped', 0, 'mspaint.exe not found');
    rep.endSuite();
    return;
  }

  const logName = 'llm-agent-paint-tictactoe.json';
  const logPath = join(PACKAGE_ROOT, logName);
  const beforeShot = 'paint_canvas_before.jpg';
  const afterShot = 'paint_canvas_after.jpg';

  cleanAgentArtifacts([beforeShot, afterShot, logName]);

  // ASCII-only prompt: PowerShell argv round-trips UTF-8 through Windows-1252,
  // so em dashes / smart quotes corrupt to 0x97 etc. and crash the JSON parser.
  //
  // Agent stays inside first-class computer-use tools only:
  //   app_open -> app_inspect_dump -> app_screenshot -> app_batch (4 drags) -> app_screenshot
  // No `run`, no `write_file`, no shell — that was the harness weakness in the
  // earlier baseline (7 rounds, 84k tokens, ~$0.09).
  const prompt = [
    'Use the Windows computer-use tools to draw a 3x3 tic-tac-toe grid in MS Paint: 2 horizontal lines and 2 vertical lines, evenly spaced inside the canvas.',
    'Do NOT use the run, write_file, schedule, or memory tools. Use ONLY: app_open, app_inspect_dump, app_screenshot, app_batch.',
    'Step 1 - app_open {"exe":"mspaint.exe","x":120,"y":80,"width":960,"height":760,"wait_ms":6000}. The result contains rect.{x,y,w,h} of the Paint window.',
    'Step 2 - app_inspect_dump {"title":"Paint","format":"md"}. Use this only to confirm the Paint window is present and read its rect if step 1 did not return one.',
    `Step 3 - app_screenshot {"title":"Paint","output_path":${JSON.stringify(beforeShot)}}.`,
    'Step 4 - compute the drawable canvas inside the Paint window:',
    '   cx = rect.x + 16, cy = rect.y + 150, cw = rect.w - 32, ch = rect.h - 210',
    '   pad = 24 (otherwise mouse-down lands on the ribbon and Paint silently drops the stroke).',
    'Step 5 - call app_batch ONCE with all four strokes inside one tool call (saves rounds and tokens):',
    '   {"default_delay_ms":80,"steps":[',
    '     {"action":"activate","title":"Paint","delayMs":900},',
    '     {"action":"drag","x":cx+pad,"y":cy + ch/3,    "dx":cw - 2*pad,"dy":0,"steps":36,"durationMs":650,"delayMs":400},',
    '     {"action":"drag","x":cx+pad,"y":cy + 2*ch/3,  "dx":cw - 2*pad,"dy":0,"steps":36,"durationMs":650,"delayMs":400},',
    '     {"action":"drag","x":cx + cw/3,  "y":cy+pad,  "dx":0,"dy":ch - 2*pad,"steps":36,"durationMs":650,"delayMs":400},',
    '     {"action":"drag","x":cx + 2*cw/3,"y":cy+pad,  "dx":0,"dy":ch - 2*pad,"steps":36,"durationMs":650,"delayMs":400}',
    '   ]}',
    '   (Substitute real integers for the cx/cy/cw/ch/pad expressions before sending; the batch tool needs concrete numbers, not formulas.)',
    `Step 6 - app_screenshot {"title":"Paint","output_path":${JSON.stringify(afterShot)}}.`,
    'Step 7 - reply with exactly the single word DONE.',
    'Do not close Paint. Horizontals first, then verticals (Paint needs the first stroke to warm up).',
  ].join(' ');

  const r = await runCliStreaming(exe, [
    '--cwd', '.',
    '--log-level', 'trace',
    'llm', 'agent',
    '--prompt', prompt,
    '--single-turn',
    '--max-iter', '12',
    '--log', `./${logName}`,
  ], 240_000, 'llm paint agent', PACKAGE_ROOT);

  const doc = readAgentResult(logPath);
  const toolCalls = readAgentToolCalls(logPath);
  rep.step('llm agent paint tictactoe', r.ms, `exit=${r.status}; ok=${doc?.ok}; tools=${toolCalls.join(',') || '(none)'}`);
  assert(r.status === 0, `llm Paint agent exits 0 (got ${r.status}); stderr: ${truncateText(r.stderr, 800)}`);
  assert(doc?.ok === true, `llm Paint agent JSON ok=true (got ${doc?.ok}); error=${truncateText(doc?.error, 300)}`);
  assert(toolCalls.includes('app_open'), `llm Paint agent used app_open (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('app_batch') || toolCalls.filter((t) => t === 'app_drag').length >= 4,
    `llm Paint agent issued the strokes via app_batch or 4x app_drag (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.filter((t) => t === 'app_screenshot').length >= 2,
    `llm Paint agent took before+after screenshots (tools=${JSON.stringify(toolCalls)})`);
  assert(!toolCalls.includes('run'), `llm Paint agent did NOT shell out via run (tools=${JSON.stringify(toolCalls)})`);
  assert(!toolCalls.includes('write_file'), `llm Paint agent did NOT write_file a batch JSON (tools=${JSON.stringify(toolCalls)})`);
  assert(existsSync(join(PACKAGE_ROOT, beforeShot)), `llm Paint agent created ${beforeShot}`);
  assert(existsSync(join(PACKAGE_ROOT, afterShot)), `llm Paint agent created ${afterShot}`);
  const beforeBytes = statSync(join(PACKAGE_ROOT, beforeShot)).size;
  const afterBytes = statSync(join(PACKAGE_ROOT, afterShot)).size;
  // Stronger than "after > before": four dark strokes on a white JPEG canvas
  // should add several KB. A single stroke or noise alone would not.
  assert(afterBytes > beforeBytes + 3 * 1024,
    `paint after-screenshot grew by at least 3 KB after drawing (before=${beforeBytes}B, after=${afterBytes}B, delta=${afterBytes - beforeBytes}B)`);

  const inspect = runCli(exe, ['assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json'], 15_000);
  const inspectDoc = parseJson(inspect.stdout);
  const titles = (inspectDoc.windows || []).map((w) => String(w.title || '')).filter(Boolean);
  rep.step('verify paint after agent', inspect.ms, `exit=${inspect.status}; titles=${truncateText(titles.join(' | '), 300)}`);
  assert(inspect.status === 0 && inspectDoc.ok === true, 'post-agent Paint inspect succeeds');
  assert(titles.some((t) => /paint/i.test(t)), `post-agent Paint window still present (titles=${JSON.stringify(titles)})`);

  // Best-effort cleanup: close Paint without saving so the next run starts clean.
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  await new Promise((res) => setTimeout(res, 600));
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'n', '--json']);

  rep.endSuite();
}

async function suiteStartMenuTerminalDir(rep, assert, exe, workDir) {
  console.log('\n-- App Use: open terminal from Start menu, maximize, dir --\n');
  rep.beginSuite('App use Start menu terminal dir');

  const termDir = join(workDir, 'terminal-dir');
  mkdirSync(termDir, { recursive: true });
  writeFileSync(join(termDir, 'alpha-start-menu-dir.txt'), 'alpha\n', 'utf8');
  writeFileSync(join(termDir, 'beta-start-menu-dir.txt'), 'beta\n', 'utf8');
  const batchPath = join(workDir, 'app-use-start-terminal.batch.json');
  const cdCommand = `cd /d "${termDir}"`;

  writeFileSync(
    batchPath,
    `${JSON.stringify(
      {
        steps: [
          { action: 'clipboard-set', text: '', delayMs: 50 },
          { action: 'hotkey', keys: 'win', delayMs: 700 },
          { action: 'type', text: 'cmd', delayMs: 150 },
          { action: 'hotkey', keys: 'enter', delayMs: 2200 },
          { action: 'hotkey', keys: 'win+up', delayMs: 700 },
          { action: 'type', text: cdCommand, delayMs: 100 },
          { action: 'hotkey', keys: 'enter', delayMs: 300 },
          { action: 'type', text: 'dir', delayMs: 100 },
          { action: 'hotkey', keys: 'enter', delayMs: 700 },
          { action: 'type', text: 'dir /b | clip', delayMs: 100 },
          { action: 'hotkey', keys: 'enter', delayMs: 900 },
          { action: 'clipboard-get', delayMs: 100 },
        ],
      },
      null,
      2,
    )}\n`,
    'utf8',
  );

  const r = runCli(exe, ['assistant', 'app-use', 'batch', '--file', batchPath, '--json', '--default-delay-ms', '50'], 45_000);
  const doc = parseJson(r.stdout);
  const clipboardText = String(doc.steps?.find((step) => step.action === 'clipboard-get')?.data?.text || '');
  rep.step('start menu terminal dir', r.ms, `exit=${r.status}; clipboard=${JSON.stringify(truncateText(clipboardText, 220))}`);
  assert(r.status === 0, `app-use Start terminal batch exits 0 (got ${r.status}); stderr: ${truncateText(r.stderr, 300)}`);
  assert(doc.ok === true, 'app-use Start terminal batch reports ok=true');
  assert(Array.isArray(doc.steps) && doc.steps.every((step) => step.ok === true), 'app-use Start terminal batch marks every step ok');
  assert(clipboardText.includes('alpha-start-menu-dir.txt'), 'terminal dir result includes alpha fixture');
  assert(clipboardText.includes('beta-start-menu-dir.txt'), 'terminal dir result includes beta fixture');
  assert(clipboardText.split(/\r?\n/).filter((line) => line.trim()).length >= 2, 'terminal dir result is a multi-line list');

  const inspect = runCli(exe, ['assistant', 'app-inspect', 'dump', '--foreground', '--limit', '40', '--json'], 15_000);
  const inspectDoc = parseJson(inspect.stdout);
  const rect = inspectDoc.windows?.[0]?.rect;
  rep.step('inspect maximized terminal', inspect.ms, `exit=${inspect.status}; rect=${JSON.stringify(rect)}`);
  assert(inspect.status === 0, `app-inspect maximized terminal exits 0 (got ${inspect.status}); stderr: ${truncateText(inspect.stderr, 300)}`);
  assert(inspectDoc.ok === true, 'app-inspect maximized terminal reports ok=true');
  assert(rect?.w >= 900 || rect?.h >= 650, `terminal window appears maximized or large (rect=${JSON.stringify(rect)})`);

  const close = runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  rep.step('close terminal', close.ms, `exit=${close.status}`);

  rep.endSuite();
}

async function suiteLlmAgentNotepad(rep, assert, exe, workDir) {
  console.log('\n-- LLM Agent: write something in Notepad --\n');
  rep.beginSuite('LLM agent Notepad computer-use');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped LLM agent Notepad test: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('llm agent notepad skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }

  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push(
      'Skipped LLM agent Notepad test: `llm agent --dry-run` shows no configured API key. Configure chat provider/API key in app settings to run it.',
    );
    rep.step('llm agent notepad skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }

  const fileName = 'app-use-llm-agent-notepad.txt';
  const file = join(PACKAGE_ROOT, fileName);
  const marker = `llm agent nodepad marker ${Date.now()}`;
  const logPath = join(PACKAGE_ROOT, 'llm-agent-notepad.json');
  cleanAgentArtifacts([fileName, 'llm-agent-notepad.json']);
  writeFileSync(file, '', 'utf8');

  const prompt = [
    'Use the Windows computer-use tools to complete this desktop task.',
    `Open Notepad on this file path, relative to the current working directory: ${JSON.stringify(fileName)}.`,
    `Use the run tool only to invoke this CLI executable for app-use actions: ${JSON.stringify(exe)}.`,
    `First open Notepad with exactly this app-use command: ${JSON.stringify(`${exe} assistant app-use open-app --exe notepad.exe --args ${fileName} --wait-ms 3000 --json`)}.`,
    'Do not call notepad.exe directly from the run tool.',
    'After opening Notepad, use app_inspect_dump to observe/confirm the Notepad window before typing.',
    `Type exactly this marker into it: ${JSON.stringify(marker)}.`,
    `Type with this app-use command: ${JSON.stringify(`${exe} assistant app-use type --text ${marker} --json`)}.`,
    `Save with this app-use command: ${JSON.stringify(`${exe} assistant app-use hotkey --keys ctrl+s --json`)}.`,
    'Then stop. Do not close Notepad.',
    'Do not claim success until the file is saved.',
    'Final answer should be exactly DONE after the file is saved.',
  ].join(' ');

  const r = await runCliStreaming(
    exe,
    [
      '--cwd',
      '.',
      '--log-level',
      'trace',
      'llm',
      'agent',
      '--prompt',
      prompt,
      '--single-turn',
      '--max-iter',
      '12',
      '--log',
      './llm-agent-notepad.json',
    ],
    180_000,
    'llm notepad agent',
    PACKAGE_ROOT,
  );

  const doc = readAgentResult(logPath);
  const toolCalls = readAgentToolCalls(logPath);
  rep.step('llm agent write marker', r.ms, `exit=${r.status}; ok=${doc?.ok}; tools=${toolCalls.join(',') || '(none)'}`);
  assert(r.status === 0, `llm agent exits 0 (got ${r.status}); stderr: ${truncateText(r.stderr, 600)}`);
  assert(doc?.ok === true, `llm agent JSON ok=true (got ${doc?.ok}); error=${truncateText(doc?.error, 300)}`);
  assert(toolCalls.includes('run'), `llm agent used run tool for app-use actions (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('app_inspect_dump'), `llm agent used app_inspect_dump to observe Notepad (tools=${JSON.stringify(toolCalls)})`);

  await new Promise((resolveDelay) => setTimeout(resolveDelay, 800));
  const saved = readFileSync(file, 'utf8');
  assert(saved.includes(marker), `llm agent writes and saves marker to Notepad file (saved=${JSON.stringify(truncateText(saved, 300))})`);

  rep.endSuite();
}

async function suiteLlmAgentChromeAmazon(rep, assert, exe, workDir) {
  console.log('\n-- LLM Agent: open/navigate Chrome to amazon.es --\n');
  rep.beginSuite('LLM agent Chrome amazon.es');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped LLM agent Chrome test: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('llm agent chrome skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped LLM agent Chrome test: no configured API key.');
    rep.step('llm agent chrome skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }

  const logPath = join(PACKAGE_ROOT, 'llm-agent-chrome-amazon.json');
  cleanAgentArtifacts(['llm-agent-chrome-amazon.json']);
  const prompt = [
    'Use the Windows computer-use tools to complete this desktop task.',
    'Open or reuse Chrome with the user profile and navigate to https://www.amazon.es/.',
    `Use the run tool with this CLI executable when you need app-use actions: ${JSON.stringify(exe)}.`,
    'Use app_inspect_dump to observe Chrome and confirm an Amazon window/title is visible.',
    'Do not use hardcoded coordinates unless inspection gives you no better option.',
    'Final answer should be exactly DONE after Amazon is open in Chrome.',
  ].join(' ');
  const r = await runCliStreaming(exe, [
    '--cwd', '.',
    '--log-level', 'trace',
    'llm', 'agent',
    '--prompt', prompt,
    '--single-turn',
    '--max-iter', '12',
    '--log', './llm-agent-chrome-amazon.json',
  ], 180_000, 'llm chrome agent', PACKAGE_ROOT);

  const doc = readAgentResult(logPath);
  const toolCalls = readAgentToolCalls(logPath);
  rep.step('llm agent chrome amazon', r.ms, `exit=${r.status}; ok=${doc?.ok}; tools=${toolCalls.join(',') || '(none)'}`);
  assert(r.status === 0, `llm Chrome agent exits 0 (got ${r.status}); stderr: ${truncateText(r.stderr, 800)}`);
  assert(doc?.ok === true, `llm Chrome agent JSON ok=true (got ${doc?.ok}); error=${truncateText(doc?.error, 300)}`);
  assert(toolCalls.includes('run'), `llm Chrome agent used run for app-use actions (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('app_inspect_dump'), `llm Chrome agent used app_inspect_dump (tools=${JSON.stringify(toolCalls)})`);

  const inspect = runCli(exe, ['assistant', 'app-inspect', 'dump', '--process', 'chrome.exe', '--limit', '80', '--json'], 15_000);
  const inspectDoc = parseJson(inspect.stdout);
  const titles = (inspectDoc.windows || []).map((w) => String(w.title || '')).filter(Boolean);
  rep.step('verify chrome amazon after agent', inspect.ms, `exit=${inspect.status}; titles=${truncateText(titles.join(' | '), 300)}`);
  assert(inspect.status === 0 && inspectDoc.ok === true, 'post-agent Chrome inspect succeeds');
  assert(titles.some((title) => /amazon/i.test(title)), `post-agent Chrome title contains Amazon (titles=${JSON.stringify(titles)})`);

  rep.endSuite();
}

async function suiteLlmAgentCalculator(rep, assert, exe, workDir) {
  console.log('\n-- LLM Agent: Calculator resize and 10*10 --\n');
  rep.beginSuite('LLM agent Calculator resize');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped LLM agent Calculator test: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('llm agent calculator skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped LLM agent Calculator test: no configured API key.');
    rep.step('llm agent calculator skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }

  const logPath = join(PACKAGE_ROOT, 'llm-agent-calculator.json');
  const batchName = 'calc-agent.batch.json';
  const cliRel = process.platform === 'win32' ? 'dist\\win-x64\\pm-image-cli.exe' : './dist/pm-image-cli';
  const beforeShot = 'calculator_current.jpg';
  const afterShot = 'calculator_result.jpg';
  cleanAgentArtifacts([batchName, beforeShot, afterShot, 'llm-agent-calculator.json']);
  const prompt = [
    'Use the Windows computer-use tools to complete this desktop task.',
    'Open Calculator, inspect it, resize the Calculator window by dragging its lower-right corner about 200 pixels wider/taller, then enter 10*10 and press Enter.',
    `Use the run tool with this relative CLI executable when you need app-use or batch actions: ${JSON.stringify(cliRel)}.`,
    `Use app_screenshot to save a before screenshot as ${JSON.stringify(beforeShot)} after opening/observing Calculator, and an after screenshot as ${JSON.stringify(afterShot)} after the result is 100.`,
    `You MUST batch the actual UI action sequence: use write_file to create ${JSON.stringify(batchName)} in the current working directory.`,
    'The batch JSON schema is exactly: {"steps":[{"action":"open-app","exe":"calc.exe","waitMs":3000},{"action":"drag","x":<screenX>,"y":<screenY>,"dx":200,"dy":200,"steps":24,"durationMs":450},{"action":"type","text":"10*10"},{"action":"hotkey","keys":"enter"}]}.',
    'Do not use {"version":1,"actions":[...]} and do not use "type" fields; this tool expects steps[] and action.',
    `Then use run to execute exactly this command without quoting the exe: ${JSON.stringify(`${cliRel} assistant app-use batch --file ${batchName} --json --default-delay-ms 50`)}.`,
    'Use app_inspect_dump before creating the final batch to get the Calculator window rectangle for the drag start point, and use app_inspect_dump after the batch to verify the result is 100.',
    'Do not claim success until the Calculator display/result is 100.',
    'Final answer should be exactly DONE after verification.',
  ].join(' ');
  const r = await runCliStreaming(exe, [
    '--cwd', '.',
    '--log-level', 'trace',
    'llm', 'agent',
    '--prompt', prompt,
    '--single-turn',
    '--max-iter', '16',
    '--log', './llm-agent-calculator.json',
  ], 240_000, 'llm calculator agent', PACKAGE_ROOT);

  const doc = readAgentResult(logPath);
  const toolCalls = readAgentToolCalls(logPath);
  rep.step('llm agent calculator resize', r.ms, `exit=${r.status}; ok=${doc?.ok}; tools=${toolCalls.join(',') || '(none)'}`);
  assert(r.status === 0, `llm Calculator agent exits 0 (got ${r.status}); stderr: ${truncateText(r.stderr, 800)}`);
  assert(doc?.ok === true, `llm Calculator agent JSON ok=true (got ${doc?.ok}); error=${truncateText(doc?.error, 300)}`);
  assert(toolCalls.includes('write_file'), `llm Calculator agent wrote a batch JSON file (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('run'), `llm Calculator agent used run for app-use actions (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('app_inspect_dump'), `llm Calculator agent used app_inspect_dump (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('app_screenshot'), `llm Calculator agent captured screenshots (tools=${JSON.stringify(toolCalls)})`);
  assert(existsSync(join(PACKAGE_ROOT, batchName)), `llm Calculator agent created ${batchName}`);
  assert(existsSync(join(PACKAGE_ROOT, beforeShot)), `llm Calculator agent created ${beforeShot}`);
  assert(existsSync(join(PACKAGE_ROOT, afterShot)), `llm Calculator agent created ${afterShot}`);

  const inspect = runCli(exe, ['assistant', 'app-inspect', 'dump', '--foreground', '--limit', '200', '--json'], 15_000);
  const inspectDoc = parseJson(inspect.stdout);
  const resultText = findCalculatorResultText(inspectDoc);
  rep.step('verify calculator after agent', inspect.ms, `exit=${inspect.status}; result=${JSON.stringify(resultText)}`);
  assert(inspect.status === 0 && inspectDoc.ok === true, 'post-agent Calculator inspect succeeds');
  assert(Boolean(resultText), 'post-agent Calculator UIA result includes 100');

  rep.endSuite();
}

// ─────────────────────────────────────────────────────────────────────────────
// Paint tic-tac-toe BATTLE: harness (me) plays X, agent plays O.
// Both draw their marks on the Paint canvas with the new first-class tools.
// Harness uses CLI `app-use batch` for X; agent uses `app_batch` for O.
// ─────────────────────────────────────────────────────────────────────────────

/// Compute the rows*cols cell-center screen coords inside a Paint canvas rect.
/// Cells numbered 1..(rows*cols) left-to-right, top-to-bottom.
function cellCentersGrid(canvas, rows, cols) {
  const cw = canvas.w / cols;
  const ch = canvas.h / rows;
  const centers = [];
  for (let r = 0; r < rows; ++r) {
    for (let c = 0; c < cols; ++c) {
      centers.push({
        x: Math.round(canvas.x + c * cw + cw / 2),
        y: Math.round(canvas.y + r * ch + ch / 2),
      });
    }
  }
  return centers;
}

/// Back-compat for the 3x3 A2A suite.
function cellCentersFromCanvas(canvas) {
  return cellCentersGrid(canvas, 3, 3);
}

/// All k-in-a-row winning lines on a rows*cols board.
/// Returns an array of arrays of cell indices (0-based).
function winningLinesGrid(rows, cols, k) {
  const idx = (r, c) => r * cols + c;
  const lines = [];
  for (let r = 0; r < rows; ++r)
    for (let c = 0; c + k <= cols; ++c)
      lines.push(Array.from({ length: k }, (_, i) => idx(r, c + i)));
  for (let c = 0; c < cols; ++c)
    for (let r = 0; r + k <= rows; ++r)
      lines.push(Array.from({ length: k }, (_, i) => idx(r + i, c)));
  for (let r = 0; r + k <= rows; ++r)
    for (let c = 0; c + k <= cols; ++c) {
      lines.push(Array.from({ length: k }, (_, i) => idx(r + i, c + i)));
      lines.push(Array.from({ length: k }, (_, i) => idx(r + i, c + k - 1 - i)));
    }
  return lines;
}

/// Generalized winner detection on a rows*cols board with k-in-a-row to win.
function detectWinnerGrid(cells, rows, cols, k) {
  const lines = winningLinesGrid(rows, cols, k);
  for (const ln of lines) {
    const v0 = cells[ln[0]];
    if (v0 === '.' || !v0) continue;
    if (ln.every((i) => cells[i] === v0)) return v0;
  }
  return cells.every((c) => c !== '.') ? 'draw' : 'ongoing';
}

/// Render a rows*cols board for the agent prompt, e.g.
///   1[X]  2[.]  3[O]  4[.]
///   5[.]  6[X]  ...
function renderBoardGrid(cells, rows, cols) {
  const out = [];
  for (let r = 0; r < rows; ++r) {
    const line = [];
    for (let c = 0; c < cols; ++c) {
      const idx = r * cols + c;
      const lbl = String(idx + 1).padStart(2, ' ');
      line.push(`${lbl}[${cells[idx]}]`);
    }
    out.push('  ' + line.join('  '));
  }
  return out.join('\\n');
}

/// Build a batch JSON that draws an "X" mark (two diagonals) at a screen point.
/// Used by the HARNESS for its X moves (so we control the visual style).
function xMarkBatch(center, halfSize = 26) {
  const { x, y } = center;
  return {
    steps: [
      { action: 'activate', title: 'Paint', delayMs: 350 },
      { action: 'drag', x: x - halfSize, y: y - halfSize, dx: 2 * halfSize, dy: 2 * halfSize,
        steps: 18, durationMs: 260, delayMs: 220 },
      { action: 'drag', x: x - halfSize, y: y + halfSize, dx: 2 * halfSize, dy: -2 * halfSize,
        steps: 18, durationMs: 260, delayMs: 220 },
    ],
  };
}

/// Run a harness draw via CLI `app-use batch`.
function drawXAtCell(exe, workDir, centers, cellIdx /* 0..8 */) {
  const file = join(workDir, `paint-battle-x-${cellIdx}.batch.json`);
  writeFileSync(file, `${JSON.stringify(xMarkBatch(centers[cellIdx]), null, 2)}\n`, 'utf8');
  return runCli(exe, ['assistant', 'app-use', 'batch', '--file', file, '--json', '--default-delay-ms', '60'], 30_000);
}

/// Pick the X move using a classic tic-tac-toe heuristic:
/// 1) win, 2) block, 3) take center, 4) take a corner, 5) take a side.
/// `cells` is 9 chars, '.' / 'X' / 'O'. Returns 0..8 or -1 if no empty cells.
function pickXMove(cells) {
  const lines = [
    [0,1,2],[3,4,5],[6,7,8],
    [0,3,6],[1,4,7],[2,5,8],
    [0,4,8],[2,4,6],
  ];
  const finishLine = (mark) => {
    for (const [a,b,c] of lines) {
      const trio = [cells[a], cells[b], cells[c]];
      const empty = [a,b,c].find((i, k) => trio[k] === '.');
      if (empty !== undefined && trio.filter((v) => v === mark).length === 2 && trio.filter((v) => v === '.').length === 1) {
        return empty;
      }
    }
    return -1;
  };
  const win = finishLine('X'); if (win >= 0) return win;
  const block = finishLine('O'); if (block >= 0) return block;
  if (cells[4] === '.') return 4;
  for (const c of [0, 2, 6, 8]) if (cells[c] === '.') return c;
  for (const c of [1, 3, 5, 7]) if (cells[c] === '.') return c;
  return cells.findIndex((c) => c === '.');
}

function renderBoardForPrompt(cells) {
  const c = (i) => cells[i] === '.' ? '.' : cells[i];
  return [
    `  1[${c(0)}]  2[${c(1)}]  3[${c(2)}]`,
    `  4[${c(3)}]  5[${c(4)}]  6[${c(5)}]`,
    `  7[${c(6)}]  8[${c(7)}]  9[${c(8)}]`,
  ].join('\\n');
}

function parseOMove(text) {
  return parseAgentMove(text, 'O');
}

function parseAgentMove(text, mark) {
  // Strict: only match the agreed-on "PLAYED <mark> AT CELL N" envelope.
  // The previous \bCELL\s+(\d+) fallback matched stale cell mentions
  // inside agent-rambled analysis text (e.g. "O(16)" → 16) and produced
  // false-positive moves on cells the opponent already owned.
  const up = String(text || '').toUpperCase();
  const m = up.match(new RegExp(`PLAYED\\s+${mark}\\s+AT\\s+CELL\\s+(\\d+)`));
  if (m) return parseInt(m[1], 10);
  return -1;
}

async function suiteLlmAgentPaintTicTacToeBattle(rep, assert, exe, workDir) {
  console.log('\n-- LLM Agent: Paint tic-tac-toe BATTLE (harness X vs agent O) --\n');
  rep.beginSuite('LLM agent Paint tic-tac-toe battle');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped Paint battle: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('paint battle skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped Paint battle: no configured API key.');
    rep.step('paint battle skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }
  const mspaint = paintExePath();
  if (!mspaint) {
    rep.notes.push('Skipped Paint battle: mspaint.exe not found.');
    rep.step('paint battle skipped', 0, 'mspaint.exe not found');
    rep.endSuite();
    return;
  }

  const beforeShot = 'paint_battle_before.jpg';
  const afterShot = 'paint_battle_after.jpg';
  // Per-turn agent log files, so failures stay attributable.
  cleanAgentArtifacts([
    beforeShot, afterShot,
    'llm-agent-paint-battle-turn1.json',
    'llm-agent-paint-battle-turn2.json',
    'llm-agent-paint-battle-turn3.json',
    'llm-agent-paint-battle-turn4.json',
    'llm-agent-paint-battle-turn5.json',
  ]);

  // ── 1. Open Paint, draw the grid using CLI batch (proven path) ───────────
  const open = runCli(exe, [
    'assistant', 'app-use', 'open-app',
    '--exe', mspaint, '--wait-ms', '6000',
    '--x', '120', '--y', '80', '--width', '960', '--height', '760',
    '--json',
  ], 30_000);
  const openDoc = parseJson(open.stdout);
  rep.step('open Paint', open.ms, `exit=${open.status}; pid=${openDoc.pid}`);
  assert(open.status === 0 && openDoc.ok === true && openDoc.pid > 0, 'Paint launches');

  await new Promise((r) => setTimeout(r, 1200));

  const inspect = runCli(exe, ['assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json'], 15_000);
  const inspectDoc = parseJson(inspect.stdout);
  const paintWin = findPaintWindow(inspectDoc);
  const winRect = paintWin?.rect;
  rep.step('locate Paint', inspect.ms, `title=${JSON.stringify(paintWin?.title)}; rect=${JSON.stringify(winRect)}`);
  assert(winRect?.w > 200 && winRect?.h > 200, `Paint window rect resolved (${JSON.stringify(winRect)})`);
  const canvas = paintCanvasRect(winRect);
  const centers = cellCentersFromCanvas(canvas);

  // Empty-canvas baseline for byte-growth comparison.
  runCli(exe, [
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint',
    '--output', join(PACKAGE_ROOT, beforeShot),
    '--json',
  ], 15_000);
  const beforeBytes = existsSync(join(PACKAGE_ROOT, beforeShot)) ? statSync(join(PACKAGE_ROOT, beforeShot)).size : 0;
  assert(beforeBytes > 1024, `empty Paint screenshot saved (${beforeBytes}B)`);

  // Draw the grid (4 lines), reusing the harness batch path.
  const padX = Math.max(20, Math.round(canvas.w * 0.06));
  const padY = Math.max(20, Math.round(canvas.h * 0.06));
  const gridBatchFile = join(workDir, 'paint-battle-grid.batch.json');
  writeFileSync(gridBatchFile, `${JSON.stringify({
    steps: [
      { action: 'activate', title: paintWin?.title || 'Paint', delayMs: 500 },
      { action: 'drag', x: canvas.x + padX, y: canvas.y + Math.round(canvas.h / 3),     dx: canvas.w - 2 * padX, dy: 0, steps: 30, durationMs: 500, delayMs: 350 },
      { action: 'drag', x: canvas.x + padX, y: canvas.y + Math.round(2 * canvas.h / 3), dx: canvas.w - 2 * padX, dy: 0, steps: 30, durationMs: 500, delayMs: 350 },
      { action: 'drag', x: canvas.x + Math.round(canvas.w / 3),     y: canvas.y + padY, dx: 0, dy: canvas.h - 2 * padY, steps: 30, durationMs: 500, delayMs: 350 },
      { action: 'drag', x: canvas.x + Math.round(2 * canvas.w / 3), y: canvas.y + padY, dx: 0, dy: canvas.h - 2 * padY, steps: 30, durationMs: 500, delayMs: 350 },
    ],
  }, null, 2)}\n`, 'utf8');
  const grid = runCli(exe, ['assistant', 'app-use', 'batch', '--file', gridBatchFile, '--json', '--default-delay-ms', '60'], 60_000);
  const gridDoc = parseJson(grid.stdout);
  rep.step('draw grid', grid.ms, `exit=${grid.status}; steps=${gridDoc.steps?.length ?? 0}`);
  assert(grid.status === 0 && gridDoc.ok === true, 'grid drawn ok');

  // ── 2. Harness opens with X at center (cell 5, index 4). ─────────────────
  const cells = ['.','.','.','.','.','.','.','.','.'];
  const xRun1 = drawXAtCell(exe, workDir, centers, 4);
  rep.step('harness X opens at 5', xRun1.ms, `exit=${xRun1.status}`);
  assert(xRun1.status === 0, `first X drawn (exit=${xRun1.status}; stderr=${truncateText(xRun1.stderr, 200)})`);
  cells[4] = 'X';

  await new Promise((r) => setTimeout(r, 400));

  // ── 3. Game loop: agent plays O, harness plays X, until terminal. ────────
  const tokenLines = [];
  let agentTurn = 0;
  let cumulativeBytes = beforeBytes;
  let totalCost = 0;
  let totalRounds = 0;

  while (true) {
    const w1 = detectTicTacToeWinner(cells.map((c) => c));
    if (w1 !== 'ongoing') break;

    agentTurn++;
    const logName = `llm-agent-paint-battle-turn${agentTurn}.json`;
    const logPath = join(PACKAGE_ROOT, logName);
    const emptyIndices = cells.map((c, i) => c === '.' ? i + 1 : null).filter((v) => v !== null);

    // ASCII-only prompt; PowerShell mangles non-ASCII.
    // Encode all cell centers + which cells are empty + which mark to draw.
    const centersLine = centers.map((p, i) => `${i + 1}=(${p.x},${p.y})`).join(', ');
    const boardText = renderBoardForPrompt(cells);
    const prompt = [
      `You are O in a tic-tac-toe battle in MS Paint. The harness draws X, you draw O. Turn ${agentTurn}.`,
      'Current board (1-9 keypad order; . = empty):',
      `  ${boardText}`,
      `Empty cells you may choose from: ${JSON.stringify(emptyIndices)}.`,
      `Cell-center screen coordinates (Paint canvas, absolute px): ${centersLine}.`,
      'Pick ONE empty cell N and draw a "+" mark (two short strokes that cross at the cell center) using exactly ONE app_batch tool call:',
      '  { "default_delay_ms": 80, "steps": [',
      '    { "action": "activate", "title": "Paint", "delayMs": 350 },',
      '    { "action": "drag", "x": cx-22, "y": cy,    "dx": 44, "dy": 0,  "steps": 14, "duration_ms": 200, "delayMs": 220 },',
      '    { "action": "drag", "x": cx,    "y": cy-22, "dx": 0,  "dy": 44, "steps": 14, "duration_ms": 200, "delayMs": 220 }',
      '  ] }',
      'Substitute REAL integers from the cell-center list above for cx and cy; the batch tool wants concrete numbers, not formulas. Use button left (default).',
      'Strategy hint: if you can win this turn, do it; else block X if X threatens to win; else take center, then a corner, then a side.',
      'After the app_batch call, reply with EXACTLY one line of plain text and nothing else: "PLAYED O AT CELL N" (N = the cell you chose).',
      'Use ONLY the app_batch tool. Do NOT call run, write_file, schedule, or any other tool. Do NOT close Paint.',
    ].join(' ');

    const r = await runCliStreaming(exe, [
      '--cwd', '.',
      '--log-level', 'trace',
      'llm', 'agent',
      '--prompt', prompt,
      '--single-turn',
      '--max-iter', '6',
      '--log', `./${logName}`,
    ], 180_000, `llm paint battle turn ${agentTurn}`, PACKAGE_ROOT);

    const doc = readAgentResult(logPath);
    const toolCalls = readAgentToolCalls(logPath);
    const replyText = String(doc?.final_text || '');
    const chosen = parseOMove(replyText);

    // Cost / rounds bookkeeping (best-effort: parse from log)
    const cost = Number(doc?.llm_usage?.cost ?? 0);
    const rounds = Number(doc?.iterations ?? 0);
    const completionTokens = Number(doc?.llm_usage?.completion_tokens ?? 0);
    const inputTokens = (doc?.llm_usage?.llm_rounds ?? []).reduce((sum, rd) => sum + (rd?.usage?.input_tokens ?? 0), 0);
    if (Number.isFinite(cost)) totalCost += cost;
    if (Number.isFinite(rounds)) totalRounds += rounds;
    tokenLines.push(`turn ${agentTurn}: rounds=${rounds}; in=${inputTokens}; out=${completionTokens}; cost=$${cost.toFixed(4)}`);

    rep.step(`agent O turn ${agentTurn}`, r.ms,
      `exit=${r.status}; rounds=${rounds}; cost=$${cost.toFixed(4)}; chose=${chosen}; reply=${JSON.stringify(truncateText(replyText, 80))}; tools=${toolCalls.join(',') || '(none)'}`);
    assert(r.status === 0, `agent O turn ${agentTurn} CLI exits 0 (got ${r.status})`);
    assert(doc, `agent O turn ${agentTurn} produced a result log`);
    assert(toolCalls.includes('app_batch') || toolCalls.filter((t) => t === 'app_drag').length >= 2,
      `agent O turn ${agentTurn} used app_batch / app_drag (tools=${JSON.stringify(toolCalls)})`);
    assert(!toolCalls.includes('run') && !toolCalls.includes('write_file'),
      `agent O turn ${agentTurn} avoided run/write_file (tools=${JSON.stringify(toolCalls)})`);
    assert(chosen >= 1 && chosen <= 9, `agent O turn ${agentTurn} reply names a cell (reply=${JSON.stringify(replyText)})`);
    assert(cells[chosen - 1] === '.', `agent O turn ${agentTurn} chose empty cell (cell ${chosen}, board=${cells.join('')})`);

    cells[chosen - 1] = 'O';

    // Visual proof: take a quick screenshot and verify byte growth (each
    // mark adds ~1-2 KB to a JPEG of a mostly-white canvas).
    const probeShot = join(workDir, `paint-battle-after-o${agentTurn}.jpg`);
    runCli(exe, [
      'assistant', 'app-inspect', 'screenshot',
      '--title', paintWin?.title || 'Paint',
      '--output', probeShot,
      '--json',
    ], 15_000);
    const probeBytes = existsSync(probeShot) ? statSync(probeShot).size : 0;
    assert(probeBytes > cumulativeBytes + 600,
      `agent O move left visible ink on the canvas (was=${cumulativeBytes}B, now=${probeBytes}B, +${probeBytes - cumulativeBytes}B)`);
    cumulativeBytes = probeBytes;

    const w2 = detectTicTacToeWinner(cells.map((c) => c));
    if (w2 !== 'ongoing') break;

    // ── Harness X turn ────────────────────────────────────────────────────
    const xMove = pickXMove(cells);
    assert(xMove >= 0, 'harness picks a valid X cell');
    const xRun = drawXAtCell(exe, workDir, centers, xMove);
    rep.step(`harness X plays cell ${xMove + 1}`, xRun.ms, `exit=${xRun.status}`);
    assert(xRun.status === 0, `harness X drew (exit=${xRun.status}; stderr=${truncateText(xRun.stderr, 200)})`);
    cells[xMove] = 'X';
    await new Promise((r) => setTimeout(r, 300));
  }

  // ── 4. Final verification ────────────────────────────────────────────────
  const finalWinner = detectTicTacToeWinner(cells.map((c) => c));
  const xCount = cells.filter((c) => c === 'X').length;
  const oCount = cells.filter((c) => c === 'O').length;

  runCli(exe, [
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint',
    '--output', join(PACKAGE_ROOT, afterShot),
    '--json',
  ], 15_000);
  const afterBytes = existsSync(join(PACKAGE_ROOT, afterShot)) ? statSync(join(PACKAGE_ROOT, afterShot)).size : 0;
  const totalMarks = xCount + oCount;

  rep.step('battle final state', 0,
    `cells=${cells.join('')}; X=${xCount}; O=${oCount}; winner=${finalWinner}; ` +
    `agent_turns=${agentTurn}; agent_cost=${totalCost.toFixed(4)}; agent_rounds=${totalRounds}; ` +
    `screenshot_delta=${afterBytes - beforeBytes}B`);

  assert(['X', 'O', 'draw'].includes(finalWinner),
    `battle reached a terminal state (winner=${finalWinner}, cells=${cells.join('')})`);
  assert(xCount === oCount + 1, `X plays first, so X count is O count + 1 (X=${xCount}, O=${oCount})`);
  assert(totalMarks >= 5, `at least 5 marks placed (X+O=${totalMarks})`);
  assert(afterBytes > beforeBytes + Math.max(3 * 1024, totalMarks * 600),
    `final screenshot grew enough for ${totalMarks} marks (before=${beforeBytes}B, after=${afterBytes}B, delta=${afterBytes - beforeBytes}B)`);

  // tokenLines diagnostic dump
  for (const l of tokenLines) rep.notes.push(l);

  // Best-effort cleanup.
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  await new Promise((r) => setTimeout(r, 600));
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'n', '--json']);

  rep.endSuite();
}

// ─────────────────────────────────────────────────────────────────────────────
// Paint tic-tac-toe A2A (agent vs agent): two independent LLM agents play
// against each other. Harness is only a referee — opens Paint, draws the grid,
// alternates X/O agent invocations, validates each move, captures the final
// screenshot to ./tests/battle.jpg.
//
// Each agent invocation:
//   - is stateless (single-turn, fresh prompt with current board)
//   - is told its role (X or O) and the mark style to draw
//   - issues ONE app_batch call to draw + replies "PLAYED <mark> AT CELL N"
//
// Why two different mark styles?
//   - X agent draws an "X" (two diagonals)
//   - O agent draws a "+" (horizontal + vertical)
//   - The final screenshot is visually readable; the harness ledger is the
//     source of truth for move validation.
// ─────────────────────────────────────────────────────────────────────────────
async function suiteLlmAgentPaintTicTacToeA2A(rep, assert, exe, workDir) {
  console.log('\n-- LLM A2A: agent X vs agent O in MS Paint --\n');
  rep.beginSuite('LLM A2A Paint tic-tac-toe');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped Paint A2A: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('paint a2a skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped Paint A2A: no configured API key.');
    rep.step('paint a2a skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }
  const mspaint = paintExePath();
  if (!mspaint) {
    rep.notes.push('Skipped Paint A2A: mspaint.exe not found.');
    rep.step('paint a2a skipped', 0, 'mspaint.exe not found');
    rep.endSuite();
    return;
  }

  const finalShotRel = 'tests/battle.jpg';
  const finalShotAbs = join(PACKAGE_ROOT, 'tests', 'battle.jpg');
  mkdirSync(join(PACKAGE_ROOT, 'tests'), { recursive: true });

  const turnLogNames = Array.from({ length: 9 }, (_, i) => `llm-agent-paint-a2a-turn${i + 1}.json`);
  cleanAgentArtifacts([finalShotRel, ...turnLogNames]);

  // ── Open Paint and draw the empty grid via CLI batch (proven path) ──────
  const open = runCli(exe, [
    'assistant', 'app-use', 'open-app',
    '--exe', mspaint, '--wait-ms', '6000',
    '--x', '120', '--y', '80', '--width', '960', '--height', '760',
    '--json',
  ], 30_000);
  const openDoc = parseJson(open.stdout);
  rep.step('open Paint', open.ms, `exit=${open.status}; pid=${openDoc.pid}`);
  assert(open.status === 0 && openDoc.ok === true && openDoc.pid > 0, 'Paint launches');

  await new Promise((r) => setTimeout(r, 1200));

  const inspect = runCli(exe, ['assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json'], 15_000);
  const inspectDoc = parseJson(inspect.stdout);
  const paintWin = findPaintWindow(inspectDoc);
  const winRect = paintWin?.rect;
  assert(winRect?.w > 200 && winRect?.h > 200, `Paint window rect resolved (${JSON.stringify(winRect)})`);
  const canvas = paintCanvasRect(winRect);
  const centers = cellCentersFromCanvas(canvas);

  const padX = Math.max(20, Math.round(canvas.w * 0.06));
  const padY = Math.max(20, Math.round(canvas.h * 0.06));
  const gridBatchFile = join(workDir, 'paint-a2a-grid.batch.json');
  writeFileSync(gridBatchFile, `${JSON.stringify({
    steps: [
      { action: 'activate', title: paintWin?.title || 'Paint', delayMs: 500 },
      { action: 'drag', x: canvas.x + padX, y: canvas.y + Math.round(canvas.h / 3),     dx: canvas.w - 2 * padX, dy: 0, steps: 30, durationMs: 500, delayMs: 350 },
      { action: 'drag', x: canvas.x + padX, y: canvas.y + Math.round(2 * canvas.h / 3), dx: canvas.w - 2 * padX, dy: 0, steps: 30, durationMs: 500, delayMs: 350 },
      { action: 'drag', x: canvas.x + Math.round(canvas.w / 3),     y: canvas.y + padY, dx: 0, dy: canvas.h - 2 * padY, steps: 30, durationMs: 500, delayMs: 350 },
      { action: 'drag', x: canvas.x + Math.round(2 * canvas.w / 3), y: canvas.y + padY, dx: 0, dy: canvas.h - 2 * padY, steps: 30, durationMs: 500, delayMs: 350 },
    ],
  }, null, 2)}\n`, 'utf8');
  const grid = runCli(exe, ['assistant', 'app-use', 'batch', '--file', gridBatchFile, '--json', '--default-delay-ms', '60'], 60_000);
  const gridDoc = parseJson(grid.stdout);
  rep.step('draw grid', grid.ms, `exit=${grid.status}; steps=${gridDoc.steps?.length ?? 0}`);
  assert(grid.status === 0 && gridDoc.ok === true, 'grid drawn ok');

  // Baseline for byte-growth comparison.
  const baselineShot = join(workDir, 'paint-a2a-baseline.jpg');
  runCli(exe, [
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint',
    '--output', baselineShot,
    '--json',
  ], 15_000);
  const baselineBytes = existsSync(baselineShot) ? statSync(baselineShot).size : 0;
  assert(baselineBytes > 1024, `empty-grid baseline screenshot saved (${baselineBytes}B)`);

  // ── A2A loop ────────────────────────────────────────────────────────────
  const cells = ['.','.','.','.','.','.','.','.','.'];
  let turnNum = 0;
  let totalCost = 0;
  let totalRounds = 0;
  let cumulativeBytes = baselineBytes;
  const tokenLines = [];

  while (true) {
    const winnerSoFar = detectTicTacToeWinner(cells.map((c) => c));
    if (winnerSoFar !== 'ongoing') break;
    if (turnNum >= 9) break; // safety

    turnNum++;
    const xCount = cells.filter((c) => c === 'X').length;
    const oCount = cells.filter((c) => c === 'O').length;
    const mark = (xCount === oCount) ? 'X' : 'O';
    const opponent = mark === 'X' ? 'O' : 'X';

    const markStyle = mark === 'X'
      ? 'Draw an X using two DIAGONAL strokes that cross at the cell center. Stroke 1: top-left to bottom-right of the cell. Stroke 2: bottom-left to top-right.'
      : 'Draw an O using two PERPENDICULAR strokes that cross at the cell center (a "+" shape). Stroke 1: horizontal. Stroke 2: vertical.';

    const halfSize = 22;
    const exampleSteps = mark === 'X' ? [
      `    { "action": "drag", "x": cx-${halfSize}, "y": cy-${halfSize}, "dx": ${2 * halfSize}, "dy": ${2 * halfSize}, "steps": 16, "duration_ms": 220, "delayMs": 220 },`,
      `    { "action": "drag", "x": cx-${halfSize}, "y": cy+${halfSize}, "dx": ${2 * halfSize}, "dy": -${2 * halfSize}, "steps": 16, "duration_ms": 220, "delayMs": 220 }`,
    ] : [
      `    { "action": "drag", "x": cx-${halfSize}, "y": cy,    "dx": ${2 * halfSize}, "dy": 0,  "steps": 14, "duration_ms": 200, "delayMs": 220 },`,
      `    { "action": "drag", "x": cx,    "y": cy-${halfSize}, "dx": 0,  "dy": ${2 * halfSize}, "steps": 14, "duration_ms": 200, "delayMs": 220 }`,
    ];

    const logName = turnLogNames[turnNum - 1];
    const logPath = join(PACKAGE_ROOT, logName);
    const emptyIndices = cells.map((c, i) => c === '.' ? i + 1 : null).filter((v) => v !== null);
    const centersLine = centers.map((p, i) => `${i + 1}=(${p.x},${p.y})`).join(', ');
    const boardText = renderBoardForPrompt(cells);

    const prompt = [
      `You are AGENT ${mark} in an agent-vs-agent tic-tac-toe match in MS Paint. Your opponent is AGENT ${opponent}. Turn ${turnNum} of the match.`,
      'Current board (1-9 keypad order; . = empty):',
      `  ${boardText}`,
      `Empty cells you may choose from: ${JSON.stringify(emptyIndices)}.`,
      `Cell-center screen coordinates (Paint canvas, absolute px): ${centersLine}.`,
      `Your mark style: ${markStyle}`,
      `Pick ONE empty cell N. Place your ${mark} there using exactly ONE app_batch tool call (substitute REAL integers from the cell-center list above for cx and cy):`,
      '  { "default_delay_ms": 80, "steps": [',
      '    { "action": "activate", "title": "Paint", "delayMs": 350 },',
      ...exampleSteps,
      '  ] }',
      `Strategy: win this turn if you can; else block ${opponent} if they threaten to win; else take center, then a corner (1,3,7,9), then a side (2,4,6,8).`,
      `After the app_batch call, reply with EXACTLY one line of plain text and nothing else: "PLAYED ${mark} AT CELL N" (N = the cell you chose).`,
      'Use ONLY the app_batch tool. Do NOT call run, write_file, schedule, or any other tool. Do NOT close Paint.',
    ].join(' ');

    const r = await runCliStreaming(exe, [
      '--cwd', '.',
      '--log-level', 'trace',
      'llm', 'agent',
      '--prompt', prompt,
      '--single-turn',
      '--max-iter', '6',
      '--log', `./${logName}`,
    ], 180_000, `a2a ${mark} turn ${turnNum}`, PACKAGE_ROOT);

    const doc = readAgentResult(logPath);
    const toolCalls = readAgentToolCalls(logPath);
    const replyText = String(doc?.final_text || '');
    const chosen = parseAgentMove(replyText, mark);

    const cost = Number(doc?.llm_usage?.cost ?? 0);
    const rounds = Number(doc?.iterations ?? 0);
    if (Number.isFinite(cost)) totalCost += cost;
    if (Number.isFinite(rounds)) totalRounds += rounds;
    tokenLines.push(`turn ${turnNum} agent ${mark}: rounds=${rounds}; cost=$${cost.toFixed(4)}; chose=${chosen}`);

    rep.step(`agent ${mark} turn ${turnNum}`, r.ms,
      `exit=${r.status}; rounds=${rounds}; cost=$${cost.toFixed(4)}; chose=${chosen}; reply=${JSON.stringify(truncateText(replyText, 80))}; tools=${toolCalls.join(',') || '(none)'}`);
    assert(r.status === 0, `agent ${mark} turn ${turnNum} CLI exits 0 (got ${r.status})`);
    assert(doc, `agent ${mark} turn ${turnNum} produced a result log`);
    assert(toolCalls.includes('app_batch') || toolCalls.filter((t) => t === 'app_drag').length >= 2,
      `agent ${mark} turn ${turnNum} used app_batch/app_drag (tools=${JSON.stringify(toolCalls)})`);
    assert(!toolCalls.includes('run') && !toolCalls.includes('write_file'),
      `agent ${mark} turn ${turnNum} avoided run/write_file (tools=${JSON.stringify(toolCalls)})`);
    assert(chosen >= 1 && chosen <= 9, `agent ${mark} turn ${turnNum} reply names a cell (reply=${JSON.stringify(replyText)})`);
    assert(cells[chosen - 1] === '.', `agent ${mark} turn ${turnNum} chose empty cell (cell ${chosen}, board=${cells.join('')})`);
    cells[chosen - 1] = mark;

    // Let Paint commit the stroke to the canvas surface before capturing.
    await new Promise((res) => setTimeout(res, 250));

    // Visual proof: byte growth per move. The threshold (+150 B) is calibrated
    // above pure JPEG-re-encoding noise of an unchanged image (~50 B) and below
    // the typical delta for a 44 px "+" / "X" glyph on white canvas (~400-1500 B).
    // The end-of-game (afterBytes vs baselineBytes) check is the real proof.
    const probeShot = join(workDir, `paint-a2a-after-turn${turnNum}.jpg`);
    runCli(exe, [
      'assistant', 'app-inspect', 'screenshot',
      '--title', paintWin?.title || 'Paint',
      '--output', probeShot,
      '--json',
    ], 15_000);
    const probeBytes = existsSync(probeShot) ? statSync(probeShot).size : 0;
    assert(probeBytes > cumulativeBytes + 150,
      `agent ${mark} move left visible ink on the canvas (was=${cumulativeBytes}B, now=${probeBytes}B, +${probeBytes - cumulativeBytes}B)`);
    cumulativeBytes = probeBytes;
  }

  // ── Final state + persist requested screenshot ──────────────────────────
  const finalWinner = detectTicTacToeWinner(cells.map((c) => c));
  const xCount = cells.filter((c) => c === 'X').length;
  const oCount = cells.filter((c) => c === 'O').length;

  const finalShot = runCli(exe, [
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint',
    '--output', finalShotAbs,
    '--json',
  ], 15_000);
  const finalBytes = existsSync(finalShotAbs) ? statSync(finalShotAbs).size : 0;
  rep.step('save final screenshot', finalShot.ms, `path=${finalShotRel}; bytes=${finalBytes}; exit=${finalShot.status}`);
  assert(finalShot.status === 0, `final screenshot CLI exits 0 (got ${finalShot.status}; stderr=${truncateText(finalShot.stderr, 200)})`);
  assert(existsSync(finalShotAbs), `final screenshot saved at ${finalShotAbs}`);
  assert(finalBytes > baselineBytes + 3 * 1024,
    `final screenshot reflects the played game (baseline=${baselineBytes}B, final=${finalBytes}B, +${finalBytes - baselineBytes}B)`);

  rep.step('battle final state', 0,
    `cells=${cells.join('')}; X=${xCount}; O=${oCount}; winner=${finalWinner}; ` +
    `turns=${turnNum}; total_agent_cost=$${totalCost.toFixed(4)}; total_agent_rounds=${totalRounds}; ` +
    `screenshot=${finalShotRel}`);
  assert(['X', 'O', 'draw'].includes(finalWinner),
    `A2A reached a terminal state (winner=${finalWinner}, cells=${cells.join('')})`);
  assert(xCount === oCount || xCount === oCount + 1,
    `move counts consistent for X-first play (X=${xCount}, O=${oCount})`);
  assert(turnNum >= 5, `A2A played enough turns (turns=${turnNum})`);

  for (const l of tokenLines) rep.notes.push(l);

  // Best-effort cleanup.
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  await new Promise((r) => setTimeout(r, 600));
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'n', '--json']);

  rep.endSuite();
}

// ─────────────────────────────────────────────────────────────────────────────
// Paint A2A 4x4 — 4 in a row on a 4x4 board, agent vs agent.
// Same orchestration as the 3x3 A2A, parameterized via cellCentersGrid /
// winningLinesGrid / detectWinnerGrid. Final screenshot lives at
// ./tests/battle.jpg (overwrites the 3x3 one — this is the latest game).
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Memory persistence — confirms a session_id round-trips across two CLI
// processes via the on-disk session store (sessions/<id>.json next to
// settings.json). Uses PM_IMAGE_SESSION_DIR to keep test sessions out of the
// user's roaming profile.
// ─────────────────────────────────────────────────────────────────────────────
async function suiteLlmAgentMemoryPersistence(rep, assert, exe, workDir) {
  console.log('\n-- LLM memory persistence: --session-id across processes --\n');
  rep.beginSuite('LLM memory persistence');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped memory persistence: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('memory persist skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped memory persistence: no configured API key.');
    rep.step('memory persist skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }

  const sessionsDir = join(workDir, 'sessions');
  if (existsSync(sessionsDir)) rmSync(sessionsDir, { recursive: true, force: true });
  mkdirSync(sessionsDir, { recursive: true });

  const sessionId = 'orchestrator-persist-' + Date.now().toString(36);
  const sessionFile = join(sessionsDir, `${sessionId}.json`);

  const envOverride = { ...process.env, PM_IMAGE_SESSION_DIR: sessionsDir };

  // ── Invocation 1: write memory ───────────────────────────────────────────
  const writePrompt = 'Call the memory_write tool ONCE with state = ' +
    '{"favorite_color": "blue", "favorite_number": 42, "test_marker": "orchestrator-' +
    sessionId.slice(-6) + '"}. After the tool call, reply with exactly: WROTE';
  const write = runCli(exe, [
    '--log-level', 'info',
    'llm', 'agent',
    '--prompt', writePrompt,
    '--single-turn', '--session-id', sessionId, '--max-iter', '4',
  ], 60_000, envOverride);
  rep.step('write memory (invocation 1)', write.ms, `exit=${write.status}; stdout_tail=${truncateText(write.stdout, 80)}`);
  assert(write.status === 0, `write-memory CLI exits 0 (got ${write.status}; stderr=${truncateText(write.stderr, 200)})`);

  // ── Inspect persisted file ───────────────────────────────────────────────
  assert(existsSync(sessionFile), `session JSON persisted at ${sessionFile}`);
  let parsed = null;
  try { parsed = JSON.parse(readFileSync(sessionFile, 'utf8')); } catch (e) {
    rep.notes.push(`parse error: ${e.message}`);
  }
  assert(parsed && typeof parsed === 'object', 'session file is valid JSON');
  assert(parsed?.id === sessionId, `id round-trips (file=${parsed?.id}, expected=${sessionId})`);
  assert(parsed?.memory_state?.favorite_color === 'blue',
    `memory_state.favorite_color persisted (got=${JSON.stringify(parsed?.memory_state)})`);
  assert(parsed?.memory_state?.favorite_number === 42,
    `memory_state.favorite_number persisted (got=${JSON.stringify(parsed?.memory_state)})`);
  assert(Array.isArray(parsed?.events) && parsed.events.length === 1,
    `1 event after invocation 1 (got=${parsed?.events?.length})`);

  // ── Invocation 2: read memory back in a fresh process ────────────────────
  const readPrompt = 'Use the memory_read tool, then reply with exactly: ' +
    'FOUND color=<favorite_color> number=<favorite_number>';
  const read = runCli(exe, [
    '--log-level', 'info',
    'llm', 'agent',
    '--prompt', readPrompt,
    '--single-turn', '--session-id', sessionId, '--max-iter', '4',
  ], 60_000, envOverride);
  rep.step('read memory (invocation 2)', read.ms, `exit=${read.status}; stdout_tail=${truncateText(read.stdout, 100)}`);
  assert(read.status === 0, `read-memory CLI exits 0 (got ${read.status}; stderr=${truncateText(read.stderr, 200)})`);
  // The agent's final text is the LAST non-tool line of stdout. Search loosely.
  const finalLine = String(read.stdout || '').split(/\r?\n/).filter(Boolean).pop() || '';
  assert(/blue/i.test(read.stdout) && /42/.test(read.stdout),
    `fresh process read back "blue" and 42 (final_line=${JSON.stringify(finalLine)})`);

  // ── Verify the event log grew ────────────────────────────────────────────
  let parsed2 = null;
  try { parsed2 = JSON.parse(readFileSync(sessionFile, 'utf8')); } catch {}
  assert(Array.isArray(parsed2?.events) && parsed2.events.length === 2,
    `2 events after invocation 2 (got=${parsed2?.events?.length})`);
  assert(parsed2?.memory_state?.favorite_color === 'blue',
    'memory_state survives invocation 2 (still blue)');
  assert(parsed2?.updated_at && parsed2.updated_at !== parsed.updated_at,
    `updated_at advanced after invocation 2 (was=${parsed?.updated_at}, now=${parsed2?.updated_at})`);

  rep.notes.push(`session file: ${sessionFile}`);
  rep.notes.push(`memory_state: ${JSON.stringify(parsed2?.memory_state)}`);
  rep.notes.push(`events: ${parsed2?.events?.length ?? 0}`);

  rep.endSuite();
}

// ─────────────────────────────────────────────────────────────────────────────
// 4x4 human-vs-agent battle
// ─────────────────────────────────────────────────────────────────────────────
// The harness plays X using a 4-in-a-row tactical heuristic and emulates
// what a real human would do at the keyboard: open MS Paint, draw an X with
// the brush, hand control to the agent, wait, draw another X, etc.
//
// The agent plays O via the LLM, with a stable --session-id so the new
// roaming session persistence (sessions/<id>.json) carries its memory_state
// + event log across every turn's CLI invocation.
//
// Why "human vs agent" and not just A2A 4x4: the A2A test proves two LLM
// players can co-exist in the same canvas; this proves the harness can stand
// in for a real human player, and exercises the new session memory layer in
// a tight game-loop where every agent turn is a fresh process.

/// 4-in-a-row 4x4 X picker. Stronger than the 3x3 one because the search
/// space is too large for full minimax and a center-then-corner ladder
/// is too weak. Order of precedence:
///   1. Immediate winning move.
///   2. Block opponent's immediate winning move.
///   3. Move that creates a double threat (>=2 lines where placing X next
///      would win and opponent can only block one).
///   4. Block opponent's double-threat move.
///   5. Score each empty cell by line-participation potential, plus a small
///      bias toward the central 4 cells (6, 7, 10, 11) early in the game.
function pickXMove4x4(cells, lines) {
  const N = cells.length;
  const empties = [];
  for (let i = 0; i < N; ++i) if (cells[i] === '.') empties.push(i);
  if (!empties.length) return -1;

  const completesFor = (mark, idx) => {
    const t = cells.slice(); t[idx] = mark;
    return lines.some((ln) => ln.every((k) => t[k] === mark));
  };

  // 1) Win.
  for (const i of empties) if (completesFor('X', i)) return i;
  // 2) Block opponent's win.
  for (const i of empties) if (completesFor('O', i)) return i;

  // 3 / 4) Double-threat detection. After placing `mark` at `idx`, count how
  //         many distinct empty cells would let `mark` complete 4-in-a-row
  //         on the next turn. >=2 = fork (opponent can only block one).
  const threatCountAfter = (mark, idx) => {
    const t = cells.slice(); t[idx] = mark;
    const threats = new Set();
    for (let j = 0; j < N; ++j) {
      if (t[j] !== '.') continue;
      const u = t.slice(); u[j] = mark;
      for (const ln of lines) {
        if (ln.every((k) => u[k] === mark)) { threats.add(j); break; }
      }
    }
    return threats.size;
  };

  let forkSelf  = empties.find((i) => threatCountAfter('X', i) >= 2);
  if (forkSelf  !== undefined) return forkSelf;
  let forkOther = empties.find((i) => threatCountAfter('O', i) >= 2);
  if (forkOther !== undefined) return forkOther;

  // 5) Line-participation score.
  const central = new Set([5, 6, 9, 10]); // cells 6, 7, 10, 11 (0-indexed)
  let bestIdx = empties[0];
  let bestScore = -Infinity;
  for (const i of empties) {
    let s = 0;
    for (const ln of lines) {
      if (!ln.includes(i)) continue;
      const xc = ln.filter((k) => cells[k] === 'X').length;
      const oc = ln.filter((k) => cells[k] === 'O').length;
      if (oc === 0) s += [0, 2, 8, 32][xc] || 0;      // build my lines
      if (xc === 0 && oc >= 2) s += oc * 4;            // soft defense
    }
    if (central.has(i)) s += 1;                        // very small tiebreaker
    if (s > bestScore) { bestScore = s; bestIdx = i; }
  }
  return bestIdx;
}

/// Draw an X glyph at a cell-center using a 2-stroke app-use batch.
function drawXAtCell4x4(exe, workDir, centers, cellIdx /* 0..15 */) {
  const { x, y } = centers[cellIdx];
  const HS = 20;
  const batch = {
    steps: [
      { action: 'activate', title: 'Paint', delayMs: 350 },
      { action: 'drag', x: x - HS, y: y - HS, dx: 2 * HS, dy: 2 * HS,
        steps: 16, durationMs: 240, delayMs: 200 },
      { action: 'drag', x: x - HS, y: y + HS, dx: 2 * HS, dy: -2 * HS,
        steps: 16, durationMs: 240, delayMs: 200 },
    ],
  };
  const file = join(workDir, `paint-h2a-4x4-x-${cellIdx}.batch.json`);
  writeFileSync(file, `${JSON.stringify(batch, null, 2)}\n`, 'utf8');
  return runCli(exe, ['assistant', 'app-use', 'batch', '--file', file, '--json', '--default-delay-ms', '60'], 30_000);
}

async function suiteLlmAgentPaintTicTacToeHumanVsAgent4x4(rep, assert, exe, workDir) {
  console.log('\n-- LLM Agent: Paint 4x4 HUMAN (harness X) vs AGENT (O) --\n');
  rep.beginSuite('LLM Paint tic-tac-toe 4x4 human-vs-agent');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped human-vs-agent 4x4: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('h2a 4x4 skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped human-vs-agent 4x4: no configured API key.');
    rep.step('h2a 4x4 skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }
  const mspaint = paintExePath();
  if (!mspaint) {
    rep.notes.push('Skipped human-vs-agent 4x4: mspaint.exe not found.');
    rep.step('h2a 4x4 skipped', 0, 'mspaint.exe not found');
    rep.endSuite();
    return;
  }

  const ROWS = 4, COLS = 4, WIN_K = 4, N_CELLS = ROWS * COLS;
  const MAX_TURNS = N_CELLS;
  const lines = winningLinesGrid(ROWS, COLS, WIN_K);

  const finalShotRel = 'tests/battle-human-vs-agent.jpg';
  const finalShotAbs = join(PACKAGE_ROOT, 'tests', 'battle-human-vs-agent.jpg');
  mkdirSync(join(PACKAGE_ROOT, 'tests'), { recursive: true });

  const agentLogNames = Array.from({ length: MAX_TURNS }, (_, i) => `llm-agent-paint-h2a-4x4-turn${i + 1}.json`);
  cleanAgentArtifacts([finalShotRel, ...agentLogNames]);

  // Per-run isolated session dir so we don't pollute roaming/.
  const sessionsDir = join(workDir, 'sessions');
  if (existsSync(sessionsDir)) rmSync(sessionsDir, { recursive: true, force: true });
  mkdirSync(sessionsDir, { recursive: true });
  const agentSessionId = `paint-h2a-4x4-o-${Date.now().toString(36)}`;
  const h2aEnv = { ...process.env, PM_IMAGE_SESSION_DIR: sessionsDir };

  // ── Open Paint ──────────────────────────────────────────────────────────
  const open = runCli(exe, [
    'assistant', 'app-use', 'open-app',
    '--exe', mspaint, '--wait-ms', '6000',
    '--x', '100', '--y', '60', '--width', '1080', '--height', '880',
    '--json',
  ], 30_000);
  const openDoc = parseJson(open.stdout);
  rep.step('open Paint', open.ms, `exit=${open.status}; pid=${openDoc.pid}`);
  assert(open.status === 0 && openDoc.ok === true && openDoc.pid > 0, 'Paint launches');
  await new Promise((r) => setTimeout(r, 1200));

  const inspect = runCli(exe, ['assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json'], 15_000);
  const inspectDoc = parseJson(inspect.stdout);
  const paintWin = findPaintWindow(inspectDoc);
  const winRect = paintWin?.rect;
  assert(winRect?.w > 200 && winRect?.h > 200, `Paint window rect resolved (${JSON.stringify(winRect)})`);
  const canvas = paintCanvasRect(winRect);
  const centers = cellCentersGrid(canvas, ROWS, COLS);

  // ── Draw 4x4 grid (3 verticals + 3 horizontals) ─────────────────────────
  const padX = Math.max(20, Math.round(canvas.w * 0.05));
  const padY = Math.max(20, Math.round(canvas.h * 0.05));
  const gridSteps = [{ action: 'activate', title: paintWin?.title || 'Paint', delayMs: 500 }];
  for (let i = 1; i < COLS; ++i) {
    gridSteps.push({
      action: 'drag',
      x: canvas.x + Math.round((i * canvas.w) / COLS),
      y: canvas.y + padY,
      dx: 0, dy: canvas.h - 2 * padY,
      steps: 28, durationMs: 500, delayMs: 320,
    });
  }
  for (let i = 1; i < ROWS; ++i) {
    gridSteps.push({
      action: 'drag',
      x: canvas.x + padX,
      y: canvas.y + Math.round((i * canvas.h) / ROWS),
      dx: canvas.w - 2 * padX, dy: 0,
      steps: 28, durationMs: 500, delayMs: 320,
    });
  }
  const gridFile = join(workDir, 'paint-h2a-4x4-grid.batch.json');
  writeFileSync(gridFile, `${JSON.stringify({ steps: gridSteps }, null, 2)}\n`, 'utf8');
  const grid = runCli(exe, ['assistant', 'app-use', 'batch', '--file', gridFile, '--json', '--default-delay-ms', '60'], 90_000);
  const gridDoc = parseJson(grid.stdout);
  rep.step('draw 4x4 grid', grid.ms, `exit=${grid.status}; steps=${gridDoc.steps?.length ?? 0}`);
  assert(grid.status === 0 && gridDoc.ok === true, `grid drawn ok (steps=${gridDoc.steps?.length ?? 0})`);

  const baselineShot = join(workDir, 'paint-h2a-4x4-baseline.jpg');
  runCli(exe, ['assistant', 'app-inspect', 'screenshot', '--title', paintWin?.title || 'Paint',
               '--output', baselineShot, '--json'], 15_000);
  const baselineBytes = existsSync(baselineShot) ? statSync(baselineShot).size : 0;
  assert(baselineBytes > 1024, `empty-grid baseline screenshot (${baselineBytes}B)`);

  // ── Game loop: harness X plays first, agent O responds ──────────────────
  const cells = Array(N_CELLS).fill('.');
  let turn = 0;
  let totalAgentCost = 0;
  let totalAgentRounds = 0;
  const winningLinesForPrompt = lines.map((ln) => ln.map((i) => i + 1).join('-')).join(', ');

  while (true) {
    const w = detectWinnerGrid(cells, ROWS, COLS, WIN_K);
    if (w !== 'ongoing') break;
    if (turn >= MAX_TURNS) break;

    const xCount = cells.filter((c) => c === 'X').length;
    const oCount = cells.filter((c) => c === 'O').length;
    const mark = (xCount === oCount) ? 'X' : 'O';
    turn++;

    if (mark === 'X') {
      // ── Harness "human" plays X ──
      const xCell = pickXMove4x4(cells, lines);
      assert(xCell >= 0 && cells[xCell] === '.',
        `harness X picks valid empty cell (cell ${xCell + 1}, board=${cells.join('')})`);
      const xDraw = drawXAtCell4x4(exe, workDir, centers, xCell);
      const xDrawDoc = parseJson(xDraw.stdout);
      cells[xCell] = 'X';
      rep.step(`turn ${turn}: harness X plays`, xDraw.ms,
        `cell=${xCell + 1}; exit=${xDraw.status}; batch_ok=${xDrawDoc?.ok}; board=${cells.join('')}`);
      assert(xDraw.status === 0 && xDrawDoc?.ok === true,
        `harness X drew cell ${xCell + 1} via app-use batch`);
      await new Promise((r) => setTimeout(r, 250));
      continue;
    }

    // ── Agent O plays via LLM ──
    const empties = cells.map((c, i) => c === '.' ? i + 1 : null).filter((v) => v !== null);
    const centersLine = centers.map((p, i) => `${i + 1}=(${p.x},${p.y})`).join(', ');
    const HS = 20;
    const boardText = renderBoardGrid(cells, ROWS, COLS);
    const prompt = [
      'You are AGENT O playing 4x4 tic-tac-toe against a HUMAN player (X) on a real MS Paint canvas. You are using a persistent session memory across turns.',
      `Board is 4 rows by 4 columns. To WIN you need ${WIN_K} of your mark in a row (horizontal, vertical, or diagonal). Cells are numbered 1..16 left-to-right top-to-bottom.`,
      'Current board (. = empty):',
      `  ${boardText}`,
      `Empty cells: ${JSON.stringify(empties)}.`,
      `Cell-center screen coordinates (Paint canvas, absolute px): ${centersLine}.`,
      'Draw your O using two perpendicular strokes that cross at the cell center (a "+" shape).',
      `Pick ONE empty cell N. Place your O there using exactly ONE app_batch tool call (substitute REAL integers for cx and cy from the cell-center list):`,
      '  { "default_delay_ms": 60, "steps": [',
      '    { "action": "activate", "title": "Paint", "delayMs": 300 },',
      `    { "action": "drag", "x": cx-${HS}, "y": cy,    "dx": ${2 * HS}, "dy": 0,  "steps": 12, "duration_ms": 180, "delayMs": 200 },`,
      `    { "action": "drag", "x": cx,    "y": cy-${HS}, "dx": 0,  "dy": ${2 * HS}, "steps": 12, "duration_ms": 180, "delayMs": 200 }`,
      '  ] }',
      `Strategy priorities: (1) if any empty cell completes 4-in-a-row for O, take it and WIN; (2) if any empty cell would let X complete 4-in-a-row next turn, BLOCK it; (3) create a double threat where possible; (4) extend your longest open line; (5) prefer central cells (6, 7, 10, 11) early. Winning lines: ${winningLinesForPrompt}.`,
      'STRICT OUTPUT FORMAT: your TEXT reply must be EXACTLY one line: "PLAYED O AT CELL N". Do not echo the board, do not print analysis - put reasoning into your private memory if you want (see below). Token-rambling in the text channel wastes your output budget BEFORE the app_batch call lands and you LOSE the turn.',
      'PERSISTENT MEMORY (your private scratchpad, survives ACROSS THIS WHOLE GAME via session id): you may call memory_read first to recall your prior plan, then memory_write({"state": {...}}) with a compact JSON like {"plan": "extend 6-11", "human_pattern": "favours corners", "threats": [...]}. This is OPTIONAL but recommended for a sustained game vs a human.',
      'Use ONLY: app_batch (mandatory), plus optionally memory_read / memory_write. Do NOT call run, write_file, schedule_*, app_inspect_*, app_screenshot, app_open, app_close, or any other tool. Do NOT close Paint.',
    ].join(' ');

    const logName = agentLogNames[turn - 1];
    const logPath = join(PACKAGE_ROOT, logName);

    const r = await runCliStreaming(exe, [
      '--cwd', '.', '--log-level', 'trace',
      'llm', 'agent',
      '--prompt', prompt,
      '--single-turn',
      '--session-id', agentSessionId,
      '--max-iter', '6',
      '--log', `./${logName}`,
    ], 180_000, `h2a4x4 O turn ${turn}`, PACKAGE_ROOT, h2aEnv);

    const doc = readAgentResult(logPath);
    const toolCalls = readAgentToolCalls(logPath);
    const replyText = String(doc?.final_text || '');
    const chosen = parseAgentMove(replyText, 'O');

    const cost = Number(doc?.llm_usage?.cost ?? 0);
    const rounds = Number(doc?.iterations ?? 0);
    if (Number.isFinite(cost)) totalAgentCost += cost;
    if (Number.isFinite(rounds)) totalAgentRounds += rounds;

    const usedDrawTool = toolCalls.includes('app_batch') || toolCalls.filter((t) => t === 'app_drag').length >= 2;
    const usedForbidden = toolCalls.includes('run') || toolCalls.includes('write_file');
    const cellInRange = chosen >= 1 && chosen <= N_CELLS;
    const cellEmpty = cellInRange && cells[chosen - 1] === '.';

    rep.step(`turn ${turn}: agent O plays`, r.ms,
      `exit=${r.status}; rounds=${rounds}; cost=$${cost.toFixed(4)}; chose=${chosen}; reply=${JSON.stringify(truncateText(replyText, 80))}; tools=${toolCalls.join(',') || '(none)'}`);

    assert(r.status === 0, `agent O turn ${turn} CLI exits 0`);
    assert(doc, `agent O turn ${turn} produced a result log`);
    assert(usedDrawTool, `agent O turn ${turn} used app_batch/app_drag`);
    assert(!usedForbidden, `agent O turn ${turn} avoided run/write_file`);
    assert(cellInRange, `agent O turn ${turn} reply names a cell 1..${N_CELLS}`);
    assert(cellEmpty, `agent O turn ${turn} chose empty cell`);

    if (!usedDrawTool || !cellInRange || !cellEmpty) {
      rep.notes.push(`agent O forfeited turn ${turn}`);
      break;
    }
    cells[chosen - 1] = 'O';
    await new Promise((res) => setTimeout(res, 250));
  }

  // ── Final state + save user-visible artifact ────────────────────────────
  const finalWinner = detectWinnerGrid(cells, ROWS, COLS, WIN_K);
  const xCount = cells.filter((c) => c === 'X').length;
  const oCount = cells.filter((c) => c === 'O').length;

  const finalShot = runCli(exe, ['assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint', '--output', finalShotAbs, '--json'], 15_000);
  const finalBytes = existsSync(finalShotAbs) ? statSync(finalShotAbs).size : 0;
  rep.step('save final screenshot', finalShot.ms, `path=${finalShotRel}; bytes=${finalBytes}; exit=${finalShot.status}`);
  assert(finalShot.status === 0, 'final screenshot CLI exits 0');
  assert(existsSync(finalShotAbs), `final screenshot saved at ${finalShotAbs}`);
  const inkFloor = Math.max(2 * 1024, turn * 350);
  assert(finalBytes > baselineBytes + inkFloor,
    `final screenshot reflects the played game (baseline=${baselineBytes}B, final=${finalBytes}B, +${finalBytes - baselineBytes}B, floor=+${inkFloor}B)`);

  rep.step('h2a 4x4 final state', 0,
    `cells=${cells.join('')}; X=${xCount}; O=${oCount}; winner=${finalWinner}; turns=${turn}; ` +
    `total_agent_cost=$${totalAgentCost.toFixed(4)}; total_agent_rounds=${totalAgentRounds}; ` +
    `screenshot=${finalShotRel}`);

  assert(['X', 'O', 'draw', 'ongoing'].includes(finalWinner),
    `h2a 4x4 reached a valid state (winner=${finalWinner})`);
  assert(xCount === oCount || xCount === oCount + 1,
    `move counts consistent with X-first play (X=${xCount}, O=${oCount})`);

  // Inspect agent's persisted session.
  const agentSessionFile = join(sessionsDir, `${agentSessionId}.json`);
  if (existsSync(agentSessionFile)) {
    let p = null;
    try { p = JSON.parse(readFileSync(agentSessionFile, 'utf8')); } catch {}
    const memKeys = p?.memory_state && typeof p.memory_state === 'object'
      ? Object.keys(p.memory_state) : [];
    rep.notes.push(`agent O session: file=${agentSessionId}.json; events=${p?.events?.length ?? 0}; memory_state_keys=${JSON.stringify(memKeys)}`);
  }
  rep.notes.push(`screenshot artifact: ${finalShotRel}`);
  rep.notes.push(`final: winner=${finalWinner}; turns=${turn}; cost=$${totalAgentCost.toFixed(4)}`);

  // Best-effort cleanup.
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  await new Promise((r) => setTimeout(r, 600));
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'n', '--json']);

  rep.endSuite();
}

async function suiteLlmAgentPaintTicTacToeA2A4x4(rep, assert, exe, workDir) {
  console.log('\n-- LLM A2A 4x4: agent X vs agent O, 4 in a row --\n');
  rep.beginSuite('LLM A2A Paint tic-tac-toe 4x4');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped Paint A2A 4x4: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('paint a2a 4x4 skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped Paint A2A 4x4: no configured API key.');
    rep.step('paint a2a 4x4 skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }
  const mspaint = paintExePath();
  if (!mspaint) {
    rep.notes.push('Skipped Paint A2A 4x4: mspaint.exe not found.');
    rep.step('paint a2a 4x4 skipped', 0, 'mspaint.exe not found');
    rep.endSuite();
    return;
  }

  const ROWS = 4;
  const COLS = 4;
  const WIN_K = 4;
  const N_CELLS = ROWS * COLS;
  const MAX_TURNS = N_CELLS; // upper bound

  const finalShotRel = 'tests/battle.jpg';
  const finalShotAbs = join(PACKAGE_ROOT, 'tests', 'battle.jpg');
  mkdirSync(join(PACKAGE_ROOT, 'tests'), { recursive: true });

  const turnLogNames = Array.from({ length: MAX_TURNS }, (_, i) => `llm-agent-paint-a2a-4x4-turn${i + 1}.json`);
  cleanAgentArtifacts([finalShotRel, ...turnLogNames]);

  // Each agent gets a stable session id so it accumulates its own per-turn
  // notes via memory_write / memory_append_event across CLI invocations.
  // Sessions are isolated under a per-run tmp dir so we don't trample the
  // user's roaming sessions/.
  const sessionsDir = join(workDir, 'sessions');
  if (existsSync(sessionsDir)) rmSync(sessionsDir, { recursive: true, force: true });
  mkdirSync(sessionsDir, { recursive: true });
  const sessionIdFor = (mark) => `a2a-4x4-${mark.toLowerCase()}-${Date.now().toString(36)}`;
  const xSessionId = sessionIdFor('X');
  const oSessionId = sessionIdFor('O');
  const a2aEnv = { ...process.env, PM_IMAGE_SESSION_DIR: sessionsDir };

  // ── Open Paint (larger window so 4x4 cells stay comfortable) ─────────────
  const open = runCli(exe, [
    'assistant', 'app-use', 'open-app',
    '--exe', mspaint, '--wait-ms', '6000',
    '--x', '100', '--y', '60', '--width', '1080', '--height', '880',
    '--json',
  ], 30_000);
  const openDoc = parseJson(open.stdout);
  rep.step('open Paint', open.ms, `exit=${open.status}; pid=${openDoc.pid}`);
  assert(open.status === 0 && openDoc.ok === true && openDoc.pid > 0, 'Paint launches');

  await new Promise((r) => setTimeout(r, 1200));

  const inspect = runCli(exe, ['assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json'], 15_000);
  const inspectDoc = parseJson(inspect.stdout);
  const paintWin = findPaintWindow(inspectDoc);
  const winRect = paintWin?.rect;
  assert(winRect?.w > 200 && winRect?.h > 200, `Paint window rect resolved (${JSON.stringify(winRect)})`);
  const canvas = paintCanvasRect(winRect);
  const centers = cellCentersGrid(canvas, ROWS, COLS);

  // ── Draw the 4x4 grid: 3 horizontal + 3 vertical dividers ────────────────
  const padX = Math.max(20, Math.round(canvas.w * 0.05));
  const padY = Math.max(20, Math.round(canvas.h * 0.05));
  const gridSteps = [
    { action: 'activate', title: paintWin?.title || 'Paint', delayMs: 500 },
  ];
  for (let i = 1; i < COLS; ++i) {
    gridSteps.push({
      action: 'drag',
      x: canvas.x + Math.round((i * canvas.w) / COLS),
      y: canvas.y + padY,
      dx: 0, dy: canvas.h - 2 * padY,
      steps: 28, durationMs: 500, delayMs: 320,
    });
  }
  for (let i = 1; i < ROWS; ++i) {
    gridSteps.push({
      action: 'drag',
      x: canvas.x + padX,
      y: canvas.y + Math.round((i * canvas.h) / ROWS),
      dx: canvas.w - 2 * padX, dy: 0,
      steps: 28, durationMs: 500, delayMs: 320,
    });
  }
  const gridBatchFile = join(workDir, 'paint-a2a-4x4-grid.batch.json');
  writeFileSync(gridBatchFile, `${JSON.stringify({ steps: gridSteps }, null, 2)}\n`, 'utf8');
  const grid = runCli(exe, ['assistant', 'app-use', 'batch', '--file', gridBatchFile, '--json', '--default-delay-ms', '60'], 90_000);
  const gridDoc = parseJson(grid.stdout);
  rep.step('draw 4x4 grid', grid.ms, `exit=${grid.status}; steps=${gridDoc.steps?.length ?? 0}`);
  assert(grid.status === 0 && gridDoc.ok === true, `4x4 grid drawn ok (steps=${gridDoc.steps?.length ?? 0})`);

  // Baseline screenshot.
  const baselineShot = join(workDir, 'paint-a2a-4x4-baseline.jpg');
  runCli(exe, [
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint',
    '--output', baselineShot,
    '--json',
  ], 15_000);
  const baselineBytes = existsSync(baselineShot) ? statSync(baselineShot).size : 0;
  assert(baselineBytes > 1024, `4x4 empty-grid baseline screenshot saved (${baselineBytes}B)`);

  // ── A2A loop ─────────────────────────────────────────────────────────────
  const cells = Array(N_CELLS).fill('.');
  let turnNum = 0;
  let totalCost = 0;
  let totalRounds = 0;
  let cumulativeBytes = baselineBytes;
  const tokenLines = [];
  const winningLinesForPrompt = winningLinesGrid(ROWS, COLS, WIN_K)
    .map((ln) => ln.map((i) => i + 1).join('-'))
    .join(', ');

  // Mark stroke size — cells are ~250x167 with the larger Paint window.
  // halfSize 20 makes 40px glyphs that fit comfortably inside a cell.
  const HS = 20;

  while (true) {
    const w = detectWinnerGrid(cells, ROWS, COLS, WIN_K);
    if (w !== 'ongoing') break;
    if (turnNum >= MAX_TURNS) break;

    turnNum++;
    const xCount = cells.filter((c) => c === 'X').length;
    const oCount = cells.filter((c) => c === 'O').length;
    const mark = (xCount === oCount) ? 'X' : 'O';
    const opponent = mark === 'X' ? 'O' : 'X';

    const exampleSteps = mark === 'X' ? [
      `    { "action": "drag", "x": cx-${HS}, "y": cy-${HS}, "dx": ${2 * HS}, "dy": ${2 * HS}, "steps": 14, "duration_ms": 200, "delayMs": 200 },`,
      `    { "action": "drag", "x": cx-${HS}, "y": cy+${HS}, "dx": ${2 * HS}, "dy": -${2 * HS}, "steps": 14, "duration_ms": 200, "delayMs": 200 }`,
    ] : [
      `    { "action": "drag", "x": cx-${HS}, "y": cy,    "dx": ${2 * HS}, "dy": 0,  "steps": 12, "duration_ms": 180, "delayMs": 200 },`,
      `    { "action": "drag", "x": cx,    "y": cy-${HS}, "dx": 0,  "dy": ${2 * HS}, "steps": 12, "duration_ms": 180, "delayMs": 200 }`,
    ];

    const logName = turnLogNames[turnNum - 1];
    const logPath = join(PACKAGE_ROOT, logName);
    const emptyIndices = cells.map((c, i) => c === '.' ? i + 1 : null).filter((v) => v !== null);
    const centersLine = centers.map((p, i) => `${i + 1}=(${p.x},${p.y})`).join(', ');
    const boardText = renderBoardGrid(cells, ROWS, COLS);

    const markStyle = mark === 'X'
      ? 'Draw an X using two DIAGONAL strokes that cross at the cell center. Stroke 1: top-left to bottom-right. Stroke 2: bottom-left to top-right.'
      : 'Draw an O using two PERPENDICULAR strokes that cross at the cell center (a "+" shape). Stroke 1: horizontal. Stroke 2: vertical.';

    const prompt = [
      `You are AGENT ${mark} in an agent-vs-agent 4x4 tic-tac-toe match in MS Paint. Your opponent is AGENT ${opponent}. Turn ${turnNum} of the match.`,
      `Board is 4 rows by 4 columns, cells numbered 1..16 left-to-right top-to-bottom. To WIN you need ${WIN_K} of your mark in a row (horizontal, vertical, or diagonal).`,
      'Current board (. = empty):',
      `  ${boardText}`,
      `Empty cells you may choose from: ${JSON.stringify(emptyIndices)}.`,
      `Cell-center screen coordinates (Paint canvas, absolute px): ${centersLine}.`,
      `Your mark style: ${markStyle}`,
      `Pick ONE empty cell N. Place your ${mark} there using exactly ONE app_batch tool call (substitute REAL integers from the cell-center list above for cx and cy):`,
      '  { "default_delay_ms": 60, "steps": [',
      '    { "action": "activate", "title": "Paint", "delayMs": 300 },',
      ...exampleSteps,
      '  ] }',
      `Strategy priorities, in order: (1) if any empty cell completes 4-in-a-row for you, take it and WIN; (2) if any empty cell would let ${opponent} complete 4-in-a-row next turn, BLOCK it; (3) extend your longest open line of 2 or 3 toward 4-in-a-row; (4) prefer the central 4 cells (6, 7, 10, 11) early; (5) corners (1, 4, 13, 16); (6) any side. Winning lines: ${winningLinesForPrompt}.`,
      'STRICT OUTPUT FORMAT: your TEXT reply must be EXACTLY one line, nothing else: "PLAYED ' + mark + ' AT CELL N". Do your analysis silently. Do NOT echo the board, do NOT print "Row 1:", "Step 1:", or any explanation in the text channel - put your action into the app_batch tool call. If you spend tokens analysing in text you will run out of output budget BEFORE calling app_batch, and you will lose the turn.',
      `After (not before) the app_batch tool call, the text reply must be exactly: PLAYED ${mark} AT CELL N`,
      'PERSISTENT MEMORY (your private scratchpad, survives across turns): you may also call memory_write({"state": {...}}) to store a short JSON summary of your strategy (e.g. {"plan": "build diagonal 1-6-11-16", "threats": [...]}); your saved state is injected into the system prompt on your next turn. memory_append_event({"event":{...}}) appends to your event log. Both are OPTIONAL - skip them if you do not need them.',
      'Use ONLY app_batch (mandatory) plus optionally memory_write / memory_append_event. Do NOT call run, write_file, schedule_*, app_inspect_*, app_screenshot, app_open, app_close, or any other tool. Do NOT close Paint.',
    ].join(' ');

    const r = await runCliStreaming(exe, [
      '--cwd', '.',
      '--log-level', 'trace',
      'llm', 'agent',
      '--prompt', prompt,
      '--single-turn',
      '--session-id', mark === 'X' ? xSessionId : oSessionId,
      '--max-iter', '6',
      '--log', `./${logName}`,
    ], 180_000, `a2a4x4 ${mark} turn ${turnNum}`, PACKAGE_ROOT, a2aEnv);

    const doc = readAgentResult(logPath);
    const toolCalls = readAgentToolCalls(logPath);
    const replyText = String(doc?.final_text || '');
    const chosen = parseAgentMove(replyText, mark);

    const cost = Number(doc?.llm_usage?.cost ?? 0);
    const rounds = Number(doc?.iterations ?? 0);
    if (Number.isFinite(cost)) totalCost += cost;
    if (Number.isFinite(rounds)) totalRounds += rounds;
    tokenLines.push(`turn ${turnNum} agent ${mark}: rounds=${rounds}; cost=$${cost.toFixed(4)}; chose=${chosen}`);

    rep.step(`agent ${mark} turn ${turnNum}`, r.ms,
      `exit=${r.status}; rounds=${rounds}; cost=$${cost.toFixed(4)}; chose=${chosen}; reply=${JSON.stringify(truncateText(replyText, 80))}; tools=${toolCalls.join(',') || '(none)'}`);
    assert(r.status === 0, `agent ${mark} turn ${turnNum} CLI exits 0 (got ${r.status})`);
    assert(doc, `agent ${mark} turn ${turnNum} produced a result log`);

    const usedDrawTool = toolCalls.includes('app_batch') || toolCalls.filter((t) => t === 'app_drag').length >= 2;
    const usedForbidden = toolCalls.includes('run') || toolCalls.includes('write_file');
    const cellInRange = chosen >= 1 && chosen <= N_CELLS;
    const cellEmpty = cellInRange && cells[chosen - 1] === '.';

    assert(usedDrawTool, `agent ${mark} turn ${turnNum} used app_batch/app_drag (tools=${JSON.stringify(toolCalls)})`);
    assert(!usedForbidden, `agent ${mark} turn ${turnNum} avoided run/write_file (tools=${JSON.stringify(toolCalls)})`);
    assert(cellInRange, `agent ${mark} turn ${turnNum} reply names a cell 1..${N_CELLS} (reply=${JSON.stringify(replyText)})`);
    assert(cellEmpty, `agent ${mark} turn ${turnNum} chose empty cell (cell ${chosen}, board=${cells.join('')})`);

    // If the agent forfeited this turn (no draw tool, no parseable reply, or
    // picked an occupied cell) STOP the game cleanly. Do not advance the
    // ledger — otherwise the board state diverges from the canvas and the
    // post-game checks become meaningless.
    if (!usedDrawTool || !cellInRange || !cellEmpty) {
      rep.notes.push(`agent ${mark} forfeited turn ${turnNum} (usedDrawTool=${usedDrawTool}; cellInRange=${cellInRange}; cellEmpty=${cellEmpty})`);
      break;
    }

    cells[chosen - 1] = mark;
    await new Promise((res) => setTimeout(res, 250));

    // Per-turn ink probe is kept as a diagnostic only — small marks in a
    // crowded JPEG can grow by as little as 100 B even when the stroke
    // landed correctly. The end-of-game aggregate check is the real guard.
    const probeShot = join(workDir, `paint-a2a-4x4-after-turn${turnNum}.jpg`);
    runCli(exe, [
      'assistant', 'app-inspect', 'screenshot',
      '--title', paintWin?.title || 'Paint',
      '--output', probeShot,
      '--json',
    ], 15_000);
    const probeBytes = existsSync(probeShot) ? statSync(probeShot).size : 0;
    tokenLines.push(`turn ${turnNum} ink: was=${cumulativeBytes}B now=${probeBytes}B delta=${probeBytes - cumulativeBytes}B`);
    cumulativeBytes = probeBytes;
  }

  // ── Final state + persist requested screenshot ──────────────────────────
  const finalWinner = detectWinnerGrid(cells, ROWS, COLS, WIN_K);
  const xCount = cells.filter((c) => c === 'X').length;
  const oCount = cells.filter((c) => c === 'O').length;

  const finalShot = runCli(exe, [
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintWin?.title || 'Paint',
    '--output', finalShotAbs,
    '--json',
  ], 15_000);
  const finalBytes = existsSync(finalShotAbs) ? statSync(finalShotAbs).size : 0;
  rep.step('save final screenshot', finalShot.ms, `path=${finalShotRel}; bytes=${finalBytes}; exit=${finalShot.status}`);
  assert(finalShot.status === 0, `final screenshot CLI exits 0 (got ${finalShot.status}; stderr=${truncateText(finalShot.stderr, 200)})`);
  assert(existsSync(finalShotAbs), `final screenshot saved at ${finalShotAbs}`);
  // ~400 B per mark is a comfortable floor for small 4x4 strokes in JPEG;
  // need at least 2 KB regardless to avoid the "no agent ever drew" case.
  const inkFloor = Math.max(2 * 1024, turnNum * 400);
  assert(finalBytes > baselineBytes + inkFloor,
    `final screenshot reflects the played game (baseline=${baselineBytes}B, final=${finalBytes}B, +${finalBytes - baselineBytes}B, floor=+${inkFloor}B)`);

  rep.step('4x4 final state', 0,
    `cells=${cells.join('')}; X=${xCount}; O=${oCount}; winner=${finalWinner}; ` +
    `turns=${turnNum}; total_agent_cost=$${totalCost.toFixed(4)}; total_agent_rounds=${totalRounds}; ` +
    `screenshot=${finalShotRel}`);
  assert(['X', 'O', 'draw', 'ongoing'].includes(finalWinner),
    `4x4 reached a valid state (winner=${finalWinner}, cells=${cells.join('')})`);
  assert(xCount === oCount || xCount === oCount + 1,
    `move counts consistent for X-first play (X=${xCount}, O=${oCount})`);
  assert(turnNum >= WIN_K, `enough turns played to potentially win (turns=${turnNum})`);

  for (const l of tokenLines) rep.notes.push(l);

  // ── Inspect each agent's persisted memory ───────────────────────────────
  for (const [m, sid] of [['X', xSessionId], ['O', oSessionId]]) {
    const path = join(sessionsDir, `${sid}.json`);
    if (!existsSync(path)) {
      rep.notes.push(`agent ${m} session: no file at ${path}`);
      continue;
    }
    let p = null;
    try { p = JSON.parse(readFileSync(path, 'utf8')); } catch {}
    const memKeys = p?.memory_state && typeof p.memory_state === 'object'
      ? Object.keys(p.memory_state)
      : [];
    rep.notes.push(`agent ${m} memory: file=${sid}.json; events=${p?.events?.length ?? 0}; memory_state_keys=${JSON.stringify(memKeys)}`);
  }

  // Best-effort cleanup.
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'alt+f4', '--json']);
  await new Promise((r) => setTimeout(r, 600));
  runCli(exe, ['assistant', 'app-use', 'hotkey', '--keys', 'n', '--json']);

  rep.endSuite();
}

async function suiteLlmAgentWordpadTicTacToeBattle(rep, assert, exe, workDir) {
  console.log('\n-- LLM Agent: WordPad tic-tac-toe battle (X already played) --\n');
  rep.beginSuite('LLM agent WordPad tic-tac-toe battle');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped LLM agent WordPad tic-tac-toe battle: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('llm agent wordpad battle skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped LLM agent WordPad tic-tac-toe battle: no configured API key.');
    rep.step('llm agent wordpad battle skipped', 0, 'no configured API key');
    rep.endSuite();
    return;
  }
  const wordpad = wordpadExePath();
  if (!wordpad) {
    rep.notes.push('Skipped LLM agent WordPad tic-tac-toe battle: wordpad.exe not found (likely Win11 24H2+ where WordPad was removed).');
    rep.step('llm agent wordpad battle skipped', 0, 'wordpad.exe not found');
    rep.endSuite();
    return;
  }

  const logName = 'llm-agent-wordpad-tictactoe.json';
  const logPath = join(PACKAGE_ROOT, logName);
  const screenshotName = 'wordpad_battle_final.jpg';
  cleanAgentArtifacts([logName, screenshotName]);

  // ── pre-setup using the NEW first-class tools (via CLI for the harness) ──
  // 3-line visual board. type_text() in app_use translates '\n' to VK_RETURN
  // so the lines come through in WordPad's RichEdit.
  const initialBoard = '.|.|.\n.|X|.\n.|.|.';

  const openRun = runCli(exe, [
    'assistant', 'app-use', 'open-app',
    '--exe', wordpad,
    '--wait-ms', '5000',
    '--x', '140', '--y', '90',
    '--width', '640', '--height', '600',
    '--json',
  ], 30_000);
  const openDoc = parseJson(openRun.stdout);
  rep.step('open wordpad', openRun.ms, `exit=${openRun.status}; pid=${openDoc.pid}; hwnd=${openDoc.hwnd}`);
  assert(openRun.status === 0, `app-use open-app wordpad exits 0 (got ${openRun.status}); stderr: ${truncateText(openRun.stderr, 300)}`);
  assert(openDoc.ok === true && openDoc.pid > 0, 'app-use open-app returns a WordPad process id');

  await new Promise((r) => setTimeout(r, 900));

  const setupBatch = runCli(exe, [
    'assistant', 'app-use', 'batch',
    '--file', writeTempBatch(workDir, 'wordpad-setup.batch.json', {
      steps: [
        { action: 'activate', title: 'WordPad', delayMs: 700 },
        { action: 'hotkey', keys: 'ctrl+a', delayMs: 250 },
        { action: 'hotkey', keys: 'delete', delayMs: 250 },
        { action: 'type', text: initialBoard, delayMs: 500 },
      ],
    }),
    '--json', '--default-delay-ms', '60',
  ], 30_000);
  const setupDoc = parseJson(setupBatch.stdout);
  rep.step('draw board + first X', setupBatch.ms, `exit=${setupBatch.status}; steps=${setupDoc.steps?.length ?? 0}`);
  assert(setupBatch.status === 0 && setupDoc.ok === true, `wordpad setup batch ok (got status=${setupBatch.status})`);
  assert(Array.isArray(setupDoc.steps) && setupDoc.steps.every((s) => s.ok === true), 'wordpad setup marks every step ok');

  await new Promise((r) => setTimeout(r, 600));

  const preRead = readWordpadDocumentText(exe);
  const preText = preRead.text;
  const preCells = parseTicTacToeBoard(preText);
  rep.step('verify pre board', 0, `cells=${preCells.join('') || '(empty)'}; text=${JSON.stringify(truncateText(preText, 60))}`);
  assert(preCells.length === 9, `pre board has 9 cells (got ${preCells.length}; text=${JSON.stringify(truncateText(preText, 300))})`);
  assert(preCells[4] === 'X', `pre board has X in center cell 5 (cells=${JSON.stringify(preCells)})`);
  assert(preCells.filter((c) => c === 'X').length === 1, 'pre board has exactly one X');
  assert(preCells.filter((c) => c === 'O').length === 0, 'pre board has zero O');

  // ── agent call: finish the game ─────────────────────────────────────────
  const prompt = [
    'You are playing tic-tac-toe in WordPad. Continue the game until a winner or a draw, alternating moves between X and O.',
    'The WordPad document holds the board as three rows; each row is three cells separated by "|". Cells use "." for empty, "X" for an X move, "O" for an O move. Read cells row by row left to right, positions 1..9 like a phone keypad.',
    'Right now WordPad contains three rows: ".|.|." then ".|X|." then ".|.|." (X already played the center). It is O\'s turn first.',
    'Per turn, do exactly:',
    '  1. app_inspect_dump with title "WordPad". The dump shows each cell row and uses the two-character sequence \\n where WordPad has a line break.',
    '  2. Parse the 9 cells. If count(X) == count(O), X moves; if count(X) == count(O) + 1, O moves.',
    '  3. Choose the next empty cell by this priority: (a) finish your own three-in-a-row, (b) block opponent\'s three-in-a-row, (c) center, (d) any corner (1,3,7,9), (e) any side (2,4,6,8).',
    '  4. Compose the NEW three-row board, with only that one cell changed, as a single JSON string that contains a real newline between rows. In your tool-call JSON write it like "X|.|.\\n.|O|.\\n.|.|." (the JSON escape \\n is ONE character, a newline, which is what WordPad needs).',
    '  5. app_hotkey {"keys":"ctrl+a","title":"WordPad"}',
    '  6. app_hotkey {"keys":"delete"}',
    '  7. app_type {"text": <new board>}.  Do NOT pass literal backslash + n; do NOT pass spaces between rows.',
    '  8. app_inspect_dump again. Verify the new 9 cells match what you intended (the dump will still show \\n between rows, that\'s correct).',
    '  9. Check for a winner (three of the same in a row, column, or diagonal) or a full-board draw. If the game is over, stop. Otherwise continue.',
    `When the game ends, call app_screenshot with title "WordPad" and output_path ${JSON.stringify(screenshotName)}, then answer with exactly one line: GAME OVER: X, GAME OVER: O, or GAME OVER: DRAW.`,
    'Use only app_inspect_dump, app_hotkey, app_type, app_screenshot. Do not use run, write_file, schedule, or any other tool. Do not close WordPad.',
  ].join(' ');

  const r = await runCliStreaming(exe, [
    '--cwd', '.',
    '--log-level', 'trace',
    'llm', 'agent',
    '--prompt', prompt,
    '--single-turn',
    '--max-iter', '60',
    '--log', `./${logName}`,
  ], 360_000, 'llm wordpad battle', PACKAGE_ROOT);

  const doc = readAgentResult(logPath);
  const toolCalls = readAgentToolCalls(logPath);
  rep.step('llm agent play to completion', r.ms, `exit=${r.status}; ok=${doc?.ok}; tools=${toolCalls.length}; ${toolCalls.join(',') || '(none)'}`);
  assert(r.status === 0, `llm WordPad agent exits 0 (got ${r.status}); stderr: ${truncateText(r.stderr, 600)}`);
  assert(doc?.ok === true, `llm WordPad agent JSON ok=true (got ${doc?.ok}); error=${truncateText(doc?.error, 300)}`);
  assert(toolCalls.includes('app_inspect_dump'), `agent used app_inspect_dump (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('app_type'), `agent used app_type (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('app_hotkey'), `agent used app_hotkey (tools=${JSON.stringify(toolCalls)})`);
  assert(toolCalls.includes('app_screenshot'), `agent used app_screenshot (tools=${JSON.stringify(toolCalls)})`);
  assert(existsSync(join(PACKAGE_ROOT, screenshotName)) && statSync(join(PACKAGE_ROOT, screenshotName)).size > 1024,
    `agent saved ${screenshotName} with a non-trivial image`);

  // ── verify final board state via the WordPad RichEdit document value ────
  const finalRead = readWordpadDocumentText(exe);
  const finalText = finalRead.text;
  const finalCells = parseTicTacToeBoard(finalText);
  const winner = detectTicTacToeWinner(finalCells);
  const xCount = finalCells.filter((c) => c === 'X').length;
  const oCount = finalCells.filter((c) => c === 'O').length;
  rep.step('verify final board', 0, `cells=${finalCells.join('')}; X=${xCount}; O=${oCount}; winner=${winner}; text=${JSON.stringify(truncateText(finalText, 60))}`);
  assert(finalCells.length === 9, `final board has 9 cells (got ${finalCells.length}; text=${JSON.stringify(truncateText(finalText, 300))})`);
  assert(winner === 'X' || winner === 'O' || winner === 'draw',
    `final board reaches a terminal state (winner=${winner}, cells=${JSON.stringify(finalCells)})`);
  assert(xCount >= 2 && oCount >= 1, `both players moved at least once (X=${xCount}, O=${oCount})`);
  assert(xCount === oCount || xCount === oCount + 1, `move counts are consistent for X-first play (X=${xCount}, O=${oCount})`);

  rep.endSuite();
}

function writeTempBatch(workDir, name, obj) {
  const path = join(workDir, name);
  writeFileSync(path, `${JSON.stringify(obj, null, 2)}\n`, 'utf8');
  return path;
}

async function suiteLibreOfficeCalc(rep, assert, exe, workDir) {
  console.log('\n-- App Use: LibreOffice Calc formula --\n');
  rep.beginSuite('App use LibreOffice Calc formula');

  const soffice = libreOfficeExePath();
  if (!soffice) {
    rep.notes.push('Skipped LibreOffice Calc app-use test: soffice.exe was not found.');
    rep.step('libreoffice calc skipped', 0, 'soffice.exe not found');
    rep.endSuite();
    return;
  }

  const profileDir = join(workDir, 'lo-profile');
  mkdirSync(profileDir, { recursive: true });
  const batchPath = join(workDir, 'app-use-libreoffice-calc.batch.json');
  const tableText = ['1', '2', '3', '4', '5', '=SUM(A1:A5)'].join('\n');
  writeFileSync(
    batchPath,
    `${JSON.stringify(
      {
        steps: [
          {
            action: 'open-app',
            exe: soffice,
            args: `--calc --norestore --nolockcheck --nofirststartwizard`,
            waitMs: 9000,
            x: 90,
            y: 90,
            width: 980,
            height: 720,
            delayMs: 1800,
          },
          { action: 'click-element', name: 'OK', controlType: 'Button', optional: true, timeoutMs: 800, delayMs: 200 },
          { action: 'click-element', name: 'Skip', controlType: 'Button', optional: true, timeoutMs: 800, delayMs: 200 },
          { action: 'hotkey', keys: 'escape', delayMs: 250 },
          { action: 'click', x: 50, y: 155, windowRelative: true, delayMs: 150 },
          { action: 'clipboard-set', text: tableText, delayMs: 50 },
          { action: 'hotkey', keys: 'ctrl+v', delayMs: 700 },
          { action: 'hotkey', keys: 'escape', delayMs: 150 },
          { action: 'click', x: 50, y: 255, windowRelative: true, delayMs: 250 },
          { action: 'hotkey', keys: 'ctrl+c', delayMs: 500 },
          { action: 'clipboard-get', delayMs: 50 },
          { action: 'hotkey', keys: 'alt+f4', delayMs: 700 },
          { action: 'hotkey', keys: 'n', delayMs: 100 },
        ],
      },
      null,
      2,
    )}\n`,
    'utf8',
  );

  const r = runCli(exe, ['assistant', 'app-use', 'batch', '--file', batchPath, '--json', '--default-delay-ms', '25'], 60_000);
  const doc = parseJson(r.stdout);
  rep.step('libreoffice calc formula', r.ms, `exit=${r.status}; steps=${doc.steps?.length ?? 0}`);
  assert(r.status === 0, `app-use LibreOffice Calc batch exits 0 (got ${r.status}); stderr: ${r.stderr.slice(0, 300)}`);
  assert(doc.ok === true, 'app-use LibreOffice Calc batch reports ok=true');
  assert(Array.isArray(doc.steps) && doc.steps.length === 13, 'app-use LibreOffice Calc batch reports all thirteen steps');
  assert(doc.steps.filter((step) => step.action !== 'click-element').every((step) => step.ok === true), 'app-use LibreOffice Calc marks all required steps ok');

  const clipboardText = String(doc.steps.find((step) => step.action === 'clipboard-get')?.data?.text || '');
  assert(/(^|[^\d])15([^\d]|$)/.test(clipboardText), `LibreOffice Calc SUM result is copied as 15 (clipboard=${JSON.stringify(clipboardText)})`);

  rep.endSuite();
}

// Locate freepiano.exe. Search order:
//   1. FREEPIANO_EXE env var (explicit override)
//   2. Known good install on the dev box
//   3. PATH (`where.exe`)
//   4. Common install / portable-zip locations under %USERPROFILE%\Desktop and
//      %LOCALAPPDATA%\Programs (FreePiano ships as a portable zip, so the dev
//      may have it anywhere; we try the obvious spots before giving up).
function freepianoExePath() {
  const env = process.env.FREEPIANO_EXE;
  if (env && env.trim() && existsSync(env.trim())) return resolve(env.trim());
  const candidates = [
    'C:\\Users\\zx\\Desktop\\freepiano_2.2.2_win32\\freepiano.exe',
    join(process.env.USERPROFILE || '', 'Desktop', 'freepiano_2.2.2_win32', 'freepiano.exe'),
    join(process.env.USERPROFILE || '', 'Desktop', 'FreePiano', 'freepiano.exe'),
    join(process.env.LOCALAPPDATA || '', 'Programs', 'FreePiano', 'freepiano.exe'),
    join(process.env.PROGRAMFILES || 'C:\\Program Files', 'FreePiano', 'freepiano.exe'),
    join(process.env['PROGRAMFILES(X86)'] || 'C:\\Program Files (x86)', 'FreePiano', 'freepiano.exe'),
  ];
  for (const c of candidates) {
    if (c && existsSync(c)) return c;
  }
  const where = spawnSync('where.exe', ['freepiano.exe'], { encoding: 'utf8', timeout: 5000 });
  if ((where.status ?? -1) === 0) {
    const first = String(where.stdout || '')
      .split(/\r?\n/)
      .map((s) => s.trim())
      .find((s) => s && existsSync(s));
    if (first) return first;
  }
  return '';
}

// Read full tool-call events (name + payload + envelope) from the agent log.
// Used by the piano test to inspect the app_batch report the agent received,
// not just which tools it called.
function readAgentToolEvents(logPath) {
  if (!existsSync(logPath)) return [];
  const events = JSON.parse(readFileSync(logPath, 'utf8')).events || [];
  return events.filter((ev) => ev.kind === 'tool_call' || ev.kind === 'tool_result');
}

// FreePiano default keymap. Each entry is { key: KEYBOARD-LETTER, note: 'C3' }.
// The a-row plays one octave starting at C3 (C major scale: a s d f g h j k).
// The q-row plays one octave starting at C4. The z-row plays one octave at C2.
// Reference: freepiano_2.2.2_win32/keymap/freepiano.map.
const FREEPIANO_KEYS = {
  // a-row C3..G4
  C3: 'a', D3: 's', E3: 'd', F3: 'f', G3: 'g', A3: 'h', B3: 'j',
  C4_aRow: 'k', D4_aRow: 'l',
  // q-row C4..G5 (we prefer this for the higher octave)
  C4: 'q', D4: 'w', E4: 'e', F4: 'r', G4: 't', A4: 'y', B4: 'u',
  C5: 'i', D5: 'o', E5: 'p',
};

async function suitePiano(rep, assert, exe, workDir) {
  console.log('\n-- Piano: app-use batch + key-press timing against freepiano.exe --\n');
  rep.beginSuite('Piano (FreePiano batched keystrokes + key-press timing)');

  if (process.platform !== 'win32') {
    rep.notes.push('Skipped piano test: Win32-only.');
    rep.step('piano skipped', 0, 'non-windows');
    rep.endSuite();
    return;
  }

  const fpExe = freepianoExePath();
  if (!fpExe) {
    rep.notes.push(
      'Skipped piano test: freepiano.exe not found. Install FreePiano (https://freepiano.tiwb.com/) or '
        + 'set FREEPIANO_EXE to its absolute path.',
    );
    rep.step('piano skipped', 0, 'freepiano.exe not found');
    rep.endSuite();
    return;
  }
  console.log(`  freepiano.exe: ${fpExe}`);
  rep.step('freepiano.exe located', 0, fpExe);

  // Fresh start: kill any lingering FreePiano so we own the foreground window.
  spawnSync('taskkill.exe', ['/IM', 'freepiano.exe', '/F'], { timeout: 5000 });
  await new Promise((r) => setTimeout(r, 400));

  // ── Phase A: harness-direct via `app-use batch` ──────────────────────────
  console.log('\n  -- Phase A: harness-direct batch (C major scale, hold_ms=300) --');

  const launch = runCli(
    exe,
    ['assistant', 'app-use', 'open-app', '--exe', fpExe, '--x', '120', '--y', '120', '--width', '1280', '--height', '720', '--wait-ms', '4000', '--json'],
    20_000,
  );
  rep.step('open freepiano (phase A)', launch.ms, `exit=${launch.status}`);
  assert(launch.status === 0, `app-use open-app freepiano exits 0 (got ${launch.status}); stderr=${truncateText(launch.stderr, 300)}`);
  await new Promise((r) => setTimeout(r, 600));

  // Resolve the EXACT Freepiano main window. We do this once via app-inspect
  // dump --process so the batch step targets a known hwnd, immune to title
  // ambiguity (e.g. a file-explorer window with "freepiano" in its title that
  // would otherwise outrank the piano in title-substring search).
  const fpDump = runCli(exe, ['assistant', 'app-inspect', 'dump', '--process', 'freepiano.exe', '--limit', '5', '--json'], 8_000);
  assert(fpDump.status === 0, `resolve freepiano window: dump exits 0 (got ${fpDump.status}); stderr=${truncateText(fpDump.stderr, 300)}`);
  const fpDoc = parseJson(fpDump.stdout);
  const fpWindow = (fpDoc.windows || []).find((w) => /freepiano/i.test(String(w?.title || ''))) || (fpDoc.windows || [])[0];
  assert(fpWindow && typeof fpWindow.hwnd === 'number', 'freepiano window resolved with numeric hwnd');
  const fpHwnd = fpWindow.hwnd;
  console.log(`  Freepiano hwnd=${fpHwnd}  title='${fpWindow.title}'`);
  rep.step('resolve freepiano hwnd', fpDump.ms, `hwnd=${fpHwnd} title='${fpWindow.title}'`);

  // Build a batch that plays a C major scale (8 notes: C D E F G A B C).
  // The first key-press's elapsedMs proves hold_ms is honored end-to-end.
  // We use a-row keys for C3..B3 plus q-row C4 for the octave.
  const NOTES_PHASE_A = [
    { note: 'C3', key: FREEPIANO_KEYS.C3 },
    { note: 'D3', key: FREEPIANO_KEYS.D3 },
    { note: 'E3', key: FREEPIANO_KEYS.E3 },
    { note: 'F3', key: FREEPIANO_KEYS.F3 },
    { note: 'G3', key: FREEPIANO_KEYS.G3 },
    { note: 'A3', key: FREEPIANO_KEYS.A3 },
    { note: 'B3', key: FREEPIANO_KEYS.B3 },
    { note: 'C4', key: FREEPIANO_KEYS.C4 },
  ];
  const HOLD_MS_A = 300;
  const DELAY_MS_A = 80;
  const batchDoc = {
    default_delay_ms: 20,
    steps: [
      { action: 'activate', hwnd: fpHwnd, delayMs: 400 },
      ...NOTES_PHASE_A.map((n) => ({ action: 'key-press', key: n.key, hold_ms: HOLD_MS_A, delayMs: DELAY_MS_A })),
    ],
  };
  const batchPath = join(workDir, 'piano-scale.json');
  writeFileSync(batchPath, JSON.stringify(batchDoc, null, 2), 'utf8');

  const batchRun = runCli(exe, ['assistant', 'app-use', 'batch', '--file', batchPath, '--json'], 30_000);
  rep.step('phase A batch: c major scale', batchRun.ms, `exit=${batchRun.status}`);
  assert(batchRun.status === 0, `phase A batch exits 0 (got ${batchRun.status}); stderr=${truncateText(batchRun.stderr, 400)}`);
  const batchReport = parseJson(batchRun.stdout);
  assert(batchReport.ok === true, 'phase A batch report ok=true');
  const phaseASteps = Array.isArray(batchReport.steps) ? batchReport.steps : [];
  assert(phaseASteps.length === 1 + NOTES_PHASE_A.length, `phase A batch reports ${1 + NOTES_PHASE_A.length} steps (got ${phaseASteps.length})`);

  // Validate the new key-press timing feature: each key-press step must report
  // elapsedMs >= hold_ms, proving the harness honored the requested sustain.
  const keyPressSteps = phaseASteps.filter((s) => s.action === 'key-press');
  assert(keyPressSteps.length === NOTES_PHASE_A.length, `phase A has ${NOTES_PHASE_A.length} key-press steps (got ${keyPressSteps.length})`);
  for (let i = 0; i < keyPressSteps.length; i++) {
    const s = keyPressSteps[i];
    const note = NOTES_PHASE_A[i].note;
    assert(s.ok === true, `phase A step ${i + 1} (${note}, key=${NOTES_PHASE_A[i].key}) ok=true (got ok=${s.ok}, err=${truncateText(s.error, 200)})`);
    // hold_ms is the floor; the SendInput + sleep + SendInput chain adds a few
    // ms of OS overhead. Cap at hold_ms + 250 to catch a missing sleep too.
    const elapsed = Number(s.elapsedMs ?? 0);
    assert(elapsed >= HOLD_MS_A, `phase A step ${i + 1} (${note}) elapsedMs >= hold_ms (${HOLD_MS_A}); got ${elapsed}`);
    assert(elapsed <= HOLD_MS_A + 500, `phase A step ${i + 1} (${note}) elapsedMs not absurdly long; got ${elapsed} (cap ${HOLD_MS_A + 500})`);
    assert(s.data?.hold_ms === HOLD_MS_A, `phase A step ${i + 1} (${note}) echoes hold_ms=${HOLD_MS_A} (got ${s.data?.hold_ms})`);
    assert(s.data?.key === NOTES_PHASE_A[i].key, `phase A step ${i + 1} (${note}) echoes key=${NOTES_PHASE_A[i].key} (got ${s.data?.key})`);
  }
  console.log(`  phase A: ${keyPressSteps.length} notes played, elapsedMs per note = [${keyPressSteps.map((s) => Number(s.elapsedMs).toFixed(0)).join(', ')}] ms`);

  // Visual evidence: screenshot the FreePiano window. The new --activate
  // default brings it to the foreground for an accurate capture.
  const shotA = join(workDir, 'freepiano-after-scale.jpg');
  const shotResA = runCli(exe, ['assistant', 'app-inspect', 'screenshot', '--pid', String(fpWindow.pid), '--output', shotA, '--json'], 15_000);
  rep.step('phase A screenshot', shotResA.ms, `exit=${shotResA.status}; out=${shotA}`);
  assert(shotResA.status === 0 && existsSync(shotA), `phase A screenshot saved (status=${shotResA.status})`);

  // ── Phase A visual proof: held key produces a UI highlight ───────────────
  // Pure SendInput timing isn't enough -- if the OS dispatches our event but
  // the target app drops it (e.g. wScan=0 silently filtered by an audio app
  // like FreePiano), the batch still reports ok=true with the right elapsedMs
  // and we'd never know. We compare two BASELINE screenshots (no key held)
  // to learn the JPEG re-encode noise floor, then capture a HELD screenshot
  // during a 600 ms key-down and assert the held-vs-baseline delta clears
  // 3 x noise (with a hard floor of 300 B). FreePiano highlights pressed
  // keys on the on-screen keymap + lights up the corresponding piano key +
  // animates a green VU meter, which together shift several hundred bytes
  // of JPEG when the key actually registered.
  const baseline1 = join(workDir, 'freepiano-baseline-1.jpg');
  const baseline2 = join(workDir, 'freepiano-baseline-2.jpg');
  const heldShot  = join(workDir, 'freepiano-held.jpg');
  const visualBatch = {
    default_delay_ms: 30,
    steps: [
      { action: 'activate', hwnd: fpHwnd, delayMs: 400 },
      { action: 'screenshot', x: fpWindow.rect.x, y: fpWindow.rect.y, w: fpWindow.rect.w, h: fpWindow.rect.h, output: baseline1, delayMs: 300 },
      { action: 'screenshot', x: fpWindow.rect.x, y: fpWindow.rect.y, w: fpWindow.rect.w, h: fpWindow.rect.h, output: baseline2, delayMs: 200 },
      { action: 'key-down', key: FREEPIANO_KEYS.C3, delayMs: 500 }, // hold 'a' = C3, give FreePiano time to repaint
      { action: 'screenshot', x: fpWindow.rect.x, y: fpWindow.rect.y, w: fpWindow.rect.w, h: fpWindow.rect.h, output: heldShot, delayMs: 100 },
      { action: 'key-up',   key: FREEPIANO_KEYS.C3, delayMs: 100 },
    ],
  };
  const visualPath = join(workDir, 'piano-visual-proof.json');
  writeFileSync(visualPath, JSON.stringify(visualBatch, null, 2), 'utf8');
  const visualRun = runCli(exe, ['assistant', 'app-use', 'batch', '--file', visualPath, '--json'], 20_000);
  rep.step('phase A visual proof', visualRun.ms, `exit=${visualRun.status}`);
  assert(visualRun.status === 0, `phase A visual batch exits 0 (got ${visualRun.status}); stderr=${truncateText(visualRun.stderr, 300)}`);
  assert(existsSync(baseline1) && existsSync(baseline2) && existsSync(heldShot),
    'phase A visual proof: all three screenshots (2 baseline + held) exist');
  const b1Size = statSync(baseline1).size;
  const b2Size = statSync(baseline2).size;
  const heldSize = statSync(heldShot).size;
  const baselineNoise = Math.abs(b2Size - b1Size);
  const heldDelta = Math.abs(heldSize - b1Size);
  const threshold = Math.max(300, baselineNoise * 3);
  console.log(`  visual proof: baseline1=${b1Size}B  baseline2=${b2Size}B  held=${heldSize}B  noise=${baselineNoise}B  heldDelta=${heldDelta}B  threshold=${threshold}B`);
  assert(heldDelta >= threshold,
    `phase A visual proof: held-note screenshot differs from baseline by >= ${threshold}B `
    + `(got ${heldDelta}B, noise floor ${baselineNoise}B). If this is at or below noise, FreePiano is NOT `
    + `receiving our SendInput -- check scancode handling in key_input() and that find_window picked the real piano `
    + `(not e.g. a file-explorer window with "freepiano" in its title).`);

  // Tear down phase A FreePiano so phase B starts from a clean slate.
  spawnSync('taskkill.exe', ['/IM', 'freepiano.exe', '/F'], { timeout: 5000 });
  await new Promise((r) => setTimeout(r, 400));

  // ── Phase B: LLM agent composes the batch ────────────────────────────────
  console.log('\n  -- Phase B: LLM agent plays Mary Had a Little Lamb (E D C D E E E) --');

  if (String(process.env.MEDIA_IMG_TEST_APP_USE_LLM_AGENT || '1').trim() === '0') {
    rep.notes.push('Skipped piano LLM agent phase: MEDIA_IMG_TEST_APP_USE_LLM_AGENT=0.');
    rep.step('phase B skipped', 0, 'disabled by env');
    rep.endSuite();
    return;
  }
  if (!llmAgentDryRunHasConfiguredKey(exe, workDir)) {
    rep.notes.push('Skipped piano LLM agent phase: no configured API key.');
    rep.step('phase B skipped', 0, 'no API key configured');
    rep.endSuite();
    return;
  }

  // Launch FreePiano for the agent to play with.
  const launchB = runCli(
    exe,
    ['assistant', 'app-use', 'open-app', '--exe', fpExe, '--x', '120', '--y', '120', '--width', '1280', '--height', '720', '--wait-ms', '4000', '--json'],
    20_000,
  );
  rep.step('open freepiano (phase B)', launchB.ms, `exit=${launchB.status}`);
  assert(launchB.status === 0, `phase B: open-app exits 0 (got ${launchB.status})`);
  await new Promise((r) => setTimeout(r, 600));

  // Resolve the real hwnd again (new process from phase A teardown).
  const fpDumpB = runCli(exe, ['assistant', 'app-inspect', 'dump', '--process', 'freepiano.exe', '--limit', '5', '--json'], 8_000);
  const fpDocB = parseJson(fpDumpB.stdout);
  const fpWindowB = (fpDocB.windows || []).find((w) => /freepiano/i.test(String(w?.title || ''))) || (fpDocB.windows || [])[0];
  assert(fpWindowB && typeof fpWindowB.hwnd === 'number', 'phase B: freepiano window resolved');
  const fpHwndB = fpWindowB.hwnd;
  console.log(`  Freepiano hwnd=${fpHwndB}  title='${fpWindowB.title}'`);

  // Pre-shot for the agent to see the silent keyboard.
  const preShot = join(workDir, 'freepiano-pre-agent.jpg');
  runCli(exe, ['assistant', 'app-inspect', 'screenshot', '--pid', String(fpWindowB.pid), '--output', preShot, '--json'], 15_000);

  const HOLD_MS_B = 250;
  const DELAY_MS_B = 80;
  // Mary Had a Little Lamb first phrase: E D C D E E E.
  // a-row keymap: a=C3, s=D3, d=E3. So melody = d s a s d d d.
  const NOTES_PHASE_B = [
    { note: 'E3', key: FREEPIANO_KEYS.E3 },
    { note: 'D3', key: FREEPIANO_KEYS.D3 },
    { note: 'C3', key: FREEPIANO_KEYS.C3 },
    { note: 'D3', key: FREEPIANO_KEYS.D3 },
    { note: 'E3', key: FREEPIANO_KEYS.E3 },
    { note: 'E3', key: FREEPIANO_KEYS.E3 },
    { note: 'E3', key: FREEPIANO_KEYS.E3 },
  ];
  const melodySpec = NOTES_PHASE_B.map((n) => `${n.note}=${n.key}`).join(', ');
  const postShot = join(workDir, 'freepiano-post-agent.jpg');
  const logPath = join(PACKAGE_ROOT, 'llm-agent-piano.json');
  cleanAgentArtifacts(['llm-agent-piano.json']);

  // ASCII-only prompt -- non-ASCII in PowerShell argv has historically caused
  // crashes when the C++ CLI's JSON parser sees mangled UTF-16.
  const prompt = [
    'You are driving Wispow Freepiano (a virtual keyboard piano) on Windows.',
    `The Freepiano window is OPEN and visible at hwnd=${fpHwndB} (pid=${fpWindowB.pid}).`,
    'Always target it by HWND (not by title substring -- a file explorer with the install folder',
    'open also has "freepiano" in its title and would steal the focus).',
    'Freepiano maps PHYSICAL keyboard keys to MIDI notes. Each KEYDOWN = NoteOn, KEYUP = NoteOff,',
    'so the time the key is held DOWN is the note sustain.',
    '',
    'Your task: play "Mary Had a Little Lamb" first phrase -- the seven notes E D C D E E E --',
    `using these key mappings (verbatim): ${melodySpec}.`,
    `Each note should sustain for hold_ms=${HOLD_MS_B} ms with delayMs=${DELAY_MS_B} ms rest between notes.`,
    '',
    'You MUST do this in EXACTLY ONE app_batch call. The batch must contain:',
    `  1. one {"action":"activate","hwnd":${fpHwndB},"delayMs":300} step FIRST (foreground guarantee), then`,
    `  2. one {"action":"key-press","key":"<letter>","hold_ms":${HOLD_MS_B},"delayMs":${DELAY_MS_B}} step per note, in order.`,
    'Do NOT use type / hotkey / individual app_click steps. Use key-press exclusively for the notes.',
    '',
    `Then call app_screenshot with {"process":"freepiano.exe","output_path":${JSON.stringify(postShot)}} to capture visual evidence.`,
    '',
    'Allowed tools: app_batch, app_screenshot. Nothing else.',
    'Final answer: exactly "DONE" once the batch report says ok=true and the screenshot was saved.',
  ].join(' ');

  const r = await runCliStreaming(
    exe,
    [
      '--cwd', '.',
      '--log-level', 'trace',
      'llm', 'agent',
      '--prompt', prompt,
      '--include', preShot,
      '--single-turn',
      '--max-iter', '6',
      '--log', './llm-agent-piano.json',
    ],
    240_000,
    'llm piano agent',
    PACKAGE_ROOT,
  );

  const result = readAgentResult(logPath);
  const toolCalls = readAgentToolCalls(logPath);
  const toolEvents = readAgentToolEvents(logPath);
  rep.step('phase B agent run', r.ms, `exit=${r.status}; ok=${result?.ok}; tools=${toolCalls.join(',') || '(none)'}`);
  assert(r.status === 0, `phase B agent exits 0 (got ${r.status}); stderr=${truncateText(r.stderr, 600)}`);
  assert(result?.ok === true, `phase B agent JSON ok=true (got ${result?.ok}); error=${truncateText(result?.error, 300)}`);
  assert(toolCalls.includes('app_batch'), `phase B agent used app_batch (tools=${JSON.stringify(toolCalls)})`);

  // Find the app_batch tool_result the agent received from us and validate
  // its per-step report. This proves the agent assembled a CORRECT batch
  // (right keys, right hold_ms) and that timing held end-to-end through the
  // tool-call boundary. The log shape is:
  //   { kind: "tool_result", tool: "app_batch",
  //     detail: { envelope: { ok, steps: [...] }, id, duration_ms } }
  const batchResults = toolEvents.filter((ev) => ev.kind === 'tool_result' && ev.tool === 'app_batch');
  assert(batchResults.length >= 1, `phase B: at least one app_batch tool_result event (got ${batchResults.length})`);
  if (batchResults.length >= 1) {
    const env = batchResults[batchResults.length - 1].detail?.envelope || {};
    const steps = Array.isArray(env.steps) ? env.steps : [];
    const keyPress = steps.filter((s) => s.action === 'key-press');
    assert(env.ok === true, `phase B agent's batch ok=true (got ${env.ok})`);
    assert(keyPress.length === NOTES_PHASE_B.length, `phase B agent issued ${NOTES_PHASE_B.length} key-press steps (got ${keyPress.length})`);
    let mismatched = 0;
    for (let i = 0; i < Math.min(keyPress.length, NOTES_PHASE_B.length); i++) {
      const s = keyPress[i];
      const want = NOTES_PHASE_B[i].key;
      if (String(s?.data?.key ?? '').toLowerCase() !== want.toLowerCase()) mismatched++;
      const elapsed = Number(s.elapsedMs ?? 0);
      assert(s.ok === true, `phase B step ${i + 1} ok=true (got ok=${s.ok}, err=${truncateText(s.error, 200)})`);
      assert(elapsed >= HOLD_MS_B, `phase B step ${i + 1} (${NOTES_PHASE_B[i].note}) elapsedMs >= hold_ms=${HOLD_MS_B}; got ${elapsed}`);
      assert(Number(s?.data?.hold_ms) === HOLD_MS_B, `phase B step ${i + 1} (${NOTES_PHASE_B[i].note}) echoes hold_ms=${HOLD_MS_B} (got ${s?.data?.hold_ms})`);
    }
    assert(mismatched === 0, `phase B: agent used the correct keys for all notes (mismatched=${mismatched})`);
    console.log(`  phase B: agent played ${keyPress.length} notes; per-note elapsedMs = [${keyPress.map((s) => Number(s.elapsedMs).toFixed(0)).join(', ')}] ms`);
  }

  assert(existsSync(postShot), `phase B post-screenshot exists at ${postShot}`);

  // Cleanup.
  spawnSync('taskkill.exe', ['/IM', 'freepiano.exe', '/F'], { timeout: 5000 });

  rep.endSuite();
}

const startedAtIso = new Date().toISOString();
const metricsCollector = createMetricsCollector();
const stats = createAssert();
const rep = createTestReport();
const t0 = performance.now();
const exe = cliExePath();
const workDir = mkdtempSync(join(tmpdir(), 'pm-app-use-test-'));
const inspectOnly = process.argv.includes('--inspect-only');
const notepadOnly = process.argv.includes('--notepad-only');
const chromeOnly = process.argv.includes('--chrome-only');
const calculatorOnly = process.argv.includes('--calculator-only');
const terminalOnly = process.argv.includes('--terminal-only');
const llmAgentOnly = process.argv.includes('--llm-agent-only');
const llmChromeOnly = process.argv.includes('--llm-chrome-only');
const llmCalculatorOnly = process.argv.includes('--llm-calculator-only');
const paintTicTacToeOnly = process.argv.includes('--paint-tictactoe-only');
const llmPaintTicTacToeOnly = process.argv.includes('--llm-paint-tictactoe-only');
const llmWordpadTicTacToeBattleOnly = process.argv.includes('--llm-wordpad-tictactoe-battle-only');
const llmPaintTicTacToeBattleOnly = process.argv.includes('--llm-paint-tictactoe-battle-only');
const llmPaintTicTacToeA2AOnly = process.argv.includes('--llm-paint-tictactoe-a2a-only');
const llmPaintTicTacToeA2A4x4Only = process.argv.includes('--llm-paint-tictactoe-a2a-4x4-only');
const llmMemoryPersistOnly = process.argv.includes('--llm-memory-persist-only');
const llmPaintTicTacToeH2A4x4Only = process.argv.includes('--llm-paint-tictactoe-h2a-4x4-only');
const libreOfficeOnly = process.argv.includes('--libreoffice-only');
const pianoOnly = process.argv.includes('--piano-only');

try {
  mkdirSync(workDir, { recursive: true });
  if (process.platform !== 'win32') {
    rep.notes.push('Skipped: app-use/app-inspect are Win32 computer-use helpers.');
  } else if (inspectOnly) {
    await suiteInspect(rep, stats.assert, exe, workDir);
  } else if (notepadOnly) {
    await suiteNotepadWrite(rep, stats.assert, exe, workDir);
  } else if (chromeOnly) {
    await suiteChromeAmazon(rep, stats.assert, exe, workDir);
  } else if (calculatorOnly) {
    await suiteCalculatorResize(rep, stats.assert, exe, workDir);
  } else if (terminalOnly) {
    await suiteStartMenuTerminalDir(rep, stats.assert, exe, workDir);
  } else if (llmAgentOnly) {
    await suiteLlmAgentNotepad(rep, stats.assert, exe, workDir);
  } else if (llmChromeOnly) {
    await suiteLlmAgentChromeAmazon(rep, stats.assert, exe, workDir);
  } else if (llmCalculatorOnly) {
    await suiteLlmAgentCalculator(rep, stats.assert, exe, workDir);
  } else if (paintTicTacToeOnly) {
    await suitePaintTicTacToe(rep, stats.assert, exe, workDir);
  } else if (llmPaintTicTacToeOnly) {
    await suiteLlmAgentPaintTicTacToe(rep, stats.assert, exe, workDir);
  } else if (llmWordpadTicTacToeBattleOnly) {
    await suiteLlmAgentWordpadTicTacToeBattle(rep, stats.assert, exe, workDir);
  } else if (llmPaintTicTacToeBattleOnly) {
    await suiteLlmAgentPaintTicTacToeBattle(rep, stats.assert, exe, workDir);
  } else if (llmPaintTicTacToeA2AOnly) {
    await suiteLlmAgentPaintTicTacToeA2A(rep, stats.assert, exe, workDir);
  } else if (llmPaintTicTacToeA2A4x4Only) {
    await suiteLlmAgentPaintTicTacToeA2A4x4(rep, stats.assert, exe, workDir);
  } else if (llmMemoryPersistOnly) {
    await suiteLlmAgentMemoryPersistence(rep, stats.assert, exe, workDir);
  } else if (llmPaintTicTacToeH2A4x4Only) {
    await suiteLlmAgentPaintTicTacToeHumanVsAgent4x4(rep, stats.assert, exe, workDir);
  } else if (libreOfficeOnly) {
    await suiteLibreOfficeCalc(rep, stats.assert, exe, workDir);
  } else if (pianoOnly) {
    await suitePiano(rep, stats.assert, exe, workDir);
  } else {
    await suiteInspect(rep, stats.assert, exe, workDir);
    await suiteNotepadWrite(rep, stats.assert, exe, workDir);
    await suiteBatchNotepad(rep, stats.assert, exe, workDir);
  }
} catch (e) {
  console.error(e);
  rep.meta.abortReason = 'uncaught exception';
  rep.meta.uncaughtError = e?.stack || String(e);
} finally {
  rep.finalize(stats, performance.now() - t0, exe, { workDir });
  writeTestReportFile(rep, metricsCollector, startedAtIso);
  if (process.env.PM_KEEP_APP_USE_TEST_TMP !== '1') {
    try {
      rmSync(workDir, { recursive: true, force: true });
    } catch {
      /* ignore cleanup */
    }
  }
}

if (stats.failed > 0 || rep.meta.abortReason) {
  process.exitCode = 1;
}
