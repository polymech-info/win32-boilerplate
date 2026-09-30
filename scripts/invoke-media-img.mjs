#!/usr/bin/env node
/**
 * Cross-platform launcher: runs dist/pm-image (or dist-osx/pm-image on macOS) or dist/pm-image.exe.
 * Same path rules as orchestrator/media-presets.js `mediaExePath`.
 */
import { spawnSync } from 'node:child_process';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

import { mediaExePath } from '../orchestrator/media-presets.js';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const bin = mediaExePath(resolve(root, 'orchestrator'));
const args = process.argv.slice(2);
const r = spawnSync(bin, args, { stdio: 'inherit' });
process.exit(r.status ?? 1);
