#!/usr/bin/env node
/**
 * Generate a brief LLM-oriented command catalog at dist/shared/help/en/commands.md.
 *
 * Built-in CLI metadata comes from pm-image-cli `--help --json` (same shape as
 * settingsCliCommandHelpGet / CommandEditor). Custom commands are read from
 * commands.json (profile path by default; override with --commands or PM_COMMANDS_JSON).
 *
 *   node scripts/cli-llm.mjs
 *   node scripts/cli-llm.mjs --commands path/to/commands.json
 */
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { homedir, platform } from 'node:os';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(__dirname, '..');
const OUT_PATH = join(ROOT, 'dist', 'shared', 'help', 'en', 'commands.md');

function parseArgs(argv) {
  const out = { commandsPath: '', outPath: OUT_PATH };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg === '--commands' && argv[i + 1]) {
      out.commandsPath = resolve(argv[++i]);
    } else if (arg === '--out' && argv[i + 1]) {
      out.outPath = resolve(argv[++i]);
    } else if (arg === '--help' || arg === '-h') {
      console.log(`Usage: node scripts/cli-llm.mjs [--commands path] [--out path]

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

async function loadCliSchemaTree(exe, commandsPath, commandId) {
  const out = [];
  const seen = new Set();

  async function visit(commandPath) {
    if (seen.has(commandPath)) return;
    seen.add(commandPath);
    const tokens = commandPath.split(/\s+/).filter(Boolean);
    const result = runCli(exe, commandsPath, [...tokens, '--help', '--json']);
    if (result.status !== 0) {
      throw new Error(`${commandPath} --help --json failed (exit ${result.status}): ${result.stderr.trim() || result.stdout.trim()}`);
    }
    const schema = parseJson(result.stdout, commandPath);
    out.push([commandPath, schema]);
    for (const child of schema.subcommands || []) {
      if (child?.name) await visit(`${commandPath} ${child.name}`);
    }
  }

  await visit(commandId);
  return out;
}

function optionPrimaryName(option) {
  return option.names?.find((name) => name.startsWith('--')) || option.names?.[0] || option.name;
}

function isHelpOption(option) {
  const names = new Set((option.names || []).concat(option.name || []));
  return names.has('-h') || names.has('--help') || names.has('-v') || names.has('--version');
}

function formatOptionLine(option) {
  const name = optionPrimaryName(option);
  const bits = [];
  if (option.typeName) bits.push(option.typeName);
  if (option.required) bits.push('required');
  else bits.push('optional');
  if (option.default) bits.push(`default \`${option.default}\``);
  if (option.positional) bits.push('positional');
  if (option.expectedMin === 0 && option.expectedMax === 0) bits.push('flag');
  const meta = bits.length ? ` (${bits.join(', ')})` : '';
  const desc = (option.description || '').trim();
  return desc ? `- \`${name}\`${meta} — ${desc}` : `- \`${name}\`${meta}`;
}

function renderCliSchemaSection(commandPath, schema) {
  const lines = [];
  const heading = commandPath.includes(' ') ? `#### \`${commandPath}\`` : `### \`${commandPath}\``;
  lines.push(heading, '');
  const description = (schema.description || '').trim();
  if (description) {
    lines.push(description, '');
  }
  if (schema.allowExtras) {
    lines.push('- Allows extra positional arguments.', '');
  }

  const subcommands = (schema.subcommands || []).filter((s) => s?.name);
  if (subcommands.length) {
    lines.push('**Subcommands:**', '');
    for (const sub of subcommands) {
      const subDesc = (sub.description || '').trim();
      lines.push(subDesc ? `- \`${sub.name}\` — ${subDesc}` : `- \`${sub.name}\``);
    }
    lines.push('');
  }

  const options = (schema.options || []).filter((opt) => !isHelpOption(opt));
  if (options.length) {
    lines.push('**Options:**', '');
    for (const opt of options) lines.push(formatOptionLine(opt));
    lines.push('');
  }

  return lines;
}

function jsonString(obj, key) {
  const value = obj?.[key];
  return typeof value === 'string' ? value : '';
}

function actionLabel(item) {
  const app = jsonString(item, 'appCommand');
  if (app) return `app:${app}`;
  const cli = jsonString(item, 'cliCommand');
  if (cli) return `cli:${cli}`;
  const ribbon = jsonString(item, 'ribbonCommand');
  if (ribbon) return `ribbon:${ribbon}`;
  const url = jsonString(item, 'url');
  if (url) return 'url';
  const path = jsonString(item, 'path');
  if (path) return 'path';
  const ext = item?.externalCommand;
  if (ext && typeof ext === 'object') {
    if (jsonString(ext, 'command')) return 'external';
    if (jsonString(ext, 'mode') === 'shell' && jsonString(ext, 'shellLine')) return 'external:shell';
  }
  return 'metadata';
}

function collectVisibleCustomItems(items, groupLabel, out) {
  if (!Array.isArray(items)) return;
  for (const item of items) {
    if (!item || typeof item !== 'object') continue;
    if (item.enabled === false || item.visible === false) continue;
    const type = item.type || 'button';
    if (type !== 'separator') {
      const id = jsonString(item, 'id');
      const label = jsonString(item, 'label');
      if (id || label) {
        out.push({
          group: groupLabel,
          id,
          label: label || id,
          type,
          action: actionLabel(item),
          item,
        });
      }
    }
    collectVisibleCustomItems(item.items, groupLabel, out);
  }
}

function loadCustomCommands(commandsPath) {
  if (!existsSync(commandsPath)) return { path: commandsPath, items: [] };
  const raw = readFileSync(commandsPath, 'utf8').replace(/^\uFEFF/, '').trim();
  if (!raw) return { path: commandsPath, items: [] };
  const doc = JSON.parse(raw);
  const groups = doc?.ribbon?.groups ?? doc?.groups ?? (Array.isArray(doc) ? doc : []);
  const items = [];
  if (Array.isArray(groups)) {
    for (const group of groups) {
      if (!group || typeof group !== 'object') continue;
      const groupLabel = jsonString(group, 'label') || jsonString(group, 'id') || 'Custom';
      collectVisibleCustomItems(group.items, groupLabel, items);
    }
  }
  return { path: commandsPath, items };
}

function renderCustomCommand(itemRow) {
  const { id, label, type, action, item } = itemRow;
  const lines = [];
  lines.push(`#### \`${id || label}\`${label && label !== id ? ` — ${label}` : ''}`, '');
  lines.push(`- **Type:** ${type}`);
  lines.push(`- **Action:** ${action}`);
  const tooltip = jsonString(item, 'tooltip');
  if (tooltip && tooltip !== label) lines.push(`- **Tooltip:** ${tooltip}`);
  if (item.appCommand) lines.push(`- **App command:** \`${item.appCommand}\``);
  if (item.cliCommand) lines.push(`- **CLI command:** \`${item.cliCommand}\``);
  if (item.ribbonCommand) lines.push(`- **Ribbon command:** \`${item.ribbonCommand}\``);
  if (item.url) lines.push(`- **URL:** ${item.url}`);
  if (item.path) lines.push(`- **Path:** \`${item.path}\``);
  if (Array.isArray(item.args) && item.args.length) {
    lines.push(`- **Args:** ${item.args.map((a) => `\`${a}\``).join(' ')}`);
  }
  if (item.cwd) lines.push(`- **CWD:** \`${item.cwd}\``);
  if (item.logLevel) lines.push(`- **Log level:** \`${item.logLevel}\``);
  if (item.externalCommand && typeof item.externalCommand === 'object') {
    const ext = item.externalCommand;
    lines.push(`- **External:** mode \`${ext.mode || 'argv'}\``);
    if (ext.command) lines.push(`  - command: \`${ext.command}\``);
    if (ext.shellLine) lines.push(`  - shell: \`${ext.shellLine}\``);
    if (Array.isArray(ext.args) && ext.args.length) {
      lines.push(`  - args: ${ext.args.map((a) => `\`${a}\``).join(' ')}`);
    }
    if (ext.cwd) lines.push(`  - cwd: \`${ext.cwd}\``);
  }
  if (item.source && typeof item.source === 'object') {
    const kind = item.source.kind || 'selection';
    lines.push(`- **Source:** ${kind}`);
    if (Array.isArray(item.source.files) && item.source.files.length) {
      lines.push(`  - files: ${item.source.files.map((f) => `\`${f}\``).join(', ')}`);
    }
    if (Array.isArray(item.source.folders) && item.source.folders.length) {
      lines.push(`  - folders: ${item.source.folders.map((f) => `\`${f}\``).join(', ')}`);
    }
  }
  if (item.output && typeof item.output === 'object') {
    const bits = [];
    if (item.output.directory) bits.push(`directory \`${item.output.directory}\``);
    if (item.output.filenamePattern) bits.push(`pattern \`${item.output.filenamePattern}\``);
    if (item.output.overwrite === true) bits.push('overwrite');
    if (bits.length) lines.push(`- **Output:** ${bits.join('; ')}`);
  }
  if (item.registerInExplorer) lines.push('- **Explorer menu:** yes');
  if (item.asLlmTool) lines.push('- **LLM tool candidate:** yes');
  lines.push('');
  return lines;
}

function groupCustomCommands(items) {
  const groups = new Map();
  for (const item of items) {
    const key = item.group || 'Custom';
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(item);
  }
  return [...groups.entries()].sort(([a], [b]) => a.localeCompare(b));
}

function loadCommandVariables() {
  const settingsCpp = join(ROOT, 'src', 'win', 'ui_next', 'CSettingsWebView.cpp');
  const out = [];
  const seen = new Set();

  const add = (name, group, description) => {
    if (!name || seen.has(name)) return;
    seen.add(name);
    out.push({ name, group: group || 'Other', description: description || '' });
  };

  if (existsSync(settingsCpp)) {
    const text = readFileSync(settingsCpp, 'utf8');
    const pushRe = /push\("([^"]+)",\s*"([^"]*)",\s*"((?:\\.|[^"\\])*)"\)/g;
    for (const match of text.matchAll(pushRe)) {
      add(match[1], match[2], match[3].replace(/\\"/g, '"'));
    }
  }

  add('ENV:NAME', 'Environment', 'Any process environment variable. Example: `${ENV:USERPROFILE}`, `${ENV:HOME}`. Unknown or unset names are left unchanged.');

  const groupOrder = [
    'Current file',
    'Source',
    'Process',
    'Date / time',
    'Known folders - Portable',
    'Known folders - User',
    'Known folders - App data',
    'Known folders - Public',
    'Known folders - System',
    'Known folders - Virtual',
    'Known folders',
    'Environment',
    'Other',
  ];
  const rank = new Map(groupOrder.map((g, i) => [g, i]));
  out.sort((a, b) => {
    const ga = rank.has(a.group) ? rank.get(a.group) : groupOrder.length;
    const gb = rank.has(b.group) ? rank.get(b.group) : groupOrder.length;
    if (ga !== gb) return ga - gb;
    return a.name.localeCompare(b.name);
  });
  return out;
}

function groupCommandVariables(items) {
  const groups = new Map();
  for (const item of items) {
    const key = item.group || 'Other';
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(item);
  }
  return [...groups.entries()];
}

function renderCommandVariablesSection(variables) {
  const lines = [
    '## Command variables',
    '',
    'Custom command fields (`args`, `cwd`, `path`, `externalCommand`, `source`, `output`) support `${NAME}` substitution via `media::commands::resolve_variables` (`src/core/command_variables.cpp`).',
    '',
  ];
  for (const [groupLabel, groupItems] of groupCommandVariables(variables)) {
    lines.push(`### ${groupLabel}`, '');
    for (const item of groupItems) {
      const desc = item.description ? ` — ${item.description}` : '';
      lines.push(`- \`${item.name}\`${desc}`);
    }
    lines.push('');
  }
  return lines;
}

function blockDefaultPreview(block) {
  const value = block?.block;
  if (!value || typeof value !== 'object') return '';
  return JSON.stringify(value);
}

function renderXbloxInfoSection(xbloxInfo) {
  const lines = ['## XBlox composition metadata', ''];
  if (!xbloxInfo || typeof xbloxInfo !== 'object') {
    lines.push('_XBlox metadata unavailable in this build._', '');
    return lines;
  }

  lines.push('Use `pm-image-cli xblox info --json` for machine-readable block definitions, grouped palette metadata, default block JSON, param schemas, registered CLI commands, and custom command payloads.', '');

  const blocks = Array.isArray(xbloxInfo.blocks) ? xbloxInfo.blocks : [];
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

  const registered = xbloxInfo.commands?.registeredCommands || [];
  const customCommands = xbloxInfo.commands?.customCommands || [];
  lines.push('### Supported commands in XBlox', '');
  lines.push(`- Registered CLI commands: ${registered.filter((cmd) => cmd?.available !== false).map((cmd) => `\`${cmd.id}\``).join(', ') || '(none)'}`);
  lines.push(`- Custom commands from commands.json: ${customCommands.length}`);
  if (customCommands.length) {
    for (const cmd of customCommands.slice(0, 30)) {
      lines.push(`- \`${cmd.id || cmd.label}\`${cmd.label && cmd.label !== cmd.id ? ` — ${cmd.label}` : ''} (${cmd.action || 'metadata'}, group ${cmd.group || 'Custom'})`);
    }
    if (customCommands.length > 30) lines.push(`- ... ${customCommands.length - 30} more custom commands`);
  }
  lines.push('');
  return lines;
}

function renderMarkdown({ registeredCommands, schemaTrees, custom, variables, xbloxInfo }) {
  const lines = [
    '# PM-Image commands (LLM brief)',
    '',
    '## Built-in CLI commands',
    '',
  ];

  const available = registeredCommands.filter((cmd) => cmd.available !== false);
  for (const cmd of available) {
    const trees = schemaTrees.get(cmd.id) || [];
    if (!trees.length) {
      lines.push(`### \`${cmd.id}\``, '');
      if (cmd.label) lines.push(`${cmd.label}.`, '');
      lines.push('_Schema unavailable in this build._', '');
      continue;
    }
    const [rootPath, rootSchema] = trees[0];
    lines.push(...renderCliSchemaSection(rootPath, rootSchema));
    for (let i = 1; i < trees.length; ++i) {
      const [path, schema] = trees[i];
      if (path === rootPath) continue;
      lines.push(...renderCliSchemaSection(path, schema));
    }
  }

  lines.push('---', '', ...renderXbloxInfoSection(xbloxInfo), '---', '', ...renderCommandVariablesSection(variables), '---', '', '## Custom commands', '');
  if (!custom.items.length) {
    lines.push('_No enabled/visible custom commands found in commands.json._', '');
  } else {
    for (const [groupLabel, groupItems] of groupCustomCommands(custom.items)) {
      lines.push(`### ${groupLabel}`, '');
      for (const item of groupItems) lines.push(...renderCustomCommand(item));
    }
  }

  return `${lines.join('\n').replace(/\n{3,}/g, '\n\n')}\n`;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  const exe = cliExePath();
  if (!existsSync(exe)) {
    console.error(`pm-image CLI not found at ${exe}. Run npm run build:cpp first.`);
    process.exit(1);
  }

  const commandsPath = resolveCommandsJsonPath(args.commandsPath);
  const listResult = runCli(exe, commandsPath, ['commands', '--json']);
  if (listResult.status !== 0) {
    console.error(listResult.stderr.trim() || listResult.stdout.trim() || `commands --json failed (exit ${listResult.status})`);
    process.exit(1);
  }
  const listing = parseJson(listResult.stdout, 'commands --json');
  const registeredCommands = listing.registeredCommands || [];

  const schemaTrees = new Map();
  for (const cmd of registeredCommands) {
    if (!cmd?.id || cmd.available === false) continue;
    process.stderr.write(`cli-llm: schema ${cmd.id}\n`);
    try {
      schemaTrees.set(cmd.id, await loadCliSchemaTree(exe, commandsPath, cmd.id));
    } catch (err) {
      console.error(`cli-llm: warning: ${cmd.id}: ${err.message}`);
      schemaTrees.set(cmd.id, []);
    }
  }

  const custom = loadCustomCommands(commandsPath);
  const variables = loadCommandVariables();
  let xbloxInfo = null;
  if (registeredCommands.some((cmd) => cmd?.id === 'xblox' && cmd.available !== false)) {
    const xbloxResult = runCli(exe, commandsPath, ['xblox', 'info', '--json']);
    if (xbloxResult.status === 0) {
      xbloxInfo = parseJson(xbloxResult.stdout, 'xblox info --json');
    } else {
      console.error(`cli-llm: warning: xblox info --json failed (exit ${xbloxResult.status}): ${xbloxResult.stderr.trim() || xbloxResult.stdout.trim()}`);
    }
  }
  const markdown = renderMarkdown({ registeredCommands, schemaTrees, custom, variables, xbloxInfo });
  mkdirSync(dirname(args.outPath), { recursive: true });
  writeFileSync(args.outPath, markdown, 'utf8');
  console.log(`Wrote ${args.outPath} (${registeredCommands.length} CLI commands, ${variables.length} variables, ${custom.items.length} custom commands)`);
}

main().catch((err) => {
  console.error(err?.stack || String(err));
  process.exit(1);
});
