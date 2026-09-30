#!/usr/bin/env node
/**
 * Runs the full `test:all` sequence with one aggregated **markdown** report at
 * `tests/test-report-all.md` (one `##` chapter per step).
 *
 * See `orchestrator/test-report-accumulate.mjs` for the env contract used by
 * `test-media.mjs` and `test-media-duplicate.mjs`. Steps that do not emit a report
 * get a short result chapter (exit code) from this script.
 *
 * `test:replicate` (orchestrator/test-replicate.mjs) is not included here: it is a
 * long, network-heavy Replicate smoke test; run it manually with
 * `npm run test:replicate` when `IMAGE_TRANSFORM_REPLICATE_API_KEY` is set.
 */
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import {
  initAccumulatedReport,
  appendResultChapter,
  appendSummaryFooter,
  TEST_REPORT_ALL_PATH,
} from '../orchestrator/test-report-accumulate.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const isWin = process.platform === 'win32';

/** [npm script name, chapter H2 text] */
const SELF_REPORTING = [
  ['test:media', 'Media — full integration'],
  ['test:media:templates', 'Media — templates (REST, IPC, CLI)'],
  ['test:media:glob', 'Media — glob batch (nested PNG)'],
  ['test:media:resize', 'Media — resize (in-place, formats, fit, raw)'],
  ['test:media:rest', 'Media — REST only'],
  ['test:media:multipart', 'Media — multipart POST /v1/resize'],
  ['test:media:ipc', 'Media — line IPC (TCP)'],
  ['test:media:url', 'Media — HTTPS URL input'],
  ['test:media:compress', 'Media — compress CLI'],
  ['test:media:meta', 'Media — meta CLI'],
  ['test:media:find', 'Media — find CLI'],
  ['test:duplicates', 'Duplicates (CLI)'],
  ['test:media:api', 'Media — JSON API (REST + IPC)'],
  ['test:media:llm', 'Media — LLM / tools'],
  ['test:llm:security', 'Media — LLM FS guard (file_read)'],
  ['test:llm:tools', 'Media — LLM FS tools (file_read + MCP path-tools + optional llm agent from settings.json)'],
  ['test:media:chat', 'Media — chat agent (optional API key)'],
  ['test:media:queue', 'Media — batch / queue'],
  ['test:settings', 'Media — API key resolution'],
  ['test:service', 'Service — Pixlwiz /api/images upload (CLI)'],
  ['test:service:posts', 'Service — Pixlwiz post + pictures (CLI)'],
];

const RESULT_ONLY = [
  ['test:trial-protection', 'Trial — `status --json` (Windows)'],
  ['test:license-smoke', 'License — `fingerprint` (Windows)'],
];

function runWithReportChapter(npmScript, chapterTitle) {
  const r = spawnSync('npm', ['run', npmScript], {
    cwd: root,
    stdio: 'inherit',
    env: {
      ...process.env,
      MEDIA_CPP_TEST_REPORT_ACCUMULATE: '1',
      MEDIA_CPP_TEST_CHAPTER: chapterTitle,
      MEDIA_CPP_NPM_SCRIPT: npmScript,
    },
    shell: isWin,
  });
  return r.status ?? 1;
}

function runPlain(npmScript) {
  const r = spawnSync('npm', ['run', npmScript], {
    cwd: root,
    stdio: 'inherit',
    env: { ...process.env },
    shell: isWin,
  });
  return r.status ?? 1;
}

function runLicenseServerTest() {
  const r = spawnSync('npm', ['test', '--prefix', 'apps/license-server'], {
    cwd: root,
    stdio: 'inherit',
    env: { ...process.env },
    shell: isWin,
  });
  return r.status ?? 1;
}

initAccumulatedReport();
let stepIndex = 0;
let failedAt = '';
let allPassed = true;

for (const [script, title] of SELF_REPORTING) {
  stepIndex += 1;
  console.log(`\n[test:all] ▶ ${script} — ${title}\n`);
  const st = runWithReportChapter(script, title);
  if (st !== 0) {
    allPassed = false;
    failedAt = script;
    break;
  }
}

if (allPassed) {
  for (const [script, title] of RESULT_ONLY) {
    stepIndex += 1;
    console.log(`\n[test:all] ▶ ${script}\n`);
    const st = runPlain(script);
    appendResultChapter(title, { npmScript: script, exitCode: st });
    if (st !== 0) {
      allPassed = false;
      failedAt = script;
      break;
    }
  }
}

if (allPassed) {
  stepIndex += 1;
  console.log(`\n[test:all] ▶ test:license-dat (npm test in apps/license-server)\n`);
  const st = runLicenseServerTest();
  appendResultChapter('License server (package test)', {
    npmScript: 'test:license-dat',
    exitCode: st,
    detail: 'Runs `npm test` in `apps/license-server`.',
  });
  if (st !== 0) {
    allPassed = false;
    failedAt = 'test:license-dat';
  }
}

appendSummaryFooter({
  allPassed,
  stepCount: stepIndex,
  failedAt: allPassed ? '' : failedAt,
});

if (allPassed) {
  console.log(`\n[test:all] OK — ${stepIndex} step(s). Aggregated report: ${TEST_REPORT_ALL_PATH}\n`);
  process.exit(0);
}
console.log(`\n[test:all] FAILED at ${failedAt} — see ${TEST_REPORT_ALL_PATH}\n`);
process.exit(1);
