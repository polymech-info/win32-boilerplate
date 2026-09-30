/**
 * orchestrator/test-ipc-classifier.mjs
 *
 * IPC + local llama: one kbot-ai call — semantic distance from anchor "machine workshop"
 * to every business label (JobViewer.tsx ~205). Output is a single JSON array (+ meta).
 *
 * Run: npm run test:ipc:classifier
 * CLI (overrides env): yargs — see parseClassifierArgv()
 *   npm run test:ipc:classifier -- --help
 *   npm run test:ipc:classifier -- --provider openrouter --model openai/gpt-4o-mini --backend remote -n 3
 *   npm run test:ipc:classifier -- -r openrouter -m openai/gpt-4o-mini --backend remote -n 3 -F structured
 *   npm run test:ipc:classifier -- -r openrouter -m x -F stress,no-heartbeat
 *   npm run test:ipc:classifier -- -r openrouter -m x --backend remote -n 3 -F stress,structured
 *   npm run test:ipc:classifier -- -r openrouter -m x --backend remote -F structured --dst ./out.json
 *
 * Env:
 *   KBOT_IPC_CLASSIFIER_LLAMA — set 0 to use OpenRouter (KBOT_ROUTER, KBOT_IPC_MODEL) instead of local llama :8888
 *   KBOT_IPC_LLAMA_AUTOSTART — 0 to skip spawning run-7b.sh (llama mode only)
 *   KBOT_ROUTER / KBOT_IPC_MODEL — when classifier llama is off (same as test-ipc step 6)
 *   KBOT_CLASSIFIER_LIMIT    — max labels in the batch (default: all)
 *   KBOT_CLASSIFIER_TIMEOUT_MS — single batched kbot-ai call (default: 300000)
 *
 * OpenRouter: npm run test:ipc:classifier:openrouter   (sets KBOT_IPC_CLASSIFIER_LLAMA=0)
 * Stress (batch repeats, one worker): KBOT_CLASSIFIER_STRESS_RUNS=N (default 1)
 *   npm run test:ipc:classifier:openrouter:stress  → OpenRouter + 5 runs (override N via env)
 *
 * Reports (reports.js): cwd/tests/test-ipc-classifier__HH-mm.{json,md}; distances in
 * test-ipc-classifier-distances__HH-mm.json (same timestamp as the main JSON).
 * With -F structured, the prompt asks for {"items":[...]} to match json_object APIs.
 */

import { spawn } from 'node:child_process';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
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
  kbotAiPayloadLlamaLocal,
  kbotAiPayloadFromEnv,
  ensureLlamaLocalServer,
  llama,
  router,
} from './presets.js';
import {
  createAssert,
  payloadObj,
  llamaAutostartEnabled,
  ipcClassifierLlamaEnabled,
  createIpcClient,
  pipeWorkerStderr,
} from './test-commons.js';
import {
  reportFilePathWithExt,
  timeParts,
  createMetricsCollector,
  buildMetricsBundle,
  writeTestReports,
} from './reports.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
/** Set at run start; used by catch for error reports */
let classifierMetricsCollector = null;
let classifierRunStartedAt = null;
/** Feature flags from `-F` / `--feature` (stress, structured, no-heartbeat, no-report, quiet) */
let classifierFeatures = /** @type {Set<string>} */ (new Set());
/** Parsed argv (after yargs); set in parseClassifierArgv */
let classifierArgv = /** @type {Record<string, unknown> | null} */ (null);

/**
 * @param {unknown} featureOpt
 * @returns {Set<string>}
 */
function parseFeatureList(featureOpt) {
  const out = [];
  const arr = Array.isArray(featureOpt) ? featureOpt : [];
  for (const f of arr) {
    if (typeof f === 'string') out.push(...f.split(',').map((s) => s.trim()).filter(Boolean));
  }
  return new Set(out);
}

/**
 * Parse CLI and apply to `process.env` (CLI wins over prior env).
 * @returns {Record<string, unknown> & { featuresSet: Set<string> }}
 */
export function parseClassifierArgv() {
  const y = yargs(hideBin(process.argv))
    .scriptName('test-ipc-classifier')
    .usage('$0 [options]\n\nIPC classifier batch test. Flags override env vars for this process.')
    .option('provider', {
      alias: 'r',
      type: 'string',
      describe: 'Router / provider → KBOT_ROUTER (e.g. openrouter, ollama, openai)',
    })
    .option('model', {
      alias: 'm',
      type: 'string',
      describe: 'Model id → KBOT_IPC_MODEL',
    })
    .option('runs', {
      alias: 'n',
      type: 'number',
      describe: 'Batch repeats (stress) → KBOT_CLASSIFIER_STRESS_RUNS',
    })
    .option('limit', {
      alias: 'l',
      type: 'number',
      describe: 'Max labels → KBOT_CLASSIFIER_LIMIT',
    })
    .option('timeout', {
      alias: 't',
      type: 'number',
      describe: 'LLM HTTP timeout ms → KBOT_CLASSIFIER_TIMEOUT_MS',
    })
    .option('backend', {
      type: 'string',
      choices: ['local', 'remote'],
      describe: 'local = llama :8888; remote = router (sets KBOT_IPC_CLASSIFIER_LLAMA=0)',
    })
    .option('no-autostart', {
      type: 'boolean',
      default: false,
      describe: 'Do not spawn run-7b.sh → KBOT_IPC_LLAMA_AUTOSTART=0',
    })
    .option('feature', {
      alias: 'F',
      type: 'array',
      default: [],
      describe:
        'Feature flags (repeat or comma-separated): stress, structured, no-heartbeat, no-report, quiet',
    })
    .option('dst', {
      type: 'string',
      describe:
        'Forwarded to kbot-ai IPC `dst` (worker writes completion text here; path resolved from cwd). Same as C++ --dst.',
    })
    .option('output', {
      type: 'string',
      describe:
        'Forwarded to IPC if --dst omitted (C++ `output` field). Prefer --dst when both are set.',
    })
    .strict()
    .help()
    .alias('h', 'help');

  const argv = y.parseSync();
  const featuresSet = parseFeatureList(argv.feature);

  if (argv.provider != null && String(argv.provider).trim() !== '') {
    process.env.KBOT_ROUTER = String(argv.provider).trim();
  }
  if (argv.model != null && String(argv.model).trim() !== '') {
    process.env.KBOT_IPC_MODEL = String(argv.model).trim();
  }
  if (argv.runs != null && Number.isFinite(argv.runs) && argv.runs >= 1) {
    process.env.KBOT_CLASSIFIER_STRESS_RUNS = String(Math.min(500, Math.floor(Number(argv.runs))));
  }
  if (argv.limit != null && Number.isFinite(argv.limit) && argv.limit >= 1) {
    process.env.KBOT_CLASSIFIER_LIMIT = String(Math.floor(Number(argv.limit)));
  }
  if (argv.timeout != null && Number.isFinite(argv.timeout) && argv.timeout > 0) {
    process.env.KBOT_CLASSIFIER_TIMEOUT_MS = String(Math.floor(Number(argv.timeout)));
  }
  if (argv['no-autostart'] === true) {
    process.env.KBOT_IPC_LLAMA_AUTOSTART = '0';
  }
  if (argv.backend === 'remote') {
    process.env.KBOT_IPC_CLASSIFIER_LLAMA = '0';
  } else if (argv.backend === 'local') {
    delete process.env.KBOT_IPC_CLASSIFIER_LLAMA;
  }

  if (featuresSet.has('stress') && (argv.runs == null || !Number.isFinite(argv.runs))) {
    if (!process.env.KBOT_CLASSIFIER_STRESS_RUNS) {
      process.env.KBOT_CLASSIFIER_STRESS_RUNS = '5';
    }
  }

  classifierFeatures = featuresSet;
  const out = { ...argv, featuresSet };
  classifierArgv = out;
  return out;
}
const EXE = distExePath(__dirname);
const stats = createAssert();
const { assert } = stats;

/** @see packages/kbot/.../JobViewer.tsx — business type options */
export const JOB_VIEWER_MACHINE_LABELS = [
  '3D printing service',
  'Drafting service',
  'Engraver',
  'Furniture maker',
  'Industrial engineer',
  'Industrial equipment supplier',
  'Laser cutting service',
  'Machine construction',
  'Machine repair service',
  'Machine shop',
  'Machine workshop',
  'Machinery parts manufacturer',
  'Machining manufacturer',
  'Manufacturer',
  'Mechanic',
  'Mechanical engineer',
  'Mechanical plant',
  'Metal fabricator',
  'Metal heat treating service',
  'Metal machinery supplier',
  'Metal working shop',
  'Metal workshop',
  'Novelty store',
  'Plywood supplier',
  'Sign shop',
  'Tool manufacturer',
  'Trophy shop',
];

const ANCHOR = 'machine workshop';

/** Keys we accept for the batch array when API forces a JSON object (e.g. response_format json_object). */
const BATCH_ARRAY_OBJECT_KEYS = ['items', 'distances', 'results', 'data', 'labels', 'rows'];

/** Build one prompt: plain mode = JSON array root; structured (-F structured) = JSON object with "items" (json_object API). */
function classifierBatchPrompt(labels) {
  const numbered = labels.map((l, i) => `${i + 1}. ${JSON.stringify(l)}`).join('\n');
  const structured = classifierFeatures.has('structured');

  const rules = `Rules for each element:
- Use shape: {"label": <exact string from the list below>, "distance": <number>}
- "distance" is semantic distance from 0 (same as anchor or direct synonym) to 10 (unrelated). One decimal allowed.
- Include EXACTLY one object per line item below, in the SAME ORDER, with "label" copied character-for-character from the list.

Anchor business type: ${ANCHOR}

Candidate labels (in order):
${numbered}`;

  if (structured) {
    return `You classify business types against one anchor. Output ONLY valid JSON: one object, no markdown fences, no commentary.
The API requires a JSON object (not a top-level array). Use exactly one top-level key "items" whose value is the array.

${rules}

Example: {"items":[{"label":"Example","distance":2.5},...]}`;
  }

  return `You classify business types against one anchor. Output ONLY a JSON array, no markdown fences, no commentary.

${rules}

Output: one JSON array, e.g. [{"label":"...","distance":2.5},...]`;
}

/**
 * Parse model text into the batch array: root [...] or {"items":[...]} (json_object).
 * @returns {unknown[] | null}
 */
function extractJsonArray(text) {
  if (!text || typeof text !== 'string') return null;
  let s = text.trim().replace(/^```(?:json)?\s*/i, '').replace(/\s*```$/u, '').trim();

  /** @param {unknown} v */
  const arrayFromParsed = (v) => {
    if (Array.isArray(v)) return v;
    if (!v || typeof v !== 'object') return null;
    const o = /** @type {Record<string, unknown>} */ (v);
    for (const key of BATCH_ARRAY_OBJECT_KEYS) {
      const a = o[key];
      if (Array.isArray(a)) return a;
    }
    for (const val of Object.values(o)) {
      if (
        Array.isArray(val) &&
        val.length > 0 &&
        val[0] &&
        typeof val[0] === 'object' &&
        val[0] !== null &&
        'label' in val[0]
      ) {
        return val;
      }
    }
    return null;
  };

  try {
    const v = JSON.parse(s);
    return arrayFromParsed(v);
  } catch {
    /* fall through */
  }

  const i = s.indexOf('[');
  const j = s.lastIndexOf(']');
  if (i >= 0 && j > i) {
    try {
      const v = JSON.parse(s.slice(i, j + 1));
      if (Array.isArray(v)) return v;
    } catch {
      /* ignore */
    }
  }

  const oi = s.indexOf('{');
  const oj = s.lastIndexOf('}');
  if (oi >= 0 && oj > oi) {
    try {
      const v = JSON.parse(s.slice(oi, oj + 1));
      return arrayFromParsed(v);
    } catch {
      /* ignore */
    }
  }

  return null;
}

/**
 * @param {unknown[]} arr
 * @param {string[]} expectedLabels — ordered
 */
function normalizeBatchArray(arr, expectedLabels) {
  const expectedSet = new Set(expectedLabels);
  const byLabel = new Map();

  for (const item of arr) {
    if (!item || typeof item !== 'object') continue;
    const label = item.label;
    let d = item.distance;
    if (typeof d === 'string') d = parseFloat(d);
    if (typeof label !== 'string' || typeof d !== 'number' || !Number.isFinite(d)) continue;
    if (!expectedSet.has(label)) continue;
    byLabel.set(label, d);
  }

  const distances = expectedLabels.map((label) => ({
    label,
    distance: byLabel.has(label) ? byLabel.get(label) : null,
  }));

  const missing = distances.filter((r) => r.distance == null).map((r) => r.label);
  return { distances, missing };
}

function batchTimeoutMs() {
  const raw = process.env.KBOT_CLASSIFIER_TIMEOUT_MS;
  if (raw === undefined || raw === '') return 30_000;
  const n = Number.parseInt(raw, 10);
  return Number.isFinite(n) && n > 0 ? n : 30_000;
}

/** Sequential batch iterations on one worker (stress). Default 1 = single run. */
function stressRunCount() {
  const raw = process.env.KBOT_CLASSIFIER_STRESS_RUNS;
  if (raw === undefined || raw === '') return 1;
  const n = Number.parseInt(String(raw).trim(), 10);
  if (!Number.isFinite(n) || n < 1) return 1;
  return Math.min(n, 500);
}

/** @param {unknown} llm — job_result.llm from kbot-ai */
function usageTokens(llm) {
  if (!llm || typeof llm !== 'object') return null;
  const u = /** @type {Record<string, unknown>} */ (llm).usage;
  if (!u || typeof u !== 'object') return null;
  const o = /** @type {Record<string, unknown>} */ (u);
  return {
    prompt: o.prompt_tokens ?? o.promptTokens ?? null,
    completion: o.completion_tokens ?? o.completionTokens ?? null,
    total: o.total_tokens ?? o.totalTokens ?? null,
  };
}

/** @param {number[]} values */
function summarizeMs(values) {
  if (values.length === 0) return null;
  const sorted = [...values].sort((a, b) => a - b);
  const sum = values.reduce((a, b) => a + b, 0);
  const mid = (a, b) => (a + b) / 2;
  const p = (q) => sorted[Math.min(sorted.length - 1, Math.max(0, Math.floor(q * (sorted.length - 1))))];
  return {
    min: sorted[0],
    max: sorted[sorted.length - 1],
    avg: Math.round((sum / values.length) * 100) / 100,
    p50: sorted.length % 2 ? sorted[Math.floor(sorted.length / 2)] : mid(sorted[sorted.length / 2 - 1], sorted[sorted.length / 2]),
    p95: p(0.95),
  };
}

/** Log progress while awaiting a long LLM call (no silent hang). */
function withHeartbeat(promise, ipcTimeoutMs, backendLabel) {
  const every = 10_000;
  let n = 0;
  const id = setInterval(() => {
    n += 1;
    const sec = (n * every) / 1000;
    console.log(
      `  … still waiting on ${backendLabel} (batch is large; ${sec}s elapsed, IPC deadline ${Math.round(ipcTimeoutMs / 1000)}s)…`
    );
  }, every);
  return promise.finally(() => clearInterval(id));
}

function buildKbotAiPayload(labels, tmo) {
  const prompt = classifierBatchPrompt(labels);
  const useLlama = ipcClassifierLlamaEnabled();
  const structured = classifierFeatures.has('structured');

  if (useLlama) {
    return { ...kbotAiPayloadLlamaLocal({ prompt }), llm_timeout_ms: tmo };
  }

  const payload = {
    ...kbotAiPayloadFromEnv(),
    prompt,
    llm_timeout_ms: tmo,
  };
  /** OpenAI-style structured outputs; forwarded by kbot LLMClient → liboai ChatCompletion. */
  if (structured) {
    payload.response_format = { type: 'json_object' };
  }

  const rawDst = classifierArgv?.dst || classifierArgv?.output;
  if (rawDst != null && String(rawDst).trim() !== '') {
    payload.dst = path.resolve(process.cwd(), String(rawDst).trim());
  }

  return payload;
}

/**
 * Parse kbot-ai job_result; updates assertion stats.
 * @returns {{ distances: {label:string,distance:number|null}[], missing: string[], parseError: string|null, rawText: string|null, batchOk: boolean }}
 */
function processBatchResponse(p, labels) {
  let rawText = null;
  let distances = [];
  let parseError = null;
  let missing = [];
  let batchOk = false;

  if (p?.status === 'success' && typeof p?.text === 'string') {
    rawText = p.text;
    const arr = extractJsonArray(p.text);
    if (arr) {
      const norm = normalizeBatchArray(arr, labels);
      distances = norm.distances;
      missing = norm.missing;
      if (missing.length === 0) {
        assert(true, 'batch JSON array: all labels have distance');
        batchOk = true;
      } else {
        assert(false, `batch array complete (${missing.length} missing labels)`);
        parseError = `missing: ${missing.join('; ')}`;
      }
    } else {
      assert(false, 'batch response parses as JSON array or {"items":[...]}');
      parseError = 'could not parse batch array from model text';
    }
  } else {
    assert(false, 'kbot-ai success');
    parseError = p?.error ?? 'not success';
  }

  return { distances, missing, parseError, rawText, batchOk };
}

async function runSingleBatch(ipc, labels, tmo, ipcDeadlineMs, waitLabel) {
  const payload = buildKbotAiPayload(labels, tmo);
  const t0 = performance.now();
  const pending = ipc.request({ type: 'kbot-ai', payload }, ipcDeadlineMs);
  const msg = classifierFeatures.has('no-heartbeat')
    ? await pending
    : await withHeartbeat(pending, ipcDeadlineMs, waitLabel);
  const elapsedMs = Math.round(performance.now() - t0);
  const p = payloadObj(msg);
  const parsed = processBatchResponse(p, labels);
  return { elapsedMs, p, ...parsed };
}

async function run() {
  const quiet = classifierFeatures.has('quiet');
  classifierMetricsCollector = createMetricsCollector();
  classifierRunStartedAt = new Date().toISOString();
  const startedAt = classifierRunStartedAt;
  const useLlama = ipcClassifierLlamaEnabled();
  const backendLabel = useLlama ? `llama @ :${llama.port}` : `router=${router.fromEnv()}`;
  if (!quiet) {
    console.log(`\n📐 IPC classifier (${backendLabel}) — one batch, distance vs "machine workshop"\n`);
    if (classifierFeatures.has('structured')) {
      if (useLlama) {
        console.log(
          `  ⚠️ -F structured: ignored for local llama (use --backend remote for response_format json_object)\n`
        );
      } else {
        console.log(
          `  Structured: response_format json_object + prompt asks for {"items":[...]} (not a top-level array)\n`
        );
      }
    }
  }

  if (!existsSync(EXE)) {
    console.error(`❌ Binary not found at ${EXE}`);
    process.exit(1);
  }

  if (useLlama) {
    await ensureLlamaLocalServer({
      autostart: llamaAutostartEnabled(),
      startTimeoutMs: timeouts.llamaServerStart,
    });
  }

  const limitRaw = process.env.KBOT_CLASSIFIER_LIMIT;
  let labels = [...JOB_VIEWER_MACHINE_LABELS];
  if (limitRaw !== undefined && limitRaw !== '') {
    const lim = Number.parseInt(limitRaw, 10);
    if (Number.isFinite(lim) && lim > 0) labels = labels.slice(0, lim);
  }

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
    } catch (e) {
      if (i === timeouts.connectAttempts - 1) throw e;
      await new Promise((r) => setTimeout(r, timeouts.connectRetryMs));
    }
  }

  const ipc = createIpcClient(socket);
  ipc.attach();
  await ipc.readyPromise;

  const tmo = batchTimeoutMs();
  const ipcDeadlineMs = tmo + 60_000;
  const waitLabel = useLlama ? 'llama' : router.fromEnv();
  const nRuns = stressRunCount();

  if (!quiet) {
    console.log(`  kbot-ai batch: ${labels.length} labels × ${nRuns} run(s)`);
    console.log(`  liboai HTTP timeout: ${tmo} ms (llm_timeout_ms) — rebuild kbot if this was stuck at ~30s before`);
    console.log(`  IPC wait deadline: ${ipcDeadlineMs} ms (HTTP + margin)`);
    const hb = classifierFeatures.has('no-heartbeat') ? 'off' : '15s';
    console.log(`  (Large batches can take many minutes; heartbeat ${hb}…)\n`);
  }

  /** @type {Array<{ index: number, wallMs: number, batchOk: boolean, parseError: string|null, usage: ReturnType<typeof usageTokens>}>} */
  const stressIterations = [];

  let lastP = /** @type {Record<string, unknown>|null} */ (null);
  let lastDistances = [];
  let lastRawText = null;
  let lastParseError = null;
  let lastByDistance = [];

  for (let r = 0; r < nRuns; r++) {
    if (nRuns > 1 && !quiet) {
      console.log(`  ── Stress run ${r + 1}/${nRuns} ──`);
    }
    const batch = await runSingleBatch(ipc, labels, tmo, ipcDeadlineMs, waitLabel);
    lastP = batch.p;
    lastDistances = batch.distances;
    lastRawText = batch.rawText;
    lastParseError = batch.parseError;
    lastByDistance = [...batch.distances].sort((a, b) => {
      if (a.distance == null && b.distance == null) return 0;
      if (a.distance == null) return 1;
      if (b.distance == null) return -1;
      return a.distance - b.distance;
    });

    const u = usageTokens(batch.p?.llm);
    stressIterations.push({
      index: r + 1,
      wallMs: batch.elapsedMs,
      batchOk: batch.batchOk,
      parseError: batch.parseError,
      usage: u,
    });

    if (nRuns > 1 && !quiet) {
      const tok = u
        ? `tokens p/c/t ${u.prompt ?? '—'}/${u.completion ?? '—'}/${u.total ?? '—'}`
        : 'tokens —';
      console.log(`  wall: ${batch.elapsedMs} ms  ${batch.batchOk ? 'OK' : 'FAIL'}  ${tok}`);
    }
  }

  const wallMsList = stressIterations.map((x) => x.wallMs);
  /** @type {null | { requestedRuns: number, wallMs: NonNullable<ReturnType<typeof summarizeMs>>, successCount: number, failCount: number, totalPromptTokens: number, totalCompletionTokens: number, totalTokens: number }} */
  let stressSummary = null;
  if (nRuns > 1) {
    const w = summarizeMs(wallMsList);
    stressSummary = {
      requestedRuns: nRuns,
      wallMs: /** @type {NonNullable<typeof w>} */ (w),
      successCount: stressIterations.filter((x) => x.batchOk).length,
      failCount: stressIterations.filter((x) => !x.batchOk).length,
      totalPromptTokens: stressIterations.reduce((s, x) => s + (Number(x.usage?.prompt) || 0), 0),
      totalCompletionTokens: stressIterations.reduce((s, x) => s + (Number(x.usage?.completion) || 0), 0),
      totalTokens: stressIterations.reduce((s, x) => s + (Number(x.usage?.total) || 0), 0),
    };
    if (quiet) {
      console.log(
        `stress ${nRuns} runs: min=${stressSummary.wallMs.min}ms max=${stressSummary.wallMs.max}ms avg=${stressSummary.wallMs.avg}ms ok=${stressSummary.successCount}/${nRuns} tokensΣ=${stressSummary.totalTokens}`
      );
    } else {
      console.log(`\n  ═══════════════ Stress summary (${nRuns} batch runs) ═══════════════`);
      console.log(
        `  Wall time (ms): min ${stressSummary.wallMs.min}  max ${stressSummary.wallMs.max}  avg ${stressSummary.wallMs.avg}  p50 ${stressSummary.wallMs.p50}  p95 ${stressSummary.wallMs.p95}`
      );
      console.log(
        `  Batches OK: ${stressSummary.successCount}  fail: ${stressSummary.failCount}  (assertions: passed ${stats.passed}  failed ${stats.failed})`
      );
      if (
        stressSummary.totalPromptTokens > 0 ||
        stressSummary.totalCompletionTokens > 0 ||
        stressSummary.totalTokens > 0
      ) {
        console.log(
          `  Token totals (sum over runs): prompt ${stressSummary.totalPromptTokens}  completion ${stressSummary.totalCompletionTokens}  total ${stressSummary.totalTokens}`
        );
      }
      console.log(`  ═══════════════════════════════════════════════════════════════════\n`);
    }
  }

  const p = lastP;
  const distances = lastDistances;
  const rawText = lastRawText;
  const parseError = lastParseError;
  const byDistance = lastByDistance;

  const shutdownRes = await ipc.request({ type: 'shutdown' }, timeouts.ipcDefault);
  assert(shutdownRes.type === 'shutdown_ack', 'shutdown ack');
  await new Promise((r) => setTimeout(r, timeouts.postShutdownMs));
  socket.destroy();
  assert(workerProc.exitCode === 0, 'worker exit 0');

  const finishedAt = new Date().toISOString();

  const reportNow = new Date();
  const cwd = process.cwd();

  const reportData = {
    startedAt,
    finishedAt,
    passed: stats.passed,
    failed: stats.failed,
    ok: stats.failed === 0,
    ipcClassifierLlama: useLlama,
    cli: {
      features: [...classifierFeatures],
      provider: process.env.KBOT_ROUTER ?? null,
      model: process.env.KBOT_IPC_MODEL ?? null,
      backend: useLlama ? 'local' : 'remote',
      stressRuns: nRuns,
      structuredOutput: !useLlama && classifierFeatures.has('structured'),
      dst:
        classifierArgv?.dst || classifierArgv?.output
          ? path.resolve(
              process.cwd(),
              String(classifierArgv.dst || classifierArgv.output).trim()
            )
          : null,
    },
    env: {
      KBOT_IPC_CLASSIFIER_LLAMA: process.env.KBOT_IPC_CLASSIFIER_LLAMA ?? null,
      KBOT_IPC_LLAMA_AUTOSTART: process.env.KBOT_IPC_LLAMA_AUTOSTART ?? null,
      KBOT_ROUTER: process.env.KBOT_ROUTER ?? null,
      KBOT_IPC_MODEL: process.env.KBOT_IPC_MODEL ?? null,
      KBOT_CLASSIFIER_LIMIT: process.env.KBOT_CLASSIFIER_LIMIT ?? null,
      KBOT_CLASSIFIER_TIMEOUT_MS: process.env.KBOT_CLASSIFIER_TIMEOUT_MS ?? null,
      KBOT_CLASSIFIER_STRESS_RUNS: process.env.KBOT_CLASSIFIER_STRESS_RUNS ?? null,
      KBOT_LLAMA_PORT: process.env.KBOT_LLAMA_PORT ?? null,
      KBOT_LLAMA_BASE_URL: process.env.KBOT_LLAMA_BASE_URL ?? null,
    },
    metrics: buildMetricsBundle(classifierMetricsCollector, startedAt, finishedAt),
    anchor: ANCHOR,
    source: 'JobViewer.tsx:205',
    batch: true,
    backend: useLlama ? 'llama_local' : 'remote_router',
    ...(useLlama
      ? {
          llama: {
            baseURL: llama.baseURL,
            port: llama.port,
            router: llama.router,
            model: llama.model,
          },
        }
      : {
          router: router.fromEnv(),
          model: process.env.KBOT_IPC_MODEL ?? null,
        }),
    labelCount: labels.length,
    /** Provider metadata from API (usage, model, id, OpenRouter fields) — see LLMClient + kbot `llm` key */
    llm: p?.llm ?? null,
    distances,
    byDistance,
    rawText,
    parseError: parseError ?? null,
    ...(nRuns > 1 && stressSummary
      ? {
          stress: {
            requestedRuns: nRuns,
            iterations: stressIterations,
            summary: stressSummary,
          },
        }
      : {}),
  };

  let jsonPath = '';
  let mdPath = '';
  let arrayPath = '';
  if (!classifierFeatures.has('no-report')) {
    try {
      const written = await writeTestReports('test-ipc-classifier', reportData, { cwd, now: reportNow });
      jsonPath = written.jsonPath;
      mdPath = written.mdPath;
    } catch (e) {
      console.error('  ⚠️ Failed to write report:', e?.message ?? e);
    }

    /** Array-only artifact (same timestamp as main report). */
    arrayPath = reportFilePathWithExt('test-ipc-classifier-distances', '.json', { cwd, now: reportNow });
    await mkdir(path.dirname(arrayPath), { recursive: true });
    await writeFile(arrayPath, `${JSON.stringify(distances, null, 2)}\n`, 'utf8');
  }

  const { label: timeLabel } = timeParts(reportNow);
  if (!classifierFeatures.has('quiet')) {
    console.log(`\n────────────────────────────────`);
    console.log(`  Passed: ${stats.passed}  Failed: ${stats.failed}`);
    if (jsonPath) console.log(`  Report JSON: ${jsonPath}`);
    if (mdPath) console.log(`  Report MD:   ${mdPath}`);
    if (arrayPath) console.log(`  Distances JSON: ${arrayPath}`);
    console.log(`  Run id: test-ipc-classifier::${timeLabel}`);
    console.log(`  distances.length: ${distances.length}`);
    console.log(`────────────────────────────────\n`);
  } else {
    console.log(
      `done: passed=${stats.passed} failed=${stats.failed} ok=${stats.failed === 0}${jsonPath ? ` json=${jsonPath}` : ''}`
    );
  }

  process.exit(stats.failed > 0 ? 1 : 0);
}

parseClassifierArgv();
run().catch(async (err) => {
  console.error('Classifier error:', err);
  if (!classifierFeatures.has('no-report')) {
    try {
      const finishedAt = new Date().toISOString();
      const c = classifierMetricsCollector ?? createMetricsCollector();
      const started = classifierRunStartedAt ?? finishedAt;
      await writeTestReports(
        'test-ipc-classifier',
        {
          startedAt: started,
          finishedAt,
          error: String(err?.stack ?? err),
          passed: stats.passed,
          failed: stats.failed,
          ok: false,
          ipcClassifierLlama: ipcClassifierLlamaEnabled(),
          metrics: buildMetricsBundle(c, started, finishedAt),
        },
        { cwd: process.cwd() }
      );
    } catch (_) {
      /* ignore */
    }
  }
  process.exit(1);
});
