#!/usr/bin/env node
/**
 * test:pixlwiz — smoke-tests the PixlWiz provider model list against
 * https://llm.polymech.info/ (LiteLLM proxy).
 *
 * API key resolution order:
 *   1. PIXLWIZ_API_KEY env var
 *   2. IMAGE_TRANSFORM_PIXLWIZ_API_KEY env var
 *   3. Hardcoded dev key (temporary; removed once settings.json resolution is wired in CLI)
 *
 * The test skips gracefully when no key can be resolved.
 */
import { spawnSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mediaExePath } from './media-presets.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(__dirname, '../..');

function pixlwizCliPath() {
  const fromEnv = process.env.PM_IMAGE || process.env.MEDIA_IMG_EXE;
  if (fromEnv && fromEnv.trim()) return resolve(fromEnv.trim());
  const candidates = process.platform === 'win32'
    ? [
        resolve(REPO, 'dist', 'win-x64', 'pm-image-cli.exe'),
        resolve(REPO, 'dist', 'win-x64', 'pm-image.exe'),
        mediaExePath(resolve(REPO, 'orchestrator')),
      ]
    : [mediaExePath(resolve(REPO, 'orchestrator'))];
  return candidates.find((p) => existsSync(p)) || candidates[0];
}

const EXE = pixlwizCliPath();

function loadEnvFromPackage() {
  const envPath = join(REPO, '.env');
  if (!existsSync(envPath)) return;
  for (const raw of readFileSync(envPath, 'utf8').split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const k = line.slice(0, eq).trim();
    let v = line.slice(eq + 1).trim();
    if ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith("'") && v.endsWith("'"))) v = v.slice(1, -1);
    if (!process.env[k]) process.env[k] = v;
  }
}

function assert(ok, msg) {
  if (!ok) throw new Error(msg);
}

/** Resolve the API key: env vars first, then the temporary dev key. */
function resolveApiKey() {
  return (
    process.env.PIXLWIZ_API_KEY ||
    process.env.IMAGE_TRANSFORM_PIXLWIZ_API_KEY ||
    // Temporary dev key — will be removed once settings.json resolution is in place.
    'sk-ca02TZssN8d2mP871pwU2Q'
  );
}

const BASE_URL = 'https://llm.polymech.info/';

async function run() {
  loadEnvFromPackage();
  const key = resolveApiKey();

  if (!key) {
    console.log('[test:pixlwiz] skipped: no API key available');
    return;
  }

  console.log('[test:pixlwiz] testing PixlWiz provider model list...');
  console.log(`[test:pixlwiz] base_url: ${BASE_URL}`);

  // --- provider models list ---
  {
    console.log('[test:pixlwiz] listing models via CLI...');
    const t0 = Date.now();
    const r = spawnSync(
      EXE,
      [
        '--no-gui',
        '--no-mcp',
        'provider',
        'models',
        'list',
        '--provider', 'pixlwiz',
        '--api-key',  key,
        '--base-url', BASE_URL,
      ],
      { encoding: 'utf8', timeout: 60_000 },
    );

    const elapsed = Date.now() - t0;

    if (r.status !== 0) {
      const errTail = (r.error && String(r.error)) || '';
      console.error(
        `[test:pixlwiz] models list exit: ${r.status == null ? 'null' : r.status} ${errTail}\n` +
          `stdout:\n${(r.stdout || '').trim()}\n` +
          `stderr:\n${(r.stderr || '').trim()}`,
      );
      assert(r.status === 0, 'provider models list should exit 0');
    }

    const stdout = (r.stdout || '').trim();
    console.log(`[test:pixlwiz] models list ok (${elapsed} ms)`);
    console.log(`[test:pixlwiz] output (first 800 chars):\n${stdout.slice(0, 800)}`);

    // Minimal sanity: at least one non-empty line in stdout
    const lines = stdout.split(/\r?\n/).filter(l => l.trim());
    assert(lines.length > 0, 'models list should return at least one model line');
    console.log(`[test:pixlwiz] model count (lines): ${lines.length}`);
  }

  console.log('[test:pixlwiz] PASSED');
}

run().catch((e) => {
  console.error(`[test:pixlwiz] FAILED: ${e.message || e}`);
  process.exit(1);
});
