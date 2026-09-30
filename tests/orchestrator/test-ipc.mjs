/**
 * orchestrator/test-ipc.mjs
 *
 * Integration test: spawn the C++ worker in UDS mode, exchange messages, verify responses.
 *
 * Run:   npm run test:ipc
 *
 * Env:
 *   KBOT_IPC_LLM         — real LLM step is on by default; set to 0 / false / no / off to skip (CI / offline).
 *   KBOT_ROUTER          — router (default: openrouter; same defaults as C++ LLMClient / CLI)
 *   KBOT_IPC_MODEL       — optional model id (e.g. openrouter slug); else C++ default for that router
 *   KBOT_IPC_PROMPT      — custom prompt (default: capital of Germany; asserts "berlin" in reply)
 *   KBOT_IPC_LLM_LOG_MAX — max chars to print for LLM text (default: unlimited)
 *   KBOT_IPC_LLAMA         — llama :8888 step on by default; set 0/false/no/off to skip
 *   KBOT_IPC_LLAMA_AUTOSTART — if 0, do not spawn scripts/run-7b.sh when :8888 is closed
 *   KBOT_LLAMA_*        — KBOT_LLAMA_PORT, KBOT_LLAMA_BASE_URL, KBOT_LLAMA_MODEL, KBOT_LLAMA_START_TIMEOUT_MS
 *
 * Shared: presets.js, test-commons.js, reports.js
 * Report: cwd/tests/test-ipc__HH-mm.{json,md} (see reports.js)
 */

import { spawn } from 'node:child_process';
import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import net from 'node:net';
import { existsSync, unlinkSync } from 'node:fs';

import {
  distExePath,
  platform,
  uds,
  timeouts,
  kbotAiPayloadFromEnv,
  kbotAiPayloadLlamaLocal,
  usingDefaultGermanyPrompt,
  ensureLlamaLocalServer,
} from './presets.js';
import {
  createAssert,
  payloadObj,
  logKbotAiResponse,
  ipcLlmEnabled,
  ipcLlamaEnabled,
  llamaAutostartEnabled,
  createIpcClient,
  pipeWorkerStderr,
} from './test-commons.js';
import {
  createMetricsCollector,
  buildMetricsBundle,
  writeTestReports,
} from './reports.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const EXE = distExePath(__dirname);
const stats = createAssert();
const { assert } = stats;

/** Set at run start for error reports */
let ipcRunStartedAt = null;
let ipcMetricsCollector = null;
/** `llm` object from kbot-ai job_result (usage, model, OpenRouter extras) — filled in steps 6–7 */
let ipcKbotAiLlmRouter = null;
let ipcKbotAiLlmLlama = null;

async function run() {
  ipcMetricsCollector = createMetricsCollector();
  ipcRunStartedAt = new Date().toISOString();
  ipcKbotAiLlmRouter = null;
  ipcKbotAiLlmLlama = null;
  console.log('\n🔧 IPC [UDS] Integration Tests\n');

  if (!existsSync(EXE)) {
    console.error(`❌ Binary not found at ${EXE}`);
    process.exit(1);
  }

  const CPP_UDS_ARG = uds.workerArg();
  if (!platform.isWin && existsSync(CPP_UDS_ARG)) {
    unlinkSync(CPP_UDS_ARG);
  }

  // ── 1. Spawn & ready ────────────────────────────────────────────────────
  console.log('1. Spawn worker (UDS mode) and wait for ready signal');
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
    } catch (e) {
      if (i === timeouts.connectAttempts - 1) throw e;
      await new Promise((r) => setTimeout(r, timeouts.connectRetryMs));
    }
  }
  assert(true, 'Socket connected successfully');

  const ipc = createIpcClient(socket);
  ipc.attach();

  const readyMsg = await ipc.readyPromise;
  assert(readyMsg.type === 'ready', 'Worker sends ready message on startup');

  // ── 2. Ping / Pong ─────────────────────────────────────────────────────
  console.log('2. Ping → Pong');
  const pong = await ipc.request({ type: 'ping' }, timeouts.ipcDefault);
  assert(pong.type === 'pong', `Response type is "pong" (got "${pong.type}")`);

  // ── 3. Job echo ─────────────────────────────────────────────────────────
  console.log('3. Job → Job Result (echo payload)');
  const payload = { action: 'resize', width: 1024, format: 'webp' };
  const jobResult = await ipc.request({ type: 'job', payload }, timeouts.ipcDefault);
  assert(jobResult.type === 'job_result', `Response type is "job_result" (got "${jobResult.type}")`);
  assert(
    jobResult.payload?.action === 'resize' && jobResult.payload?.width === 1024,
    'Payload echoed back correctly'
  );

  // ── 4. Unknown type → error ─────────────────────────────────────────────
  console.log('4. Unknown type → error response');
  const errResp = await ipc.request({ type: 'nonsense' }, timeouts.ipcDefault);
  assert(errResp.type === 'error', `Response type is "error" (got "${errResp.type}")`);

  // ── 5. Multiple rapid requests ──────────────────────────────────────────
  console.log('5. Multiple concurrent requests');
  const promises = [];
  for (let i = 0; i < 10; i++) {
    promises.push(ipc.request({ type: 'ping', payload: { seq: i } }, timeouts.ipcDefault));
  }
  const results = await Promise.all(promises);
  assert(results.length === 10, `All 10 responses received`);
  assert(results.every((r) => r.type === 'pong'), 'All responses are pong');

  // ── 6. kbot-ai — real LLM (optional via ipcLlmEnabled) ─────────────────
  if (ipcLlmEnabled()) {
    const aiPayload = kbotAiPayloadFromEnv();
    const r = aiPayload.router;
    console.log(`6. kbot-ai → real LLM (router=${r}, timeout 3m)`);
    const live = await ipc.request(
      {
        type: 'kbot-ai',
        payload: aiPayload,
      },
      timeouts.kbotAi
    );
    assert(live.type === 'job_result', `LLM response type job_result (got "${live.type}")`);
    const lp = payloadObj(live);
    assert(lp?.status === 'success', `payload status success (got "${lp?.status}")`);
    assert(
      typeof lp?.text === 'string' && lp.text.trim().length >= 3,
      `assistant text present (length ${(lp?.text || '').length})`
    );
    if (usingDefaultGermanyPrompt()) {
      assert(
        /berlin/i.test(String(lp?.text || '')),
        'assistant text mentions Berlin (capital of Germany)'
      );
    }
    ipcKbotAiLlmRouter = lp?.llm ?? null;
    logKbotAiResponse('kbot-ai response', live);
  } else {
    console.log('6. kbot-ai — skipped (KBOT_IPC_LLM=0/false/no/off; default is to run live LLM)');
  }

  // ── 7. kbot-ai — llama local :8888 (optional; llama-basics parity) ───────
  if (ipcLlamaEnabled()) {
    console.log('7. kbot-ai → llama local runner (OpenAI :8888, presets.llama)');
    let llamaReady = false;
    try {
      await ensureLlamaLocalServer({
        autostart: llamaAutostartEnabled(),
        startTimeoutMs: timeouts.llamaServerStart,
      });
      llamaReady = true;
    } catch (e) {
      console.error(`  ❌ ${e?.message ?? e}`);
    }
    assert(llamaReady, 'llama-server listening on :8888 (or autostart run-7b.sh succeeded)');

    if (llamaReady) {
      const llamaPayload = kbotAiPayloadLlamaLocal();
      const llamaRes = await ipc.request(
        { type: 'kbot-ai', payload: llamaPayload },
        timeouts.llamaKbotAi
      );
      assert(llamaRes.type === 'job_result', `llama IPC response type job_result (got "${llamaRes.type}")`);
      const llp = payloadObj(llamaRes);
      assert(llp?.status === 'success', `llama payload status success (got "${llp?.status}")`);
      assert(
        typeof llp?.text === 'string' && llp.text.trim().length >= 1,
        `llama assistant text present (length ${(llp?.text || '').length})`
      );
      assert(/\b8\b/.test(String(llp?.text || '')), 'llama arithmetic: reply mentions 8 (5+3)');
      ipcKbotAiLlmLlama = llp?.llm ?? null;
      logKbotAiResponse('kbot-ai llama local', llamaRes);
    }
  } else {
    console.log('7. kbot-ai llama local — skipped (KBOT_IPC_LLAMA=0; default is to run)');
  }

  // ── 8. Graceful shutdown ────────────────────────────────────────────────
  console.log('8. Graceful shutdown');
  const shutdownRes = await ipc.request({ type: 'shutdown' }, timeouts.ipcDefault);
  assert(shutdownRes.type === 'shutdown_ack', `Shutdown acknowledged (got "${shutdownRes.type}")`);

  await new Promise((r) => setTimeout(r, timeouts.postShutdownMs));
  socket.destroy();
  assert(workerProc.exitCode === 0, `Worker exited with code 0 (got ${workerProc.exitCode})`);

  // ── Summary ─────────────────────────────────────────────────────────────
  console.log(`\n────────────────────────────────`);
  console.log(`  Passed: ${stats.passed}  Failed: ${stats.failed}`);
  console.log(`────────────────────────────────\n`);

  try {
    const finishedAt = new Date().toISOString();
    const { jsonPath, mdPath } = await writeTestReports(
      'test-ipc',
      {
        startedAt: ipcRunStartedAt,
        finishedAt,
        passed: stats.passed,
        failed: stats.failed,
        ok: stats.failed === 0,
        ipcLlm: ipcLlmEnabled(),
        ipcLlama: ipcLlamaEnabled(),
        env: {
          KBOT_IPC_LLM: process.env.KBOT_IPC_LLM ?? null,
          KBOT_IPC_LLAMA: process.env.KBOT_IPC_LLAMA ?? null,
          KBOT_IPC_LLAMA_AUTOSTART: process.env.KBOT_IPC_LLAMA_AUTOSTART ?? null,
          KBOT_ROUTER: process.env.KBOT_ROUTER ?? null,
          KBOT_IPC_MODEL: process.env.KBOT_IPC_MODEL ?? null,
          KBOT_IPC_PROMPT: process.env.KBOT_IPC_PROMPT ?? null,
          KBOT_LLAMA_PORT: process.env.KBOT_LLAMA_PORT ?? null,
          KBOT_LLAMA_BASE_URL: process.env.KBOT_LLAMA_BASE_URL ?? null,
        },
        metrics: buildMetricsBundle(ipcMetricsCollector, ipcRunStartedAt, finishedAt),
        kbotAi: {
          routerStep: ipcKbotAiLlmRouter,
          llamaStep: ipcKbotAiLlmLlama,
        },
      },
      { cwd: process.cwd() }
    );
    console.log(`  📄 Report JSON: ${jsonPath}`);
    console.log(`  📄 Report MD:   ${mdPath}\n`);
  } catch (e) {
    console.error('  ⚠️ Failed to write report:', e?.message ?? e);
  }

  process.exit(stats.failed > 0 ? 1 : 0);
}

run().catch(async (err) => {
  console.error('Test runner error:', err);
  try {
    const finishedAt = new Date().toISOString();
    const c = ipcMetricsCollector ?? createMetricsCollector();
    const started = ipcRunStartedAt ?? finishedAt;
    await writeTestReports(
      'test-ipc',
      {
        startedAt: started,
        finishedAt,
        error: String(err?.stack ?? err),
        passed: stats.passed,
        failed: stats.failed,
        ok: false,
        metrics: buildMetricsBundle(c, started, finishedAt),
      },
      { cwd: process.cwd() }
    );
  } catch (_) {
    /* ignore */
  }
  process.exit(1);
});
