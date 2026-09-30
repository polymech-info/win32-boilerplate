#pragma once
//
// Agent feature interface — composable building blocks for agent behaviour.
//
// An IAgentFeature has two hook points:
//
//   on_prepare(turn, provider, run)
//     Called once before the LLM loop starts.
//     Features may:
//       - Modify `turn`  (e.g. inject memory state, override selection)
//       - Populate/augment `run.tools`
//       - Set `run.system_prompt` (SystemPromptFeature) or append to it
//       - Wrap `run.cb` to intercept events
//       - Install RAII scopes via run.add_scope(…) — held for the full run
//       - Attach an MCP bridge via run.mcp_bridge
//
//   on_finish(turn, result)
//     Called after the loop exits (success, error, or cancelled).
//     E.g. auto-memory append, telemetry flush.
//
// AgentFactory::default_agent() assembles the standard feature set.
// Pass a custom AgentOptions to suppress or replace individual features.
//

#include "agent.hpp"
#include "agent_mcp_bridge.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace media::llm::agent {

// ── RAII scope base ───────────────────────────────────────────────────────────

/// Opaque lifetime guard returned by IAgentFeature::on_prepare.
/// Destructor runs after the LLM loop exits, before on_finish.
struct FeatureScope {
    virtual ~FeatureScope() = default;
    FeatureScope()                               = default;
    FeatureScope(const FeatureScope&)            = delete;
    FeatureScope& operator=(const FeatureScope&) = delete;
};

// ── Run context — assembled by features before the loop ───────────────────────

struct PreparedRun {
    /// System prompt (assembled by SystemPromptFeature or set directly).
    std::string system_prompt;

    /// Tools JSON array sent to the LLM (assembled by ToolsFeature).
    nlohmann::json tools = nlohmann::json::array();

    /// Optional MCP bridge (created by ToolsFeature when mcp.json is present).
    std::unique_ptr<mcp::AgentMcpBridge> mcp_bridge;

    /// Effective event callback — features may wrap this to intercept events.
    EventCallback cb;

    /// RAII scopes held until the end of Agent::run() (path base, blocklist, etc.).
    std::vector<std::unique_ptr<FeatureScope>> scopes;

    void add_scope(std::unique_ptr<FeatureScope> s) {
        if (s) scopes.push_back(std::move(s));
    }
};

// ── Feature interface ─────────────────────────────────────────────────────────

class IAgentFeature {
public:
    virtual ~IAgentFeature() = default;

    /// Setup phase — called once before the LLM loop.
    /// `turn` is a mutable working copy; `provider` is const.
    virtual void on_prepare(Turn& turn,
                            const ProviderSettings& provider,
                            PreparedRun& run) {}

    /// Post-run phase — called after the loop exits (any outcome).
    virtual void on_finish(const Turn& turn, const Result& result) {}
};

} // namespace media::llm::agent
