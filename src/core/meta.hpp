#pragma once
//
// meta — extract structured metadata for an image with an LLM (Gemini/Google).
//
// Pipeline:
//   1. Optional in-memory resize (default 512px) so the image is small enough
//      for fast vision inference.
//   2. Read EXIF basics from libvips header (camera, GPS, datetime, dims).
//   3. Build a structured prompt asking the model for JSON.
//   4. Call Gemini generateContent with responseMimeType=application/json.
//   5. Parse, then write any combination of:
//        - <stem>.md   (human-readable markdown description)
//        - <stem>.json (full structured payload + EXIF copy)
//        - In-place EXIF "ImageDescription" (rewrites the file with libvips).
//
// Use --dry-run on the CLI to validate options + in-memory resize without
// calling the API or writing any output file.
//
#include "polymech_export.h"
#include "transform.hpp"  // re-uses provider/model concepts

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace media {

struct MetaOptions {
    // ── Provider / model ─────────────────────────────────────────────────────
    std::string provider;
    std::string model;
    std::string api_key;                         // empty → filled from app provider settings by CLI/server
    /// Google Gemini API root; empty = default. Mirrored on `TransformOptions::base_url` for the same call.
    std::string base_url;
    std::string prompt;                          // empty → built-in default

    /**
     * Optional contextual question. When non-empty AND `prompt` is empty,
     * the built-in cataloguer prompt is **augmented** with this string so
     * Gemini's response explicitly addresses the user's intent inside the
     * structured `description` field. Useful for the chat agent: when the
     * user asks "is there a person in this picture?", we forward that
     * question here so the meta call answers it directly instead of just
     * producing generic catalog data.
     *
     * Ignored when `prompt` is set (the explicit override wins).
     */
    std::string user_query;

    // ── Pre-flight resize (in memory only) ──────────────────────────────────
    bool resize_first = true;
    int  resize_width = 512;   // longest edge after resize; LLMs do well at 512–768

    // ── Outputs (any combination) ───────────────────────────────────────────
    bool out_md      = true;   // write <stem>.md beside source / in out_dir
    bool out_json    = true;   // write <stem>.json
    bool update_exif = false;  // rewrite source: set ImageDescription EXIF tag

    // ── Output target ───────────────────────────────────────────────────────
    std::string out_dir;       // empty = next to source

    // ── Behaviour ───────────────────────────────────────────────────────────
    bool dry_run = false;      // do everything except API call + writes
};

struct MetaResult {
    bool        ok = false;
    std::string error;
    std::string md_path;       // empty if not written / dry_run
    std::string json_path;
    bool        exif_updated = false;

    // Always populated on success (also on dry_run when content is built).
    std::string  markdown;
    std::string  json_text;    // full JSON payload as written

    // Diagnostics
    std::size_t bytes_sent  = 0;     // payload size sent to API (resized image)
    int         resized_w   = 0;     // 0 if not resized
    int         resized_h   = 0;
    int         orig_w      = 0;
    int         orig_h      = 0;
};

using MetaProgressFn = std::function<void(const std::string& status)>;

/// The default prompt (returned as-is when MetaOptions::prompt is empty).
POLYMECH_API std::string default_meta_prompt();

/// Run the meta pipeline for a single image.
POLYMECH_API MetaResult meta_extract(const std::string& input_path,
                                     const MetaOptions& opts,
                                     MetaProgressFn progress = nullptr);

/// Buffer-only variant for the REST endpoint — never touches the host fs.
/// Output flags (`out_md`, `out_json`, `update_exif`, `out_dir`) are ignored;
/// the JSON payload is returned in `MetaResult::json_text` and the markdown in
/// `MetaResult::markdown`. EXIF is read from the in-memory bytes via libvips.
POLYMECH_API MetaResult meta_extract_buffer(const void* in_data, std::size_t in_size,
                                             const std::string& in_mime,
                                             const MetaOptions& opts,
                                             MetaProgressFn progress = nullptr);

/// Merge JSON keys into @p opts (REST / IPC). Unknown keys are ignored.
/// Recognized: provider, model, api_key, prompt, resize_first, resize_width,
/// out_md, out_json, update_exif, out_dir, dry_run.
/// Absent or non-string `provider` / `model` clears those fields (struct defaults are not JSON).
POLYMECH_API void apply_meta_options_from_json(const nlohmann::json& j, MetaOptions& opts);

} // namespace media
