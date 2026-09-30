#!/usr/bin/env node
/**
 * Generate all LLM brief help files under dist/shared/help/en/.
 *
 *   npm run docs:llm
 */
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = join(__dirname, '..');
const HELP_EN = join(ROOT, 'dist', 'shared', 'help', 'en');

const SCRIPTS = ['cli-llm.mjs', 'xblox-llm.mjs', 'tools-llm.mjs', 'skills-llm.mjs'];

function runScript(name) {
  const path = join(__dirname, name);
  process.stderr.write(`docs:llm: ${name}\n`);
  const result = spawnSync(process.execPath, [path], { cwd: ROOT, stdio: 'inherit' });
  if (result.status !== 0) process.exit(result.status ?? 1);
}

function writeIndex() {
  const body = `# PM-Image LLM help index

Brief generated references for agents and skills. Regenerate with \`npm run docs:llm\`.

| File | Contents |
|------|----------|
| [commands.md](commands.md) | CLI subcommands, custom ribbon commands, command variables |
| [xblox.md](xblox.md) | XBlox block groups, params, defaults, and supported commands |
| [tools.md](tools.md) | Chat-agent path tools (schemas) |
| [skills-index.md](skills-index.md) | Discovered agent skills |
| [cli.md](cli.md) | Full CLI prose reference |

**When to use which surface**

- **In-app chat agent** → \`tools.md\` (path tools like \`file_read\`, \`image_transform\`).
- **Shell / automation / custom commands** → \`commands.md\` (\`pm-image-cli resize\`, \`commands.json\` presets).
- **XBlox block-flow composition** → \`xblox.md\`.
- **Deep option semantics** → \`cli.md\`.
`;
  writeFileSync(join(HELP_EN, 'index.md'), body, 'utf8');
  console.log(`Wrote ${join(HELP_EN, 'index.md')}`);
}

function main() {
  mkdirSync(HELP_EN, { recursive: true });
  for (const script of SCRIPTS) runScript(script);
  writeIndex();
}

main();
