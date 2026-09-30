#!/usr/bin/env node
/**
 * Modbus xblox smoke test.
 *
 * Spins up a local Modbus TCP server block in one pm-image-cli process, then
 * drives it from a second xblox document using read/write/read blocks.
 */

import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { createAssert } from './test-commons.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = resolve(__dirname, '..', '..');

function cliExePath() {
  const env = process.env.PM_IMAGE_CLI || process.env.MEDIA_IMG_CLI_EXE;
  if (env && env.trim()) return resolve(env.trim());
  const name = process.platform === 'win32' ? 'pm-image-cli.exe' : 'pm-image-cli';
  const winX64 = resolve(PACKAGE_ROOT, 'dist', 'win-x64', name);
  if (existsSync(winX64)) return winX64;
  return resolve(PACKAGE_ROOT, 'dist', name);
}

function parseTrailingJson(stdout) {
  const text = String(stdout || '').trim();
  const starts = [text.lastIndexOf('\n{'), text.indexOf('{')].filter((n) => n >= 0);
  for (const start of starts) {
    try {
      return JSON.parse(text.slice(text[start] === '{' ? start : start + 1));
    } catch {
      /* try next */
    }
  }
  return JSON.parse(text);
}

function freePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close(() => resolve(port));
    });
  });
}

function xbloxArgs(src) {
  return ['xblox', '--log-level', 'off', 'run', '--json', '--no-wait', '--src', src];
}

const assert = createAssert();
const exe = cliExePath();
const workDir = mkdtempSync(join(tmpdir(), 'pm-modbus-'));

function ok(condition, label) {
  assert.assert(Boolean(condition), label);
}

function equal(actual, expected, label) {
  assert.assert(Object.is(actual, expected), `${label} (got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)})`);
}

function deepEqual(actual, expected, label) {
  assert.assert(JSON.stringify(actual) === JSON.stringify(expected), `${label} (got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)})`);
}

let server;
try {
  const port = await freePort();
  const url = `tcp:127.0.0.1:${port}`;
  const serverDoc = {
    version: 1,
    roots: [
      {
        kind: 'modbusServer',
        id: 'server',
        url,
        holdingRegisters: [11, 22, 33],
        registerCount: 16,
        maxRequests: 3,
        durationMs: 8000,
        storeAs: 'serverStats',
      },
    ],
  };
  const clientDoc = {
    version: 1,
    roots: [
      { kind: 'modbusReadHoldingRegisters', id: 'read-initial', url, address: 0, count: 3, storeAs: 'initial' },
      { kind: 'modbusWriteRegister', id: 'write-one', url, address: 1, value: 77, storeAs: 'writeOne' },
      { kind: 'modbusReadHoldingRegisters', id: 'read-final', url, address: 0, count: 3, storeAs: 'final' },
    ],
  };
  const serverPath = join(workDir, 'server.json');
  const clientPath = join(workDir, 'client.json');
  writeFileSync(serverPath, `${JSON.stringify(serverDoc, null, 2)}\n`, 'utf8');
  writeFileSync(clientPath, `${JSON.stringify(clientDoc, null, 2)}\n`, 'utf8');

  server = spawn(exe, xbloxArgs(serverPath), { cwd: PACKAGE_ROOT, encoding: 'utf8' });
  let serverStdout = '';
  let serverStderr = '';
  server.stdout.on('data', (chunk) => { serverStdout += chunk; });
  server.stderr.on('data', (chunk) => { serverStderr += chunk; });

  await new Promise((resolve) => setTimeout(resolve, 300));
  ok(server.exitCode === null, `modbus server process is running for ${url}`);

  const client = spawnSync(exe, xbloxArgs(clientPath), {
    cwd: PACKAGE_ROOT,
    encoding: 'utf8',
    timeout: 10_000,
  });
  if (client.status !== 0) {
    console.error('client stdout:\n', client.stdout);
    console.error('client stderr:\n', client.stderr);
  }
  equal(client.status, 0, 'modbus client xblox run exits 0');
  const clientReport = parseTrailingJson(client.stdout);
  ok(clientReport.ok, 'modbus client report ok=true');
  const events = (clientReport.events || []).filter((event) => event.status === 'ok');
  const byId = new Map(events.map((event) => [event.blockId, event]));
  deepEqual(byId.get('read-initial')?.data?.values, [11, 22, 33], 'initial holding registers match server fixture');
  deepEqual(byId.get('read-final')?.data?.values, [11, 77, 33], 'write register mutates server mapping');

  const serverExit = await new Promise((resolve) => {
    const timer = setTimeout(() => {
      try { server.kill(); } catch { /* ignore */ }
      resolve(-1);
    }, 10_000);
    server.once('exit', (code) => {
      clearTimeout(timer);
      resolve(code ?? 0);
    });
  });
  if (serverExit !== 0) {
    console.error('server stdout:\n', serverStdout);
    console.error('server stderr:\n', serverStderr);
  }
  equal(serverExit, 0, 'modbus server exits after maxRequests');
  const serverReport = parseTrailingJson(serverStdout);
  ok(serverReport.ok, 'modbus server report ok=true');
  const serverEvent = (serverReport.events || []).find((event) => event.blockId === 'server' && event.status === 'ok');
  equal(serverEvent?.data?.requests, 3, 'modbus server handled 3 requests');

  if (serverStderr.trim()) {
    console.warn(serverStderr.trim());
  }
} finally {
  if (server && server.exitCode === null) {
    try { server.kill(); } catch { /* ignore */ }
  }
  rmSync(workDir, { recursive: true, force: true });
}

console.log(`\nmodbus: ${assert.passed} passed, ${assert.failed} failed`);
process.exit(assert.failed ? 1 : 0);
