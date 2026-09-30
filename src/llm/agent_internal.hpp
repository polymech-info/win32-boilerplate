#pragma once
//
// Internal (detail) API shared across the agent translation units.
//
// agent_commons.cpp  — time helpers, emit, RAII scopes, load, env ctx, usage parsing
// agent_context.cpp  — system-prompt building
// agent.cpp          — default run_turn implementation
//
// Nothing here is part of the public media::llm::agent:: surface — keep it in detail::.
//
#include "agent.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <vector>

namespace media::llm::agent::detail {

// ── String utilities ─────────────────────────────────────────────────────────

void str_tolower_in_place(std::string& s);

// ── Event emission ────────────────────────────────────────────────────────────

/// Returns false when the callback signals cancellation.
bool emit(const EventCallback& cb, const Event& ev);

// ── System-prompt file loading ────────────────────────────────────────────────

std::string trim_system_prompt_utf8(std::string s);

/// Config directory that hosts `system-prompt.md` (platform-specific).
std::filesystem::path system_prompt_config_parent();

/// Loads `system-prompt.md` from beside the exe or from the config dir.
/// Returns an empty string when the file is absent.
std::string load_system_prompt_md_if_present();

// ── Date / time ───────────────────────────────────────────────────────────────

/// "YYYY-MM-DDTHH:MM:SS±HH:MM (Timezone Name)" for the host's local clock.
std::string local_datetime_with_offset();

// ── RAII scopes ───────────────────────────────────────────────────────────────

struct PathToolBlocklistScope {
    bool active = false;
    explicit PathToolBlocklistScope(const std::vector<std::string>& disabled, int max_iter);
    ~PathToolBlocklistScope();
    PathToolBlocklistScope(const PathToolBlocklistScope&)            = delete;
    PathToolBlocklistScope& operator=(const PathToolBlocklistScope&) = delete;
};

struct AgentPathBaseScope {
    bool active = false;
    explicit AgentPathBaseScope(const Turn& t);
    ~AgentPathBaseScope();
    AgentPathBaseScope(const AgentPathBaseScope&)            = delete;
    AgentPathBaseScope& operator=(const AgentPathBaseScope&) = delete;
};

struct GodmodeScope {
    bool active = false;
    explicit GodmodeScope(bool on);
    ~GodmodeScope();
    GodmodeScope(const GodmodeScope&)            = delete;
    GodmodeScope& operator=(const GodmodeScope&) = delete;
};

// ── System-prompt assembly ────────────────────────────────────────────────────

/// Appends "Runtime context" block (OS, app, date/time, MCP note).
void append_runtime_environment_context(std::ostringstream& s);

/// Appends tool list, selection / folder block, and routing guidelines.
/// `tools` is the fully-assembled OpenAI tools JSON array (path + MCP, already filtered).
/// Only the names present in `tools` are ever mentioned in the generated text.
void append_agent_tooling_and_context(std::ostringstream& s,
                                      const Turn& turn,
                                      const nlohmann::json& tools);

// ── LLM usage parsing ─────────────────────────────────────────────────────────

std::int64_t json_usage_int64(const nlohmann::json& usage, const char* key);

/// Parse provider_meta_json and accumulate token counts + round entry into agg.
void merge_llm_usage_from_provider_meta(nlohmann::json& agg,
                                        const std::string& provider_meta_json,
                                        const std::string& router);

} // namespace media::llm::agent::detail
