# PM-Image XBlox (LLM brief)

XBlox is the block-tree command flow surface. Use `pm-image-cli xblox info --json` for machine-readable block definitions, grouped palette metadata, default block JSON, param schemas, registered CLI commands, and custom command payloads.

## Document shape

`{ "version": 1, "context": {}, "roots": [...] }`

## Blocks

### Context

#### `getVariable` — Get Variable

Read a value from root scope.

Params:
- `name` (string, required)
- `target` (string)

Default block: `{"kind":"getVariable","name":"value","target":"PREVIOUS"}`

#### `log` — Log

Log a message, expression, variable, or the whole scope.

Params:
- `level` (string, default `"info"`)
- `message` (string)

Default block: `{"kind":"log","level":"info","message":"PREVIOUS"}`

#### `setVariable` — Set Variable

Write a value into root scope.

Params:
- `name` (string, required)
- `value` (json)
- `expression` (string)

Default block: `{"kind":"setVariable","name":"value","value":null}`

### Data

#### `Parse` — Parse JSON

Parse PREVIOUS or named input with a jq-style selector.

Params:
- `parser` (string, default `"jq"`)
- `filter` (string, default `"."`)
- `input` (string, default `"PREVIOUS"`)
- `storeAs` (string)

Default block: `{"filter":".","kind":"Parse","parser":"jq","storeAs":"parsed"}`

### Flow

#### `break` — Break

Break out of the current loop.

Default block: `{"kind":"break"}`

#### `for` — For

Run child blocks over a numeric range.

Default block: `{"comparator":"<","final":"3","initial":"0","items":[],"kind":"for","modifier":"+1"}`

#### `if` — If

Run consequent or alternate blocks based on a condition.

Default block: `{"alternate":[],"condition":"true","consequent":[],"kind":"if"}`

#### `switch` — Switch

Run the first matching case.

Default block: `{"items":[],"kind":"switch","variable":"mode"}`

#### `wait` — Wait

Sleep for a fixed number of milliseconds.

Params:
- `ms` (integer, default `500`)

Default block: `{"kind":"wait","ms":500}`

#### `while` — While

Run child blocks while a condition is true.

Default block: `{"condition":"false","items":[],"kind":"while","loopLimit":10}`

### Network

#### `fetch` — Fetch

Fetch a URL and store the raw response in PREVIOUS.

Params:
- `url` (string, required)
- `method` (string, default `"GET"`)
- `timeoutMs` (integer, default `30000`)
- `connectTimeoutMs` (integer, default `10000`)
- `followRedirects` (boolean, default `true`)
- `retries` (integer, default `2`)
- `storeAs` (string)

Default block: `{"followRedirects":true,"kind":"fetch","storeAs":"response","timeoutMs":30000,"url":"https://"}`

### Shell

#### `shell` — Shell

Run a shell command through the native RunTool.

Params:
- `mode` (string, default `"shell"`)
- `shell` (string, default `"auto"`)
- `command` (string, required)
- `cwd` (string)
- `args` (string[])
- `timeoutMs` (integer, default `30000`)
- `background` (boolean, default `false`)
- `log` (boolean, default `false`)
- `stdout` (logger::level, default `"info"`)
- `stderr` (logger::level, default `"error"`)
- `storeAs` (string)

Default block: `{"command":"echo hello","kind":"shell","log":false,"mode":"shell","shell":"auto","stderr":"error","stdout":"info","storeAs":"stdout","timeoutMs":30000}`

## Supported Commands

- Registered CLI commands: `resize`, `transform`, `create`, `settings`, `provider`, `llm`, `service`, `audio`, `video`, `xblox`, `commands`, `daemon`, `assistant`
- Custom commands from commands.json: 17
- `custom.capture` — Capture (metadata, group Pictures)
- `custom.capture.screenshot` — Take screenshot (app:browse, group Pictures)
- `custom.command-mpi5b1hw-8bdc2` — browse (ribbon:appSettings, group Pictures)
- `custom.mic-start` — Mic Capture Start (cli:audio, group Pictures)
- `custom.mic-stop` — Mic Capture Stop (cli:audio, group Pictures)
- `custom.mic-status` — Mic Capture Status (cli:audio, group Pictures)
- `custom.video-start` — Video Capture Start (cli:video, group Pictures)
- `custom.video-stop` — Video Capture Stop (cli:video, group Pictures)
- `custom.video-status` — Video Capture Status (cli:video, group Pictures)
- `custom.tools` — Tools (metadata, group Pictures)
- `custom.tools.theme` — Toggle theme (ribbon:theme, group Pictures)
- `custom.tools.resetLayout` — Reset layout (ribbon:resetLayout, group Pictures)
- `custom.command-mpcmqepq-ee6f3` — AI:Images List (cli:llm, group Files)
- `custom.command-mpduqkaw-b621b` — cat (external, group Files)
- `custom.command-mpch9gdx-44982` — AI:Illustration (cli:transform, group Files)
- `custom.command-mpejo01t-98bf0` — ls (external, group Files)
- `custom.command-mpi5stmy-8e9cb` — x1 (cli:daemon, group Files)

