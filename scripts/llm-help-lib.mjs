import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { platform } from 'node:os';

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export const HELP_EN = join(ROOT, 'dist', 'shared', 'help', 'en');

export function cliExePath() {
  const env = process.env.PM_IMAGE_CLI || process.env.MEDIA_IMG_CLI_EXE || process.env.PM_IMAGE || process.env.MEDIA_IMG_EXE;
  if (env?.trim()) return resolve(env.trim());
  const name = platform() === 'win32' ? 'pm-image-cli.exe' : 'pm-image-cli';
  for (const candidate of [
    join(ROOT, 'dist', 'win-x64', name),
    join(ROOT, 'dist', name),
    join(ROOT, 'dist-osx', name),
  ]) {
    if (existsSync(candidate)) return candidate;
  }
  return join(ROOT, 'dist', 'win-x64', name);
}

export function runCli(exe, args, timeoutMs = 120_000) {
  const result = spawnSync(exe, args, {
    cwd: ROOT,
    encoding: 'utf8',
    timeout: timeoutMs,
    maxBuffer: 32 * 1024 * 1024,
  });
  if (result.error) throw result.error;
  return {
    status: result.status ?? -1,
    stdout: result.stdout || '',
    stderr: result.stderr || '',
  };
}

export function parseJson(stdout, label) {
  const start = stdout.indexOf('{');
  const startArr = stdout.indexOf('[');
  let trimmed = stdout.trim();
  if (start >= 0 && (startArr < 0 || start < startArr)) {
    trimmed = stdout.slice(start);
  } else if (startArr >= 0) {
    trimmed = stdout.slice(startArr);
  }
  if (!trimmed) throw new Error(`${label}: empty stdout`);
  try {
    return JSON.parse(trimmed);
  } catch (err) {
    throw new Error(`${label}: invalid JSON (${err.message})`);
  }
}

export function requireCli() {
  const exe = cliExePath();
  if (!existsSync(exe)) {
    throw new Error(`pm-image CLI not found at ${exe}. Run npm run build:cpp first.`);
  }
  return exe;
}
