#include "agent_feature.hpp"
#include "agent_factory.hpp"   // feature factory declarations live there
#include "agent_internal.hpp"  // detail::, build_system_prompt

#include "path_tool_catalog.hpp"
#include "path_tool_executor.hpp"
#include "agent_memory.hpp"
#include "agent_mcp_bridge.hpp"
#include "agent_tools.hpp"
#include "agent_skills.hpp"
#include "core/settings_runtime.hpp"
#include "logger/logger.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace media::llm::agent {

// ═══════════════════════════════════════════════════════════════════════════════
// PathScopeFeature
//   Pins the path-tool base directory and installs the disabled-tool blocklist
//   for the duration of the run.
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

struct PathScopeGuard : FeatureScope {
    detail::AgentPathBaseScope     path_base;
    detail::PathToolBlocklistScope blocklist;
    detail::GodmodeScope           godmode;

    PathScopeGuard(const Turn& turn, const ProviderSettings& provider)
        : path_base(turn)
        , blocklist(turn.disabled_path_tools, provider.max_iterations)
        , godmode(turn.godmode)
    {}
};

class PathScopeFeature final : public IAgentFeature {
public:
    void on_prepare(Turn& turn, const ProviderSettings& provider, PreparedRun& run) override {
#if FEATURE_HOME_LLM_TOOLS
        // Validate disable-lists before constructing scope guards.
        // PathScopeGuard is created before ToolsFeature, so unknown IDs must be
        // pruned here (not only later in ToolsFeature) to keep blocklist setup safe.
        if (!turn.disabled_path_tools.empty()) {
            std::vector<std::string> valid;
            valid.reserve(turn.disabled_path_tools.size());
            for (const auto& n : turn.disabled_path_tools) {
                if (pm::llm::agent_tool_find_by_name(n))
                    valid.push_back(n);
            }
            turn.disabled_path_tools = std::move(valid);
        }
#else
        turn.disabled_path_tools.clear();
#endif
        run.add_scope(std::make_unique<PathScopeGuard>(turn, provider));
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// ToolsFeature
//   Builds the tools JSON array from the path catalog (minus disabled tools).
//   MCP tool merging is handled separately by McpFeature.
// ═══════════════════════════════════════════════════════════════════════════════

#if FEATURE_HOME_LLM_TOOLS

class ToolsFeature final : public IAgentFeature {
public:
    void on_prepare(Turn& turn, const ProviderSettings& provider, PreparedRun& run) override {
        if (provider.max_iterations <= 0) return;  // text-only mode

        {
            std::string gerr;
            media::runtime_settings::apply_global_tool_policy_to_values(
                turn.disabled_path_tools, turn.mcp_tools_enabled, turn.disabled_mcp_servers, gerr);
            if (!gerr.empty())
                logger::warn(std::string("agent tools: global policy load: ") + gerr);
        }
        {
            std::vector<std::string> valid;
            valid.reserve(turn.disabled_path_tools.size());
            for (const auto& n : turn.disabled_path_tools) {
                if (pm::llm::agent_tool_find_by_name(n))
                    valid.push_back(n);
            }
            turn.disabled_path_tools = std::move(valid);
        }

        run.tools = turn.disabled_path_tools.empty()
                  ? path::tool_catalog_openai()
                  : path::tool_catalog_openai_excluding(turn.disabled_path_tools);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// McpFeature
//   Merges MCP tools from mcp.json into run.tools and creates the bridge.
//   Must run after ToolsFeature so the base catalog is already present.
//   Respects turn.mcp_tools_enabled (master toggle) and
//   turn.disabled_mcp_servers (per-server toggle).
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Converts a server key to the same slug embedded in tool names by
// AgentMcpBridge (matches openai_slug() in agent_mcp_bridge.cpp).
std::string to_mcp_slug(std::string_view in)
{
    std::string o;
    o.reserve(in.size());
    for (unsigned char c : in) {
        if (std::isalnum(c))     o += static_cast<char>(std::tolower(c));
        else if (c == '_' || c == '-') o += static_cast<char>(c);
        else                     o += '_';
    }
    return o.empty() ? "srv" : o;
}

// Extracts the server slug from "mcp_<server>__<tool>".
// Returns empty string if the name is not an MCP tool.
std::string mcp_server_slug_from_fn(const std::string& fn_name)
{
    if (fn_name.size() < 5 || fn_name.compare(0, 4, "mcp_") != 0) return {};
    const auto sep = fn_name.find("__", 4);
    return sep != std::string::npos ? fn_name.substr(4, sep - 4) : fn_name.substr(4);
}

} // namespace

#if FEATURE_MCP_CLIENT
class McpFeature final : public IAgentFeature {
public:
    void on_prepare(Turn& turn, const ProviderSettings& provider, PreparedRun& run) override {
        if (provider.max_iterations <= 0) return;
        if (!turn.mcp_tools_enabled)      return;  // master toggle off — skip entirely

        std::string mcp_warn;
        run.mcp_bridge = mcp::AgentMcpBridge::try_create({}, run.tools, mcp_warn);
        if (!mcp_warn.empty())
            logger::debug(std::string("agent MCP: ") + mcp_warn);
        // AgentMcpBridge::try_create already merged the mcp_* entries into run.tools.

        // Per-server filter: remove tools from disabled servers.
        if (!turn.disabled_mcp_servers.empty() && run.tools.is_array()) {
            std::unordered_set<std::string> disabled_slugs;
            disabled_slugs.reserve(turn.disabled_mcp_servers.size());
            for (const auto& key : turn.disabled_mcp_servers)
                disabled_slugs.insert(to_mcp_slug(key));

            nlohmann::json filtered = nlohmann::json::array();
            for (const auto& t : run.tools) {
                if (t.contains("function") && t["function"].is_object()) {
                    const std::string fn  = t["function"].value("name", std::string{});
                    const std::string srv = mcp_server_slug_from_fn(fn);
                    if (!srv.empty() && disabled_slugs.count(srv))
                        continue;  // drop this tool
                }
                filtered.push_back(t);
            }
            run.tools = std::move(filtered);
        }
    }
};
#endif

#endif // FEATURE_HOME_LLM_TOOLS

// ═══════════════════════════════════════════════════════════════════════════════
// MemoryFeature
//   Installs the thread-local task-ID scope so memory_write/memory_read know
//   which session or scheduled-task context to target during this run.
// ═══════════════════════════════════════════════════════════════════════════════

struct TaskIdGuard : FeatureScope {
    bool active;
    explicit TaskIdGuard(const std::string& id) : active(!id.empty()) {
        if (active) set_current_task_id(id);
    }
    ~TaskIdGuard() override {
        if (active) clear_current_task_id();
    }
};

class MemoryFeature final : public IAgentFeature {
public:
    void on_prepare(Turn& turn, const ProviderSettings& provider, PreparedRun& run) override {
        run.add_scope(std::make_unique<TaskIdGuard>(turn.task_id));
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// SkillsFeature
//   Resolves roaming/workspace skills and appends an agent-readable section into
//   turn.system_extra so SystemPromptFeature includes it.
// ═══════════════════════════════════════════════════════════════════════════════

#if FEATURE_HOME_LLM_SKILLS

class SkillsFeature final : public IAgentFeature {
public:
    void on_prepare(Turn& turn, const ProviderSettings&, PreparedRun&) override {
        media::runtime_settings::AgentSkillsSettings cfg{};
        std::string serr;
        (void)media::runtime_settings::load_agent_skills_settings(cfg, serr);
        media::llm::skills::SkillPolicy policy;
        policy.enabled = cfg.enabled;
        policy.roaming_enabled = cfg.roaming_enabled;
        policy.workspace_enabled = cfg.workspace_enabled;
        policy.pinned = cfg.pinned;
        policy.disabled = cfg.disabled;

        const std::string cwd_hint = !turn.folder_hint.empty()
            ? turn.folder_hint
            : std::filesystem::current_path().string();
        const auto snap = media::llm::skills::discover_skills(policy, cwd_hint);

        std::ostringstream s;
        s << "## Skills context\n"
          << "- skills_enabled: " << (snap.policy.enabled ? "true" : "false") << "\n"
          << "- roaming_root: " << snap.roaming_root.string() << "\n"
          << "- workspace_root: " << snap.workspace_root.string() << "\n";
        if (!serr.empty())
            s << "- settings_warning: " << serr << "\n";
        if (snap.entries.empty()) {
            s << "- discovered: none\n";
        } else {
            s << "- discovered:\n";
            for (const auto& e : snap.entries) {
                s << "  - " << e.name
                  << " [" << (e.source == media::llm::skills::SkillSource::Roaming ? "roaming" : "workspace") << "]"
                  << " available=" << (e.available ? "true" : "false")
                  << " active=" << (e.active ? "true" : "false");
                if (!e.missing_requirements.empty()) {
                    s << " missing=";
                    for (std::size_t i = 0; i < e.missing_requirements.size(); ++i) {
                        if (i) s << ",";
                        s << e.missing_requirements[i];
                    }
                }
                s << "\n";
            }
        }

        // Inject full SKILL.md bodies for effective active skills only.
        // Effective active = global enabled + source enabled + available + !disabled + (always || pinned)
        // (already computed by discover_skills as e.active).
        if (snap.policy.enabled) {
            constexpr std::size_t k_max_per_skill_bytes = 24 * 1024; // 24 KiB per active skill
            constexpr std::size_t k_max_total_bytes     = 96 * 1024; // total injection budget
            std::size_t total_injected = 0;
            int active_count = 0;
            for (const auto& e : snap.entries)
                if (e.active)
                    ++active_count;

            s << "- active_skill_count: " << active_count << "\n";
            if (active_count > 0)
                s << "\n## Active skill bodies (injected)\n";

            for (const auto& e : snap.entries) {
                if (!e.active)
                    continue;
                if (total_injected >= k_max_total_bytes) {
                    s << "- [truncated] active skill injection budget reached ("
                      << k_max_total_bytes << " bytes)\n";
                    break;
                }

                std::ifstream in(e.skill_md_path, std::ios::binary);
                if (!in) {
                    s << "- [read-error] " << e.name << " path=" << e.skill_md_path.string() << "\n";
                    continue;
                }

                std::string body;
                body.reserve(k_max_per_skill_bytes);
                char buf[4096];
                std::size_t read_total = 0;
                bool truncated = false;
                while (in) {
                    in.read(buf, static_cast<std::streamsize>(sizeof(buf)));
                    const std::streamsize n = in.gcount();
                    if (n <= 0)
                        break;
                    const std::size_t can_take = (read_total < k_max_per_skill_bytes)
                        ? (k_max_per_skill_bytes - read_total)
                        : 0;
                    if (can_take == 0) {
                        truncated = true;
                        break;
                    }
                    const std::size_t take = std::min<std::size_t>(static_cast<std::size_t>(n), can_take);
                    body.append(buf, take);
                    read_total += take;
                    if (take < static_cast<std::size_t>(n)) {
                        truncated = true;
                        break;
                    }
                }

                // Clamp by total budget as well.
                if (total_injected + body.size() > k_max_total_bytes) {
                    const std::size_t keep = k_max_total_bytes - total_injected;
                    if (keep < body.size()) {
                        body.resize(keep);
                        truncated = true;
                    }
                }

                s << "\n### skill `" << e.name << "`"
                  << " (" << (e.source == media::llm::skills::SkillSource::Roaming ? "roaming" : "workspace")
                  << ")\n";
                s << "path: " << e.skill_md_path.string() << "\n";
                s << "```markdown\n" << body;
                if (!body.empty() && body.back() != '\n')
                    s << "\n";
                if (truncated)
                    s << "[...truncated...]\n";
                s << "```\n";

                total_injected += body.size();
            }
        } else {
            s << "- active_skill_count: 0 (global skills disabled)\n";
        }

        const std::string block = s.str();
        if (turn.system_extra.empty())
            turn.system_extra = block;
        else
            turn.system_extra += "\n\n" + block;
    }
};

#endif // FEATURE_HOME_LLM_SKILLS

// ═══════════════════════════════════════════════════════════════════════════════
// SystemPromptFeature
//   Assembles the canonical system prompt via build_system_prompt(turn).
//   Runs last in the default feature list so every preceding feature has had
//   the chance to mutate the Turn (e.g. memory injection, selection pruning).
// ═══════════════════════════════════════════════════════════════════════════════

class SystemPromptFeature final : public IAgentFeature {
public:
    void on_prepare(Turn& turn, const ProviderSettings& provider, PreparedRun& run) override {
        // Pass run.tools (already assembled by ToolsFeature + McpFeature) so the
        // system prompt names exactly the tools the LLM will see — including MCP tools.
        const std::string built = build_system_prompt(turn, run.tools);
        if (run.system_prompt.empty())
            run.system_prompt = built;
        else
            run.system_prompt += "\n\n---\n\n" + built;
    }
};

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Factory functions (declared in agent_factory.hpp)
// ═══════════════════════════════════════════════════════════════════════════════

namespace AgentFactory {

std::shared_ptr<IAgentFeature> feature_path_scope()     { return std::make_shared<PathScopeFeature>(); }
#if FEATURE_HOME_LLM_TOOLS
std::shared_ptr<IAgentFeature> feature_tools()          { return std::make_shared<ToolsFeature>(); }
#if FEATURE_MCP_CLIENT
std::shared_ptr<IAgentFeature> feature_mcp()            { return std::make_shared<McpFeature>(); }
#endif
#endif
std::shared_ptr<IAgentFeature> feature_memory()         { return std::make_shared<MemoryFeature>(); }
#if FEATURE_HOME_LLM_SKILLS
std::shared_ptr<IAgentFeature> feature_skills()         { return std::make_shared<SkillsFeature>(); }
#endif
std::shared_ptr<IAgentFeature> feature_system_prompt()  { return std::make_shared<SystemPromptFeature>(); }

} // namespace AgentFactory

} // namespace media::llm::agent
