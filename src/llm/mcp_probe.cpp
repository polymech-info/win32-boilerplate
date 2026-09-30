#include "mcp_probe.hpp"

#include "mcp_stdio_client.hpp"
#include "mcp_stream_client.hpp"
#include "mcp_session.hpp"

#include <fstream>
#include <sstream>

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

nlohmann::json minimal_config_echo(const nlohmann::json& srv)
{
    nlohmann::json e = nlohmann::json::object();
    if (srv.contains("type"))
        e["type"] = srv["type"];
    if (srv.contains("url"))
        e["url"] = srv["url"];
    if (srv.contains("command"))
        e["command"] = srv["command"];
    if (srv.contains("args"))
        e["args"] = srv["args"];
    return e;
}

} // namespace

nlohmann::json probe_mcp_config(const std::filesystem::path& profile_config_dir)
{
    namespace fs = std::filesystem;
    nlohmann::json out;

    fs::path dir = profile_config_dir;
    if (dir.empty())
        dir = profile_config_parent();

    const fs::path mcp_path = dir.empty() ? fs::path("mcp.json") : (dir / "mcp.json");
    out["mcp_json_path"]     = dir.empty() ? std::string("(no profile dir)/mcp.json") : mcp_path.generic_string();
    out["profile_config_dir"] =
        dir.empty() ? std::string("(empty — could not resolve profile)") : dir.generic_string();

    std::error_code ec;
    const bool      exists = !dir.empty() && fs::is_regular_file(mcp_path, ec);
    out["exists"]          = exists;
    out["servers"]         = nlohmann::json::array();

    if (dir.empty()) {
        out["note"] = "Profile directory unknown; cannot locate mcp.json (use --config-dir or the platform profile).";
        return out;
    }
    if (!exists) {
        out["note"] = "No mcp.json in profile dir — agent uses path tools only.";
        return out;
    }

    std::ifstream in(mcp_path, std::ios::binary);
    if (!in) {
        out["read_error"] = "Could not open mcp.json for reading";
        return out;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        out["parse_error"] = e.what();
        return out;
    }

    if (!root.is_object()) {
        out["parse_error"] = "mcp.json root must be a JSON object";
        return out;
    }

    if (!root.contains("mcpServers") || !root["mcpServers"].is_object()) {
        out["config_shape_error"] = R"(Expected "mcpServers": { "<name>": { ... } })";
        if (root.contains("url"))
            out["hint"] = R"(Move "url" under a server name, e.g. "mcpServers": { "OfficeMCP": { "url": "..." } }.)";
        return out;
    }

    for (auto it = root["mcpServers"].begin(); it != root["mcpServers"].end(); ++it) {
        const std::string server_name = it.key();
        nlohmann::json    one           = nlohmann::json::object();
        one["name"]                     = server_name;
        one["config"]                   = it.value().is_object() ? minimal_config_echo(it.value()) : it.value();

        if (!it.value().is_object()) {
            one["skipped"]        = true;
            one["skip_reason"]    = "value is not a JSON object (each mcpServers entry must be an object)";
            one["handshake_ok"]   = false;
            one["tools_list_ok"]  = false;
            one["tool_count"]     = 0;
            one["tools"]          = nlohmann::json::array();
            out["servers"].push_back(std::move(one));
            continue;
        }

        const auto&       srv = it.value();
        if (srv.contains("enabled") && srv["enabled"].is_boolean() && !srv["enabled"].get<bool>()) {
            one["skipped"] = true;
            one["skip_reason"] = "disabled";
            one["handshake_ok"] = false;
            one["tools_list_ok"] = false;
            one["tool_count"] = 0;
            one["tools"] = nlohmann::json::array();
            out["servers"].push_back(std::move(one));
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
        std::string                  transport;
        if (type == "stdio") {
            transport = "stdio";
            if (cmd.empty()) {
                one["skipped"]        = true;
                one["skip_reason"]    = "type stdio but command missing or empty";
                one["handshake_ok"]   = false;
                one["tools_list_ok"]  = false;
                one["tool_count"]     = 0;
                one["tools"]          = nlohmann::json::array();
                out["servers"].push_back(std::move(one));
                continue;
            }
            client = std::make_unique<StdioMcpClient>(cmd, cmd_args, std::move(env_map));
        } else if (!url.empty()) {
            transport = "http";
            client    = std::make_unique<StreamHttpClient>(url);
        } else if (!cmd.empty()) {
            transport = "stdio";
            client    = std::make_unique<StdioMcpClient>(cmd, cmd_args, std::move(env_map));
        } else {
            one["skipped"]        = true;
            one["skip_reason"]    = "need non-empty url or command";
            one["handshake_ok"]   = false;
            one["tools_list_ok"]  = false;
            one["tool_count"]     = 0;
            one["tools"]          = nlohmann::json::array();
            out["servers"].push_back(std::move(one));
            continue;
        }

        one["transport"] = transport;

        std::string init_err;
        if (!client->initialize(init_err)) {
            one["handshake_ok"]  = false;
            one["handshake_error"] = init_err;
            one["tools_list_ok"] = false;
            one["tool_count"]    = 0;
            one["tools"]         = nlohmann::json::array();
            client->try_delete_session();
            out["servers"].push_back(std::move(one));
            continue;
        }
        one["handshake_ok"] = true;

        std::string list_err;
        nlohmann::json tools = client->tools_list_all(list_err);
        client->try_delete_session();

        if (!list_err.empty()) {
            one["tools_list_ok"]   = false;
            one["tools_list_error"] = list_err;
            one["tool_count"]      = 0;
            one["tools"]           = nlohmann::json::array();
            out["servers"].push_back(std::move(one));
            continue;
        }

        one["tools_list_ok"] = true;
        nlohmann::json       tool_summaries = nlohmann::json::array();
        int                  ntools         = 0;
        if (tools.is_array()) {
            for (const auto& tj : tools) {
                if (!tj.is_object() || !tj.contains("name") || !tj["name"].is_string())
                    continue;
                ++ntools;
                nlohmann::json row;
                row["name"]        = tj["name"].get<std::string>();
                row["description"] = tj.value("description", std::string());
                tool_summaries.push_back(std::move(row));
            }
        }
        one["tool_count"] = ntools;
        one["tools"]      = std::move(tool_summaries);
        out["servers"].push_back(std::move(one));
    }

    out["note"] = "Per-server: handshake = JSON-RPC initialize + notifications/initialized; tools = tools/list. "
                  "Errors here match what the chat agent would see when loading mcp.json.";
    return out;
}

} // namespace media::llm::mcp
