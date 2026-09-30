# PM-Image commands (LLM brief)

## Built-in CLI commands

### `resize`

Resize / transform an image (libvips, Sharp-like options)

- Allows extra positional arguments.

**Options:**

- `input` (TEXT, optional, positional) — Input path, glob (*, ?, **), or http(s):// URL
- `output` (TEXT, optional, positional) — Output file/dir, or omit when there is exactly one input → write under cwd (sanitized name)
- `--src` (TEXT, optional) — Input (repeat for multiple); use with --dst; Explorer passes several files
- `--dst` (TEXT, optional) — Same as positional output; directory if multiple inputs
- `--max-width` (INT, optional) — Target / max width (0 = no limit)
- `--max-height` (INT, optional) — Target / max height (0 = no limit)
- `--format` (TEXT, optional) — Output format (default: from extension)
- `--fit` (TEXT, optional, default `inside`) — inside|cover|contain|fill|outside — see Sharp resize.fit
- `--position` (TEXT, optional, default `centre`) — For cover: centre|attention|entropy|…
- `--kernel` (TEXT, optional, default `lanczos3`) — nearest|cubic|mitchell|lanczos2|lanczos3
- `--quality` (INT, optional, default `85`) — JPEG/WebP/AVIF quality 1–100
- `--png-compression` (INT, optional, default `6`) — PNG DEFLATE 0–9
- `--background` (TEXT, optional) — Letterbox colour #rrggbb (contain)
- `--rotate` (INT, optional, default `0`) — Rotate 0|90|180|270 after EXIF autorotate
- `--flip` (optional, flag) — Vertical flip
- `--flop` (optional, flag) — Horizontal flop
- `--no-autorotate` (optional, flag) — Disable EXIF orientation
- `--no-strip` (optional, flag) — Keep metadata on output
- `--allow-enlargement` (optional, flag) — Allow upscaling (inside/contain/outside)
- `--no-cache` (optional, flag) — Disable output cache (default: cache on)
- `--cache-dir` (TEXT, optional) — Cache root (default: <cwd>/cache/images)
- `--url-timeout` (INT, optional, default `5`) — HTTP(S) fetch timeout (seconds, 0 = libcurl default)
- `--url-max-redirects` (INT, optional, default `20`) — Max redirects when fetching URL inputs

### `transform`

AI image editing (Gemini / Google)

- Allows extra positional arguments.

**Options:**

- `input` (TEXT, optional, positional) — Input image, or use --src for a batch (Explorer multi-select uses --src)
- `--src` (TEXT, optional) — Input path (repeat for multiple files; one job queue / one window with --job-ui on Windows)
- `output` (TEXT, optional, positional) — Output path (omit = auto from input + prompt)
- `--prompt` (TEXT, optional) — Editing prompt (required if --preset-id is not set, unless preset supplies prompt)
- `--preset-id` (TEXT, optional) — Preset id from settings: explorer_presets (op=transform), or chat_web quick action as chat-<id>
- `--provider` (TEXT, optional) — AI provider (google, replicate, pixlwiz); omit = from app Chat image_provider (aborts if unset)
- `--model` (TEXT, optional) — Model id; omit = from app Chat image_model (aborts if unset)
- `--api-key` (TEXT, optional) — API key (optional; default from app provider settings)
- `--aspect-ratio` (TEXT, optional) — Output aspect ratio (1:1,16:9,4:3,...)
- `--image-size` (TEXT, optional) — Output size (512,1K,2K,4K)
- `--reference` (TEXT, optional) — Reference image path (logo / brand sheet / style swatch). Repeatable: -r logo.png -r palette.jpg
- `--json` (optional, flag) — Print machine-readable outputs JSON on stdout.
- `--job-ui` (optional, flag) — List-style job window (pause / cancel) — Windows

### `create`

AI text-to-image (Gemini / Google, no input file)

- Allows extra positional arguments.

**Options:**

- `output` (TEXT, optional, positional) — Output path (omit = create_<slug>.png in cwd)
- `--prompt` (TEXT, required) — Generation prompt
- `--provider` (TEXT, optional) — AI provider (google, replicate, pixlwiz); omit = from app Chat image_provider (aborts if unset)
- `--model` (TEXT, optional) — Model id; omit = from app Chat image_model (aborts if unset)
- `--api-key` (TEXT, optional) — API key (optional; default from app provider settings)
- `--aspect-ratio` (TEXT, optional) — Output aspect ratio (1:1,16:9,4:3,...)
- `--image-size` (TEXT, optional) — Output size (512,1K,2K,4K)
- `--reference` (TEXT, optional) — Reference image path (style / brand). Repeat as needed.
- `--json` (optional, flag) — Print machine-readable outputs JSON on stdout.

### `settings`

Import / export the app settings profile (UTF-8 JSON). macOS / Linux: portable profile (`~/Library/…/settings.json`, `~/.pm-image/settings.json`, or `--config-dir`; fallback `./config/settings.json` if HOME unset). Windows: live store path (see `settings path`); `export --encrypted` writes PME1. Top-level --settings=… (Windows) is a one-off read path for the process, not `settings import`.

- Allows extra positional arguments.

**Subcommands:**

- `path` — Print the canonical on-disk settings.json path for this OS (no file I/O) and exit.
- `import` — Replace the live profile store with the given UTF-8 JSON file (full document replace). Windows: accepts PME1 or JSON (same as in-app). Other OS: JSON object only. All OSes: --archive restores a profile ZIP.
- `export` — Write the current profile settings to a UTF-8 JSON file (default: settings.json in cwd). Windows: `--encrypted` writes PME1 instead of JSON. All OSes: `--archive` writes a profile ZIP.

#### `settings path`

Print the canonical on-disk settings.json path for this OS (no file I/O) and exit.

- Allows extra positional arguments.

#### `settings import`

Replace the live profile store with the given UTF-8 JSON file (full document replace). Windows: accepts PME1 or JSON (same as in-app). Other OS: JSON object only. All OSes: --archive restores a profile ZIP.

- Allows extra positional arguments.

**Options:**

- `path` (TEXT, required, positional) — Source file (relative paths are from cwd)
- `--archive` (optional, flag) — Read a profile ZIP exported by `settings export --archive`. Skips web* folders and never imports .settings-key.dat.

#### `settings export`

Write the current profile settings to a UTF-8 JSON file (default: settings.json in cwd). Windows: `--encrypted` writes PME1 instead of JSON. All OSes: `--archive` writes a profile ZIP.

- Allows extra positional arguments.

**Options:**

- `path` (TEXT, optional, default `settings.json`, positional) — Output file (relative paths are from cwd)
- `--encrypted` (optional, flag) — Write PME1 binary (DPAPI-bound key on this profile) instead of UTF-8 JSON.
- `--archive` (optional, flag) — Write a ZIP of the app profile. Skips web* WebView folders; Windows settings.json is portable JSON; .settings-key.dat is omitted.

### `provider`

Provider utilities (model catalog, etc.)

- Allows extra positional arguments.

**Subcommands:**

- `models` — Provider model catalog operations

#### `provider models`

Provider model catalog operations

- Allows extra positional arguments.

**Subcommands:**

- `list` — List provider models and return full JSON payload

#### `provider models list`

List provider models and return full JSON payload

- Allows extra positional arguments.

**Options:**

- `--provider` (TEXT, optional, default `replicate`) — Provider id (replicate|openrouter)
- `--api-key` (TEXT, optional) — API key (Replicate: required. OpenRouter: optional for public /v1/models; from app if set for openrouter)
- `--base-url` (TEXT, optional) — Replicate: catalog URL (default official collection). OpenRouter: API root (default https://openrouter.ai/api/v1)
- `--limit` (INT, optional) — Replicate /v1/models: optional page size (ignored for openrouter)
- `--cursor` (TEXT, optional) — Replicate: optional pagination cursor (ignored for openrouter)
- `--sort-by` (TEXT, optional) — Replicate: optional sort field (ignored for openrouter)
- `--sort-direction` (TEXT, optional) — Replicate: optional sort direction (asc|desc) (ignored for openrouter)

### `llm`

LLM: tools-list / tools-call / info (saved chat + image defaults) / agent (path tools)

- Allows extra positional arguments.

**Subcommands:**

- `info` — Show Chat router/model and image provider/model from app settings, effective defaults for path tools, and which CLI flags override per command (find/transform/meta/duplicates, …)
- `tools-list` — Print the JSON-Schema tool catalog (one entry per pm-image op)
- `tools-call` — Invoke a tool by name with a JSON arguments envelope
- `agent` — Run a single chat-agent turn: LLM picks tools (image_resize / compress / transform / meta / find), runs them on the supplied paths, and writes outputs to disk. Use --no-tools for a plain one-shot text reply (no path tools). Use --disable-tools=a,b to omit specific path tools from the catalog. Provider is router-aware (OpenAI-compatible client).

#### `llm info`

Show Chat router/model and image provider/model from app settings, effective defaults for path tools, and which CLI flags override per command (find/transform/meta/duplicates, …)

- Allows extra positional arguments.

**Subcommands:**

- `providers` — List all LLM providers configured in app settings (name, api_key_set, base_url, default_model).
- `models` — List models for a given provider. pixlwiz / openrouter / replicate: fetches live catalog (disk-cached; replicate 3 d, others 24 h). Other names: shows configured default_model from app settings.
- `tools` — List all built-in path-mode agent tools (name + description). These are the tools available to the chat agent in every session.
- `skills` — List discovered agent skills from roaming and workspace roots with availability/active state.

**Options:**

- `--json` (optional, flag) — Print machine-readable JSON on stdout
- `--markdown` (TEXT:{auto,plain,render}, optional, default `render`) — Human stdout: render (default — always pretty-print), auto (only on TTY), or plain (raw UTF-8). Ignored with --json.
- `--color` (TEXT:{auto,never,always}, optional, default `auto`) — When markdown rendering is used: auto (color on TTY unless NO_COLOR/TERM=dumb), never, or always.
- `--no-mcp-probe` (optional, flag) — Do not run live MCP profile probes (stdio/HTTP handshakes). JSON/text output still lists settings; the `mcp` object notes that the probe was skipped.
- `--mcp-probe` (optional, flag) — Run live MCP profile probes (stdio/HTTP handshakes).

#### `llm info providers`

List all LLM providers configured in app settings (name, api_key_set, base_url, default_model).

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Output JSON array

#### `llm info models`

List models for a given provider. pixlwiz / openrouter / replicate: fetches live catalog (disk-cached; replicate 3 d, others 24 h). Other names: shows configured default_model from app settings.

- Allows extra positional arguments.

**Options:**

- `--provider` (TEXT, required) — Provider to query: pixlwiz | openrouter | replicate | <name from `llm info providers`>
- `--no-cache` (optional, flag) — Bypass the disk cache and force a live HTTP fetch (pixlwiz / openrouter).
- `--json` (optional, flag) — Output JSON

#### `llm info tools`

List all built-in path-mode agent tools (name + description). These are the tools available to the chat agent in every session.

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Output JSON array {name, description}

#### `llm info skills`

List discovered agent skills from roaming and workspace roots with availability/active state.

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Output JSON

#### `llm tools-list`

Print the JSON-Schema tool catalog (one entry per pm-image op)

- Allows extra positional arguments.

**Options:**

- `--path` (optional, flag) — List path-mode chat agent tools (default catalog for llm agent). Omit for in-buffer REST/MCP tools.

#### `llm tools-call`

Invoke a tool by name with a JSON arguments envelope

- Allows extra positional arguments.

**Options:**

- `--name` (TEXT, required) — Tool name (image_resize|image_compress|image_transform|image_create|image_meta|image_find|file_read)
- `--args` (TEXT, optional) — Path to JSON arguments file ('-' or '@-' for stdin; omit = empty {}).
- `--image-file` (TEXT, optional) — Convenience: read this file, base64-encode it, and inject as arguments.image.b64.

#### `llm agent`

Run a single chat-agent turn: LLM picks tools (image_resize / compress / transform / meta / find), runs them on the supplied paths, and writes outputs to disk. Use --no-tools for a plain one-shot text reply (no path tools). Use --disable-tools=a,b to omit specific path tools from the catalog. Provider is router-aware (OpenAI-compatible client).

- Allows extra positional arguments.

**Options:**

- `--prompt` (TEXT, optional) — User prompt (required unless piped via stdin or --mic is used, e.g. 'compress these as MozJPEG quality 70')
- `--include` (TEXT, optional) — One or more file paths to put in the agent's selection context. Repeatable. When omitted, --cwd is used as the folder context.
- `--router` (TEXT, optional) — LLM router (openrouter|openai|deepseek|gemini|ollama|fireworks|xai|huggingface). Default: from Chat Provider Settings in app.
- `--model` (TEXT, optional) — Model id (router-specific, e.g. openai/gpt-4o-mini). Default: from Chat Provider Settings in app.
- `--api-key` (TEXT, optional) — API key (optional; default from app chat / API Keys in settings.json)
- `--base-url` (TEXT, optional) — Override the router's default base URL (OpenAI-compatible endpoints). Default: from app API Providers settings.
- `--timeout-ms` (INT, optional) — HTTP timeout per LLM round (ms). Default: from Chat Provider Settings (or 60000).
- `--max-iter` (INT, optional) — Maximum tool-call iterations before forcing a final response. Default: from Chat Provider Settings (or 8).
- `--no-tools` (optional, flag) — Do not register path tools (no list_images, image_resize, image_compress, …) — one LLM text turn only
- `--multi-turn` (optional, flag) — Enable session memory across turns (default: on).
- `--single-turn` (optional, flag) — Disable session memory and run as one-shot only.
- `--session-id` (TEXT, optional) — Optional session id used when --multi-turn is enabled (allows continuity across CLI invocations).
- `--disable-tools` (TEXT, optional) — Comma- or semicolon-separated path tools to omit: list_images, file_glob, file_read, file_search, image_resize, image_compress, image_transform, image_create, image_meta, image_find, write_file. Ineffective with --no-tools
- `--godmode` (optional, flag) — Bypass ALL filesystem safety guards (sensitive-path deny, extension blocklist, dotfile block, shell command validation). Dangerous — use only when you know what you are doing.
- `--json` (optional, flag) — Emit the full transcript as JSON on stdout (instead of plain-text events)
- `--dry-run` (optional, flag) — Resolve provider + tools + selection; print context as Markdown to stdout; no LLM call.
- `--log` (TEXT, optional, default `agent.json`) — Write a JSON run log (provider, per-event tool calls with full envelopes, transcript) to this file (truncates).
- `--type` (TEXT:{completion,responses,realtime}, optional, default `responses`) — LLM API type: completion (POST /chat/completions) or responses (POST /responses — OpenAI Responses API; supported by OpenAI, OpenRouter, and LiteLLM proxy), or realtime (experimental WebSocket /realtime PoC; text-only; best with router=openai). Default: responses.
- `--streaming` (TEXT:{auto,on,off}, optional, default `auto`) — Streaming mode: auto (default), on, off. Implemented now for --type responses; completion remains non-streaming.
- `--markdown` (TEXT:{auto,plain,render}, optional, default `auto`) — Human stdout: auto (render when stdout is a TTY), plain (raw model UTF-8), or render (always run the terminal markdown pass). Ignored with --json.
- `--color` (TEXT:{auto,never,always}, optional, default `auto`) — When markdown rendering is used: auto (color on TTY unless NO_COLOR/TERM=dumb), never, or always.
- `--mic` (optional, flag) — Use the microphone as prompt input (continuous STT → LLM → TTS loop). Replaces --prompt for user input; --prompt may still be given as context. Requires --stt-api-key or ELEVENLABS_API_KEY. Press Ctrl+C to stop.
- `--stt-api-key` (TEXT, optional) — ElevenLabs API key for real-time STT (and TTS when --voice-id is set). Falls back to ELEVENLABS_API_KEY environment variable.
- `--voice-id` (TEXT, optional) — ElevenLabs voice ID to speak LLM responses aloud (empty = text-only). Browse voices at elevenlabs.io/app/voice-library.
- `--no-tts` (optional, flag) — Mic mode: disable TTS playback entirely (keep listening continuously after each response).
- `--tts-model-id` (TEXT, optional, default `eleven_v3`) — ElevenLabs TTS model used with --voice-id (default: eleven_v3).
- `--input` (TEXT, optional) — Microphone device name (case-insensitive substring; use `audio info` to list). Omit to use the system default input device.
- `--silence-ms` (INT, optional, default `1500`) — Silence duration in ms after which speech is auto-committed to the LLM (0 = disabled; default 1500).

### `service`

Call the configured web service API (see pm-pics src/lib/db.ts + uploadUtils). Uses SERVER_URL or VITE_SERVER_IMAGE_API_URL and access_token from zitadel-oauth.json (run `login` first).

- Allows extra positional arguments.

**Subcommands:**

- `upload` — POST /api/images?forward=vfs&original=true — multipart field "file" (same as uploadUtils.uploadImage).

#### `service upload`

POST /api/images?forward=vfs&original=true — multipart field "file" (same as uploadUtils.uploadImage).

- Allows extra positional arguments.

**Options:**

- `files` (TEXT, required, positional) — Local image path(s); repeat or list several
- `--server-url` (TEXT, optional) — Service base URL (default: env SERVER_URL, else VITE_SERVER_IMAGE_API_URL, else CLIENT_URL). No trailing slash.
- `--dump-raw-http` (optional, flag) — Each stdout JSON line also includes http_status and raw_body (exact /api/images response string).

### `audio`

Audio utilities: list devices, record to WAV, play audio files, and TTS synthesis. Uses miniaudio (MP3, WAV, FLAC) and WASAPI/CoreAudio/ALSA. Requires FEATURE_STT build.

- Allows extra positional arguments.

**Subcommands:**

- `info` — List capture devices available on this system (name, channels, sample rate, default flag).
- `record` — Record from a capture device (PCM s16le mono 16 kHz). Stops after --duration-ms, Ctrl+C, console close, or `audio record stop`. Plain recording: --dst. Transcript only: --text-out (enables STT). Both is also fine.
- `play` — Play an audio file through the default output device. Supports MP3, WAV, FLAC via miniaudio. Playback is asynchronous by default; use --wait to block until finished.
- `tts` — Synthesise speech from text using a TTS provider and write audio to a file.

#### `audio info`

List capture devices available on this system (name, channels, sample rate, default flag).

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Machine-readable JSON array on stdout.

#### `audio record`

Record from a capture device (PCM s16le mono 16 kHz). Stops after --duration-ms, Ctrl+C, console close, or `audio record stop`. Plain recording: --dst. Transcript only: --text-out (enables STT). Both is also fine.

- Allows extra positional arguments.

**Subcommands:**

- `stop` — Signal a running `audio record` session in another terminal to stop cooperatively.
- `status` — Show the active `audio record` session (device, format, provider/model, elapsed time, dst, text-out, live STT buffer).

**Options:**

- `--dst` (TEXT, optional) — Destination WAV file. Optional when --text-out is set (scratch WAV is used internally). Relative paths are resolved from the current working directory.
- `--input` (TEXT, optional) — Capture device name (case-insensitive substring; use `audio info` to list names). Omit to use the system default input device.
- `--duration-ms` (INT:NONNEGATIVE, optional, default `0`) — Stop recording after this many milliseconds (0 = run until Ctrl+C).
- `--stt` (optional, flag) — Enable speech-to-text alongside recording (live ElevenLabs or batch Whisper). Implied by --text-out. Without either flag, --dst records WAV only.
- `--provider` (TEXT:{elevenlabs,pixlwiz}, optional) — STT provider when --stt or --text-out is used. Currently supported: elevenlabs (Scribe v2 Realtime), pixlwiz (Whisper batch). Defaults to chat.stt_provider from app settings.
- `--api-key` (TEXT, optional) — API key for the selected STT provider (--provider elevenlabs → xi-api-key). Also used as the TTS key when --voice-id is set. Falls back to the provider entry in App Settings when omitted.
- `--voice-id` (TEXT, optional) — ElevenLabs voice ID for real-time TTS playback. When set, each committed transcript is synthesised and played through the speakers. Requires --stt/--text-out and --provider elevenlabs. Browse voices at elevenlabs.io/app/voice-library.
- `--model-id` (TEXT, optional, default `eleven_v3`) — ElevenLabs TTS model used with --voice-id (default: eleven_v3).
- `--silence-ms` (INT, optional, default `1500`) — Auto-commit STT utterance after this many milliseconds of silence (0 = disabled; requires --stt or --text-out; default: 1500).
- `--text-out` (TEXT, optional) — Write the full STT transcript to this file (UTF-8 text). Enables STT automatically. --dst is optional. Relative paths are resolved from the current working directory.

#### `audio record stop`

Signal a running `audio record` session in another terminal to stop cooperatively.

- Allows extra positional arguments.

#### `audio record status`

Show the active `audio record` session (device, format, provider/model, elapsed time, dst, text-out, live STT buffer).

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Machine-readable JSON on stdout.

#### `audio play`

Play an audio file through the default output device. Supports MP3, WAV, FLAC via miniaudio. Playback is asynchronous by default; use --wait to block until finished.

- Allows extra positional arguments.

**Options:**

- `path` (TEXT, required, positional) — Path to the audio file. Relative paths are resolved from the current working directory.
- `--wait` (optional, flag) — Block until playback finishes (normally async). Ctrl+C aborts playback.

#### `audio tts`

Synthesise speech from text using a TTS provider and write audio to a file.

- Allows extra positional arguments.

**Options:**

- `--text` (TEXT, required) — Text to synthesise. Use quotes for multi-word input.
- `--dst` (TEXT, optional) — Destination file (.mp3 / .wav / .opus). Extension determines the default output format when --format is omitted. Omit to play through speakers without saving.
- `--no-play` (optional, flag) — Do not play audio through speakers; only save to --dst.
- `--provider` (TEXT:{elevenlabs,pixlwiz}, optional) — TTS provider: elevenlabs (direct API) or pixlwiz (proxy /audio/speech). Defaults to chat.tts_provider from app settings, then elevenlabs.
- `--api-key` (TEXT, optional) — API key for the TTS provider. Falls back to ELEVENLABS_API_KEY env var.
- `--voice-id` (TEXT, optional, default `tLK6fPv15M0oKv4V3ACR`) — Voice ID. For elevenlabs: ElevenLabs voice UUID. For pixlwiz: ElevenLabs voice UUID override (empty = proxy default). Defaults to chat.tts_model from app settings when omitted.
- `--model-id` (TEXT, optional, default `eleven_v3`) — Model ID. For elevenlabs: eleven_v3, eleven_turbo_v2, etc. For pixlwiz: proxy alias (pixlwiz-speech, pixlwiz-speech-turbo). Defaults to chat.tts_model from app settings when omitted.
- `--format` (TEXT, optional) — Output format override. ElevenLabs: mp3_44100_128, pcm_44100, opus_48000_32. PixlWiz proxy: mp3, opus, aac, flac. Defaults to mp3_44100_128 for .mp3, pcm_44100 for .wav.

### `video`

Video capture utilities: enumerate devices, capture still frames, and record video (Win32 Media Foundation; MJPEG AVI output). Requires FEATURE_VIDEO build.

- Allows extra positional arguments.

**Subcommands:**

- `info` — List video capture devices available on this system (name, id, default flag). Use --modes to also enumerate each device's supported resolutions and frame rates.
- `image` — Capture a single still frame from a webcam and save it as an image file.
- `record` — Record video from a webcam to an MJPEG AVI file (or H.264 MP4/MOV on macOS). Stops after --duration-ms, on Ctrl+C, or via `video record stop`.

#### `video info`

List video capture devices available on this system (name, id, default flag). Use --modes to also enumerate each device's supported resolutions and frame rates.

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Machine-readable JSON on stdout.
- `--modes` (optional, flag) — Enumerate supported capture modes (resolution, fps, format) for each device.
- `--input` (TEXT, optional) — Filter to a specific device (case-insensitive substring match on name). With --modes, enumerate modes for this device only.

#### `video image`

Capture a single still frame from a webcam and save it as an image file.

- Allows extra positional arguments.

**Options:**

- `--dst` (TEXT, required) — Destination image file (.jpg / .jpeg / .png / .bmp). Relative paths are resolved from the current working directory.
- `--input` (TEXT, optional) — Capture device name (case-insensitive substring; use `video info` to list). Omit to use the first/default device.
- `--mode` (INT, optional, default `-1`) — Mode index from `video info --modes` (0-based). Overrides --width/--height when set.
- `--width` (INT, optional, default `0`) — Preferred capture width in pixels (0 = device default).
- `--height` (INT, optional, default `0`) — Preferred capture height in pixels (0 = device default).

#### `video record`

Record video from a webcam to an MJPEG AVI file (or H.264 MP4/MOV on macOS). Stops after --duration-ms, on Ctrl+C, or via `video record stop`.

- Allows extra positional arguments.

**Subcommands:**

- `stop` — Signal a running `video record` session in another terminal to stop cooperatively.
- `status` — Show the active `video record` session (device, mode, codec, elapsed time, frames, dst).

**Options:**

- `--dst` (TEXT, optional) — Destination file (.avi MJPEG, or .mp4/.mov H.264 on macOS). Relative paths are resolved from the current working directory.
- `--input` (TEXT, optional) — Capture device name (case-insensitive substring; use `video info` to list). Omit to use the first/default device.
- `--mode` (INT, optional, default `-1`) — Mode index from `video info --modes` (0-based). Overrides --width/--height/--fps when set.
- `--width` (INT, optional, default `0`) — Preferred capture width in pixels (0 = device default).
- `--height` (INT, optional, default `0`) — Preferred capture height in pixels (0 = device default).
- `--fps` (INT:POSITIVE, optional, default `30`) — Frame rate for capture and AVI header (default: 30).
- `--duration-ms` (INT:NONNEGATIVE, optional, default `0`) — Stop recording after this many milliseconds (0 = run until Ctrl+C).
- `--quality` (INT:INT bounded to [1 - 100], optional, default `85`) — JPEG quality for MJPEG frames (1-100; default: 85).

#### `video record stop`

Signal a running `video record` session in another terminal to stop cooperatively.

- Allows extra positional arguments.

#### `video record status`

Show the active `video record` session (device, mode, codec, elapsed time, frames, dst).

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Machine-readable JSON on stdout.

### `xblox`

Run XBlox block-tree command flows.

- Allows extra positional arguments.

**Subcommands:**

- `info` — Print XBlox block/command metadata for builders and LLM composition.
- `run` — Run a blocks-file JSON document emitted by the XBlox web app.

**Options:**

- `--log-level` (TEXT:{trace,debug,info,warn,warning,error,err,critical,off,none}, optional) — Alias for the global --log-level option when using `xblox --log-level ... run`.

#### `xblox info`

Print XBlox block/command metadata for builders and LLM composition.

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Print full JSON metadata: block groups, descriptions, params, defaults, and supported commands.
- `--commands` (TEXT, optional) — Optional commands.json override for custom command metadata.

#### `xblox run`

Run a blocks-file JSON document emitted by the XBlox web app.

- Allows extra positional arguments.

**Options:**

- `--src` (TEXT, required) — Path to a blocks-file JSON document: { version: 1, context?: {}, roots: [...] }.
- `--commands` (TEXT, optional) — Optional commands.json override for resolving host.runCustomCommand({ id }).
- `--json` (optional, flag) — Print a JSON execution report.
- `--dry-run` (optional, flag) — Stage CLI/external commands but do not spawn child processes.
- `--no-wait` (optional, flag) — Skip sleeping for wait blocks.
- `--max-loop-iterations` (INT:INT in [0 - 100], optional, default `1`) — Temporary loop cap while expression/context mapping is stubbed.
- `--arg` (TEXT, optional) — Extra argument appended to cliCommand/external argv command invocations; repeatable.

### `commands`

List registered pm-image CLI commands for UI/custom-command pickers and scripts.

- Allows extra positional arguments.

**Options:**

- `--json` (optional, flag) — Print command metadata as JSON.

### `daemon`

Global shortcut daemon. Windows currently supports registering a logon daemon, listening for hotkeys, opening UI presets, forwarding app commands, launching CLI/external commands, and starting STT chat.

- Allows extra positional arguments.

**Subcommands:**

- `run` — Run the foreground hotkey daemon (default action).
- `tray` — Run the user-session tray daemon with global hotkeys and a notification-area menu.
- `register` — Windows: register the daemon for logon by writing HKLM Run (requires elevation). Seeds config if missing.
- `unregister` — Windows: remove the daemon HKLM Run entry (requires elevation).
- `stop` — Windows: stop the running daemon for this user session.
- `path` — Print the effective daemon.json path and exit.

**Options:**

- `--config` (TEXT, optional) — Daemon JSON config path. Default: the app roaming profile daemon.json next to settings.json.
- `--elevated-write-only` (optional, flag) — Internal: elevated register writes autorun only; caller starts the daemon.

#### `daemon run`

Run the foreground hotkey daemon (default action).

- Allows extra positional arguments.

**Options:**

- `--tray` (optional, flag) — Run with a notification-area tray icon.

#### `daemon tray`

Run the user-session tray daemon with global hotkeys and a notification-area menu.

- Allows extra positional arguments.

#### `daemon register`

Windows: register the daemon for logon by writing HKLM Run (requires elevation). Seeds config if missing.

- Allows extra positional arguments.

#### `daemon unregister`

Windows: remove the daemon HKLM Run entry (requires elevation).

- Allows extra positional arguments.

#### `daemon stop`

Windows: stop the running daemon for this user session.

- Allows extra positional arguments.

#### `daemon path`

Print the effective daemon.json path and exit.

- Allows extra positional arguments.

### `assistant`

AI assistant branch (UIA focus spy, STT dictation, write-back, toolbar UI).

- Allows extra positional arguments.

**Subcommands:**

- `spy` — Foreground UIAutomation focus spy: polls the focused element and logs all available UIA properties + text content (ValuePattern, TextPattern). Target apps: Notepad, LibreOffice, Chrome. Press Ctrl+C to stop.

Targets with special handling in src/win/assistant/:
  notepad.exe  — class RichEditD2DPT; ValuePattern + TextPattern both work.
  soffice.bin  — class SALFRAME; TextPattern in Writer, clipboard in Calc/Impress.
  chrome.exe   — framework 'Chrome'; address bar via ValuePattern, content via TextPattern.
  msedge.exe   — same as Chrome.
  code.exe     — VSCode Electron; Monaco a11y bridge exposes TextPattern.

#### `assistant spy`

Foreground UIAutomation focus spy: polls the focused element and logs all available UIA properties + text content (ValuePattern, TextPattern). Target apps: Notepad, LibreOffice, Chrome. Press Ctrl+C to stop.

Targets with special handling in src/win/assistant/:
  notepad.exe  — class RichEditD2DPT; ValuePattern + TextPattern both work.
  soffice.bin  — class SALFRAME; TextPattern in Writer, clipboard in Calc/Impress.
  chrome.exe   — framework 'Chrome'; address bar via ValuePattern, content via TextPattern.
  msedge.exe   — same as Chrome.
  code.exe     — VSCode Electron; Monaco a11y bridge exposes TextPattern.

- Allows extra positional arguments.

**Options:**

- `--interval-ms` (INT:INT in [50 - 60000], optional, default `500`) — Poll interval in milliseconds (default 500; minimum 50).
- `--no-value` (optional, flag) — Skip IUIAutomationValuePattern (edit fields, cells, address bars).
- `--no-selection` (optional, flag) — Skip IUIAutomationTextPattern selection ranges.
- `--no-text` (optional, flag) — Skip IUIAutomationTextPattern document range (full buffer).
- `--all` (optional, flag) — Log every poll tick even when nothing changed (very verbose).
- `--text-max-chars` (INT, optional, default `4096`) — Maximum characters to extract from the TextPattern document range (default 4096; -1 = no cap — caution: can be very large).
- `--ui` (optional, flag) — Show the assistant toolbar window (48×176 px, topmost, left-edge snap).
The toolbar provides mic (STT) and speaker (TTS) toggle buttons.
Drag to reposition; position is saved to %APPDATA%\PolyMech\pm-image\assistant-bar.pos.
The toolbar can also be launched via the daemon (CTRL+ALT+F5, see daemon.json).
- `--stt` (optional, flag) — Enable live STT dictation: microphone → ElevenLabs Scribe v2 Realtime →
write-back to the currently focused UI element via SendInput (KEYEVENTF_UNICODE).
Ctrl+C stops both spy and STT. In --stt mode, spy output shows focus changes only.
- `--stt-live` (optional, default `1`, flag) — Stream audio to the STT server as each chunk is captured (default: on).
With --no-stt-live, PCM is buffered per utterance and sent in one shot on VAD commit
(no partial transcript feedback; may improve accuracy for short phrases).
- `--stt-provider` (TEXT, optional) — STT provider name (default: chat settings stt_provider, e.g. "elevenlabs").
- `--stt-api-key` (TEXT, optional) — API key for the STT provider (default: from app provider settings).
- `--stt-silence-ms` (INT:INT in [300 - 10000], optional, default `1200`) — VAD silence threshold in ms before auto-committing an utterance (default 1200).

---

## Command variables

Custom command fields (`args`, `cwd`, `path`, `externalCommand`, `source`, `output`) support `${NAME}` substitution via `media::commands::resolve_variables` (`src/core/command_variables.cpp`).

### Current file

- `CURRENT_FILE` — Current file absolute path from Explorer selection or the open preview.
- `CURRENT_FILE_NAME` — Current file name including extension.
- `CURRENT_PATH` — Current Explorer folder, or current file parent folder when a file is selected.
- `CURRENT_SELECTION` — Whitespace-separated current Explorer selection list; entries are files or folders.

### Source

- `SRC_DIR` — Selected source parent directory.
- `SRC_EXT` — Selected source extension without the leading dot.
- `SRC_FILE` — Selected source file path.
- `SRC_FILE_EXT` — Selected source extension including the leading dot.
- `SRC_NAME` — Selected source filename without extension.

### Process

- `CWD` — Current command working directory.
- `PATH_LIST_SEP` — Native delimiter for lists of paths.
- `PATH_SEP` — Native path separator for this platform.

### Date / time

- `DD` — Current two-digit day of month.
- `HH` — Current two-digit hour.
- `MM` — Current two-digit month.
- `SS` — Current two-digit seconds.
- `YYYY` — Current four-digit year.

### Known folders - Portable

- `KNOWNFOLDER:Cache` — User cache folder.
- `KNOWNFOLDER:Config` — User configuration folder.
- `KNOWNFOLDER:Data` — User data folder.
- `KNOWNFOLDER:Home` — User home/profile folder.
- `KNOWNFOLDER:Temp` — Temporary files folder.

### Known folders - User

- `KNOWNFOLDER:Desktop` — Current user's Desktop folder.
- `KNOWNFOLDER:Documents` — Current user's Documents folder.
- `KNOWNFOLDER:Downloads` — Current user's Downloads folder.
- `KNOWNFOLDER:Favorites` — Current user's Favorites folder.
- `KNOWNFOLDER:Links` — Current user's Links folder.
- `KNOWNFOLDER:Music` — Current user's Music folder.
- `KNOWNFOLDER:Pictures` — Current user's Pictures folder.
- `KNOWNFOLDER:Profile` — User profile folder.
- `KNOWNFOLDER:Saved_Games` — Current user's Saved Games folder.
- `KNOWNFOLDER:Screenshots` — Current user's Screenshots folder.
- `KNOWNFOLDER:Templates` — Current user's Templates folder.
- `KNOWNFOLDER:Videos` — Current user's Videos folder.

### Known folders - App data

- `KNOWNFOLDER:Local_App_Data` — Current user's local application data folder.
- `KNOWNFOLDER:Program_Data` — Shared program data folder.
- `KNOWNFOLDER:Roaming_App_Data` — Current user's roaming application data folder.

### Known folders - Public

- `KNOWNFOLDER:Public` — Public user profile folder.
- `KNOWNFOLDER:Public_Desktop` — Public Desktop folder.
- `KNOWNFOLDER:Public_Documents` — Public Documents folder.
- `KNOWNFOLDER:Public_Downloads` — Public Downloads folder.
- `KNOWNFOLDER:Public_Music` — Public Music folder.
- `KNOWNFOLDER:Public_Pictures` — Public Pictures folder.
- `KNOWNFOLDER:Public_Videos` — Public Videos folder.

### Known folders - System

- `KNOWNFOLDER:Fonts` — Fonts folder.
- `KNOWNFOLDER:Program_Files` — Program Files folder.
- `KNOWNFOLDER:Program_Files_X86` — Program Files (x86) folder.
- `KNOWNFOLDER:Programs` — Current user's Start Menu Programs folder.
- `KNOWNFOLDER:Start_Menu` — Current user's Start Menu folder.
- `KNOWNFOLDER:Startup` — Current user's Startup folder.
- `KNOWNFOLDER:System` — Windows System folder.
- `KNOWNFOLDER:Windows` — Windows installation folder.

### Environment

- `ENV:NAME` — Any process environment variable. Example: `${ENV:USERPROFILE}`, `${ENV:HOME}`. Unknown or unset names are left unchanged.

---

## Custom commands

### Files

#### `custom.command-mpcmqepq-ee6f3` — AI:Images List

- **Type:** button
- **Action:** cli:llm
- **CLI command:** `llm`
- **Args:** `agent` `--prompt` `create directory listing, for images, in images.md`
- **CWD:** `${CURRENT_PATH}`
- **Log level:** `trace`
- **Source:** selection
- **Explorer menu:** yes

#### `custom.command-mpduqkaw-b621b` — cat

- **Type:** button
- **Action:** external
- **External:** mode `shell`
  - command: `cat`
  - shell: `cat ${CURRENT_FILE}`
- **Source:** selection

#### `custom.command-mpch9gdx-44982` — AI:Illustration

- **Type:** button
- **Action:** cli:transform
- **CLI command:** `transform`
- **Args:** `--prompt` `as technical illustration` `--src` `${CURRENT_FILE}`
- **Source:** files
  - files: `tests/assets/commands/illustration.jpg`
- **Explorer menu:** yes

#### `custom.command-mpejo01t-98bf0` — ls

- **Type:** button
- **Action:** external
- **External:** mode `argv`
  - command: `dir`
- **Source:** selection

#### `custom.command-mpi5stmy-8e9cb` — x1

- **Type:** button
- **Action:** cli:daemon
- **CLI command:** `daemon`
- **Log level:** `trace`
- **Source:** selection

### Pictures

#### `custom.capture` — Capture

- **Type:** dropdown
- **Action:** metadata
- **Tooltip:** Session and screenshot examples

#### `custom.capture.screenshot` — Take screenshot

- **Type:** button
- **Action:** app:browse
- **App command:** `browse`
- **Args:** `${KNOWNFOLDER:Profile}`

#### `custom.command-mpi5b1hw-8bdc2` — browse

- **Type:** button
- **Action:** ribbon:appSettings
- **Ribbon command:** `appSettings`
- **Args:** `C:\Users\zx\AppData\Roaming\PolyMech\pm-image`
- **Source:** selection

#### `custom.mic-start` — Mic Capture Start

- **Type:** button
- **Action:** cli:audio
- **CLI command:** `audio`
- **Args:** `record` `--text-out` `${KNOWNFOLDER:Config}/last.md`
- **Log level:** `trace`
- **Source:** selection

#### `custom.mic-stop` — Mic Capture Stop

- **Type:** button
- **Action:** cli:audio
- **CLI command:** `audio`
- **Args:** `record` `stop`
- **Source:** selection

#### `custom.mic-status` — Mic Capture Status

- **Type:** button
- **Action:** cli:audio
- **CLI command:** `audio`
- **Args:** `record` `status`
- **Source:** selection

#### `custom.video-start` — Video Capture Start

- **Type:** button
- **Action:** cli:video
- **CLI command:** `video`
- **Args:** `record` `--dst` `${KNOWNFOLDER:Videos}/capture.avi`
- **Log level:** `trace`
- **Source:** selection

#### `custom.video-stop` — Video Capture Stop

- **Type:** button
- **Action:** cli:video
- **CLI command:** `video`
- **Args:** `record` `stop`
- **Source:** selection

#### `custom.video-status` — Video Capture Status

- **Type:** button
- **Action:** cli:video
- **CLI command:** `video`
- **Args:** `record` `status`
- **Source:** selection

#### `custom.tools` — Tools

- **Type:** dropdown
- **Action:** metadata
- **Tooltip:** Built-in ribbon command examples

#### `custom.tools.theme` — Toggle theme

- **Type:** button
- **Action:** ribbon:theme
- **Ribbon command:** `theme`

#### `custom.tools.resetLayout` — Reset layout

- **Type:** button
- **Action:** ribbon:resetLayout
- **Ribbon command:** `resetLayout`

