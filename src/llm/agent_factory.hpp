#pragma once
//
// Agent factory — compose an Agent from a list of features.
//
// Usage:
//
//   // Default agent (all enabled standard features):
//   auto result = AgentFactory::default_agent().run(turn, provider, cb);
//
//   // Custom agent (tools only, no memory, custom prompt):
//   Agent a(AgentOptions{
//       .features = {
//           AgentFactory::feature_path_scope(),
//           AgentFactory::feature_tools(),
//           my_custom_prompt_feature(),
//       }
//   });
//   auto result = a.run(turn, provider, cb);
//
// The feature ordering matters:
//   PathScopeFeature    — must come first (blocklist active before tools are built)
//   ToolsFeature        — builds path-catalog tools JSON (no MCP)
//   McpFeature          — merges mcp.json tools + creates bridge (runs after ToolsFeature)
//   MemoryFeature       — installs thread-local task-ID scope
//   SystemPromptFeature — runs last so it sees the fully assembled Turn and tools
//
#include "agent_feature.hpp"

#include <memory>
#include <vector>

#ifndef FEATURE_HOME_LLM_TOOLS
#define FEATURE_HOME_LLM_TOOLS 1
#endif
#ifndef FEATURE_HOME_LLM_SKILLS
#define FEATURE_HOME_LLM_SKILLS 1
#endif

namespace media::llm::agent {

// ── Options object ────────────────────────────────────────────────────────────

struct AgentOptions {
    std::vector<std::shared_ptr<IAgentFeature>> features;
};

// ── Agent ─────────────────────────────────────────────────────────────────────

class Agent {
public:
    explicit Agent(AgentOptions opts) : opts_(std::move(opts)) {}

    /// Synchronous turn. Blocks until done, cancelled, or max_iterations hit.
    /// On_event follows the same contract as the legacy run_turn signature.
    Result run(const Turn&            turn,
               const ProviderSettings& provider,
               EventCallback           on_event = nullptr) const;

private:
    AgentOptions opts_;
};

// ── Factory helpers ───────────────────────────────────────────────────────────

namespace AgentFactory {

/// Standard built-in features (available individually for custom assemblies).
std::shared_ptr<IAgentFeature> feature_path_scope();    ///< Path-base + blocklist RAII
#if FEATURE_HOME_LLM_TOOLS
std::shared_ptr<IAgentFeature> feature_tools();         ///< Path catalog (disabled tools excluded)
#endif
#if FEATURE_HOME_LLM_TOOLS && FEATURE_MCP_CLIENT
std::shared_ptr<IAgentFeature> feature_mcp();           ///< MCP bridge + tool merge (runs after feature_tools)
#endif
std::shared_ptr<IAgentFeature> feature_memory();        ///< Thread-local task-ID scope
#if FEATURE_HOME_LLM_SKILLS
std::shared_ptr<IAgentFeature> feature_skills();        ///< Discover skills + inject summary into system text
#endif
std::shared_ptr<IAgentFeature> feature_system_prompt(); ///< Assemble canonical system prompt

/// Fully-assembled default agent (PathScope -> optional tools/MCP/skills -> Memory -> SystemPrompt).
Agent default_agent();

} // namespace AgentFactory

} // namespace media::llm::agent
