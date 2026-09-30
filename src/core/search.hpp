#pragma once
//
// search — find any file by name (literal/regex) or content (grep).
//
// Scope is controlled by --type:
//   Any   (default) — all files, filtered by include/exclude globs.
//   Image           — image files only (same extensions as the `find` command).
//
// Source is controlled by --indexer:
//   Own (default)   — own recursive std::filesystem walker; always available.
//   Os              — OS indexer (Windows Search, Spotlight, locate); falls back
//                     to Own when unavailable and emits a warning.
//
// Modes:
//   name  (default) — match the query against filenames (and optionally parent
//                     folder components). Literal or regex; case-insensitive by
//                     default.
//   grep  (--grep)  — scan file contents for the query. Binary files skipped
//                     (NUL probe on first 8 KB). Large files streamed in chunks.
//
// Architecture:  lib (search.hpp/cpp)  →  cli | gui | rest | ipc | llm-tool
//
// The synchronous search_files() entry point is shared by CLI, REST, and IPC.
// The GUI layer wraps it in a worker thread.
//
// Cancellation — two sources, both honoured:
//   CLI  : install_cli_interrupt_handlers() (Ctrl+C / SIGINT) sets the global
//          media::cli::cancel_requested() flag.  No caller change required.
//   UI   : pass a BatchControl* to search_files(); call batch->request_cancel()
//          from the ribbon / toolbar button.  pause() / resume() also work.
//
// See docs/search.md for background and the async SearchEngine design (future).
//
#include "polymech_export.h"
#include "batch_queue.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace media {

// ── Enums ─────────────────────────────────────────────────────────────────────

/// Which files are considered candidates.
enum class SearchType {
    Any   = 0, ///< All regular files (subject to include/exclude globs).
    Image = 1, ///< Image files only (jpg, png, webp, raw, …).
};

/// Walker / index source.
enum class SearchIndexer {
    Own = 0, ///< Own recursive std::filesystem walker (cross-platform, no setup).
    Os  = 1, ///< OS indexer: Windows Search (ISearchManager2) | Spotlight (mdfind) | locate.
             ///< Falls back to Own if unavailable; emits a warning via the progress callback.
};

/// What the grep output contains — mirrors the GrepTool output_mode.
enum class SearchOutputMode {
    Content          = 0, ///< Matching lines with path:line:col: prefix (default).
    FilesWithMatches = 1, ///< Only file paths (one per file that has ≥ 1 match).
    Count            = 2, ///< file:count pairs (total matches per file).
};

// ── Options ───────────────────────────────────────────────────────────────────

struct SearchOptions {
    // ── Query ────────────────────────────────────────────────────────────────
    std::string query;
    bool        is_regex       = false; ///< Treat query as ECMAScript regex (std::regex).
    bool        case_sensitive = false; ///< Default: case-insensitive.
    bool        whole_word     = false; ///< Require word-boundary match (non-regex mode).

    // ── Scope ─────────────────────────────────────────────────────────────────
    SearchType  type           = SearchType::Any;
    bool        grep           = false; ///< Scan file contents (not just filenames).
    bool        names_only     = false; ///< With grep=true: report only the filepath, not match lines.
    bool        recursive      = true;
    bool        include_hidden = false; ///< Include dot-files and dot-dirs.
    bool        follow_symlinks = false;

    // ── Filters ───────────────────────────────────────────────────────────────
    /// Only include files whose filename matches one of these globs (e.g. "*.cpp").
    /// When empty, all files pass (subject to type filter).
    std::vector<std::string> include_globs;
    /// Skip files whose filename matches one of these globs.
    std::vector<std::string> exclude_globs;
    /// Directory names to prune during recursion (exact match on the final component).
    /// When empty, the built-in default list (.git, node_modules, …) is used.
    std::vector<std::string> exclude_dirs;

    // ── Grep options ──────────────────────────────────────────────────────────
    bool     skip_binary          = true;
    uint64_t max_file_size_bytes  = 256ull * 1024 * 1024;
    uint32_t max_matches_per_file = 1000;
    int      context_lines        = 0; ///< Symmetric context before AND after each match (-C).
    int      context_before_lines = 0; ///< Lines before each match (-B); ignored when context_lines > 0.
    int      context_after_lines  = 0; ///< Lines after each match (-A); ignored when context_lines > 0.
    bool     multiline            = false; ///< Regex: ^ / $ match per-line; enables std::regex::multiline.
    bool     max_columns_limit    = true;  ///< Truncate preview lines > 500 chars (like rg --max-columns).

    /// When non-empty, treated as an rg-style type name and expanded to include_globs.
    /// E.g. "cpp" → {"*.cpp","*.cxx","*.cc","*.c","*.h","*.hpp","*.hxx"}.
    /// Takes lower priority than an explicit include_globs (if both set, both apply as OR).
    std::string type_filter;

    // ── Indexer ───────────────────────────────────────────────────────────────
    SearchIndexer indexer = SearchIndexer::Own;

    // ── Safety ────────────────────────────────────────────────────────────────
    /// When true (default), paths that match the SENSITIVE_PATH_PREFIXES list
    /// (secrets, DB dumps, credential stores, browser profiles, …) are silently
    /// excluded from results.  Set to false only for explicitly trusted callers
    /// that have their own access control (e.g. the admin debug shell).
    bool exclude_sensitive = true;

    // ── Output / behaviour ────────────────────────────────────────────────────
    SearchOutputMode output_mode = SearchOutputMode::Content;
    int  max_results = 0;   ///< 0 = unlimited.
    int  head_limit  = 0;   ///< 0 = unlimited. When > 0, truncates result list (LLM pagination).
    int  offset      = 0;   ///< Skip first N results before applying head_limit.
    bool dry_run     = false;
};

// ── Match ─────────────────────────────────────────────────────────────────────

struct SearchMatch {
    std::string path;
    uint64_t    line        = 0;  ///< 1-based; 0 for filename-only matches.
    uint64_t    column      = 0;  ///< 1-based byte column; 0 when not reported.
    std::string preview;          ///< Matched line (trimmed), or empty for name matches.
    std::string source;           ///< "name" | "folder" | "content"
    double      score       = 1.0;

    /// Lines before the match (context_lines > 0, grep mode).
    std::vector<std::string> context_before;
    /// Lines after the match (context_lines > 0, grep mode).
    std::vector<std::string> context_after;
};

// ── Stats / Result ────────────────────────────────────────────────────────────

struct SearchStats {
    int files_visited = 0; ///< Total candidate files before scanning.
    int files_scanned = 0; ///< Files opened and searched.
    int files_skipped = 0; ///< Permission errors, too-large, etc.
    int files_binary  = 0; ///< Skipped due to binary probe.
    int matches       = 0; ///< Total match count (line hits or filename hits).
};

struct SearchResult {
    bool                     ok = false;
    std::string              error;
    std::vector<SearchMatch> matches;
    SearchStats              stats;

    // ── Aggregate output (for LLM tools / REST) ───────────────────────────────
    // Populated only when output_mode is set and head_limit processing is done.
    // For CLI use, callers iterate `matches` directly.
    SearchOutputMode         output_mode     = SearchOutputMode::Content;
    std::string              content;          ///< Mode=Content: formatted match lines joined by '\n'.
    std::vector<std::string> file_paths;       ///< Mode=FilesWithMatches: unique file paths.
    int                      total_matches    = 0; ///< Total match count before head_limit.
    int                      applied_limit    = 0; ///< head_limit that was applied (0 = none).
    int                      applied_offset   = 0; ///< offset that was applied.
};

using SearchProgressFn = std::function<void(const std::string& status)>;

// ── Public API ────────────────────────────────────────────────────────────────

/// Run the search pipeline against a list of inputs (files / dirs / globs).
/// Thread-safe; may be called from any thread.
/// Cancellation: CLI Ctrl+C (media::cli::cancel_requested) is always checked.
/// Pass a BatchControl* for UI-driven pause / cancel.
POLYMECH_API SearchResult search_files(
    const std::vector<std::string>& inputs,
    const SearchOptions&            opts,
    SearchProgressFn                progress = nullptr,
    BatchControl*                   batch    = nullptr);

/// Apply JSON keys into @p opts (REST / IPC / llm-tool).
/// Recognised keys: all SearchOptions field names in snake_case.
POLYMECH_API void apply_search_options_from_json(const nlohmann::json& j, SearchOptions& opts);

/// The default list of directory names pruned when opts.exclude_dirs is empty.
POLYMECH_API const std::vector<std::string>& default_search_exclude_dirs();

} // namespace media
