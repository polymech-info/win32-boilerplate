#!/usr/bin/env node
// Interactive Paint tic-tac-toe vs the LLM agent.
//
//   npm run play:paint:tictactoe            # 4x4 (default), you (X) vs agent (O)
//   npm run play:paint:tictactoe -- --3x3   # 3x3 variant
//   npm run play:paint:tictactoe -- --moves "10,7,9"   # script your moves instead of typing
//
// Real-time session: the agent uses --session-id <stable-id> so its memory
// persists across turns via %APPDATA%\PolyMech\pm-image\sessions\<id>.json.
//
// The test suite `test:app-use:agent:paint:tictactoe:human-vs-agent` does
// exactly the same thing, with the harness picking your moves for you.

import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync, readFileSync, statSync } from 'node:fs';
import { createInterface } from 'node:readline';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = resolve(__dirname, '..', '..');
const CLI_EXE = join(PACKAGE_ROOT, 'dist', 'win-x64', 'pm-image-cli.exe');

if (!existsSync(CLI_EXE)) {
  console.error(`pm-image-cli.exe not found at ${CLI_EXE}.`);
  console.error('Run `npm run build:cpp` first.');
  process.exit(1);
}

// ── Args ────────────────────────────────────────────────────────────────────
const argv = process.argv.slice(2);
const ROWS = argv.includes('--3x3') ? 3 : 4;
const COLS = ROWS;
const WIN_K = ROWS;
const N_CELLS = ROWS * COLS;
const movesArgIdx = argv.indexOf('--moves');
const scriptedMoves = movesArgIdx >= 0 && argv[movesArgIdx + 1]
  ? argv[movesArgIdx + 1].split(',').map((s) => parseInt(s.trim(), 10))
  : null;
const sessionId = `play-paint-${ROWS}x${ROWS}-${Date.now().toString(36)}`;

console.log(`\n  ${ROWS}x${ROWS} tic-tac-toe vs agent O — session ${sessionId}`);
console.log('  You play X; the agent plays O. Type a cell number (1..' + N_CELLS + ') and Enter.');
console.log('  Type "q" to quit, "b" to see the current board, "shot" to take a screenshot.\n');

// ── Helpers ─────────────────────────────────────────────────────────────────
function runCli(args, timeout = 30_000, env = process.env) {
  const r = spawnSync(CLI_EXE, args, { cwd: PACKAGE_ROOT, encoding: 'utf8', timeout, env });
  return { status: r.status ?? -1, stdout: r.stdout || '', stderr: r.stderr || '' };
}

function runCliStream(args, label, timeout = 180_000, env = process.env) {
  return new Promise((resolve) => {
    const c = spawn(CLI_EXE, args, { cwd: PACKAGE_ROOT, env, stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', err = '';
    const timer = setTimeout(() => { try { c.kill(); } catch {} }, timeout);
    c.stdout.on('data', (d) => { const t = d.toString('utf8'); out += t; });
    c.stderr.on('data', (d) => { const t = d.toString('utf8'); err += t; });
    c.on('close', (code) => {
      clearTimeout(timer);
      resolve({ status: code ?? -1, stdout: out, stderr: err });
    });
    c.on('error', (e) => {
      clearTimeout(timer);
      resolve({ status: -1, stdout: out, stderr: String(e), });
    });
  });
}

function parseJson(s) { try { return JSON.parse(s); } catch { return null; } }

function findPaintWindow(inspectDoc) {
  const wins = Array.isArray(inspectDoc?.windows) ? inspectDoc.windows : [];
  return wins.find((w) => /(^|[^a-z])paint([^a-z]|$)/i.test(String(w?.title || ''))) || wins[0] || null;
}

function paintCanvasRect(rect) {
  return {
    x: rect.x + 16,
    y: rect.y + 150,
    w: rect.w - 32,
    h: rect.h - 210,
  };
}

function cellCenters(canvas, rows, cols) {
  const cw = canvas.w / cols, ch = canvas.h / rows;
  const out = [];
  for (let r = 0; r < rows; ++r) for (let c = 0; c < cols; ++c) {
    out.push({ x: Math.round(canvas.x + c * cw + cw / 2), y: Math.round(canvas.y + r * ch + ch / 2) });
  }
  return out;
}

function winningLines(rows, cols, k) {
  const idx = (r, c) => r * cols + c;
  const lines = [];
  for (let r = 0; r < rows; ++r)
    for (let c = 0; c + k <= cols; ++c)
      lines.push(Array.from({ length: k }, (_, i) => idx(r, c + i)));
  for (let c = 0; c < cols; ++c)
    for (let r = 0; r + k <= rows; ++r)
      lines.push(Array.from({ length: k }, (_, i) => idx(r + i, c)));
  for (let r = 0; r + k <= rows; ++r)
    for (let c = 0; c + k <= cols; ++c) {
      lines.push(Array.from({ length: k }, (_, i) => idx(r + i, c + i)));
      lines.push(Array.from({ length: k }, (_, i) => idx(r + i, c + k - 1 - i)));
    }
  return lines;
}

function detectWinner(cells, lines) {
  for (const ln of lines) {
    const v0 = cells[ln[0]];
    if (v0 === '.' || !v0) continue;
    if (ln.every((i) => cells[i] === v0)) return v0;
  }
  return cells.every((c) => c !== '.') ? 'draw' : 'ongoing';
}

function renderBoard(cells, rows, cols) {
  const out = [];
  for (let r = 0; r < rows; ++r) {
    const line = [];
    for (let c = 0; c < cols; ++c) {
      const i = r * cols + c;
      line.push(String(i + 1).padStart(2, ' ') + '[' + cells[i] + ']');
    }
    out.push('  ' + line.join('  '));
  }
  return out.join('\n');
}

function paintExePath() {
  for (const p of [
    'C:\\Windows\\System32\\mspaint.exe',
    'C:\\Windows\\SysNative\\mspaint.exe',
  ]) if (existsSync(p)) return p;
  return null;
}

function ask(rl, prompt) {
  return new Promise((r) => rl.question(prompt, r));
}

// ── Open Paint + draw grid ─────────────────────────────────────────────────
const mspaint = paintExePath();
if (!mspaint) { console.error('mspaint.exe not found'); process.exit(2); }

console.log('  Opening Paint...');
const open = runCli([
  'assistant', 'app-use', 'open-app',
  '--exe', mspaint, '--wait-ms', '6000',
  '--x', '100', '--y', '60', '--width', '1080', '--height', '880',
  '--json',
]);
const openDoc = parseJson(open.stdout);
if (open.status !== 0 || !openDoc?.ok) {
  console.error('failed to open Paint:', open.stderr || open.stdout);
  process.exit(3);
}
await new Promise((r) => setTimeout(r, 1200));

const inspect = runCli([
  'assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json',
], 15_000);
const inspectDoc = parseJson(inspect.stdout);
const paintWin = findPaintWindow(inspectDoc);
const winRect = paintWin?.rect;
if (!winRect?.w || !winRect?.h) {
  console.error('could not locate Paint window');
  console.error('inspect dump:', JSON.stringify(inspectDoc).slice(0, 400));
  process.exit(4);
}
const canvas = paintCanvasRect(winRect);
const centers = cellCenters(canvas, ROWS, COLS);
const lines = winningLines(ROWS, COLS, WIN_K);

const tmpDir = join(PACKAGE_ROOT, 'tmp', 'play-paint');
mkdirSync(tmpDir, { recursive: true });

const padX = Math.max(20, Math.round(canvas.w * 0.05));
const padY = Math.max(20, Math.round(canvas.h * 0.05));
const gridSteps = [{ action: 'activate', title: paintWin?.title || 'Paint', delayMs: 500 }];
for (let i = 1; i < COLS; ++i) {
  gridSteps.push({
    action: 'drag',
    x: canvas.x + Math.round((i * canvas.w) / COLS),
    y: canvas.y + padY,
    dx: 0, dy: canvas.h - 2 * padY,
    steps: 28, durationMs: 500, delayMs: 320,
  });
}
for (let i = 1; i < ROWS; ++i) {
  gridSteps.push({
    action: 'drag',
    x: canvas.x + padX,
    y: canvas.y + Math.round((i * canvas.h) / ROWS),
    dx: canvas.w - 2 * padX, dy: 0,
    steps: 28, durationMs: 500, delayMs: 320,
  });
}
const gridFile = join(tmpDir, 'grid.batch.json');
writeFileSync(gridFile, JSON.stringify({ steps: gridSteps }, null, 2), 'utf8');
console.log('  Drawing grid...');
const grid = runCli([
  'assistant', 'app-use', 'batch', '--file', gridFile, '--json', '--default-delay-ms', '60',
], 90_000);
const gridDoc = parseJson(grid.stdout);
if (grid.status !== 0 || !gridDoc?.ok) {
  console.error('grid draw failed:', grid.stderr || grid.stdout);
  process.exit(5);
}

// ── Game loop ──────────────────────────────────────────────────────────────
const HS = ROWS === 4 ? 20 : 26;
const cells = Array(N_CELLS).fill('.');

function xMarkBatch(c) {
  return {
    steps: [
      { action: 'activate', title: 'Paint', delayMs: 350 },
      { action: 'drag', x: c.x - HS, y: c.y - HS, dx: 2 * HS, dy: 2 * HS, steps: 16, durationMs: 240, delayMs: 200 },
      { action: 'drag', x: c.x - HS, y: c.y + HS, dx: 2 * HS, dy: -2 * HS, steps: 16, durationMs: 240, delayMs: 200 },
    ],
  };
}

function drawXAtCell(cellIdx) {
  const f = join(tmpDir, `human-${cellIdx}.batch.json`);
  writeFileSync(f, JSON.stringify(xMarkBatch(centers[cellIdx]), null, 2), 'utf8');
  return runCli(['assistant', 'app-use', 'batch', '--file', f, '--json', '--default-delay-ms', '60']);
}

function shotPath(turn) { return join(tmpDir, `turn-${String(turn).padStart(2, '0')}.jpg`); }

function takeScreenshot(path) {
  return runCli([
    'assistant', 'app-inspect', 'screenshot', '--title', 'Paint',
    '--output', path, '--json',
  ], 15_000);
}

function readAgentResult(logPath) {
  if (!existsSync(logPath)) return null;
  try { return JSON.parse(readFileSync(logPath, 'utf8')).result || null; } catch { return null; }
}

function parseAgentMove(text, mark) {
  const m = String(text || '').toUpperCase()
    .match(new RegExp(`PLAYED\\s+${mark}\\s+AT\\s+CELL\\s+(\\d+)`));
  return m ? parseInt(m[1], 10) : -1;
}

const winningLinesForPrompt = lines.map((ln) => ln.map((i) => i + 1).join('-')).join(', ');

async function agentTurn(turnNo) {
  const empties = cells.map((c, i) => c === '.' ? i + 1 : null).filter((v) => v !== null);
  const centersLine = centers.map((p, i) => `${i + 1}=(${p.x},${p.y})`).join(', ');
  const boardText = renderBoard(cells, ROWS, COLS).replace(/\n/g, '\\n');
  const prompt = [
    `You are AGENT O playing ${ROWS}x${ROWS} tic-tac-toe against a HUMAN (X) on a real MS Paint canvas, with PERSISTENT MEMORY across turns.`,
    `Board is ${ROWS} rows by ${COLS} cols. Win = ${WIN_K} in a row. Cells numbered 1..${N_CELLS} left-to-right, top-to-bottom.`,
    'Current board (. = empty):',
    `  ${boardText}`,
    `Empty cells: ${JSON.stringify(empties)}.`,
    `Cell-center screen coordinates (Paint canvas, absolute px): ${centersLine}.`,
    'Draw your O as a "+" (two perpendicular strokes crossing at the cell center).',
    'Pick ONE empty cell N. Place your O there using ONE app_batch tool call (substitute REAL integers for cx,cy):',
    '  { "default_delay_ms": 60, "steps": [',
    '    { "action": "activate", "title": "Paint", "delayMs": 300 },',
    `    { "action": "drag", "x": cx-${HS}, "y": cy,    "dx": ${2 * HS}, "dy": 0,  "steps": 12, "duration_ms": 180, "delayMs": 200 },`,
    `    { "action": "drag", "x": cx,    "y": cy-${HS}, "dx": 0,  "dy": ${2 * HS}, "steps": 12, "duration_ms": 180, "delayMs": 200 }`,
    '  ] }',
    `Strategy priorities: (1) WIN if any cell completes 4-in-a-row for O; (2) BLOCK if X has 3 in any line and one empty; (3) create double threats; (4) extend longest open O line; (5) prefer central cells early. Winning lines: ${winningLinesForPrompt}.`,
    'STRICT OUTPUT FORMAT: text reply must be EXACTLY one line: "PLAYED O AT CELL N". Put reasoning into memory_write, not into the text channel.',
    'PERSISTENT MEMORY: call memory_read first to recall your plan; memory_write({"state":{...}}) at the end to update it (e.g. {"plan":"...","threats":[...]}).',
    'Use ONLY: app_batch (mandatory), plus optionally memory_read / memory_write. Do NOT call run, write_file, schedule_*, app_inspect_*, app_screenshot, app_open, app_close. Do NOT close Paint.',
  ].join(' ');

  // Per-turn agent log lives in tmp/ — never pollute the user's repo root.
  const logPath = join(tmpDir, `agent-turn-${String(turnNo).padStart(2, '0')}.json`);

  const env = { ...process.env };
  // We deliberately do NOT override PM_IMAGE_SESSION_DIR — let it persist to
  // the real roaming/sessions/ directory so multi-day matches are possible.

  console.log(`  ...agent thinking (session ${sessionId}, turn ${turnNo})`);
  const r = await runCliStream([
    '--cwd', '.', '--log-level', 'info',
    'llm', 'agent',
    '--prompt', prompt,
    '--single-turn',
    '--session-id', sessionId,
    '--max-iter', '6',
    '--log', logPath,
  ], `agent O turn ${turnNo}`, 180_000, env);

  const doc = readAgentResult(logPath);
  const replyText = String(doc?.final_text || '');
  const chosen = parseAgentMove(replyText, 'O');
  const cost = Number(doc?.llm_usage?.cost ?? 0);
  const rounds = Number(doc?.iterations ?? 0);
  console.log(`  agent reply: ${replyText.trim()} | rounds=${rounds} cost=$${cost.toFixed(4)} cli_exit=${r.status}`);
  return { chosen, cost, rounds };
}

// ── Input source: stdin (interactive) or --moves list (scripted) ───────────
const rl = scriptedMoves
  ? null
  : createInterface({ input: process.stdin, output: process.stdout });

let turn = 0;
let scriptedIdx = 0;
let totalAgentCost = 0;

while (true) {
  const w = detectWinner(cells, lines);
  if (w !== 'ongoing') {
    console.log(`\n  Game over — winner: ${w}`);
    console.log(renderBoard(cells, ROWS, COLS));
    break;
  }
  if (turn >= N_CELLS) break;

  const xCount = cells.filter((c) => c === 'X').length;
  const oCount = cells.filter((c) => c === 'O').length;
  const mark = (xCount === oCount) ? 'X' : 'O';
  turn++;

  if (mark === 'X') {
    console.log('\n' + renderBoard(cells, ROWS, COLS));
    let cellNum = -1;

    if (scriptedMoves) {
      if (scriptedIdx >= scriptedMoves.length) {
        console.log('\n  Out of scripted moves. Stopping.');
        break;
      }
      cellNum = scriptedMoves[scriptedIdx++];
      console.log(`  [scripted] you play X at cell ${cellNum}`);
    } else {
      const ans = (await ask(rl, `\n  your move (X) cell 1..${N_CELLS} ('q'=quit, 'b'=board, 'shot'=screenshot)? `)).trim();
      if (ans === 'q' || ans === 'quit') { console.log('  bye!'); break; }
      if (ans === 'b' || ans === 'board') { turn--; continue; }
      if (ans === 'shot') {
        const sp = shotPath(turn);
        const s = takeScreenshot(sp);
        console.log(`  saved ${sp} (exit=${s.status})`);
        turn--;
        continue;
      }
      cellNum = parseInt(ans, 10);
    }

    if (!Number.isFinite(cellNum) || cellNum < 1 || cellNum > N_CELLS || cells[cellNum - 1] !== '.') {
      console.log(`  invalid: cell must be empty and in 1..${N_CELLS}`);
      turn--;
      continue;
    }
    const draw = drawXAtCell(cellNum - 1);
    const drawDoc = parseJson(draw.stdout);
    if (draw.status !== 0 || !drawDoc?.ok) {
      console.log(`  draw failed: ${draw.stderr || draw.stdout}`);
      turn--;
      continue;
    }
    cells[cellNum - 1] = 'X';
    await new Promise((r) => setTimeout(r, 250));
    continue;
  }

  // ── Agent O turn ──
  const { chosen, cost } = await agentTurn(turn);
  totalAgentCost += cost;
  if (chosen < 1 || chosen > N_CELLS || cells[chosen - 1] !== '.') {
    console.log(`  agent forfeited (chose=${chosen}, board=${cells.join('')})`);
    break;
  }
  cells[chosen - 1] = 'O';
  await new Promise((r) => setTimeout(r, 250));
}

// Final screenshot.
const finalShot = join(PACKAGE_ROOT, 'tests', `play-final-${sessionId}.jpg`);
mkdirSync(dirname(finalShot), { recursive: true });
takeScreenshot(finalShot);
console.log(`\n  Final board: ${cells.join('')}`);
console.log(`  Final screenshot: ${finalShot}`);
console.log(`  Total agent cost: $${totalAgentCost.toFixed(4)}`);
console.log(`  Session memory:  %APPDATA%/PolyMech/pm-image/sessions/${sessionId}.json`);

if (rl) rl.close();
process.exit(0);
