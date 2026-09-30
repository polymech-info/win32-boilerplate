#!/usr/bin/env node
/**
 * Generate dist/shared/help/en/tools.md — brief catalog of chat-agent path tools.
 *
 *   node scripts/tools-llm.mjs
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

import { HELP_EN, parseJson, requireCli, runCli } from './llm-help-lib.mjs';

const OUT_PATH = join(HELP_EN, 'tools.md');

function schemaType(schema) {
  if (!schema || typeof schema !== 'object') return 'any';
  if (Array.isArray(schema.type)) return schema.type.join('|');
  if (schema.type) return schema.type;
  if (schema.enum) return 'enum';
  return 'object';
}

function formatSchemaField(name, schema, required) {
  const bits = [schemaType(schema)];
  bits.push(required ? 'required' : 'optional');
  if (Array.isArray(schema.enum) && schema.enum.length) {
    const shown = schema.enum.filter((v) => v !== '').slice(0, 6);
    if (shown.length) bits.push(`enum: ${shown.map((v) => `\`${JSON.stringify(v)}\``).join(', ')}`);
  }
  const desc = (schema.description || '').trim();
  return desc
    ? `- \`${name}\` (${bits.join(', ')}) — ${desc}`
    : `- \`${name}\` (${bits.join(', ')})`;
}

function flattenSchema(schema, prefix = '', depth = 0) {
  if (!schema?.properties || depth > 1) return [];
  const required = new Set(Array.isArray(schema.required) ? schema.required : []);
  const lines = [];
  for (const [name, field] of Object.entries(schema.properties)) {
    const path = prefix ? `${prefix}.${name}` : name;
    if (field?.type === 'object' && field.properties && depth === 0) {
      lines.push(formatSchemaField(path, field, required.has(name)));
      for (const [child, childSchema] of Object.entries(field.properties)) {
        lines.push(formatSchemaField(`${path}.${child}`, childSchema, false));
      }
      continue;
    }
    lines.push(formatSchemaField(path, field, required.has(name)));
  }
  return lines;
}

function loadToolCatalog(exe) {
  const result = runCli(exe, ['llm', 'tools-list', '--path', '--json']);
  if (result.status !== 0) {
    throw new Error(result.stderr.trim() || result.stdout.trim() || `tools-list --path failed (exit ${result.status})`);
  }
  const doc = parseJson(result.stdout, 'llm tools-list --path');
  return doc.tools || [];
}

function loadToolMeta(exe) {
  const result = runCli(exe, ['llm', 'info', 'tools', '--json']);
  if (result.status !== 0) {
    throw new Error(result.stderr.trim() || `llm info tools failed (exit ${result.status})`);
  }
  const arr = parseJson(result.stdout, 'llm info tools');
  const meta = new Map();
  if (Array.isArray(arr)) {
    for (const row of arr) {
      if (!row?.name || row.name.startsWith('_')) continue;
      meta.set(row.name, row);
    }
  }
  return meta;
}

function groupTools(tools, meta) {
  const groups = new Map();
  for (const tool of tools) {
    const name = tool.name;
    const row = meta.get(name);
    const group = row?.group || 'Other';
    if (!groups.has(group)) groups.set(group, []);
    groups.get(group).push({ tool, row });
  }
  const order = ['File', 'Image', 'Utility', 'Scheduler', 'Memory', 'Other'];
  const rank = new Map(order.map((g, i) => [g, i]));
  return [...groups.entries()].sort(([a], [b]) => {
    const ra = rank.has(a) ? rank.get(a) : order.length;
    const rb = rank.has(b) ? rank.get(b) : order.length;
    if (ra !== rb) return ra - rb;
    return a.localeCompare(b);
  });
}

function renderMarkdown(tools, meta) {
  const lines = [
    '# PM-Image agent tools (LLM brief)',
    '',
    'Path-mode tools sent to the in-app chat agent (`llm agent`). For shell/CLI subcommands see [commands.md](commands.md).',
    '',
    '**Guidelines:**',
    '',
    '- Relative paths resolve against the current Explorer folder (or process cwd).',
    '- Use **file_read** / **file_glob** / **write_file** for text and structured files (`.md`, `.json`, `.csv`, `.dxf`, `.svg`, logs).',
    '- Use **list_images** and **image_*** tools for raster photos.',
    '- Call the tool that matches intent; do not narrate without invoking tools.',
    '',
    '---',
    '',
  ];

  for (const [group, entries] of groupTools(tools, meta)) {
    lines.push(`## ${group}`, '');
    for (const { tool, row } of entries.sort((a, b) => a.tool.name.localeCompare(b.tool.name))) {
      const desc = (tool.description || row?.short_desc || '').trim();
      lines.push(`### \`${tool.name}\``, '');
      if (desc) lines.push(desc, '');
      if (row?.globally_enabled === false) lines.push('- _Globally disabled in settings._', '');
      const fields = flattenSchema(tool.input_schema);
      if (fields.length) {
        lines.push('**Parameters:**', '', ...fields, '');
      }
    }
  }

  return `${lines.join('\n').replace(/\n{3,}/g, '\n\n')}\n`;
}

function main() {
  const exe = requireCli();
  const tools = loadToolCatalog(exe);
  const meta = loadToolMeta(exe);
  const markdown = renderMarkdown(tools, meta);
  mkdirSync(dirname(OUT_PATH), { recursive: true });
  writeFileSync(OUT_PATH, markdown, 'utf8');
  console.log(`Wrote ${OUT_PATH} (${tools.length} tools)`);
}

main();
