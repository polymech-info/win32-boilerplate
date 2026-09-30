#pragma once
//
// media::llm::run — self-contained Run Tool for LLM agent shell execution.
//
// Provides:
//   validate_command()       — three-tier (Allow/Warn/Deny) command security  [RunValidators.hpp]
//   detect_shell()          — cross-platform shell resolution
//   spawn_shell_process()   — pipe-captured child process creation
//   execute()               — full do_run lifecycle (validate→spawn→poll→drain→envelope)
//   abort_active_run()      — cross-thread cancellation of the running child
//
#include "polymech_export.h"
#include "llm/tools/run/RunValidators.hpp"   // ValidationTier, ValidationResult, validate_command
#include "llm/path_tool_executor.hpp"        // ExecuteResult

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace media::llm::run {

// ── Shell detection ───────────────────────────────────────────────────────────

struct ShellSpec {
    std::string exe;
    std::string flag;
    bool        is_pwsh = false;
};

POLYMECH_API ShellSpec detect_shell(const std::string& hint = "auto");

// ── Process spawning ──────────────────────────────────────────────────────────

struct SpawnResult {
    bool        ok = false;
    std::string error;
#if defined(_WIN32)
    void*         process_handle = nullptr;
    unsigned long pid = 0;
    void*         stdout_read = nullptr;
    void*         stderr_read = nullptr;
#else
    int           pid = -1;
    int           stdout_fd = -1;
    int           stderr_fd = -1;
#endif
};

POLYMECH_API SpawnResult spawn_shell_process(const ShellSpec& shell,
                                             const std::string& command,
                                             const std::string& cwd);

// ── Tool execution (called from path_tool_executor dispatch) ──────────────────

/// Full lifecycle: validate → resolve CWD → detect shell → spawn → poll with
/// timeout/cancel → drain → build envelope.  Returns an ExecuteResult that
/// path_tool_executor forwards to the agent loop.
POLYMECH_API media::llm::path::ExecuteResult
execute(const nlohmann::json& args,
        const std::string& agent_path_base);

/// Kill the child process of a currently-running `run` tool call.
/// Safe to call from any thread; no-op when idle.
POLYMECH_API void abort_active_run();

// ── Streaming progress hook ──────────────────────────────────────────────────

/// Callback receives (stream, chunk) where stream is "stdout" or "stderr".
using OutputChunkHook = std::function<void(const std::string& stream,
                                           const std::string& chunk)>;

POLYMECH_API void set_output_chunk_hook(OutputChunkHook hook);
POLYMECH_API void clear_output_chunk_hook();

} // namespace media::llm::run
