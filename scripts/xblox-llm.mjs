#!/usr/bin/env node
/**
 * Generate dist/shared/help/en/xblox.md from `pm-image-cli xblox info --json`.
 */
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { homedir, platform } from 'node:os';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(__dirname, '..');
const OUT_PATH = join(ROOT, 'dist', 'shared', 'help', 'en', 'xblox.md');

function parseArgs(argv) {
  const out = { commandsPath: '', outPath: OUT_PATH };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg === '--commands' && argv[i + 1]) out.commandsPath = resolve(argv[++i]);
    else if (arg === '--out' && argv[i + 1]) out.outPath = resolve(argv[++i]);
    else if (arg === '--help' || arg === '-h') {
      console.log(`Usage: node scripts/xblox-llm.mjs [--commands path] [--out path]

Writes ${OUT_PATH} by default.`);
      process.exit(0);
    }
  }
  return out;
}

function cliExePath() {
  const env = process.env.PM_IMAGE_CLI || process.env.MEDIA_IMG_CLI_EXE || process.env.PM_IMAGE || process.env.MEDIA_IMG_EXE;
  if (env && env.trim()) return resolve(env.trim());
  const name = platform() === 'win32' ? 'pm-image-cli.exe' : 'pm-image-cli';
  const candidates = [
    join(ROOT, 'dist', 'win-x64', name),
    join(ROOT, 'dist', name),
    join(ROOT, 'dist-osx', name),
  ];
  for (const candidate of candidates) {
    if (existsSync(candidate)) return candidate;
  }
  return candidates[0];
}

function defaultCommandsJsonPath() {
  if (process.env.PM_COMMANDS_JSON?.trim()) return resolve(process.env.PM_COMMANDS_JSON.trim());
  if (platform() === 'win32' && process.env.APPDATA) {
    return join(process.env.APPDATA, 'PolyMech', 'pm-image', 'commands.json');
  }
  const home = homedir();
  if (platform() === 'darwin') {
    return join(home, 'Library', 'Application Support', 'PolyMech', 'pm-image', 'commands.json');
  }
  const xdg = process.env.XDG_CONFIG_HOME;
  if (xdg) return join(xdg, 'PolyMech', 'pm-image', 'commands.json');
  return join(home, '.config', 'PolyMech', 'pm-image', 'commands.json');
}

function resolveCommandsJsonPath(explicit) {
  const candidates = [
    explicit,
    defaultCommandsJsonPath(),
    join(ROOT, 'dist', 'data', 'commands.json'),
    join(ROOT, 'apps', 'shared', 'customCommands', 'commands.json'),
    join(ROOT, 'apps', 'settings', 'src', 'settings', 'customCommands', 'commands.json'),
  ].filter(Boolean);
  for (const candidate of candidates) {
    if (existsSync(candidate)) return candidate;
  }
  return candidates[0] || defaultCommandsJsonPath();
}

function runCli(exe, commandsPath, args, timeoutMs = 60_000) {
  const argv = commandsPath ? ['--commands', commandsPath, ...args] : args;
  const result = spawnSync(exe, argv, {
    cwd: ROOT,
    encoding: 'utf8',
    timeout: timeoutMs,
    maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) throw result.error;
  return {
    status: result.status ?? -1,
    stdout: result.stdout || '',
    stderr: result.stderr || '',
  };
}

function parseJson(stdout, label) {
  const trimmed = stdout.trim();
  if (!trimmed) throw new Error(`${label}: empty stdout`);
  try {
    return JSON.parse(trimmed);
  } catch (err) {
    throw new Error(`${label}: invalid JSON (${err.message})`);
  }
}

function blockDefaultPreview(block) {
  const value = block?.block;
  if (!value || typeof value !== 'object') return '';
  return JSON.stringify(value);
}

function renderMarkdown(info) {
  const lines = [
    '# PM-Image XBlox (LLM brief)',
    '',
    'XBlox is the block-tree command flow surface. Use `pm-image-cli xblox info --json` for machine-readable block definitions, grouped palette metadata, default block JSON, param schemas, registered CLI commands, and custom command payloads.',
    '',
    '## Document shape',
    '',
    '`{ "version": 1, "context": {}, "roots": [...] }`',
    '',
    '## Blocks',
    '',
  ];

  const blocks = Array.isArray(info.blocks) ? info.blocks : [];
  const byGroup = new Map();
  for (const block of blocks) {
    const group = block?.group || 'Other';
    if (!byGroup.has(group)) byGroup.set(group, []);
    byGroup.get(group).push(block);
  }

  for (const [group, groupBlocks] of [...byGroup.entries()].sort(([a], [b]) => a.localeCompare(b))) {
    lines.push(`### ${group}`, '');
    for (const block of groupBlocks.sort((a, b) => String(a?.kind || '').localeCompare(String(b?.kind || '')))) {
      const kind = String(block?.kind || '');
      const label = String(block?.label || kind);
      const desc = String(block?.description || '').trim();
      lines.push(`#### \`${kind}\`${label && label !== kind ? ` — ${label}` : ''}`, '');
      if (desc) lines.push(desc, '');
      const params = Array.isArray(block?.params) ? block.params : [];
      if (params.length) {
        lines.push('Params:');
        for (const param of params) {
          const name = String(param?.name || '');
          const type = String(param?.type || 'value');
          const bits = [type];
          if (param?.required) bits.push('required');
          if (Object.prototype.hasOwnProperty.call(param || {}, 'default')) bits.push(`default \`${JSON.stringify(param.default)}\``);
          lines.push(`- \`${name}\` (${bits.join(', ')})`);
        }
        lines.push('');
      }
      const preview = blockDefaultPreview(block);
      if (preview) lines.push(`Default block: \`${preview}\``, '');
    }
  }

  const registered = info.commands?.registeredCommands || [];
  const customCommands = info.commands?.customCommands || [];
  lines.push('## Supported Commands', '');
  lines.push(`- Registered CLI commands: ${registered.filter((cmd) => cmd?.available !== false).map((cmd) => `\`${cmd.id}\``).join(', ') || '(none)'}`);
  lines.push(`- Custom commands from commands.json: ${customCommands.length}`);
  if (customCommands.length) {
    for (const cmd of customCommands) {
      lines.push(`- \`${cmd.id || cmd.label}\`${cmd.label && cmd.label !== cmd.id ? ` — ${cmd.label}` : ''} (${cmd.action || 'metadata'}, group ${cmd.group || 'Custom'})`);
    }
  }
  lines.push('');

  return `${lines.join('\n').replace(/\n{3,}/g, '\n\n')}\n`;
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const exe = cliExePath();
  if (!existsSync(exe)) {
    console.error(`pm-image CLI not found at ${exe}. Run npm run build:cpp first.`);
    process.exit(1);
  }

  const commandsPath = resolveCommandsJsonPath(args.commandsPath);
  const result = runCli(exe, commandsPath, ['xblox', 'info', '--json']);
  if (result.status !== 0) {
    console.error(result.stderr.trim() || result.stdout.trim() || `xblox info --json failed (exit ${result.status})`);
    process.exit(result.status || 1);
  }
  const info = parseJson(result.stdout, 'xblox info --json');
  const markdown = renderMarkdown(info);
  mkdirSync(dirname(args.outPath), { recursive: true });
  writeFileSync(args.outPath, markdown, 'utf8');
  console.log(`Wrote ${args.outPath} (${Array.isArray(info.blocks) ? info.blocks.length : 0} blocks, ${info.commands?.customCommands?.length || 0} custom commands)`);
}

main();
