#!/usr/bin/env node
/**
 * Refresh generated PM-Image agent skill references from the freshly built CLI.
 *
 * Usage:
 *   node scripts/info-skills.mjs
 *   node scripts/info-skills.mjs commands
 *   node scripts/info-skills.mjs xblox
 */
import { spawnSync } from 'node:child_process';

import { ROOT, requireCli } from './llm-help-lib.mjs';

const targets = process.argv.slice(2);
const commands = targets.length ? targets : ['commands', 'xblox'];
const valid = new Set(['commands', 'xblox']);

for (const command of commands) {
  if (!valid.has(command)) {
    console.error(`info-skills: unsupported target "${command}". Expected one of: ${[...valid].join(', ')}`);
    process.exit(2);
  }
}

const exe = requireCli();
for (const command of commands) {
  console.error(`info-skills: pm-image-cli info ${command}`);
  const result = spawnSync(exe, ['info', command], {
    cwd: ROOT,
    stdio: 'inherit',
    timeout: 120_000,
  });
  if (result.error) throw result.error;
  if ((result.status ?? 1) !== 0) process.exit(result.status ?? 1);
}
