/**
 * orchestrator/spawn.mjs
 *
 * Spawn a C++ worker as a child process, send/receive length-prefixed
 * JSON messages over stdin/stdout.
 *
 * Usage:
 *   import { spawnWorker } from './spawn.mjs';
 *   const w = await spawnWorker('./dist/polymech-cli.exe');
 *   console.log(res); // { id: '...', type: 'pong', payload: {} }
 *   await w.shutdown();
 */

import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';

// ── frame helpers ────────────────────────────────────────────────────────────

/** Write a 4-byte LE length + JSON body to a writable stream. */
function writeFrame(stream, msg) {
  const body = JSON.stringify(msg);
  const bodyBuf = Buffer.from(body, 'utf8');
  const lenBuf = Buffer.alloc(4);
  lenBuf.writeUInt32LE(bodyBuf.length, 0);
  stream.write(Buffer.concat([lenBuf, bodyBuf]));
}

/**
 * Creates a streaming frame parser.
 * Calls `onMessage(parsed)` for each complete frame.
 */
function createFrameReader(onMessage) {
  let buffer = Buffer.alloc(0);

  return (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);

    while (buffer.length >= 4) {
      const bodyLen = buffer.readUInt32LE(0);
      const totalLen = 4 + bodyLen;

      if (buffer.length < totalLen) break; // need more data

      const bodyBuf = buffer.subarray(4, totalLen);
      buffer = buffer.subarray(totalLen);

      try {
        const msg = JSON.parse(bodyBuf.toString('utf8'));
        onMessage(msg);
      } catch (e) {
        console.error('[orchestrator] failed to parse frame:', e.message);
      }
    }
  };
}

// ── spawnWorker ──────────────────────────────────────────────────────────────

/**
 * Spawn the C++ binary in `worker` mode.
 * Returns: { send, request, shutdown, kill, process, ready }
 *
 * `ready` is a Promise that resolves when the worker sends `{ type: 'ready' }`.
 */
export function spawnWorker(exePath, args = ['worker']) {
  const proc = spawn(exePath, args, {
    stdio: ['pipe', 'pipe', 'pipe'],
  });

  // Pending request map: id → { resolve, reject, timer }
  const pending = new Map();

  // Event handler for unmatched messages (progress events, etc.)
  let eventHandler = null;

  let readyResolve;
  const ready = new Promise((resolve) => { readyResolve = resolve; });

  // stderr → console (worker logs via spdlog go to stderr)
  proc.stderr.on('data', (chunk) => {
    const text = chunk.toString().trim();
    if (text) console.error(`[worker:stderr] ${text}`);
  });

  // stdout → frame parser → route by id / type
  const feedData = createFrameReader((msg) => {
    // Handle the initial "ready" signal
    if (msg.type === 'ready') {
      readyResolve(msg);
      return;
    }

    // Route response to pending request
    if (msg.id && pending.has(msg.id)) {
      const { resolve, timer } = pending.get(msg.id);
      clearTimeout(timer);
      pending.delete(msg.id);
      resolve(msg);
      return;
    }

    // Unmatched message (progress event, broadcast, etc.)
    if (eventHandler) {
      eventHandler(msg);
    } else {
      console.log('[orchestrator] unmatched message:', msg);
    }
  });

  proc.stdout.on('data', feedData);

  // ── public API ──────────────────────────────────────────────────────────

  /** Fire-and-forget send. */
  function send(msg) {
    if (!msg.id) msg.id = randomUUID();
    writeFrame(proc.stdin, msg);
  }

  /** Send a message and wait for the response with matching `id`. */
  function request(msg, timeoutMs = 5000) {
    return new Promise((resolve, reject) => {
      const id = msg.id || randomUUID();
      msg.id = id;

      const timer = setTimeout(() => {
        pending.delete(id);
        reject(new Error(`IPC request timed out after ${timeoutMs}ms (id=${id}, type=${msg.type})`));
      }, timeoutMs);

      pending.set(id, { resolve, reject, timer });
      writeFrame(proc.stdin, msg);
    });
  }

  /** Graceful shutdown: send shutdown message & wait for process exit. */
  async function shutdown(timeoutMs = 3000) {
    const res = await request({ type: 'shutdown' }, timeoutMs);
    // Wait for process to exit
    await new Promise((resolve) => {
      const timer = setTimeout(() => {
        proc.kill();
        resolve();
      }, timeoutMs);
      proc.on('exit', () => { clearTimeout(timer); resolve(); });
    });
    return res;
  }

  return {
    send,
    request,
    shutdown,
    kill: () => proc.kill(),
    process: proc,
    ready,
    onEvent: (handler) => { eventHandler = handler; },
  };
}
