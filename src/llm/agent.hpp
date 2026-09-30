#pragma once
#include "constants.hpp"
#include <string_view>
//
// media::llm::agent — synchronous chat-agent loop on top of polymech::kbot
// and media::llm::path. Used by both the in-app chat dock (Win32 UI) and
// the `pm-image llm agent` CLI subcommand.
//
// Flow per turn:
//   1. Build a system prompt: optional `system-prompt.md` (trimmed) next to the executable,
//      else under the app config dir (Windows: get_config_dir(); macOS/Linux/Unix: same folder as portable
//      settings.json, see settings_portable) — replaces the built-in **intro** only. A fixed **runtime context**
//      block (OS, product, chat UI) plus tool catalog + Explorer selection/folder + guidelines
//      are always appended (see build_system_prompt).
//   2. Send {system, user} to kbot with tools = path::tool_catalog_openai() plus optional
//      MCP tools from profile mcp.json (Streamable HTTP; see ref/agent.cpp src/mcp/).
//   3. While the response carries tool_calls[]: dispatch each via
//      media::llm::path::execute(...), append results back as `tool` messages,
//      call kbot again. Capped by AgentOptions::max_iterations.
//   4. Return the final assistant text + a structured transcript.
//
// Cancellation: (1) the EventCallback may return false to abort between
// iterations; (2) global Ctrl+C / SIGINT (see `core/cli_cancel.hpp`) stops
// the loop before the next LLM or tool call and between files inside path
// tools; returns Result with `cancelled=true` (exit code 130 in CLI).
//
// See docs/chat.md §6 for the design rationale.
//
#include "polymech_export.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace media::llm::agent {

// One human chat turn: a prompt, plus the user's current selection /
// folder context that the agent should treat as implicit input when the
// prompt says "these files" / "this folder" / etc.
struct Turn {
    std::string              user_prompt;
    std::vector<std::string> selection;     // explorer-selected files (may be empty)
    std::string              folder_hint;   // current Explorer folder ("" when N/A)
    std::string              system_extra;  // optional caller-supplied extra system text
    /// Path tool names to omit (case-insensitive), e.g. from `llm agent --disable-tools=image_resize,list_images`.
    /// Empty = full catalog. Invalid names are ignored (with a warning on the CLI).
    std::vector<std::string> disabled_path_tools;
    /// When false, MCP-backed OpenAI tools (`mcp_*` function names) are removed after `mcp.json` merge
    /// (Web chat sidebar master toggle). Default true — same behavior as CLI / native when unset.
    bool mcp_tools_enabled = true;
    /// MCP server names (mcp.json keys) to suppress for this turn. Empty = all servers active.
    /// Applied by McpFeature after AgentMcpBridge::try_create merges all servers.
    std::vector<std::string> disabled_mcp_servers;
    /// Quick-tools Replicate video parameters (chat-next): merged into each `create_video` call so UI
    /// overrides model defaults / LLM-chosen enum values for overlapping keys (non-empty values only).
    nlohmann::json create_video_ui_overrides = nlohmann::json::object();
    /// When true, all filesystem safety guards (sensitive-path deny, extension
    /// blocklist, dotfile block, run-command validation) are bypassed.
    /// CLI: `--godmode`.  Never exposed in the GUI — power-user escape hatch.
    bool godmode = false;

    // ── Scheduled-task context (set by AgentScheduler; empty for normal turns) ─
    /// Non-empty when this turn belongs to a scheduled task. Scopes memory_write.
    std::string    task_id;
    /// Compact JSON state from the task's last memory_write (injected into system prompt).
    nlohmann::json memory_state    = nlohmann::json::object();
    /// Last N events from the task event log (injected into system prompt).
    nlohmann::json recent_events   = nlohmann::json::array();
    /// True when this turn was fired by the scheduler timer (vs a user-initiated normal turn).
    bool           is_scheduled_tick = false;
};

/// Which API endpoint/transport the agent uses to talk to the LLM.
/// ChatCompletion: POST /chat/completions — OpenAI-compatible, broad router support.
/// Responses: POST /responses — OpenAI Responses API; supported natively by OpenAI /
///   OpenRouter and via bridge by LiteLLM proxy (auto-bridges non-native models).
/// Realtime: WebSocket /realtime PoC (text-only, no tools), currently OpenAI-focused.
enum class LlmApiMode { ChatCompletion, Responses, Realtime };

/// Streaming preference for LLM calls.
/// Auto: endpoint-specific default behavior.
/// On: force streaming when supported by the selected endpoint.
/// Off: disable streaming even if supported.
enum class LlmStreamingMode { Auto, On, Off };

// Provider settings — all fields can fall back to env vars at the kbot layer.
struct ProviderSettings {
    std::string router;  // matches polymech::kbot::KBotOptions::router
    std::string base_url;                       // empty = router default
    std::string api_key;                        // empty = take from env (caller decides which)
    std::string model;
    int         timeout_ms    = 180'000;
    int         max_iterations = 30;             // tool-call loop cap (0 = no tools)
    LlmApiMode  api_mode      = (std::string_view(pm::llm::k_default_api_mode) == "responses"
                                    ? LlmApiMode::Responses : LlmApiMode::ChatCompletion);
    LlmStreamingMode streaming_mode = LlmStreamingMode::Auto;
};

struct Event {
    enum Kind {
        TurnStarted,        // before the first LLM round; payload = {prompt, selection}
        /// Fired after every successful LLM HTTP response. payload shape:
        ///   { "round": N, "duration_ms": N, "stateful": bool,
        ///     "response_id": "resp_…",         // Responses API only
        ///     "tokens": { "input": N, "output": N, "reasoning": N? },
        ///     "model": "gpt-5.5-…" }
        /// Appears in the agent JSON between tool_result and the next tool_call
        /// so you can see exactly what each round cost.
        LlmRound,
        ToolCall,           // model requested a tool; tool_name + payload (arguments)
        ToolResult,         // we ran the tool; tool_name + payload (envelope)
        /// One file finished during `image_transform` (multi-file). payload = {id, envelope} — same
        /// envelope shape as ToolResult but `results` has a single entry. UIs can update the
        /// file queue before the full tool returns.
        ToolFileProgress,
        /// Model emitted reasoning / thinking text (Responses API only; output[].type="reasoning").
        /// text = concatenated summary. Fired before AssistantText. Never fed back to the model.
        Thinking,
        /// Incremental assistant text chunk from a streaming endpoint.
        /// text = delta chunk (may be empty for keepalive/control events).
        TextDelta,
        AssistantText,      // model produced final text; text = the response
        Error,              // text = error message
        Done,               // turn complete; text = final assistant text or empty
    };
    Kind           kind;
    std::string    text;
    std::string    tool_name;
    nlohmann::json payload;
};

/// Returning `false` from the callback signals the loop to abort cleanly
/// at the next iteration boundary.
using EventCallback = std::function<bool(const Event&)>;

struct Result {
    bool           ok = false;
    std::string    error;
    std::string    final_text;       // last assistant message content
    bool           cancelled = false;
    int            iterations = 0;
    nlohmann::json transcript;       // full {messages:[…], summary} for debug/UI
    /**
     * Aggregated LLM telemetry from each successful chat completion in this turn
     * (one HTTP response per agent iteration). OpenAI-style `usage` fields plus optional
     * `usage.cost` (OpenRouter). Shape:
     *   { "prompt_tokens", "completion_tokens", "total_tokens", "cost"?, "llm_rounds": [ { "usage", "model"?, "id"? } ] }
     */
    nlohmann::json llm_usage_aggregate;
};

/// Run a synchronous turn. Blocks until the model stops calling tools, hits
/// max_iterations, or the callback returns false.
POLYMECH_API Result run_turn(const Turn& turn,
                              const ProviderSettings& provider,
                              EventCallback on_event = nullptr);

/// Build the canonical system prompt using the pre-assembled OpenAI tools JSON
/// (path tools + MCP tools, already filtered). Preferred inside Agent::run().
POLYMECH_API std::string build_system_prompt(const Turn& turn, const nlohmann::json& tools);

/// Backward-compat overload: rebuilds the tools JSON from the path catalog
/// using turn.disabled_path_tools. Used by CLI --dry-run and tests.
POLYMECH_API std::string build_system_prompt(const Turn& turn);

} // namespace media::llm::agent
