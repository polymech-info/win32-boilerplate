/**
 * orchestrator/test-files.mjs
 *
 * IPC + CLI parity for text file sources (port of kbot/src/source.ts — text slice only; images later).
 * Fixtures: packages/kbot/tests/test-data/files (path below is resolved from orchestrator/).
 *
 * Run: npm run test:files
 *
 * Env (optional live LLM step):
 *   KBOT_IPC_LLM — set 0/false/off to skip live kbot-ai (default: run when key available)
 *   KBOT_ROUTER, KBOT_IPC_MODEL — same as test-ipc
 *
 * CLI (npm run test:files -- --help):
 *   --fixtures <dir>   Override fixture root (default: ../../tests/test-data/files)
 */

import { spawn } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import net from 'node:net';
import { existsSync, unlinkSync } from 'node:fs';
import yargs from 'yargs';
import { hideBin } from 'yargs/helpers';

import {
  distExePath,
  platform,
  uds,
  timeouts,
  kbotAiPayloadFromEnv,
} from './presets.js';
import {
  createAssert,
  payloadObj,
  ipcLlmEnabled,
  createIpcClient,
  pipeWorkerStderr,
} from './test-commons.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const EXE = distExePath(__dirname);
const stats = createAssert();
const { assert } = stats;

const defaultFixtures = resolve(__dirname, '../../tests/test-data/files');

function parseArgv() {
  const y = yargs(hideBin(process.argv))
    .scriptName('test-files')
    .usage('$0 [options]\n\nText file source IPC tests (fixtures under packages/kbot/tests/test-data/files).')
    .option('fixtures', {
      type: 'string',
      default: defaultFixtures,
      describe: 'Directory used as kbot-ai `path` (project root for includes)',
    })
    .strict()
    .help()
    .alias('h', 'help');
  return y.parseSync();
}

/**
 * @param {import('node:net').Socket} socket
 * @param {string} fixturesDir
 */
async function runFileSuite(socket, fixturesDir) {
  const ipc = createIpcClient(socket);
  ipc.attach();

  const readyMsg = await ipc.readyPromise;
  assert(readyMsg.type === 'ready', 'worker ready');

  console.log('\n── Dry-run source attachment (no LLM) ──\n');

  /** @param {Record<string, unknown>} payload */
  async function dry(payload) {
    const msg = await ipc.request({ type: 'kbot-ai', payload }, timeouts.ipcDefault);
    assert(msg.type === 'job_result', `job_result (got ${msg.type})`);
    const p = payloadObj(msg);
    assert(p?.dry_run === true, 'dry_run flag');
    assert(p?.status === 'success', 'status success');
    assert(Array.isArray(p?.sources), 'sources array');
    return p;
  }

  let p = await dry({
    dry_run: true,
    path: fixturesDir,
    include: ['bubblesort.js'],
    prompt: 'What function is defined? Reply one word.',
  });
  assert(
    p.sources.some((s) => String(s).includes('bubblesort')),
    'sources lists bubblesort.js',
  );
  assert(
    /bubbleSort/i.test(String(p.prompt_preview || '')),
    'prompt_preview contains bubbleSort',
  );

  p = await dry({
    dry_run: true,
    path: fixturesDir,
    include: ['*.js'],
    prompt: 'List algorithms.',
  });
  assert(p.sources.length >= 2, 'glob *.js yields at least 2 files');
  const names = p.sources.map((s) => String(s).toLowerCase());
  assert(names.some((n) => n.includes('bubblesort')), 'glob includes bubblesort.js');
  assert(names.some((n) => n.includes('factorial')), 'glob includes factorial.js');

  p = await dry({
    dry_run: true,
    path: fixturesDir,
    include: ['glob/data.json'],
    prompt: 'What is the title?',
  });
  assert(
    String(p.prompt_preview || '').includes('Injection Barrel'),
    'JSON fixture content in preview',
  );

  if (ipcLlmEnabled()) {
    console.log('\n── Live LLM — single file prompt ──\n');
    const base = kbotAiPayloadFromEnv();
    const payload = {
      ...base,
      path: fixturesDir,
      include: ['bubblesort.js'],
      prompt:
        process.env.KBOT_FILES_LIVE_PROMPT ||
        'What is the name of the sorting algorithm in the code? Reply with two words: bubble sort',
    };
    const msg = await ipc.request({ type: 'kbot-ai', payload }, timeouts.kbotAi);
    assert(msg.type === 'job_result', 'live job_result');
    const lp = payloadObj(msg);
    assert(lp?.status === 'success', 'live status success');
    const text = String(lp?.text || '');
    assert(/bubble/i.test(text), 'assistant mentions bubble (file context worked)');
  } else {
    console.log('\n── Live LLM — skipped (KBOT_IPC_LLM off) ──\n');
  }

  const shutdownRes = await ipc.request({ type: 'shutdown' }, timeouts.ipcDefault);
  assert(shutdownRes.type === 'shutdown_ack', 'shutdown ack');
}

async function run() {
  const argv = parseArgv();
  const fixturesDir = resolve(argv.fixtures);

  if (!existsSync(EXE)) {
    console.error(`Binary not found: ${EXE}`);
    process.exit(1);
  }
  if (!existsSync(fixturesDir)) {
    console.error(`Fixtures directory not found: ${fixturesDir}`);
    process.exit(1);
  }

  console.log(`\n📁 test:files — fixtures: ${fixturesDir}\n`);

  const CPP_UDS_ARG = uds.workerArg();
  if (!platform.isWin && existsSync(CPP_UDS_ARG)) {
    unlinkSync(CPP_UDS_ARG);
  }

  const workerProc = spawn(EXE, ['worker', '--uds', CPP_UDS_ARG], { stdio: 'pipe' });
  pipeWorkerStderr(workerProc);

  let socket;
  for (let i = 0; i < timeouts.connectAttempts; i++) {
    try {
      await new Promise((res, rej) => {
        socket = net.connect(uds.connectOpts(CPP_UDS_ARG));
        socket.once('connect', res);
        socket.once('error', rej);
      });
      break;
    } catch {
      if (i === timeouts.connectAttempts - 1) throw new Error('connect failed');
      await new Promise((r) => setTimeout(r, timeouts.connectRetryMs));
    }
  }

  try {
    await runFileSuite(socket, fixturesDir);
  } finally {
    try {
      socket?.destroy();
    } catch {
      /* ignore */
    }
    workerProc.kill();
  }

  console.log(`\nDone. Passed: ${stats.passed}  Failed: ${stats.failed}\n`);
  process.exit(stats.failed > 0 ? 1 : 0);
}

run().catch((e) => {
  console.error(e);
  process.exit(1);
});
