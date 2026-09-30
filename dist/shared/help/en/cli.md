# `pm-image-cli` CLI reference

Executable name follows your build (often `pm-image-cli`). The embedded CLI title is `media-img` — same binary.

**Settings file layout, API keys, and `settings.json` keys** (admin / power users): see [Settings & credentials](../settings.md).

- **Subcommands:** at most one subcommand per run.
- **Platforms:** **Desktop** = Windows and **macOS** (same CLI semantics for GUI, job windows, profile paths where applicable). **Linux / headless** builds focus on server and batch subcommands without the native shell.
- **No subcommand:** on **desktop**, launch the main native UI (Windows: current `--ui-next` path; macOS: same product entry, native shell). On builds **without** that UI bundle, print help and exit with code `1`.

**Desktop note:** a few options appear only on the **Windows** build today; the **macOS** app is intended to expose the same flags when the desktop shell ships.

---

## Global options (apply before the subcommand when relevant)

### All platforms

| Option | Purpose |
|--------|---------|
| `--no-gui` | Parsed on every OS. **Desktop (Windows, macOS):** skips the list-style job window when a command would open it (multi-file batch, `--job-ui`, etc.); use plain console for scripts and CI. **Headless / no GUI shell:** accepted; **no effect** when there is no job UI to suppress. |

**`--help` / `-h`**, **`-v` / `--version`**: root app.

### Desktop GUI only (Windows + macOS)

Not offered on **headless** Linux-style builds. Intended semantics are **identical** on Windows and macOS (see **Desktop note** above).

| Option | Purpose |
|--------|---------|
| `--console` | **Windows:** attach or allocate a console for stdout/stderr after parse. **macOS:** make stdio usable when the process is not started from a terminal (same intent as Windows; exact mechanism is implementation-defined). Default off for GUI-style launches. |
`--src`). |
| `--src` | With `--ui-chat`, or with `--ui-preset` and no subcommand: seed paths. **Not** the same as `resize --src` (error if used alone). |
| `--ui-preset` | No subcommand: `main` (workbench), `chat`, or `viewer`. Overrides persisted `ui.workbench` for this process. Pair with `--src` to seed. |
| `--ui-reset` | This run ignores saved workbench layout / dock / chrome; built-in defaults. Does not rewrite the on-disk profile. |
| `--layout` | No subcommand: apply an exported layout JSON for this process only. Does not import or persist it. Uses the file's `workbench` unless `--ui-preset` is also provided. |
| `--splash` / `--no-splash` | Startup splash for the native UI: **default off**. **`--splash`** shows the GDI+ window when `FEATURE_SPLASH` is compiled in; **`--no-splash`** is explicit off (same as default). |
| `--settings` | Read settings from this file **for this process only** (UTF-8 JSON; **Windows** may also accept PME1). Path relative to cwd. Does **not** import into the profile — use **`settings import`** (desktop) to persist. |

---

## `login` — ZITADEL (Pixlwiz / Polymech)

Sign in against the same OIDC issuer as the web app (**PKCE**, loopback callback, system browser). Full flags, token file paths, redirect URI checklist, and **`sub` vs app user id** notes: **[ZITADEL & CLI login](../zitadel.md)**. With **`SERVER_URL`** (or image API base env) set, login also calls **`GET /api/me/identity`** (same as pm-pics **`fetchUserIdentity`**) and saves **`app_user_id`**, **`zitadel_sub`**, and **`roles`** into **`zitadel-oauth.json`**.

Quick checks:

```text
pm-image-cli login --probe
pm-image-cli login --decode-jwt
```

Interactive login needs the **exact** loopback redirect URI in ZITADEL (default `http://127.0.0.1:8844/callback`; use **`127.0.0.1`**, not `localhost`). Override with **`--oauth-port`** if you registered a different port — see [ZITADEL & CLI login](../zitadel.md).

Automated smoke: **`npm run test:zitadel`** (see `tests/zitadel-login.mjs`).

---

## `service` — Pixlwiz / Polymech HTTP API

Subcommand **`service upload`** sends multipart image uploads to **`/api/images?forward=vfs&original=true`** (same contract as **pm-pics** `uploadUtils.uploadImage`). Requires **`pm-image-cli login`** first and **`SERVER_URL`** in `.env` (see **[ZITADEL & CLI login](../zitadel.md)**).

**`service posts create`** creates a **post** (`POST /api/posts`) then uploads each file and attaches **picture** rows (`POST /api/pictures`) with `post_id`, `type: supabase-image`, and positions `0..n-1` — same sequence as **pm-pics** `publishHandlers` / **`insertPostServer`** + **`createPicture`**. Default **post title** is the **first file’s filename** (e.g. `photo.png`); each picture’s **title** is that file’s filename. **`--title`** / **`--description`** are optional; omit description to send `null`.

Integration tests: **`npm run test:service`** (upload fixtures under `tests/assets/service/` from **`build-fixtures.mjs`**), **`npm run test:service:posts`** (every image you keep under `tests/assets/service/posts/` — not seeded by **`build-fixtures.mjs`**). Both use **`--dump-raw-http`** and write **`tests/test-service-upload-responses.json`** / **`tests/test-service-posts-responses.json`** with exact HTTP response bodies for contract checks.

---

## Camera RAW and HEIC

Camera **RAW** (e.g. `.arw`, `.cr2`, `.cr3`, `.dng`, `.nef`, `.orf`, `.raf`, `.rw2`, …) and **HEIC / HEIF** are not valid “opaque bytes + wrong MIME” uploads for vision APIs. Where the pipeline talks to a model about **pixels**, inputs are decoded with **libvips** and sent as **JPEG in memory** (unless you explicitly opt out where the CLI allows it).

| Command / path | What happens |
|----------------|----------------|
| **`meta`** (default) | **`--resize-width`** (default **512** px on the longest edge) decodes through vips → JPEG before the cataloguer call. **`--no-resize`** skips that step and sends raw file bytes with a MIME guess from the extension — **do not use `--no-resize`** for RAW/HEIC unless you know the provider accepts that format. |
| **`find --llm`** | When it **generates** missing `.md` / `.json` sidecars, it runs the same **`meta_extract`** path — same **`--resize-width`** / **`--no-resize`** semantics as **`meta`**. |
| **`duplicates`** (`--meta-json-…` with **`--meta-json-implicit-generate`**) | Implicit sidecar generation calls **`meta_extract`** with the same **`MetaOptions`** resize behaviour as the **`meta`** command. |
| **`transform`** | The CLI does not expose **`--resize-first`**; defaults still **decode RAW/HEIC to JPEG** with an **implicit longest-edge cap of 2048 px** before the provider. Presets / JSON tool options can set **`resize_first`**, **`resize_width`**, and **`preresize_raw_only`** for stricter control. |

---

## Supported formats (libvips)

**Decode → process → encode** for **`resize`**, **`compress`**, **`serve`**, **`ipc`**, and the vips-backed parts of **`meta`** / **`find`** / **`duplicates`** / **`transform`** follows **whatever your linked libvips build can load and write**, including optional **`vips-modules-*`** / plugin DLLs next to the binary (typical **Windows** install). There is no second “format matrix” inside `pm-image-cli` beyond vips.

| Role | What to expect |
|------|----------------|
| **Read** | Common rasters (**JPEG**, **PNG**, **WebP**, **GIF**, **TIFF**, **SVG**, …), **camera RAW** when **LibRaw** is in the stack, **HEIC / HEIF** when **libheif** is present, plus any other loaders your build ships. Unsupported extensions fail at open with a vips error. **`resize`** also accepts **`http(s)://`** inputs (fetched then decoded like a local file). |
| **Write (`resize`)** | **JPEG**, **PNG**, **WebP**, **TIFF** are first-class in CLI / JSON. **AVIF** / **HEIC** output when the HEIF saver exists in your vips build; otherwise pick an extension vips can write or you get a saver error. Anything else vips exposes a writer for can work via **`vips_image_write_to_file`** from the **output extension** (e.g. **GIF**, **JP2K**, **BMP** — build-dependent). |
| **`compress`** | MozJPEG / PNG pipelines; **input** must still be something libvips opens; **output** is JPEG or PNG (from extension / `--compressor`). |
| **RAW / HEIC + LLMs** | Vision uploads: see **[Camera RAW and HEIC](#camera-raw-and-heic)**. For **`resize`** / **`compress`**, RAW and HEIC are ordinary vips inputs if your build supports them. |

**Check a machine:** `vipsheader path/to/file.ext` (quick open probe) and `vips -l` (operations / reflects build). **Sharp-style field names** for `resize` / JSON: [README — Formats](../../README.md#formats--same-idea-as-sharp--libvips). **Windows UI** RAW preview CMake flags: [win32xx-ui.md](../win32/win32xx-ui.md) (when building the ribbon shell).

---

## `resize` — resize / transform images

**Positional:** `[input] [output]` — `input` can be a path, glob (`*`, `?`, `**`), or `http(s)://` URL. **Batch:** `--src` (repeat) + `--dst` directory (or positional output as directory). **Formats:** libvips read/write set for your build — **[Supported formats (libvips)](#supported-formats-libvips)**.

| Parameter | Default / fallback |
|-----------|-------------------|
| `--fit` | `inside` (`inside\|cover\|contain\|fill\|outside`, Sharp-like) |
| `--position` | `centre` (meaningful for `cover`) |
| `--kernel` | `lanczos3` |
| `-q` / `--quality` | `85` (JPEG / WebP / AVIF) |
| `--png-compression` | `6` |
| `--background` | `#ffffff` (letterbox for `contain`) |
| `--rotate` | `0` (after EXIF autorotate unless `--no-autorotate`) |
| `--max-width` / `--max-height` | `0` = no limit |
| `--format` | From output file extension if omitted |
| Output path | Single input, output omitted → write under **cwd** with a sanitized name |
| Caching | On by default; `--no-cache` disables; `--cache-dir` default **`<cwd>/cache/images`** |
| `--url-timeout` | `5` seconds (`0` = implementation default for URL fetches) |
| `--url-max-redirects` | `20` |

**Desktop GUI:** `--ui`, `--ui-next`, `--chat` (with `--ui-next`), `--job-ui` (batch job window with pause / cancel).

**Examples**

```text
pm-image-cli resize photo.jpg thumb.jpg --max-width 800 --max-height 800
pm-image-cli resize --src a.jpg b.jpg --dst out/
```

---

## `compress` — MozJPEG / PNG pipeline

**Positional:** `[input] [output]` or **`--src`** + **`--dst`** for batch. **Input formats:** whatever libvips can open — **[Supported formats (libvips)](#supported-formats-libvips)**.

| Parameter | Default / fallback |
|-----------|-------------------|
| `--compressor` | `mozjpeg` or `png`; **inferred from output extension**; if unclear, **mozjpeg** |
| `-q` / `--quality` | `85` (MozJPEG) |
| `--level` | `9` (PNG DEFLATE) |
| `--colors` / `--quant-quality` | `256` / `85` (when `--quantize`; not in every trimmed install) |
| `--zopfli-iter` | `15` (when `--zopfli`; not in every trimmed install) |
| `--suffix` | Default stem suffix **`_compressed`** when format unchanged |

**Desktop GUI:** `--job-ui`.

**Examples**

```text
pm-image-cli compress in.jpg out.jpg -q 75
pm-image-cli compress logo.png out.png --compressor png --level 9
```

---

## `transform` — AI image edit (Gemini / Google, etc.)

**Input:** positional `input` and/or repeated **`--src`** (batch / multi-select from the host shell). **`--preset-id`**: preset from settings (`explorer_presets` transform op, chat quick actions `chat-<id>`, or ribbon `style-0`, …). **`-p` / `--prompt`:** required unless preset supplies it.

| Parameter | Default / fallback |
|-----------|-------------------|
| `output` | Omitted → derived from input + prompt slug |
| `--provider` | From app **Chat image provider**; **required** in practice if unset → command aborts |
| `--model` | From app **Chat image model** or provider **default_model**; aborts if still unset |
| `--api-key` | Optional on CLI; otherwise from **app provider / API Keys** in settings |

**RAW / HEIC:** implicit decode + long-edge cap before the API — see **[Camera RAW and HEIC](#camera-raw-and-heic)**.

**Desktop GUI:** `--job-ui`.

**Examples**

```text
pm-image-cli transform shot.png out.png -p "remove power lines from sky"
pm-image-cli transform --src a.jpg b.jpg -p "warm color grade" --preset-id style-0
```

With **multiple** `--src` paths, omit the positional **`output`** (outputs use per-input names next to each source).

---

## `create` — AI text-to-image

**Required:** `-p` / `--prompt`. **Output:** positional `output` or default **`create_<slug>.png`** in cwd.

| Parameter | Default / fallback |
|-----------|-------------------|
| `--provider` / `--model` / `--api-key` | Same pattern as **`transform`** (Chat image defaults + provider default model; credentials from settings) |

**Examples**

```text
pm-image-cli create hero.png -p "flat vector icon of a camera, white on navy"
pm-image-cli create -p "isometric city block" --aspect-ratio 16:9 --image-size 2K
```

---

## `meta` — descriptions, JSON, EXIF (LLM)

**Required:** one or more **`input`** paths (repeat positional).

| Parameter | Default / fallback |
|-----------|-------------------|
| `--out-dir` | Sidecar outputs next to each source |
| `--provider` / `--model` / `--api-key` | Same as **`transform`** (Chat image provider/model; key from settings) |
| `-p` / `--prompt` | Empty → **built-in cataloguer** prompt |
| `--resize-width` | `512` longest edge (skipped with `--no-resize`) |
| Outputs | Both **`<stem>.md`** and **`<stem>.json`** unless `--no-md` / `--no-json` |

**RAW / HEIC:** default resize-on is what makes those formats work for the vision model; see **[Camera RAW and HEIC](#camera-raw-and-heic)**.

**Desktop GUI:** `--job-ui`.

**Examples**

```text
pm-image-cli meta vacation/*.jpg
pm-image-cli --no-gui meta shot.jpg --no-json --update-exif
```

---

## `find` — by name/path or by LLM over cached meta

**Required:** **`input`** (repeat: files, folders, globs) and **`-p` / `--prompt`**.

| Mode | Flag | Behavior |
|------|------|------------|
| **Name** (default) | *(no `--llm`)* | Match prompt against filename / folder (see `--case-sensitive`, `--no-folders`, `--no-recursive`) |
| **LLM** | `--llm` | Uses / builds `.md` / `.json` / EXIF cache; default judge is **LLM** unless `--local-text` (substring/word match on text only) |

| Parameter | Default / fallback |
|-----------|-------------------|
| `--max` | `0` = unlimited |
| `--resize-width` | `512` when meta is generated for LLM path |
| `--provider` / `--model` | Required when **`--llm`** if not set: from Chat image defaults (same abort rules as **`transform`**) |

**RAW / HEIC:** LLM mode uses the same **`meta`** resize rules when it builds sidecars; see **[Camera RAW and HEIC](#camera-raw-and-heic)**.

**Examples**

```text
pm-image-cli find ./assets -p "hero" --no-recursive
pm-image-cli find ./photos -p "golden hour beach" --llm --max 20
```

---

## `search` — find files by name or content (grep)

**Required:** one or more **`input`** paths (repeat: files, folders, or globs) and **`-q` / `--query`**.

Operates in two modes selected by **`--grep`**:

| Mode | Trigger | What is matched |
|------|---------|-----------------|
| **name** (default) | *(no `--grep`)* | File name (and optionally parent folder names) against the query |
| **grep** | `--grep` | File contents, line by line — binary files skipped by default |

### Key options

| Option | Default | Description |
|--------|---------|-------------|
| `-q` / `--query` | *(required)* | Search pattern: literal substring, ECMAScript regex (`--regex`), or grep needle |
| `--grep` | off | Scan file contents instead of names |
| `--type` | `any` | `any` · `image` · or an **rg-style type shorthand**: `cpp`, `c`, `cs`, `css`, `go`, `html`, `java`, `js`, `json`, `kotlin`, `md`, `py`, `rs`, `rust`, `sh`, `swift`, `ts`, `toml`, `txt`, `xml`, `yaml`. Type shorthands expand to the matching filename globs automatically. |
| `--indexer` | `own` | `own` (recursive walker, cross-platform) · `os` (Windows Search / Spotlight / locate; falls back to `own`) |
| `--include` | — | Only scan files whose name matches this glob (e.g. `*.cpp`). Repeatable. |
| `--exclude` | — | Skip files matching this glob. Repeatable. |
| `--exclude-dir` | built-in | Directory names to prune during recursion. Default list: `.git`, `node_modules`, `dist`, `build`, … Repeatable. |
| `--regex` | off | Treat `--query` as an ECMAScript regex (`std::regex`) |
| `--case-sensitive` | off | Default is case-insensitive |
| `--whole-word` | off | Require word-boundary match (literal mode) |
| `--no-recursive` | off | Only the top level of each input directory |
| `--include-hidden` | off | Include dot-files and dot-directories |
| `-C` / `--context` | `0` | Symmetric context lines before **and** after each grep match |
| `-B` / `--context-before` | `0` | Context lines **before** each match (`-B` wins unless `-C` is also set) |
| `-A` / `--context-after` | `0` | Context lines **after** each match (`-A` wins unless `-C` is also set) |
| `--multiline` | off | Regex: `^` and `$` match per-line boundaries (like `rg -U`) |
| `--output-mode` | `content` | `content` · `files_with_matches` · `count` |
| `--head-limit` | `0` | Cap output to first N entries (`0` = unlimited) |
| `--offset` | `0` | Skip first N entries before applying `--head-limit` |
| `--max` | `0` | Max total results (`0` = unlimited) |
| `--max-per-file` | `1000` | Max grep matches per file |
| `--max-file-size` | `0` | Skip files larger than N bytes (`0` = 256 MB default) |
| `--names-only` | off | With `--grep`: print only the file path, not individual match lines |
| `--no-skip-binary` | off | Do not skip binary-detected files in grep mode |
| `--dry-run` | off | Resolve and count candidate files; no matching performed |
| `--json` | off | Emit a JSON array `[{path, source, score, line, column, preview, context_before, context_after}]` |

**Exit codes:** `0` = ≥ 1 match found (or `--dry-run` succeeds); `1` = no matches; `> 1` = error.

### Examples

**Name search — find files by filename fragment**

```text
# All files under a folder whose name contains "checker" (case-insensitive)
pm-image-cli search ./assets -q checker

# Restrict to image files only
pm-image-cli search ./photos -q sunset --type image

# Limit to .cpp files via glob
pm-image-cli search ./src -q scheduler --include "*.cpp"

# Multiple top-level folders
pm-image-cli search ./src ./tests -q config --include "*.json"
```

**Name search with regex**

```text
# Filenames that start with a digit sequence (e.g. "128x128.png", "64.jpg")
pm-image-cli search ./assets -q "\d+" --regex

# Files starting with "test_" followed by a word character
pm-image-cli search ./tests -q "^test_\w" --regex

# Case-sensitive regex match
pm-image-cli search ./src -q "^MyClass" --regex --case-sensitive
```

**Glob patterns — include / exclude**

```text
# Only TypeScript and TSX files
pm-image-cli search ./src -q "useState" --grep --include "*.ts" --include "*.tsx"

# Use the rg-style --type shorthand (expands automatically)
pm-image-cli search ./src -q "useState" --grep --type ts

# Exclude generated files
pm-image-cli search ./src -q "TODO" --grep --exclude "*.min.js" --exclude "*.bundle.js"

# Exclude a directory
pm-image-cli search ./repo -q "deprecated" --grep --exclude-dir dist --exclude-dir build
```

**Grep — search file contents**

```text
# All #include lines in .cpp files under tests/unit/
pm-image-cli search ./tests/unit -q "#include" --grep --include "*.cpp"

# All markdown files under ./tests that contain the word "Contract"
pm-image-cli search ./tests -q "Contract" --grep --type md

# Same search, show 2 lines of context around each match
pm-image-cli search ./tests -q "Contract" --grep --type md -C 2

# Find all TODO comments in the source tree
pm-image-cli search ./src -q "TODO" --grep --type cpp

# Case-insensitive function search with line numbers
pm-image-cli search ./src -q "initCamera" --grep --type cpp

# Only file paths (not individual match lines)
pm-image-cli search ./src -q "deprecated" --grep --names-only
```

**Context lines around grep matches**

```text
# 2 lines before and after each match (symmetric, like grep -C)
pm-image-cli search ./src -q "throw" --grep --type cpp -C 2

# 3 lines before, 1 line after (asymmetric, like grep -B / -A)
pm-image-cli search ./src -q "error_code" --grep --type cpp -B 3 -A 1

# JSON output with context for tooling
pm-image-cli search ./src -q "assert" --grep --type cpp -C 2 --json
```

**Output modes**

```text
# List only the files that contain the pattern (like rg -l)
pm-image-cli search ./src -q "FIXME" --grep --output-mode files_with_matches

# Count matches per file (like rg -c)
pm-image-cli search ./src -q "printf" --grep --output-mode count

# Paginate: skip first 50 results, show next 25
pm-image-cli search ./src -q "error" --grep --offset 50 --head-limit 25
```

**Dry run and JSON**

```text
# Preview how many candidate files would be scanned, without matching
pm-image-cli search ./repo -q anything --dry-run

# Full JSON output (path / source / score / line / column / preview)
pm-image-cli search ./assets -q "holiday" --json
pm-image-cli search ./src    -q "main"    --grep --type cpp --json
```

---

## `duplicates` — duplicate grouping

**Input:** repeatable **`input`** (omit if **`--load-session`** only). **`--by`:** `size` \| `fingerprint` (default) \| `meta`.

| Parameter | Default / fallback |
|-----------|-------------------|
| `--min-group` | `2` |
| `--max-hamming` | `0` (exact dHash; used with `fingerprint`) |
| `--meta-json-min-sim` | `7` (0–10, for `--meta-compare-json-llm`) |
| `--llm-router` / `--llm-model` / `--llm-api-key` / `--llm-base-url` | Defaults: **Chat provider** in app settings, else **openrouter**; model default **gpt-4o-mini** family; key from **chat / API Keys**; base URL from chat settings |
| `--llm-timeout-ms` | `0` → from settings or **60000** ms |

**Examples**

```text
pm-image-cli duplicates ./imports --by fingerprint --max-hamming 3
pm-image-cli duplicates --load-session report.json
```

**Implicit meta sidecars** (`--meta-json-implicit-generate` with **`--meta-compare-json-llm`**): same **`meta`** / vips / resize behaviour as the **`meta`** command for RAW/HEIC — see **[Camera RAW and HEIC](#camera-raw-and-heic)**.

---

## `serve` — HTTP REST server

HTTP routes and multipart contracts: **[REST API reference](../public/api-rest.md)**. OpenAPI 3 spec: **[openapi.yaml](../public/openapi.yaml)** (embedded in the binary; live **`GET /openapi.yaml`**, **`GET /api-docs`** when `serve` is running). Decode / encode for image routes uses the same **libvips** capabilities as the CLI — **[Supported formats (libvips)](#supported-formats-libvips)**.

| Parameter | Default |
|-----------|---------|
| `--host` | `127.0.0.1` |
| `-p` / `--port` | `8080` |
| `--no-cache` | Server default: cache off when client JSON omits `"cache"` |
| `--cache-dir` | Default cache directory when JSON omits `"cache_dir"` |

**Examples**

```text
pm-image-cli serve --port 8080
pm-image-cli serve --host 0.0.0.0 -p 9000 --no-cache
```

---

## `ipc` — JSON-lines server (TCP; Unix socket elsewhere)

Image workers use **libvips** the same way as **`serve`** — **[Supported formats (libvips)](#supported-formats-libvips)**.

| Parameter | Default |
|-----------|---------|
| `--host` | `127.0.0.1` |
| `-p` / `--port` | `9333` |
| `--unix` | Unix domain socket path (**macOS** / Linux; not Windows TCP-only builds) |
| `--no-cache` / `--cache-dir` | Same idea as **`serve`** |

**Example**

```text
pm-image-cli ipc -p 9333
```

---

## `settings` — profile store (desktop)

Profile paths, PME1 / DPAPI, env overrides, and key resolution: [Settings & credentials](../settings.md).

**Windows** today; **macOS** when the desktop package ships the same commands. Distinct from top-level **`--settings`** (one-off read path for a single process).

On **macOS**, the app’s default settings file is typically  
`~/Library/Application Support/PolyMech/pm-image-cli/settings.json`. **`settings import`** / **`export`** move data into or out of the live profile for that platform.

| Command | Notes |
|---------|--------|
| `settings import <path>` | Merges into the live profile store (Windows: under `%APPDATA%\…`; macOS: under Application Support as above). |
| `settings export [path]` | Default output file: **`settings.json`** in cwd. **`--encrypted`**: **Windows** PME1 binary (DPAPI-bound); macOS may use a different on-disk encryption story when implemented — prefer plain UTF-8 JSON for interchange until documented otherwise. |

**Examples**

```text
pm-image-cli settings import my-backup.json
pm-image-cli settings export backup.json
```

---

## `provider models list` — model catalog JSON

| Parameter | Default / fallback |
|-----------|-------------------|
| `--provider` | `replicate` |
| `--api-key` | Replicate: **required** on CLI; OpenRouter: optional (public catalog); else from app when set |
| `--base-url` | Replicate: official collection URL; OpenRouter: **`https://openrouter.ai/api/v1`** |

**Example**

```text
pm-image-cli provider models list --provider openrouter
```

---

## `llm` — tools and agent

| Subcommand | Role |
|------------|------|
| `llm info` | Prints effective Chat / image defaults and CLI overrides; **`--json`** for machine output |
| `llm tools-list` | JSON-Schema tool catalog |
| `llm tools-call` | **`--name`** (required), **`--args`** JSON file (`-` stdin), optional **`--image-file`** → injects `arguments.image.b64` |
| `llm agent` | One agent turn with path tools; **`-p`** required |

**`llm agent` — human stdout (markdown / color)**

| Option | Purpose |
|--------|---------|
| `--markdown` | `auto` (default): run the terminal markdown pass when stdout is a TTY. `plain`: raw model UTF-8. `render`: always run the pass. **Ignored with `--json`.** |
| `--color` | `auto` (default): emit ANSI when rendering and stdout is a TTY and **`NO_COLOR`** is unset and **`TERM`** is not **`dumb`**. `never` / `always` override. On Windows, **virtual terminal processing** is enabled on stdout when color is used. |

Full spec (streaming stderr, future **`--interactive`** sessions): **[LLM agent CLI](../llm-agent-cli.md)**.

**`llm agent` defaults (from settings when omitted)**

| Option | Fallback |
|--------|----------|
| `--router` | Chat provider settings, else **openrouter** |
| `--model` | Chat model in settings |
| `--api-key` / `--base-url` | Chat / API Keys |
| `--timeout-ms` | Settings or **60000** |
| `--max-iter` | Settings or **8** |

**Examples**

```text
pm-image-cli llm info --json
pm-image-cli llm agent -p "compress these to quality 80 mozjpeg" --paths a.jpg b.jpg
```

---

## `register-explorer` (Windows host integration)

Registers Explorer context menus for image extensions, **Directory**, and **Directory\\Background** under a group (default **`--group`** = **`PM-Media`**).

### Verbs vs **Presets**

- **PM-Media root** (always where selection supports it): resize width pairs from **`--widths`** (default **`1980,1200`** — in-place and “copy with `_width` suffix” variants), **Open in …**, **Convert to JPG**, **Generate Meta (.md + .json)**, and **Chat…** (runs `pm-image-cli --ui-preset=chat --src` with semicolon-joined paths via the shell extension’s in-process handler, not a plain `command` string in the registry).
- **Nested submenu “Presets”** (only when at least one transform-style entry exists): holds **AI transform** verbs built **from your settings** at register time:
  - **`explorer_presets`** entries with **`op`** = **`transform`** and an **`id`** → Explorer label **`Transform:`** plus label or id; choosing the verb runs **`transform --preset-id`** with that id.
  - **`chat_web.quick_actions`** (each needs **`id`**, **`name`**, non-empty **`prompt`**) → preset ids **`chat-…`** (prefix applied unless the id already starts with **`chat-`**).
  - **`transform.prompt_presets`** array (each object needs non-empty **`prompt`**) → ids **`style-0`**, **`style-1`**, …

The **Presets** block is registered **first** under the cascade so Windows Explorer is less likely to drop it when many third-party verbs compete on image file types.

### Flags

| Flag | Role |
|------|------|
| **`--unregister`** | Removes the **`--group`** shell subtree for all registered association targets, clears **Open with** / ProgId hooks for this app, and clears the shell-handler registration used by the extension. Use this before a clean re-register if context menus look wrong or stale. |
| **`--dry`** | Log and print what would happen; **no** registry writes (unregister dry-run only prints intent). |
| **`--no-refresh-shell`** | Skip **`SHChangeNotify`** after register/unregister (default is to refresh). |
| **`--media-bin`** | **Optional.** Full path to the **`pm-image-cli`** executable written into **Open with** / ProgId and association commands. **Omit** (default) to use the **same executable that runs** `register-explorer`. Only needed if you launch registration from another program but want menus to start a specific installed **`pm-image-cli`**. |
| **`--widths`** | Comma-separated max widths for the resize verbs (see defaults above). |

**Examples**

```text
pm-image-cli register-explorer --dry
pm-image-cli register-explorer --unregister
pm-image-cli register-explorer
```

**macOS:** Finder / Services integration is expected as a **separate** flow — do not assume this subcommand exists on Darwin.

---

## `audio` — microphone capture, STT, TTS, and playback

Requires **`FEATURE_STT`** compiled in (the default on desktop and server builds). Provider credentials: ElevenLabs API key from **`--api-key`**, the `elevenlabs` entry in `settings.json` providers, or the **`ELEVENLABS_API_KEY`** environment variable — resolved in that order for every subcommand.

---

### `audio info` — list capture devices

Enumerate microphone / capture devices available on the system.

| Option | Default | Description |
|--------|---------|-------------|
| `--json` | off | Machine-readable JSON array |

**Examples**

```text
pm-image-cli audio info
pm-image-cli audio info --json
```

**JSON output shape**

```json
[
  { "index": 0, "name": "Microphone (Yeti X)", "is_default": true,  "channels": 2, "sample_rate": 48000 },
  { "index": 1, "name": "Stereo Mix",          "is_default": false, "channels": 2, "sample_rate": 44100 }
]
```

---

### `audio record` — capture to WAV, with optional live STT

Captures microphone audio as **s16le mono 16 kHz** and writes a WAV file. Add **`--provider elevenlabs`** to stream audio to ElevenLabs Scribe v2 Realtime and receive live transcripts. Add **`--voice-id`** on top of that to speak each committed segment back through the default speakers (STT → TTS → speaker loop).

| Option | Default | Description |
|--------|---------|-------------|
| `--dst` | *(required)* | Output `.wav` path |
| `--input` | system default | Capture device name (substring match against `audio info` names) |
| `--duration-ms` | `0` = until Ctrl+C | Stop automatically after N milliseconds |
| `--provider` | off | `elevenlabs` — enable real-time STT |
| `--api-key` | from settings / env | ElevenLabs API key |
| `--silence-ms` | `1500` | Auto-commit STT utterance after N ms of silence (VAD) |
| `--voice-id` | off | ElevenLabs voice ID — enables TTS echo; requires `--provider` |
| `--model-id` | `eleven_v3` | ElevenLabs TTS model used with `--voice-id` |

**Audio-only — record until Ctrl+C**

```text
pm-image-cli audio record --dst out.wav
```

**Timed capture — 10 seconds, specific mic**

```text
pm-image-cli audio record --dst meeting.wav --input "Yeti" --duration-ms 10000
```

**Live STT — transcripts printed as you speak**

```text
pm-image-cli audio record --dst out.wav --provider elevenlabs
pm-image-cli audio record --dst out.wav --provider elevenlabs --api-key $ELEVENLABS_API_KEY
```

Progress line while recording:

```text
    3.2 s     51200 samples     100.0 KB  [hello wor...]
```

Committed transcripts print on a new line:

```text
[committed] Hello world.
```

**STT with custom VAD window**

```text
# commit after 800 ms silence instead of the 1500 ms default
pm-image-cli audio record --dst out.wav --provider elevenlabs --silence-ms 800
```

**STT → TTS → speaker (voice echo loop)**

Speak into the mic — each committed sentence is synthesised and played back through the default speakers. The mic is muted while the speaker is active to prevent feedback.

```text
pm-image-cli audio record --dst session.wav --provider elevenlabs --voice-id tLK6fPv15M0oKv4V3ACR
```

With a non-default TTS model:

```text
pm-image-cli audio record --dst session.wav --provider elevenlabs \
  --voice-id tLK6fPv15M0oKv4V3ACR --model-id eleven_turbo_v2_5
```

---

### `audio tts` — text-to-speech synthesis

Synthesises text with ElevenLabs and either **saves to a file**, **plays through speakers**, or both.

| Option | Default | Description |
|--------|---------|-------------|
| `--text` | *(required)* | The text to synthesise |
| `--dst` | off | Output file path; extension determines format (`.mp3` → `mp3_44100_128`, `.wav` → `pcm_44100` + WAV header) |
| `--format` | auto from `--dst` ext, else `mp3_44100_128` | Override output format (e.g. `pcm_44100`, `mp3_44100_128`) |
| `--no-play` | off | Skip speaker playback (useful when only saving to file) |
| `--voice-id` | `JBFqnCBsd6RMkjVDRZzb` (`George`) | ElevenLabs voice ID |
| `--model-id` | `eleven_v3` | ElevenLabs model |
| `--provider` | `elevenlabs` | Provider label (display only; only ElevenLabs is supported) |
| `--api-key` | from settings / env | ElevenLabs API key |

**Play through speakers only (no file)**

```text
pm-image-cli audio tts --text "The first move is what sets everything in motion."
pm-image-cli audio tts --text "Hello world" --api-key $ELEVENLABS_API_KEY
```

**Save to MP3, no playback**

```text
pm-image-cli audio tts --text "The first move is what sets everything in motion." \
  --dst out.mp3 --no-play
```

**Save to WAV — PCM 44 kHz, WAV header added automatically**

```text
pm-image-cli audio tts --text "Hello world" --dst out.wav
```

**Custom voice and model**

```text
pm-image-cli audio tts --text "Concise. Precise." \
  --voice-id tLK6fPv15M0oKv4V3ACR --model-id eleven_turbo_v2_5 --dst reply.mp3
```

**Save and play**

```text
pm-image-cli audio tts --text "Recording complete." --dst notify.mp3
```

---

### Full STT → TTS → speaker pipeline

The two most common end-to-end patterns:

**1. Live dictation echo — speak, hear your words played back**

```text
pm-image-cli audio record --dst session.wav \
  --provider elevenlabs \
  --voice-id tLK6fPv15M0oKv4V3ACR
```

Flow:

```
mic → s16le PCM → ElevenLabs Scribe v2 Realtime
                       ↓ committed transcript (delta)
              ElevenLabs TTS (eleven_v3, mp3_44100_128)
                       ↓ decoded audio bytes
                  AudioOutput.play_sync()   [mic muted during playback]
```

**2. Transcribe first, synthesise second**

Record and transcribe a session, then synthesise individual lines:

```text
# Step 1 — transcribe (transcripts printed to stdout as they arrive)
pm-image-cli audio record --dst raw.wav --provider elevenlabs > transcript.txt

# Step 2 — synthesise each committed line
pm-image-cli audio tts --text "Hello world." --dst response.mp3 --no-play
```

**API key resolution order** (same for `record` and `tts`):

1. `--api-key` flag
2. `elevenlabs` entry in `settings.json` providers
3. `ELEVENLABS_API_KEY` environment variable

---

### `llm agent --mic` — voice-driven LLM agent

The LLM agent subcommand also supports microphone input with live STT and optional TTS response. See **[LLM agent CLI](../llm-agent-cli.md)** for full details.

| Option | Default | Description |
|--------|---------|-------------|
| `--mic` | off | Enable microphone STT input |
| `--api-key` | from settings / env | ElevenLabs key for STT (and TTS when `--voice-id` set) |
| `--voice-id` | off | Speak agent responses back through speakers |
| `--tts-model-id` | `eleven_v3` | TTS model for spoken responses |
| `--silence-ms` | `1500` | VAD: auto-commit after N ms silence |

```text
# Voice conversation: speak a question, hear the answer
pm-image-cli llm agent --mic --voice-id tLK6fPv15M0oKv4V3ACR -p "you are a helpful assistant"
```

---

## `video` — webcam / video capture (Win32)

Requires **`FEATURE_VIDEO`** compiled in (default ON for Windows builds). Uses **Media Foundation** (`IMFSourceReader`) for capture and **WIC** for image encoding — no third-party codec dependencies. Frames are always delivered as BGR24; MJPEG AVI output uses per-frame JPEG encoding via WIC.

---

### `video info` — list capture devices

Enumerate video capture devices (webcams, capture cards, virtual cameras).

| Option | Default | Description |
|--------|---------|-------------|
| `--modes` / `-m` | off | Also enumerate every supported capture mode (resolution, frame rate, pixel format) for each device |
| `--input` | all devices | Filter to one device by case-insensitive name substring; with `--modes` only that device's modes are shown |
| `--json` | off | Machine-readable JSON |

**Examples**

```text
pm-image-cli video info
pm-image-cli video info --modes
pm-image-cli video info --modes --input "NexiGo"
pm-image-cli video info --modes --json
```

**Text output (with `--modes`)**

```text
video devices  (1)
  [0] NexiGo N930E FHD Webcam  (default)
     [ 0]  1920 x 1080  @     30 fps  MJPEG
     [ 1]  1920 x 1080  @     30 fps  NV12
     [ 2]  1280 x  720  @     30 fps  MJPEG
     [ 3]   640 x  480  @     30 fps  MJPEG
     [ 4]   640 x  480  @     30 fps  YUY2
     ...
```

The `[N]` index can be passed directly to `--mode N` in `video image` and `video video`.

**JSON output shape (with `--modes`)**

```json
[
  {
    "name": "NexiGo N930E FHD Webcam",
    "id": "\\\\?\\usb#vid_3433...",
    "is_default": true,
    "modes": [
      { "width": 1920, "height": 1080, "fps": 30.0, "format": "MJPEG" },
      { "width": 1920, "height": 1080, "fps": 30.0, "format": "NV12"  },
      { "width":  640, "height":  480, "fps": 30.0, "format": "YUY2"  }
    ]
  }
]
```

---

### `video image` — capture a still frame

Open a webcam, grab one frame, and save it as an image file. The device is opened and closed immediately after the first frame arrives.

| Option | Default | Description |
|--------|---------|-------------|
| `--dst` | *(required)* | Output image file (`.jpg` / `.jpeg` / `.png` / `.bmp`); relative paths resolved from cwd |
| `--input` | first device | Device name (case-insensitive substring match against `video info` names) |
| `--mode` | `-1` = auto | Mode index from `video info --modes` — exact native mode, no scorer |
| `--width` | `0` = device default | Preferred capture width in pixels (ignored when `--mode` is set) |
| `--height` | `0` = device default | Preferred capture height in pixels (ignored when `--mode` is set) |

Resolution selection: if `--mode N` is given the indexed native mode is used exactly. Otherwise a best-match scorer across all native modes minimises squared distance to the requested dimensions, with a tiebreak that favours larger resolutions.

**Examples**

```text
# Default resolution, first webcam
pm-image-cli video image --dst shot.jpg

# Specific resolution (picks closest native mode)
pm-image-cli video image --dst hd.jpg --width 1920 --height 1080

# Specific device
pm-image-cli video image --dst cam.png --input "NexiGo"
```

**Console output**

```text
video image: connecting to device...
video image
  device : NexiGo N930E FHD Webcam
  format : bgr24
  size   : 1920 x 1080
  file   : C:\…\hd.jpg  (462553 bytes)
```

---

### `video video` — record to MJPEG AVI

Stream frames from a webcam and write them to an MJPEG AVI file. Stops on Ctrl+C or automatically after `--duration-ms` milliseconds. Frames are JPEG-encoded via WIC and written to a standards-compliant RIFF/AVI container — no FFmpeg or codec install required.

| Option | Default | Description |
|--------|---------|-------------|
| `--dst` | *(required)* | Output `.avi` file; relative paths resolved from cwd |
| `--input` | first device | Device name (case-insensitive substring) |
| `--mode` | `-1` = auto | Mode index from `video info --modes` — exact native mode, overrides `--width`/`--height`/`--fps` |
| `--width` | `0` = device default | Preferred capture width |
| `--height` | `0` = device default | Preferred capture height |
| `--fps` | `30` | Target frame rate written to AVI header and used by the best-mode picker |
| `--duration-ms` | `0` = until Ctrl+C | Stop recording automatically after N milliseconds |
| `--quality` | `85` | JPEG quality for MJPEG frames (1–100) |

**Examples**

```text
# 5-second clip at default resolution
pm-image-cli video video --dst clip.avi --duration-ms 5000

# 1080p, 30 fps, until Ctrl+C (by preferred dimensions)
pm-image-cli video video --dst session.avi --width 1920 --height 1080

# Exact native mode [0] from `video info --modes` (e.g. 1920x1080 MJPEG)
pm-image-cli video video --dst session.avi --mode 0

# Specific device, mode [2], 10 s, quality 75
pm-image-cli video video --dst cam.avi --input "NexiGo" --mode 2 --duration-ms 10000 --quality 75
```

**Console output**

```text
video record: opening device...
video record
  device  : NexiGo N930E FHD Webcam
  fps     : 30
  duration: 5 s
  output  : C:\…\clip.avi
  stop    : Ctrl+C or after duration
  3.2 s  47 frames
video record: done
  frames  : 90
  duration: 5.1 s
  size    : 1920 x 1080
  file    : C:\…\clip.avi  (45829120 bytes)
```

**Notes**

- Output format is **MJPEG AVI** (FourCC `MJPG`). VLC, Windows Media Player, and ffmpeg can all play the result.
- Actual frame rate depends on USB bandwidth, device exposure time, and JPEG encoding overhead. The `--fps` value is written to the AVI header but is not enforced; use `--quality` to trade file size against frame rate.
- To convert to H.264 MP4: `ffmpeg -i clip.avi -c:v libx264 -crf 23 out.mp4`

---

## `app` — commands to a **running** UI instance (desktop)

Subcommands include **`takescreenshot`**, **`pausebatch`**, **`resumebatch`**, **`cancelbatch`**, **`chat`**, **`browse`**, **`recordstart`**, **`recordstop`**, **`videorecordstart`**, **`videorecordstop`**, **`replay`**.

| Subcommand | Notes |
|------------|--------|
| `app takescreenshot` | Default PNG: **`<cwd>/screenshots/<timestamp>.png`**; **`-o`**. **`--window-width` / `--window-height`**: both **`0`** = saved layout size |
| `app replay` | **`--path`** session JSON (required) |

**Example**

```text
pm-image-cli app browse "C:\work\session\shot.png"
pm-image-cli app browse "/Users/me/work/session/shot.png"
```

---

## `test` / `replay` — developer UI harness (desktop)

| Command | Role |
|---------|------|
| `test screenshot` | **`-o`** required; **`--wait-ms`** default **2000** |
| `replay --path` | New process: load session layout (for live UI use **`app replay`**) |

**Example**

```text
pm-image-cli test screenshot -o probe.png --wait-ms 3000
```

---

## `batch` — saved batch sessions

| Subcommand | Arguments |
|------------|-----------|
| `batch list` | — |
| `batch discard` | **`session-id`** |
| `batch resume` | **`session-id`** |

**Example**

```text
pm-image-cli batch list
pm-image-cli batch resume abc-123-session-id
```

---

## `license` (optional; some desktop installs)

When your product build includes offline activation:

- **`license fingerprint`** — print machine id for the licensing portal.
- **`license import <path>`** — install a license file from the issuer.
- **`license verify`** — confirm the installed license validates on this machine.

## `status` (optional; some desktop installs)

Read-only licensing or trial summary for support (use **`--json`** for machine-readable output). Behavior and availability depend on your SKU; use documentation from your vendor or administrator.

---

## Shared “image AI” settings rule

**Cross-platform** (not Windows-only): the same flags exist on every build that includes these subcommands.

For **`transform`**, **`create`**, **`meta`**, and **`find --llm`**: if **`--provider`** and/or **`--model`** are omitted, **`pm-image-cli`** fills them from the **Chat image provider / image model** (and provider default model) in app settings. If they remain empty, the command **fails** with a clear error. **`--api-key`** is optional when the key is already stored for that provider.

Full key waterfall and `settings.json` layout: [Settings & credentials](../settings.md).
