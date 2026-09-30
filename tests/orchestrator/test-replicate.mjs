#!/usr/bin/env node
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync } from 'node:fs';
import { dirname, join, resolve, parse } from 'node:path';
import { fileURLToPath } from 'node:url';
import { mediaExePath } from './media-presets.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const EXE = mediaExePath(__dirname);
const REPO = resolve(__dirname, '..');
const ASSETS = join(REPO, 'tests', 'assets');
const VISION = join(ASSETS, 'vision');
const KATCLAUS_PREFERRED = join(VISION, 'katclaus.jpg');
const KATCLAUS_FALLBACK = join(ASSETS, 'agent', 'katclaus.png');
const FLUX6 = join(VISION, 'flux-6.png');

function loadEnvFromPackage() {
  const envPath = join(__dirname, '..', '.env');
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

/**
 * Style reference: prefer `vision/katclaus.jpg`, else `tests/assets/agent/katclaus.png`.
 * Edit input must be `tests/assets/vision/flux-6.png`.
 */
function resolveKatclausPath() {
  if (existsSync(KATCLAUS_PREFERRED)) return KATCLAUS_PREFERRED;
  if (existsSync(KATCLAUS_FALLBACK)) {
    console.log(
      `[test:replicate] ${KATCLAUS_PREFERRED} not found; using ${KATCLAUS_FALLBACK} (add vision/katclaus.jpg to prefer).`,
    );
    return KATCLAUS_FALLBACK;
  }
  throw new Error(
    `[test:replicate] missing katclaus: add ${KATCLAUS_PREFERRED} or commit ${KATCLAUS_FALLBACK}.`,
  );
}

function ensureFlux6Fixture() {
  if (existsSync(FLUX6)) return;
  throw new Error(
    [
      '[test:replicate] missing edit input image:',
      `  - ${FLUX6}`,
      'Add `tests/assets/vision/flux-6.png` (curated; used for transform + meta).',
    ].join('\n'),
  );
}

function stemOf(p) {
  return parse(p).name;
}

async function run() {
  loadEnvFromPackage();
  const key = process.env.IMAGE_TRANSFORM_REPLICATE_API_KEY || '';
  if (!key) {
    console.log('[test:replicate] skipped: IMAGE_TRANSFORM_REPLICATE_API_KEY missing');
    return;
  }
  const kat = resolveKatclausPath();
  ensureFlux6Fixture();

  const metaStemA = stemOf(kat);
  const metaStemB = stemOf(FLUX6);

  // CLI transform (edit): edit flux-6.png; katclaus.jpg as style reference; chair prompt
  {
    const out = join(VISION, `replicate-transform-${Date.now()}.png`);
    console.log('[test:replicate] starting CLI transform / edit (can take ~30-90s)...');
    console.log(`[test:replicate] transform output (on success): ${out}`);
    const t0 = Date.now();
    const r = spawnSync(
      EXE,
      [
        'transform',
        FLUX6,
        out,
        '--provider',
        'replicate',
        '--model',
        'google/nano-banana-pro',
        '--prompt',
        'add chair - same style',
        '--reference',
        kat,
      ],
      { encoding: 'utf8', timeout: 300_000 },
    );
    if (r.status !== 0) {
      const errTail = (r.error && String(r.error)) || '';
      console.error(
        `[test:replicate] transform exit: ${r.status == null ? 'null' : r.status} ${errTail}\n` +
          `stdout:\n${(r.stdout || '').trim()}\n` +
          `stderr:\n${(r.stderr || '').trim()}`,
      );
    }
    assert(r.status === 0, `CLI transform should exit 0 (see full stderr above)`);
    assert(existsSync(out), `CLI transform output should exist: ${out}`);
    console.log(`[test:replicate] CLI transform ok (${Date.now() - t0} ms)`);
  }

  // Meta (Replicate): katclaus + flux-6 → <stem>.json / <stem>.md in outDir
  {
    const outDir = join(VISION, `replicate-meta-${Date.now()}`);
    mkdirSync(outDir, { recursive: true });
    console.log(`[test:replicate] meta --out-dir (on success): ${outDir}`);

    console.log('[test:replicate] starting CLI meta (2 images, can take ~30-180s)...');
    const t0 = Date.now();
    const r = spawnSync(
      EXE,
      [
        '--no-gui',
        '--no-mcp',
        'meta',
        kat,
        FLUX6,
        '--provider',
        'replicate',
        '--model',
        'google/gemini-2.5-flash',
        '--api-key',
        key,
        '--out-dir',
        outDir,
        // Omit --prompt: use built-in cataloguer (image-faithful JSON, no markdown).
      ],
      { encoding: 'utf8', timeout: 360_000 },
    );
    if (r.status !== 0) {
      const errTail = (r.error && String(r.error)) || '';
      console.error(
        `[test:replicate] meta exit: ${r.status == null ? 'null' : r.status} ${errTail}\n` +
          `stdout:\n${(r.stdout || '').trim()}\n` +
          `stderr:\n${(r.stderr || '').trim()}`,
      );
    }
    assert(r.status === 0, `meta replicate should exit 0 (see full stderr above)`);
    for (const stem of [metaStemA, metaStemB]) {
      assert(
        existsSync(join(outDir, `${stem}.json`)),
        `meta output ${stem}.json should exist in ${outDir}`,
      );
      assert(
        existsSync(join(outDir, `${stem}.md`)),
        `meta output ${stem}.md should exist in ${outDir}`,
      );
    }
    console.log(`[test:replicate] CLI meta ok (${Date.now() - t0} ms)`);
  }
}

run().catch((e) => {
  console.error(`[test:replicate] FAILED: ${e.message || e}`);
  process.exit(1);
});
