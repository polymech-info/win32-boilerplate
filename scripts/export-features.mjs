#!/usr/bin/env node
import { existsSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const args = process.argv.slice(2);
let preset = 'release';
const cmakeArgs = [];

for (let i = 0; i < args.length; ++i) {
  const arg = args[i];
  if (arg === '--preset') {
    preset = args[++i] ?? preset;
  } else if (arg.startsWith('--preset=')) {
    preset = arg.slice('--preset='.length) || preset;
  } else {
    cmakeArgs.push(arg);
  }
}

const sourceDir = process.cwd();
const scriptPath = fileURLToPath(import.meta.url);
const out = resolve('dist', 'shared', 'features.json');

function sourceInputPaths() {
  return [
    resolve('CMakeLists.txt'),
    resolve('CMakePresets.json'),
    scriptPath,
  ];
}

function fileMtimeMs(path) {
  return statSync(path).mtimeMs;
}

function expandPresetPath(path) {
  const sourceParentDir = dirname(sourceDir);
  const sourceDirName = sourceDir.split(/[\\/]/).pop() ?? '';
  return path
    .replaceAll('${sourceDir}', sourceDir)
    .replaceAll('${sourceParentDir}', sourceParentDir)
    .replaceAll('${sourceDirName}', sourceDirName)
    .replace(/\$penv\{([^}]+)\}/g, (_, name) => process.env[name] ?? '');
}

function loadConfigurePreset(name) {
  const presetsPath = resolve('CMakePresets.json');
  const presets = JSON.parse(readFileSync(presetsPath, 'utf8'));
  const byName = new Map((presets.configurePresets ?? []).map((p) => [p.name, p]));
  const seen = new Set();

  function mergePreset(presetName) {
    if (seen.has(presetName)) {
      throw new Error(`Cyclic CMake preset inheritance involving ${presetName}`);
    }
    const current = byName.get(presetName);
    if (!current) {
      throw new Error(`Unknown CMake configure preset: ${presetName}`);
    }

    seen.add(presetName);
    const inherited = Array.isArray(current.inherits)
      ? current.inherits
      : current.inherits
        ? [current.inherits]
        : [];
    const merged = inherited.reduce(
      (acc, parentName) => ({ ...acc, ...mergePreset(parentName) }),
      {},
    );
    seen.delete(presetName);
    return { ...merged, ...current };
  }

  return mergePreset(name);
}

function presetCachePath() {
  const configurePreset = loadConfigurePreset(preset);
  const binaryDir = configurePreset.binaryDir
    ? expandPresetPath(configurePreset.binaryDir)
    : resolve('build', preset);
  return resolve(binaryDir, 'CMakeCache.txt');
}

function featureStampPath() {
  return resolve(dirname(presetCachePath()), '.features.stamp');
}

function featuresAreCurrent() {
  const stamp = featureStampPath();
  if (cmakeArgs.length > 0 || !existsSync(out) || !existsSync(stamp)) {
    return false;
  }

  const cachePath = presetCachePath();
  if (!existsSync(cachePath)) {
    return false;
  }

  const stampMtime = fileMtimeMs(stamp);
  return sourceInputPaths().every((input) => !existsSync(input) || fileMtimeMs(input) <= stampMtime);
}

function run(prog, argv) {
  const r = spawnSync(prog, argv, { stdio: 'inherit' });
  if (r.error) throw r.error;
  if (r.status !== 0) process.exit(r.status ?? 1);
}

if (featuresAreCurrent()) {
  console.log(`features: up to date ${out}`);
  process.exit(0);
}

run('node', ['scripts/cmake.mjs', '--preset', preset, ...cmakeArgs]);

if (!existsSync(out)) {
  console.error(`features: expected ${out} to be written by CMake configure`);
  process.exit(1);
}

writeFileSync(featureStampPath(), `${new Date().toISOString()}\n`);
console.log(`features: verified ${out}`);
