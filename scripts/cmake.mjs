#!/usr/bin/env node
import { existsSync } from 'node:fs';
import { spawnSync } from 'node:child_process';

const exe =
  process.env.POLYMECH_CMAKE ||
  (process.platform === 'linux' && existsSync('/snap/bin/cmake')
    ? '/snap/bin/cmake'
    : 'cmake');

const r = spawnSync(exe, process.argv.slice(2), { stdio: 'inherit' });
if (r.error) throw r.error;
process.exit(r.status ?? 1);
