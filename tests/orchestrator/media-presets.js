/**
 * Defaults for media-img orchestrator tests (REST + line IPC).
 */
import { existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));

export const platform = {
  isWin: process.platform === 'win32',
};

/**
 * Path to pm-image next to packages/media/cpp (orchestratorDir is usually …/orchestrator).
 *
 * Resolution order:
 * 1. `PM_IMAGE` or `MEDIA_IMG_EXE` (absolute or relative path) if set
 * 2. On **darwin**: `../dist-osx/pm-image` if that file exists (release-macos / Ninja build)
 * 3. `../dist/pm-image` (or .exe on Windows)
 */
export function mediaExePath(orchestratorDir = __dirname) {
  const name = platform.isWin ? 'pm-image.exe' : 'pm-image';
  const fromEnv = process.env.PM_IMAGE || process.env.MEDIA_IMG_EXE;
  if (fromEnv && fromEnv.trim()) {
    return resolve(fromEnv.trim());
  }
  const root = resolve(orchestratorDir, '..');
  if (process.platform === 'darwin') {
    const osx = resolve(root, 'dist-osx', name);
    if (existsSync(osx)) return osx;
  }
  return resolve(root, 'dist', name);
}

/** Default fixtures directory: packages/media/cpp/tests/assets */
export function defaultAssetsDir(orchestratorDir = __dirname) {
  return resolve(orchestratorDir, '..', 'tests', 'assets');
}

export const timeouts = {
  connectAttempts: 20,
  connectRetryMs: 100,
  httpMs: 15_000,
  ipcReadMs: 10_000,
};

/** Unix socket path for IPC tests (non-Windows). */
export function ipcUnixPath() {
  return process.env.MEDIA_IMG_TEST_UNIX || '/tmp/media-img-test.sock';
}
