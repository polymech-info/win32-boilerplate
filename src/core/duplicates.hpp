#pragma once
//
// duplicates — find groups of image files that match under size, perceptual
// dHash, or sidecar+EXIF "meta" signatures (see docs/duplicates.md).
//
// Inputs: same rules as `find` — each entry is a file, directory, or glob
// understood by `expand_input_paths` (from `collect_inputs_for_duplicates` via
// the same file walk as `find.cpp`).
//
#include "polymech_export.h"
#include "meta.hpp"

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace media {

struct BatchControl;

enum class DuplicatesMode {
    Size         = 0,
    Fingerprint  = 1,
    Meta         = 2,
};

struct DuplicatesOptions {
    DuplicatesMode mode = DuplicatesMode::Fingerprint;

    /// Reuse find-style directory / glob walk.
    bool recursive = true;

    /// Only emit groups with this many or more members (default 2).
    int min_group_size = 2;

    // ── fingerprint (dHash 64-bit, 9×8 grey) ─────────────────────────────
    /// Hamming distance threshold between hashes (0 = identical hash only).
    int  max_hamming                = 0;
    /// If true, only compare fingerprint pairs with equal file size (faster).
    bool fingerprint_same_size_only = false;

    // ── meta (sidecar + optional EXIF, hashed) ────────────────────────────
    bool use_md  = true;
    bool use_json= true;
    bool use_exif= true;
    /// When non-empty, prepended to the meta corpus before normalize+SHA-256 so
    /// the same sidecars can be bucketed with a caller-defined “semantic” key
    /// (e.g. shared instructions / campaign id), without changing on-disk files.
    std::string meta_prompt;
    MetaOptions meta;  // for future --generate; generation flags read from `meta` when wired

    // ── meta: LLM JSON sidecar compare (OpenAI-compatible / OpenRouter via kbot) ─
    /// When true, do not hash sidecars; compare extracted JSON fields per pair
    /// with the chat provider (`llm_*`), then group by connected components (score ≥ min).
    bool         meta_json_llm_compare  = false;
    int          meta_json_min_similarity = 7; ///< 0–10 inclusive; edge if similarity >= this
    /// Override prompt instructions; when empty, a built-in default is used.
    std::string  meta_json_compare_prompt;
    /// Filled by CLI: same resolution as `llm agent` (settings.json chat, then env).
    std::string  llm_router;
    std::string  llm_model;
    std::string  llm_base_url;
    std::string  llm_api_key;
    int          llm_timeout_ms = 0; ///< 0 = use 60000 in the compare path

    /// When meta+LLM JSON compare has fewer than two usable sidecars, run `meta_extract`
    /// (same pipeline as the Meta command: resize, provider, prompt from `meta`) to create
    /// `<stem>.json`, then continue. Requires a cataloguer API key on `meta` (e.g. Google).
    bool meta_json_implicit_generate = false;

    /// When true, `DuplicatesResult::report` is filled with per-file diagnostics
    /// (dHash, sizes, meta corpus preview, etc.). The CLI also enables this when
    /// `--report-md` or `--report-json` is set.
    bool detailed_report = false;
};

struct DuplicateGroup {
    /// Discriminator: "size", "fingerprint", or "meta".
    std::string method;
    /// Key: decimal size, hex hash, or sha256 of normalized corpus.
    std::string key;
    /// Absolute paths, sorted for stable output.
    std::vector<std::string> paths;
};

struct DuplicatesResult {
    bool                          ok     = false;
    std::string                   error;
    int                           scanned = 0;  // images considered
    int                           skipped_fingerprint = 0;
    int                           skipped_meta = 0;
    std::vector<DuplicateGroup>   groups;

    // Diagnostics (e.g. empty meta)
    int meta_empty_files = 0;

    /// `report` is always filled on success: at minimum `duplicate_map` (per-path → peers w/
    /// pairwise `size` / dHash+Hamming / meta key). When `detailed_report` is true, also
    /// options, candidates, mode tables, and full markdown via `format_duplicates_markdown`.
    nlohmann::json report = nlohmann::json::object();
};

/// Render a markdown document from a `result.report` payload (same object written
/// for `--report-json` when detailed mode is on).
POLYMECH_API std::string format_duplicates_markdown(const nlohmann::json& report);

using DuplicatesProgressFn = std::function<void(const std::string& status)>;

/// Discover duplicate groups. Never throws; failures per file are counted in
/// `skipped_*` fields. See docs/duplicates.md for algorithm notes.
/// @param batch  Optional: cooperative pause / cancel (check_pause between stages;
///                cancel is polled). Pass nullptr for CLI / tests.
/// @param on_before_pause  Invoked on the worker thread when pause is set, immediately
///                        before blocking in check_pause() (so the UI can post
///                        partial batch state, same pattern as other batch workers).
POLYMECH_API DuplicatesResult find_duplicates(const std::vector<std::string>& inputs,
                                                const DuplicatesOptions&        opts,
                                                DuplicatesProgressFn            progress   = nullptr,
                                                BatchControl*                    batch     = nullptr,
                                                std::function<void()>            on_before_pause = nullptr);

POLYMECH_API void apply_duplicates_options_from_json(const nlohmann::json& j, DuplicatesOptions& opts);

/// True if `j` looks like a saved duplicates report (`--report-json` / `--save-session`).
/// Accepts `kind` : `"pm-image.duplicates"` (v2) or legacy `tool` : `"duplicates"` with `format_version` 1–2.
POLYMECH_API bool validate_duplicates_session_json(const nlohmann::json& j, std::string& err);

} // namespace media
