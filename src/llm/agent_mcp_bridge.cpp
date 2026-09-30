#include "agent_mcp_bridge.hpp"

#include "mcp_stdio_client.hpp"
#include "mcp_stream_client.hpp"

#include "logger/logger.h"

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>

#include "core/settings_store.hpp"

namespace media::llm::mcp {
namespace {

std::filesystem::path profile_config_parent()
{
    namespace fs = std::filesystem;
    try {
        return media::settings::get_config_dir();
    } catch (...) {
        return {};
    }
}

std::string openai_slug(std::string_view in)
{
    std::string o;
    o.reserve(in.size());
    for (unsigned char c : in) {
        if (std::isalnum(c))
            o += static_cast<char>(std::tolower(c));
        else if (c == '_' || c == '-')
            o += static_cast<char>(c);
        else
            o += '_';
    }
    if (o.empty())
        return "srv";
    return o;
}

std::string truncate_fn_name(std::string s)
{
    // OpenAI function names: practical limit 64 characters.
    if (s.size() > 64)
        s.resize(64);
    return s;
}

std::string make_prefixed_tool_name(const std::string& server_key, const std::string& remote_tool,
                                    const std::unordered_map<std::string, RegisteredMcpTool>& existing)
{
    std::string base = truncate_fn_name("mcp_" + openai_slug(server_key) + "__" + openai_slug(remote_tool));
    if (!existing.count(base))
        return base;
    for (int i = 2; i < 1000; ++i) {
        const std::string suffix = "_" + std::to_string(i);
        std::string       cand   = base;
        if (cand.size() + suffix.size() > 64)
            cand.resize(64 - suffix.size());
        cand += suffix;
        if (!existing.count(cand))
            return cand;
    }
    return base + "_x";
}

nlohmann::json parameters_from_mcp_tool(const nlohmann::json& tool_json)
{
    if (tool_json.contains("inputSchema") && tool_json["inputSchema"].is_object())
        return tool_json["inputSchema"];
    return nlohmann::json{{"type", "object"}, {"additionalProperties", true}};
}

} // namespace

std::unique_ptr<AgentMcpBridge> AgentMcpBridge::try_create(const std::filesystem::path& profile_config_dir,
                                                          nlohmann::json&                inout_openai_tools,
                                                          std::string&                   warnings_out)
{
    warnings_out.clear();
    namespace fs = std::filesystem;

    fs::path dir = profile_config_dir;
    if (dir.empty())
        dir = profile_config_parent();
    if (dir.empty()) {
        warnings_out = "MCP: no profile config directory (skip mcp.json)\n";
        return nullptr;
    }

    const fs::path mcp_path = dir / "mcp.json";
    std::error_code ec;
    if (!fs::is_regular_file(mcp_path, ec)) {
        return nullptr;
    }

    std::ifstream in(mcp_path, std::ios::binary);
    if (!in) {
        warnings_out = "MCP: could not open mcp.json\n";
        return nullptr;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        warnings_out = std::string("MCP: mcp.json parse error: ") + e.what() + "\n";
        return nullptr;
    }

    if (!root.contains("mcpServers") || !root["mcpServers"].is_object()) {
        warnings_out = "MCP: mcp.json has no mcpServers object (same shape as Cursor: mcpServers.<name>.url)\n";
        return nullptr;
    }

    auto bridge = std::unique_ptr<AgentMcpBridge>(new AgentMcpBridge());

    if (!inout_openai_tools.is_array())
        inout_openai_tools = nlohmann::json::array();

    for (auto it = root["mcpServers"].begin(); it != root["mcpServers"].end(); ++it) {
        const std::string server_key = it.key();
        const auto&       srv        = it.value();
        if (!srv.is_object()) {
            warnings_out += "MCP: server \"" + server_key + "\" skipped (not an object)\n";
            continue;
        }
        if (srv.contains("enabled") && srv["enabled"].is_boolean() && !srv["enabled"].get<bool>()) {
            warnings_out += "MCP: server \"" + server_key + "\" skipped (disabled)\n";
            continue;
        }

        std::string type;
        if (srv.contains("type") && srv["type"].is_string())
            type = srv["type"].get<std::string>();
        std::string       url;
        if (srv.contains("url") && srv["url"].is_string())
            url = srv["url"].get<std::string>();

        std::string              cmd;
        std::vector<std::string> cmd_args;
        if (srv.contains("command") && srv["command"].is_string())
            cmd = srv["command"].get<std::string>();
        if (srv.contains("args") && srv["args"].is_array()) {
            for (const auto& a : srv["args"]) {
                if (a.is_string())
                    cmd_args.push_back(a.get<std::string>());
            }
        }

        // If "command" is itself a bare URL, treat it as "url" (no npx wrapper needed).
        if (url.empty() && !cmd.empty() &&
            (cmd.compare(0, 7, "http://") == 0 || cmd.compare(0, 8, "https://") == 0)) {
            url = cmd;
            cmd.clear();
        }

        std::map<std::string, std::string> env_map;
        if (srv.contains("env") && srv["env"].is_object()) {
            for (auto eit = srv["env"].begin(); eit != srv["env"].end(); ++eit) {
                if (eit.value().is_string())
                    env_map[eit.key()] = eit.value().get<std::string>();
            }
        }

        std::unique_ptr<IMcpSession> client;
        std::string                  log_where;
        if (type == "stdio") {
            if (cmd.empty()) {
                warnings_out += "MCP: server \"" + server_key + "\" type stdio but \"command\" missing or empty\n";
                continue;
            }
            client    = std::make_unique<StdioMcpClient>(cmd, cmd_args, std::move(env_map));
            log_where = "stdio:" + cmd;
        } else if (!url.empty()) {
            client    = std::make_unique<StreamHttpClient>(url);
            log_where = url;
        } else if (!cmd.empty()) {
            client    = std::make_unique<StdioMcpClient>(cmd, cmd_args, std::move(env_map));
            log_where = std::string("stdio:") + cmd;
        } else {
            warnings_out += "MCP: server \"" + server_key + "\" skipped (need non-empty \"url\" or \"command\")\n";
            continue;
        }

        std::string init_err;
        if (!client->initialize(init_err)) {
            std::ostringstream oss;
            oss << "MCP: server \"" << server_key << "\" (" << log_where << ") failed to initialize: " << init_err
                << "\n";
            warnings_out += oss.str();
            continue;
        }

        std::string list_err;
        nlohmann::json tools = client->tools_list_all(list_err);
        if (!list_err.empty()) {
            std::ostringstream oss;
            oss << "MCP: server \"" << server_key << "\" tools/list failed: " << list_err << "\n";
            warnings_out += oss.str();
            client->try_delete_session();
            continue;
        }
        if (!tools.is_array() || tools.empty()) {
            warnings_out += "MCP: server \"" + server_key + "\" returned no tools\n";
            client->try_delete_session();
            continue;
        }

        const size_t client_index = bridge->clients_.size();
        bridge->clients_.push_back(std::move(client));

        for (const auto& tj : tools) {
            if (!tj.is_object() || !tj.contains("name") || !tj["name"].is_string())
                continue;
            const std::string remote_name = tj["name"].get<std::string>();
            const std::string openai_name = make_prefixed_tool_name(server_key, remote_name, bridge->registry_);
            const std::string desc        = tj.value("description", std::string());

            RegisteredMcpTool reg;
            reg.client_index     = client_index;
            reg.server_key       = server_key;
            reg.remote_tool_name = remote_name;
            bridge->registry_.emplace(openai_name, std::move(reg));

            inout_openai_tools.push_back(nlohmann::json{
                {"type", "function"},
                {"function",
                 {{"name", openai_name},
                  {"description", std::string("[MCP ") + server_key + "] " + desc},
                  {"parameters", parameters_from_mcp_tool(tj)}}}});
        }

        logger::info(std::string("MCP: connected server \"") + server_key + "\" via " + log_where + " tools="
                     + std::to_string(tools.size()));
    }

    if (bridge->clients_.empty()) {
        bridge.reset();
        if (warnings_out.empty())
            warnings_out = "MCP: no usable servers in mcp.json\n";
    }
    return bridge;
}

AgentMcpBridge::~AgentMcpBridge()
{
    for (auto& c : clients_) {
        if (c)
            c->try_delete_session();
    }
}

bool AgentMcpBridge::is_mcp_openai_function(const std::string& name) const
{
    return registry_.count(name) != 0;
}

bool AgentMcpBridge::call_tool(const std::string& openai_function_name, const nlohmann::json& arguments,
                               std::string& result_json, std::string& err)
{
    err.clear();
    result_json.clear();
    const auto it = registry_.find(openai_function_name);
    if (it == registry_.end()) {
        err = "unknown MCP tool binding";
        return false;
    }
    if (it->second.client_index >= clients_.size() || !clients_[it->second.client_index]) {
        err = "MCP client index out of range";
        return false;
    }

    std::string              call_err;
    const nlohmann::json      tr = clients_[it->second.client_index]->tools_call(it->second.remote_tool_name, arguments, call_err);
    nlohmann::json            envelope;
    envelope["ok"]             = call_err.empty();
    envelope["mcp"]["server"] = it->second.server_key;
    envelope["mcp"]["tool"]   = it->second.remote_tool_name;
    if (!call_err.empty()) {
        envelope["error"] = call_err;
        result_json       = envelope.dump();
        return true;
    }
    envelope["mcp"]["result"] = tr;
    result_json               = envelope.dump();
    return true;
}

} // namespace media::llm::mcp
