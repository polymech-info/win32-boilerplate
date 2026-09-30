#pragma once
//
// find — search a set of images that match a user prompt.
//
// Two modes:
//   1. Name      — literal/substring match against the file name and the
//                  containing folder name(s). No network, no model, free.
//   2. Llm       — semantic match. For each candidate we collect a *metadata
//                  corpus* (sidecar `<stem>.md`, `<stem>.json`, EXIF tags),
//                  then ask an LLM judge: "does this match the user query?".
//                  If the corpus is empty (or `bypass_cache` is set) and
//                  `generate` is true, the worker calls `media::meta_extract`
//                  to materialise `.md` / `.json` next to the source first.
//                  Default is true; CLI / Find UI / JSON may override. The chat
//                  `image_find` path tool sets `generate` true for `mode:llm` so missing
//                  sidecars are auto-materialised; existing sidecars skip meta_extract.
//
// The LLM judge reuses the same provider / model / api_key plumbing as
// `meta` (see `MetaOptions`). A separate `judge_prompt` lets callers tune the
// match decision text; the user query (`opts.prompt`) is always appended.
//
// Inputs accept anything `expand_input_paths` understands (literal files,
// directories, globs).  Non-image files are skipped silently.
//
// Internally re-usable building block: `match_metadata_against_prompt(...)`
// — the LLM judge as a free function.  This is the seed for the future
// "image-compare-against-prompt" library API the user mentioned.
//
#include "polymech_export.h"
#include "batch_queue.hpp"
#include "meta.hpp"

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace media {

enum class FindMode {
    Name = 0,
    Llm  = 1,
};

struct FindOptions {
    FindMode    mode             = FindMode::Name;
    std::string prompt;                  // user query (required)
    bool        case_insensitive = true; // Name mode: case-fold match
    bool        match_folders    = true; // Name mode: also test parent folder names
    bool        recursive        = true; // recurse into directory inputs

    // ── LLM mode ────────────────────────────────────────────────────────────
    bool        bypass_cache = false;    // ignore existing .md/.json/EXIF
    bool        generate     = true;     // run meta_extract when corpus is empty
    bool        use_md       = true;     // consult <stem>.md  if present
    bool        use_json     = true;     // consult <stem>.json if present
    bool        use_exif     = true;     // consult libvips EXIF fields

    // Reuse the meta plumbing for both generation AND the judge call. Set
    // `meta.prompt` to override the cataloguer prompt; set provider / model /
    // api_key / resize_first / resize_width as you would for `meta`.
    MetaOptions meta;

    // Override the built-in judge prompt (the one that asks the model
    // "does this metadata match the search?"). Empty = built-in default.
    std::string judge_prompt;

    // Optional **reference images** — examples of what the user is looking for
    // (a screenshot, a product photo, a swatch). Sent as additional multimodal
    // parts in every judge call so the model can match by visual similarity in
    // addition to (or instead of) the textual prompt. Same idea as the
    // `transform` reference images.
    //
    // Read once at the start of `find_images` and reused for every candidate.
    std::vector<std::string> reference_images;

    // When true, call the LLM judge per candidate. When false, local case-insensitive
    // substring + word search on the corpus (name, sidecar, EXIF). `reference_images`
    // still forces the multimodal judge. Used by the `find` CLI; the `image_find` tool
    // always uses local + optional auto `meta` (this flag is forced off there).
    bool        find_semantic_judge = true;

    // ── Output / behaviour ──────────────────────────────────────────────────
    int  max_results = 0;     // 0 = unlimited
    bool dry_run     = false; // resolve + scan + log; no model calls / no writes
};

struct FindMatch {
    std::string path;        // absolute path to the matching image
    double      score = 0.0; // 0..1; 1.0 for Name mode, model-supplied for LLM
    std::string source;      // "name" | "folder" | "md" | "json" | "exif" | "llm" | "text"
    std::string reason;      // short excerpt explaining the match
};

struct FindResult {
    bool                    ok = false;
    std::string             error;
    std::vector<FindMatch>  matches;
    int                     scanned    = 0;  // total candidates examined
    int                     considered = 0;  // images that reached the judge
    int                     generated  = 0;  // .md/.json files created on demand
    int                     cache_hits = 0;  // images that already had sidecar
};

using FindProgressFn = std::function<void(const std::string& status)>;

/// The default judge prompt used when `FindOptions::judge_prompt` is empty.
POLYMECH_API std::string default_find_judge_prompt();

/// Run the find pipeline against a list of inputs (files / dirs / globs).
/// @param batch optional pause/cancel gate (call check_pause between items; not inside libvips/curl)
/// @param on_before_batch_pause if non-empty, called on the worker thread when pause is set, before check_pause blocks
POLYMECH_API FindResult find_images(const std::vector<std::string>& inputs,
                                     const FindOptions& opts,
                                     FindProgressFn            progress                 = nullptr,
                                     BatchControl*             batch                    = nullptr,
                                     std::function<void()>     on_before_batch_pause  = nullptr);

/// Stand-alone judge primitive — given a metadata corpus and a user query,
/// return {match, score 0..1, reason}.  Used internally by `find_images`,
/// exposed publicly so the future "image-compare-against-prompt" feature
/// can re-use the exact same decision function for testing and end-user
/// scenarios (see find.hpp top comment).
struct JudgeResult {
    bool        ok    = false;
    bool        match = false;
    double      score = 0.0;
    std::string reason;
    std::string error;
};

/// Reference image already loaded into memory (so the find pipeline can read
/// each ref image once and reuse it for every candidate judge call).
struct JudgeReference {
    std::string mime;        // e.g. "image/png"
    std::string bytes;       // raw image bytes
};

POLYMECH_API JudgeResult match_metadata_against_prompt(
    const std::string& metadata_corpus,
    const std::string& user_prompt,
    const MetaOptions& meta_opts,             // provider / model / api_key
    const std::string& judge_prompt = {},     // empty = default
    const std::vector<JudgeReference>& refs = {},
    const std::string& candidate_path = {});  // for logs: which image is being judged

/// Merge JSON keys into `opts` (REST / IPC / future).  Recognized keys:
///   mode            "name" | "llm"
///   prompt          string (the user query)
///   case_insensitive, match_folders, recursive : bool
///   bypass_cache, generate, use_md, use_json, use_exif : bool
///   judge_prompt    string
///   max_results     int
///   dry_run, find_semantic_judge  bool
///   meta            object — passed to apply_meta_options_from_json()
POLYMECH_API void apply_find_options_from_json(const nlohmann::json& j, FindOptions& opts);

} // namespace media
