#!/usr/bin/env node
/**
 * Generate dist/shared/help/en/skills-index.md from llm info skills --json.
 *
 *   node scripts/skills-llm.mjs
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

import { HELP_EN, parseJson, requireCli, runCli } from './llm-help-lib.mjs';

const OUT_PATH = join(HELP_EN, 'skills-index.md');

function loadSkillsSnapshot(exe) {
  const result = runCli(exe, ['llm', 'info', 'skills', '--json']);
  if (result.status !== 0) {
    throw new Error(result.stderr.trim() || `llm info skills failed (exit ${result.status})`);
  }
  return parseJson(result.stdout, 'llm info skills');
}

function renderMarkdown(snapshot) {
  const lines = [
    '# PM-Image agent skills index',
    '',
    'Discovered `SKILL.md` entries for the chat agent. Full skill bodies live at the paths below.',
    '',
  ];

  const policy = snapshot.policy || {};
  lines.push('## Policy', '');
  lines.push(`- **enabled:** ${policy.enabled !== false}`);
  lines.push(`- **roaming_enabled:** ${policy.roaming_enabled !== false}`);
  lines.push(`- **workspace_enabled:** ${policy.workspace_enabled !== false}`);
  if (Array.isArray(policy.pinned) && policy.pinned.length) {
    lines.push(`- **pinned:** ${policy.pinned.map((x) => `\`${x}\``).join(', ')}`);
  }
  if (Array.isArray(policy.disabled) && policy.disabled.length) {
    lines.push(`- **disabled:** ${policy.disabled.map((x) => `\`${x}\``).join(', ')}`);
  }
  lines.push('');

  const roots = snapshot.roots || {};
  lines.push('## Roots', '');
  if (roots.roaming) lines.push(`- **roaming:** \`${roots.roaming}\``);
  if (roots.workspace) lines.push(`- **workspace:** \`${roots.workspace}\``);
  lines.push('');

  const skills = Array.isArray(snapshot.skills) ? snapshot.skills : [];
  lines.push('## Skills', '');
  if (!skills.length) {
    lines.push('_No skills discovered at generation time._', '');
    return `${lines.join('\n')}\n`;
  }

  const bySource = new Map();
  for (const skill of skills) {
    const key = skill.source || 'unknown';
    if (!bySource.has(key)) bySource.set(key, []);
    bySource.get(key).push(skill);
  }

  for (const [source, entries] of [...bySource.entries()].sort(([a], [b]) => a.localeCompare(b))) {
    lines.push(`### ${source}`, '');
    for (const skill of entries.sort((a, b) => (a.name || '').localeCompare(b.name || ''))) {
      const flags = [];
      if (skill.active) flags.push('active');
      if (skill.always) flags.push('always');
      if (skill.pinned) flags.push('pinned');
      if (skill.disabled) flags.push('disabled');
      if (skill.available === false) flags.push('unavailable');
      const flagText = flags.length ? ` (${flags.join(', ')})` : '';
      lines.push(`#### \`${skill.name || 'unnamed'}\`${flagText}`, '');
      if (skill.description) lines.push(skill.description, '');
      if (skill.path) lines.push(`- **Path:** \`${skill.path}\``);
      if (Array.isArray(skill.missing_requirements) && skill.missing_requirements.length) {
        lines.push(`- **Missing:** ${skill.missing_requirements.map((x) => `\`${x}\``).join(', ')}`);
      }
      lines.push('');
    }
  }

  return `${lines.join('\n').replace(/\n{3,}/g, '\n\n')}\n`;
}

function main() {
  const exe = requireCli();
  const snapshot = loadSkillsSnapshot(exe);
  const markdown = renderMarkdown(snapshot);
  mkdirSync(dirname(OUT_PATH), { recursive: true });
  writeFileSync(OUT_PATH, markdown, 'utf8');
  const count = Array.isArray(snapshot.skills) ? snapshot.skills.length : 0;
  console.log(`Wrote ${OUT_PATH} (${count} skills)`);
}

main();
