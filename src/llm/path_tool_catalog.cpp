#include "path_tool_catalog.hpp"
#include "agent_tools.hpp"
#include "llm/tools/computer_use/Tool_ComputerUse.hpp"

#include "llm_image_tool_defaults.hpp"
#include "replicate_provider_models_cli.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <vector>

namespace media::llm::path {

namespace {

nlohmann::json paths_array_schema() {
    return nlohmann::json{
        {"type", "array"},
        {"description", "Host file paths (absolute, or relative to the current Explorer folder "
                         "in-app; else process cwd). When the user says 'these files' without "
                         "naming any, the agent passes the current Explorer selection (or folder)."},
        {"items", {{"type", "string"}}},
        {"minItems", 1},
    };
}

ToolDef make_resize() {
    return ToolDef{
        "image_resize",
        "Resize / re-encode one or more images on disk (libvips). "
        "Writes the result next to each source file with a '_resized' suffix "
        "(or to options.out_dir when set). Returns one entry per input.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"paths", paths_array_schema()},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"out_dir",        {{"type", "string"}, {"description", "Output folder; empty = next to source"}}},
                        {"max_width",      {{"type", "integer"}, {"minimum", 0}}},
                        {"max_height",     {{"type", "integer"}, {"minimum", 0}}},
                        {"format",         {{"type", "string"}, {"enum", {"jpeg","jpg","png","webp",""}}}},
                        {"fit",            {{"type", "string"}, {"enum", {"inside","cover","contain","fill","outside"}}}},
                        {"position",       {{"type", "string"}, {"enum", {"centre","attention","entropy","low","high"}}}},
                        {"kernel",         {{"type", "string"}, {"enum", {"nearest","cubic","mitchell","lanczos2","lanczos3"}}}},
                        {"quality",        {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                        {"png_compression",{{"type", "integer"}, {"minimum", 0}, {"maximum", 9}}},
                        {"rotate",         {{"type", "integer"}, {"enum", {0,90,180,270}}}},
                        {"flip",           {{"type", "boolean"}}},
                        {"flop",           {{"type", "boolean"}}},
                        {"autorotate",     {{"type", "boolean"}}},
                        {"strip_metadata", {{"type", "boolean"}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"paths"})},
        }};
}

ToolDef make_compress() {
    return ToolDef{
        "image_compress",
        "Re-encode one or more images on disk with MozJPEG or optimised PNG. "
        "Writes the result next to each source file with '_compressed' (or to "
        "options.out_dir when set). Returns one entry per input.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"paths", paths_array_schema()},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"out_dir",         {{"type", "string"}}},
                        {"compressor",      {{"type", "string"}, {"enum", {"mozjpeg","jpeg","jpg","png","auto"}}}},
                        {"quality",         {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                        {"jpeg_quality",    {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                        {"progressive",     {{"type", "boolean"}}},
                        {"optimize_scans",  {{"type", "boolean"}}},
                        {"trellis",         {{"type", "boolean"}}},
                        {"level",           {{"type", "integer"}, {"minimum", 0}, {"maximum", 9}}},
                        {"png_level",       {{"type", "integer"}, {"minimum", 0}, {"maximum", 9}}},
                        {"strip_metadata",  {{"type", "boolean"}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"paths"})},
        }};
}

ToolDef make_transform() {
    return ToolDef{
        "image_transform",
        "AI image edit over one or more images on disk. Use "
        "this for ANY action verb that changes the image content: 'enhance', "
        "'edit', 'fix', 'improve', 'clean up', 'remove X', 'add Y', "
        "'restyle', 'colourise', 'beautify', 'make brighter', 'sharpen', "
        "'denoise'. The user's wording (or your light clarification) goes in "
        "options.prompt. Do NOT call image_meta first to 'see' the image \u2014 "
        "Gemini reads the bytes itself and a recon meta call is pure overhead. "
        "Writes the edited raster next to each source unless `output_path` (single input) or `output_paths` "
        "(one per input) is provided. "
        "**Do not** use this tool for Replicate **image-to-video** (MP4 from a selected photo): it always sends "
        "`image_input` + `output_format` in the shape of an **image** edit API, which fails validation on Wan, MiniMax, "
        "Kling-style i2v slugs (\u201cimage is required\u201d / wrong fields). For video-from-selection, call **create_video** "
        "with `frame_indices` [0] or [0,1]. Provider/key resolution falls back to app settings when omitted.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"paths", paths_array_schema()},
                {"output_path", {{"type", "string"},
                    {"description", "Optional exact output file path for a single input. Relative paths resolve to the current context folder."}}},
                {"output_paths", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "Optional exact output file paths for multiple inputs; length must match paths."},
                }},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"prompt",       {{"type", "string"}, {"description", "Editing prompt (required, e.g. 'remove the watermark')"}}},
                        {"provider",     {{"type", "string"}, {"enum", {"google","replicate"}}}},
                        {"model",        {{"type", "string"}, {"description", "Gemini model id or a Replicate **image-edit** slug from the catalog. For i2v video from `paths`, use **create_video** instead."}}},
                        {"api_key",      {{"type", "string"}}},
                        {"aspect_ratio", {{"type", "string"}, {"enum", {"1:1","16:9","4:3","3:4","9:16","21:9",""}}}},
                        {"image_size",   {{"type", "string"}, {"enum", {"512","1K","2K","4K",""}}}},
                        {"reference_images", {
                            {"type", "array"},
                            {"items", {{"type", "string"}}},
                            {"description", "Optional reference image paths (logo / brand sheet / style swatch)."},
                        }},
                    }},
                    {"required", nlohmann::json::array({"prompt"})},
                }},
            }},
            {"required", nlohmann::json::array({"paths","options"})},
        }};
}

ToolDef make_image_create() {
    return ToolDef{
        "image_create",
        "AI media generation on disk from a **text prompt** (and optional reference images). "
        "Use for 'generate', 'create an image', 'draw', 'imagine' when there is no full raster file "
        "to edit as the main target. **Provider, model, and API keys come from app Settings** (Chat \u2192 Image "
        "provider / model), not from tool arguments \u2014 same as **create_video**. "
        "Replicate can output video from prompt-only flows; for **image-to-video from selected files**, "
        "use **create_video** with `frame_indices`. "
        "There is no `paths` input file; output is written from the prompt alone, or pass "
        "**reference image paths** (options.reference_images, or `references` / `reference`) for style / layout / logo. "
        "`output_path` is optional; if omitted, a default filename is chosen from the prompt. "
        "To edit an existing file in place, use image_transform instead.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"output_path", {{"type", "string"},
                    {"description", "Optional exact output file path. Relative paths resolve to the current context folder. If omitted, a default name is chosen from the prompt."}}},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"prompt",       {{"type", "string"}, {"description", "Generation prompt (required)"}}},
                        {"aspect_ratio", {{"type", "string"}, {"enum", {"1:1","16:9","4:3","3:4","9:16","21:9",""}}}},
                        {"image_size",   {{"type", "string"}, {"enum", {"512","1K","2K","4K",""}}}},
                        {"resize_first", {{"type", "boolean"}}},
                        {"resize_width", {{"type", "integer"}, {"minimum", 64}, {"maximum", 4096}}},
                        {"preresize_raw_only", {{"type", "boolean"}}},
                        {"reference_images", {
                            {"type", "array"},
                            {"items", {{"type", "string"}}},
                            {"description", "Optional. Host paths to images used only as visual references (style, "
                                             "subjects, logo, color palette) — not a 'main' file to retouch. May mix "
                                             "user-named paths, list_images output, or Explorer selection. Also "
                                             "accepts the keys `references` (array) or `reference` (one string) "
                                             "in options, same as image_transform."},
                        }},
                        {"references", {
                            {"type", "array"},
                            {"items", {{"type", "string"}}},
                            {"description", "Alias of reference_images."},
                        }},
                        {"reference", {{"type", "string"}, {"description", "Single-path alias of reference_images."}}},
                    }},
                    {"required", nlohmann::json::array({"prompt"})},
                    {"additionalProperties", false},
                }},
            }},
            {"required", nlohmann::json::array({"options"})},
        }};
}

static nlohmann::json create_video_fallback_input_schema() {
    nlohmann::json props = nlohmann::json::object();
    props["prompt"] = {
        {"type", "string"},
        {"description", "Motion / camera / lighting; combine with the user\u2019s wording and context."},
    };
    return nlohmann::json{
        {"type", "object"},
        {"properties", std::move(props)},
        {"required", nlohmann::json::array({"prompt"})},
        {"additionalProperties", false},
    };
}

ToolDef make_create_video() {
    return ToolDef{
        "create_video",
        "Replicate video: **infer** arguments once from the user prompt, the runtime folder/selection block, and this tool\u2019s "
        "parameter schema. With two image fields, **[0]** \u2248 start still, **[1]** \u2248 end (use full paths). "
        "**Single-call rule:** issue **at most one** `create_video` per user request. **Never** call it again immediately after a "
        "failure to \u201cretry\u201d\u2014that spams Replicate and causes HTTP **429** throttling. If the result mentions throttling, "
        "429, or rate limit, **stop** and answer in plain text: ask the user to wait several minutes before trying again; "
        "do **not** emit another `create_video` unless they explicitly request a new attempt. "
        "The tool reply includes `results[0].result.intended_output_path` (and `output_path` when `ok`) so you can tell the user "
        "where the clip was written or would have been written even when Replicate fails; when present, `replicate_web_url` is "
        "Replicate\u2019s `urls.web` dashboard link for that run.",
        create_video_fallback_input_schema(),
    };
}

// image_meta removed from tool catalog — replaced by image_understand.
// do_meta() in path_tool_executor.cpp is kept for internal use (e.g. image_find sidecar generation).
// ToolDef make_meta() { ... }

ToolDef make_understand() {
    return ToolDef{
        "image_understand",
        "Ask the vision model a question about one or more images on disk. "
        "You MUST compose `prompt` as the exact question or instruction for the model — "
        "translate the user's intent into direct, specific language "
        "(e.g. 'Describe the scene and any people visible', "
        "'Compare these two images and list the key differences', "
        "'What text is readable in this photo?', "
        "'Is there a cat in this image?'). "
        "Use for ALL content questions: 'what is this', 'describe', 'caption', "
        "'compare A and B', 'is there a person', 'read the text'. "
        "Pass ALL relevant images in one call — the model sees them together, "
        "which is essential for comparisons. "
        "Provider and model are taken from Chat settings "
        "(image_recognition_provider / image_recognition_model); "
        "do NOT pass them as options. "
        "Does NOT write sidecars or update metadata — pure query, no side-effects.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"paths", paths_array_schema()},
                {"prompt", {
                    {"type", "string"},
                    {"description",
                        "Required. The exact vision question or instruction for the model. "
                        "Be specific and complete: write what you want the model to answer "
                        "or do with the image(s). For comparisons, mention that multiple "
                        "images are provided (e.g. 'Compare image 1 and image 2: …')."},
                }},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"resize_first", {{"type", "boolean"},
                            {"description", "Pre-resize images before upload (default true). "
                                            "Reduces cost and latency with no meaningful quality "
                                            "loss for most vision questions."}}},
                        {"resize_width", {{"type", "integer"}, {"minimum", 64}, {"maximum", 2048},
                            {"description", "Target width when resize_first is true (default 512)."}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"paths", "prompt"})},
        }};
}

ToolDef make_list_images() {
    return ToolDef{
        "list_images",
        "Enumerate image files inside one or more folders / globs. Use this "
        "BEFORE any other tool when the user asks 'what's in this folder' / "
        "'show me the photos here' / 'how many images do I have'. Filters by "
        "standard image extensions by default (jpg, jpeg, png, webp, tif, "
        "tiff, bmp, gif, avif, heic; plus arw, cr2, cr3, nef, dng, orf, rw2, "
        "raf, pef, nrw, srw, x3f, 3fr, mef, mrw when options.include_raw is "
        "true). Read-only — never writes to disk.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"inputs", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "Folder paths or globs (e.g. 'C:\\\\photos\\\\trip', 'C:\\\\photos\\\\**\\\\*.jpg'). When the user is browsing a folder, pass that folder."},
                }},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"recursive",    {{"type", "boolean"}, {"description", "Recurse into subfolders (default: true)"}}},
                        {"include_raw",  {{"type", "boolean"}, {"description", "Also include camera RAW formats (default: true)"}}},
                        {"max_results",  {{"type", "integer"}, {"minimum", 0},  {"description", "Cap; 0 = unlimited (default: 0)"}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"inputs"})},
        }};
}

ToolDef make_file_glob() {
    return ToolDef{
        "file_glob",
        "List non-sensitive text-friendly files matching a glob or directory (e.g. *.md, *.dxf, *.svg, *.csv). "
        "Patterns are resolved like other path tools: relative to the current Explorer folder in-app "
        "(or the first selected file's parent when there is no folder hint), otherwise the process cwd. "
        "Skips dev/build/cache directory names (node_modules, .git, target, CMakeFiles, … — same idea as "
        "pm-pics exclude-default), applies the same read policy as file_read, and drops raster image/binary/archive "
        "extensions so results are suitable for follow-up with file_read. Read-only; caps output with "
        "options.max_results (default 500, max 5000).",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"pattern", {{"type", "string"},
                             {"description", "Glob or path: e.g. '*.md', 'src/**/*.txt', or a folder path "
                                             "without wildcards (see options.recursive)."}}},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"max_results", {{"type", "integer"}, {"minimum", 1}, {"maximum", 5000},
                            {"description", "Maximum files returned (default: 500)."}}},
                        {"recursive", {{"type", "boolean"},
                            {"description", "When `pattern` resolves to a directory and has no * ? **, "
                                            "recurse into subfolders (default: true)."}}},
                        {"skip_dev_folders", {{"type", "boolean"},
                            {"description", "Skip node_modules, .git, build outputs, IDE caches, … (default: true)."}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"pattern"})},
        }};
}

ToolDef make_file_read() {
    return ToolDef{
        "file_read",
        "Read a UTF-8 text file from disk (logs, sidecars, configs, **ASCII** CAD/vector exchange such as .dxf or .svg). "
        "Same policy as the buffer `file_read` tool: skips binary-looking content, raster **image** extensions "
        "(use image_* for photos), over-size files, hidden paths, and sensitive locations. "
        "Relative `path` resolves to the current Explorer folder in-app (same as `write_file` and `file_glob`); "
        "absolute paths are accepted.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"},
                          {"description", "File path: absolute, or relative to the Explorer folder / "
                                          "selection base in-app."}}},
            }},
            {"required", nlohmann::json::array({"path"})},
        }};
}

// image_find removed from tool catalog — sidecar-based search is no longer exposed to the agent.
// do_find() in path_tool_executor.cpp is kept for internal/CLI use.
// ToolDef make_find() {
//     return ToolDef{
//         "image_find",
[[maybe_unused]] ToolDef make_find_disabled() {
    return ToolDef{
        "image_find",
        "Search images under `inputs` (files, folders, globs). `options.mode` = `name` for "
        "path/folder names, or `llm` (default) to match the query with **local** text on the "
        "file name, <stem>.md, <stem>.json, and EXIF (no per-image find:judge from this tool). "
        "If a file has no sidecar, the worker may run `image_meta` **once** to build `.md`/`.json` "
        "(Gemini[meta] — cataloguing, not a separate 'search' model). `reference_images` is for "
        "rare 'find by visual similarity' and uses the multimodal API. Returns `matches` sorted by score.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"inputs", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "File paths, directories, or globs (* ? **)."},
                }},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"prompt", {{"type", "string"}, {"description", "What to look for (required), e.g. a phrase or a filename fragment."}}},
                        {"mode",   {{"type", "string"},
                                    {"enum", {"name", "llm"}},
                                    {"description", "`llm` = local text match on metadata; `name` = path/folder substring. Omit in-app to default to `llm`."}}},
                        {"max_results", {{"type", "integer"}, {"minimum", 0},
                            {"description", "Cap the number of matches; 0 = no cap."}}},
                        {"reference_images", {
                            {"type", "array"},
                            {"items", {{"type", "string"}}},
                            {"description", "Optional. Paths to example images (llm mode) for visual similarity, same idea as image_transform."},
                        }},
                    }},
                    {"required", nlohmann::json::array({"prompt"})},
                }},
            }},
            {"required", nlohmann::json::array({"inputs","options"})},
        }};
}

ToolDef make_file_search() {
    return ToolDef{
        "file_search",
        "Search file contents (grep) or file names with a regex or literal pattern. "
        "Use for 'find all usages of X', 'which files contain Y', 'grep for Z'. "
        "output_mode controls what comes back: "
        "  \"content\" (default) — matching lines with path:line: prefix (like rg); "
        "  \"files_with_matches\" — file paths only; "
        "  \"count\" — file:N lines (match counts per file). "
        "Defaults: grep=true (content search), recursive=true, case-insensitive, head_limit=250. "
        "Set grep=false for filename-only search (matches against the filename, not the content). "
        "Use the `type` parameter for quick file-type filters (js, ts, cpp, py, go, rust, …) "
        "instead of writing out glob patterns. "
        "The `glob` parameter accepts patterns like *.cpp or *.{ts,tsx}. "
        "Read-only — never writes to disk.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"pattern", {
                    {"type", "string"},
                    {"description", "The regex or literal pattern to search for (required). "
                                    "Regex syntax: ECMAScript (std::regex). "
                                    "Use -i for case-insensitive. "
                                    "If pattern starts with '-', it is treated as a literal dash."},
                }},
                {"path", {
                    {"type", "string"},
                    {"description", "File or directory to search in. Defaults to the current Explorer folder / cwd."},
                }},
                {"glob", {
                    {"type", "string"},
                    {"description", "Filename glob filter, e.g. \"*.cpp\" or \"*.{ts,tsx}\". "
                                    "Space- or comma-separated; braced patterns are kept intact."},
                }},
                {"type", {
                    {"type", "string"},
                    {"description", "File type shorthand (rg --type equivalent): "
                                    "cpp, c, cs, css, go, html, java, js, json, kotlin, md, py, rs, ruby, rust, sh, swift, ts, toml, txt, xml, yaml. "
                                    "Expands to the matching include globs automatically."},
                }},
                {"output_mode", {
                    {"type", "string"},
                    {"enum", {"content", "files_with_matches", "count"}},
                    {"description", "content = matching lines (default); files_with_matches = file paths; count = file:N pairs."},
                }},
                {"grep", {
                    {"type", "boolean"},
                    {"description", "true (default) = search file contents; false = search filenames only."},
                }},
                {"-B", {{"type", "integer"}, {"minimum", 0},
                        {"description", "Context lines before each match (rg -B). Requires output_mode=content."}}},
                {"-A", {{"type", "integer"}, {"minimum", 0},
                        {"description", "Context lines after each match (rg -A). Requires output_mode=content."}}},
                {"-C", {{"type", "integer"}, {"minimum", 0},
                        {"description", "Symmetric context lines before AND after each match (rg -C). "
                                        "Takes priority over -B / -A when set."}}},
                {"-i", {{"type", "boolean"},
                        {"description", "Case-insensitive search (default: true)."}}},
                {"multiline", {{"type", "boolean"},
                               {"description", "Enable multiline mode: ^ and $ match at line boundaries (rg -U equivalent)."}}},
                {"head_limit", {
                    {"type", "integer"}, {"minimum", 0},
                    {"description", "Limit output to first N matches/lines/files. "
                                    "Default: 250. Pass 0 for unlimited (use sparingly — large results waste context)."},
                }},
                {"offset", {
                    {"type", "integer"}, {"minimum", 0},
                    {"description", "Skip first N entries before applying head_limit (pagination). Default: 0."},
                }},
            }},
            {"required", nlohmann::json::array({"pattern"})},
        }};
}

ToolDef make_camera() {
    return ToolDef{
        "image_from_camera",
        "Capture a still image from a connected webcam / camera and save it to disk.\n\n"
        "Workflow:\n"
        "  1. Call with action=\"list\" to discover available devices and their supported "
        "resolutions — do this first if you are unsure which camera or mode to use.\n"
        "  2. Call with action=\"capture\" (the default) to take the photo. The image is "
        "saved as a JPEG (or the format implied by output_path's extension). "
        "Without an explicit output_path the file lands in the current context folder "
        "named cam_YYYYMMDD_HHMMSS_mmm.jpg; if no folder is active it goes to the "
        "system temp directory.\n\n"
        "On Linux, if capture looks wrong, verify the camera with the native CLI: "
        "`pm-image video info` then `pm-image video image --dst /tmp/test.jpg` "
        "(same capture stack as this tool).\n\n"
        "Always report the saved path and captured_at timestamp to the user so they can "
        "track which photo was taken when.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"action", {
                    {"type", "string"},
                    {"enum", nlohmann::json::array({"capture", "list"})},
                    {"default", "capture"},
                    {"description",
                     "\"list\" — return all capture devices and their available resolutions. "
                     "\"capture\" (default) — take a still image and save it to disk."},
                }},
                {"device", {
                    {"type", "string"},
                    {"description",
                     "Case-insensitive substring of the camera's friendly name (e.g. \"Logitech\", \"FaceTime\"). "
                     "Omit or pass \"\" to use the first / default camera."},
                }},
                {"width", {
                    {"type", "integer"},
                    {"minimum", 0},
                    {"description",
                     "Preferred capture width in pixels. "
                     "0 or omit = highest available resolution (recommended for photos)."},
                }},
                {"height", {
                    {"type", "integer"},
                    {"minimum", 0},
                    {"description",
                     "Preferred capture height in pixels. "
                     "0 or omit = highest available resolution."},
                }},
                {"output_path", {
                    {"type", "string"},
                    {"description",
                     "Destination file path (.jpg, .png, or .bmp). "
                     "Relative paths resolve to the current context folder (same as other tools). "
                     "Omit to auto-generate a timestamped filename in the context folder "
                     "(or system temp when no folder is set)."},
                }},
            }},
            {"required", nlohmann::json::array()},
        }
    };
}

ToolDef make_write_file() {
    return ToolDef{
        "write_file",
        "Create, overwrite, or append a UTF-8 text file on disk (reports, notes, .md, "
        "CSV snippets). Use whenever the user asks to save, write, or export text to a "
        "file. Parent directories are created as needed. Relative paths resolve to the current "
        "Explorer folder in-app (same as other path tools; if no folder, the first selected file's "
        "directory; only then the process cwd). Do not claim a file was written without "
        "calling this tool first.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "Output path: absolute, or relative to the Explorer folder / selection base in-app (e.g. santa.md, sub/report.txt)."},
                }},
                {"content", {
                    {"type", "string"},
                    {"description", "Full file body in UTF-8. Use \"\\n\" for newlines in JSON string values."},
                }},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"append", {{"type", "boolean"},
                                    {"description", "If true, append to the file (creates the file if missing). Default false (truncate / create)."}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"path", "content"})},
        }};
}

void str_tolower_in_place(std::string& s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

void trim_in_place(std::string& s) {
    const auto* ws = " \t\r\n";
    const auto  b  = s.find_first_not_of(ws);
    if (b == std::string::npos) { s.clear(); return; }
    s.erase(0, b);
    const auto e = s.find_last_not_of(ws);
    s.erase(e + 1);
}

/// Replace **create_video** `input_schema` with JSON Schema derived from OpenAPI **Input** for the current Chat model.
void enrich_create_video_from_replicate_cache(std::vector<ToolDef>& tools) {
    for (ToolDef& t : tools) {
        if (t.name != "create_video")
            continue;
        std::string provider;
        std::string model;
        nlohmann::json empty_opts = nlohmann::json::object();
        media::llm::apply_chat_video_defaults_for_tool_options(provider, model, empty_opts);
        str_tolower_in_place(provider);
        if (provider != "replicate" || model.empty() || model.find('/') == std::string::npos)
            return;

        nlohmann::json flat;
        nlohmann::json required_arr;
        std::string    err;
        if (!media::replicate_cli::lookup_replicate_openapi_input_flat(model, flat, required_arr, err)) {
            t.description += "\n\n(OpenAPI Input for `" + model + "` not in local model cache: " + err
                + " \u2014 refresh models in Settings \u2192 Video.)";
            t.input_schema = create_video_fallback_input_schema();
            return;
        }

        t.input_schema = media::replicate_cli::openapi_flat_to_create_tool_parameters(flat, required_arr);
        t.description += "\n\n**Same single-call rule as above:** one `create_video` per user intent; no auto-retry after errors or 429.";
        return;
    }
}

// ── Scheduler tools ──────────────────────────────────────────────────────────

ToolDef make_schedule_at() {
    return ToolDef{
        "schedule_at",
        "Schedule a one-shot agent task to run at an exact UTC time. "
        "Use when the user says 'at 18:00', 'tomorrow morning', or gives an explicit time.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"title",         {{"type", "string"}, {"description", "Short human-readable name for the task."}}},
                {"prompt",        {{"type", "string"}, {"description", "The full prompt to run at fire time."}}},
                {"run_at",        {{"type", "string"}, {"description", "ISO 8601 UTC datetime, e.g. \"2026-05-10T18:30:00Z\"."}}},
                {"initial_state", {{"type", "object"}, {"description", "Optional initial memory_state JSON."}}},
                {"folder_hint",   {{"type", "string"}, {"description", "Optional folder context for the scheduled turn."}}},
            }},
            {"required", nlohmann::json::array({"title", "prompt", "run_at"})},
        }};
}

ToolDef make_schedule_in() {
    return ToolDef{
        "schedule_in",
        "Schedule a one-shot agent task to run after a delay. "
        "Use when the user says 'in 30 seconds', 'after 5 minutes', etc.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"title",          {{"type", "string"}}},
                {"prompt",         {{"type", "string"}}},
                {"delay_seconds",  {{"type", "integer"}, {"minimum", 1}, {"description", "Seconds from now until the task fires."}}},
                {"initial_state",  {{"type", "object"}}},
                {"folder_hint",    {{"type", "string"}}},
            }},
            {"required", nlohmann::json::array({"title", "prompt", "delay_seconds"})},
        }};
}

ToolDef make_schedule_every() {
    return ToolDef{
        "schedule_every",
        "Schedule a recurring agent task on a fixed interval. "
        "Use when the user says 'every 30 seconds', 'once per hour', 'monitor', or 'keep watching'.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"title",            {{"type", "string"}}},
                {"prompt",           {{"type", "string"}}},
                {"interval_seconds", {{"type", "integer"}, {"minimum", 5}, {"description", "Seconds between ticks."}}},
                {"start_at",         {{"type", "string"}, {"description", "Optional ISO 8601 UTC start time. Defaults to now + interval."}}},
                {"max_runs",         {{"type", "integer"}, {"minimum", 1}, {"description", "Optional cap on total runs; omit for unlimited."}}},
                {"initial_state",    {{"type", "object"}}},
                {"folder_hint",      {{"type", "string"}}},
            }},
            {"required", nlohmann::json::array({"title", "prompt", "interval_seconds"})},
        }};
}

ToolDef make_schedule_cancel() {
    return ToolDef{
        "schedule_cancel",
        "Cancel (disable) a scheduled task by its id. The task is kept in memory but will not fire again.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"task_id", {{"type", "string"}, {"description", "The task id returned by schedule_at / schedule_in / schedule_every."}}},
            }},
            {"required", nlohmann::json::array({"task_id"})},
        }};
}

ToolDef make_schedule_list() {
    return ToolDef{
        "schedule_list",
        "List all scheduled tasks (id, title, schedule, enabled, next_run_at, run_count).",
        nlohmann::json{
            {"type", "object"},
            {"properties", nlohmann::json::object()},
        }};
}

// ── Memory tools ─────────────────────────────────────────────────────────────

ToolDef make_memory_read() {
    return ToolDef{
        "memory_read",
        "Read the persistent JSON memory you have stored for this session (or scheduled task). "
        "Returns the state object last written with memory_write. "
        "Omit task_id to read the current session's memory.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"task_id", {{"type", "string"}, {"description", "Task id. Defaults to the current session."}}},
            }},
        }};
}

ToolDef make_memory_write() {
    return ToolDef{
        "memory_write",
        "Persist a compact JSON object as your long-term memory for this session. "
        "Use this whenever the user asks you to remember something, or when you want to store "
        "facts, preferences, names, or context that should survive across multiple turns. "
        "The stored object is injected into your system prompt on every future turn. "
        "Keep values small — store strings and numbers, not base64 or full file contents.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"state", {{"type", "object"}, {"description", "Full replacement for the memory state. Merge manually if you want to keep existing keys."}}},
            }},
            {"required", nlohmann::json::array({"state"})},
        }};
}

ToolDef make_memory_append_event() {
    return ToolDef{
        "memory_append_event",
        "Append a structured event record to the current session or task event log. "
        "Useful for logging milestones, decisions, or structured state transitions.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"event", {{"type", "object"}, {"description", "Arbitrary JSON object. Include at least a 'type' string."}}},
            }},
            {"required", nlohmann::json::array({"event"})},
        }};
}

} // namespace

ToolDef make_run() {
    return ToolDef{
        "run",
        "Execute a shell command in the user's environment and return combined stdout / stderr. "
        "Use for build commands, file operations, git, package managers, or any task not "
        "covered by a dedicated tool. The process inherits the current Explorer folder as cwd. "
        "Commands are validated against a security policy before execution; dangerous patterns "
        "(privilege escalation, download cradles, obfuscated commands) are blocked. "
        "Maximum execution time is 120 seconds.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"command", {
                    {"type", "string"},
                    {"description",
                     "The shell command to execute. Use platform-appropriate syntax "
                     "(PowerShell on Windows, bash/sh on Unix). Avoid interactive commands."},
                }},
                {"shell", {
                    {"type", "string"},
                    {"enum", {"auto", "bash", "sh", "pwsh", "cmd"}},
                    {"description",
                     "Shell to use. 'auto' (default) picks pwsh on Windows, bash on Unix. "
                     "'cmd' is Windows cmd.exe. Rarely needed."},
                }},
                {"timeout_ms", {
                    {"type", "integer"},
                    {"minimum", 1},
                    {"maximum", 120000},
                    {"description",
                     "Max wall-clock milliseconds before the process is killed. Default 30000 (30s). "
                     "Cap is 120000 (2 min)."},
                }},
            }},
            {"required", nlohmann::json::array({"command"})},
        }};
}

ToolDef make_speak() {
    return ToolDef{
        "speak",
        "Synthesise text to speech and play it on the default speaker "
        "Use when the user asks to say, announce, read aloud, narrate, or speak text. "
        "Playback is synchronous — the tool returns after the audio finishes playing.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"text", {
                    {"type", "string"},
                    {"description",
                     "The text to synthesise and play aloud. "
                     "Plain prose works best; keep under ~500 words for responsive playback. "
                     "Do not include SSML tags."},
                }},
            }},
            {"required", nlohmann::json::array({"text"})},
        }
    };
}

std::vector<ToolDef> tool_catalog() {
    std::vector<ToolDef> out = {make_list_images(), make_file_glob(), make_file_read(),
                                make_file_search(),
                                make_resize(),
                                make_transform(), make_image_create(), make_create_video(),
                                make_understand(), make_camera(), make_write_file(),
                                // Audio
                                make_speak(),
                                // Scheduler tools
                                make_schedule_at(), make_schedule_in(), make_schedule_every(),
                                make_schedule_cancel(), make_schedule_list(),
                                // Memory tools
                                make_memory_read(), make_memory_write(), make_memory_append_event(),
                                // Shell execution
                                make_run()};
    const auto computer_tools = media::llm::computer_use::tool_defs();
    out.insert(out.end(), computer_tools.begin(), computer_tools.end());
    // image_meta and image_find removed: make_meta() → replaced by make_understand()
    //                                    make_find() → commented out above
    enrich_create_video_from_replicate_cache(out);
    return out;
}

std::vector<ToolDef> tool_catalog_excluding(const std::vector<std::string>& disabled) {
    if (disabled.empty()) return tool_catalog();
    std::unordered_set<std::string> dset;
    dset.reserve(disabled.size() * 2);
    for (const auto& d : disabled) {
        if (d.empty()) continue;
        std::string k = d;
        trim_in_place(k);
        str_tolower_in_place(k);
        if (!k.empty()) dset.insert(std::move(k));
    }
    if (dset.empty()) return tool_catalog();
    std::vector<ToolDef> out;
    for (const auto& t : tool_catalog()) {
        std::string nm = t.name;
        str_tolower_in_place(nm);
        if (dset.count(nm)) continue;
        out.push_back(t);
    }
    return out;
}

nlohmann::json tool_catalog_json() {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tool_catalog()) {
        arr.push_back({
            {"name",         t.name},
            {"description",  t.description},
            {"input_schema", t.input_schema},
        });
    }
    return nlohmann::json{{"tools", arr}};
}

nlohmann::json tool_catalog_openai() {
    // OpenAI Chat Completions `tools[]` parameter shape.
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : tool_catalog()) {
        arr.push_back({
            {"type", "function"},
            {"function", {
                {"name",        t.name},
                {"description", t.description},
                {"parameters",  t.input_schema},
            }},
        });
    }
    return arr;
}

nlohmann::json tool_catalog_openai_excluding(const std::vector<std::string>& disabled) {
    nlohmann::json       arr = nlohmann::json::array();
    for (const auto& t : tool_catalog_excluding(disabled)) {
        arr.push_back({
            {"type", "function"},
            {"function", {
                {"name",        t.name},
                {"description", t.description},
                {"parameters",  t.input_schema},
            }},
        });
    }
    return arr;
}

// ── Flag-based helpers ────────────────────────────────────────────────────────
//
// k_tool_flag_table removed — use pm::llm::agent_tool_registry_data() instead.

std::vector<std::string> agent_tools_disabled_names(pm::llm::AgentTools flags) {
    std::vector<std::string> out;
    const auto* reg  = pm::llm::agent_tool_registry_data();
    const auto  size = pm::llm::agent_tool_registry_size();
    for (std::size_t i = 0; i < size; ++i) {
        if (!pm::llm::agent_tool_enabled(flags, reg[i].flag))
            out.emplace_back(reg[i].name);
    }
    return out;
}

nlohmann::json tool_catalog_openai_for_flags(pm::llm::AgentTools flags) {
    return tool_catalog_openai_excluding(agent_tools_disabled_names(flags));
}

std::vector<ToolDef> tool_catalog_for_flags(pm::llm::AgentTools flags) {
    return tool_catalog_excluding(agent_tools_disabled_names(flags));
}

} // namespace media::llm::path
