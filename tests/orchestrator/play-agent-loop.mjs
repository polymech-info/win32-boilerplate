#!/usr/bin/env node
// play-agent-loop.mjs
//
// Drive a sustained "real-time" game session in which the agent polls the
// canvas every N seconds, decides whether it is its turn, and plays/speaks
// accordingly. Rules of the game are described in natural language; board
// dimensions come from CLI args (rows/cols/win-k).
//
//   npm run play:agent:loop -- \
//       --rules "3x3 tic-tac-toe in MS Paint. AGENT plays O, HUMAN plays X. X plays first. Win = 3 in a row." \
//       --interval 20
//
// Architecture
// ============
// The driver owns the *deterministic* Win32 work; the agent owns the
// *strategic* reasoning and the per-turn pixel drawing.
//
//   Setup (NO LLM call):
//     - locate mspaint.exe
//     - open Paint at a known (x,y,w,h) with `app-use open-app`
//     - inspect the Paint window, derive canvas rect
//     - compute rows*cols cell-center pixel coords
//     - draw the (rows-1) horizontal + (cols-1) vertical grid lines via
//       `app-use batch` (harness draws, deterministic, cheap)
//     - take a baseline screenshot
//
//   Polling loop, every --interval seconds:
//     - screenshot the Paint window
//     - if the byte size delta vs the previous screenshot is below a noise
//       threshold, SKIP the agent (saves $) and just wait
//     - otherwise invoke the agent with --include <shot>, --session-id <id>,
//       and a meta-prompt that injects:
//           * the NLP rules + grid dims
//           * cell-center pixel coords
//           * the agent's prior board state (from session memory)
//           * a STRICT reply protocol: PLAYED / WAITING / GAMEOVER
//       The agent must use image_understand to read the current board state.
//
// Why we removed the NLP setup phase: the agent was hallucinating Paint's
// layout, clicking the Select tool in the ribbon and dragging across the
// toolbar instead of the canvas. Deterministic Win32 work belongs in the
// driver; the agent's value-add is strategy, not pixel arithmetic.
//
// Why NOT use the in-process `schedule_every` tool: AgentScheduler is only
// started by ChatWebPanel; the CLI `llm agent` command does not spin up the
// scheduler thread, so `schedule_every` from a CLI invocation queues a task
// that never fires. The 20s cadence here comes from this external Node
// loop; "memory" comes from the on-disk session store (sessions/<id>.json).

import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync, readFileSync, statSync } from 'node:fs';
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

// ── Args ─────────────────────────────────────────────────────────────────────
const argv = process.argv.slice(2);
function flagValue(name, def = '') {
  const i = argv.indexOf(name);
  return i >= 0 && argv[i + 1] !== undefined ? argv[i + 1] : def;
}
function hasFlag(name) { return argv.includes(name); }

const RULES = flagValue('--rules',
  'NxM tic-tac-toe in MS Paint. AGENT plays O. HUMAN plays X. X plays first. ' +
  'Win = K in a row horizontally, vertically, or diagonally.');
const ROWS         = Math.max(2, parseInt(flagValue('--rows', '4'), 10));
const COLS         = Math.max(2, parseInt(flagValue('--cols', '4'), 10));
const WIN_K        = Math.max(2, parseInt(flagValue('--win-k', String(Math.min(ROWS, COLS))), 10));
const MARK_AGENT   = flagValue('--mark-agent', 'O');
const MARK_HUMAN   = flagValue('--mark-human', 'X');
const FIRST_PLAYER = flagValue('--first', MARK_HUMAN);  // who plays first ('X' or 'O')
const WINDOW_TITLE = flagValue('--window', 'Paint');
const INTERVAL_S   = Math.max(2, parseInt(flagValue('--interval', '5'), 10));
const MAX_TICKS    = Math.max(1, parseInt(flagValue('--max-ticks', '60'), 10));
const SESSION_ID   = flagValue('--session-id', `play-loop-${Date.now().toString(36)}`);
const NO_SETUP     = hasFlag('--no-setup');
const NO_SPEAK     = hasFlag('--no-speak'); // force off even if a TTS provider is configured
// 0..1 fraction of the Paint canvas the playable grid occupies (centered).
// Default 0.6 leaves comfortable margin so you can scribble outside the grid
// and watch the agent (correctly) ignore those marks.
const BOARD_SCALE  = Math.max(0.2, Math.min(1.0, parseFloat(flagValue('--board-scale', '0.6'))));
// JPEG re-encoding noise on a static Paint canvas is typically +/- 0-700 B.
// A single new short stroke is +400-1500 B; a whole X/O is +1500-5000 B.
// We use TWO thresholds:
//   per-tick : skip the agent if THIS tick's |delta| < per-tick gate
//   cumulative: sum |delta| across consecutive skipped ticks; fire if the
//               running total crosses (2 * per-tick) -- catches slow drift
//               from a sequence of small strokes that each fell below the gate.
const NOISE_BYTES  = Math.max(0, parseInt(flagValue('--noise-bytes', '500'), 10));
const CUM_BYTES    = Math.max(NOISE_BYTES + 1,
                              parseInt(flagValue('--cum-bytes', String(NOISE_BYTES * 2)), 10));

console.log(`\n  play-agent-loop`);
console.log(`    rules        : ${RULES}`);
console.log(`    board        : ${ROWS}x${COLS}  win=${WIN_K}-in-a-row  first=${FIRST_PLAYER}`);
console.log(`    marks        : agent=${MARK_AGENT}  human=${MARK_HUMAN}`);
console.log(`    window       : ${WINDOW_TITLE}`);
console.log(`    interval     : ${INTERVAL_S}s   max ticks: ${MAX_TICKS}`);
console.log(`    session id   : ${SESSION_ID}`);
console.log(`    noise gate   : per-tick=${NOISE_BYTES} B   cumulative=${CUM_BYTES} B`);
console.log(`    board scale  : ${BOARD_SCALE.toFixed(2)} of the Paint canvas, centered (1.0 = full)`);
console.log(`    setup phase  : ${NO_SETUP ? 'off (assumes Paint already open with a grid drawn)' : 'on (driver opens + draws grid)'}\n`);

const tmpDir = join(PACKAGE_ROOT, 'tmp', 'play-agent-loop');
mkdirSync(tmpDir, { recursive: true });
console.log(`  per-tick logs   : ${tmpDir}\\tick-NNN-agent.json   (full prompt, tool calls + envelopes, final text)`);
console.log(`  per-tick shots  : ${tmpDir}\\tick-NNN.jpg           (exactly what the agent saw)`);
console.log(`  session memory  : %APPDATA%\\PolyMech\\pm-image\\sessions\\${SESSION_ID}.json\n`);

// ── CLI helpers ─────────────────────────────────────────────────────────────
function runCli(args, timeout = 30_000, env = process.env) {
  const r = spawnSync(CLI_EXE, args, { cwd: PACKAGE_ROOT, encoding: 'utf8', timeout, env });
  return { status: r.status ?? -1, stdout: r.stdout || '', stderr: r.stderr || '' };
}
// Stream stderr to console as it arrives so you can watch a long agent
// tick happen in real time. Lines are line-prefixed so they don't pollute
// the play-loop's own status output. Pass streamStderr=false to silence.
function runCliStream(args, label, timeout = 240_000, streamStderr = true) {
  return new Promise((resolve_) => {
    const t0 = Date.now();
    console.log(`  [${label}] starting...`);
    const c = spawn(CLI_EXE, args, { cwd: PACKAGE_ROOT, env: process.env, stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '', err = '';
    let stderrTail = '';
    const timer = setTimeout(() => { try { c.kill(); } catch {} }, timeout);
    c.stdout.on('data', (d) => { out += d.toString('utf8'); });
    c.stderr.on('data', (d) => {
      const s = d.toString('utf8');
      err += s;
      if (streamStderr) {
        stderrTail += s;
        let nl;
        while ((nl = stderrTail.indexOf('\n')) >= 0) {
          const line = stderrTail.slice(0, nl).replace(/\r$/, '');
          stderrTail = stderrTail.slice(nl + 1);
          if (line.length) console.log(`    [${label}] ${line}`);
        }
      }
    });
    c.on('close', (code) => {
      clearTimeout(timer);
      if (streamStderr && stderrTail.length) console.log(`    [${label}] ${stderrTail.replace(/\r?\n$/, '')}`);
      const dt = Date.now() - t0;
      console.log(`  [${label}] exit=${code} in ${dt}ms`);
      resolve_({ status: code ?? -1, stdout: out, stderr: err, ms: dt });
    });
    c.on('error', (e) => {
      clearTimeout(timer);
      resolve_({ status: -1, stdout: out, stderr: String(e), ms: Date.now() - t0 });
    });
  });
}
function parseJson(s) { try { return JSON.parse(s); } catch { return null; } }
// Strip non-ASCII chars from prompt text -- Windows argv (CP1252 -> UTF-8) mangles
// em-dashes, smart quotes, ellipsis, etc., which crashes the agent JSON encoder.
function asciiOnly(s) {
  return String(s)
    .replace(/[\u2010-\u2015]/g, '-')
    .replace(/[\u2018\u2019\u201A\u201B]/g, "'")
    .replace(/[\u201C\u201D\u201E\u201F]/g, '"')
    .replace(/\u2026/g, '...')
    // eslint-disable-next-line no-control-regex
    .replace(/[^\x09\x0A\x0D\x20-\x7E]/g, '?');
}
function readAgentResult(logPath) {
  if (!existsSync(logPath)) return null;
  try { return JSON.parse(readFileSync(logPath, 'utf8')).result || null; } catch { return null; }
}
function readAgentToolCalls(logPath) {
  if (!existsSync(logPath)) return [];
  try { return (JSON.parse(readFileSync(logPath, 'utf8')).events || [])
    .filter((e) => e.kind === 'tool_call').map((e) => e.tool); } catch { return []; }
}

// ── Paint-specific helpers (copied from tests/orchestrator/test-app-use.mjs)
function paintExePath() {
  const env = process.env.MSPAINT_EXE || process.env.PAINT_EXE;
  if (env && env.trim() && existsSync(env.trim())) return resolve(env.trim());
  const candidates = [
    join(process.env.SYSTEMROOT || 'C:\\Windows', 'System32', 'mspaint.exe'),
    join(process.env.SYSTEMROOT || 'C:\\Windows', 'mspaint.exe'),
  ];
  for (const c of candidates) if (existsSync(c)) return c;
  const where = spawnSync('where.exe', ['mspaint.exe'], { encoding: 'utf8', timeout: 5000 });
  if ((where.status ?? -1) === 0) {
    const first = String(where.stdout || '').split(/\r?\n/).map((l) => l.trim()).find((l) => l && existsSync(l));
    if (first) return first;
  }
  return '';
}
function paintCanvasRect(windowRect) {
  // Win11 Paint layout: ~30 px title + ~110 px ribbon, ~50 px status bar at bottom.
  const left = windowRect.x + 16;
  const top = windowRect.y + 150;
  const right = windowRect.x + windowRect.w - 16;
  const bottom = windowRect.y + windowRect.h - 60;
  return { x: left, y: top, w: Math.max(0, right - left), h: Math.max(0, bottom - top) };
}
function findPaintWindow(inspectDoc) {
  const wins = Array.isArray(inspectDoc?.windows) ? inspectDoc.windows : [];
  return wins.find((w) => /(^|[^a-z])paint([^a-z]|$)/i.test(String(w?.title || ''))) || wins[0] || null;
}
function cellCentersGrid(canvas, rows, cols) {
  const cw = canvas.w / cols, ch = canvas.h / rows;
  const out = [];
  for (let r = 0; r < rows; ++r)
    for (let c = 0; c < cols; ++c)
      out.push({
        x: Math.round(canvas.x + c * cw + cw / 2),
        y: Math.round(canvas.y + r * ch + ch / 2),
      });
  return out;
}

/// Shrink a rect to `scale` of its size, kept centered in the original.
function scaleRectCentered(rect, scale) {
  const w = Math.round(rect.w * scale);
  const h = Math.round(rect.h * scale);
  const x = Math.round(rect.x + (rect.w - w) / 2);
  const y = Math.round(rect.y + (rect.h - h) / 2);
  return { x, y, w, h };
}

// ── Setup phase (HARNESS-OWNED) ──────────────────────────────────────────────
let paintTitle = WINDOW_TITLE;
let cellCenters = []; // [{x,y}], 1-indexed via centers[i-1]
let boardRect = null; // scaled board rect inside the Paint canvas (for the prompt)

if (!NO_SETUP) {
  const mspaint = paintExePath();
  if (!mspaint) {
    console.error('  mspaint.exe not found; pass --no-setup if Paint is already open.');
    process.exit(1);
  }

  console.log(`  [setup] opening Paint at (120,80) size 960x760 ...`);
  const open = runCli([
    'assistant', 'app-use', 'open-app',
    '--exe', mspaint,
    '--wait-ms', '6000',
    '--x', '120', '--y', '80',
    '--width', '960', '--height', '760',
    '--json',
  ], 30_000);
  if (open.status !== 0) {
    console.error(`  [setup] open-app failed: ${open.stderr.slice(0, 300)}`);
    process.exit(1);
  }
  await new Promise((r) => setTimeout(r, 1200));

  const dump = runCli(['assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json'], 15_000);
  const dumpDoc = parseJson(dump.stdout);
  const paintWin = findPaintWindow(dumpDoc);
  const winRect = paintWin?.rect;
  if (!winRect || winRect.w < 200 || winRect.h < 200) {
    console.error(`  [setup] could not locate Paint window (rect=${JSON.stringify(winRect)})`);
    process.exit(1);
  }
  paintTitle = paintWin?.title || 'Paint';

  const canvas = paintCanvasRect(winRect);
  boardRect = scaleRectCentered(canvas, BOARD_SCALE);
  cellCenters = cellCentersGrid(boardRect, ROWS, COLS);
  console.log(`  [setup] Paint window: title=${JSON.stringify(paintTitle)}  rect=${JSON.stringify(winRect)}`);
  console.log(`  [setup] canvas rect (full): ${JSON.stringify(canvas)}`);
  console.log(`  [setup] board  rect (${BOARD_SCALE.toFixed(2)} of canvas, centered): ${JSON.stringify(boardRect)}`);
  console.log(`  [setup] ${ROWS}x${COLS} cell centers: ${cellCenters.slice(0, 4).map((p) => `(${p.x},${p.y})`).join(' ')}${cellCenters.length > 4 ? ' ...' : ''}`);

  // Grid lines are drawn at the EDGES of the board rect plus the interior
  // boundaries -- when the board is scaled down, the grid sits inside the
  // canvas with whitespace around it so the human can scribble outside it.
  const padX = Math.max(10, Math.round(boardRect.w * 0.04));
  const padY = Math.max(10, Math.round(boardRect.h * 0.04));
  const dragSteps = 30, dragDur = 500;
  const steps = [{ action: 'activate', title: paintTitle, delayMs: 400 }];
  // Vertical lines: at cols-1 interior boundaries of the board rect.
  for (let c = 1; c < COLS; ++c) {
    const x = Math.round(boardRect.x + (c * boardRect.w) / COLS);
    steps.push({ action: 'drag', x, y: boardRect.y + padY, dx: 0, dy: boardRect.h - 2 * padY,
                 steps: dragSteps, durationMs: dragDur, delayMs: 350 });
  }
  // Horizontal lines: at rows-1 interior boundaries of the board rect.
  for (let r = 1; r < ROWS; ++r) {
    const y = Math.round(boardRect.y + (r * boardRect.h) / ROWS);
    steps.push({ action: 'drag', x: boardRect.x + padX, y, dx: boardRect.w - 2 * padX, dy: 0,
                 steps: dragSteps, durationMs: dragDur, delayMs: 350 });
  }

  const batchPath = join(tmpDir, 'setup-grid.batch.json');
  writeFileSync(batchPath, `${JSON.stringify({ steps }, null, 2)}\n`, 'utf8');
  const batch = runCli(['assistant', 'app-use', 'batch', '--file', batchPath, '--json', '--default-delay-ms', '60'], 60_000);
  const batchDoc = parseJson(batch.stdout);
  if (batch.status !== 0 || batchDoc?.ok !== true) {
    console.error(`  [setup] grid draw failed: ${batch.stderr.slice(0, 300)}`);
    process.exit(1);
  }
  console.log(`  [setup] drew ${steps.length - 1} grid lines (${ROWS - 1} horiz + ${COLS - 1} vert) in ${batch.ms ?? '?'} ms`);

  await new Promise((r) => setTimeout(r, 600));
} else {
  console.log(`  [setup] skipped -- assuming Paint is already open with a ${ROWS}x${COLS} grid.`);
  // Try to locate Paint anyway so the polling loop has a title to screenshot.
  const dump = runCli(['assistant', 'app-inspect', 'dump', '--title', 'Paint', '--limit', '40', '--json'], 15_000);
  const dumpDoc = parseJson(dump.stdout);
  const paintWin = findPaintWindow(dumpDoc);
  if (paintWin) {
    paintTitle = paintWin.title || 'Paint';
    const canvas = paintCanvasRect(paintWin.rect);
    boardRect = scaleRectCentered(canvas, BOARD_SCALE);
    cellCenters = cellCentersGrid(boardRect, ROWS, COLS);
    console.log(`  [setup] found Paint: ${paintTitle}  canvas=${JSON.stringify(canvas)}  board=${JSON.stringify(boardRect)}`);
  } else {
    console.warn('  [setup] could not locate Paint window; agent will still try to play.');
  }
}

// ── TTS auto-detection ──────────────────────────────────────────────────────
// "Speak" needs FEATURE_STT compiled in AND a chat tts_provider configured
// (App Settings -> Chat -> Voice & Audio). Auto-detect; allow --no-speak to
// force off if you want a silent run.
let useSpeak = false;
let ttsProvider = '';
if (!NO_SPEAK) {
  const info = runCli(['llm', 'info', '--json'], 10_000);
  const doc = parseJson(info.stdout);
  ttsProvider = doc?.chat?.tts_provider
             || doc?.chat_settings?.tts_provider
             || doc?.chat?.tts?.provider
             || doc?.tts?.provider
             || '';
  if (ttsProvider) {
    useSpeak = true;
    console.log(`  TTS (speak)     : on  -- provider=${ttsProvider}  (the agent will quip board state + announce game over)`);
  } else {
    console.log('  TTS (speak)     : off -- no chat_settings.tts_provider in `llm info --json`');
  }
} else {
  console.log('  TTS (speak)     : off -- forced by --no-speak');
}

// ── Polling loop ────────────────────────────────────────────────────────────
const cellCenterText = cellCenters.length
  ? cellCenters.map((p, i) => `${i + 1}=(${p.x},${p.y})`).join(', ')
  : '(none -- could not measure canvas; agent must skip drawing)';
const winLinesText = (() => {
  const idx = (r, c) => r * COLS + c + 1;
  const lines = [];
  for (let r = 0; r < ROWS; ++r)
    for (let c = 0; c + WIN_K <= COLS; ++c)
      lines.push(Array.from({ length: WIN_K }, (_, i) => idx(r, c + i)).join('-'));
  for (let c = 0; c < COLS; ++c)
    for (let r = 0; r + WIN_K <= ROWS; ++r)
      lines.push(Array.from({ length: WIN_K }, (_, i) => idx(r + i, c)).join('-'));
  for (let r = 0; r + WIN_K <= ROWS; ++r)
    for (let c = 0; c + WIN_K <= COLS; ++c) {
      lines.push(Array.from({ length: WIN_K }, (_, i) => idx(r + i, c + i)).join('-'));
      lines.push(Array.from({ length: WIN_K }, (_, i) => idx(r + i, c + WIN_K - 1 - i)).join('-'));
    }
  return lines.join(', ');
})();

// Baseline screenshot so the FIRST tick can compute a meaningful byte delta.
const baselinePath = join(tmpDir, 'baseline.jpg');
runCli(['assistant', 'app-inspect', 'screenshot', '--title', paintTitle, '--output', baselinePath, '--json'], 15_000);
let lastBytes = existsSync(baselinePath) ? statSync(baselinePath).size : 0;
console.log(`  baseline shot: ${lastBytes} B`);

console.log(`\n  Entering poll loop (interval=${INTERVAL_S}s, max-ticks=${MAX_TICKS}). Ctrl+C to stop.\n`);

let tick = 0;
let lastAction = 'idle';
// Sum of |delta| across consecutive *skipped* ticks. Resets when we fire the
// agent. This is the "slow drift" tracker: small strokes that each fall under
// the per-tick gate but add up to a real change get caught on a later tick.
let cumDelta = 0;

while (tick < MAX_TICKS) {
  tick++;
  await new Promise((r) => setTimeout(r, INTERVAL_S * 1000));

  // 1) Screenshot.
  const shotPath = join(tmpDir, `tick-${String(tick).padStart(3, '0')}.jpg`);
  const shot = runCli([
    'assistant', 'app-inspect', 'screenshot',
    '--title', paintTitle,
    '--output', shotPath,
    '--json',
  ], 15_000);
  if (shot.status !== 0 || !existsSync(shotPath)) {
    console.warn(`  tick ${tick}: screenshot failed (exit=${shot.status}, stderr=${shot.stderr.slice(0, 120).trim()}); skipping.`);
    continue;
  }
  const curBytes = statSync(shotPath).size;
  const delta = curBytes - lastBytes;
  const absDelta = Math.abs(delta);
  cumDelta += absDelta;
  console.log(`  tick ${tick}: shot ${curBytes} B   delta ${delta >= 0 ? '+' : ''}${delta} B   cum-since-agent ${cumDelta} B`);

  // 2) Two-stage byte-delta gate.
  //      - this-tick gate (NOISE_BYTES, default 500): a real new stroke
  //        usually pushes a JPEG by >= ~400 B; below that we assume re-encode noise.
  //      - cumulative gate (CUM_BYTES, default 2x per-tick): catches the case
  //        where two small strokes each landed under the per-tick gate but
  //        together represent a real change.
  if (lastBytes > 0 && absDelta < NOISE_BYTES && cumDelta < CUM_BYTES) {
    console.log(`  tick ${tick}: per-tick ${absDelta} B < ${NOISE_BYTES} B  AND  cumulative ${cumDelta} B < ${CUM_BYTES} B  -- no agent call.`);
    lastBytes = curBytes;
    lastAction = 'waiting';
    continue;
  }
  // Fire the agent. Reset the cumulative tracker so the next quiet period
  // starts fresh.
  if (lastBytes > 0 && absDelta < NOISE_BYTES)
    console.log(`  tick ${tick}: per-tick under gate but cumulative ${cumDelta} B >= ${CUM_BYTES} B -- firing agent on drift.`);
  lastBytes = curBytes;
  cumDelta = 0;

  // 3) Agent invocation.
  const prompt = [
    `Game rules (verbatim from the user):`,
    `  ${RULES}`,
    ``,
    `Board: ${ROWS} rows by ${COLS} columns. Win = ${WIN_K} in a row.`,
    `You play ${MARK_AGENT}. The HUMAN plays ${MARK_HUMAN}. ${FIRST_PLAYER} plays first.`,
    `Cells are numbered 1..${ROWS * COLS} left-to-right top-to-bottom.`,
    boardRect
      ? `The PLAYABLE GRID occupies the rectangle (left=${boardRect.x}, top=${boardRect.y}, right=${boardRect.x + boardRect.w}, bottom=${boardRect.y + boardRect.h}) in absolute screen px. The Paint canvas is LARGER than the grid -- any marks the human draws OUTSIDE this rectangle are DOODLES and MUST be ignored for board state, turn parity, and win detection. Only count marks that are clearly inside one of the ${ROWS * COLS} cells.`
      : `The grid fills the Paint canvas.`,
    `Cell-center pixel coordinates inside the grid (absolute screen px):`,
    `  ${cellCenterText}`,
    `Winning lines: ${winLinesText}.`,
    ``,
    `Fresh screenshot of the Paint window: ${shotPath}`,
    ``,
    `Your task on THIS tick:`,
    `  1. Call image_understand on the screenshot. Ask exactly: "There is a ${ROWS}x${COLS} grid drawn inside the Paint canvas${boardRect ? ` (the grid does NOT fill the canvas -- there may be doodles outside it; IGNORE them)` : ''}. For each of the ${ROWS * COLS} numbered cells (1..${ROWS * COLS}, left-to-right top-to-bottom INSIDE the grid only), say whether it contains ${MARK_HUMAN}, ${MARK_AGENT}, or is empty." This is your perception of the current board.`,
    `  2. Call memory_read to recall your last known board state and your last move.`,
    `  3. Compute turn parity from what image_understand reported.`,
    `       Let H = count of ${MARK_HUMAN} marks, A = count of ${MARK_AGENT} marks.`,
    `       ${FIRST_PLAYER} plays first, so the player whose count is LOWER is the next to move (ties go to ${FIRST_PLAYER}).`,
    `       It is YOUR turn IF AND ONLY IF the next-to-move equals ${MARK_AGENT}. Otherwise it is the HUMAN's turn.`,
    `  4. Decide which case you are in (in order):`,
    `       (A) Game is over: someone has ${WIN_K} in a row, or the board is full and nobody won.${useSpeak ? ` BEFORE replying, call speak({"text": "..."}) ONCE with playful announcing the result. Be witty -- gloat if YOU (${MARK_AGENT}) won, be a graceful loser if the HUMAN won, shrug at a draw. Examples: "Three in a row! Better luck next time, human." / "You got me! Rematch?" / "Stalemate. Honourable battle."` : ''} Reply EXACTLY one line: "GAMEOVER: <winner or DRAW>".`,
    `       (B) It is YOUR turn (per the parity rule above). Pick the best empty cell N. Place your ${MARK_AGENT} with ONE app_batch call drawing two perpendicular strokes (a "+" shape) crossing at the cell center for that cell. Use the cell-center coordinates above. After the batch lands, call memory_write({"state": {...}}) with the new board, your last move, and your plan.${useSpeak ? ` AFTER the app_batch lands, call speak({"text": "..."}) ONCE with a SHORT (under 12 words) snarky or amused commentary on the move and the board state. Examples: "Center taken. Classic." / "Blocking that diagonal -- nice try." / "Forking you in two places." Do NOT speak before the draw lands.` : ''} Reply EXACTLY one line: "PLAYED ${MARK_AGENT} AT CELL N".`,
    `       (C) Otherwise it is still the HUMAN's turn -- wait.${useSpeak ? ` If this is the FIRST time you've waited since the human's last move (i.e. they just played), you MAY call speak({"text": "..."}) ONCE with a SHORT (under 10 words) reaction to their move. Examples: "Interesting opening." / "Going for the corner, are we?" / "Bold." Do NOT speak on every wait tick -- only when memory_read shows the board changed since your previous turn.` : ''} Reply EXACTLY one line: "WAITING".`,
    ``,
    `Example app_batch for placing ${MARK_AGENT} at cell N with center (cx,cy):`,
    `  { "default_delay_ms": 60, "steps": [`,
    `      { "action": "activate", "title": "${paintTitle.replace(/"/g, '\\"')}", "delayMs": 300 },`,
    `      { "action": "drag", "x": cx-20, "y": cy,    "dx": 40, "dy": 0,  "steps": 12, "duration_ms": 180, "delayMs": 200 },`,
    `      { "action": "drag", "x": cx,    "y": cy-20, "dx": 0,  "dy": 40, "steps": 12, "duration_ms": 180, "delayMs": 200 }`,
    `  ] }`,
    ``,
    `Use ONLY: image_understand (mandatory), memory_read, memory_write, app_batch${useSpeak ? ', speak (per the rules above)' : ''}.`,
    `Do NOT call: run, write_file, ${useSpeak ? '' : 'speak, '}schedule_*, app_inspect_*, app_screenshot, app_open, app_close, app_click, app_type, app_hotkey, app_drag.`,
    ``,
    `STRICT OUTPUT FORMAT: your text reply MUST be exactly one line, one of:`,
    `    "GAMEOVER: <result>"`,
    `    "PLAYED ${MARK_AGENT} AT CELL N"`,
    `    "WAITING"`,
    `Do not echo the rules. Do not narrate. All reasoning goes into memory_write.`,
  ].join('\n');

  const tickLogPath = join(tmpDir, `tick-${String(tick).padStart(3, '0')}-agent.json`);
  const r = await runCliStream([
    '--cwd', '.', '--log-level', 'trace',
    'llm', 'agent',
    '--prompt', asciiOnly(prompt),
    '--include', shotPath,
    '--single-turn',
    '--session-id', SESSION_ID,
    '--max-iter', '10',
    '--log', tickLogPath,
  ], `tick ${tick}`, 240_000);

  const doc = readAgentResult(tickLogPath);
  const tools = readAgentToolCalls(tickLogPath);
  const replyText = String(doc?.final_text || '').trim();
  const cost = Number(doc?.llm_usage?.cost ?? 0);
  const rounds = Number(doc?.iterations ?? 0);

  // Lenient parse: agents tend to narrate their reasoning then write the
  // verdict at the end ("...WAITING"), violating the "EXACTLY one line"
  // rule. The strategic content is what matters, not formatting purity, so
  // we just scan the reply for the verdict tokens with priority
  // GAMEOVER > PLAYED > WAITING (game-over short-circuits everything).
  let verdict = 'unknown';
  let gameoverMatch = null;
  let playedMatch = null;
  if ((gameoverMatch = /GAMEOVER\s*:\s*([^\n]+)/i.exec(replyText))) {
    verdict = 'gameover';
  } else if ((playedMatch = /PLAYED\s+([XO])\s+AT\s+CELL\s+(\d+)/i.exec(replyText))) {
    verdict = 'played';
  } else if (/\bWAITING\b/i.test(replyText)) {
    verdict = 'waiting';
  }

  // Full reply printed across lines so you can read the agent's reasoning.
  const replyLines = replyText.split(/\r?\n/);
  console.log(`  tick ${tick}: verdict=${verdict}  rounds=${rounds}  cost=$${cost.toFixed(4)}  tools=${tools.join(',') || '(none)'}`);
  for (const line of replyLines) console.log(`    | ${line}`);

  if (verdict === 'gameover') {
    console.log(`\n  Game over: ${gameoverMatch[1].trim()}`);
    lastAction = 'gameover';
    break;
  } else if (verdict === 'played') {
    lastAction = 'moved';
    console.log(`  tick ${tick}: agent played ${playedMatch[1].toUpperCase()} at cell ${playedMatch[2]}`);
    // Force-refresh lastBytes so the next tick measures from AFTER our draw,
    // not from before.
    await new Promise((r2) => setTimeout(r2, 600));
    const post = runCli(['assistant', 'app-inspect', 'screenshot', '--title', paintTitle, '--output', shotPath, '--json'], 15_000);
    if (post.status === 0 && existsSync(shotPath)) lastBytes = statSync(shotPath).size;
  } else if (verdict === 'waiting') {
    lastAction = 'waiting';
  } else {
    console.warn(`  tick ${tick}: unrecognised reply -- no GAMEOVER/PLAYED/WAITING token found; treating as waiting.`);
    lastAction = 'waiting';
  }
}

console.log(`\n  Loop ended (ticks=${tick}, last action=${lastAction}).`);
console.log(`  Session file:   %APPDATA%\\PolyMech\\pm-image\\sessions\\${SESSION_ID}.json`);
console.log(`  Per-tick logs:  ${tmpDir}\\tick-*.json / tick-*.jpg`);
process.exit(0);
