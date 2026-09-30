# PM-Image agent tools (LLM brief)

Path-mode tools sent to the in-app chat agent (`llm agent`). For shell/CLI subcommands see [commands.md](commands.md).

**Guidelines:**

- Relative paths resolve against the current Explorer folder (or process cwd).
- Use **file_read** / **file_glob** / **write_file** for text and structured files (`.md`, `.json`, `.csv`, `.dxf`, `.svg`, logs).
- Use **list_images** and **image_*** tools for raster photos.
- Call the tool that matches intent; do not narrate without invoking tools.

---

## File

### `file_glob`

List non-sensitive text-friendly files matching a glob or directory (e.g. *.md, *.dxf, *.svg, *.csv). Patterns are resolved like other path tools: relative to the current Explorer folder in-app (or the first selected file's parent when there is no folder hint), otherwise the process cwd. Skips dev/build/cache directory names (node_modules, .git, target, CMakeFiles, … — same idea as pm-pics exclude-default), applies the same read policy as file_read, and drops raster image/binary/archive extensions so results are suitable for follow-up with file_read. Read-only; caps output with options.max_results (default 500, max 5000).

**Parameters:**

- `options` (object, optional)
- `options.max_results` (integer, optional) — Maximum files returned (default: 500).
- `options.recursive` (boolean, optional) — When `pattern` resolves to a directory and has no * ? **, recurse into subfolders (default: true).
- `options.skip_dev_folders` (boolean, optional) — Skip node_modules, .git, build outputs, IDE caches, … (default: true).
- `pattern` (string, required) — Glob or path: e.g. '*.md', 'src/**/*.txt', or a folder path without wildcards (see options.recursive).

### `file_read`

Read a UTF-8 text file from disk (logs, sidecars, configs, **ASCII** CAD/vector exchange such as .dxf or .svg). Same policy as the buffer `file_read` tool: skips binary-looking content, raster **image** extensions (use image_* for photos), over-size files, hidden paths, and sensitive locations. Relative `path` resolves to the current Explorer folder in-app (same as `write_file` and `file_glob`); absolute paths are accepted.

**Parameters:**

- `path` (string, required) — File path: absolute, or relative to the Explorer folder / selection base in-app.

### `file_search`

Search file contents (grep) or file names with a regex or literal pattern. Use for 'find all usages of X', 'which files contain Y', 'grep for Z'. output_mode controls what comes back:   "content" (default) — matching lines with path:line: prefix (like rg);   "files_with_matches" — file paths only;   "count" — file:N lines (match counts per file). Defaults: grep=true (content search), recursive=true, case-insensitive, head_limit=250. Set grep=false for filename-only search (matches against the filename, not the content). Use the `type` parameter for quick file-type filters (js, ts, cpp, py, go, rust, …) instead of writing out glob patterns. The `glob` parameter accepts patterns like *.cpp or *.{ts,tsx}. Read-only — never writes to disk.

**Parameters:**

- `-A` (integer, optional) — Context lines after each match (rg -A). Requires output_mode=content.
- `-B` (integer, optional) — Context lines before each match (rg -B). Requires output_mode=content.
- `-C` (integer, optional) — Symmetric context lines before AND after each match (rg -C). Takes priority over -B / -A when set.
- `-i` (boolean, optional) — Case-insensitive search (default: true).
- `glob` (string, optional) — Filename glob filter, e.g. "*.cpp" or "*.{ts,tsx}". Space- or comma-separated; braced patterns are kept intact.
- `grep` (boolean, optional) — true (default) = search file contents; false = search filenames only.
- `head_limit` (integer, optional) — Limit output to first N matches/lines/files. Default: 250. Pass 0 for unlimited (use sparingly — large results waste context).
- `multiline` (boolean, optional) — Enable multiline mode: ^ and $ match at line boundaries (rg -U equivalent).
- `offset` (integer, optional) — Skip first N entries before applying head_limit (pagination). Default: 0.
- `output_mode` (string, optional, enum: `"content"`, `"files_with_matches"`, `"count"`) — content = matching lines (default); files_with_matches = file paths; count = file:N pairs.
- `path` (string, optional) — File or directory to search in. Defaults to the current Explorer folder / cwd.
- `pattern` (string, required) — The regex or literal pattern to search for (required). Regex syntax: ECMAScript (std::regex). Use -i for case-insensitive. If pattern starts with '-', it is treated as a literal dash.
- `type` (string, optional) — File type shorthand (rg --type equivalent): cpp, c, cs, css, go, html, java, js, json, kotlin, md, py, rs, ruby, rust, sh, swift, ts, toml, txt, xml, yaml. Expands to the matching include globs automatically.

### `list_images`

Enumerate image files inside one or more folders / globs. Use this BEFORE any other tool when the user asks 'what's in this folder' / 'show me the photos here' / 'how many images do I have'. Filters by standard image extensions by default (jpg, jpeg, png, webp, tif, tiff, bmp, gif, avif, heic; plus arw, cr2, cr3, nef, dng, orf, rw2, raf, pef, nrw, srw, x3f, 3fr, mef, mrw when options.include_raw is true). Read-only — never writes to disk.

**Parameters:**

- `inputs` (array, required) — Folder paths or globs (e.g. 'C:\\photos\\trip', 'C:\\photos\\**\\*.jpg'). When the user is browsing a folder, pass that folder.
- `options` (object, optional)
- `options.include_raw` (boolean, optional) — Also include camera RAW formats (default: true)
- `options.max_results` (integer, optional) — Cap; 0 = unlimited (default: 0)
- `options.recursive` (boolean, optional) — Recurse into subfolders (default: true)

## Image

### `create_video`

Replicate video: **infer** arguments once from the user prompt, the runtime folder/selection block, and this tool’s parameter schema. With two image fields, **[0]** ≈ start still, **[1]** ≈ end (use full paths). **Single-call rule:** issue **at most one** `create_video` per user request. **Never** call it again immediately after a failure to “retry”—that spams Replicate and causes HTTP **429** throttling. If the result mentions throttling, 429, or rate limit, **stop** and answer in plain text: ask the user to wait several minutes before trying again; do **not** emit another `create_video` unless they explicitly request a new attempt. The tool reply includes `results[0].result.intended_output_path` (and `output_path` when `ok`) so you can tell the user where the clip was written or would have been written even when Replicate fails; when present, `replicate_web_url` is Replicate’s `urls.web` dashboard link for that run.

**Same single-call rule as above:** one `create_video` per user intent; no auto-retry after errors or 429.

**Parameters:**

- `aspect_ratio` (string, optional, enum: `"16:9"`, `"9:16"`, `"1:1"`) — An enumeration.
- `duration` (integer, optional) — Video duration in seconds.
- `end_image` (string, optional) — Last frame image. Requires start_image. Supports .jpg/.jpeg/.png, max 10MB, min 300px.
- `generate_audio` (boolean, optional) — Generate native audio for the video.
- `mode` (string, optional, enum: `"standard"`, `"pro"`, `"4k"`) — An enumeration.
- `multi_prompt` (string, optional) — JSON array of shot definitions for multi-shot mode. Each shot: {"prompt": "...", "duration": N}. Max 6 shots, min 1s per shot, total must equal duration.
- `negative_prompt` (string, optional) — Things you do not want to see in the video. Max 2500 characters.
- `prompt` (string, required) — Text prompt for video generation. Max 2500 characters.
- `start_image` (string, optional) — First frame image. Supports .jpg/.jpeg/.png, max 10MB, min 300px, aspect ratio 1:2.5 to 2.5:1.

### `image_create`

AI media generation on disk from a **text prompt** (and optional reference images). Use for 'generate', 'create an image', 'draw', 'imagine' when there is no full raster file to edit as the main target. **Provider, model, and API keys come from app Settings** (Chat → Image provider / model), not from tool arguments — same as **create_video**. Replicate can output video from prompt-only flows; for **image-to-video from selected files**, use **create_video** with `frame_indices`. There is no `paths` input file; output is written from the prompt alone, or pass **reference image paths** (options.reference_images, or `references` / `reference`) for style / layout / logo. `output_path` is optional; if omitted, a default filename is chosen from the prompt. To edit an existing file in place, use image_transform instead.

**Parameters:**

- `options` (object, required)
- `options.aspect_ratio` (string, optional, enum: `"1:1"`, `"16:9"`, `"4:3"`, `"3:4"`, `"9:16"`, `"21:9"`)
- `options.image_size` (string, optional, enum: `"512"`, `"1K"`, `"2K"`, `"4K"`)
- `options.preresize_raw_only` (boolean, optional)
- `options.prompt` (string, optional) — Generation prompt (required)
- `options.reference` (string, optional) — Single-path alias of reference_images.
- `options.reference_images` (array, optional) — Optional. Host paths to images used only as visual references (style, subjects, logo, color palette) — not a 'main' file to retouch. May mix user-named paths, list_images output, or Explorer selection. Also accepts the keys `references` (array) or `reference` (one string) in options, same as image_transform.
- `options.references` (array, optional) — Alias of reference_images.
- `options.resize_first` (boolean, optional)
- `options.resize_width` (integer, optional)
- `output_path` (string, optional) — Optional exact output file path. Relative paths resolve to the current context folder. If omitted, a default name is chosen from the prompt.

### `image_from_camera`

Capture a still image from a connected webcam / camera and save it to disk.

Workflow:
  1. Call with action="list" to discover available devices and their supported resolutions — do this first if you are unsure which camera or mode to use.
  2. Call with action="capture" (the default) to take the photo. The image is saved as a JPEG (or the format implied by output_path's extension). Without an explicit output_path the file lands in the current context folder named cam_YYYYMMDD_HHMMSS_mmm.jpg; if no folder is active it goes to the system temp directory.

On Linux, if capture looks wrong, verify the camera with the native CLI: `pm-image video info` then `pm-image video image --dst /tmp/test.jpg` (same capture stack as this tool).

Always report the saved path and captured_at timestamp to the user so they can track which photo was taken when.

**Parameters:**

- `action` (string, optional, enum: `"capture"`, `"list"`) — "list" — return all capture devices and their available resolutions. "capture" (default) — take a still image and save it to disk.
- `device` (string, optional) — Case-insensitive substring of the camera's friendly name (e.g. "Logitech", "FaceTime"). Omit or pass "" to use the first / default camera.
- `height` (integer, optional) — Preferred capture height in pixels. 0 or omit = highest available resolution.
- `output_path` (string, optional) — Destination file path (.jpg, .png, or .bmp). Relative paths resolve to the current context folder (same as other tools). Omit to auto-generate a timestamped filename in the context folder (or system temp when no folder is set).
- `width` (integer, optional) — Preferred capture width in pixels. 0 or omit = highest available resolution (recommended for photos).

### `image_resize`

Resize / re-encode one or more images on disk (libvips). Writes the result next to each source file with a '_resized' suffix (or to options.out_dir when set). Returns one entry per input.

**Parameters:**

- `options` (object, optional)
- `options.autorotate` (boolean, optional)
- `options.fit` (string, optional, enum: `"inside"`, `"cover"`, `"contain"`, `"fill"`, `"outside"`)
- `options.flip` (boolean, optional)
- `options.flop` (boolean, optional)
- `options.format` (string, optional, enum: `"jpeg"`, `"jpg"`, `"png"`, `"webp"`)
- `options.kernel` (string, optional, enum: `"nearest"`, `"cubic"`, `"mitchell"`, `"lanczos2"`, `"lanczos3"`)
- `options.max_height` (integer, optional)
- `options.max_width` (integer, optional)
- `options.out_dir` (string, optional) — Output folder; empty = next to source
- `options.png_compression` (integer, optional)
- `options.position` (string, optional, enum: `"centre"`, `"attention"`, `"entropy"`, `"low"`, `"high"`)
- `options.quality` (integer, optional)
- `options.rotate` (integer, optional, enum: `0`, `90`, `180`, `270`)
- `options.strip_metadata` (boolean, optional)
- `paths` (array, required) — Host file paths (absolute, or relative to the current Explorer folder in-app; else process cwd). When the user says 'these files' without naming any, the agent passes the current Explorer selection (or folder).

### `image_transform`

AI image edit over one or more images on disk. Use this for ANY action verb that changes the image content: 'enhance', 'edit', 'fix', 'improve', 'clean up', 'remove X', 'add Y', 'restyle', 'colourise', 'beautify', 'make brighter', 'sharpen', 'denoise'. The user's wording (or your light clarification) goes in options.prompt. Do NOT call image_meta first to 'see' the image — Gemini reads the bytes itself and a recon meta call is pure overhead. Writes the edited raster next to each source unless `output_path` (single input) or `output_paths` (one per input) is provided. **Do not** use this tool for Replicate **image-to-video** (MP4 from a selected photo): it always sends `image_input` + `output_format` in the shape of an **image** edit API, which fails validation on Wan, MiniMax, Kling-style i2v slugs (“image is required” / wrong fields). For video-from-selection, call **create_video** with `frame_indices` [0] or [0,1]. Provider/key resolution falls back to app settings when omitted.

**Parameters:**

- `options` (object, required)
- `options.api_key` (string, optional)
- `options.aspect_ratio` (string, optional, enum: `"1:1"`, `"16:9"`, `"4:3"`, `"3:4"`, `"9:16"`, `"21:9"`)
- `options.image_size` (string, optional, enum: `"512"`, `"1K"`, `"2K"`, `"4K"`)
- `options.model` (string, optional) — Gemini model id or a Replicate **image-edit** slug from the catalog. For i2v video from `paths`, use **create_video** instead.
- `options.prompt` (string, optional) — Editing prompt (required, e.g. 'remove the watermark')
- `options.provider` (string, optional, enum: `"google"`, `"replicate"`)
- `options.reference_images` (array, optional) — Optional reference image paths (logo / brand sheet / style swatch).
- `output_path` (string, optional) — Optional exact output file path for a single input. Relative paths resolve to the current context folder.
- `output_paths` (array, optional) — Optional exact output file paths for multiple inputs; length must match paths.
- `paths` (array, required) — Host file paths (absolute, or relative to the current Explorer folder in-app; else process cwd). When the user says 'these files' without naming any, the agent passes the current Explorer selection (or folder).

### `image_understand`

Ask the vision model a question about one or more images on disk. You MUST compose `prompt` as the exact question or instruction for the model — translate the user's intent into direct, specific language (e.g. 'Describe the scene and any people visible', 'Compare these two images and list the key differences', 'What text is readable in this photo?', 'Is there a cat in this image?'). Use for ALL content questions: 'what is this', 'describe', 'caption', 'compare A and B', 'is there a person', 'read the text'. Pass ALL relevant images in one call — the model sees them together, which is essential for comparisons. Provider and model are taken from Chat settings (image_recognition_provider / image_recognition_model); do NOT pass them as options. Does NOT write sidecars or update metadata — pure query, no side-effects.

**Parameters:**

- `options` (object, optional)
- `options.resize_first` (boolean, optional) — Pre-resize images before upload (default true). Reduces cost and latency with no meaningful quality loss for most vision questions.
- `options.resize_width` (integer, optional) — Target width when resize_first is true (default 512).
- `paths` (array, required) — Host file paths (absolute, or relative to the current Explorer folder in-app; else process cwd). When the user says 'these files' without naming any, the agent passes the current Explorer selection (or folder).
- `prompt` (string, required) — Required. The exact vision question or instruction for the model. Be specific and complete: write what you want the model to answer or do with the image(s). For comparisons, mention that multiple images are provided (e.g. 'Compare image 1 and image 2: …').

## Utility

### `run`

Execute a shell command in the user's environment and return combined stdout / stderr. Use for build commands, file operations, git, package managers, or any task not covered by a dedicated tool. The process inherits the current Explorer folder as cwd. Commands are validated against a security policy before execution; dangerous patterns (privilege escalation, download cradles, obfuscated commands) are blocked. Maximum execution time is 120 seconds.

**Parameters:**

- `command` (string, required) — The shell command to execute. Use platform-appropriate syntax (PowerShell on Windows, bash/sh on Unix). Avoid interactive commands.
- `shell` (string, optional, enum: `"auto"`, `"bash"`, `"sh"`, `"pwsh"`, `"cmd"`) — Shell to use. 'auto' (default) picks pwsh on Windows, bash on Unix. 'cmd' is Windows cmd.exe. Rarely needed.
- `timeout_ms` (integer, optional) — Max wall-clock milliseconds before the process is killed. Default 30000 (30s). Cap is 120000 (2 min).

### `speak`

Synthesise text to speech and play it on the default speaker Use when the user asks to say, announce, read aloud, narrate, or speak text. Playback is synchronous — the tool returns after the audio finishes playing.

**Parameters:**

- `text` (string, required) — The text to synthesise and play aloud. Plain prose works best; keep under ~500 words for responsive playback. Do not include SSML tags.

### `write_file`

Create, overwrite, or append a UTF-8 text file on disk (reports, notes, .md, CSV snippets). Use whenever the user asks to save, write, or export text to a file. Parent directories are created as needed. Relative paths resolve to the current Explorer folder in-app (same as other path tools; if no folder, the first selected file's directory; only then the process cwd). Do not claim a file was written without calling this tool first.

**Parameters:**

- `content` (string, required) — Full file body in UTF-8. Use "\n" for newlines in JSON string values.
- `options` (object, optional)
- `options.append` (boolean, optional) — If true, append to the file (creates the file if missing). Default false (truncate / create).
- `path` (string, required) — Output path: absolute, or relative to the Explorer folder / selection base in-app (e.g. santa.md, sub/report.txt).

## Scheduler

### `schedule_at`

Schedule a one-shot agent task to run at an exact UTC time. Use when the user says 'at 18:00', 'tomorrow morning', or gives an explicit time.

**Parameters:**

- `folder_hint` (string, optional) — Optional folder context for the scheduled turn.
- `initial_state` (object, optional) — Optional initial memory_state JSON.
- `prompt` (string, required) — The full prompt to run at fire time.
- `run_at` (string, required) — ISO 8601 UTC datetime, e.g. "2026-05-10T18:30:00Z".
- `title` (string, required) — Short human-readable name for the task.

### `schedule_cancel`

Cancel (disable) a scheduled task by its id. The task is kept in memory but will not fire again.

**Parameters:**

- `task_id` (string, required) — The task id returned by schedule_at / schedule_in / schedule_every.

### `schedule_every`

Schedule a recurring agent task on a fixed interval. Use when the user says 'every 30 seconds', 'once per hour', 'monitor', or 'keep watching'.

**Parameters:**

- `folder_hint` (string, optional)
- `initial_state` (object, optional)
- `interval_seconds` (integer, required) — Seconds between ticks.
- `max_runs` (integer, optional) — Optional cap on total runs; omit for unlimited.
- `prompt` (string, required)
- `start_at` (string, optional) — Optional ISO 8601 UTC start time. Defaults to now + interval.
- `title` (string, required)

### `schedule_in`

Schedule a one-shot agent task to run after a delay. Use when the user says 'in 30 seconds', 'after 5 minutes', etc.

**Parameters:**

- `delay_seconds` (integer, required) — Seconds from now until the task fires.
- `folder_hint` (string, optional)
- `initial_state` (object, optional)
- `prompt` (string, required)
- `title` (string, required)

### `schedule_list`

List all scheduled tasks (id, title, schedule, enabled, next_run_at, run_count).

## Memory

### `memory_append_event`

Append a structured event record to the current session or task event log. Useful for logging milestones, decisions, or structured state transitions.

**Parameters:**

- `event` (object, required) — Arbitrary JSON object. Include at least a 'type' string.

### `memory_read`

Read the persistent JSON memory you have stored for this session (or scheduled task). Returns the state object last written with memory_write. Omit task_id to read the current session's memory.

**Parameters:**

- `task_id` (string, optional) — Task id. Defaults to the current session.

### `memory_write`

Persist a compact JSON object as your long-term memory for this session. Use this whenever the user asks you to remember something, or when you want to store facts, preferences, names, or context that should survive across multiple turns. The stored object is injected into your system prompt on every future turn. Keep values small — store strings and numbers, not base64 or full file contents.

**Parameters:**

- `state` (object, required) — Full replacement for the memory state. Merge manually if you want to keep existing keys.

