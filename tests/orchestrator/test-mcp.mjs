/**
 * Embedded MCP HTTP (loopback) + LLM buffer/path tools (see src/cli/pm_image_mcp_embed.cpp).
 *
 * Run from packages/media/cpp after building pm-image:
 *   npm run test:mcp
 *
 * Uses tests/assets/square-64.png (same as test-media LLM suite).
 * Optional: IMAGE_TRANSFORM_GOOGLE_API_KEY — runs buffer `image_create` smoke.
 */
import { spawn } from 'node:child_process';
import { existsSync, readFileSync, copyFileSync, mkdtempSync, rmSync } from 'node:fs';
import net from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { mediaExePath, defaultAssetsDir, timeouts } from './media-presets.js';
import { probeTcpPort, pipeWorkerStderr } from './test-commons.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const exe = mediaExePath(__dirname);

function fail(msg) {
  console.error(msg);
  process.exit(1);
}

function pmImgArgs(...argv) {
  return ['--no-gui', ...argv];
}

function getFreePort() {
  return new Promise((resolvePort, reject) => {
    const s = net.createServer();
    s.listen(0, '127.0.0.1', () => {
      const p = s.address().port;
      s.close(() => resolvePort(p));
    });
    s.on('error', reject);
  });
}

async function waitListen(host, port, label) {
  for (let i = 0; i < timeouts.connectAttempts; i++) {
    if (await probeTcpPort(host, port, 300)) return;
    await new Promise((r) => setTimeout(r, timeouts.connectRetryMs));
  }
  throw new Error(`${label}: nothing listening on ${host}:${port}`);
}

async function fetchJson(url, init) {
  const r = await fetch(url, init);
  const text = await r.text();
  let j;
  try {
    j = JSON.parse(text);
  } catch {
    throw new Error(`not JSON (${r.status}): ${text.slice(0, 200)}`);
  }
  return { ok: r.ok, status: r.status, json: j };
}

// ── 1. Help strings ───────────────────────────────────────────────────────
if (!existsSync(exe)) {
  console.log('test:mcp: SKIP — binary not found:', exe);
  process.exit(0);
}

{
  const { spawnSync } = await import('node:child_process');
  const main = spawnSync(exe, ['--help'], { encoding: 'utf8', maxBuffer: 4 << 20 });
  const out = `${main.stdout || ''}${main.stderr || ''}`;
  if (main.status !== 0) fail(`test:mcp: --help failed (${main.status})`);
  if (!out.includes('--mcp')) fail('test:mcp: --help missing --mcp');
  if (!out.includes('--no-mcp')) fail('test:mcp: --help missing --no-mcp');
  if (!out.includes('--mcp-port')) fail('test:mcp: --help missing --mcp-port');
  if (!out.includes('--mcp-bind')) fail('test:mcp: --help missing --mcp-bind');
  if (!out.includes('4444')) fail('test:mcp: --help missing default port 4444');
}

const assetsDir = defaultAssetsDir(__dirname);
const inPng = resolve(assetsDir, 'square-64.png');
if (!existsSync(inPng)) fail(`test:mcp: missing fixture ${inPng}`);

const imgB64 = readFileSync(inPng).toString('base64');
const httpPort = await getFreePort();
const mcpPort = await getFreePort();

const proc = spawn(
  exe,
  pmImgArgs('--mcp', '--mcp-bind=127.0.0.1', `--mcp-port=${mcpPort}`, 'serve', '--host', '127.0.0.1', '--port', String(httpPort)),
  { stdio: ['ignore', 'pipe', 'pipe'] },
);
pipeWorkerStderr(proc, '[test:mcp:serve]');

try {
  await waitListen('127.0.0.1', httpPort, 'serve');
  await waitListen('127.0.0.1', mcpPort, 'embedded MCP');

  const mcpBase = `http://127.0.0.1:${mcpPort}`;
  const mcpRpc = `${mcpBase}/mcp`;
  const mcpRpcHeaders = {
    'Content-Type': 'application/json',
    Accept: 'application/json, text/event-stream',
    'MCP-Protocol-Version': '2025-11-25',
  };

  // ── MCP Streamable HTTP (Cursor "url") — POST+GET /mcp ───────────────────
  {
    const r = await fetch(mcpRpc, {
      method: 'POST',
      headers: mcpRpcHeaders,
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: 1,
        method: 'initialize',
        params: {
          protocolVersion: '2025-11-25',
          capabilities: {},
          clientInfo: { name: 'test-mcp', version: '0' },
        },
      }),
    });
    if (!r.ok) fail(`test:mcp: MCP initialize HTTP ${r.status}`);
    const sid = r.headers.get('mcp-session-id');
    if (!sid || !String(sid).trim()) fail('test:mcp: MCP initialize missing MCP-Session-Id');
    const initBody = await r.json();
    if (initBody.jsonrpc !== '2.0' || initBody.result?.serverInfo?.name !== 'pm-image') {
      fail(`test:mcp: MCP initialize body unexpected: ${JSON.stringify(initBody).slice(0, 400)}`);
    }

    const rN = await fetch(mcpRpc, {
      method: 'POST',
      headers: { ...mcpRpcHeaders, 'MCP-Session-Id': sid },
      body: JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }),
    });
    if (rN.status !== 202) fail(`test:mcp: MCP notifications/initialized expect 202 got ${rN.status}`);

    const rList = await fetch(mcpRpc, {
      method: 'POST',
      headers: { ...mcpRpcHeaders, 'MCP-Session-Id': sid },
      body: JSON.stringify({ jsonrpc: '2.0', id: 3, method: 'tools/list', params: {} }),
    });
    const listBody = await rList.json();
    if (!rList.ok) fail(`test:mcp: MCP tools/list HTTP ${rList.status}`);
    if (!Array.isArray(listBody.result?.tools)) fail('test:mcp: MCP tools/list missing result.tools');
    if (!listBody.result.tools.some((t) => t.name === 'image_resize')) fail('test:mcp: MCP tools/list image_resize');

    const rCall = await fetch(mcpRpc, {
      method: 'POST',
      headers: { ...mcpRpcHeaders, 'MCP-Session-Id': sid },
      body: JSON.stringify({
        jsonrpc: '2.0',
        id: 4,
        method: 'tools/call',
        params: {
          name: 'image_resize',
          arguments: {
            image: { mime: 'image/png', b64: imgB64 },
            options: { max_width: 16, max_height: 16, format: 'jpeg', quality: 75 },
          },
        },
      }),
    });
    const callBody = await rCall.json();
    if (!rCall.ok) fail(`test:mcp: MCP tools/call HTTP ${rCall.status}`);
    const c0 = callBody.result?.content?.[0];
    if (!c0 || c0.type !== 'image' || c0.mimeType !== 'image/jpeg') {
      fail(`test:mcp: MCP tools/call image content (got ${JSON.stringify(callBody).slice(0, 400)})`);
    }
    if (typeof c0.data !== 'string' || c0.data.length < 20) fail('test:mcp: MCP tools/call image data');

    const rSse = await fetch(mcpRpc, {
      method: 'GET',
      headers: {
        Accept: 'text/event-stream',
        'MCP-Protocol-Version': '2025-11-25',
        'MCP-Session-Id': sid,
      },
    });
    if (!rSse.ok) fail(`test:mcp: MCP GET /mcp (SSE) HTTP ${rSse.status}`);
    if (!rSse.body) fail('test:mcp: MCP GET /mcp missing body');
    const reader = rSse.body.getReader();
    const dec = new TextDecoder();
    const { value, done } = await reader.read();
    await reader.cancel().catch(() => {});
    if (done && (!value || value.length === 0)) fail('test:mcp: MCP GET /mcp SSE first read empty');
    const sseText = dec.decode(value || new Uint8Array(), { stream: false });
    if (!sseText.includes('stream open')) fail('test:mcp: MCP GET /mcp SSE missing stream open');
  }

  // ── Buffer catalog (media::llm::tool_catalog + execute) ───────────────
  {
    const { json } = await fetchJson(`${mcpBase}/v1/llm/tools/list`);
    if (!Array.isArray(json.tools)) fail('test:mcp: tools/list: missing tools[]');
    const names = new Set(json.tools.map((t) => t.name));
    for (const n of ['image_resize', 'image_compress', 'image_create']) {
      if (!names.has(n)) fail(`test:mcp: tools/list missing ${n}`);
    }
  }

  {
    const { json } = await fetchJson(`${mcpBase}/v1/llm/tools/call`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name: 'image_resize',
        arguments: {
          image: { mime: 'image/png', b64: imgB64 },
          options: { max_width: 32, max_height: 32, format: 'jpeg', quality: 80 },
        },
      }),
    });
    if (json.ok !== true) fail(`test:mcp: buffer image_resize ok=true (got ${JSON.stringify(json).slice(0, 300)})`);
    if (json.mime !== 'image/jpeg') fail(`test:mcp: buffer resize mime jpeg (got ${json.mime})`);
    if (typeof json.b64 !== 'string' || json.b64.length < 20) fail('test:mcp: buffer resize b64');
  }

  // ── Path catalog + path::execute (path_tool_executor) ───────────────────
  {
    const { json } = await fetchJson(`${mcpBase}/v1/llm/path-tools/list`);
    if (!Array.isArray(json.tools)) fail('test:mcp: path-tools/list: missing tools[]');
    const names = new Set(json.tools.map((t) => t.name));
    if (!names.has('image_resize')) fail('test:mcp: path-tools/list missing image_resize');
  }

  {
    const tmp = mkdtempSync(join(tmpdir(), 'mcp-path-resize-'));
    const workPng = join(tmp, 'square-64.png');
    copyFileSync(inPng, workPng);
    const { json } = await fetchJson(`${mcpBase}/v1/llm/path-tools/call`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name: 'image_resize',
        arguments: {
          paths: [workPng],
          options: { max_width: 24, max_height: 24, format: 'png' },
        },
      }),
    });
    try {
      rmSync(tmp, { recursive: true, force: true });
    } catch {
      /* ignore */
    }
    if (json.ok !== true) fail(`test:mcp: path image_resize envelope ok (got ${JSON.stringify(json).slice(0, 400)})`);
    const r0 = json.results?.[0];
    if (!r0?.ok) fail(`test:mcp: path image_resize first file ok (got ${JSON.stringify(r0)})`);
    if (typeof r0.output_path !== 'string' || !r0.output_path.length) fail('test:mcp: path image_resize output_path');
  }

  // ── Optional: buffer image_create (Google) ─────────────────────────────
  const googleKey = process.env.IMAGE_TRANSFORM_GOOGLE_API_KEY || '';
  if (googleKey.trim()) {
    const { json } = await fetchJson(`${mcpBase}/v1/llm/tools/call`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name: 'image_create',
        arguments: {
          options: {
            prompt: 'Tiny flat solid blue square icon, 64 pixels, no text, simple shapes only',
            provider: 'google',
            aspect_ratio: '1:1',
            image_size: '512',
            api_key: googleKey,
          },
        },
      }),
    });
    if (json.ok !== true) {
      console.log('test:mcp: image_create skipped or failed:', JSON.stringify(json).slice(0, 400));
      fail('test:mcp: image_create expected ok=true when IMAGE_TRANSFORM_GOOGLE_API_KEY is set');
    }
    if (typeof json.mime !== 'string' || !json.mime.startsWith('image/')) fail(`test:mcp: image_create mime (got ${json.mime})`);
    if (typeof json.b64 !== 'string' || json.b64.length < 50) fail('test:mcp: image_create b64');
    console.log('test:mcp: image_create ok (API key present)');
  } else {
    console.log('test:mcp: image_create skipped (no IMAGE_TRANSFORM_GOOGLE_API_KEY)');
  }
} finally {
  proc.kill();
  await new Promise((r) => setTimeout(r, 200));
}

console.log('test:mcp: ok');
process.exit(0);
