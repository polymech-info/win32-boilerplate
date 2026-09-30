#pragma once
//
// media::llm::path::execute — invoke a path-mode tool by name.
//
// Walks `arguments.paths[]` (or `arguments.inputs[]` for image_find), calls
// the appropriate `*_file` / `*_image` worker per entry, writes results to
// disk, and returns one PerFileResult per input. The standard envelope is
// the same shape across all path-mode tools (see docs/chat.md §4).
//
// Error semantics: per-file errors do NOT short-circuit the batch — the
// envelope reports `ok:true` with `summary.failed > 0` so the chat agent can
// surface partial successes ("✓ a.jpg ✓ b.jpg ✗ c.jpg"). A catastrophic
// failure (unknown tool name, malformed arguments) returns `ok:false` and
// an empty `results[]`.
//
#include "polymech_export.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace media::llm::path {

struct PerFileResult {
    std::string    path;          // input path the model passed in
    bool           ok = false;
    std::string    error;         // populated on failure
    std::string    output_path;   // written file (resize / compress / transform)
    nlohmann::json result;        // populated for meta (parsed description JSON)
};

struct ExecuteResult {
    bool                       ok = false;
    std::string                error;        // catastrophic failure (e.g. unknown tool)
    std::vector<PerFileResult> per_file;
    nlohmann::json             envelope;     // {ok, results, summary} — what to send back to the LLM
};

/// Run a tool by catalog name. `arguments` follows the path-mode envelope
/// (see docs/chat.md §4). `arguments.paths` (or `arguments.inputs` for
/// image_find) is the list of host paths to operate on.
POLYMECH_API ExecuteResult execute(const std::string& name,
                                   const nlohmann::json& arguments);

/// While the LLM agent turn is running, `execute` refuses tool names in this
/// set (case-insensitive). Call `clear` when the turn ends. Used with
/// `tool_catalog_openai_excluding` so the model should not see disabled tools.
POLYMECH_API void set_agent_tool_blocklist(const std::vector<std::string>& disabled_names);
POLYMECH_API void clear_agent_tool_blocklist();

/// When non-empty, `resolve_tool_path_string` (all path tools) resolves **relative** paths
/// against this directory first instead of `std::filesystem::current_path()`.
/// Used by the in-app / `llm agent` turn so the effective “cwd” is the Explorer
/// folder or CLI `--folder` when set. Call `clear_agent_path_base()` at end of the turn.
/// Empty = process CWD (CLI, tests, or agent with no `--folder`).
POLYMECH_API void set_agent_path_base(const std::string& absolute_dir_or_empty);
POLYMECH_API void clear_agent_path_base();

/// When true, all filesystem safety guards (sensitive-path deny, extension
/// blocklist, dotfile block) are skipped for path tools and the run tool.
/// Set at the start of a turn, cleared at the end via GodmodeScope.
POLYMECH_API void set_agent_godmode(bool on);
POLYMECH_API bool get_agent_godmode();

/// Convenience: serialise an ExecuteResult to the JSON string the agent
/// loop should send back to the LLM as the `tool` message content.
POLYMECH_API std::string envelope_text(const ExecuteResult& r);

/// If a `speak` tool call is currently synthesising or playing audio, signal it
/// to stop immediately. Safe to call from any thread; no-op when idle.
/// Called automatically by AgentScheduler::stop() so the scheduler thread can
/// be joined cleanly when the app exits.
POLYMECH_API void abort_active_speak();

/// If a `run` tool call has a child process running, terminate it immediately.
/// Safe to call from any thread; no-op when idle.
POLYMECH_API void abort_active_run();

/// While set, `image_transform` calls @p f after **each** input file finishes
/// (the argument is a full tool `envelope` for that file only). The UI can
/// show queue rows immediately; the final `execute` result still includes all
/// files for the LLM. Clear when the turn ends; never invoke after `execute` returns.
POLYMECH_API void set_image_transform_file_progress_hook(
    std::function<void(const nlohmann::json& one_file_envelope)> f);
POLYMECH_API void clear_image_transform_file_progress_hook();

} // namespace media::llm::path
