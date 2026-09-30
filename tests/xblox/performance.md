# XBlox C++ Performance TODOs

Current rough `fps-unthrottled` numbers from `tests/xblox/tests.sh`:

```text
cpp     423862
python  456895.9
rust    6137224.6
```

Latest C++ smoke after cached muParser fallback instances, non-event path elision, native builtin ABI, numeric scalar context store, and compiled non-event core blocks: `546,811,263` counted events in ~6.2s. For the current `fps-unthrottled.xblox` shape, that is roughly `22M` loop iterations/sec (`setVariable` + `if` count about four events per loop).

Raw loop smoke using `tests/xblox/fps-raw-loop.xblox` and matching raw comparison scripts. This benchmark intentionally avoids in-loop expression math; the C++ path uses a no-JSON hot loop specialization for `while(1) { setVariable constant }`.

| Runtime | Script | Elapsed | Events | Iterations/sec | Events/sec |
| --- | --- | ---: | ---: | ---: | ---: |
| C++ XBlox raw | `fps-raw-loop.xblox` | `3.482s` | `4,294,967,296` | `616,725,572` | `1,233,451,144` |
| Node raw | `fps-raw-loop-node.mjs` | `6.000s` | `2,974,126,000` | `247,843,833` | `495,687,667` |
| Rust raw | `fps-raw-loop-rust.rs` | `6.000s` | `1,537,376,000` | `128,114,575` | `256,229,150` |
| Python raw | `fps-raw-loop-python.py` | `6.000s` | `97,404,000` | `8,116,963` | `16,233,925` |

The immediate goal is not to beat raw Rust, but to make the C++ XBlox runner clearly faster than the Python comparison and within a sane distance of the Rust simulation. The Rust gap shows the remaining cost is mostly in the C++ interpreter architecture, not the CPU.

## P0: Measure Before Each Cut

- [ ] Add a bounded C++ benchmark mode for `fps-unthrottled.xblox` so we can run `pm-image-cli ... --duration 6` instead of using Ctrl+C from a wrapper.
- [ ] Emit a compact perf summary for non-JSON CLI runs: elapsed ms, event count, blocks/sec, max-loop-iterations, and whether event collection was disabled.
- [ ] Add a repeatable script that runs C++, Node, Python, and Rust for the same duration and writes one summary block.
- [ ] Keep `--json` correctness tests separate from perf runs; `--json` intentionally pays for full event materialization.
- [ ] Record platform and compiler metadata in benchmark output: OS, CPU, compiler, build type, pointer width, and timer source.
- [ ] Make benchmark scripts cross-platform:
  - Windows PowerShell
  - Linux shell
  - macOS shell
  - CI-friendly no-interrupt bounded mode
- [ ] Avoid Windows-only Ctrl+C wrappers for primary numbers; use duration or iteration limits.

## P1: Kill Remaining Hot-Loop Interpreter Overhead

- [x] Stop building path strings (`path + "/items/" + index`) in non-event perf runs. Use a lightweight path stack or numeric path only when events/errors need it.
- [x] Avoid creating a fresh `BlockRuntime` full of `std::function` wrappers for every block execution. Move callbacks into a stable execution context object and pass references.
- [x] Replace the string-keyed builtin registry lookup in the hot path with direct function-pointer dispatch.
- [ ] Cache common JSON field lookups (`kind`, `items`, `condition`, `name`, `expression`, `value`) in a compiled block representation.
- [x] Cache common JSON field lookups (`kind`, `items`, `condition`, `name`, `expression`, `value`) in a compiled block representation for non-event core blocks.
- [ ] Avoid repeated `json_string()` allocations for loop conditions and setVariable expressions.

## P1: Compile Blocks Before Running

- [ ] Introduce an internal compiled AST for XBlox:
  - block kind enum
  - child spans/references
  - pre-parsed fields
  - precompiled expressions
  - optional source path for diagnostics/events
- [ ] Keep the public input/output as JSON, but run against compiled blocks.
- [x] Compile once in `run_blocks_file`, then execute the compiled form for supported non-event core blocks.
- [ ] Keep a slow JSON interpreter path only if needed for debugging or compatibility.

## P1: Bytecode Execution Options

- [ ] Add explicit C++ execution modes:
  - `json`: current JSON interpreter, compatibility/debugging path.
  - `compiled`: compiled AST with typed fields and expression objects.
  - `bytecode`: linear instruction stream for hot execution.
- [ ] Add a CLI option like `--engine json|compiled|bytecode`, defaulting conservatively to `json` until parity tests pass.
- [ ] Define a minimal bytecode instruction set:
  - `SetConst slot, value`
  - `SetExpr slot, exprIndex`
  - `Jump target`
  - `JumpIfFalse exprIndex, target`
  - `Log level, exprIndex`
  - `Command commandIndex`
  - `Break`
  - `Wait ms`
- [ ] Keep source path/debug info as side metadata, not part of hot bytecode execution.
- [ ] Add a bytecode verifier before execution:
  - valid jump targets
  - valid slot indices
  - valid expression indices
  - loop limits/cancellation points present
- [ ] Keep bytecode internal for now; do not expose it as a persisted file format until the schema stabilizes.
- [ ] Add parity tests that run the same `.xblox` document through `json`, `compiled`, and `bytecode` engines and compare outcome/events in `--json` mode.

## P1: Compile Expressions

- [x] Add an expression cache keyed by source string for muParser fallback expressions.
- [ ] Preclassify simple expressions:
  - numeric literal
  - direct variable read
  - `var + literal`
  - `var - literal`
  - `var * literal`
  - `var / literal`
  - binary comparisons
  - `nowMs` arithmetic
- [ ] Store compiled expression objects in the compiled AST instead of parsing strings on each evaluation.
- [ ] Reuse `mu::Parser` instances for non-simple expressions rather than constructing parser + variable map every evaluation.
- [ ] Split expression benchmarks into modes:
  - `raw`: no expressions, constant write only.
  - `fast-expr`: simple expression fast paths (`frames + 1`, comparisons, direct reads).
  - `muparser`: force fallback through muParser.
  - `mixed`: realistic script with fast paths, fallback expressions, logs, and context writes.
- [ ] Add matching Node/Python/Rust expression-engine variants so comparisons are honest:
  - hardcoded fast path
  - restricted AST/parser evaluator
  - dependency-backed evaluator if we accept a toolchain dependency
- [ ] Add a C++ option to disable expression fast paths for measurement, e.g. `--expression-fast-paths=false`.
- [ ] Track expression cache hit/miss counts in perf summaries.
- [ ] Track time spent in expression evaluation separately from block dispatch when profiling mode is enabled.

## P1: Fast Context Store

- [x] Add a numeric context store next to the JSON context for hot scalar variables.
- [x] Resolve simple variable names to integer slots during block compilation.
- [ ] Keep JSON context as the compatibility/source-of-truth layer for object values, command payloads, and final reporting.
- [ ] Fast-path `setVariable`/`getVariable` for scalar slot writes and reads.
- [ ] Only sync numeric slots back to JSON when full event payloads, command context, or final diagnostics require it.

## P2: Event Pipeline

- [ ] Split event handling into modes:
  - `countOnly`
  - `errorsAndLogs`
  - `full`
- [ ] In `countOnly`, avoid constructing `ExecutionEvent` objects for successful hot blocks.
- [ ] In `errorsAndLogs`, keep logs/errors/cancel but skip `running` and successful `setVariable`/`if` events.
- [ ] In `full`, preserve current `--json` behavior for tests and WebView diagnostics.
- [ ] Add a CLI flag like `--event-mode full|logs|count` with defaults:
  - `--json` => `full`
  - normal CLI => `logs`
  - perf CLI => `count`

## P2: Loop Specialization

- [ ] Recognize the common `while(condition) { items... }` shape and execute it without recursive `execute_block_list` calls per iteration.
- [ ] Specialize `while 1`/`while true` so it only checks cancellation and loop cap.
- [ ] Specialize one-item and two-item loop bodies to avoid array iteration overhead.
- [ ] Add a loop-level cancellation polling interval so ultra-hot loops do not call the cancel predicate for every block.

## P2: Logging Cost Control

- [ ] Keep log blocks real, but ensure logger formatting is only paid when the log block actually fires.
- [ ] Consider a perf mode where log blocks still print every 5 seconds but avoid extra event payload construction.
- [ ] Verify logger level checks avoid work when the requested log level is disabled.

## P3: Native Block ABI

- [ ] Define a lightweight native block execution interface independent of JSON.
- [ ] Move builtins from `std::function` callbacks to direct virtual-free function calls over an execution context.
- [ ] Keep JSON conversion at the edges: load/save/report, not per executed block.
- [ ] Consider generated or table-driven field access for builtins once the schema stabilizes.

## P3: Cross-Platform Benchmark Matrix

- [ ] Run and record the benchmark matrix on:
  - Windows MSVC Release
  - Windows clang-cl Release, if available
  - Linux clang Release
  - Linux gcc Release
  - macOS clang Release
- [ ] Keep compiler flags visible in the report (`/O2`, `/GL`, `-O3`, LTO on/off).
- [ ] Separate process startup cost from runtime cost for short benchmarks.
- [ ] Prefer 5-10 second duration runs for Node/Python/Rust and iteration-limited direct runs for ultra-fast C++ bytecode/raw loops.
- [ ] Make the raw/fast-expr/muparser/mixed benchmark result table append-only with date, machine, and git commit.

## Guardrails

- [ ] Do not break `npm run test:xblox:expressions`.
- [ ] Do not break `npm run test:xblox:context`.
- [ ] Preserve full event payloads for `--json`.
- [ ] Preserve graceful Ctrl+C exit code `130`.
- [ ] Preserve muParser compatibility for expressions outside the simple fast paths.
- [ ] Preserve identical observable behavior across `json`, `compiled`, and `bytecode` engines before changing defaults.

## Recent Wins To Preserve

- [x] Non-JSON CLI runs count events without storing the full event vector.
- [x] Simple hot expressions bypass muParser.
- [x] Simple context names avoid dotted-path splitting.
- [x] Builtin control-flow blocks avoid copying JSON child arrays in hot loops.
- [x] Non-collected OK events avoid building heavy JSON payloads.
- [x] muParser fallback expressions reuse cached parser instances and variable storage.
- [x] Non-event CLI runs skip hot-path diagnostic path construction.
- [x] Raw constant `while(1) { setVariable value }` benchmark avoids JSON in the hot loop.
- [x] Builtin handlers use a native function-pointer bridge instead of per-block `std::function` callbacks.
- [x] Builtin dispatch avoids the unordered-map registry lookup in the hot path.
- [x] Simple scalar variables are mirrored into a numeric context store for fast reads.
- [x] Non-event core blocks (`setVariable`, `if`, `while`, `log`) can run through a compiled block representation with scalar slots.
