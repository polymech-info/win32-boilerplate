#pragma once

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#ifndef FEATURE_MCP_CLIENT
#define FEATURE_MCP_CLIENT 1
#endif

#if FEATURE_MCP_CLIENT
#include "mcp_session.hpp"
#include <unordered_map>
#include <vector>
#endif

namespace media::llm::mcp {

#if FEATURE_MCP_CLIENT
struct RegisteredMcpTool {
    size_t              client_index = 0;
    std::string         server_key;
    std::string         remote_tool_name;
};

/**
 * Loads `%AppData%/.../pm-image/mcp.json` (same profile root as settings / system-prompt.md on Windows;
 * macOS/Linux/Unix: same folder as portable `settings.json` (e.g. `~/Library/…/pm-image`, `~/.pm-image`,
 * or `--config-dir`). Connects to each `mcpServers` entry:
 * **Streamable HTTP** (`url`, e.g. Cursor) or **stdio** (`type: "stdio"`, `command` + `args`, e.g.
 * [OfficeMCP](https://github.com/officemcp/officemcp) with `uvx officemcp`). Tools are merged into the
 * OpenAI `tools` array as `mcp_<server>__<tool>` (sanitized).
 */
class AgentMcpBridge {
public:
    /**
     * @param profile_config_dir directory containing mcp.json (may be empty → nullptr)
     * @param inout_openai_tools existing array; MCP function tools are appended
     * @param warnings_out human-readable lines for logs (optional diagnostics)
     */
    static std::unique_ptr<AgentMcpBridge> try_create(const std::filesystem::path& profile_config_dir,
                                                      nlohmann::json&                inout_openai_tools,
                                                      std::string&                   warnings_out);

    ~AgentMcpBridge();

    AgentMcpBridge(const AgentMcpBridge&)            = delete;
    AgentMcpBridge& operator=(const AgentMcpBridge&) = delete;

    bool empty() const { return clients_.empty(); }

    bool is_mcp_openai_function(const std::string& name) const;

    /** On success sets @p result_json to a JSON string suitable for a `tool` message. */
    bool call_tool(const std::string& openai_function_name, const nlohmann::json& arguments, std::string& result_json,
                   std::string& err);

private:
    AgentMcpBridge() = default;

    std::vector<std::unique_ptr<IMcpSession>> clients_;
    std::unordered_map<std::string, RegisteredMcpTool>   registry_;
};
#else
class AgentMcpBridge {
public:
    static std::unique_ptr<AgentMcpBridge> try_create(const std::filesystem::path&,
                                                      nlohmann::json&,
                                                      std::string& warnings_out)
    {
        warnings_out.clear();
        return nullptr;
    }

    bool empty() const { return true; }
    bool is_mcp_openai_function(const std::string&) const { return false; }
    bool call_tool(const std::string&, const nlohmann::json&, std::string&, std::string& err)
    {
        err = "MCP client support is disabled in this build (FEATURE_MCP_CLIENT=OFF).";
        return false;
    }
};
#endif

} // namespace media::llm::mcp
