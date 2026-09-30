#!/usr/bin/env node
/**
 * XBlox CLI tests: block-tree execution and custom-command dispatch.
 *
 * Invoked via:
 *   npm run test:xblox
 */

import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
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
const XBLOX_FIXTURE_DIR = join(PACKAGE_ROOT, 'tests', 'xblox');

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
      displayName: 'xblox',
      testName: 'xblox',
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
    md += '\n---\n\n*Per-run: `tests/test-report-last.md` — this run is also a chapter in `tests/test-report-all.md`.*\n';
  } else {
    md += '\n---\n\n*Artifact: `tests/test-report-last.md` — overwritten on each test run.*\n';
  }
  writeFileSync(TEST_REPORT_PATH, md, 'utf8');
  if (acc) {
    appendMarkdownChapter(getChapterTitle(), md, { npmScript: getNpmScriptLabel() || undefined });
    console.log(`  (aggregated report chapter: ${TEST_REPORT_ALL_PATH})`);
  }
}

function writeBlocksFile(path, doc) {
  writeFileSync(path, `${JSON.stringify(doc, null, 2)}\n`, 'utf8');
}

function runXblox(exe, src, extraArgs = []) {
  const t0 = performance.now();
  const r = spawnSync(exe, ['xblox', '--log-level', 'off', 'run', '--json', '--no-wait', '--src', src, ...extraArgs], {
    cwd: PACKAGE_ROOT,
    encoding: 'utf8',
    timeout: 30_000,
  });
  return {
    status: r.status ?? -1,
    stdout: r.stdout || '',
    stderr: r.stderr || '',
    ms: performance.now() - t0,
  };
}

function parseTrailingJson(stdout) {
  const text = String(stdout || '').trim();
  const starts = [text.lastIndexOf('\n{'), text.indexOf('{')].filter((n) => n >= 0);
  for (const start of starts) {
    try {
      return JSON.parse(text.slice(text[start] === '{' ? start : start + 1));
    } catch {
      /* try next candidate */
    }
  }
  return JSON.parse(text);
}

function commandEvents(report) {
  return (report.events || []).filter((event) => event.kind === 'command');
}

function actionEvents(report, action) {
  return commandEvents(report).filter((event) => event.data?.action === action);
}

function eventsByKind(report, kind) {
  return (report.events || []).filter((event) => event.kind === kind);
}

function flowBlocksFile() {
  return {
    version: 1,
    context: {
      selection: { files: ['demo/input.png'] },
      mode: 'capture',
      hasQueuedWork: true,
    },
    roots: [
      {
        kind: 'runScript',
        id: 'metadata-noop',
        method: 'return host.runCustomCommand({"id":"metadata-noop","label":"Metadata Noop"});',
      },
      {
        kind: 'if',
        condition: '(this.selection?.files?.length ?? 0) > 0',
        consequent: [
          {
            kind: 'runScript',
            id: 'selection-branch',
            method: 'return host.runCustomCommand({"id":"app-chat","label":"Chat Noop","appCommand":"chat"});',
          },
        ],
        alternate: [
          {
            kind: 'runScript',
            id: 'selection-missing',
            method: 'return host.runCustomCommand({"id":"bad-branch","label":"Bad Branch","appCommand":"bad"});',
          },
        ],
      },
      {
        kind: 'if',
        condition: 'false',
        consequent: [
          {
            kind: 'runScript',
            id: 'false-branch',
            method: 'return host.runCustomCommand({"id":"bad-false","label":"Bad False","appCommand":"bad"});',
          },
        ],
        alternate: [
          {
            kind: 'runScript',
            id: 'alternate-branch',
            method: 'return host.runCustomCommand({"id":"settings-ribbon","label":"Settings Noop","ribbonCommand":"settings"});',
          },
        ],
      },
      {
        kind: 'switch',
        variable: 'this.mode',
        items: [
          {
            kind: 'case',
            comparator: '===',
            expression: "'capture'",
            consequent: [
              {
                kind: 'runScript',
                id: 'switch-capture',
                method: 'return host.runCustomCommand({"id":"capture-url","label":"Capture Url Noop","url":"https://polymech.info"});',
              },
            ],
          },
          {
            kind: 'switchDefault',
            consequent: [
              {
                kind: 'runScript',
                id: 'switch-default',
                method: 'return host.runCustomCommand({"id":"default-path","label":"Default Path Noop","path":"."});',
              },
            ],
          },
        ],
      },
      {
        kind: 'while',
        condition: 'host.hasQueuedWork()',
        loopLimit: 3,
        items: [
          { kind: 'wait', ms: 5 },
          {
            kind: 'runScript',
            id: 'while-noop',
            method: 'return host.runCustomCommand({"id":"while-meta","label":"While Metadata"});',
          },
          { kind: 'break' },
        ],
      },
    ],
  };
}

function realCommandBlocksFile(workDir) {
  const isWin = process.platform === 'win32';
  const externalCommand = isWin
    ? { mode: 'argv', command: 'cmd.exe', args: ['/c', 'dir', '/b'], cwd: workDir }
    : { mode: 'argv', command: 'ls', args: ['-1'], cwd: workDir };
  return {
    version: 1,
    roots: [
      {
        kind: 'runScript',
        id: 'list-dir',
        method: `return host.runCustomCommand(${JSON.stringify({
          id: 'list-dir',
          label: isWin ? 'Dir' : 'Ls',
          externalCommand,
        })});`,
      },
    ],
  };
}

function expressionBlocksFile() {
  return {
    version: 1,
    context: {
      selection: { files: ['demo/input.png', 'demo/input-2.png'] },
      score: 7,
      modeNum: 2,
      hasQueuedWork: true,
    },
    roots: [
      {
        kind: 'if',
        condition: 'score * 2 >= 14',
        consequent: [
          {
            kind: 'runScript',
            id: 'expr-if-true',
            method: 'return host.runCustomCommand({"id":"expr-if-true","label":"Expression If","appCommand":"chat"});',
          },
        ],
        alternate: [
          {
            kind: 'runScript',
            id: 'expr-if-bad-alternate',
            method: 'return host.runCustomCommand({"id":"expr-if-bad-alternate","label":"Bad Alternate","appCommand":"bad"});',
          },
        ],
      },
      {
        kind: 'if',
        condition: 'selectionCount == 0',
        consequent: [
          {
            kind: 'runScript',
            id: 'expr-selection-bad',
            method: 'return host.runCustomCommand({"id":"expr-selection-bad","label":"Bad Selection","appCommand":"bad"});',
          },
        ],
        elseIfBlocks: [
          {
            condition: 'selectionCount == 2',
            consequent: [
              {
                kind: 'runScript',
                id: 'expr-elseif',
                method: 'return host.runCustomCommand({"id":"expr-elseif","label":"Expression Else If","ribbonCommand":"settings"});',
              },
            ],
          },
        ],
        alternate: [
          {
            kind: 'runScript',
            id: 'expr-alternate-bad',
            method: 'return host.runCustomCommand({"id":"expr-alternate-bad","label":"Bad Alternate","appCommand":"bad"});',
          },
        ],
      },
      {
        kind: 'switch',
        variable: 'modeNum + 1',
        items: [
          {
            kind: 'case',
            comparator: '===',
            expression: '2',
            consequent: [
              {
                kind: 'runScript',
                id: 'expr-switch-bad',
                method: 'return host.runCustomCommand({"id":"expr-switch-bad","label":"Bad Switch","appCommand":"bad"});',
              },
            ],
          },
          {
            kind: 'case',
            comparator: '>=',
            expression: '3',
            consequent: [
              {
                kind: 'runScript',
                id: 'expr-switch-numeric',
                method: 'return host.runCustomCommand({"id":"expr-switch-numeric","label":"Expression Switch","url":"https://polymech.info"});',
              },
            ],
          },
          {
            kind: 'switchDefault',
            consequent: [
              {
                kind: 'runScript',
                id: 'expr-switch-default-bad',
                method: 'return host.runCustomCommand({"id":"expr-switch-default-bad","label":"Bad Default","path":"."});',
              },
            ],
          },
        ],
      },
      {
        kind: 'while',
        condition: 'hasQueuedWork * (score > 6)',
        loopLimit: 3,
        items: [
          {
            kind: 'runScript',
            id: 'expr-while',
            method: 'return host.runCustomCommand({"id":"expr-while","label":"Expression While"});',
          },
          { kind: 'break' },
        ],
      },
    ],
  };
}

function contextBlocksFile() {
  return {
    version: 1,
    context: {
      seed: 3,
    },
    roots: [
      { kind: 'setVariable', name: 'score', expression: 'seed + 4' },
      { kind: 'getVariable', name: 'score', target: 'copiedScore' },
      { kind: 'setVariable', name: 'counter', value: 0 },
      {
        kind: 'for',
        variable: 'j',
        initial: '0',
        final: '3',
        comparator: '<',
        modifier: '+1',
        loopLimit: 10,
        items: [
          { kind: 'setVariable', name: 'counter', expression: 'counter + 1' },
        ],
      },
      { kind: 'getVariable', name: 'counter', target: 'copiedCounter' },
      { kind: 'log', level: 'info', message: 'copiedScore + copiedCounter' },
      {
        kind: 'if',
        condition: '(copiedScore == 7) * (copiedCounter == 3)',
        consequent: [
          {
            kind: 'runScript',
            id: 'context-if',
            method: 'return host.runCustomCommand({"id":"context-if","label":"Context If","appCommand":"chat"});',
          },
        ],
        alternate: [
          {
            kind: 'runScript',
            id: 'context-bad-alternate',
            method: 'return host.runCustomCommand({"id":"context-bad-alternate","label":"Bad Context","appCommand":"bad"});',
          },
        ],
      },
      { kind: 'setVariable', name: 'nested.total', expression: 'copiedScore * 2' },
      {
        kind: 'switch',
        variable: 'nested.total',
        items: [
          {
            kind: 'case',
            comparator: '>=',
            expression: '14',
            consequent: [
              {
                kind: 'runScript',
                id: 'context-switch',
                method: 'return host.runCustomCommand({"id":"context-switch","label":"Context Switch","url":"https://polymech.info"});',
              },
            ],
          },
          {
            kind: 'switchDefault',
            consequent: [
              {
                kind: 'runScript',
                id: 'context-default-bad',
                method: 'return host.runCustomCommand({"id":"context-default-bad","label":"Bad Default","path":"."});',
              },
            ],
          },
        ],
      },
    ],
  };
}

function runFlagsAbortBlocksFile() {
  return {
    version: 1,
    roots: [
      { kind: 'setVariable', value: 1 },
      { kind: 'setVariable', name: 'afterAbort', value: 1 },
    ],
  };
}

function runFlagsContinueBlocksFile() {
  return {
    version: 1,
    roots: [
      { kind: 'setVariable', value: 1, continueOnError: true },
      { kind: 'setVariable', name: 'afterContinue', value: 1 },
    ],
  };
}

function shellBlocksFile() {
  const isWin = process.platform === 'win32';
  return {
    version: 1,
    roots: [
      {
        kind: 'shell',
        shell: isWin ? 'pwsh' : 'sh',
        command: isWin ? 'Write-Output xblox-shell' : 'printf xblox-shell',
        timeoutMs: 30_000,
        storeAs: 'shellOut',
      },
      { kind: 'log', level: 'info', message: 'PREVIOUS' },
    ],
  };
}

async function suiteXbloxExpressions(rep, assert, exe, workDir) {
  console.log('\n── XBlox: muParser expressions ──\n');
  rep.beginSuite('XBlox muParser expressions');

  const expressionsPath = join(workDir, 'expressions.blocks.json');
  writeBlocksFile(expressionsPath, expressionBlocksFile());
  const r = runXblox(exe, expressionsPath);
  const report = parseTrailingJson(r.stdout);
  rep.step('xblox expression blocks', r.ms, `exit=${r.status}; events=${report.eventCount}`);

  assert(r.status === 0, `xblox expressions: exits 0 (got ${r.status}); stderr: ${r.stderr.slice(0, 300)}`);
  assert(report.ok === true, 'xblox expressions: report ok=true');
  assert(actionEvents(report, 'app').some((event) => event.data?.id === 'expr-if-true'), 'xblox expressions: arithmetic if condition runs consequent');
  assert(actionEvents(report, 'ribbon').some((event) => event.data?.id === 'expr-elseif'), 'xblox expressions: selectionCount else-if condition runs');
  assert(actionEvents(report, 'url').some((event) => event.data?.id === 'expr-switch-numeric'), 'xblox expressions: numeric switch comparison runs matching case');
  assert(actionEvents(report, 'metadata').some((event) => event.data?.id === 'expr-while'), 'xblox expressions: while condition runs metadata command');
  assert(!commandEvents(report).some((event) => String(event.data?.id || '').includes('bad')), 'xblox expressions: non-matching branches do not run bad commands');
  assert((report.events || []).some((event) => event.kind === 'break' && event.status === 'ok'), 'xblox expressions: while reaches break');

  rep.endSuite();
}

async function suiteXbloxContext(rep, assert, exe, workDir) {
  console.log('\n── XBlox: context scope variables ──\n');
  rep.beginSuite('XBlox context scope variables');

  const contextPath = join(workDir, 'context.blocks.json');
  writeBlocksFile(contextPath, contextBlocksFile());
  const r = runXblox(exe, contextPath);
  const report = parseTrailingJson(r.stdout);
  rep.step('xblox context blocks', r.ms, `exit=${r.status}; events=${report.eventCount}`);

  const setEvents = (report.events || []).filter((event) => event.kind === 'setVariable');
  const getEvents = (report.events || []).filter((event) => event.kind === 'getVariable');
  assert(r.status === 0, `xblox context: exits 0 (got ${r.status}); stderr: ${r.stderr.slice(0, 300)}`);
  assert(report.ok === true, 'xblox context: report ok=true');
  assert(setEvents.some((event) => event.data?.name === 'score' && event.data?.value === 7), 'xblox context: setVariable writes evaluated score');
  assert(getEvents.some((event) => event.data?.name === 'score' && event.data?.target === 'copiedScore' && event.data?.value === 7), 'xblox context: getVariable reads and copies score');
  assert(getEvents.some((event) => event.data?.name === 'counter' && event.data?.target === 'copiedCounter' && event.data?.value === 3), 'xblox context: for loop updates counter three times');
  assert((report.events || []).some((event) => event.kind === 'log' && event.data?.level === 'info' && event.data?.message === '10'), 'xblox context: log block evaluates message expression');
  assert(actionEvents(report, 'app').some((event) => event.data?.id === 'context-if'), 'xblox context: copied variables are available to if condition');
  assert(actionEvents(report, 'url').some((event) => event.data?.id === 'context-switch'), 'xblox context: nested variable is available to switch condition');
  assert(!commandEvents(report).some((event) => String(event.data?.id || '').includes('bad')), 'xblox context: non-matching context branches do not run');

  rep.endSuite();
}

async function suiteXbloxRunFlags(rep, assert, exe, workDir) {
  console.log('\n── XBlox: run flags ──\n');
  rep.beginSuite('XBlox run flags');

  const abortPath = join(workDir, 'run-flags-abort.blocks.json');
  writeBlocksFile(abortPath, runFlagsAbortBlocksFile());
  {
    const r = runXblox(exe, abortPath);
    const report = parseTrailingJson(r.stdout);
    const setEvents = (report.events || []).filter((event) => event.kind === 'setVariable');
    rep.step('xblox run flags abort', r.ms, `exit=${r.status}; events=${report.eventCount}`);
    assert(r.status !== 0, `xblox run flags: abort case exits non-zero (got ${r.status})`);
    assert(report.ok === false, 'xblox run flags: abort case report ok=false');
    assert(setEvents.some((event) => event.status === 'error' && String(event.message || '').includes('missing name')), 'xblox run flags: failing block emits error');
    assert(!setEvents.some((event) => event.data?.name === 'afterAbort'), 'xblox run flags: default abort skips following sibling');
  }

  const continuePath = join(workDir, 'run-flags-continue.blocks.json');
  writeBlocksFile(continuePath, runFlagsContinueBlocksFile());
  {
    const r = runXblox(exe, continuePath);
    const report = parseTrailingJson(r.stdout);
    const setEvents = (report.events || []).filter((event) => event.kind === 'setVariable');
    rep.step('xblox run flags continue', r.ms, `exit=${r.status}; events=${report.eventCount}`);
    assert(r.status !== 0, `xblox run flags: continue case still reports failure (got ${r.status})`);
    assert(report.ok === false, 'xblox run flags: continue case report ok=false');
    assert(setEvents.some((event) => event.data?.name === 'afterContinue' && event.data?.value === 1), 'xblox run flags: continueOnError allows following sibling');
  }

  rep.endSuite();
}

async function suiteXbloxShell(rep, assert, exe, workDir) {
  console.log('\n── XBlox: shell blocks ──\n');
  rep.beginSuite('XBlox shell blocks');

  const shellPath = join(workDir, 'shell.blocks.json');
  writeBlocksFile(shellPath, shellBlocksFile());
  const r = runXblox(exe, shellPath);
  const report = parseTrailingJson(r.stdout);
  rep.step('xblox shell block', r.ms, `exit=${r.status}; events=${report.eventCount}`);

  const shellEvent = eventsByKind(report, 'shell').find((event) => event.status === 'ok');
  const logEvent = eventsByKind(report, 'log').at(-1);
  assert(r.status === 0, `xblox shell: exits 0 (got ${r.status}); stderr: ${r.stderr.slice(0, 300)}`);
  assert(report.ok === true, 'xblox shell: report ok=true');
  assert(shellEvent?.type === 'shell', 'xblox shell: emits shell typed event');
  assert(String(shellEvent?.data?.stdout || '').includes('xblox-shell'), 'xblox shell: captures stdout');
  assert(shellEvent?.data?.result === shellEvent?.data?.stdout, 'xblox shell: result mirrors stdout for storeAs');
  assert(String(logEvent?.message || '').includes('xblox-shell'), 'xblox shell: stdout flows into PREVIOUS');

  rep.endSuite();
}

async function suiteXbloxNetwork(rep, assert, exe) {
  console.log('\n── XBlox: network blocks ──\n');
  rep.beginSuite('XBlox network blocks');

  const networkPath = join(XBLOX_FIXTURE_DIR, 'network.xblox');
  const r = runXblox(exe, networkPath);
  const report = parseTrailingJson(r.stdout);
  rep.step('xblox network fixture', r.ms, `exit=${r.status}; events=${report.eventCount}`);

  const networkEvent = eventsByKind(report, 'network')[0];
  const parseEvent = eventsByKind(report, 'Parse').find((event) => event.status === 'ok');
  const logEvent = eventsByKind(report, 'log').at(-1);

  assert(r.status === 0, `xblox network: exits 0 (got ${r.status}); stderr: ${r.stderr.slice(0, 300)}`);
  assert(report.ok === true, 'xblox network: report ok=true');
  assert(networkEvent?.status === 'ok' && networkEvent.type === 'network', 'xblox network: fetch event succeeds with network type');
  assert(networkEvent?.data?.statusCode === 200, 'xblox network: fetch status code is 200');
  assert(typeof networkEvent?.data?.raw === 'string' && networkEvent.data.raw.includes('Cassandra - 65cm'), 'xblox network: raw response contains Cassandra page');
  assert(parseEvent?.status === 'ok' && parseEvent.data?.result === 'Cassandra - 65cm', 'xblox network: Parse jq transforms PREVIOUS into page title');
  assert(!eventsByKind(report, 'setVariable').some((event) => event.data?.name === 'cassandra'), 'xblox network: generic storeAs replaces setVariable handoff');
  assert(logEvent?.message === 'Cassandra - 65cm', 'xblox network: log PREVIOUS prints parsed title');

  console.log(`\nnetwork log PREVIOUS:\n${logEvent.message}\n`);

  rep.endSuite();
}

async function suiteXblox(rep, assert, exe, workDir) {
  console.log('\n── XBlox: blocks + command execution ──\n');
  rep.beginSuite('XBlox block flow and commands');

  const flowPath = join(workDir, 'flow.blocks.json');
  writeBlocksFile(flowPath, flowBlocksFile());
  {
    const r = runXblox(exe, flowPath);
    const report = parseTrailingJson(r.stdout);
    rep.step('xblox flow blocks', r.ms, `exit=${r.status}; events=${report.eventCount}`);
    assert(r.status === 0, `xblox: flow exits 0 (got ${r.status}); stderr: ${r.stderr.slice(0, 300)}`);
    assert(report.ok === true, 'xblox: flow report ok=true');
    assert(actionEvents(report, 'metadata').length >= 2, 'xblox: metadata noop commands are reported');
    assert(actionEvents(report, 'app').length === 1, 'xblox: true if branch runs app noop once');
    assert(actionEvents(report, 'ribbon').length === 1, 'xblox: false if branch runs alternate ribbon noop');
    assert(actionEvents(report, 'url').length === 1, 'xblox: switch capture case runs url noop');
    assert(actionEvents(report, 'path').length === 0, 'xblox: switch default does not run for capture mode');
    assert((report.events || []).some((event) => event.kind === 'break' && event.status === 'ok'), 'xblox: while block reaches break');
  }

  const realPath = join(workDir, 'real.blocks.json');
  const markerFile = join(workDir, 'xblox-real-file.txt');
  writeFileSync(markerFile, 'listed-by-xblox\n', 'utf8');
  writeBlocksFile(realPath, realCommandBlocksFile(workDir));
  {
    const r = runXblox(exe, realPath);
    const report = parseTrailingJson(r.stdout);
    rep.step('xblox real external dir/ls', r.ms, `exit=${r.status}; stdout=${r.stdout.trim().slice(0, 160)}`);
    assert(r.status === 0, `xblox: real external command exits 0 (got ${r.status}); stderr: ${r.stderr.slice(0, 300)}`);
    assert(r.stdout.includes('xblox-real-file.txt'), 'xblox: dir/ls external command lists fixture file');
    assert(report.ok === true, 'xblox: real external command report ok=true');
    const externalEvents = actionEvents(report, 'external');
    const externalEvent = externalEvents[0];
    assert(externalEvents.length === 1, 'xblox: real external command event is external');
    assert(externalEvent.type === 'command' && externalEvent.errorCode === undefined, 'xblox: command event has structured type/error fields');
    assert(Array.isArray(externalEvent.stdout) && externalEvent.stdout.includes('xblox-real-file.txt'), 'xblox: command stdout is captured as lines');
    assert(Array.isArray(externalEvent.stderr), 'xblox: command stderr is captured as lines');
  }

  rep.endSuite();
}

const startedAtIso = new Date().toISOString();
const metricsCollector = createMetricsCollector();
const stats = createAssert();
const rep = createTestReport();
const t0 = performance.now();
const exe = cliExePath();
const workDir = mkdtempSync(join(tmpdir(), 'pm-xblox-test-'));
const expressionsOnly = process.argv.includes('--expressions-only');
const contextOnly = process.argv.includes('--context-only');
const networkOnly = process.argv.includes('--network-only');

try {
  mkdirSync(workDir, { recursive: true });
  if (expressionsOnly) {
    await suiteXbloxExpressions(rep, stats.assert, exe, workDir);
  } else if (contextOnly) {
    await suiteXbloxContext(rep, stats.assert, exe, workDir);
  } else if (networkOnly) {
    await suiteXbloxNetwork(rep, stats.assert, exe);
  } else {
    await suiteXblox(rep, stats.assert, exe, workDir);
    await suiteXbloxExpressions(rep, stats.assert, exe, workDir);
    await suiteXbloxContext(rep, stats.assert, exe, workDir);
    await suiteXbloxRunFlags(rep, stats.assert, exe, workDir);
    await suiteXbloxShell(rep, stats.assert, exe, workDir);
  }
} catch (e) {
  console.error(e);
  rep.meta.abortReason = 'uncaught exception';
  rep.meta.uncaughtError = e?.stack || String(e);
} finally {
  rep.finalize(stats, performance.now() - t0, exe, { workDir });
  writeTestReportFile(rep, metricsCollector, startedAtIso);
  if (process.env.PM_KEEP_XBLOX_TEST_TMP !== '1') {
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
