#!/usr/bin/env node
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { spawnSync } from 'node:child_process';

const preset = process.env.PM_STORE_PRESET || 'store-msi-release';
const overrides = process.argv.slice(2);
const pkg = JSON.parse(readFileSync(resolve('package.json'), 'utf8'));
const version = String(pkg.version || '0.0.0');
const outDir = resolve('dist', 'v1');
const nsisOut = resolve('dist', 'pm-images-setup.exe');
const nsisWrapper = resolve('installer-store-root.nsi');
const finalOut = resolve(outDir, `Pixlwiz-v-${version}-msi-x64.exe`);

function run(cmd, args) {
  const r = spawnSync(cmd, args, { stdio: 'inherit', shell: process.platform === 'win32' });
  if (r.error) throw r.error;
  if (r.status !== 0) process.exit(r.status ?? 1);
}

run('npm', ['run', 'buildf:web']);
run('node', ['scripts/export-features.mjs', '--preset', preset, ...overrides]);
run('node', ['scripts/cmake.mjs', '--build', '--preset', preset, '--config', 'Release']);
run('npm', ['run', 'docs:installer']);
run('node', ['scripts/refresh-installer-branding.mjs']);
run('node', ['scripts/make-installer-bitmaps.mjs']);
run('powershell', ['-ExecutionPolicy', 'Bypass', '-File', 'scripts/fetch-vc-redist.ps1']);
writeFileSync(
  nsisWrapper,
  '!addincludedir "dist\\installer"\r\n!include "dist\\installer\\installer.nsi"\r\n',
  'utf8',
);
run('makensis', ['-V2', 'installer-store-root.nsi']);

if (!existsSync(nsisOut)) {
  console.error(`Expected NSIS output not found: ${nsisOut}`);
  process.exit(1);
}

mkdirSync(outDir, { recursive: true });
copyFileSync(nsisOut, finalOut);
console.log(`Wrote ${finalOut}`);
