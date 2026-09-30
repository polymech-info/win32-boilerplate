#include "pm_image_mcp_embed.hpp"

#include "pm_image_mcp_streamable.hpp"
#include "pm_image_cli_state.hpp"

#include "llm/path_tool_catalog.hpp"
#include "llm/path_tool_executor.hpp"
#include "llm/tool_catalog.hpp"
#include "llm/tool_executor.hpp"

#include <httplib.h>

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace media::mcp_embed {
namespace {

bool                        g_enabled         = true;
int                         g_configured_port = k_default_mcp_port;
std::string                 g_bind_host;
int                         g_effective_port  = 0;
std::shared_ptr<httplib::Server> g_http;
std::once_flag              g_start_once;

static void stop_mcp_at_exit()
{
    if (g_http)
        g_http->stop();
}

static bool stderr_is_character_console()
{
#if defined(_WIN32)
    const HANDLE h = ::GetStdHandle(STD_ERROR_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE)
        return false;
    return ::GetFileType(h) == FILE_TYPE_CHAR;
#else
    return ::isatty(2) != 0;
#endif
}

/** When set, forces embedded MCP on or off regardless of `--mcp` / `--no-mcp` (CI / scripts). */
static void apply_pm_image_mcp_env_override(bool* enabled)
{
    const char* v = std::getenv("PM_IMAGE_MCP");
    if (!v || v[0] == '\0')
        return;
    std::string s(v);
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "0" || s == "false" || s == "off" || s == "no")
        *enabled = false;
    else if (s == "1" || s == "true" || s == "on" || s == "yes")
        *enabled = true;
}

static void register_llm_routes(httplib::Server& svr)
{
    register_mcp_streamable_http(svr);

    svr.Get("/v1/llm/tools/list", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(media::llm::tool_catalog_json().dump(), "application/json");
    });

    svr.Post("/v1/llm/tools/call", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"invalid JSON body"})json", "application/json");
            return;
        }
        if (!body.contains("name") || !body["name"].is_string()) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"'name' (string) is required"})json", "application/json");
            return;
        }
        const std::string          name = body["name"].get<std::string>();
        const nlohmann::json       args =
            body.contains("arguments") && body["arguments"].is_object() ? body["arguments"]
                                                                       : nlohmann::json::object();
        const media::llm::ExecuteResult out = media::llm::execute(name, args);
        if (!out.ok)
            res.status = 200;
        res.set_content(out.envelope.dump(), "application/json");
    });

    svr.Get("/v1/llm/path-tools/list", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(media::llm::path::tool_catalog_json().dump(), "application/json");
    });

    svr.Post("/v1/llm/path-tools/call", [](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
        } catch (...) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"invalid JSON body"})json", "application/json");
            return;
        }
        if (!body.contains("name") || !body["name"].is_string()) {
            res.status = 400;
            res.set_content(R"json({"ok":false,"error":"'name' (string) is required"})json", "application/json");
            return;
        }
        const std::string                name = body["name"].get<std::string>();
        const nlohmann::json             args =
            body.contains("arguments") && body["arguments"].is_object() ? body["arguments"]
                                                                        : nlohmann::json::object();
        const media::llm::path::ExecuteResult out = media::llm::path::execute(name, args);
        if (!out.ok)
            res.status = 200;
        res.set_content(out.envelope.dump(), "application/json");
    });
}

} // namespace

namespace {

void trim_ascii_ws(std::string& s)
{
    const auto not_space = [](unsigned char c) { return c > ' '; };
    while (!s.empty() && !not_space(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && !not_space(static_cast<unsigned char>(s.back())))
        s.pop_back();
}

} // namespace

void apply_after_cli_parse(const PmImageCliState& s)
{
    bool en = s.mcp_enabled;
    apply_pm_image_mcp_env_override(&en);
    g_enabled         = en;
    g_configured_port = s.mcp_port;
    g_bind_host       = s.mcp_bind;
    trim_ascii_ws(g_bind_host);
    if (g_bind_host.empty()) {
#if defined(_WIN32)
        g_bind_host = "0.0.0.0";
#else
        g_bind_host = "127.0.0.1";
#endif
    }
    if (!g_enabled)
        g_effective_port = 0;
}

void start_embedded_mcp_if_enabled()
{
    if (!g_enabled)
        return;

    std::call_once(g_start_once, [] {
        int                              chosen = 0;
        std::shared_ptr<httplib::Server> svr;
        for (int step = 0; step < 64; ++step) {
            const int p = g_configured_port + step;
            if (p > 65535)
                break;
            auto trial = std::make_shared<httplib::Server>();
            register_llm_routes(*trial);
            if (trial->bind_to_port(g_bind_host, p)) {
                chosen = p;
                svr    = std::move(trial);
                break;
            }
        }
        if (chosen <= 0 || !svr) {
            std::cerr << "pm-image: embedded MCP: could not bind " << g_bind_host << " from port " << g_configured_port
                      << " (tried 64 ports)\n";
            return;
        }

        g_effective_port = chosen;
        g_http           = svr;
        (void)std::atexit(stop_mcp_at_exit);

        std::thread([svr] {
            (void)svr->listen_after_bind();
        }).detach();

        if (stderr_is_character_console()) {
            std::cerr << "pm-image: embedded MCP http://" << g_bind_host << ":" << chosen
                      << " - POST|GET /mcp (Cursor MCP Streamable HTTP), "
                         "GET /v1/llm/tools/list, POST /v1/llm/tools/call, "
                         "GET /v1/llm/path-tools/list, POST /v1/llm/path-tools/call";
            if (g_bind_host == "0.0.0.0" || g_bind_host == "*")
                std::cerr << " - loopback http://127.0.0.1:" << chosen;
            std::cerr << "\n";
        }
    });
}

bool enabled() noexcept
{
    return g_enabled;
}

int configured_port() noexcept
{
    return g_configured_port;
}

int effective_listen_port() noexcept
{
    return g_effective_port;
}

} // namespace media::mcp_embed
