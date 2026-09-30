/**
 * orchestrator/reports.js — JSON + Markdown reports under cwd/tests/
 *
 * File pattern (logical): test-name::hh:mm
 * On-disk: test-name__HH-mm.json / .md (Windows: no `:` in filenames)
 */

import { readFileSync, statSync } from 'node:fs';
import { mkdir, writeFile } from 'node:fs/promises';
import { join, dirname, basename } from 'node:path';
import os from 'node:os';
import { performance } from 'node:perf_hooks';
import { resourceUsage } from 'node:process';

const WIN_BAD = /[<>:"/\\|?*\x00-\x1f]/g;

/** Strip characters invalid in Windows / POSIX filenames. */
export function sanitizeTestName(name) {
  const s = String(name).trim().replace(WIN_BAD, '_').replace(/\s+/g, '_');
  return s || 'test';
}

/**
 * @param {Date} [now]
 * @returns {{ hh: string, mm: string, label: string }}
 */
export function timeParts(now = new Date()) {
  const hh = String(now.getHours()).padStart(2, '0');
  const mm = String(now.getMinutes()).padStart(2, '0');
  return { hh, mm, label: `${hh}:${mm}` };
}

/**
 * @param {string} testName
 * @param {string} ext — including dot, e.g. '.json'
 * @param {{ cwd?: string, now?: Date }} [options]
 */
export function reportFilePathWithExt(testName, ext, options = {}) {
  const cwd = options.cwd ?? process.cwd();
  const now = options.now ?? new Date();
  const base = sanitizeTestName(testName);
  const { hh, mm } = timeParts(now);
  const file = `${base}__${hh}-${mm}${ext}`;
  return join(cwd, 'tests', file);
}

export function reportFilePath(testName, options = {}) {
  return reportFilePathWithExt(testName, '.json', options);
}

export function reportMarkdownPath(testName, options = {}) {
  return reportFilePathWithExt(testName, '.md', options);
}

function formatBytes(n) {
  if (typeof n !== 'number' || Number.isNaN(n)) return String(n);
  const u = ['B', 'KB', 'MB', 'GB'];
  let i = 0;
  let x = n;
  while (x >= 1024 && i < u.length - 1) {
    x /= 1024;
    i++;
  }
  return `${x < 10 && i > 0 ? x.toFixed(1) : Math.round(x)} ${u[i]}`;
}

/**
 * @param {string} filePath
 * @returns {number | null}
 */
export function fileByteSize(filePath) {
  try {
    return statSync(filePath).size;
  } catch {
    return null;
  }
}

/**
 * PNG IHDR width/height (first chunk after 8-byte signature).
 * @param {Buffer | Uint8Array} buf
 * @returns {{ width: number, height: number } | null}
 */
export function pngDimensionsFromBuffer(buf) {
  const b = Buffer.isBuffer(buf) ? buf : Buffer.from(buf);
  if (b.length < 24) return null;
  if (b[0] !== 0x89 || b[1] !== 0x50 || b[2] !== 0x4e || b[3] !== 0x47) return null;
  if (b.slice(12, 16).toString('ascii') !== 'IHDR') return null;
  const width = b.readUInt32BE(16);
  const height = b.readUInt32BE(20);
  if (!Number.isFinite(width) || !Number.isFinite(height) || width <= 0 || height <= 0 || width > 1e6 || height > 1e6)
    return null;
  return { width, height };
}

/**
 * @param {string} filePath
 * @returns {{ path: string, name: string, bytes: number | null, widthPx: number | null, heightPx: number | null, kind: string }}
 */
export function describePngFile(filePath) {
  const name = basename(filePath);
  const bytes = fileByteSize(filePath);
  let widthPx = null;
  let heightPx = null;
  try {
    const raw = readFileSync(filePath);
    const dim = pngDimensionsFromBuffer(raw);
    if (dim) {
      widthPx = dim.width;
      heightPx = dim.height;
    }
  } catch {
    /* ignore */
  }
  return { path: filePath, name, bytes, widthPx, heightPx, kind: 'png' };
}

/** Snapshot of host / OS (cheap; call anytime). */
export function hostSnapshot() {
  const cpus = os.cpus();
  const total = os.totalmem();
  const free = os.freemem();
  return {
    hostname: os.hostname(),
    platform: os.platform(),
    arch: os.arch(),
    release: os.release(),
    cpuCount: cpus.length,
    cpuModel: cpus[0]?.model?.trim() ?? '',
    totalMemBytes: total,
    freeMemBytes: free,
    usedMemBytes: total - free,
    loadAvg: os.loadavg(),
    osUptimeSec: os.uptime(),
  };
}

/**
 * Call at test start; then call `.finalize()` at end for wall + CPU delta + memory.
 */
export function createMetricsCollector() {
  const cpu0 = process.cpuUsage();
  const perf0 = performance.now();
  const wall0 = Date.now();

  return {
    hostSnapshot,

    finalize() {
      const cpu = process.cpuUsage(cpu0);
      const perf1 = performance.now();
      let ru = null;
      try {
        ru = resourceUsage();
      } catch {
        /* older runtimes */
      }
      return {
        durationWallMs: Math.round((perf1 - perf0) * 1000) / 1000,
        durationClockMs: Date.now() - wall0,
        cpuUserUs: cpu.user,
        cpuSystemUs: cpu.system,
        cpuUserMs: cpu.user / 1000,
        cpuSystemMs: cpu.system / 1000,
        memory: process.memoryUsage(),
        resourceUsage: ru,
        pid: process.pid,
        node: process.version,
        processUptimeSec: process.uptime(),
      };
    },
  };
}

/**
 * @param {Record<string, unknown>} payload
 * @returns {string}
 */
export function renderMarkdownReport(payload) {
  const meta = payload.meta ?? {};
  const m = payload.metrics ?? {};
  const host = m.host ?? {};
  const timing = m.timing ?? {};
  const proc = m.process ?? {};
  const tStart = timing.startedAt ?? payload.startedAt;
  const tEnd = timing.finishedAt ?? payload.finishedAt;

  const lines = [];

  lines.push(`# Test report: ${meta.displayName ?? meta.testName ?? 'run'}`);
  lines.push('');
  lines.push('## Summary');
  lines.push('');
  lines.push(`| Key | Value |`);
  lines.push(`| --- | --- |`);
  lines.push(`| Result | ${payload.ok === true ? 'PASS' : payload.ok === false ? 'FAIL' : '—'} |`);
  if (payload.passed != null) lines.push(`| Assertions passed | ${payload.passed} |`);
  if (payload.failed != null) lines.push(`| Assertions failed | ${payload.failed} |`);
  if (payload.abortReason) {
    lines.push(`| Aborted | ${String(payload.abortReason).replace(/\|/g, '\\|')} |`);
  }
  if (payload.uncaughtError) {
    const u = String(payload.uncaughtError);
    lines.push(
      `| Uncaught | ${u.length > 600 ? `${u.slice(0, 600).replace(/\|/g, '\\|')}…` : u.replace(/\|/g, '\\|')} |`
    );
  }
  if (payload.ipcLlm != null) lines.push(`| IPC LLM step | ${payload.ipcLlm ? 'enabled' : 'skipped'} |`);
  if (payload.ipcLlama != null) {
    lines.push(`| IPC llama :8888 step | ${payload.ipcLlama ? 'enabled' : 'skipped'} |`);
  }
  if (payload.ipcClassifierLlama != null) {
    lines.push(
      `| IPC classifier | ${payload.ipcClassifierLlama ? 'local llama :8888' : 'remote (KBOT_ROUTER / KBOT_IPC_MODEL)'} |`
    );
  }
  lines.push(`| CWD | \`${String(meta.cwd ?? '').replace(/`/g, "'")}\` |`);
  if (meta.exe != null && String(meta.exe).length) {
    lines.push(`| Test binary | \`${String(meta.exe).replace(/`/g, "'")}\` |`);
  }
  if (meta.assetsDir != null && String(meta.assetsDir).length) {
    lines.push(`| Assets dir | \`${String(meta.assetsDir).replace(/`/g, "'")}\` |`);
  }
  if (meta.argv != null && String(meta.argv).length) {
    lines.push(`| CLI args | \`${String(meta.argv).replace(/`/g, "'")}\` |`);
  }
  if (meta.wallClockMs != null) {
    lines.push(`| Wall clock (total script) | **${meta.wallClockMs} ms** |`);
  }
  lines.push('');

  lines.push('## Timing');
  lines.push('');
  lines.push(`| Metric | Value |`);
  lines.push(`| --- | --- |`);
  if (tStart) lines.push(`| Started (ISO) | ${tStart} |`);
  if (tEnd) lines.push(`| Finished (ISO) | ${tEnd} |`);
  if (proc.durationWallMs != null) lines.push(`| Wall time (perf) | ${proc.durationWallMs} ms |`);
  if (proc.durationClockMs != null) lines.push(`| Wall time (clock) | ${proc.durationClockMs} ms |`);
  lines.push('');

  lines.push('## Process (Node)');
  lines.push('');
  lines.push(`| Metric | Value |`);
  lines.push(`| --- | --- |`);
  if (proc.pid != null) lines.push(`| PID | ${proc.pid} |`);
  if (proc.node) lines.push(`| Node | ${proc.node} |`);
  if (proc.processUptimeSec != null) lines.push(`| process.uptime() | ${proc.processUptimeSec.toFixed(3)} s |`);
  if (proc.cpuUserMs != null && proc.cpuSystemMs != null) {
    lines.push(`| CPU user (process.cpuUsage Δ) | ${proc.cpuUserMs.toFixed(3)} ms (${proc.cpuUserUs ?? '—'} µs) |`);
    lines.push(`| CPU system (process.cpuUsage Δ) | ${proc.cpuSystemMs.toFixed(3)} ms (${proc.cpuSystemUs ?? '—'} µs) |`);
  }
  const ru = proc.resourceUsage;
  if (ru && typeof ru === 'object') {
    if (ru.userCPUTime != null) {
      lines.push(`| CPU user (resourceUsage) | ${(ru.userCPUTime / 1000).toFixed(3)} ms |`);
    }
    if (ru.systemCPUTime != null) {
      lines.push(`| CPU system (resourceUsage) | ${(ru.systemCPUTime / 1000).toFixed(3)} ms |`);
    }
    if (ru.maxRSS != null) {
      lines.push(`| Max RSS (resourceUsage) | ${formatBytes(ru.maxRSS * 1024)} |`);
    }
  }
  const mem = proc.memory;
  if (mem && typeof mem === 'object') {
    lines.push(`| RSS | ${formatBytes(mem.rss)} (${mem.rss} B) |`);
    lines.push(`| Heap used | ${formatBytes(mem.heapUsed)} |`);
    lines.push(`| Heap total | ${formatBytes(mem.heapTotal)} |`);
    lines.push(`| External | ${formatBytes(mem.external)} |`);
    if (mem.arrayBuffers != null) lines.push(`| Array buffers | ${formatBytes(mem.arrayBuffers)} |`);
  }
  lines.push('');

  lines.push('## Host');
  lines.push('');
  lines.push(`| Metric | Value |`);
  lines.push(`| --- | --- |`);
  if (host.hostname) lines.push(`| Hostname | ${host.hostname} |`);
  if (host.platform) lines.push(`| OS | ${host.platform} ${host.release ?? ''} |`);
  if (host.arch) lines.push(`| Arch | ${host.arch} |`);
  if (host.cpuCount != null) lines.push(`| CPUs | ${host.cpuCount} |`);
  if (host.cpuModel) lines.push(`| CPU model | ${host.cpuModel} |`);
  if (host.totalMemBytes != null) {
    lines.push(`| RAM total | ${formatBytes(host.totalMemBytes)} |`);
    lines.push(`| RAM free | ${formatBytes(host.freeMemBytes)} |`);
    lines.push(`| RAM used | ${formatBytes(host.usedMemBytes)} |`);
  }
  if (host.loadAvg && host.loadAvg.length) {
    lines.push(`| Load avg (1/5/15) | ${host.loadAvg.map((x) => x.toFixed(2)).join(' / ')} |`);
  }
  if (host.osUptimeSec != null) lines.push(`| OS uptime | ${(host.osUptimeSec / 3600).toFixed(2)} h |`);
  lines.push('');

  const escCell = (s) =>
    String(s ?? '')
      .replace(/\|/g, '\\|')
      .replace(/\r\n/g, '<br>')
      .replace(/\n/g, '<br>');

  const fmtBytesCell = (n) => {
    if (n == null || n === '') return '—';
    const num = Number(n);
    if (!Number.isFinite(num)) return escCell(String(n));
    return `${formatBytes(num)} (${num} B)`;
  };
  const fmtMsCell = (ms) => {
    if (ms == null || ms === '') return '—';
    const n = Number(ms);
    if (!Number.isFinite(n)) return '—';
    return String(Math.round(n * 100) / 100);
  };

  if (Array.isArray(payload.images) && payload.images.length > 0) {
    lines.push('## Images & transfers');
    lines.push('');
    lines.push('| Label | Input | Output | Time (ms) | Detail |');
    lines.push('| --- | --- | --- | ---: | --- |');
    for (const row of payload.images) {
      const label = escCell(row.label ?? '—');
      const outRaw = row.outputBytes != null ? row.outputBytes : row.bytes;
      const inputCol = fmtBytesCell(row.inputBytes);
      const outputCol = fmtBytesCell(outRaw);
      const timeCol = fmtMsCell(row.computeMs != null ? row.computeMs : row.ms);
      const parts = [];
      if (row.widthPx != null && row.heightPx != null) {
        parts.push(`${row.widthPx}×${row.heightPx} px`);
      }
      if (row.contentType) parts.push(String(row.contentType));
      if (row.note) parts.push(String(row.note));
      if (row.detail) parts.push(String(row.detail));
      const detailCol = parts.length ? escCell(parts.join(' · ')) : '—';
      lines.push(`| ${label} | ${inputCol} | ${outputCol} | ${timeCol} | ${detailCol} |`);
    }
    lines.push('');
  }

  if (Array.isArray(payload.mediaSuites) && payload.mediaSuites.length > 0) {
    lines.push('## Integration: performance by suite');
    lines.push('');
    for (const s of payload.mediaSuites) {
      lines.push(`### ${escCell(s.name)}`);
      lines.push('');
      lines.push(`- **Suite wall time:** ${Math.round(s.totalMs * 100) / 100} ms`);
      lines.push('');
      const L = s.llmLive;
      if (L && typeof L === 'object' && (L.prompt != null || L.expected != null || L.actual != null || L.roundTripMs != null)) {
        lines.push('#### Live LLM (brief)');
        lines.push('');
        lines.push('| Field | Value |');
        lines.push('| --- | --- |');
        if (L.title) lines.push(`| Case | ${escCell(L.title)} |`);
        if (L.prompt != null) lines.push(`| Prompt | ${escCell(L.prompt)} |`);
        if (L.expected != null) lines.push(`| Expected | ${escCell(L.expected)} |`);
        if (L.actual != null) lines.push(`| Actual | ${escCell(L.actual)} |`);
        if (L.roundTripMs != null) {
          const n = Number(L.roundTripMs);
          lines.push(`| Round trip (ms) | ${Number.isFinite(n) ? String(Math.round(n * 100) / 100) : escCell(L.roundTripMs)} |`);
        }
        lines.push('');
      }
      if (s.steps && s.steps.length) {
        lines.push('| Step | ms | Detail |');
        lines.push('| --- | ---: | --- |');
        for (const st of s.steps) {
          lines.push(`| ${escCell(st.label)} | ${st.ms} | ${st.detail ? escCell(st.detail) : '—'} |`);
        }
        lines.push('');
      } else {
        lines.push('*(no step-level timings)*');
        lines.push('');
      }
    }
  }

  if (Array.isArray(payload.integrationNotes) && payload.integrationNotes.length > 0) {
    lines.push('### Integration notes');
    lines.push('');
    for (const n of payload.integrationNotes) {
      lines.push(`- ${escCell(n)}`);
    }
    lines.push('');
  }

  const kbotAi = payload.kbotAi;
  const hasKbotAiMeta =
    kbotAi &&
    typeof kbotAi === 'object' &&
    (kbotAi.routerStep != null || kbotAi.llamaStep != null);
  const hasClassifierLlm = payload.llm != null && typeof payload.llm === 'object';
  if (hasKbotAiMeta || hasClassifierLlm) {
    lines.push('## LLM API (provider JSON)');
    lines.push('');
    lines.push(
      'Fields from the chat completion response except assistant message bodies (`usage`, `model`, `id`, provider-specific).'
    );
    lines.push('');
    if (hasKbotAiMeta) {
      if (kbotAi.routerStep != null) {
        lines.push('### IPC step 6 — router / main kbot-ai');
        lines.push('');
        lines.push('```json');
        lines.push(JSON.stringify(kbotAi.routerStep, null, 2));
        lines.push('```');
        lines.push('');
      }
      if (kbotAi.llamaStep != null) {
        lines.push('### IPC step 7 — local llama :8888');
        lines.push('');
        lines.push('```json');
        lines.push(JSON.stringify(kbotAi.llamaStep, null, 2));
        lines.push('```');
        lines.push('');
      }
    }
    if (hasClassifierLlm) {
      lines.push('### Classifier — batched kbot-ai');
      lines.push('');
      lines.push('```json');
      lines.push(JSON.stringify(payload.llm, null, 2));
      lines.push('```');
      lines.push('');
    }
  }

  if (payload.anchor != null || (Array.isArray(payload.distances) && payload.distances.length > 0)) {
    lines.push('## Classifier batch');
    lines.push('');
    lines.push(`| Key | Value |`);
    lines.push(`| --- | --- |`);
    if (payload.anchor != null) lines.push(`| Anchor | ${payload.anchor} |`);
    if (payload.labelCount != null) lines.push(`| Label count | ${payload.labelCount} |`);
    if (payload.backend != null) lines.push(`| Backend | ${payload.backend} |`);
    const pe = payload.parseError;
    if (pe != null && String(pe).length) {
      lines.push(`| Parse | Failed: ${String(pe).replace(/\|/g, '\\|').slice(0, 500)}${String(pe).length > 500 ? '…' : ''} |`);
    } else {
      lines.push(`| Parse | OK |`);
    }
    lines.push('');
    const sorted = Array.isArray(payload.byDistance) ? payload.byDistance : [];
    const preview = sorted.filter((r) => r && r.distance != null).slice(0, 12);
    if (preview.length > 0) {
      lines.push('### Nearest labels (by distance)');
      lines.push('');
      lines.push(`| Label | Distance |`);
      lines.push(`| --- | ---: |`);
      for (const row of preview) {
        const lab = String(row.label ?? '').replace(/\|/g, '\\|');
        lines.push(`| ${lab} | ${row.distance} |`);
      }
      lines.push('');
    }
  }

  if (payload.stress?.summary && typeof payload.stress.summary === 'object') {
    const s = payload.stress.summary;
    const w = s.wallMs;
    lines.push('## Classifier stress (batch repeats)');
    lines.push('');
    lines.push(`| Metric | Value |`);
    lines.push(`| --- | --- |`);
    lines.push(`| Requested runs | ${s.requestedRuns ?? '—'} |`);
    if (w && typeof w === 'object') {
      lines.push(
        `| Wall time (ms) | min ${w.min} · max ${w.max} · avg ${w.avg} · p50 ${w.p50} · p95 ${w.p95} |`
      );
    }
    lines.push(`| Batch OK / fail | ${s.successCount ?? '—'} / ${s.failCount ?? '—'} |`);
    if (s.totalTokens > 0 || s.totalPromptTokens > 0 || s.totalCompletionTokens > 0) {
      lines.push(
        `| Σ tokens (prompt / completion / total) | ${s.totalPromptTokens} / ${s.totalCompletionTokens} / ${s.totalTokens} |`
      );
    }
    lines.push('');
  }

  if (payload.env && typeof payload.env === 'object') {
    lines.push('## Environment (selected)');
    lines.push('');
    lines.push(`| Variable | Value |`);
    lines.push(`| --- | --- |`);
    for (const [k, v] of Object.entries(payload.env)) {
      lines.push(`| \`${k}\` | ${v === null || v === undefined ? '—' : String(v)} |`);
    }
    lines.push('');
  }

  if (payload.error) {
    lines.push('## Error');
    lines.push('');
    lines.push('```');
    lines.push(String(payload.error));
    lines.push('```');
    lines.push('');
  }

  lines.push('---');
  lines.push(`*Written ${meta.writtenAt ?? new Date().toISOString()}*`);
  lines.push('');

  return lines.join('\n');
}

/**
 * Build metrics block for JSON + MD (host snapshot + process finalize).
 */
export function buildMetricsBundle(collector, startedAtIso, finishedAtIso) {
  const host = collector.hostSnapshot();
  const processMetrics = collector.finalize();
  return {
    timing: {
      startedAt: startedAtIso,
      finishedAt: finishedAtIso,
    },
    host,
    process: processMetrics,
  };
}

/**
 * @param {string} testName
 * @param {Record<string, unknown>} data — merged into payload (meta + metrics added)
 * @param {{ cwd?: string, now?: Date, metrics?: object }} [options]
 * @returns {Promise<{ jsonPath: string, mdPath: string }>}
 */
export async function writeTestReports(testName, data, options = {}) {
  const cwd = options.cwd ?? process.cwd();
  const now = options.now ?? new Date();
  const jsonPath = reportFilePath(testName, { cwd, now });
  const mdPath = reportMarkdownPath(testName, { cwd, now });
  const { hh, mm, label } = timeParts(now);

  const base = sanitizeTestName(testName);
  const payload = {
    meta: {
      testName: base,
      displayName: `${base}::${label}`,
      cwd,
      writtenAt: now.toISOString(),
      jsonFile: jsonPath,
      mdFile: mdPath,
    },
    ...data,
  };

  await mkdir(dirname(jsonPath), { recursive: true });
  await writeFile(jsonPath, JSON.stringify(payload, null, 2), 'utf8');

  const md = renderMarkdownReport(payload);
  await writeFile(mdPath, md, 'utf8');

  return { jsonPath, mdPath };
}

/** @deprecated Prefer writeTestReports */
export async function writeJsonReport(testName, data, options = {}) {
  const { jsonPath } = await writeTestReports(testName, data, options);
  return jsonPath;
}
