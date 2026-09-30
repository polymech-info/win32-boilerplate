#include "tool_catalog.hpp"
//
// In-buffer tools (REST /tools/call, file_read). Disk glob / Explorer-relative tools live in
// path_tool_catalog (`file_glob`, list_images, …) and path_tool_executor.

namespace media::llm {

namespace {

// ── Shared sub-schemas ──────────────────────────────────────────────────────

nlohmann::json image_input_schema() {
    return nlohmann::json{
        {"type", "object"},
        {"description", "Input image bytes."},
        {"properties", {
            {"mime", {{"type", "string"}, {"description", "MIME type (e.g. image/png, image/jpeg)"}}},
            {"b64",  {{"type", "string"}, {"description", "Base64-encoded image bytes"}}},
        }},
        {"required", nlohmann::json::array({"b64"})},
    };
}

ToolDef make_resize() {
    return ToolDef{
        "image_resize",
        "Resize / re-encode an image (libvips). Returns the encoded bytes inline.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"image", image_input_schema()},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"max_width",          {{"type", "integer"}, {"minimum", 0},   {"description", "Target / max width (0 = unconstrained)"}}},
                        {"max_height",         {{"type", "integer"}, {"minimum", 0},   {"description", "Target / max height (0 = unconstrained)"}}},
                        {"format",             {{"type", "string"}, {"enum", {"jpeg","jpg","png","webp",""}}, {"description", "Output container; empty = jpeg"}}},
                        {"fit",                {{"type", "string"}, {"enum", {"inside","cover","contain","fill","outside"}}}},
                        {"position",           {{"type", "string"}, {"enum", {"centre","attention","entropy","low","high"}}}},
                        {"kernel",             {{"type", "string"}, {"enum", {"nearest","cubic","mitchell","lanczos2","lanczos3"}}}},
                        {"quality",            {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"description", "JPEG/WebP/AVIF quality"}}},
                        {"png_compression",    {{"type", "integer"}, {"minimum", 0}, {"maximum", 9}}},
                        {"rotate",             {{"type", "integer"}, {"enum", {0,90,180,270}}}},
                        {"flip",               {{"type", "boolean"}}},
                        {"flop",               {{"type", "boolean"}}},
                        {"autorotate",         {{"type", "boolean"}}},
                        {"strip_metadata",     {{"type", "boolean"}}},
                        {"without_enlargement",{{"type", "boolean"}}},
                        {"background",         {{"type", "string"}, {"description", "Letterbox colour (#rrggbb) for fit=contain"}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"image"})},
        }};
}

ToolDef make_compress() {
    return ToolDef{
        "image_compress",
        "Re-encode an image with MozJPEG or optimised PNG. Returns the encoded bytes inline.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"image", image_input_schema()},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"compressor",                 {{"type", "string"}, {"enum", {"mozjpeg","jpeg","jpg","png","auto"}}}},
                        {"quality",                    {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"description", "MozJPEG quality"}}},
                        {"jpeg_quality",               {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}},
                        {"progressive",                {{"type", "boolean"}}},
                        {"jpeg_progressive",           {{"type", "boolean"}}},
                        {"optimize_scans",             {{"type", "boolean"}}},
                        {"trellis",                    {{"type", "boolean"}}},
                        {"trellis_quant",              {{"type", "boolean"}}},
                        {"overshoot_deringing",        {{"type", "boolean"}}},
                        {"level",                      {{"type", "integer"}, {"minimum", 0}, {"maximum", 9}, {"description", "PNG DEFLATE 0-9"}}},
                        {"png_level",                  {{"type", "integer"}, {"minimum", 0}, {"maximum", 9}}},
                        {"strip_metadata",             {{"type", "boolean"}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"image"})},
        }};
}

ToolDef make_transform() {
    return ToolDef{
        "image_transform",
        "AI image edit. Applies a prompt to an input image, returns the image inline. ",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"image", image_input_schema()},
                {"references", {
                    {"type", "array"},
                    {"description", "Optional reference images (logo / brand sheet / style swatch)."},
                    {"items", image_input_schema()},
                }},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"prompt",       {{"type", "string"}, {"description", "Editing prompt (required)"}}},
                        {"provider",     {{"type", "string"}, {"enum", {"google"}}}},
                        {"model",        {{"type", "string"}, {"description", "e.g. gemini-3-pro-image-preview"}}},
                        {"api_key",      {{"type", "string"}, {"description", "Optional"}}},
                        {"aspect_ratio", {{"type", "string"}, {"enum", {"1:1","16:9","4:3","3:4","9:16","21:9",""}}}},
                        {"image_size",   {{"type", "string"}, {"enum", {"512","1K","2K","4K",""}}}},
                    }},
                    {"required", nlohmann::json::array({"prompt"})},
                }},
            }},
            {"required", nlohmann::json::array({"image","options"})},
        }};
}

ToolDef make_image_create() {
    return ToolDef{
        "image_create",
        "AI text-to-image (this in-buffer schema: Google). There is no main input image; the model generates from "
        "options.prompt. You may optionally pass `references` (base64+mime) so the model can follow "
        "style, layout, logo, or subject likeness — same role as reference images on image_transform. "
        "Returns generated PNG inline. Same env key as image_transform. "
        "Note: in **pm-image chat**, the path-scoped `image_create` tool also supports **Replicate** "
        "(provider + model from the app catalog), including **text-to-video** and other video outputs — "
        "see path tool descriptions in the agent prompt; do not claim the product cannot generate video when Replicate is configured.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"references", {
                    {"type", "array"},
                    {"description", "Optional. Inlined images used only as visual references (not the image to edit)."},
                    {"items", image_input_schema()},
                }},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"prompt",       {{"type", "string"}, {"description", "Generation prompt (required)"}}},
                        {"provider",     {{"type", "string"}, {"enum", {"google"}}}},
                        {"model",        {{"type", "string"}, {"description", "e.g. gemini-3-pro-image-preview"}}},
                        {"api_key",      {{"type", "string"}, {"description", "Optional"}}},
                        {"aspect_ratio", {{"type", "string"}, {"enum", {"1:1","16:9","4:3","3:4","9:16","21:9",""}}}},
                        {"image_size",   {{"type", "string"}, {"enum", {"512","1K","2K","4K",""}}}},
                    }},
                    {"required", nlohmann::json::array({"prompt"})},
                }},
            }},
            {"required", nlohmann::json::array({"options"})},
        }};
}

ToolDef make_meta() {
    return ToolDef{
        "image_meta",
        "Generate a structured description (alt, description, tags, …) for an image using Google Gemini. "
        "Returns JSON inline.",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"image", image_input_schema()},
                {"options", {
                    {"type", "object"},
                    {"properties", {
                        {"provider",     {{"type", "string"}, {"enum", {"google"}}}},
                        {"model",        {{"type", "string"}, {"description", "e.g. gemini-3-pro-image-preview"}}},
                        {"api_key",      {{"type", "string"}}},
                        {"prompt",       {{"type", "string"}, {"description", "Override the cataloguer prompt entirely (rare; prefer user_query)"}}},
                        {"user_query",   {{"type", "string"}, {"description", "Optional: the user's actual question. Augments the default cataloguer prompt so response.description leads with the answer."}}},
                        {"resize_first", {{"type", "boolean"}}},
                        {"resize_width", {{"type", "integer"}, {"minimum", 64}, {"maximum", 4096}}},
                        {"dry_run",      {{"type", "boolean"}, {"description", "Validate options + run the in-memory resize, no API call"}}},
                    }},
                }},
            }},
            {"required", nlohmann::json::array({"image"})},
        }};
}

ToolDef make_find() {
    return ToolDef{
        "image_find",
        "Search by path/glob. Like path: `prompt`, `mode` (name|llm), `max_results`, optional "
        "`reference_images` (multimodal). Local text match on filename + sidecars + EXIF; may "
        "auto-generate missing sidecar (Gemini[meta] per file that needs it).",
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
                        {"prompt", {{"type", "string"}, {"description", "What to look for (required)."}}},
                        {"mode",   {{"type", "string"}, {"enum", {"name", "llm"}}}},
                        {"max_results", {{"type", "integer"}, {"minimum", 0}}},
                        {"reference_images", {
                            {"type", "array"},
                            {"items", {{"type", "string"}}},
                            {"description", "Optional example image paths (llm mode), like image_transform."},
                        }},
                    }},
                    {"required", nlohmann::json::array({"prompt"})},
                }},
            }},
            {"required", nlohmann::json::array({"inputs","options"})},
        }};
}

ToolDef make_file_read() {
    return ToolDef{
        "file_read",
        "Read a UTF-8 text file from disk (source, logs, sidecars, configs, ASCII CAD/vector exchange such as .dxf). "
        "Skips binary-looking content and raster image extensions (use image_* tools for photos), files over 512 KiB, "
        "hidden or dot-named files, and paths under sensitive locations (e.g. AppData/Local, .ssh).",
        nlohmann::json{
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Path to the file (absolute or relative to the process working directory)."}}},
            }},
            {"required", nlohmann::json::array({"path"})},
        }};
}

} // namespace

std::vector<ToolDef> tool_catalog() {
    return {
        make_resize(),
        make_compress(),
        make_transform(),
        make_image_create(),
        make_meta(),
        make_find(),
        make_file_read(),
    };
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

} // namespace media::llm
