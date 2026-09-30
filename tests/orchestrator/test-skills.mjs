#!/usr/bin/env node
/**
 * Skills baseline tests (isolated from test-media.mjs).
 *
 * Current scope:
 * - `pm-image llm agent --dry-run`
 * - `pm-image llm info skills --json`
 *
 * Deterministic cwd for workspace-scope behavior:
 * - runs commands from `tests/skills` (created if missing)
 */

import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { mediaExePath } from './media-presets.js';
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
const root = join(__dirname, '..');
const TEST_REPORT_PATH = join(root, 'tests', 'test-report-last.md');
const EXE = mediaExePath(__dirname);
const stats = createAssert();
const { assert } = stats;

function pmImgArgs(...argv) {
  return ['--no-gui', '--no-mcp', ...argv];
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
      this._cur.steps.push({
        label,
        ms: Math.round(ms * 100) / 100,
        detail: detail || '',
      });
    },
    finalize(statsObj, wallMs, exe, extra = {}) {
      this.meta = {
        generatedAt: new Date().toISOString(),
        node: process.version,
        argv: process.argv.slice(2),
        exe,
        passed: statsObj.passed,
        failed: statsObj.failed,
        wallClockMs: Math.round(wallMs * 100) / 100,
        ...extra,
      };
      if (this._cur) this.endSuite();
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
      displayName: 'skills-basics',
      testName: 'skills-basics',
      writtenAt: finishedAtIso,
      generatedAt: rep.meta.generatedAt,
      exe: rep.meta.exe,
      argv: Array.isArray(rep.meta.argv) ? rep.meta.argv.join(' ') : String(rep.meta.argv ?? ''),
      wallClockMs: rep.meta.wallClockMs,
    },
    metrics,
    images: [],
    mediaSuites: rep.suites,
    integrationNotes: rep.notes,
  });

  const acc = isAccumulateMode() && getChapterTitle();
  if (acc) {
    md += '\n---\n\n*Per-run: `tests/test-report-last.md` — this run is also a **chapter** in `tests/test-report-all.md` (from `npm run test:all`).*\n';
  } else {
    md += '\n---\n\n*Artifact: `tests/test-report-last.md` — overwritten on each test run.*\n';
  }
  writeFileSync(TEST_REPORT_PATH, md, 'utf8');
  if (acc) {
    try {
      appendMarkdownChapter(getChapterTitle(), md, { npmScript: getNpmScriptLabel() || undefined });
      console.log(`  (aggregated report chapter: ${TEST_REPORT_ALL_PATH})`);
    } catch (e) {
      console.error('Failed to append chapter to test-report-all.md:', e);
    }
  }
}

async function suiteSkillsBasics(rep) {
  console.log('\n── Skills basics: llm agent --dry-run + llm info skills ──\n');
  rep.beginSuite('Skills basics (cwd=tests/skills)');
  const skillsCwd = resolve(root, 'tests', 'skills');
  const fixtureSkillSource = resolve(skillsCwd, 'test-skill.md');
  const fixtureSkillDir = resolve(skillsCwd, 'skills', 'test-skill');
  const fixtureSkillMd = resolve(fixtureSkillDir, 'SKILL.md');
  mkdirSync(skillsCwd, { recursive: true });
  mkdirSync(fixtureSkillDir, { recursive: true });
  writeFileSync(fixtureSkillMd, readFileSync(fixtureSkillSource, 'utf8'), 'utf8');
  try {
    const runInSkillsCwd = (args, timeoutMs = 30_000) =>
      spawnSync(EXE, pmImgArgs(...args), {
        encoding: 'utf8',
        timeout: timeoutMs,
        cwd: skillsCwd,
      });

    {
      const t0 = performance.now();
      const r = runInSkillsCwd(['llm', 'agent', '--prompt', 'skills dry-run smoke', '--dry-run']);
      const ms = performance.now() - t0;
      rep.step('llm agent --dry-run (cwd tests/skills)', ms, `exit ${r.status}`);
      assert(r.status === 0, `llm agent --dry-run exits 0 (got ${r.status}); stderr: ${(r.stderr || '').slice(0, 300)}`);
      assert((r.stdout || '').includes('# llm agent — dry run'),
        'llm agent --dry-run prints markdown header');
      assert((r.stdout || '').includes('## Provider'),
        'llm agent --dry-run includes provider section');
      assert((r.stdout || '').includes('## Context'),
        'llm agent --dry-run includes context section');
      assert((r.stdout || '').includes('## Skills'),
        'llm agent --dry-run includes skills section');
    }

    {
      const t0 = performance.now();
      const r = runInSkillsCwd(['llm', 'info', 'skills', '--json']);
      const ms = performance.now() - t0;
      rep.step('llm info skills --json (cwd tests/skills)', ms, `exit ${r.status}`);
      assert(r.status === 0, `llm info skills --json exits 0 (got ${r.status}); stderr: ${(r.stderr || '').slice(0, 300)}`);
      let parsed = null;
      try {
        parsed = JSON.parse(r.stdout || '{}');
      } catch (e) {
        assert(false, `llm info skills --json stdout is JSON: ${e.message}`);
        parsed = {};
      }
      assert(parsed && typeof parsed === 'object' && !Array.isArray(parsed), 'llm info skills --json returns object');
      assert(Array.isArray(parsed.skills), 'llm info skills --json has skills array');
      assert(parsed.roots && typeof parsed.roots === 'object', 'llm info skills --json has roots object');
      const byName = new Map((parsed.skills || []).map((s) => [s?.name, s]));
      assert(byName.has('test-skill'), 'llm info skills includes test-skill fixture');
      assert(byName.get('test-skill')?.source === 'workspace', 'test-skill source resolves as workspace');
    }
  } finally {
    rep.endSuite();
  }
}

async function run() {
  const wallStart = performance.now();
  const rep = createTestReport();
  const metricsCollector = createMetricsCollector();
  const startedAtIso = new Date().toISOString();
  let exitCode = 1;

  try {
    if (!EXE) {
      rep.meta.abortReason = 'Binary path resolution failed';
      console.error(rep.meta.abortReason);
      return;
    }

    console.log(`\nskills basics tests\n  binary: ${EXE}\n  cwd: ${process.cwd()}\n`);
    await suiteSkillsBasics(rep);
    console.log(`\nDone (skills basics). Passed: ${stats.passed}  Failed: ${stats.failed}\n`);
    exitCode = stats.failed > 0 ? 1 : 0;
  } catch (e) {
    rep.meta.uncaughtError = String(e?.stack || e);
    console.error(e);
    exitCode = 1;
  } finally {
    rep.finalize(stats, performance.now() - wallStart, EXE);
    try {
      writeTestReportFile(rep, metricsCollector, startedAtIso);
      console.log(`Test report written: ${TEST_REPORT_PATH}`);
    } catch (e) {
      console.error('Failed to write test report:', e);
    }
    process.exit(exitCode);
  }
}

run().catch((e) => {
  console.error(e);
  process.exit(1);
});

