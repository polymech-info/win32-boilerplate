#!/usr/bin/env node
/**
 * make-deb.mjs — build a .deb package for pm-image (Linux / amd64 or arm64).
 *
 * Run on Linux after the cmake build has produced dist/pm-image:
 *   node scripts/make-deb.mjs
 *
 * Output: dist/pm-image_<version>_<arch>.deb
 *
 * Package layout:
 *   /usr/bin/pm-image                      — main binary
 *   /usr/bin/pmi                           — symlink → pm-image
 *   /usr/share/pm-image/system-prompt.md  — default agent system prompt
 *   /usr/share/pm-image/assets.pfs        — packed UI / icon assets (if present)
 */

import { execFileSync }                                  from 'child_process';
import { existsSync, mkdirSync, symlinkSync,
         copyFileSync, chmodSync, writeFileSync,
         readFileSync }                                  from 'fs';
import { join, resolve }                                 from 'path';
import { fileURLToPath }                                 from 'url';
import os                                               from 'os';

const __dir   = fileURLToPath(new URL('.', import.meta.url));
const root    = resolve(__dir, '..');
const distDir = join(root, 'dist');
const pkg     = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
const version = pkg.version ?? '0.1.0';

// ── Architecture ──────────────────────────────────────────────────────────────
const machine = (typeof os.machine === 'function')
    ? os.machine()
    : execOut('uname', ['-m']).trim();
const arch = machine === 'aarch64' ? 'arm64'
           : machine === 'x86_64'  ? 'amd64'
           :                         machine;

// ── Staging dir ───────────────────────────────────────────────────────────────
const staging = join(root, 'build', 'deb-staging');
const debOut  = join(distDir, `pm-image_${version}_${arch}.deb`);

console.log(`make-deb: version=${version}  arch=${arch}`);
console.log(`make-deb: staging=${staging}`);
console.log(`make-deb: output=${debOut}`);

// Clean + create staging tree
rmrf(staging);
for (const d of [
    join(staging, 'DEBIAN'),
    join(staging, 'usr', 'bin'),
    join(staging, 'usr', 'share', 'pm-image'),
]) mkdirSync(d, { recursive: true });

// ── Binary ────────────────────────────────────────────────────────────────────
const binarySrc = join(distDir, 'pm-image');
if (!existsSync(binarySrc))
    die('dist/pm-image not found — run build:linux:cpp first');

copyFileSync(binarySrc, join(staging, 'usr', 'bin', 'pm-image'));
chmodSync(  join(staging, 'usr', 'bin', 'pm-image'), 0o755);

// /usr/bin/pmi → pm-image  (relative symlink inside staging tree)
symlinkSync('pm-image', join(staging, 'usr', 'bin', 'pmi'));

// ── Data files ────────────────────────────────────────────────────────────────
for (const f of ['system-prompt.md', 'assets.pfs']) {
    const src = join(distDir, f);
    if (existsSync(src))
        copyFileSync(src, join(staging, 'usr', 'share', 'pm-image', f));
}

// ── DEBIAN/control ────────────────────────────────────────────────────────────
// libvips42 pulls in most image-processing transitive deps on Ubuntu 20.04+.
// libssl1.1 covers the curl / TLS stack.
writeFileSync(join(staging, 'DEBIAN', 'control'), [
    `Package: pm-image`,
    `Version: ${version}`,
    `Architecture: ${arch}`,
    `Maintainer: PolyMech <support@polymech.io>`,
    `Homepage: https://polymech.io`,
    `Section: utils`,
    `Priority: optional`,
    `Depends: libvips42, libssl1.1, libstdc++6, libgomp1`,
    `Description: pm-image — AI-powered image & media CLI`,
    ` Resize, compress, transform, classify and generate images via local`,
    ` or remote AI providers (OpenAI, Replicate, ElevenLabs, PixlWiz).`,
    ` Includes LLM agent mode with tool use, voice input and TTS output.`,
    ``,
].join('\n'), 'utf8');

// ── Build .deb ────────────────────────────────────────────────────────────────
console.log('make-deb: running dpkg-deb…');
execFileSync('dpkg-deb', ['--build', '--root-owner-group', staging, debOut], {
    stdio: 'inherit',
});

console.log(`\nmake-deb: done → ${debOut}`);
console.log(`Install : sudo dpkg -i ${debOut}`);
console.log(`Fix deps: sudo apt-get install -f`);

// ── Helpers ───────────────────────────────────────────────────────────────────
function execOut(cmd, args = []) {
    return execFileSync(cmd, args, { encoding: 'utf8' });
}
function rmrf(dir) {
    if (existsSync(dir)) execFileSync('rm', ['-rf', dir]);
}
function die(msg) { console.error('make-deb: error:', msg); process.exit(1); }
