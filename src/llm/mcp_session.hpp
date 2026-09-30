#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace media::llm::mcp {

/** Common surface for Streamable HTTP and stdio MCP transports (initialize → tools/list → tools/call). */
struct IMcpSession {
    virtual ~IMcpSession() = default;

    virtual bool initialize(std::string& err) = 0;
    virtual void try_delete_session()         = 0;
    virtual nlohmann::json tools_list_all(std::string& err) = 0;
    virtual nlohmann::json tools_call(const std::string& remote_tool_name, const nlohmann::json& arguments,
                                      std::string& err) = 0;
};

} // namespace media::llm::mcp
