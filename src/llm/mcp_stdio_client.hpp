#pragma once

#include "mcp_session.hpp"

#include <nlohmann/json.hpp>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace media::llm::mcp {

/**
 * MCP stdio transport: spawn `command` + `args`, newline-delimited JSON-RPC on stdin/stdout
 * ([MCP spec — stdio](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports#stdio)).
 * Matches OfficeMCP / Cursor-style `mcp.json` with `"type": "stdio"`, `"command": "uvx"`, `"args": ["officemcp"]`.
 * Optional `env` map is merged into the child process environment.
 */
class StdioMcpClient final : public IMcpSession {
public:
    StdioMcpClient(std::string command, std::vector<std::string> args,
                   std::map<std::string, std::string> env = {});
    ~StdioMcpClient() override;

    StdioMcpClient(const StdioMcpClient&)            = delete;
    StdioMcpClient& operator=(const StdioMcpClient&) = delete;

    bool initialize(std::string& err) override;
    void try_delete_session() override;

    nlohmann::json tools_list_all(std::string& err) override;
    nlohmann::json tools_call(const std::string& remote_tool_name, const nlohmann::json& arguments,
                              std::string& err) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace media::llm::mcp
