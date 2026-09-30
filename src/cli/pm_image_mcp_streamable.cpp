#include "pm_image_mcp_streamable.hpp"

#include "llm/tool_catalog.hpp"
#include "llm/tool_executor.hpp"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#ifndef MEDIA_IMG_VERSION
#define MEDIA_IMG_VERSION "0.1.0"
#endif

namespace media::mcp_embed {
namespace {

std::mutex                                       g_mcp_sessions_mu;
std::unordered_map<std::string, nlohmann::json> g_mcp_sessions; // id -> { "protocol", ... }

std::string ascii_lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string new_session_id()
{
    static std::mutex              rng_mu;
    static std::mt19937            gen{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 15);
    std::lock_guard<std::mutex>    lock(rng_mu);
    static const char*             hex = "0123456789abcdef";
    std::string                    out;
    out.resize(32);
    for (int i = 0; i < 32; ++i)
        out[static_cast<size_t>(i)] = hex[dist(gen)];
    return out;
}

bool accept_includes(const std::string& accept, const char* needle_lc)
{
    const std::string a = ascii_lower(accept);
    return a.find(needle_lc) != std::string::npos;
}

bool post_accept_ok(const httplib::Request& req)
{
    const std::string a = req.get_header_value("Accept");
    return accept_includes(a, "application/json") && accept_includes(a, "text/event-stream");
}

bool get_accept_sse_ok(const httplib::Request& req)
{
    return accept_includes(req.get_header_value("Accept"), "text/event-stream");
}

bool origin_ok_for_local_mcp(const httplib::Request& req)
{
    if (!req.has_header("Origin"))
        return true;
    const std::string o  = req.get_header_value("Origin");
    const std::string ol = ascii_lower(o);
    if (o == "null" || ol == "null")
        return true;
    if (ol.find("127.0.0.1") != std::string::npos || ol.find("localhost") != std::string::npos)
        return true;
    if (ol.rfind("vscode-file://", 0) == 0 || ol.rfind("cursor://", 0) == 0)
        return true;
    if (ol.rfind("http://127.0.0.1", 0) == 0 || ol.rfind("http://localhost", 0) == 0)
        return true;
    if (ol.rfind("https://127.0.0.1", 0) == 0 || ol.rfind("https://localhost", 0) == 0)
        return true;
    return false;
}

void jsonrpc_error(httplib::Response& res, int http_status, const nlohmann::json& id, int code,
                   const std::string& message)
{
    res.status = http_status;
    nlohmann::json err;
    err["jsonrpc"] = "2.0";
    if (!id.is_null())
        err["id"] = id;
    err["error"] = nlohmann::json{{"code", code}, {"message", message}};
    res.set_content(err.dump(), "application/json; charset=utf-8");
}

nlohmann::json tools_list_mcp_shape()
{
    const nlohmann::json cat = media::llm::tool_catalog_json();
    nlohmann::json       tools = nlohmann::json::array();
    if (!cat.contains("tools") || !cat["tools"].is_array())
        return tools;
    for (const auto& t : cat["tools"]) {
        if (!t.is_object() || !t.contains("name"))
            continue;
        nlohmann::json one;
        one["name"]        = t["name"];
        one["description"] = t.value("description", "");
        if (t.contains("input_schema"))
            one["inputSchema"] = t["input_schema"];
        else
            one["inputSchema"] = nlohmann::json::object({{"type", "object"}, {"additionalProperties", true}});
        tools.push_back(std::move(one));
    }
    return tools;
}

nlohmann::json mcp_call_tool_result(const media::llm::ExecuteResult& er)
{
    nlohmann::json result;
    result["isError"] = !er.ok;
    nlohmann::json content = nlohmann::json::array();
    if (!er.ok) {
        const std::string msg = !er.error.empty() ? er.error : er.envelope.dump();
        content.push_back(nlohmann::json{{"type", "text"}, {"text", msg}});
        result["content"] = std::move(content);
        return result;
    }
    const nlohmann::json& env = er.envelope;
    if (env.value("ok", false) && env.contains("mime") && env.contains("b64")) {
        content.push_back(nlohmann::json{{"type", "image"},
                                          {"data", env["b64"].get<std::string>()},
                                          {"mimeType", env["mime"].get<std::string>()}});
        result["content"] = std::move(content);
        return result;
    }
    content.push_back(nlohmann::json{{"type", "text"}, {"text", env.dump()}});
    result["content"] = std::move(content);
    return result;
}

std::string negotiate_protocol_version(const std::string& requested)
{
    if (requested == "2025-11-25" || requested == "2025-03-26" || requested == "2024-11-05")
        return requested;
    return "2025-11-25";
}

void handle_mcp_post(const httplib::Request& req, httplib::Response& res)
{
    if (!origin_ok_for_local_mcp(req)) {
        res.status = 403;
        res.set_content(R"({"jsonrpc":"2.0","error":{"code":-32000,"message":"invalid Origin"}})",
                        "application/json; charset=utf-8");
        return;
    }
    if (!post_accept_ok(req)) {
        res.status = 406;
        res.set_content(R"({"jsonrpc":"2.0","error":{"code":-32000,"message":"Accept must include application/json and text/event-stream"}})",
                        "application/json; charset=utf-8");
        return;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
        jsonrpc_error(res, 400, nlohmann::json(), -32700, "Parse error");
        return;
    }

    const nlohmann::json id_field = root.contains("id") ? root["id"] : nlohmann::json(nullptr);
    if (!root.contains("method") || !root["method"].is_string()) {
        jsonrpc_error(res, 400, id_field, -32600, "Invalid Request");
        return;
    }
    const std::string method = root["method"].get<std::string>();

    const std::string session_hdr = req.get_header_value("MCP-Session-Id");
    const std::string proto_hdr   = req.get_header_value("MCP-Protocol-Version");

    auto send_202 = [&]() {
        res.status = 202;
        res.set_header("MCP-Protocol-Version", proto_hdr.empty() ? "2025-11-25" : negotiate_protocol_version(proto_hdr));
        if (!session_hdr.empty())
            res.set_header("MCP-Session-Id", session_hdr);
        res.set_content("", "application/json");
    };

    if (method.rfind("notifications/", 0) == 0) {
        send_202();
        return;
    }

    if (method == "initialize") {
        if (!root.contains("id")) {
            jsonrpc_error(res, 400, nlohmann::json(), -32600, "initialize requires jsonrpc id");
            return;
        }
        nlohmann::json params = root.contains("params") && root["params"].is_object() ? root["params"]
                                                                                       : nlohmann::json::object();
        const std::string client_proto =
            params.contains("protocolVersion") && params["protocolVersion"].is_string()
                ? params["protocolVersion"].get<std::string>()
                : "2025-11-25";
        const std::string negotiated = negotiate_protocol_version(client_proto);

        const std::string sid = new_session_id();
        {
            std::lock_guard<std::mutex> lock(g_mcp_sessions_mu);
            g_mcp_sessions[sid] = nlohmann::json{{"protocol", negotiated}};
        }

        nlohmann::json result;
        result["protocolVersion"] = negotiated;
        result["capabilities"]    = nlohmann::json{{"tools", nlohmann::json{{"listChanged", false}}}};
        result["serverInfo"] =
            nlohmann::json{{"name", "pm-image"}, {"version", std::string(MEDIA_IMG_VERSION)}};

        nlohmann::json out;
        out["jsonrpc"] = "2.0";
        out["id"]      = root["id"];
        out["result"]  = std::move(result);

        res.status = 200;
        res.set_header("MCP-Session-Id", sid);
        res.set_header("MCP-Protocol-Version", negotiated);
        res.set_content(out.dump(), "application/json; charset=utf-8");
        return;
    }

    if (session_hdr.empty()) {
        jsonrpc_error(res, 400, id_field, -32000, "MCP-Session-Id header required (send initialize first)");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mcp_sessions_mu);
        if (!g_mcp_sessions.count(session_hdr)) {
            jsonrpc_error(res, 404, id_field, -32001, "Unknown MCP session");
            return;
        }
    }

    const std::string negotiated_sess = [&] {
        std::lock_guard<std::mutex> lock(g_mcp_sessions_mu);
        return g_mcp_sessions[session_hdr].value("protocol", std::string("2025-11-25"));
    }();
    res.set_header("MCP-Protocol-Version", negotiated_sess);

    if (method == "ping") {
        nlohmann::json out{{"jsonrpc", "2.0"}, {"id", root["id"]}, {"result", nlohmann::json::object()}};
        res.status = 200;
        res.set_header("MCP-Session-Id", session_hdr);
        res.set_content(out.dump(), "application/json; charset=utf-8");
        return;
    }

    if (method == "tools/list") {
        nlohmann::json out{{"jsonrpc", "2.0"}, {"id", root["id"]}, {"result", nlohmann::json{{"tools", tools_list_mcp_shape()}}}};
        res.status = 200;
        res.set_header("MCP-Session-Id", session_hdr);
        res.set_content(out.dump(), "application/json; charset=utf-8");
        return;
    }

    if (method == "tools/call") {
        const nlohmann::json params = root.contains("params") && root["params"].is_object() ? root["params"]
                                                                                             : nlohmann::json::object();
        if (!params.contains("name") || !params["name"].is_string()) {
            jsonrpc_error(res, 400, id_field, -32602, "tools/call requires params.name");
            return;
        }
        const std::string          name = params["name"].get<std::string>();
        const nlohmann::json         args =
            params.contains("arguments") && params["arguments"].is_object() ? params["arguments"]
                                                                            : nlohmann::json::object();
        const media::llm::ExecuteResult er = media::llm::execute(name, args);
        nlohmann::json                   out{{"jsonrpc", "2.0"},
                           {"id", root["id"]},
                           {"result", mcp_call_tool_result(er)}};
        res.status = 200;
        res.set_header("MCP-Session-Id", session_hdr);
        res.set_content(out.dump(), "application/json; charset=utf-8");
        return;
    }

    jsonrpc_error(res, 200, id_field, -32601, std::string("method not found: ") + method);
}

void handle_mcp_get(const httplib::Request& req, httplib::Response& res)
{
    if (!origin_ok_for_local_mcp(req)) {
        res.status = 403;
        return;
    }
    if (!get_accept_sse_ok(req)) {
        res.status = 406;
        res.set_content("Accept must include text/event-stream\n", "text/plain");
        return;
    }

    res.status = 200;
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider(
        "text/event-stream; charset=utf-8",
        [](size_t /*offset*/, httplib::DataSink& sink) -> bool {
            static const char prime[] = "id: 0\n\n: stream open\n\n";
            if (!sink.write(prime, sizeof(prime) - 1u))
                return false;
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(25));
                static const char ping[] = ": ping\n\n";
                if (!sink.write(ping, sizeof(ping) - 1u))
                    return false;
            }
        },
        [](bool /*success*/) {});
}

void handle_mcp_delete(const httplib::Request& req, httplib::Response& res)
{
    const std::string sid = req.get_header_value("MCP-Session-Id");
    if (!sid.empty()) {
        std::lock_guard<std::mutex> lock(g_mcp_sessions_mu);
        (void)g_mcp_sessions.erase(sid);
    }
    res.status = 204;
}

} // namespace

void register_mcp_streamable_http(httplib::Server& svr)
{
    svr.Post("/mcp", handle_mcp_post);
    svr.Get("/mcp", handle_mcp_get);
    svr.Delete("/mcp", handle_mcp_delete);
}

} // namespace media::mcp_embed
