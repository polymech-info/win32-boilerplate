#!/usr/bin/env node
/**
 * platform-build.mjs — cross-platform configure / build shortcuts.
 *
 *   node scripts/platform-build.mjs <cmd> [--remote [osx|linux|all]]
 *
 * Commands:
 *   configure   cmake configure for this OS (or the remote)
 *   build:cli   incremental build of the CLI (pm-image-cli on Windows, pm-image elsewhere)
 *   build:ui    incremental build of the GUI (pm-image on Windows, pm-codeedit on macOS, pm-image-gui on Linux)
 *   build:all   incremental build of web embeds, libs, CLI, and UI
 *
 * --remote flag:
 *   --remote osx     SSH → mc007@mac,         cd + git pull + npm run <cmd>
 *   --remote linux   SSH → polymech@linux-vm, cd + git pull + npm run <cmd>
 *   --remote [all]   both remotes sequentially
 *
 * Presets (local):
 *   win32  → release          (MSVC, multi-config → needs --config Release)
 *   darwin → release-macos    (Ninja, single-config, build tree on guest disk)
 *   linux  → release-linux    (Ninja, single-config, build tree on guest disk)
 */

import { spawnSync } from 'node:child_process';

// ── remote host config (from docs/dist/ci.md) ───────────────────────────────

const REMOTES = {
  osx:   { host: 'mc007@mac',         dir: '/Users/mc007/Desktop/mono-next/packages/media/cpp' },
  linux: { host: 'polymech@linux-vm', dir: '~/Desktop/mono-next/packages/media/cpp' },
};

// ── arg parsing ──────────────────────────────────────────────────────────────

const rawArgs   = process.argv.slice(2);
const remoteIdx = rawArgs.indexOf('--remote');
const isRemote  = remoteIdx !== -1;

const afterRemote  = isRemote ? rawArgs[remoteIdx + 1] : null;
const remoteTarget = (afterRemote && !afterRemote.startsWith('-')) ? afterRemote : 'all';
const cmd          = rawArgs.find(a => !a.startsWith('-'));

// ── helpers ──────────────────────────────────────────────────────────────────

function run(prog, args, opts = {}) {
  const r = spawnSync(prog, args, { stdio: 'inherit', ...opts });
  if (r.error) throw r.error;
  if (r.status !== 0) process.exit(r.status ?? 1);
}

function cmake(...args) {
  run('node', ['scripts/cmake.mjs', ...args]);
}

function exportFeatures(...extraArgs) {
  run('node', ['scripts/export-features.mjs', '--preset', preset, ...cfgFlags, ...extraArgs]);
}

function npmScript(name) {
  if (process.platform === 'win32') {
    run('npm.cmd', ['run', name], { shell: true });
  } else {
    run('npm', ['run', name]);
  }
}

function cmakeBuildTarget(target) {
  cmake('--build', '--preset', preset, ...bldCfg, '--target', target);
}

function ssh(name) {
  const { host, dir } = REMOTES[name];
  const remote = `bash -l -c 'cd ${dir} && git pull && npm run ${cmd}'`;
  console.log(`\n── remote: ${name} (${host}) ──`);
  run('ssh', [host, remote]);
}

// ── remote dispatch ──────────────────────────────────────────────────────────

if (isRemote) {
  if (!cmd) {
    console.error('platform-build: no command specified');
    process.exit(1);
  }
  const targets = remoteTarget === 'all' ? ['osx', 'linux'] : [remoteTarget];
  for (const t of targets) {
    if (!REMOTES[t]) { console.error(`Unknown remote "${t}" — valid: osx, linux`); process.exit(1); }
    ssh(t);
  }
  process.exit(0);
}

// ── local dispatch ───────────────────────────────────────────────────────────

const p = process.platform; // 'win32' | 'darwin' | 'linux'

const preset = p === 'darwin' ? 'release-macos'
             : p === 'linux'  ? 'release-linux'
             :                  'release';

const cfgFlags = p === 'darwin' ? ['-DPM_BUILD_CODEEDIT=ON', '-DCMAKE_EXE_LINKER_FLAGS=-L/opt/homebrew/lib']
               :                  [];

const bldCfg = p === 'win32' ? ['--config', 'Release'] : [];
const cliTarget = p === 'win32' ? 'pm-image-cli' : 'pm-image';
const uiTarget = p === 'win32' ? 'pm-image'
               : p === 'darwin' ? 'pm-codeedit'
               :                  'pm-image-gui';

switch (cmd) {
  case 'configure':
    cmake('--preset', preset, ...cfgFlags);
    break;

  case 'build:cli':
    exportFeatures();
    cmakeBuildTarget(cliTarget);
    break;

  case 'build:ui':
    npmScript('build:shell:embed');
    if (p === 'linux') exportFeatures('-DPM_BUILD_LINUX_GUI=ON');
    else exportFeatures();
    cmakeBuildTarget(uiTarget);
    break;

  case 'build:all':
    npmScript('buildf:web');
    exportFeatures();
    cmakeBuildTarget('pm-media');
    cmakeBuildTarget(cliTarget);
    if (p === 'linux') exportFeatures('-DPM_BUILD_LINUX_GUI=ON');
    cmakeBuildTarget(uiTarget);
    break;

  default:
    console.error(`platform-build: unknown command "${cmd ?? '(none)'}"`);
    console.error('Usage: node scripts/platform-build.mjs configure|build:cli|build:ui|build:all [--remote [osx|linux|all]]');
    process.exit(1);
}
