/**
 * Optional aggregation for `npm run test:all` — appends one markdown **chapter** per
 * sub-suite to `tests/test-report-all.md`.
 *
 * Enable by setting in the environment:
 *   MEDIA_CPP_TEST_REPORT_ACCUMULATE=1
 *   MEDIA_CPP_TEST_CHAPTER=<H2 title for this run>
 *   MEDIA_CPP_NPM_SCRIPT=<e.g. test:media> (optional, shown as caption)
 */
import { appendFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
export const TEST_REPORT_ALL_PATH = join(__dirname, '..', 'tests', 'test-report-all.md');

export function isAccumulateMode() {
  return process.env.MEDIA_CPP_TEST_REPORT_ACCUMULATE === '1';
}

export function getChapterTitle() {
  return (process.env.MEDIA_CPP_TEST_CHAPTER || '').trim();
}

export function getNpmScriptLabel() {
  return (process.env.MEDIA_CPP_NPM_SCRIPT || '').trim();
}

/**
 * Wipes and opens the aggregated file (start of `test:all` only).
 */
export function initAccumulatedReport() {
  mkdirSync(dirname(TEST_REPORT_ALL_PATH), { recursive: true });
  const t = new Date().toISOString();
  const md = [
    '# media-cpp — `test:all` report',
    '',
    `*Started: ${t}* — one **chapter** (heading level 2) per sub-command.`,
    '',
    'Each chapter is appended as the step finishes. Single-run reports still go to `tests/test-report-last.md` / `tests/test-report-last-duplicate.md` when those suites are executed.',
    '',
    '---',
    '',
  ].join('\n');
  writeFileSync(TEST_REPORT_ALL_PATH, `${md}\n`, 'utf8');
}

/**
 * @param {string} chapterTitle  Markdown H2 text (no leading \#)
 * @param {string} bodyMd  Full chapter body
 * @param {{ npmScript?: string }} [opts]
 */
export function appendMarkdownChapter(chapterTitle, bodyMd, opts = {}) {
  const { npmScript } = opts;
  const safe = String(chapterTitle || 'Untitled').trim() || 'Untitled';
  mkdirSync(dirname(TEST_REPORT_ALL_PATH), { recursive: true });
  let block = `## ${safe}\n\n`;
  if (npmScript) {
    block += `*npm script: \`${npmScript}\`*\n\n`;
  }
  block += bodyMd.replace(/\n+$/, '');
  block += '\n\n---\n\n';
  appendFileSync(TEST_REPORT_ALL_PATH, block, 'utf8');
}

/**
 * For suites with no JSON/markdown report (e.g. trial / license-smoke / npm subproject).
 */
export function appendResultChapter(chapterTitle, { npmScript, exitCode, stdout, stderr, detail = '' } = {}) {
  const ok = exitCode === 0;
  let body = `**Result:** ${ok ? 'passed' : '**failed**'}`;
  if (typeof exitCode === 'number' && !Number.isNaN(exitCode)) {
    body += ` (exit \`${exitCode}\`)`;
  }
  body += '\n';
  if (detail) body += `\n${detail}\n`;
  if (stderr && String(stderr).trim()) {
    const s = String(stderr).trim();
    body += `\n\`\`\`\n${s.slice(0, 2000)}${s.length > 2000 ? '\n…' : ''}\n\`\`\`\n`;
  } else if (stdout && !ok) {
    const s = String(stdout).trim();
    if (s) {
      body += `\n\`\`\`\n${s.slice(0, 2000)}${s.length > 2000 ? '\n…' : ''}\n\`\`\`\n`;
    }
  }
  appendMarkdownChapter(chapterTitle, body, { npmScript });
}

/**
 * One-line summary at the end of the full `test:all` run.
 */
export function appendSummaryFooter({ allPassed, stepCount, failedAt }) {
  const t = new Date().toISOString();
  const lines = [
    '## Summary',
    '',
    `- *Finished: ${t}*`,
    `- *Steps run:* ${stepCount}`,
    allPassed
      ? '- **Overall:** all steps completed with exit 0'
      : `- **Overall:** **failed** (first failing step: ${failedAt || 'unknown'})`,
    '',
    '---\n',
  ];
  appendFileSync(TEST_REPORT_ALL_PATH, lines.join('\n') + '\n', 'utf8');
}
