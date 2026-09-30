/**
 * Custom-command integration tests: commands.json listing + CLI execution.
 *
 * Invoked via:
 *   npm run test:commands
 *
 * This test writes an isolated temporary commands.json and passes it through
 * `pm-image-cli --commands <path>` so the user's live profile is untouched.
 */

import { spawnSync } from 'node:child_process';
import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { createAssert } from './test-commons.js';
import { renderMarkdownReport } from './reports.js';
import {
  appendMarkdownChapter,
  getChapterTitle,
  getNpmScriptLabel,
  isAccumulateMode,
  TEST_REPORT_ALL_PATH,
} from './test-report-accumulate.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = resolve(__dirname, '..', '..');
const TEST_REPORT_PATH = join(__dirname, '..', 'test-report-last.md');
const RUN_REAL_AI = process.argv.includes('--real-ai') ||
  process.argv.includes('--real-llm') ||
  process.env.PM_COMMANDS_TEST_AI_REAL === '1' ||
  process.env.PM_COMMANDS_TEST_LLM_REAL === '1';
function cliExePath() {
  const env = process.env.PM_IMAGE_CLI || process.env.MEDIA_IMG_CLI_EXE;
  if (env && env.trim()) return resolve(env.trim());
  const name = process.platform === 'win32' ? 'pm-image-cli.exe' : 'pm-image-cli';
  const winX64 = resolve(PACKAGE_ROOT, 'dist', 'win-x64', name);
  if (existsSync(winX64)) return winX64;
  return resolve(PACKAGE_ROOT, 'dist', name);
}

function createTestReport() {
  return {
    meta: {},
    notes: [],
    suites: [],
    images: [],
    _cur: null,
    note(text) { this.notes.push(text); },
    addImage(row) { this.images.push(row); },
    beginSuite(name) {
      if (this._cur) this.endSuite();
      this._cur = { name, steps: [], t0: performance.now() };
    },
    endSuite() {
      if (!this._cur) return;
      this._cur.totalMs = performance.now() - this._cur.t0;
      this.suites.push(this._cur);
      this._cur = null;
    },
    step(label, ms, detail = '') {
      if (!this._cur) return;
      this._cur.steps.push({ label, ms: Math.round(ms * 100) / 100, detail: detail || '' });
    },
    finalize(stats, wallMs, exe, commandsPath, extra = {}) {
      this.meta = { passed: stats.passed, failed: stats.failed, wallMs: Math.round(wallMs), exe, commandsPath, ...extra };
    },
  };
}

function runCli(exe, commandsPath, args, options = {}) {
  const t0 = performance.now();
  const result = spawnSync(exe, ['--commands', commandsPath, ...args], {
    cwd: PACKAGE_ROOT,
    encoding: 'utf8',
    timeout: options.timeout ?? 30_000,
    env: { ...process.env, PM_COMMANDS_TEST_VALUE: 'env-ok' },
  });
  return {
    status: result.status ?? -1,
    stdout: result.stdout || '',
    stderr: result.stderr || '',
    ms: performance.now() - t0,
  };
}

async function writeTransformFixture(path) {
  const sharp = (await import('sharp')).default;
  const svg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <rect width="512" height="512" fill="#f8fafc"/>
  <circle cx="168" cy="176" r="92" fill="#ec4899"/>
  <rect x="232" y="120" width="168" height="168" rx="28" fill="#38bdf8"/>
  <path d="M96 392 C168 292 232 452 416 324" fill="none" stroke="#111827" stroke-width="28" stroke-linecap="round"/>
  <text x="256" y="472" text-anchor="middle" font-family="Arial, sans-serif" font-size="42" fill="#111827">PM TEST</text>
</svg>`;
  await sharp(Buffer.from(svg)).jpeg({ quality: 92 }).toFile(path);
}

function testCommandDocument(workDir, sourceFile) {
  const isWin = process.platform === 'win32';
  const nodeExe = process.execPath;
  const spacedPath = join(workDir, 'path with spaces.txt');
  const llmIncludePath = join(workDir, 'llm include.txt');
  const llmExtraIncludePath = join(workDir, 'llm extra include.txt');
  const transformInputPath = join(workDir, 'tests', 'assets', 'commands', 'illustration.jpg');
  const llmLogPath = join(workDir, 'agent-log.json');
  const llmExtraLogPath = join(workDir, 'agent-extra-log.json');
  const llmRealLogPath = join(workDir, 'agent-real-log.json');
  const llmGlobLogPath = join(workDir, 'agent-glob-log.json');
  const imageGlobDir = join(workDir, 'images');
  const argvEchoScript = 'console.log(JSON.stringify(process.argv.slice(1)))';
  const cwdScript = 'console.log(process.cwd())';
  const listCommand = isWin
    ? { command: 'cmd.exe', args: ['/c', 'dir', '/b'], cwd: workDir, mode: 'argv' }
    : { command: 'ls', args: ['-1'], cwd: workDir, mode: 'argv' };
  const catCommand = isWin
    ? { command: 'cmd.exe', args: ['/c', 'type', 'hello.txt'], cwd: workDir, mode: 'argv' }
    : { command: 'cat', args: ['hello.txt'], cwd: workDir, mode: 'argv' };
  const echoCommand = isWin
    ? {
        command: 'cmd.exe',
        args: [
          '/c',
          'echo',
          'PMTEST_ARGV:${SRC_NAME}:${PATH_SEP}:${ENV:USERNAME}:${ENV:PM_COMMANDS_TEST_VALUE}:${ENV:PM_COMMANDS_TEST_MISSING}',
        ],
        mode: 'argv',
      }
    : {
        command: 'printf',
        args: [
          'PMTEST_ARGV:%s:%s:%s:%s:%s\\n',
          '${SRC_NAME}',
          '${PATH_SEP}',
          '${ENV:USER}',
          '${ENV:PM_COMMANDS_TEST_VALUE}',
          '${ENV:PM_COMMANDS_TEST_MISSING}',
        ],
        mode: 'argv',
      };
  const shellLine = isWin
    ? 'Get-Content hello.txt; Write-Output "PMTEST_SHELL:${SRC_NAME}:${PATH_SEP}:${ENV:USERNAME}:${ENV:PM_COMMANDS_TEST_VALUE}"'
    : 'cat hello.txt; printf "PMTEST_SHELL:${SRC_NAME}:${PATH_SEP}:${ENV:USER}:${ENV:PM_COMMANDS_TEST_VALUE}\\n"';
  const redirectionLine = isWin
    ? '"first" > shell-redir.txt; "second" >> shell-redir.txt; Get-Content shell-redir.txt'
    : 'printf "first\\n" > shell-redir.txt; printf "second\\n" >> shell-redir.txt; cat shell-redir.txt';
  const pipeLine = isWin
    ? '"alpha","beta" | Select-String beta | ForEach-Object { $_.Line }'
    : 'printf "alpha\\nbeta\\n" | grep beta';

  return {
    version: 1,
    ribbon: {
      groups: [{
        id: 'pm-test-custom-cli',
        label: 'PM Test',
        items: [
          {
            type: 'button',
            id: 'custom.pm-test-argv',
            label: 'PM Test Argv',
            enabled: true,
            visible: true,
            source: { kind: 'files', files: [sourceFile] },
            externalCommand: echoCommand,
          },
          {
            type: 'button',
            id: 'custom.pm-test-list',
            label: isWin ? 'PM Test Dir' : 'PM Test Ls',
            enabled: true,
            visible: true,
            source: { kind: 'files', files: [sourceFile] },
            cwd: workDir,
            externalCommand: listCommand,
          },
          {
            type: 'button',
            id: 'custom.pm-test-cat',
            label: isWin ? 'PM Test Type' : 'PM Test Cat',
            enabled: true,
            visible: true,
            source: { kind: 'files', files: [sourceFile] },
            cwd: workDir,
            externalCommand: catCommand,
          },
          {
            type: 'button',
            id: 'custom.pm-test-shell',
            label: 'PM Test Shell',
            enabled: true,
            visible: true,
            source: { kind: 'files', files: [sourceFile] },
            cwd: workDir,
            externalCommand: {
              mode: 'shell',
              shellLine,
            },
          },
          {
            type: 'button',
            id: 'custom.pm-test-disabled',
            label: 'PM Test Disabled',
            enabled: false,
            visible: true,
            externalCommand: echoCommand,
          },
          {
            type: 'button',
            id: 'custom.pm-test-hidden',
            label: 'PM Test Hidden',
            enabled: true,
            visible: false,
            externalCommand: echoCommand,
          },
          {
            type: 'button',
            id: 'custom.pm-test-missing-exe',
            label: 'PM Test Missing Executable',
            enabled: true,
            visible: true,
            externalCommand: {
              command: 'pm-image-cli-definitely-missing-executable-for-test',
              args: [],
              mode: 'argv',
            },
          },
          {
            type: 'button',
            id: 'custom.pm-test-exit7',
            label: 'PM Test Exit 7',
            enabled: true,
            visible: true,
            externalCommand: {
              command: nodeExe,
              args: ['-e', 'process.exit(7)'],
              mode: 'argv',
            },
          },
          {
            type: 'button',
            id: 'custom.pm-test-argv-edges',
            label: 'PM Test Argv Edges',
            enabled: true,
            visible: true,
            externalCommand: {
              command: nodeExe,
              args: [
                '-e',
                argvEchoScript,
                spacedPath,
                'embedded "quotes" and spaces',
              ],
              mode: 'argv',
            },
          },
          {
            type: 'button',
            id: 'custom.pm-test-redirection',
            label: 'PM Test Shell Redirection',
            enabled: true,
            visible: true,
            cwd: workDir,
            externalCommand: {
              mode: 'shell',
              shellLine: redirectionLine,
            },
          },
          {
            type: 'button',
            id: 'custom.pm-test-pipe',
            label: 'PM Test Shell Pipe',
            enabled: true,
            visible: true,
            externalCommand: {
              cwd: workDir,
              mode: 'shell',
              shellLine: pipeLine,
            },
          },
          {
            type: 'button',
            id: 'custom.pm-test-item-cwd',
            label: 'PM Test Item Cwd',
            enabled: true,
            visible: true,
            cwd: workDir,
            externalCommand: {
              command: nodeExe,
              args: ['-e', cwdScript],
              mode: 'argv',
            },
          },
          {
            type: 'button',
            id: 'custom.pm-test-ext-cwd',
            label: 'PM Test External Cwd',
            enabled: true,
            visible: true,
            externalCommand: {
              command: nodeExe,
              args: ['-e', cwdScript],
              cwd: workDir,
              mode: 'argv',
            },
          },
          {
            type: 'button',
            id: 'custom.pm-test-llm-agent-dry',
            label: 'PM Test LLM Agent Dry',
            enabled: true,
            visible: true,
            cliCommand: 'llm agent',
            args: [
              '--dry-run',
              '--single-turn',
              '--no-tools',
              '--markdown',
              'plain',
              '--color',
              'never',
              '--log',
              llmLogPath,
              '--prompt',
              '1+1',
              '--include',
              llmIncludePath,
            ],
          },
          {
            type: 'button',
            id: 'custom.pm-test-llm-agent-extra',
            label: 'PM Test LLM Agent Extra',
            enabled: true,
            visible: true,
            cliCommand: 'llm agent',
            args: [
              '--dry-run',
              '--single-turn',
              '--no-tools',
              '--markdown',
              'plain',
              '--color',
              'never',
              '--log',
              llmExtraLogPath,
              '--prompt',
              'preset prompt from commands',
            ],
          },
          {
            type: 'button',
            id: 'custom.pm-test-llm-agent-real',
            label: 'PM Test LLM Agent Real',
            enabled: true,
            visible: true,
            cliCommand: 'llm agent',
            args: [
              '--json',
              '--single-turn',
              '--no-tools',
              '--markdown',
              'plain',
              '--color',
              'never',
              '--timeout-ms',
              '60000',
              '--log',
              llmRealLogPath,
              '--prompt',
              'What is 1+1? Answer exactly with the single character 2. No punctuation. No markdown.',
            ],
          },
          {
            type: 'button',
            id: 'custom.pm-test-llm-agent-glob-list',
            label: 'PM Test LLM Agent Glob Listing',
            enabled: true,
            visible: true,
            cwd: workDir,
            cliCommand: 'llm agent',
            args: [
              '--json',
              '--single-turn',
              '--log',
              llmGlobLogPath,
              '--timeout-ms',
              '120000',
              '--max-iter',
              '8',
              '--include',
              imageGlobDir,
              '--prompt',
              'Use file_glob to find image files under the images directory. Create a markdown directory listing named dir-images.md in the current working directory. Include one bullet per image filename and include image-alpha.jpg and image-beta.jpg. Use write_file to create the markdown file. Do not transform or edit images.',
            ],
          },
          {
            type: 'button',
            id: 'custom.pm-test-transform-illustration',
            label: 'PM Test Illustration Transform',
            enabled: true,
            visible: true,
            icon: 'photo',
            tint: '#EC4899',
            cliCommand: 'transform',
            source: {
              kind: 'files',
              files: [transformInputPath],
            },
            args: [
              '--src',
              transformInputPath,
              '--prompt',
              'Create a clean editorial illustration based on this image.',
              '--json',
            ],
            output: {},
          },
        ],
      }],
    },
  };
}

export async function suiteCommands(rep, assertFn, exe, commandsPath) {
  console.log('\n── Custom commands: listing + CLI execution ──\n');
  rep.beginSuite('Custom commands: listing + CLI execution');

  const workDir = dirname(commandsPath);
  const sourceFile = join(workDir, 'hello.txt');
  const spacedPath = join(workDir, 'path with spaces.txt');
  const llmIncludePath = join(workDir, 'llm include.txt');
  const llmExtraIncludePath = join(workDir, 'llm extra include.txt');
  const transformInputPath = join(workDir, 'tests', 'assets', 'commands', 'illustration.jpg');
  const imageGlobDir = join(workDir, 'images');
  const imageAlphaPath = join(imageGlobDir, 'image-alpha.jpg');
  const imageBetaPath = join(imageGlobDir, 'image-beta.jpg');
  const dirImagesPath = join(workDir, 'dir-images.md');

  try {
    mkdirSync(workDir, { recursive: true });
    writeFileSync(sourceFile, 'hello_from_custom_commands\n', 'utf8');
    writeFileSync(spacedPath, 'spaced_path_payload\n', 'utf8');
    writeFileSync(llmIncludePath, 'llm_include_payload\n', 'utf8');
    writeFileSync(llmExtraIncludePath, 'llm_extra_include_payload\n', 'utf8');
    mkdirSync(dirname(transformInputPath), { recursive: true });
    await writeTransformFixture(transformInputPath);
    mkdirSync(imageGlobDir, { recursive: true });
    await writeTransformFixture(imageAlphaPath);
    await writeTransformFixture(imageBetaPath);
    writeFileSync(commandsPath, `${JSON.stringify(testCommandDocument(workDir, sourceFile), null, 2)}\n`, 'utf8');

    {
      const missingPath = join(workDir, 'missing-commands.json');
      const r = runCli(exe, missingPath, ['custom.pm-test-argv']);
      rep.step('missing --commands file', r.ms, `exit=${r.status}`);
      assertFn(r.status !== 0, 'commands: missing --commands file fails custom invocation');
    }

    {
      const malformedPath = join(workDir, 'malformed-commands.json');
      writeFileSync(malformedPath, '{ "ribbon": ', 'utf8');
      const r = runCli(exe, malformedPath, ['custom.pm-test-argv']);
      rep.step('invalid commands JSON', r.ms, `exit=${r.status}`);
      assertFn(r.status !== 0, 'commands: invalid JSON fails custom invocation');
    }

    {
      const r = runCli(exe, commandsPath, ['commands', '--json']);
      rep.step('commands --json', r.ms, `exit=${r.status}`);
      assertFn(r.status === 0, 'commands: --json exits 0');
      const parsed = JSON.parse(r.stdout);
      const argv = parsed.customCommands?.find((row) => row.id === 'custom.pm-test-argv');
      const list = parsed.customCommands?.find((row) => row.id === 'custom.pm-test-list');
      const cat = parsed.customCommands?.find((row) => row.id === 'custom.pm-test-cat');
      const shell = parsed.customCommands?.find((row) => row.id === 'custom.pm-test-shell');
      assertFn(argv?.action === 'external', `commands: argv action external (got ${argv?.action})`);
      assertFn(list?.action === 'external', `commands: list action external (got ${list?.action})`);
      assertFn(cat?.action === 'external', `commands: cat action external (got ${cat?.action})`);
      assertFn(shell?.action === 'external:shell', `commands: shell action external:shell (got ${shell?.action})`);
      assertFn(
        !parsed.customCommands?.some((row) => row.id === 'custom.pm-test-disabled'),
        'commands: disabled custom command is omitted from --json',
      );
      assertFn(
        !parsed.customCommands?.some((row) => row.id === 'custom.pm-test-hidden'),
        'commands: hidden custom command is omitted from --json',
      );
      const llmAgent = parsed.customCommands?.find((row) => row.id === 'custom.pm-test-llm-agent-dry');
      assertFn(llmAgent?.action === 'cli:llm agent', `commands: llm agent custom action cli:llm agent (got ${llmAgent?.action})`);
    }

    {
      const r = runCli(exe, commandsPath, ['llm', 'agent', '--help', '--json']);
      rep.step('llm agent --help --json', r.ms, `exit=${r.status}`);
      assertFn(r.status === 0, 'commands: llm agent help JSON exits 0');
      const parsed = JSON.parse(r.stdout);
      assertFn(parsed.name === 'agent', `commands: llm agent help name is agent (got ${parsed.name})`);
      const optionNames = new Set((parsed.options || []).flatMap((opt) => opt.names || []));
      for (const name of ['--prompt', '--dry-run', '--json', '--include', '--log']) {
        assertFn(optionNames.has(name), `commands: llm agent help includes ${name}`);
      }
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-argv', 'TAIL']);
      rep.step('custom.pm-test-argv', r.ms, r.stdout.trim());
      assertFn(r.status === 0, 'commands: argv custom exits 0');
      assertFn(r.stdout.includes('PMTEST_ARGV:hello:'), 'commands: argv resolves SRC_NAME');
      assertFn(r.stdout.includes(':env-ok:'), 'commands: argv resolves ENV value');
      assertFn(r.stdout.includes('${ENV:PM_COMMANDS_TEST_MISSING}'), 'commands: argv preserves missing ENV variable');
      assertFn(r.stdout.includes('TAIL'), 'commands: argv appends invocation extra args');
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-list']);
      rep.step('custom.pm-test-list', r.ms, r.stdout.trim());
      assertFn(r.status === 0, 'commands: list custom exits 0');
      assertFn(r.stdout.includes('hello.txt'), 'commands: list prints temp file');
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-cat']);
      rep.step('custom.pm-test-cat', r.ms, r.stdout.trim());
      assertFn(r.status === 0, 'commands: cat custom exits 0');
      assertFn(r.stdout.includes('hello_from_custom_commands'), 'commands: cat/type prints file content');
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-shell']);
      rep.step('custom.pm-test-shell', r.ms, r.stdout.trim());
      assertFn(r.status === 0, 'commands: shell custom exits 0');
      assertFn(r.stdout.includes('hello_from_custom_commands'), 'commands: shell can read file content');
      assertFn(r.stdout.includes('PMTEST_SHELL:hello:'), 'commands: shell resolves SRC_NAME');
      assertFn(r.stdout.includes(':env-ok'), 'commands: shell resolves ENV value');
    }

    {
      const disabled = runCli(exe, commandsPath, ['custom.pm-test-disabled']);
      rep.step('custom.pm-test-disabled', disabled.ms, `exit=${disabled.status}`);
      assertFn(disabled.status !== 0, 'commands: disabled custom command is not executable');

      const hidden = runCli(exe, commandsPath, ['custom.pm-test-hidden']);
      rep.step('custom.pm-test-hidden', hidden.ms, `exit=${hidden.status}`);
      assertFn(hidden.status !== 0, 'commands: hidden custom command is not executable');
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-missing-exe']);
      rep.step('custom.pm-test-missing-exe', r.ms, `exit=${r.status}`);
      assertFn(r.status !== 0, 'commands: missing executable exits non-zero');
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-exit7']);
      rep.step('custom.pm-test-exit7', r.ms, `exit=${r.status}`);
      if (process.platform === 'win32') {
        assertFn(r.status === 7, `commands: child process exit propagates status 7 (got ${r.status})`);
      } else {
        assertFn(r.status !== 0, `commands: child process non-zero exit propagates (got ${r.status})`);
      }
    }

    {
      const quotedArg = 'extra "quote" arg with spaces';
      const r = runCli(exe, commandsPath, ['custom.pm-test-argv-edges', quotedArg]);
      rep.step('custom.pm-test-argv-edges', r.ms, r.stdout.trim());
      assertFn(r.status === 0, 'commands: argv edge command exits 0');
      const argv = JSON.parse(r.stdout.trim());
      assertFn(argv.includes(spacedPath), 'commands: argv preserves path with spaces literally');
      assertFn(argv.includes('embedded "quotes" and spaces'), 'commands: argv preserves embedded quotes and spaces');
      assertFn(argv.includes(quotedArg), 'commands: argv preserves extra arg quotes and spaces');
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-redirection']);
      rep.step('custom.pm-test-redirection', r.ms, r.stdout.trim());
      assertFn(r.status === 0, 'commands: shell redirection command exits 0');
      assertFn(r.stdout.includes('first') && r.stdout.includes('second'), 'commands: shell > and >> output can be read back');
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-pipe']);
      rep.step('custom.pm-test-pipe', r.ms, r.stdout.trim());
      assertFn(r.status === 0, 'commands: shell pipe command exits 0');
      assertFn(r.stdout.includes('beta'), 'commands: shell pipe emits filtered output');
    }

    {
      const itemCwd = runCli(exe, commandsPath, ['custom.pm-test-item-cwd']);
      rep.step('custom.pm-test-item-cwd', itemCwd.ms, itemCwd.stdout.trim());
      assertFn(itemCwd.status === 0, 'commands: top-level cwd command exits 0');
      assertFn(resolve(itemCwd.stdout.trim()) === resolve(workDir), 'commands: top-level cwd sets process cwd');

      const extCwd = runCli(exe, commandsPath, ['custom.pm-test-ext-cwd']);
      rep.step('custom.pm-test-ext-cwd', extCwd.ms, extCwd.stdout.trim());
      assertFn(extCwd.status === 0, 'commands: externalCommand.cwd command exits 0');
      assertFn(resolve(extCwd.stdout.trim()) === resolve(workDir), 'commands: externalCommand.cwd sets process cwd');
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-llm-agent-dry']);
      rep.step('custom.pm-test-llm-agent-dry', r.ms, `exit=${r.status}`);
      assertFn(r.status === 0, 'commands: llm agent dry custom exits 0');
      assertFn(r.stdout.includes('# llm agent') && r.stdout.toLowerCase().includes('dry run'), 'commands: llm agent dry output identifies dry run');
      assertFn(r.stdout.includes('User prompt'), 'commands: llm agent dry output labels user prompt');
      assertFn(r.stdout.includes('1+1'), 'commands: llm agent dry output prints simple prompt');
      assertFn(
        r.stdout.includes(llmIncludePath) || r.stdout.includes('| included files | 1 |'),
        'commands: llm agent dry output includes forwarded path context',
      );
    }

    {
      const r = runCli(exe, commandsPath, ['custom.pm-test-llm-agent-extra', '--include', llmExtraIncludePath]);
      rep.step('custom.pm-test-llm-agent-extra', r.ms, `exit=${r.status}`);
      assertFn(r.status === 0, 'commands: llm agent preset plus extra args exits 0');
      assertFn(r.stdout.includes('preset prompt from commands'), 'commands: llm agent preset prompt is preserved');
      assertFn(
        r.stdout.includes(llmExtraIncludePath) || r.stdout.includes('| included files | 1 |'),
        'commands: llm agent extra --include is forwarded after preset args',
      );
    }

    if (RUN_REAL_AI) {
      const r = runCli(exe, commandsPath, ['custom.pm-test-llm-agent-real'], { timeout: 120_000 });
      rep.step('custom.pm-test-llm-agent-real', r.ms, `exit=${r.status}`);
      assertFn(r.status === 0, `commands: real llm agent 1+1 exits 0 (stderr: ${r.stderr.trim()})`);
      let parsed = null;
      try {
        parsed = JSON.parse(r.stdout);
      } catch {
        assertFn(false, `commands: real llm agent stdout is JSON (got ${r.stdout.slice(0, 200)})`);
      }
      const finalText = String(parsed?.final_text ?? '').trim();
      assertFn(parsed?.ok === true, `commands: real llm agent JSON ok is true (got ${parsed?.ok})`);
      assertFn(finalText === '2', `commands: real llm agent clean 1+1 result is exactly 2 (got ${JSON.stringify(finalText)})`);

      const transform = runCli(exe, commandsPath, ['custom.pm-test-transform-illustration'], { timeout: 180_000 });
      rep.step('custom.pm-test-transform-illustration', transform.ms, `exit=${transform.status}`);
      assertFn(transform.status === 0, `commands: real transform custom exits 0 (stderr: ${transform.stderr.trim()})`);
      let transformJson = null;
      try {
        transformJson = JSON.parse(transform.stdout);
      } catch {
        assertFn(false, `commands: real transform stdout is JSON (got ${transform.stdout.slice(0, 200)})`);
      }
      const outputPath = transformJson?.outputs?.[0]?.path;
      assertFn(transformJson?.ok === true, `commands: real transform JSON ok is true (got ${transformJson?.ok})`);
      assertFn(typeof outputPath === 'string' && outputPath.length > 0, `commands: real transform captures outputs[0].path (got ${JSON.stringify(outputPath)})`);
      assertFn(outputPath && existsSync(outputPath), `commands: real transform output path exists (${outputPath})`);

      const globList = runCli(exe, commandsPath, ['custom.pm-test-llm-agent-glob-list'], { timeout: 180_000 });
      rep.step('custom.pm-test-llm-agent-glob-list', globList.ms, `exit=${globList.status}`);
      assertFn(globList.status === 0, `commands: real llm agent glob listing exits 0 (stderr: ${globList.stderr.trim()})`);
      let globJson = null;
      try {
        globJson = JSON.parse(globList.stdout);
      } catch {
        assertFn(false, `commands: real llm agent glob listing stdout is JSON (got ${globList.stdout.slice(0, 200)})`);
      }
      assertFn(globJson?.ok === true, `commands: real llm agent glob listing JSON ok is true (got ${globJson?.ok})`);
      assertFn(existsSync(dirImagesPath), `commands: real llm agent creates dir-images.md (${dirImagesPath})`);
      if (existsSync(dirImagesPath)) {
        const dirImages = readFileSync(dirImagesPath, 'utf8');
        assertFn(dirImages.includes('image-alpha.jpg'), 'commands: dir-images.md includes image-alpha.jpg');
        assertFn(dirImages.includes('image-beta.jpg'), 'commands: dir-images.md includes image-beta.jpg');
      }
    } else {
      rep.note('Skipped real AI command smokes. Run with --real-ai, --real-llm, PM_COMMANDS_TEST_AI_REAL=1, or PM_COMMANDS_TEST_LLM_REAL=1 to execute real llm/transform commands.');
    }
  } finally {
    rmSync(workDir, { recursive: true, force: true });
  }

  rep.endSuite();
}

const isMain = process.argv[1] &&
  fileURLToPath(import.meta.url).replace(/\\/g, '/') ===
  process.argv[1].replace(/\\/g, '/');

if (isMain || process.argv.includes('--commands-only')) {
  const wallStart = performance.now();
  const exe = cliExePath();
  const tmp = mkdtempSync(join(tmpdir(), 'pm-commands-test-'));
  const commandsPath = join(tmp, 'commands.json');
  const statsLocal = createAssert();
  const rep = createTestReport();

  console.log('pm-image custom command integration tests');
  console.log(`  binary: ${exe}`);
  console.log(`  commands: ${commandsPath}`);

  await suiteCommands(rep, statsLocal.assert, exe, commandsPath);

  const wallMs = performance.now() - wallStart;
  rep.finalize(statsLocal, wallMs, exe, commandsPath);
  console.log(`\nDone (commands). Passed: ${statsLocal.passed}  Failed: ${statsLocal.failed}\n`);

  mkdirSync(dirname(TEST_REPORT_PATH), { recursive: true });
  writeFileSync(TEST_REPORT_PATH, renderMarkdownReport(rep));
  console.log(`Test report written: ${TEST_REPORT_PATH}`);

  if (isAccumulateMode()) {
    appendMarkdownChapter(
      TEST_REPORT_ALL_PATH,
      getChapterTitle('media:commands'),
      renderMarkdownReport(rep),
      getNpmScriptLabel(),
    );
  }

  process.exit(statsLocal.failed > 0 ? 1 : 0);
}
