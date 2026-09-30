// ChatWebPanel.cpp — WebView2-hosted chat composer.
//
// Boots a CoreWebView2Environment + Controller inside the dock view's
// client area, navigates to apps/chat-next HTML (dist/shared/chat.html), and bridges
// JSON messages between the web UI and media::llm::agent::run_turn.
//
// Compile-gated: this TU is only added to the main app target when FEATURE_CHAT_WEB=ON
// (see CMakeLists.txt). The header still declares the classes; consumers
// guard their usage with `#ifdef FEATURE_CHAT_WEB` or check
// pmui::chat_web_available() at runtime.

#include "stdafx.h"
#include "constants.hpp"
#include "ChatWebPanel.h"
#include "ChatImageFullscreenHost.h"
#include "ChatWebResource.h"
#include "Resource.h"
#include "FileQueue.h"
#include "FindPanel.h"
#include "helpers/text_conv.hpp"
#include "log_sink.h"
#include "logger/logger.h"

#include "core/cli_cancel.hpp"
#include "llm/agent.hpp"
#include "llm/agent_memory.hpp"
#include "llm/agent_scheduler.hpp"
#include "llm/path_tool_executor.hpp"
#include "llm/mcp_probe.hpp"
#include "ProviderModelRegistry.h"
#include "replicate_provider_models_cli.hpp"
#include "pm_image_cli_helpers.hpp"
#include "core/openrouter_provider_models_cli.hpp"
#include "core/openai_models_cli.hpp"
#include "core/settings_runtime.hpp"
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
#  include "win/pixlwiz_auth_payload.hpp"
#endif
#include "win/settings_store.hpp"
#include "helpers/dock_chrome_i18n.hpp"
#include "file_extensions.hpp"
#include "helpers/chat_context_attach.hpp"
#include "helpers/theme.hpp"
#include "helpers/splash_window.hpp"
#include "helpers/win_ui_debug.hpp"
#include "ui_log_file.hpp"
#include "win/web/CWebViewManager.h"
#include "win/web/webview_bootstrap.hpp"

#include <algorithm>
#include <filesystem>

#include <WebView2.h>

#include <shellapi.h>
#include "helpers/default_shell.hpp"
#include "llm/llm_fs_guard.hpp"
#include <shlobj.h>
#include <oleidl.h>
#include <dwmapi.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Version.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;

namespace fs = std::filesystem;

// ── User-message types posted from background threads ────────────────────
//
// All WebView2 calls must happen on the UI thread. Worker threads use
// PostMessage to bounce work to the view's WndProc, which then runs the
// COM call inside ProcessUiQueue() / OnAgentEvent() / OnTurnDone().
//
// wparam = std::wstring* (UTF-8 JS source to ExecuteScriptAsync); freed by handler.
// FEATURE_STT: real-time mic → ElevenLabs STT inside the chat composer.
#if defined(FEATURE_STT) && FEATURE_STT
#  include "core/audio.hpp"
#  include "stt/elevenlabs.hpp"
#  include <cmath>
#endif
// FEATURE_VIDEO: webcam still capture → attach to chat context.
#if defined(FEATURE_VIDEO) && FEATURE_VIDEO
#  include "core/video.hpp"
#endif

constexpr UINT UWM_WEB_RUN_JS         = WM_USER + 140;
// wparam = bool ok (0/1); lparam = std::wstring* error message; freed.
constexpr UINT UWM_WEB_TURN_DONE      = WM_USER + 141;
// lparam = nlohmann::json* slim mcp_catalog for setStatus (Web sidebar); freed by handler.
constexpr UINT UWM_MCP_CATALOG_READY  = WM_USER + 142;
// lparam = nlohmann::json* { spend, max_budget } from pixlwiz LiteLLM /v2/user/info; freed.
constexpr UINT UWM_PIXLWIZ_CREDIT_READY = WM_USER + 143;

namespace {

// JSON-escape arbitrary text for inclusion in a JS double-quoted string.
std::wstring js_quote(const std::wstring& s) {
    nlohmann::json j = pmui::wide_to_utf8(s);
    return pmui::utf8_to_wide(j.dump());     // returns "..."  with proper escapes
}

static fs::path chat_features_module_exe_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size())
        return {};
    buf.resize(n);
    return fs::path(buf).parent_path();
}

static nlohmann::json chat_load_app_features()
{
    const fs::path exe = chat_features_module_exe_dir();
    std::vector<fs::path> candidates;
    if (!exe.empty()) {
        if (exe.filename() == L"win-x64")
            candidates.push_back(exe.parent_path() / L"shared" / L"features.json");
        candidates.push_back(exe / L"shared" / L"features.json");
        candidates.push_back(exe / L"features.json");
    }
    try {
        candidates.push_back(fs::current_path() / L"dist" / L"shared" / L"features.json");
    } catch (...) {
    }

    for (const auto& candidate : candidates) {
        try {
            std::ifstream in(candidate, std::ios::binary);
            if (!in)
                continue;
            nlohmann::json features;
            in >> features;
            if (features.is_object())
                return features;
        } catch (...) {
            logger::warn(std::string("[chat-web] failed to parse features.json: ")
                + pmui::wide_to_utf8(candidate.wstring()));
        }
    }
    return nlohmann::json::object();
}

static std::wstring chat_app_features_bootstrap_js()
{
    const std::string json = chat_load_app_features().dump();
    const std::string js =
        "(function(){"
        "var f=" + json + ";"
        "try{Object.freeze(f);}catch(e){}"
        "try{Object.defineProperty(window,'APP_FEATURES',{value:f,writable:false,configurable:false});}"
        "catch(e){window.APP_FEATURES=f;}"
        "})();";
    return pmui::utf8_to_wide(js);
}

/** Truncate UTF-8 by byte length without splitting a multibyte code unit (avoids U+FFFD in UI). */
static std::string utf8_safe_prefix(const std::string& s, size_t max_bytes) {
    if (s.size() <= max_bytes)
        return s;
    std::string out = s.substr(0, max_bytes);
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xc0) == 0x80)
        out.pop_back();
    return out;
}

/** Background: probe profile mcp.json (same as `llm info`) and post a slim catalog to the WebView host. */
static void chat_web_start_mcp_catalog_probe(HWND host_hwnd)
{
    if (!host_hwnd || !::IsWindow(host_hwnd))
        return;
    std::thread([host_hwnd]() {
        nlohmann::json slim = nlohmann::json::object();
        try {
            const nlohmann::json probe = media::llm::mcp::probe_mcp_config({});
            nlohmann::json       servers = nlohmann::json::array();
            if (probe.contains("servers") && probe["servers"].is_array()) {
                for (const auto& s : probe["servers"]) {
                    nlohmann::json row;
                    row["name"] = s.value("name", std::string{});
                    if (s.contains("transport"))
                        row["transport"] = s["transport"];
                    row["handshake_ok"]  = s.value("handshake_ok", false);
                    row["tools_list_ok"] = s.value("tools_list_ok", false);
                    if (s.value("skipped", false)) {
                        row["skipped"]     = true;
                        row["skip_reason"] = s.value("skip_reason", std::string{});
                    }
                    if (s.contains("tools") && s["tools"].is_array())
                        row["tools"] = s["tools"];
                    else
                        row["tools"] = nlohmann::json::array();
                    servers.push_back(std::move(row));
                }
            }
            slim["servers"]       = std::move(servers);
            slim["mcp_json_path"] = probe.value("mcp_json_path", std::string{});
            slim["exists"]        = probe.value("exists", false);
        } catch (...) {
            slim["servers"]       = nlohmann::json::array();
            slim["exists"]        = false;
            slim["probe_error"]   = "exception";
        }
        auto* out = new nlohmann::json(std::move(slim));
        if (!::PostMessageW(host_hwnd, UWM_MCP_CATALOG_READY, 0, reinterpret_cast<LPARAM>(out)))
            delete out;
    }).detach();
}

#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
/** Background: fetch Pixlwiz (LiteLLM) budget info and post the shared credit payload to the host HWND. */
static void chat_web_start_pixlwiz_credit_fetch(HWND host_hwnd)
{
    if (!host_hwnd || !::IsWindow(host_hwnd))
        return;
    std::thread([host_hwnd]() {
        nlohmann::json result = pmui::pixlwiz_auth::read_credit_payload();
        auto* out = new nlohmann::json(std::move(result));
        if (!::PostMessageW(host_hwnd, UWM_PIXLWIZ_CREDIT_READY, 0, reinterpret_cast<LPARAM>(out)))
            delete out;
    }).detach();
}
#endif

// Reads provider settings: router/model/… from
// settings.json["chat"]; API key from providers (not stored under `chat`). No env here.
// Optional `send` JSON: string fields "router" / "model" apply one-turn overrides
// (same rules as `ChatProviderDlg` — switching away from the saved router clears
// a mismatched `base_url` so the kbot per-router default applies).
static media::llm::agent::ProviderSettings make_provider_for_turn(
    const nlohmann::json* send = nullptr) {
    media::settings::ChatProviderSettings cs;
    std::string                    err;
    media::settings::load_chat_provider(cs, err);

    media::llm::agent::ProviderSettings prov;
    prov.router         = cs.router.empty() ? "openrouter" : cs.router;
    prov.base_url       = {};
    prov.api_key        = {};
    prov.model          = cs.model.empty() ? "openai/gpt-4o-mini" : cs.model;
    prov.timeout_ms     = cs.timeout_ms > 0 ? cs.timeout_ms : 180'000;
    prov.max_iterations = cs.max_iterations > 0 ? cs.max_iterations : 8;

    if (send) {
        if (send->contains("router") && (*send)["router"].is_string()) {
            const std::string r = (*send)["router"].get<std::string>();
            if (!r.empty()) {
                prov.router = r;
                if (r != cs.router) prov.base_url.clear();
            }
        }
        if (send->contains("model") && (*send)["model"].is_string()) {
            const std::string m = (*send)["model"].get<std::string>();
            if (!m.empty()) prov.model = m;
        }
    }

    media_cli::fill_chat_llm_credentials_from_app_settings(prov.api_key, prov.base_url, prov.router);

    if (cs.api_mode == "responses")
        prov.api_mode = media::llm::agent::LlmApiMode::Responses;
    else if (cs.api_mode == "realtime")
        prov.api_mode = media::llm::agent::LlmApiMode::Realtime;
    prov.streaming_mode = media::llm::agent::LlmStreamingMode::Auto;

    return prov;
}

// Per-app WebView2 user-data folder: Roaming `get_config_dir() / "web"` (same tree as
// settings.json) so the profile sits beside app config. Cookies / cache / storage stay
// out of the user's main browser profile.
std::wstring webview2_user_data_folder() {
    try {
        fs::path p = media::settings::get_config_dir() / "web";
        std::error_code ec;
        fs::create_directories(p, ec);
        return p.wstring();
    } catch (...) {
        return L"";
    }
}

// ── Chat HTML on disk + virtual host: durable Web Storage (localStorage) ───
//
// NavigateToString() loads a document with an opaque / null origin. WebView2
// does not give that document a stable storage partition, so
// localStorage (prompt history, style presets) appears to reset on every
// app restart. Serving the same bundle from a stable https virtual host
// (folder mapping) provides a first-party origin and persistent storage.
// See: https://github.com/MicrosoftEdge/WebView2Feedback/issues/120
//

std::wstring chat_web_content_folder() {
    wchar_t buf[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf)))
        return L"";
    fs::path p(buf);
    p /= pm::brand::k_vendor_w;
    p /= pm::brand::k_app_id_w;
    p /= L"chat-web";
    return p.wstring();
}

static bool write_chat_html_file(const std::string& utf8, std::wstring& err) {
    err.clear();
    const std::wstring dirW = chat_web_content_folder();
    if (dirW.empty()) {
        err = L"no %LOCALAPPDATA%";
        return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(dirW), ec);
    const fs::path file = fs::path(dirW) / "chat.html";
    logger::info(std::string("[chat-web] writing virtual-host bundle: path=")
                 + pmui::wide_to_utf8(file.wstring())
                 + " bytes=" + std::to_string(utf8.size()));
    std::ofstream     out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = L"open chat.html for write";
        return false;
    }
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return out.good() || utf8.empty();
}

/// Returns true if the page should be opened via Navigate() to kChatWebStartUrl.
static bool install_chat_web_virtual_host(ICoreWebView2* webview, const std::string& utf8_html) {
    std::wstring werr;
    if (!write_chat_html_file(utf8_html, werr)) {
        logger::warn(std::string("[chat-web] bundle write failed: ") + pmui::wide_to_utf8(werr)
                     + " — localStorage will not persist (NavigateToString fallback).");
        return false;
    }
    ComPtr<ICoreWebView2>   w0(webview);
    ComPtr<ICoreWebView2_3> v3;
    if (FAILED(w0.As(&v3)) || !v3)
        return false;
    const std::wstring folder = chat_web_content_folder();
    HRESULT            hr
        = v3->SetVirtualHostNameToFolderMapping(pm::brand::k_chat_web_vhost_w, folder.c_str(),
                                                  COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    if (FAILED(hr)) {
        logger::warn(std::string("[chat-web] SetVirtualHostNameToFolderMapping (chat) failed: 0x")
                     + std::to_string(static_cast<std::uint32_t>(static_cast<ULONG>(hr)))
                     + " — localStorage may not persist.");
        return false;
    }
    logger::info(std::string("[chat-web] virtual-host mapping ok: https://")
                 + pmui::wide_to_utf8(std::wstring(pm::brand::k_chat_web_vhost_w))
                 + " -> " + pmui::wide_to_utf8(folder));
    return true;
}

static void provider_map_row(const std::string& id, std::string& api_key, std::string& base_url) {
    api_key.clear();
    base_url.clear();
    media::settings::ProviderMap pm;
    std::string                    e;
    if (!media::settings::load_providers(pm, e)) return;
    const auto it = pm.find(id);
    if (it != pm.end()) {
        api_key  = it->second.api_key;
        base_url = it->second.base_url;
    }
}

static std::int64_t json_int64_loose(const nlohmann::json& o, const char* key, std::int64_t d = 0) {
    if (!o.is_object() || !o.contains(key))
        return d;
    const auto& v = o[key];
    if (v.is_number_integer())
        return v.get<std::int64_t>();
    if (v.is_number_unsigned())
        return static_cast<std::int64_t>(v.get<std::uint64_t>());
    if (v.is_number_float())
        return static_cast<std::int64_t>(v.get<double>());
    return d;
}

// ── Resize / reframe LLM presets (`resize-templates.json` beside settings.json in get_config_dir()) ──
// Optional overlay: `resize-templates-<lang>.json` (same folder) merges `name` by template `id`.
// On `ready`, defaults from disk are merged only when `chat_web` has no `resize_presets` array yet;
// the web UI persists the user's list in `chat_web` (not stripped on save).

static fs::path chat_module_exe_dir() {
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1u));
    if (n == 0u)
        return {};
    return fs::path(std::wstring(buf.data(), n)).parent_path();
}

static bool read_utf8_file(const fs::path& p, std::string& utf8_out) {
    utf8_out.clear();
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || ec)
        return false;
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs)
        return false;
    utf8_out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return !utf8_out.empty();
}

static std::string primary_lang_from_locale(std::string_view loc) {
    if (loc.empty())
        return "en";
    std::string s(loc);
    const auto cut = s.find_first_of("-_");
    std::string prim = cut == std::string::npos ? s : s.substr(0, cut);
    for (char& c : prim)
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return prim.empty() ? "en" : prim;
}

static void string_replace_all(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::string expand_resize_prompt_template(std::string prompt, const nlohmann::json& tmpl) {
    int tw = 0, th = 0;
    std::string aspect;
    if (tmpl.contains("target") && tmpl["target"].is_object()) {
        const auto& tgt = tmpl["target"];
        tw = static_cast<int>(json_int64_loose(tgt, "w"));
        th = static_cast<int>(json_int64_loose(tgt, "h"));
        if (tgt.contains("aspect") && tgt["aspect"].is_string())
            aspect = tgt["aspect"].get<std::string>();
    }
    string_replace_all(prompt, "{{TARGET_W}}", std::to_string(tw));
    string_replace_all(prompt, "{{TARGET_H}}", std::to_string(th));
    string_replace_all(prompt, "{{TARGET_ASPECT}}", aspect);
    string_replace_all(prompt, "{{SUBJECT_HINT}}", "the main subject");
    string_replace_all(prompt, "{{STYLE_HINT}}", "preserve the original look, grain, and color grade");
    return prompt;
}

// Delimiter `pm` avoids `)"` inside JSON (e.g. `(1080×1920)"` in name) ending a plain `R"(...)"` raw string.
static const char k_resize_templates_fallback[] = R"pm({"version":1,"templates":[{"id":"resize_fallback_9x16","group":"9:16 — vertical social","name":"Reframe to 9:16 (1080×1920)","target":{"w":1080,"h":1920,"aspect":"9:16"},"variables":[],"prompt":"Reframe the attached media to {{TARGET_W}}×{{TARGET_H}} ({{TARGET_ASPECT}}). Subject: {{SUBJECT_HINT}}. Style: {{STYLE_HINT}}. Do not add text or watermarks."}]})pm";

static void apply_resize_template_name_overlay(nlohmann::json& root, const nlohmann::json& overlay) {
    if (!root.is_object() || !root.contains("templates") || !root["templates"].is_array())
        return;
    if (!overlay.is_object() || !overlay.contains("templates") || !overlay["templates"].is_array())
        return;
    std::unordered_map<std::string, std::string> names;
    for (const auto& el : overlay["templates"]) {
        if (!el.is_object())
            continue;
        const std::string id = el.value("id", std::string{});
        if (id.empty() || !el.contains("name") || !el["name"].is_string())
            continue;
        names[id] = el["name"].get<std::string>();
    }
    if (names.empty())
        return;
    for (auto& el : root["templates"]) {
        if (!el.is_object())
            continue;
        const std::string id = el.value("id", std::string{});
        const auto it = names.find(id);
        if (it != names.end())
            el["name"] = it->second;
    }
}

static bool load_resize_templates_root(nlohmann::json& root, std::string_view display_lang) {
    root = nlohmann::json::object();
    std::string raw;
    bool from_disk = false;
    try {
        const fs::path roaming = media::settings::get_config_dir() / "resize-templates.json";
        if (read_utf8_file(roaming, raw))
            from_disk = true;
        if (!from_disk) {
            const fs::path exe = chat_module_exe_dir();
            if (!exe.empty()) {
                if (read_utf8_file(exe / "data" / "resize-templates.json", raw))
                    from_disk = true;
                else if (read_utf8_file(exe / "config" / "resize-templates.json", raw))
                    from_disk = true;
            }
        }
    } catch (...) {
        raw.clear();
    }
    try {
        if (!raw.empty())
            root = nlohmann::json::parse(raw);
        else
            root = nlohmann::json::parse(k_resize_templates_fallback);
    } catch (...) {
        try {
            root = nlohmann::json::parse(k_resize_templates_fallback);
        } catch (...) {
            root = nlohmann::json::object();
        }
    }
    if (!from_disk)
        logger::warn("[chat-web] resize-templates.json not found next to settings (Roaming store) or "
                     "under exe data/config; using built-in fallback");

    const std::string lang = primary_lang_from_locale(display_lang);
    if (lang != "en" && !lang.empty()) {
        try {
            const fs::path overlay_path = media::settings::get_config_dir()
                / (std::string("resize-templates-") + lang + std::string(".json"));
            std::string oraw;
            if (read_utf8_file(overlay_path, oraw)) {
                nlohmann::json ov = nlohmann::json::parse(oraw);
                apply_resize_template_name_overlay(root, ov);
            }
        } catch (...) {
            /* keep English names from base */
        }
    }
    return root.contains("templates") && root["templates"].is_array() && !root["templates"].empty();
}

static nlohmann::json build_resize_presets_for_web(std::string_view display_lang) {
    nlohmann::json root;
    load_resize_templates_root(root, display_lang);
    nlohmann::json out = nlohmann::json::array();
    if (!root.contains("templates") || !root["templates"].is_array())
        return out;
    for (const auto& el : root["templates"]) {
        if (!el.is_object())
            continue;
        const std::string id = el.value("id", std::string{});
        std::string name = el.value("name", std::string{});
        if (!el.contains("prompt") || !el["prompt"].is_string())
            continue;
        std::string prompt = el["prompt"].get<std::string>();
        if (id.empty() || prompt.empty())
            continue;
        if (name.empty())
            name = id;
        nlohmann::json row;
        row["id"]    = id;
        row["name"]  = std::move(name);
        row["icon"]  = "\u2194"; // ↔
        row["prompt"] = expand_resize_prompt_template(std::move(prompt), el);
        if (el.contains("group") && el["group"].is_string()) {
            const std::string g = el["group"].get<std::string>();
            if (!g.empty())
                row["group"] = g;
        }
        if (el.contains("target") && el["target"].is_object())
            row["target"] = el["target"];
        out.push_back(std::move(row));
    }
    return out;
}

static void merge_resize_presets_into_chat_web_doc(nlohmann::json& cw, std::string_view display_lang) {
    if (!cw.is_object())
        return;
    if (cw.contains("resize_presets") && cw["resize_presets"].is_array())
        return;
    try {
        cw["resize_presets"] = build_resize_presets_for_web(display_lang);
    } catch (...) {
        cw["resize_presets"] = nlohmann::json::array();
    }
}

// ── Design / look LLM presets (`design-presets.json` beside settings.json in get_config_dir()) ──
// Optional overlay: `design-presets-<lang>.json` merges `name` and optional `group` by preset `id`.
// On `ready`, defaults from disk are merged only when `chat_web` has no `design_presets` array yet;
// the web UI persists the user's list in `chat_web` like `quick_actions` (not stripped on save).

static const char k_design_presets_fallback[] = R"pm({"version":1,"presets":[{"id":"design_fallback_minimal","group":"Classic photo looks","name":"High contrast black and white","icon":"\u2728","prompt":"Convert to black and white with strong contrast. Preserve faces and details. Do not add text or logos."}]})pm";

static void apply_design_preset_overlay(nlohmann::json& root, const nlohmann::json& overlay) {
    if (!root.is_object() || !root.contains("presets") || !root["presets"].is_array())
        return;
    if (!overlay.is_object() || !overlay.contains("presets") || !overlay["presets"].is_array())
        return;
    std::unordered_map<std::string, std::string> names;
    std::unordered_map<std::string, std::string> groups;
    for (const auto& el : overlay["presets"]) {
        if (!el.is_object())
            continue;
        const std::string id = el.value("id", std::string{});
        if (id.empty())
            continue;
        if (el.contains("name") && el["name"].is_string())
            names[id] = el["name"].get<std::string>();
        if (el.contains("group") && el["group"].is_string())
            groups[id] = el["group"].get<std::string>();
    }
    if (names.empty() && groups.empty())
        return;
    for (auto& el : root["presets"]) {
        if (!el.is_object())
            continue;
        const std::string id = el.value("id", std::string{});
        const auto nit = names.find(id);
        if (nit != names.end())
            el["name"] = nit->second;
        const auto git = groups.find(id);
        if (git != groups.end()) {
            if (git->second.empty())
                el.erase("group");
            else
                el["group"] = git->second;
        }
    }
}

static bool load_design_presets_root(nlohmann::json& root, std::string_view display_lang) {
    root = nlohmann::json::object();
    std::string raw;
    bool from_disk = false;
    try {
        const fs::path roaming = media::settings::get_config_dir() / "design-presets.json";
        if (read_utf8_file(roaming, raw))
            from_disk = true;
        if (!from_disk) {
            const fs::path exe = chat_module_exe_dir();
            if (!exe.empty()) {
                if (read_utf8_file(exe / "data" / "design-presets.json", raw))
                    from_disk = true;
                else if (read_utf8_file(exe / "config" / "design-presets.json", raw))
                    from_disk = true;
            }
        }
    } catch (...) {
        raw.clear();
    }
    try {
        if (!raw.empty())
            root = nlohmann::json::parse(raw);
        else
            root = nlohmann::json::parse(k_design_presets_fallback);
    } catch (...) {
        try {
            root = nlohmann::json::parse(k_design_presets_fallback);
        } catch (...) {
            root = nlohmann::json::object();
        }
    }
    if (!from_disk)
        logger::warn("[chat-web] design-presets.json not found next to settings (Roaming store) or "
                     "under exe data/config; using built-in fallback");

    const std::string lang = primary_lang_from_locale(display_lang);
    if (lang != "en" && !lang.empty()) {
        try {
            const fs::path overlay_path = media::settings::get_config_dir()
                / (std::string("design-presets-") + lang + std::string(".json"));
            std::string oraw;
            if (read_utf8_file(overlay_path, oraw)) {
                nlohmann::json ov = nlohmann::json::parse(oraw);
                apply_design_preset_overlay(root, ov);
            }
        } catch (...) {
            /* keep English from base */
        }
    }
    return root.contains("presets") && root["presets"].is_array() && !root["presets"].empty();
}

static nlohmann::json build_design_presets_for_web(std::string_view display_lang) {
    nlohmann::json root;
    load_design_presets_root(root, display_lang);
    nlohmann::json out = nlohmann::json::array();
    if (!root.contains("presets") || !root["presets"].is_array())
        return out;
    for (const auto& el : root["presets"]) {
        if (!el.is_object())
            continue;
        const std::string id = el.value("id", std::string{});
        std::string name = el.value("name", std::string{});
        if (!el.contains("prompt") || !el["prompt"].is_string())
            continue;
        std::string prompt = el["prompt"].get<std::string>();
        if (id.empty() || prompt.empty())
            continue;
        if (name.empty())
            name = id;
        nlohmann::json row;
        row["id"]     = id;
        row["name"]   = std::move(name);
        row["prompt"] = std::move(prompt);
        if (el.contains("icon") && el["icon"].is_string())
            row["icon"] = el["icon"].get<std::string>();
        else
            row["icon"] = "\u2728";
        if (el.contains("group") && el["group"].is_string()) {
            const std::string g = el["group"].get<std::string>();
            if (!g.empty())
                row["group"] = g;
        }
        out.push_back(std::move(row));
    }
    return out;
}

static void merge_design_presets_into_chat_web_doc(nlohmann::json& cw, std::string_view display_lang) {
    if (!cw.is_object())
        return;
    if (cw.contains("design_presets") && cw["design_presets"].is_array())
        return;
    try {
        cw["design_presets"] = build_design_presets_for_web(display_lang);
    } catch (...) {
        cw["design_presets"] = nlohmann::json::array();
    }
}

// ── `chat_web.sessions` in settings.json (portable store) — mirrors chat-next localStorage layout ──
constexpr const char* k_chat_web_sessions_key = "sessions";
constexpr int         k_chat_web_sessions_max = 50;

static nlohmann::json& ensure_chat_web_sessions_array(nlohmann::json& cw) {
    if (!cw.contains(k_chat_web_sessions_key) || !cw[k_chat_web_sessions_key].is_array())
        cw[k_chat_web_sessions_key] = nlohmann::json::array();
    return cw[k_chat_web_sessions_key];
}

static void sort_chat_sessions_newest_first(nlohmann::json& arr) {
    if (!arr.is_array()) return;
    std::sort(arr.begin(), arr.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return json_int64_loose(a, "updatedAt") > json_int64_loose(b, "updatedAt");
    });
}

static void cap_chat_sessions(nlohmann::json& arr) {
    if (!arr.is_array()) return;
    sort_chat_sessions_newest_first(arr);
    if (static_cast<int>(arr.size()) > k_chat_web_sessions_max)
        arr.erase(arr.begin() + k_chat_web_sessions_max, arr.end());
}

static nlohmann::json session_meta_row(const nlohmann::json& sess) {
    return nlohmann::json{{"id", sess.value("id", std::string{})},
                          {"title", sess.value("title", std::string{"Chat"})},
                          {"createdAt", json_int64_loose(sess, "createdAt")},
                          {"updatedAt", json_int64_loose(sess, "updatedAt")}};
}

static nlohmann::json rpc_list_chat_sessions(std::string& err) {
    err.clear();
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "load_chat_web" : err}};
    nlohmann::json out = nlohmann::json::array();
    if (cw.is_object() && cw.contains(k_chat_web_sessions_key) && cw[k_chat_web_sessions_key].is_array()) {
        for (const auto& el : cw[k_chat_web_sessions_key]) {
            if (el.is_object()) out.push_back(session_meta_row(el));
        }
    }
    std::sort(out.begin(), out.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return json_int64_loose(a, "updatedAt") > json_int64_loose(b, "updatedAt");
    });
    return nlohmann::json{{"ok", true}, {"data", nlohmann::json{{"sessions", std::move(out)}}}};
}

static nlohmann::json rpc_load_chat_session(const std::string& id, std::string& err) {
    err.clear();
    if (id.empty()) return nlohmann::json{{"ok", false}, {"error", "missing id"}};
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "load_chat_web" : err}};
    if (!cw.is_object() || !cw.contains(k_chat_web_sessions_key) || !cw[k_chat_web_sessions_key].is_array())
        return nlohmann::json{{"ok", true}, {"data", nlohmann::json(nullptr)}};
    for (const auto& el : cw[k_chat_web_sessions_key]) {
        if (el.is_object() && el.value("id", std::string{}) == id) {
            return nlohmann::json{{"ok", true}, {"data", nlohmann::json{{"session", el}}}};
        }
    }
    return nlohmann::json{{"ok", true}, {"data", nlohmann::json(nullptr)}};
}

static nlohmann::json rpc_save_chat_session(const nlohmann::json& sess_in, std::string& err) {
    err.clear();
    if (!sess_in.is_object()) return nlohmann::json{{"ok", false}, {"error", "missing session"}};
    const std::string sid = sess_in.value("id", std::string{});
    if (sid.empty()) return nlohmann::json{{"ok", false}, {"error", "missing session.id"}};
    {
        std::size_t ec = 0;
        if (sess_in.contains("entries") && sess_in["entries"].is_array())
            ec = sess_in["entries"].size();
        logger::info(std::string("[chat-web] sessions RPC saveChatSession id=") + sid
                     + " entries=" + std::to_string(ec));
    }
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "load_chat_web" : err}};
    if (!cw.is_object()) cw = nlohmann::json::object();
    nlohmann::json& arr = ensure_chat_web_sessions_array(cw);
    bool replaced = false;
    for (auto& el : arr) {
        if (el.is_object() && el.value("id", std::string{}) == sid) {
            el = sess_in;
            replaced = true;
            break;
        }
    }
    if (!replaced) arr.push_back(sess_in);
    cap_chat_sessions(arr);
    if (!media::settings::save_chat_web(cw, err)) {
        logger::warn(std::string("[chat-web] saveChatSession: save_chat_web failed: ")
                     + (err.empty() ? "?" : err));
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "save_chat_web" : err}};
    }
    logger::info(std::string("[chat-web] sessions saveChatSession ok id=") + sid);
    return nlohmann::json{{"ok", true}};
}

static nlohmann::json rpc_delete_chat_session(const std::string& id, std::string& err) {
    err.clear();
    logger::info(std::string("[chat-web] sessions RPC deleteChatSession id=") + id);
    if (id.empty()) return nlohmann::json{{"ok", false}, {"error", "missing id"}};
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "load_chat_web" : err}};
    if (!cw.is_object() || !cw.contains(k_chat_web_sessions_key) || !cw[k_chat_web_sessions_key].is_array())
        return nlohmann::json{{"ok", true}};
    nlohmann::json& arr = cw[k_chat_web_sessions_key];
    const auto end = std::remove_if(arr.begin(), arr.end(), [&id](const nlohmann::json& el) {
        return el.is_object() && el.value("id", std::string{}) == id;
    });
    if (end == arr.end()) return nlohmann::json{{"ok", true}};
    arr.erase(end, arr.end());
    if (!media::settings::save_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "save_chat_web" : err}};
    logger::info(std::string("[chat-web] sessions deleteChatSession ok id=") + id);
    return nlohmann::json{{"ok", true}};
}

// ── Clipboard paste: save image bytes beside settings (same contract as drag-drop paths) ──

static constexpr std::size_t kChatPasteMaxDecodedBytes = 32u * 1024u * 1024u;

static int chat_web_b64_decode_char(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<std::uint8_t> chat_web_base64_decode(std::string in)
{
    while (!in.empty() && (in.back() == ' ' || in.back() == '\n' || in.back() == '\r' || in.back() == '\t'))
        in.pop_back();
    size_t s0 = 0;
    while (s0 < in.size() && (in[s0] == ' ' || in[s0] == '\n' || in[s0] == '\r' || in[s0] == '\t'))
        ++s0;
    if (s0) in.erase(0, s0);
    const size_t comma = in.find(";base64,");
    if (comma != std::string::npos && in.size() > 8 && std::strncmp(in.c_str(), "data:", 5) == 0)
        in.erase(0, comma + 8);

    std::vector<std::uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    std::uint32_t buf = 0;
    int           bits = 0;
    for (unsigned char uc : in) {
        const char c = static_cast<char>(uc);
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const int v = chat_web_b64_decode_char(c);
        if (v < 0) continue;
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>(buf >> bits));
        }
    }
    return out;
}

static const char* chat_web_ext_for_mime(std::string mime)
{
    for (auto& c : mime) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (mime == "image/png") return ".png";
    if (mime == "image/jpeg" || mime == "image/jpg") return ".jpg";
    if (mime == "image/webp") return ".webp";
    return nullptr;
}

static nlohmann::json rpc_save_chat_pasted_image(const nlohmann::json& in)
{
    if (!in.contains("base64") || !in["base64"].is_string())
        return nlohmann::json{{"ok", false}, {"error", "missing base64"}};
    std::string mime = in.value("mime", std::string{"image/png"});
    const char* ext  = chat_web_ext_for_mime(mime);
    if (!ext) return nlohmann::json{{"ok", false}, {"error", "unsupported mime (use image/png, image/jpeg, image/webp)"}};

    std::string b64 = in["base64"].get<std::string>();
    std::vector<std::uint8_t> bytes = chat_web_base64_decode(std::move(b64));
    if (bytes.empty()) return nlohmann::json{{"ok", false}, {"error", "empty or invalid base64"}};
    if (bytes.size() > kChatPasteMaxDecodedBytes)
        return nlohmann::json{{"ok", false}, {"error", "pasted image too large"}};

    try {
        static std::atomic<std::uint64_t> s_seq{0};
        const std::uint64_t              n = ++s_seq;
        const std::uint64_t              tick = static_cast<std::uint64_t>(::GetTickCount64());
        const fs::path                   dir  = media::settings::get_config_dir() / "chat-paste";
        std::error_code                  ec;
        fs::create_directories(dir, ec);
        if (ec) return nlohmann::json{{"ok", false}, {"error", ec.message()}};
        const fs::path out_path = dir / (std::string("paste-") + std::to_string(tick) + "-" + std::to_string(n) + ext);
        std::ofstream    ofs(out_path, std::ios::binary);
        if (!ofs) return nlohmann::json{{"ok", false}, {"error", "open file for write failed"}};
        ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!ofs.good()) return nlohmann::json{{"ok", false}, {"error", "write failed"}};
        const std::wstring wpath = out_path.wstring();
        nlohmann::json     data;
        data["path"] = pmui::wide_to_utf8(wpath);
        return nlohmann::json{{"ok", true}, {"data", std::move(data)}};
    } catch (const std::exception& e) {
        return nlohmann::json{{"ok", false}, {"error", e.what()}};
    }
}

/// JSON API for the embedded chat-next Web UI — same backing as `ChatProviderDlg` / `pm_polymech_replicate`.
static nlohmann::json provider_rpc_dispatch(const nlohmann::json& in) {
    const std::string     method = in.value("method", std::string{});
    std::string           err;
    if (method == "getChatFields") {
        media::settings::ChatProviderSettings cs;
        (void)media::settings::load_chat_provider(cs, err);
        nlohmann::json d;
        d["router"]          = cs.router;
        d["model"]           = cs.model;
        d["image_provider"]               = cs.image_provider;
        d["image_model"]                  = cs.image_model;
        d["image_recognition_provider"]   = cs.image_recognition_provider;
        d["image_recognition_model"]      = cs.image_recognition_model;
        d["video_provider"]               = cs.video_provider;
        d["video_model"]                  = cs.video_model;
        d["stt_provider"]                 = cs.stt_provider;
        d["stt_model"]                    = cs.stt_model;
        d["tts_provider"]                 = cs.tts_provider;
        d["tts_model"]                    = cs.tts_model;
        d["tts_voice_id"]                 = cs.tts_voice_id;
        return nlohmann::json{{"ok", true}, {"data", std::move(d)}};
    }
    if (method == "imageProviders") {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : pmui::provider_models::providers())
            arr.push_back(nlohmann::json{{"id", p.id}, {"label", p.label}});
        return nlohmann::json{{"ok", true}, {"data", std::move(arr)}};
    }
    if (method == "imageModels") {
        const std::string         pid = in.value("provider", std::string{});
        std::string                 api_key, base_url;
        provider_map_row(pid, api_key, base_url);
        std::vector<pmui::provider_models::ModelOption> models;
        if (!pmui::provider_models::list_models_for_provider(pid, api_key, base_url, models, err))
            return nlohmann::json{{"ok", false}, {"error", err}};
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& m : models) arr.push_back(nlohmann::json{{"id", m.id}, {"label", m.label}});
        return nlohmann::json{{"ok", true}, {"data", std::move(arr)}};
    }
    if (method == "replicate") {
        const std::string op    = in.value("op", std::string{});
        std::string       api_key, base_url;
        provider_map_row("replicate", api_key, base_url);
        const bool        force = in.value("force_refresh", false);
        if (op == "collections") {
            std::vector<media::replicate_cli::CollectionInfo> cols;
            if (!media::replicate_cli::fetch_collections(api_key, base_url, cols, err, force))
                return nlohmann::json{{"ok", false}, {"error", err.empty() ? "fetch_collections" : err}};
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& c : cols)
                arr.push_back(
                    nlohmann::json{{"name", c.name}, {"slug", c.slug}, {"description", c.description}});
            return nlohmann::json{{"ok", true},
                                  {"data", nlohmann::json{{"collections", std::move(arr)}}}};
        }
        if (op == "models") {
            const std::string                            coll = in.value("collection", std::string{"official"});
            std::vector<media::replicate_cli::ModelInfo> models;
            if (!media::replicate_cli::fetch_collection_models(api_key, base_url, coll, models, err, force))
                return nlohmann::json{{"ok", false},
                                      {"error", err.empty() ? "fetch_collection_models" : err}};
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& m : models)
                arr.push_back(nlohmann::json{{"slug", m.slug},
                                              {"description", m.description},
                                              {"url", m.url},
                                              {"visibility", m.visibility},
                                              {"is_official", m.is_official}});
            return nlohmann::json{{"ok", true},
                                  {"data", nlohmann::json{{"models", std::move(arr)}}}};
        }
        if (op == "resolve_collection") {
            std::string out_slug;
            if (!media::replicate_cli::resolve_collection_for_model_cached(in.value("model_slug", std::string{}), out_slug)
                || out_slug.empty())
                return nlohmann::json{
                    {"ok", true},
                    {"data", nlohmann::json{{"collection", nlohmann::json(nullptr)}}},
                };
            return nlohmann::json{
                {"ok", true},
                {"data", nlohmann::json{{"collection", out_slug}}},
            };
        }
        if (op == "openapi_input_flat") {
            const std::string slug = in.value("model_slug", std::string{});
            if (slug.empty())
                return nlohmann::json{{"ok", false}, {"error", "openapi_input_flat: missing model_slug"}};
            nlohmann::json flat;
            nlohmann::json req = nlohmann::json::array();
            if (!media::replicate_cli::lookup_replicate_openapi_input_flat(slug, flat, req, err))
                return nlohmann::json{{"ok", false}, {"error", err.empty() ? "openapi_input_flat failed" : err}};
            return nlohmann::json{{"ok", true},
                                  {"data", nlohmann::json{{"model_slug", slug},
                                                          {"required", std::move(req)},
                                                          {"properties", std::move(flat)}}}};
        }
        return nlohmann::json{{"ok", false}, {"error", "replicate: unknown op"}};
    }
    if (method == "llmModels") {
        const std::string router = in.value("router", std::string{});
        const bool        force  = in.value("force_refresh", false);
        nlohmann::json    arr    = nlohmann::json::array();
        if (router == "openrouter") {
            std::string api_key, base_url;
            provider_map_row("openrouter", api_key, base_url);
            if (base_url.empty()) base_url = "https://openrouter.ai/api/v1";
            std::vector<media::openrouter_cli::OpenRouterCatalogModelRow> rows;
            if (!media::openrouter_cli::list_openrouter_catalog_models(api_key, base_url, rows, err, force))
                return nlohmann::json{{"ok", false}, {"error", err}};
            for (const auto& r : rows)
                arr.push_back(nlohmann::json{{"id", r.id},
                                              {"label", r.name.empty() ? r.id : r.name}});
            return nlohmann::json{{"ok", true}, {"data", std::move(arr)}};
        }
        if (router == "openai") {
            std::string api_key, base_url;
            provider_map_row("openai", api_key, base_url);
            if (base_url.empty()) base_url = "https://api.openai.com/v1";
            std::vector<media::openai_cli::OpenAIModelRow> rows;
            if (!media::openai_cli::list_openai_models(api_key, base_url, rows, err, force))
                return nlohmann::json{{"ok", false}, {"error", err}};
            for (const auto& r : rows)
                arr.push_back(nlohmann::json{{"id", r.id}, {"label", r.id}});
            return nlohmann::json{{"ok", true}, {"data", std::move(arr)}};
        }
        return nlohmann::json{{"ok", false},
                               {"error", "llmModels: unsupported router \"" + router + "\""}};
    }
    if (method == "saveChatFields") {
        media::settings::ChatProviderSettings cs;
        (void)media::settings::load_chat_provider(cs, err);
        if (in.contains("router") && in["router"].is_string()) cs.router = in["router"].get<std::string>();
        if (in.contains("model") && in["model"].is_string()) cs.model = in["model"].get<std::string>();
        if (in.contains("image_provider") && in["image_provider"].is_string())
            cs.image_provider = in["image_provider"].get<std::string>();
        if (in.contains("image_model") && in["image_model"].is_string())
            cs.image_model = in["image_model"].get<std::string>();
        if (in.contains("image_recognition_provider") && in["image_recognition_provider"].is_string())
            cs.image_recognition_provider = in["image_recognition_provider"].get<std::string>();
        if (in.contains("image_recognition_model") && in["image_recognition_model"].is_string())
            cs.image_recognition_model = in["image_recognition_model"].get<std::string>();
        if (in.contains("video_provider") && in["video_provider"].is_string())
            cs.video_provider = in["video_provider"].get<std::string>();
        if (in.contains("video_model") && in["video_model"].is_string())
            cs.video_model = in["video_model"].get<std::string>();
        if (in.contains("stt_provider") && in["stt_provider"].is_string())
            cs.stt_provider = in["stt_provider"].get<std::string>();
        if (in.contains("stt_model") && in["stt_model"].is_string())
            cs.stt_model = in["stt_model"].get<std::string>();
        if (in.contains("tts_provider") && in["tts_provider"].is_string())
            cs.tts_provider = in["tts_provider"].get<std::string>();
        if (in.contains("tts_model") && in["tts_model"].is_string())
            cs.tts_model = in["tts_model"].get<std::string>();
        if (in.contains("tts_voice_id") && in["tts_voice_id"].is_string())
            cs.tts_voice_id = in["tts_voice_id"].get<std::string>();
        if (!media::settings::save_chat_provider(cs, err)) return nlohmann::json{{"ok", false}, {"error", err}};
        return nlohmann::json{{"ok", true}};
    }
    /// Past sessions sidebar — persisted under `settings.json` → `chat_web.sessions` (portable store).
    if (method == "listChatSessions") {
        return rpc_list_chat_sessions(err);
    }
    if (method == "loadChatSession") {
        return rpc_load_chat_session(in.value("id", std::string{}), err);
    }
    if (method == "saveChatSession") {
        if (!in.contains("session") || !in["session"].is_object())
            return nlohmann::json{{"ok", false}, {"error", "missing session"}};
        return rpc_save_chat_session(in["session"], err);
    }
    if (method == "deleteChatSession") {
        return rpc_delete_chat_session(in.value("id", std::string{}), err);
    }
    if (method == "saveChatPastedImage") {
        return rpc_save_chat_pasted_image(in);
    }
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
    if (method == "getCreditInfo") {
        nlohmann::json credit = pmui::pixlwiz_auth::read_credit_payload();
        if (!credit.value("ok", false))
            return nlohmann::json{{"ok", false}, {"error", credit.value("error", std::string{"pixlwiz credit unavailable"})}};
        credit.erase("ok");
        return nlohmann::json{{"ok", true}, {"data", std::move(credit)}};
    }
#endif
    return nlohmann::json{{"ok", false}, {"error", "unknown method: " + method}};
}

} // namespace

/// WebView2 controller fills the host client rect. (Older builds shrunk the surface to hide
/// a pale fringe — that only traded the halo for a huge empty gutter.) Fringe is addressed
/// with opaque `DefaultBackgroundColor` matching `theme_palette().web_surface_bg` + nuke pass.
static int webview_host_inset_pixels(HWND)
{
    return 0;
}

/// Shrink WebView2 surface inside the host (see `webview_host_inset_pixels`).
static void apply_webview2_controller_bounds(ICoreWebView2Controller* ctrl, HWND host)
{
    if (!ctrl || !host) return;
    RECT rc{};
    ::GetClientRect(host, &rc);
    const int ins = webview_host_inset_pixels(host);
    ::InflateRect(&rc, -ins, -ins);
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        ::GetClientRect(host, &rc);
        ::InflateRect(&rc, -webview_host_inset_pixels(host), -webview_host_inset_pixels(host));
        if (rc.right <= rc.left || rc.bottom <= rc.top)
            ::GetClientRect(host, &rc);
    }
    ctrl->put_Bounds(rc);
}

/// Opaque colour matching the embedded chat HTML (`web_surface_bg`). Transparent + erase
/// left a bright compositor fringe at the edge; full-bleed opaque matches Tailwind `surface`.
static void apply_webview2_match_theme_background(ICoreWebView2Controller* ctrl)
{
    if (!ctrl) return;
    ComPtr<ICoreWebView2Controller2> c2;
    if (FAILED(ctrl->QueryInterface(IID_PPV_ARGS(&c2))) || !c2)
        return;
    const auto& pal = pmui::theme_palette();
    COREWEBVIEW2_COLOR col{};
    col.A = 255;
    col.R = GetRValue(pal.web_surface_bg);
    col.G = GetGValue(pal.web_surface_bg);
    col.B = GetBValue(pal.web_surface_bg);
    c2->put_DefaultBackgroundColor(col);
}

/// Drive the web document’s `prefers-color-scheme` (Tailwind `darkMode: 'media'`) from
/// the same source as other panels: `pmui::theme_palette()` (settings: Light / Dark / System
/// with System resolved like the rest of the app). The default WebView2 profile uses AUTO
/// (OS). `ICoreWebView2Profile::put_PreferredColorScheme` pins light/dark to the app mode.
static void apply_webview2_preferred_color_scheme(ICoreWebView2* webview)
{
    if (!webview) return;
    ComPtr<ICoreWebView2_13>        w13;
    ComPtr<ICoreWebView2Profile>    prof;
    if (FAILED(webview->QueryInterface(IID_PPV_ARGS(&w13))) || !w13) return;
    if (FAILED(w13->get_Profile(&prof)) || !prof) return;
    const bool                    dark   = pmui::theme_palette().dark;
    const COREWEBVIEW2_PREFERRED_COLOR_SCHEME scheme = dark
        ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
        : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT;
    (void)prof->put_PreferredColorScheme(scheme);
}

namespace {
constexpr UINT_PTR kTimerWebView2ChromeResync = 303u;

static void chat_resync_webview2_chrome(ICoreWebView2Controller* ctrl, HWND host)
{
    if (!ctrl || !host || !::IsWindow(host)) return;
    ComPtr<ICoreWebView2> wv;
    if (SUCCEEDED(ctrl->get_CoreWebView2(&wv)) && wv)
        apply_webview2_preferred_color_scheme(wv.Get());
    apply_webview2_controller_bounds(ctrl, host);
    apply_webview2_match_theme_background(ctrl);
    const auto& pal = pmui::theme_palette();
    pmui::nuke_webview2_host_chrome(host, pal.web_surface_bg);
    pmui::flatten_webview_host_parent_chain(host, pal.web_surface_bg);
    ::InvalidateRect(host, nullptr, TRUE);
}

static void chat_schedule_chrome_resync(HWND host)
{
    if (!host || !::IsWindow(host)) return;
    ::SetTimer(host, kTimerWebView2ChromeResync, 100, nullptr);
}
} // namespace

/// Windows paths, case-insensitive: Explorer selection first, then extras (dropped
/// in chat) without duplicates. Used for `setStatus` + `run_turn` selection.
static std::vector<std::wstring> merge_path_lists_unique(const std::vector<std::wstring>& base,
                                                        const std::vector<std::wstring>& extra) {
    std::vector<std::wstring> out;
    out.reserve(base.size() + extra.size());
    auto add_if_new = [&out](const std::wstring& p) {
        if (p.empty()) return;
        for (const auto& x : out) {
            if (::_wcsicmp(x.c_str(), p.c_str()) == 0) return;
        }
        out.push_back(p);
    };
    for (const auto& w : base) add_if_new(w);
    for (const auto& w : extra) add_if_new(w);
    return out;
}

static bool path_in_list_ci(const std::wstring& p, const std::vector<std::wstring>& list) {
    for (const auto& x : list) {
        if (::_wcsicmp(x.c_str(), p.c_str()) == 0) return true;
    }
    return false;
}

static std::vector<std::wstring> filter_paths_removed_ci(std::vector<std::wstring> merged,
                                                         const std::vector<std::wstring>& removed) {
    if (removed.empty()) return merged;
    merged.erase(std::remove_if(merged.begin(), merged.end(),
                                [&](const std::wstring& w) { return path_in_list_ci(w, removed); }),
                 merged.end());
    return merged;
}

static std::vector<std::wstring> merge_paths_for_web_chat(const std::vector<std::wstring>& selection,
                                                          const std::vector<std::wstring>& extra,
                                                          const std::vector<std::wstring>& removed) {
    return filter_paths_removed_ci(merge_path_lists_unique(selection, extra), removed);
}

// ────────────────────────────────────────────────────────────────────────
// CChatWebView::Impl — owns the WebView2 controller + worker state.
// PIMPL keeps WebView2 COM types out of the public header.
// ────────────────────────────────────────────────────────────────────────
struct CChatWebView::Impl {
    HWND m_hostHwnd = nullptr;                       // CChatWebView's HWND
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    CWebViewManager*                busManager = nullptr; // non-owning shared WebView bus.
    EventRegistrationToken          msgToken{};
    EventRegistrationToken          navCompletedToken{};
    EventRegistrationToken          navStartingToken{};
    EventRegistrationToken          contentLoadingToken{};
    EventRegistrationToken          processFailedToken{};

    bool   ready          = false;                   // true after NavigationCompleted
    std::vector<std::wstring> pendingScripts;        // queued before ready

    /// Stable session ID for cross-turn memory (task-store entry, not fired by scheduler).
    /// Created lazily on the first turn and reused for the lifetime of this chat panel.
    std::string session_task_id;

    // The HTML to navigate to. Captured in the controller-ready callback;
    // the actual navigation is deferred until the host has non-zero
    // client bounds AND IsWindowVisible() is true (otherwise WebView2's
    // first-paint pipeline gets stuck and the page renders blank — see
    // EnsureNavigated below).
    std::string deferredHtml;
    /// When true, load `https://<k_chat_web_vhost>/chat.html` (virtual https origin for localStorage;
    /// host is DNS-invalid by design — see Branding.cmake / WebView2 folder mapping latency).
    /// When false, fall back to NavigateToString (storage does not survive restarts).
    bool        useChatWebOrigin = false;
    bool        navigated = false;
    bool        navWaitLogged = false;

    // Cached context — pushed into the JS UI on every change.
    /// From Explorer (RefreshChatContext / OnExplorerSelection). Not cleared when the user
    /// adds paths via the web; those live in `context_extra` and merge for display + send.
    std::vector<std::wstring> selection;
    /// Drag/paste/RPC paths explicitly added for chat context. Kept even when they also appear
    /// in Explorer `selection`, so changing the Explorer pick does not drop user-added files.
    std::vector<std::wstring> context_extra;
    /// Paths the user dismissed in the web filmstrip (still in Explorer selection until changed there).
    std::vector<std::wstring> context_removed;
    std::wstring              folder;
    /// BCP-47 style tag (e.g. "fr", "de") from App Settings; forwarded to `window.pmChat.setLocale`.
    std::string               ui_locale;

    void FlushContextToWeb() {
        nlohmann::json j;
        const auto merged_paths = merge_paths_for_web_chat(selection, context_extra, context_removed);
        const auto explorer_for_web = filter_paths_removed_ci(selection, context_removed);
        const auto extra_for_web     = filter_paths_removed_ci(context_extra, context_removed);
        j["selection"] = nlohmann::json::array();
        for (const auto& w : merged_paths)
            j["selection"].push_back(pmui::wide_to_utf8(w));
        j["explorer_selection"] = nlohmann::json::array();
        for (const auto& w : explorer_for_web)
            j["explorer_selection"].push_back(pmui::wide_to_utf8(w));
        j["context_extra"] = nlohmann::json::array();
        for (const auto& w : extra_for_web)
            j["context_extra"].push_back(pmui::wide_to_utf8(w));
        j["folder"] = pmui::wide_to_utf8(folder);
        {
            std::uint64_t selection_bytes = 0;
            for (const auto& w : merged_paths) {
                std::error_code ec;
                const auto sz = std::filesystem::file_size(std::filesystem::path{w}, ec);
                if (!ec) {
                    constexpr std::uintmax_t kCap = (std::uintmax_t{1} << 40);
                    selection_bytes += static_cast<std::uint64_t>(std::min<std::uintmax_t>(sz, kCap));
                }
            }
            j["selection_bytes"] = selection_bytes;
        }
        {
            nlohmann::json feats = nlohmann::json::object();
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
            feats["pixlwizAuth"] = true;
#else
            feats["pixlwizAuth"] = false;
#endif
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
            feats["pixlwizShare"] = true;
#else
            feats["pixlwizShare"] = false;
#endif
            j["features"] = std::move(feats);
        }
        {
            std::string cerr;
            media::settings::ChatProviderSettings cps;
            if (media::settings::load_chat_provider(cps, cerr)) {
                if (!cps.router.empty()) j["saved_chat_router"] = cps.router;
                if (!cps.model.empty()) j["saved_chat_model"]  = cps.model;
            }
        }
        if (mcp_catalog_loaded)
            j["mcp_catalog"] = mcp_web_catalog;
        std::wstring js = L"if(window.pmChat){";
        if (!ui_locale.empty())
            js += L"window.pmChat.setLocale("
                + pmui::utf8_to_wide(nlohmann::json(ui_locale).dump()) + L");";
        js += L"window.pmChat.setStatus(" + pmui::utf8_to_wide(j.dump()) + L");}";
        if (ready) RunJsNow(js);
        else       pendingScripts.push_back(js);
    }

    /// Match chat-next composer CSS to `pmui::ui_font` / `appearance.font_size_extra_pt` (0–4),
    /// including after App Settings — see `CChatWebView::RefreshChromeForTheme`.
    void PushFontExtraToWeb() {
        media::settings::AppearanceSettings appearance;
        std::string                         aerr;
        if (!media::settings::load_appearance(appearance, aerr)) return;
        int fi = appearance.font_size_extra_pt;
        if (fi < 0) fi = 0;
        if (fi > 4) fi = 4;
        const std::wstring js = L"if(window.pmChat)window.pmChat.setFontExtraPt("
            + std::to_wstring(fi) + L");";
        if (ready) RunJsNow(js);
        else       pendingScripts.push_back(js);
    }

    // Agent worker (one in-flight at a time). m_cancel is shared with the
    // worker so OnStop() (from the JS Stop button) can signal abort.
    std::thread                            worker;
    std::shared_ptr<std::atomic<bool>>     cancel;
    bool                                   busy = false;
    /// Count of ToolFileProgress events for image_transform (per-file) this turn;
    /// final ToolResult skips re-queuing the same paths.
    int                                    transformFileProgressTally = 0;

#if defined(FEATURE_STT) && FEATURE_STT
    // ── STT (mic → ElevenLabs → JS composer) ─────────────────────────────────
    std::unique_ptr<pm::stt::ElevenLabsSTT> stt_session;
    std::unique_ptr<pm::audio::AudioInput>  stt_mic;
    std::atomic<bool>                       stt_active{false};
    std::string                             stt_last_committed; // delta-tracking baseline

    // ── Audio playback (audioPlay / audioStop IPC) ────────────────────────────
    std::mutex                               audio_play_mu;
    std::shared_ptr<pm::audio::AudioOutput>  audio_play_out;
#endif
#if defined(FEATURE_VIDEO) && FEATURE_VIDEO
    // ── Video snapshot (webcam still → temp file → addContextPaths) ──────────
    std::atomic<bool>                       video_snap_busy{false};
#endif

    /// One-shot perf: `pm-image --ui-chat` only (see `pmui::is_ui_chat_standalone_session()`).
    bool     standaloneNavPerf   = false;
    bool     standaloneReadyPerf = false;

    std::atomic<bool> mcp_catalog_probe_started{false};
    bool               mcp_catalog_loaded = false;
    nlohmann::json     mcp_web_catalog  = nlohmann::json::object();

    void RunJsNow(const std::wstring& js) {
        if (!webview) return;
        webview->ExecuteScript(js.c_str(), nullptr);
    }

    void LogDomProbe(const char* reason) {
        if (!webview) return;
        const std::wstring js =
            LR"JS((function(){
                try {
                    var root = document.getElementById('root') || document.querySelector('[data-pm-chat-root]');
                    return {
                        href: String(location.href),
                        readyState: document.readyState,
                        title: document.title || '',
                        scripts: document.scripts ? document.scripts.length : -1,
                        bodyChildren: document.body ? document.body.children.length : -1,
                        bodyTextLen: document.body && document.body.innerText ? document.body.innerText.length : 0,
                        root: !!root,
                        rootChildren: root ? root.children.length : -1,
                        pmChat: !!window.pmChat,
                        appFeatures: !!window.APP_FEATURES,
                        bootEvents: window.__pmChatBootEvents || []
                    };
                } catch (e) {
                    return { probeError: String(e && (e.stack || e.message || e)) };
                }
            })();)JS";
        const std::string why = reason ? reason : "probe";
        webview->ExecuteScript(
            js.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [why](HRESULT hr, LPCWSTR result) -> HRESULT {
                    std::string line = "[chat-web] DOM probe (" + why + "): hr=0x"
                        + std::to_string(static_cast<std::uint32_t>(static_cast<ULONG>(hr)));
                    if (result)
                        line += " result=" + pmui::wide_to_utf8(result);
                    logger::info(line);
                    return S_OK;
                }).Get());
    }

    /// Drag/drop from embedded `IExplorerBrowser` / shell views often omit HTML5 `FileList`;
    /// Win32 `CF_HDROP` on the WebView2 render HWND is registered in `EnsureShellFileDropTarget`.
    IDropTarget* shell_drop_target = nullptr;
    HWND         shell_drop_hwnd   = nullptr;
    /// `getBoundingClientRect()` of the film-strip scroller (CSS px), mapped to the same space as
    /// `MapWindowPoints(shell_drop_hwnd → m_hostHwnd)` for OLE hit-testing + highlight sync.
    RECT  film_strip_rect_css{};
    bool  film_strip_rect_valid = false;

    void AppendContextPathsFromShell(std::vector<std::wstring> paths);
    void EnsureShellFileDropTarget();
    /// Safe to call from background threads (posts to the UI thread).
    void EnqueueScript(const std::wstring& js) {
        auto* p = new std::wstring(js);
        if (!::PostMessageW(m_hostHwnd, UWM_WEB_RUN_JS,
                            reinterpret_cast<WPARAM>(p), 0)) {
            delete p;
        }
    }

    /// Idempotent: navigate to the captured HTML once the host is genuinely
    /// visible AND non-zero. Called from WM_SIZE / WM_SHOWWINDOW / the
    /// controller-ready callback. WebView2 silently no-ops the page on
    /// first paint when bounds are zero, so we wait until they aren't.
    void EnsureNavigated() {
        if (!webview || navigated || !m_hostHwnd) return;
        if (!useChatWebOrigin && deferredHtml.empty()) return;
        if (!::IsWindowVisible(m_hostHwnd)) {
            if (!navWaitLogged) {
                navWaitLogged = true;
                logger::info("[chat-web] navigation deferred: host window not visible yet");
            }
            return;
        }
        RECT rc{};
        ::GetClientRect(m_hostHwnd, &rc);
        if (rc.right <= 1 || rc.bottom <= 1) {
            if (!navWaitLogged) {
                navWaitLogged = true;
                logger::info(std::string("[chat-web] navigation deferred: host bounds too small ")
                             + std::to_string(rc.right) + "x" + std::to_string(rc.bottom));
            }
            return;
        }
        if (controller) {
            apply_webview2_controller_bounds(controller.Get(), m_hostHwnd);
            controller->put_IsVisible(TRUE);
        }
        if (useChatWebOrigin) {
            // Stable https origin so chat embed `localStorage` (presets, prompt history) persists.
            const std::wstring nav = std::wstring(L"https://") + std::wstring(pm::brand::k_chat_web_vhost_w) + L"/chat.html";
            logger::info(std::string("[chat-web] Navigate: ")
                         + pmui::wide_to_utf8(nav)
                         + " bounds=" + std::to_string(rc.right) + "x" + std::to_string(rc.bottom));
            webview->Navigate(nav.c_str());
        } else {
            const std::wstring whtml = pmui::utf8_to_wide(deferredHtml);
            logger::info(std::string("[chat-web] NavigateToString: html bytes=")
                         + std::to_string(deferredHtml.size())
                         + " bounds=" + std::to_string(rc.right) + "x" + std::to_string(rc.bottom));
            webview->NavigateToString(whtml.c_str());
        }
        navigated = true;
    }
};

static HWND find_largest_chromium_render_widget_under(HWND host)
{
    if (!host || !::IsWindow(host))
        return nullptr;
    struct Ctx {
        HWND best     = nullptr;
        LONG bestArea = -1;
    } ctx;
    const auto enumProc = [](HWND hwnd, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t cls[160]{};
        if (::GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) == 0)
            return TRUE;
        if (!::wcsstr(cls, L"Chrome_RenderWidget"))
            return TRUE;
        RECT rc{};
        if (!::GetWindowRect(hwnd, &rc))
            return TRUE;
        const LONG w   = rc.right - rc.left;
        const LONG hgt = rc.bottom - rc.top;
        const LONG area = w * hgt;
        if (area > c->bestArea) {
            c->bestArea = area;
            c->best     = hwnd;
        }
        return TRUE;
    };
    ::EnumChildWindows(host, enumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

/// OLE `IDropTarget` on the Chromium render surface: reads `CF_HDROP` where WebView2 DOM
/// `DataTransfer.files` stays empty (e.g. drags from in-app shell/Explorer hosts).
class ChatShellDropTarget final : public IDropTarget {
public:
    explicit ChatShellDropTarget(CChatWebView::Impl* impl) : impl_(impl) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG c = InterlockedDecrement(&ref_);
        if (c == 0)
            delete this;
        return static_cast<ULONG>(c);
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* obj, DWORD /*grfKeyState*/, POINTL pt,
                                        DWORD* pdwEffect) override
    {
        if (!pdwEffect)
            return E_INVALIDARG;
        *pdwEffect = DROPEFFECT_NONE;
        has_hdrop_ = false;
        if (!obj || !impl_)
            return S_OK;
        FORMATETC fe{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (obj->QueryGetData(&fe) == S_OK)
            has_hdrop_ = true;
        apply_drag_pt(pt, pdwEffect);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD /*grfKeyState*/, POINTL pt, DWORD* pdwEffect) override
    {
        if (!pdwEffect)
            return E_INVALIDARG;
        apply_drag_pt(pt, pdwEffect);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        has_hdrop_ = false;
        PostHostFilmStripDragOver(impl_, false, &last_highlight_posted_);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* obj, DWORD /*grfKeyState*/, POINTL pt,
                                   DWORD* pdwEffect) override
    {
        if (!pdwEffect)
            return E_INVALIDARG;
        *pdwEffect = DROPEFFECT_NONE;
        PostHostFilmStripDragOver(impl_, false, &last_highlight_posted_);
        if (!obj || !impl_) {
            has_hdrop_ = false;
            return S_OK;
        }

        const POINT ptHost = OleDragPtlToHostClient(impl_->m_hostHwnd, pt);
        const bool inside = has_hdrop_ && FilmStripAcceptsShellDropAt(impl_, ptHost);
        has_hdrop_ = false;
        if (!inside)
            return S_OK;

        FORMATETC fe{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM st{};
        if (FAILED(obj->GetData(&fe, &st)))
            return S_OK;
        if (st.tymed != TYMED_HGLOBAL || !st.hGlobal) {
            ::ReleaseStgMedium(&st);
            return S_OK;
        }

        std::vector<std::wstring> paths;
        if (void* locked = ::GlobalLock(st.hGlobal)) {
            const auto hdrop = static_cast<HDROP>(locked);
            const UINT n = ::DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
            paths.reserve(static_cast<size_t>(n));
            for (UINT i = 0; i < n; ++i) {
                wchar_t buf[MAX_PATH * 4]{};
                if (::DragQueryFileW(hdrop, i, buf, static_cast<UINT>(std::size(buf))))
                    paths.emplace_back(buf);
            }
            ::GlobalUnlock(st.hGlobal);
        }
        ::ReleaseStgMedium(&st);

        if (!paths.empty()) {
            impl_->AppendContextPathsFromShell(std::move(paths));
            *pdwEffect = DROPEFFECT_COPY;
        }
        return S_OK;
    }

private:
    /// `IDropTarget` passes `POINTL` in **screen** coordinates (see MSDN). `getBoundingClientRect()`
    /// in the page matches **client** coords of the WebView host when the control fills the host.
    static POINT OleDragPtlToHostClient(HWND hwndHost, POINTL ptl)
    {
        POINT pt{ ptl.x, ptl.y };
        if (hwndHost && ::IsWindow(hwndHost))
            ::ScreenToClient(hwndHost, &pt);
        return pt;
    }

    static bool HostPtInFilmStripCssRect(CChatWebView::Impl* impl, POINT ptHost)
    {
        if (!impl || !impl->film_strip_rect_valid)
            return false;
        const RECT& R = impl->film_strip_rect_css;
        return ptHost.x >= R.left && ptHost.x < R.right && ptHost.y >= R.top && ptHost.y < R.bottom;
    }

    /** When the page has not published a rect yet, allow HDROP anywhere on the WebView (legacy). */
    static bool FilmStripAcceptsShellDropAt(CChatWebView::Impl* impl, POINT ptHost)
    {
        if (!impl || !impl->film_strip_rect_valid)
            return true;
        return HostPtInFilmStripCssRect(impl, ptHost);
    }

    static void PostHostFilmStripDragOver(CChatWebView::Impl* impl, bool over, bool* last_posted)
    {
        if (!impl || !impl->webview)
            return;
        if (last_posted && *last_posted == over)
            return;
        if (last_posted)
            *last_posted = over;
        nlohmann::json msg;
        msg["kind"] = "hostFilmStripDragOver";
        msg["over"] = over;
        const std::wstring payload_w = pmui::utf8_to_wide(msg.dump());
        (void)impl->webview->PostWebMessageAsString(payload_w.c_str());
    }

    void apply_drag_pt(POINTL ptl, DWORD* pdwEffect)
    {
        *pdwEffect = DROPEFFECT_NONE;
        if (!impl_ || !pdwEffect)
            return;
        const POINT ptHost = OleDragPtlToHostClient(impl_->m_hostHwnd, ptl);
        const bool accept = has_hdrop_ && FilmStripAcceptsShellDropAt(impl_, ptHost);
        if (accept)
            *pdwEffect = DROPEFFECT_COPY;
        const bool highlight = has_hdrop_ && HostPtInFilmStripCssRect(impl_, ptHost);
        PostHostFilmStripDragOver(impl_, highlight, &last_highlight_posted_);
    }

    LONG                 ref_ = 1;
    CChatWebView::Impl*  impl_;
    bool                 has_hdrop_           = false;
    bool                 last_highlight_posted_ = false;
};

void CChatWebView::Impl::AppendContextPathsFromShell(std::vector<std::wstring> paths)
{
    for (auto& p : paths) {
        while (!p.empty()
               && (p.back() == L' ' || p.back() == L'\t' || p.back() == L'\n' || p.back() == L'\r'))
            p.pop_back();
        size_t s0 = 0;
        while (s0 < p.size() && (p[s0] == L' ' || p[s0] == L'\t'))
            ++s0;
        if (s0)
            p.erase(0, s0);
        if (p.empty())
            continue;
        if (!pmui::chat_context_path_allowed(p))
            continue;
        context_removed.erase(
            std::remove_if(context_removed.begin(), context_removed.end(),
                           [&](const std::wstring& r) { return ::_wcsicmp(r.c_str(), p.c_str()) == 0; }),
            context_removed.end());
        bool dup = false;
        for (const auto& x : context_extra) {
            if (::_wcsicmp(x.c_str(), p.c_str()) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup)
            context_extra.push_back(std::move(p));
    }
    FlushContextToWeb();
}

void CChatWebView::Impl::EnsureShellFileDropTarget()
{
    if (!m_hostHwnd || !::IsWindow(m_hostHwnd) || !controller)
        return;

    if (shell_drop_hwnd && !::IsWindow(shell_drop_hwnd)) {
        shell_drop_hwnd = nullptr;
        // `RevokeDragDrop` on a destroyed HWND is undefined; target ref is released in ~CChatWebView.
    }

    HWND dropHwnd = find_largest_chromium_render_widget_under(m_hostHwnd);
    if (!dropHwnd)
        return;

    if (dropHwnd == shell_drop_hwnd && shell_drop_target)
        return;

    if (shell_drop_hwnd && ::IsWindow(shell_drop_hwnd)) {
        (void)::RevokeDragDrop(shell_drop_hwnd);
        shell_drop_hwnd = nullptr;
    }

    if (!shell_drop_target)
        shell_drop_target = new ChatShellDropTarget(this);

    const HRESULT hr = ::RegisterDragDrop(dropHwnd, shell_drop_target);
    if (FAILED(hr)) {
        logger::warn(std::string("[chat-web] RegisterDragDrop (shell HDROP) failed: HRESULT 0x")
                     + std::to_string(static_cast<uint32_t>(hr)));
        return;
    }
    shell_drop_hwnd = dropHwnd;
}

// ────────────────────────────────────────────────────────────────────────
// CChatWebView
// ────────────────────────────────────────────────────────────────────────

CChatWebView::CChatWebView() : m_impl(std::make_unique<Impl>()) {
    char buf[64]; snprintf(buf, sizeof(buf), "[chat-web] ctor @%p", static_cast<void*>(this));
    logger::info(buf);
}

void CChatWebView::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);
    // Flat host — visual-style Edge/WebView2 otherwise keeps a classic sunken
    // client border that reads as a bright halo on dark UI.
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
}

CChatWebView::~CChatWebView() {
    {
        char buf[64]; snprintf(buf, sizeof(buf), "[chat-web] dtor @%p", static_cast<void*>(this));
        logger::info(buf);
    }
    if (HWND h = GetHwnd(); h && ::IsWindow(h)) {
        ::KillTimer(h, kTimerWebView2ChromeResync);
    }
    if (m_impl) {
        if (m_impl->busManager)
            m_impl->busManager->UnregisterExternal("chat", this);
        if (m_impl->shell_drop_hwnd && ::IsWindow(m_impl->shell_drop_hwnd)) {
            (void)::RevokeDragDrop(m_impl->shell_drop_hwnd);
            m_impl->shell_drop_hwnd = nullptr;
        }
        if (m_impl->shell_drop_target) {
            m_impl->shell_drop_target->Release();
            m_impl->shell_drop_target = nullptr;
        }
        if (m_impl->cancel) m_impl->cancel->store(true);
        if (m_impl->worker.joinable()) m_impl->worker.join();
        if (m_impl->controller) {
            m_impl->controller->Close();
        }
    }
}

int CChatWebView::OnCreate(CREATESTRUCT&) {
    m_impl->m_hostHwnd = GetHwnd();

    // Start the global agent scheduler so scheduled tasks created by the LLM
    // (schedule_every / schedule_in / schedule_at) actually fire.
    //
    // EventCallback: post key events back to this WebView host via UWM_WEB_RUN_JS
    // (same PostMessageW path as Impl::EnqueueScript — safe from any thread).
    // The HWND is captured by value; PostMessageW to a dead HWND returns FALSE
    // and the wstring is deleted safely.
    {
        const HWND host = GetHwnd();

        media::llm::agent::global_scheduler().configure(
            []{ return make_provider_for_turn(nullptr); },
            [host](const media::llm::agent::Event& e) -> bool {
                using K = media::llm::agent::Event::Kind;
                try {

                // Post a JS snippet that calls pmChat.appendText({role, text}).
                auto append_chat = [host](const char* role, const std::string& text) {
                    nlohmann::json j;
                    j["role"] = role;
                    j["text"] = text;
                    std::wstring js = L"if(window.pmChat)window.pmChat.appendText("
                                     + pmui::utf8_to_wide(j.dump()) + L");";
                    auto* p = new std::wstring(std::move(js));
                    if (!::PostMessageW(host, UWM_WEB_RUN_JS,
                                        reinterpret_cast<WPARAM>(p), 0))
                        delete p;
                };

                switch (e.kind) {
                    case K::TurnStarted: {
                        // One-liner header so the user sees which task fired.
                        std::string hdr = "\u23F1 Scheduled tick";
                        if (!e.text.empty()) {
                            std::string p = e.text;
                            if (p.size() > 80) { p.resize(77); p += "..."; }
                            hdr += ": " + p;
                        }
                        append_chat("system", hdr);
                        break;
                    }
                    case K::ToolCall: {
                        // Use object() not {} — nlohmann::json{} is null, not empty object.
                        const auto args = e.payload.is_object()
                            ? e.payload.value("arguments", nlohmann::json::object())
                            : nlohmann::json::object();
                        std::string text = "\u26A1 " + e.tool_name;
                        std::string s = args.is_null() ? std::string{} : args.dump();
                        if (s.size() > 120) s = utf8_safe_prefix(s, 117) + "...";
                        if (!s.empty() && s != "{}") text += " " + s;
                        append_chat("tool", text);
                        if (e.tool_name == "speak") {
                            auto* p2 = new std::wstring(L"if(window.pmChat)window.pmChat.ttsStarted();");
                            if (!::PostMessageW(host, UWM_WEB_RUN_JS, reinterpret_cast<WPARAM>(p2), 0))
                                delete p2;
                        }
                        break;
                    }
                    case K::ToolResult: {
                        const auto env = e.payload.is_object()
                            ? e.payload.value("envelope", nlohmann::json::object())
                            : nlohmann::json::object();
                        // summary is optional — only batch-file tools include it.
                        const auto sum = (env.is_object() && env.contains("summary") && env["summary"].is_object())
                            ? env["summary"]
                            : nlohmann::json::object();
                        const int  total = sum.value("total",     0);
                        const int  ok    = sum.value("succeeded", 0);
                        const int  fail  = sum.value("failed",    0);
                        std::string suffix;
                        if (total > 0) {
                            suffix = " \u2713 " + std::to_string(ok) + "/" + std::to_string(total);
                            if (fail > 0) suffix += " (" + std::to_string(fail) + " failed)";
                        } else {
                            suffix = (env.is_object() && env.value("ok", false)) ? " \u2713" : " \u2717";
                        }
                        append_chat("tool", "     " + e.tool_name + suffix);
                        if (e.tool_name == "speak") {
                            auto* p2 = new std::wstring(L"if(window.pmChat)window.pmChat.ttsDone();");
                            if (!::PostMessageW(host, UWM_WEB_RUN_JS, reinterpret_cast<WPARAM>(p2), 0))
                                delete p2;
                        }
                        break;
                    }
                    case K::AssistantText:
                        if (!e.text.empty()) append_chat("assistant", e.text);
                        break;
                    case K::Error:
                        append_chat("error", "\u2718 " + e.text);
                        break;
                    default:
                        break;
                }
                } catch (const std::exception& ex) {
                    // Never let a JSON parse/type error in the UI callback crash the scheduler thread.
                    auto* p = new std::wstring(
                        L"if(window.pmChat)window.pmChat.appendText({\"role\":\"error\","
                        L"\"text\":\"[scheduler callback error]\");");
                    (void)ex;
                    if (!::PostMessageW(host, UWM_WEB_RUN_JS, reinterpret_cast<WPARAM>(p), 0))
                        delete p;
                } catch (...) {}
                return true; // never cancel ticks from the UI event callback
            });

        media::llm::agent::global_scheduler().start();
    }

    const std::wstring userData = webview2_user_data_folder();
    if (userData.empty()) {
        const std::wstring wdu = std::wstring(pm::brand::k_app_id_w)
            + L": could not resolve WebView2 user-data folder (app config directory / web).";
        ::MessageBoxW(nullptr, wdu.c_str(), L"Chat (web) startup", MB_ICONERROR);
        return 0;
    }

    HWND host = m_impl->m_hostHwnd;
    Impl* impl = m_impl.get();   // captured by value (raw ptr) — Impl outlives the lambdas

    // Async WebView2 environment creation. The success callback (envCb)
    // creates a Controller; the controller-ready callback (ctrlCb) finishes
    // wiring (Settings, virtual host mapping, message handler, navigation).
    auto envCb = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [host, impl](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(hr) || !env) {
                logger::error("[chat-web] CreateCoreWebView2Environment failed: HRESULT 0x"
                              + std::to_string(static_cast<uint32_t>(hr)));
                return S_OK;
            }
            return env->CreateCoreWebView2Controller(host,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [host, impl](HRESULT hr2, ICoreWebView2Controller* ctrl) -> HRESULT {
                        if (FAILED(hr2) || !ctrl) {
                            logger::error("[chat-web] CreateCoreWebView2Controller failed: HRESULT 0x"
                                          + std::to_string(static_cast<uint32_t>(hr2)));
                            return S_OK;
                        }
                        impl->controller = ctrl;
                        ctrl->get_CoreWebView2(&impl->webview);
                        if (!impl->webview) return S_OK;
                        {
                            ComPtr<ICoreWebView2Controller4> c4;
                            if (SUCCEEDED(ctrl->QueryInterface(IID_PPV_ARGS(&c4))) && c4) {
                                // FALSE: WebView2 does not register as the file drop target — we use Win32
                                // `IDropTarget` + `CF_HDROP` on the Chromium render HWND so in-app shell
                                // (`IExplorerBrowser`) drags still yield paths (DOM `DataTransfer.files` stays empty).
                                (void)c4->put_AllowExternalDrop(FALSE);
                            }
                        }

                        // Resize WebView2 to the host's client rect. The host's
                        // WM_SIZE handler will update this on subsequent resizes.
                        apply_webview2_controller_bounds(ctrl, host);
                        apply_webview2_match_theme_background(ctrl);

                        // ── Settings: lock down what we don't need ─────────────
                        ComPtr<ICoreWebView2Settings> s;
                        impl->webview->get_Settings(&s);
                        if (s) {
                            s->put_AreDevToolsEnabled(TRUE);          // F12 in dev; harmless
                            s->put_AreDefaultContextMenusEnabled(TRUE);
                            // TRUE: Edge-style Ctrl+Plus/Minus/0 and Ctrl+wheel (page ZoomFactor),
                            // independent of App Settings `setFontExtraPt` (root rem scaling).
                            s->put_IsZoomControlEnabled(TRUE);
                            s->put_IsStatusBarEnabled(FALSE);
                            s->put_AreHostObjectsAllowed(FALSE);
                        }
                        apply_webview2_preferred_color_scheme(impl->webview.Get());

                        const std::wstring appFeaturesJs = chat_app_features_bootstrap_js();
                        impl->webview->AddScriptToExecuteOnDocumentCreated(
                            appFeaturesJs.c_str(), nullptr);

                        const std::wstring systemContextJs = pmui::webview_system_context_bootstrap_js();
                        impl->webview->AddScriptToExecuteOnDocumentCreated(
                            systemContextJs.c_str(), nullptr);

                        constexpr const wchar_t* chatBootErrorCaptureJs =
                            LR"JS((function(){
                                try {
                                    window.__pmChatBootEvents = [];
                                    var push = function(kind, detail) {
                                        try {
                                            window.__pmChatBootEvents.push({
                                                kind: kind,
                                                detail: String(detail || ''),
                                                href: String(location.href),
                                                readyState: document.readyState
                                            });
                                        } catch (_) {}
                                    };
                                    window.addEventListener('error', function(e) {
                                        push('error', (e && (e.message || e.filename || e.error)) || 'error');
                                    });
                                    window.addEventListener('unhandledrejection', function(e) {
                                        var r = e && e.reason;
                                        push('unhandledrejection', r && (r.stack || r.message || r));
                                    });
                                    document.addEventListener('DOMContentLoaded', function() {
                                        push('domcontentloaded', 'scripts=' + document.scripts.length);
                                    });
                                    push('bootstrap-installed', '');
                                } catch (_) {}
                            })();)JS";
                        impl->webview->AddScriptToExecuteOnDocumentCreated(
                            chatBootErrorCaptureJs, nullptr);

                        // ── Virtual host: render LLM-generated images ─────────
                        // WebView2's SetVirtualHostNameToFolderMapping maps a single
                        // hostname → a single folder. To cover every fixed drive we
                        // register one host per letter:
                        //   pm-files-c.local → C:\
                        //   pm-files-d.local → D:\
                        //   …
                        // The JS-side rewriter (chat-next hostBridge) routes a path
                        // like "C:\foo\bar.jpg" to "https://pm-files-c.local/foo/bar.jpg",
                        // dropping the drive letter from the URL path so WebView2
                        // resolves it correctly under the mapped folder root.
                        ComPtr<ICoreWebView2_3> v3;
                        if (SUCCEEDED(impl->webview.As(&v3)) && v3) {
                            const DWORD mask = ::GetLogicalDrives();
                            for (int i = 0; i < 26; ++i) {
                                if (!(mask & (1u << i))) continue;
                                wchar_t root[4] = { static_cast<wchar_t>(L'A' + i), L':', L'\\', 0 };
                                if (::GetDriveTypeW(root) != DRIVE_FIXED &&
                                    ::GetDriveTypeW(root) != DRIVE_REMOTE) continue;
                                wchar_t host[24] = {};
                                swprintf_s(host, L"pm-files-%c.local",
                                           static_cast<wchar_t>(L'a' + i));
                                HRESULT hr = v3->SetVirtualHostNameToFolderMapping(
                                    host, root,
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                                if (FAILED(hr)) {
                                    logger::warn(std::string("[chat-web] virtual-host mapping failed for drive ")
                                                 + static_cast<char>('A' + i));
                                }
                            }
                        }

                        // ── Bridge: web → host (JSON over PostWebMessageAsString) ─
                        impl->webview->add_WebMessageReceived(
                            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [host, impl](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                    LPWSTR raw = nullptr;
                                    if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw) return S_OK;
                                    std::wstring wmsg(raw);
                                    ::CoTaskMemFree(raw);
                                    // Posted-back to the WindowProc on the UI thread.
                                    auto* pw = new std::wstring(std::move(wmsg));
                                    if (!::PostMessageW(host, WM_APP + 1,
                                                        reinterpret_cast<WPARAM>(pw), 0)) {
                                        delete pw;
                                    }
                                    return S_OK;
                                }).Get(), &impl->msgToken);

                        impl->webview->add_NavigationStarting(
                            Callback<ICoreWebView2NavigationStartingEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                    LPWSTR uri = nullptr;
                                    if (args && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                                        logger::info(std::string("[chat-web] NavigationStarting: ")
                                                     + pmui::wide_to_utf8(uri));
                                        ::CoTaskMemFree(uri);
                                    } else {
                                        logger::info("[chat-web] NavigationStarting: <no uri>");
                                    }
                                    return S_OK;
                                }).Get(), &impl->navStartingToken);

                        impl->webview->add_ContentLoading(
                            Callback<ICoreWebView2ContentLoadingEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2ContentLoadingEventArgs* args) -> HRESULT {
                                    UINT64 nav_id = 0;
                                    if (args)
                                        (void)args->get_NavigationId(&nav_id);
                                    logger::info(std::string("[chat-web] ContentLoading: navigationId=")
                                                 + std::to_string(nav_id));
                                    return S_OK;
                                }).Get(), &impl->contentLoadingToken);

                        impl->webview->add_ProcessFailed(
                            Callback<ICoreWebView2ProcessFailedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                                    COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                                    if (args)
                                        (void)args->get_ProcessFailedKind(&kind);
                                    logger::error(std::string("[chat-web] ProcessFailed: kind=")
                                                  + std::to_string(static_cast<int>(kind)));
                                    return S_OK;
                                }).Get(), &impl->processFailedToken);

                        // Re-flatten host chrome when navigation finishes — WebView2
                        // sometimes creates / resizes GPU child HWNDs after first paint.
                        impl->webview->add_NavigationCompleted(
                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [host, impl](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    BOOL isSuccess = 0;
                                    COREWEBVIEW2_WEB_ERROR_STATUS web_status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                                    UINT64 nav_id = 0;
                                    if (args) {
                                        (void)args->get_IsSuccess(&isSuccess);
                                        (void)args->get_WebErrorStatus(&web_status);
                                        (void)args->get_NavigationId(&nav_id);
                                    }
                                    std::string source = "<unknown>";
                                    if (impl->webview) {
                                        LPWSTR srcW = nullptr;
                                        if (SUCCEEDED(impl->webview->get_Source(&srcW)) && srcW) {
                                            source = pmui::wide_to_utf8(srcW);
                                            ::CoTaskMemFree(srcW);
                                        }
                                    }
                                    logger::info(std::string("[chat-web] NavigationCompleted: success=")
                                                 + std::to_string(isSuccess ? 1 : 0)
                                                 + " status=" + std::to_string(static_cast<int>(web_status))
                                                 + " navigationId=" + std::to_string(nav_id)
                                                 + " source=" + source);
                                    if (impl->webview)
                                        impl->LogDomProbe("NavigationCompleted");
                                    if (pmui::is_ui_chat_standalone_session() && args
                                        && !impl->standaloneNavPerf) {
                                        if (isSuccess && impl->webview) {
                                            // Skip the initial about:blank completion; log the first real document.
                                            LPWSTR srcW = nullptr;
                                            bool  realDoc = false;
                                            if (SUCCEEDED(impl->webview->get_Source(&srcW)) && srcW) {
                                                if (wcsncmp(srcW, L"about:", 6) != 0) realDoc = true;
                                                ::CoTaskMemFree(srcW);
                                            }
                                            if (realDoc) {
                                            impl->standaloneNavPerf = true;
                                            const uint64_t e2e_ms = pmui::ui_log_file_ms_from_process_entry();
                                            pmui::ui_log_file_event_from_process_entry(
                                                "ui-chat: WebView2 first navigation complete (document loaded)");
                                            logger::info(
                                                std::string("[chat-web] ui-chat: ") + std::to_string(e2e_ms)
                                                + " ms from process entry to nav complete (use \"app loaded\" for E2E)");
                                            }
                                        }
                                    }
                                    if (impl->controller)
                                        chat_resync_webview2_chrome(impl->controller.Get(), host);
                                    chat_schedule_chrome_resync(host);
                                    impl->EnsureShellFileDropTarget();
                                    return S_OK;
                                }).Get(), &impl->navCompletedToken);

                        // ── Capture embedded HTML, defer the actual nav ──────
                        // WebView2's first-paint pipeline gets stuck if we
                        // navigate while the host has zero bounds (it can —
                        // the panel is hidden by default and only shows when
                        // the user clicks View → Chat). EnsureNavigated() is
                        // idempotent and runs the navigate once visibility +
                        // bounds are real — see WM_SIZE / WM_SHOWWINDOW.
                        impl->deferredHtml = pmui::load_chat_web_html();
                        if (impl->deferredHtml.empty()) {
                            const std::wstring wmiss = std::wstring(pm::brand::k_app_id_w)
                                + L": chat.html is missing or empty (expected dist\\shared\\chat.html).";
                            ::MessageBoxW(host, wmiss.c_str(), L"Chat (web) startup", MB_ICONERROR);
                            return S_OK;
                        }
                        // Durable localStorage: serve bundle from a virtual https origin (not NavigateToString).
                        impl->useChatWebOrigin
                            = install_chat_web_virtual_host(impl->webview.Get(), impl->deferredHtml);
                        if (impl->useChatWebOrigin) {
                            logger::info(
                                std::string("[chat-web] chat UI: https://")
                                + pmui::wide_to_utf8(std::wstring(pm::brand::k_chat_web_vhost_w))
                                + "/chat.html (localStorage persists)");
                        } else {
                            logger::info(
                                "[chat-web] chat UI: NavigateToString fallback (localStorage will not survive restart)");
                        }
                        impl->ready = true;
                        impl->EnsureNavigated();   // first try — usually no-op when host is hidden

                        // Flush any pending scripts queued before the controller
                        // came up (e.g. a SetContext from CMainFrame::OnChat).
                        for (auto& js : impl->pendingScripts) {
                            impl->RunJsNow(js);
                        }
                        impl->pendingScripts.clear();

                        {
                            char buf[80];
                            snprintf(buf, sizeof(buf), "[chat-web] WebView2 controller ready @%p hwnd=%p",
                                static_cast<void*>(impl), reinterpret_cast<void*>(host));
                            logger::info(buf);
                        }
                        {
                            bool expected = false;
                            if (impl->mcp_catalog_probe_started.compare_exchange_strong(expected, true))
                                chat_web_start_mcp_catalog_probe(host);
                        }
                        const auto& pal = pmui::theme_palette();
                        if (impl->controller)
                            chat_resync_webview2_chrome(impl->controller.Get(), host);
                        pmui::apply_window_theme_recursive(host, pal.dark);
                        chat_schedule_chrome_resync(host);
                        impl->EnsureShellFileDropTarget();
                        return S_OK;
                    }).Get());
        });

    // Create the environment. We pass a per-app user data folder so cookies
    // / cache live alongside the app data folder, not the system browser.
    HRESULT hr = ::CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), nullptr, envCb.Get());
    if (FAILED(hr)) {
        std::wstring msg =
            std::wstring(pm::brand::k_app_id_w) + L": WebView2 runtime is not available (HRESULT 0x"
          + std::to_wstring(static_cast<uint32_t>(hr)) + L").\n\n"
            L"Modern Windows ships the WebView2 Runtime by default. If this fails on your "
            L"machine, install it from:\n  https://developer.microsoft.com/microsoft-edge/webview2/\n\n"
            L"The native Win32 chat panel will be used instead.";
        ::MessageBoxW(nullptr, msg.c_str(), pm::brand::k_w_msgbox_chatweb_title_w, MB_ICONWARNING);
    }
    return 0;
}

void CChatWebView::SetContext(const std::vector<std::wstring>& selection,
                              const std::wstring&            folder,
                              std::string_view               display_language,
                              bool                           explorer_ctrl_additive) {
    const std::vector<std::wstring> old_sel = m_impl->selection;
    // Only Ctrl+additive Explorer changes (strict superset + Ctrl captured in FileTreePanel)
    // append *new* image/text paths (size + binary sniff) to `context_extra`. Plain clicks replace
    // `selection` only; drag/paste still use `addContextPaths`.
    bool monotonic_add = !old_sel.empty() && selection.size() > old_sel.size();
    if (monotonic_add) {
        for (const auto& w : old_sel) {
            if (!path_in_list_ci(w, selection)) {
                monotonic_add = false;
                break;
            }
        }
    }
    if (explorer_ctrl_additive && monotonic_add) {
        for (const auto& w : selection) {
            if (path_in_list_ci(w, old_sel)) continue;
            if (!pmui::chat_context_path_allowed(w)) continue;
            if (path_in_list_ci(w, m_impl->context_removed)) continue;
            bool dup = false;
            for (const auto& x : m_impl->context_extra) {
                if (::_wcsicmp(x.c_str(), w.c_str()) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            m_impl->context_extra.push_back(w);
        }
    }

    m_impl->selection = selection;
    m_impl->folder    = folder;
    if (!display_language.empty())
        m_impl->ui_locale = std::string(display_language);
    // Drop suppression entries that no longer correspond to any Explorer or extra path.
    {
        const auto merged = merge_path_lists_unique(m_impl->selection, m_impl->context_extra);
        m_impl->context_removed.erase(
            std::remove_if(m_impl->context_removed.begin(), m_impl->context_removed.end(),
                           [&](const std::wstring& r) { return !path_in_list_ci(r, merged); }),
            m_impl->context_removed.end());
    }
    m_impl->FlushContextToWeb();
}

void CChatWebView::SetDisplayLanguage(std::string_view display_language) {
    if (!display_language.empty())
        m_impl->ui_locale = std::string(display_language);
    m_impl->FlushContextToWeb();
}

void CChatWebView::AppendTranscript(const std::wstring& role, const std::wstring& text) {
    nlohmann::json j;
    j["role"] = pmui::wide_to_utf8(role);
    j["text"] = pmui::wide_to_utf8(text);
    const std::wstring js = L"if(window.pmChat)window.pmChat.appendText(" +
                             pmui::utf8_to_wide(j.dump()) + L");";
    if (m_impl->ready) m_impl->RunJsNow(js);
    else               m_impl->pendingScripts.push_back(js);
}

void CChatWebView::FocusInput() {
    const std::wstring js = L"setTimeout(()=>{const i=document.getElementById('pm-input');if(i)i.focus();},0);";
    if (m_impl->ready) m_impl->RunJsNow(js);
    else               m_impl->pendingScripts.push_back(js);
}

void CChatWebView::RefreshChromeForTheme()
{
    if (!m_impl || !m_impl->controller || !IsWindow()) return;
    chat_resync_webview2_chrome(m_impl->controller.Get(), GetHwnd());
    const auto& pal = pmui::theme_palette();
    pmui::apply_window_theme_recursive(GetHwnd(), pal.dark);
    // Re-sync JS composer typography with `appearance.font_size_extra_pt` (App Settings).
    m_impl->PushFontExtraToWeb();
    chat_schedule_chrome_resync(GetHwnd());
    m_impl->EnsureShellFileDropTarget();
    ::InvalidateRect(GetHwnd(), nullptr, TRUE);
}

void CChatWebView::PostPixlwizAuthToWeb()
{
#if !defined(FEATURE_PIXLWIZ_AUTH) || !FEATURE_PIXLWIZ_AUTH
    return;
#else
    if (!m_impl || !m_impl->webview || !m_impl->ready)
        return;
    nlohmann::json msg = pmui::pixlwiz_auth::make_host_auth_message();
    const std::wstring payload_w = pmui::utf8_to_wide(msg.dump());
    (void)m_impl->webview->PostWebMessageAsString(payload_w.c_str());
    // When the user is logged in, asynchronously fetch the LiteLLM budget and
    // post a separate hostPixlwizCredit message once the network call returns.
    if (msg.value("read_ok", false) && msg.value("logged_in", false))
        chat_web_start_pixlwiz_credit_fetch(GetHwnd());
#endif
}

void CChatWebView::SetBusManager(CWebViewManager* manager)
{
    if (!m_impl)
        return;
    if (m_impl->busManager && m_impl->busManager != manager)
        m_impl->busManager->UnregisterExternal("chat", this);
    m_impl->busManager = manager;
    if (m_impl->busManager) {
        m_impl->busManager->RegisterExternal("chat", this,
            [this]() {
                return IsWindow() != FALSE;
            },
            [this](const std::string& json_utf8) {
                PostToWeb(json_utf8);
            });
    }
}

void CChatWebView::PostToWeb(const std::string& json_utf8)
{
    if (!m_impl || !m_impl->webview)
        return;
    const std::wstring payload_w = pmui::utf8_to_wide(json_utf8);
    (void)m_impl->webview->PostWebMessageAsString(payload_w.c_str());
}

// @todo : chat web : message handling
LRESULT CChatWebView::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    try {
        switch (msg) {
        case WM_DESTROY:
            {
                char buf[80];
                snprintf(buf, sizeof(buf), "[chat-web] WM_DESTROY @%p hwnd=%p — closing WebView2",
                    static_cast<void*>(this), reinterpret_cast<void*>(GetHwnd()));
                logger::info(buf);
            }
            // Tear down WebView2 while the HWND is still valid so the browser
            // process exits promptly. Worker thread is joined first to avoid
            // it posting to a dead HWND after the controller is closed.
            if (m_impl) {
                if (m_impl->busManager)
                    m_impl->busManager->UnregisterExternal("chat", this);
                if (m_impl->cancel) m_impl->cancel->store(true);
                if (m_impl->worker.joinable()) m_impl->worker.join();
                if (m_impl->webview) {
                    m_impl->webview->remove_WebMessageReceived(m_impl->msgToken);
                    m_impl->webview->remove_NavigationStarting(m_impl->navStartingToken);
                    m_impl->webview->remove_ContentLoading(m_impl->contentLoadingToken);
                    m_impl->webview->remove_ProcessFailed(m_impl->processFailedToken);
                    m_impl->webview->remove_NavigationCompleted(m_impl->navCompletedToken);
                    m_impl->webview->Stop();
                    m_impl->webview.Reset();
                }
                if (m_impl->controller) {
                    m_impl->controller->Close();
                    m_impl->controller.Reset();
                }
                m_impl->m_hostHwnd = nullptr;
                m_impl->busManager = nullptr;
                m_impl->ready = false;
            }
            break;

        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            if (hdc) {
                RECT rc{};
                ::GetClientRect(GetHwnd(), &rc);
                const auto& pal = pmui::theme_palette();
                HBRUSH br = ::CreateSolidBrush(pal.web_surface_bg);
                if (br) {
                    ::FillRect(hdc, &rc, br);
                    ::DeleteObject(br);
                }
            }
            return 1;
        }
        case WM_SIZE: {
            if (m_impl->controller)
                apply_webview2_controller_bounds(m_impl->controller.Get(), GetHwnd());
            // Bounds are now real (or still 0). Try to finalise a deferred
            // navigation if we were waiting for visibility/size to land.
            m_impl->EnsureNavigated();
            chat_schedule_chrome_resync(GetHwnd());
            m_impl->EnsureShellFileDropTarget();
            break;
        }
        // WebView2 keeps an internal "is the host visible" + cached bounds.
        // When the panel is created HIDDEN (current default — only Explorer +
        // Queue + Log are visible at first launch), the controller comes up
        // with bounds {0,0,0,0} and IsVisible=FALSE. Showing the panel via
        // the View toggle later doesn't always re-trigger paint, so we both
        // (a) flip IsVisible explicitly here and (b) drive a deferred
        // NavigateToString from EnsureNavigated() once the host is real.
        case WM_SHOWWINDOW: {
            const bool shown = (wp != FALSE);
            if (m_impl->controller) {
                m_impl->controller->put_IsVisible(shown ? TRUE : FALSE);
                if (shown) {
                    RECT rc; ::GetClientRect(GetHwnd(), &rc);
                    if (rc.right > 0 && rc.bottom > 0)
                        apply_webview2_controller_bounds(m_impl->controller.Get(), GetHwnd());
                }
            }
            if (shown) {
                m_impl->EnsureNavigated();
                chat_schedule_chrome_resync(GetHwnd());
                m_impl->EnsureShellFileDropTarget();
            }
            break;
        }
        // Win32 does not always send WM_SHOWWINDOW to deeply-nested children
        // when an ancestor docker becomes visible — only WM_SIZE and
        // WM_WINDOWPOSCHANGED propagate reliably. Mirror the visibility +
        // navigation logic here so even the "ancestor was shown but my
        // WM_SHOWWINDOW never fired" path recovers on first reveal.
        case WM_WINDOWPOSCHANGED: {
            if (m_impl->controller && ::IsWindowVisible(GetHwnd())) {
                RECT rc; ::GetClientRect(GetHwnd(), &rc);
                if (rc.right > 0 && rc.bottom > 0) {
                    m_impl->controller->put_IsVisible(TRUE);
                    apply_webview2_controller_bounds(m_impl->controller.Get(), GetHwnd());
                }
            }
            m_impl->EnsureNavigated();
            chat_schedule_chrome_resync(GetHwnd());
            m_impl->EnsureShellFileDropTarget();
            break;
        }
        case WM_TIMER: {
            if (wp == kTimerWebView2ChromeResync) {
                ::KillTimer(GetHwnd(), kTimerWebView2ChromeResync);
                if (m_impl->controller)
                    chat_resync_webview2_chrome(m_impl->controller.Get(), GetHwnd());
                m_impl->EnsureShellFileDropTarget();
                return 0;
            }
            break;
        }
        case UWM_WEB_RUN_JS: {
            auto* pjs = reinterpret_cast<std::wstring*>(wp);
            if (pjs) {
                if (m_impl->ready) m_impl->RunJsNow(*pjs);
                delete pjs;
            }
            return 0;
        }
        case UWM_MCP_CATALOG_READY: {
            auto* pj = reinterpret_cast<nlohmann::json*>(lp);
            if (pj) {
                m_impl->mcp_web_catalog   = std::move(*pj);
                m_impl->mcp_catalog_loaded = true;
                delete pj;
                m_impl->FlushContextToWeb();
            }
            return 0;
        }
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
        case UWM_PIXLWIZ_CREDIT_READY: {
            auto* pj = reinterpret_cast<nlohmann::json*>(lp);
            if (pj) {
                nlohmann::json credit_j = std::move(*pj);
                delete pj;
                if (m_impl && m_impl->webview && m_impl->ready) {
                    nlohmann::json hostMsg = pmui::pixlwiz_auth::make_host_credit_message(credit_j);
                    if (!hostMsg.value("ok", false)) {
                        const std::string berr = credit_j.value("error", std::string{"unknown"});
                        logger::warn("[chat-web] pixlwiz credit fetch failed: " + berr);
                    }
                    const std::wstring pw = pmui::utf8_to_wide(hostMsg.dump());
                    (void)m_impl->webview->PostWebMessageAsString(pw.c_str());
                }
            }
            return 0;
        }
#endif
        case WM_APP + 1: {                           // ← web → host JSON
            auto* pw = reinterpret_cast<std::wstring*>(wp);
            if (!pw) return 0;
            std::wstring raw = std::move(*pw); delete pw;

            // Parse the JSON message and dispatch.
            const std::string rawUtf8 = pmui::wide_to_utf8(raw);
            nlohmann::json j;
            try {
                j = nlohmann::json::parse(rawUtf8);
            } catch (...) { return 0; }
            if (j.value("t", std::string{}) == "cweb_bus") {
                if (m_impl->busManager)
                    m_impl->busManager->HandleExternalMessage("chat", rawUtf8);
                return 0;
            }
            const std::string kind = j.value("kind", std::string{});

            if (kind == "ready") {
                if (pmui::is_ui_chat_standalone_session() && !m_impl->standaloneReadyPerf) {
                    m_impl->standaloneReadyPerf = true;
                    const uint64_t e2e_ms = pmui::ui_log_file_ms_from_process_entry();
                    pmui::ui_log_file_event_from_process_entry(
                        "ui-chat: app loaded (web UI ready, JS postMessage kind=ready) — E2E");
                    logger::info(std::string("[chat-web] ui-chat: ") + std::to_string(e2e_ms)
                                 + " ms from process entry to app loaded (cli → ready)");
                }
                // Standalone `--ui-chat`: fade startup splash here. Embedded chat workbench uses the normal
                // 150ms deferred splash dismiss (`OnDeferredPostLayoutInit`); this call is a no-op when no watch was registered.
                pmui::splash_on_chat_composer_ready();
                // Match App Settings display language, then push selection / folder.
                media::settings::AppearanceSettings appearance0;
                std::string                        aerr0;
                if (media::settings::load_appearance(appearance0, aerr0))
                    m_impl->ui_locale = appearance0.display_language;
                m_impl->FlushContextToWeb();
                m_impl->PushFontExtraToWeb();
                // Rehydrate WebView2 state from settings.json (prompt history, quick actions, panel toggles).
                std::string cwl_err;
                nlohmann::json cw;
                if (m_impl->webview && media::settings::load_chat_web(cw, cwl_err) && cw.is_object()) {
                    merge_resize_presets_into_chat_web_doc(cw, m_impl->ui_locale);
                    merge_design_presets_into_chat_web_doc(cw, m_impl->ui_locale);
                    nlohmann::json hostMsg;
                    hostMsg["kind"]  = "hostChatWeb";
                    hostMsg["doc"]   = std::move(cw);
                    const std::string   payload   = hostMsg.dump();
                    const std::wstring    payload_w = pmui::utf8_to_wide(payload);
                    m_impl->webview->PostWebMessageAsString(payload_w.c_str());
                }
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
                PostPixlwizAuthToWeb();
#endif
            } else if (kind == "pixlwizRefreshAuth") {
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
                PostPixlwizAuthToWeb();
#endif
                return 0;
            } else if (kind == "pixlwizRefreshCredit") {
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
                chat_web_start_pixlwiz_credit_fetch(GetHwnd());
#endif
                return 0;
            } else if (kind == "chatWebState") {
                if (j.contains("doc") && j["doc"].is_object()) {
                    std::string serr;
                    // `buildChatWebDoc()` omits `sessions`; keep host-backed `chat_web.sessions` from disk.
                    nlohmann::json doc_to_save = j["doc"];
                    {
                        nlohmann::json existing;
                        std::string    eload;
                        constexpr const char* sk = "sessions";
                        if (media::settings::load_chat_web(existing, eload) && existing.is_object()) {
                            if ((!doc_to_save.contains(sk) || !doc_to_save[sk].is_array())
                                && existing.contains(sk) && existing[sk].is_array())
                                doc_to_save[sk] = existing[sk];
                        }
                    }
                    if (!media::settings::save_chat_web(doc_to_save, serr)) {
                        logger::warn(std::string("[chat-web] save_chat_web: ") + serr);
                    }
                }
            } else if (kind == "addContextPaths") {
                if (j.contains("paths") && j["paths"].is_array()) {
                    std::vector<std::wstring> paths;
                    paths.reserve(j["paths"].size());
                    for (const auto& el : j["paths"]) {
                        if (!el.is_string()) continue;
                        const std::wstring wp = pmui::utf8_to_wide(el.get<std::string>());
                        const std::string deny = media::llm::llm_fs_guard_deny_reason(
                            std::filesystem::path(wp));
                        if (!deny.empty()) {
                            logger::warn("[chat-web] addContextPaths blocked: " + deny
                                + " path='" + el.get<std::string>() + "'");
                            continue;
                        }
                        paths.push_back(wp);
                    }
                    m_impl->AppendContextPathsFromShell(std::move(paths));
                }
            } else if (kind == "filmStripClientRect") {
                auto json_to_long = [](const nlohmann::json& v) -> LONG {
                    if (v.is_number_integer())
                        return static_cast<LONG>(v.get<std::int64_t>());
                    if (v.is_number_unsigned())
                        return static_cast<LONG>(v.get<std::uint64_t>());
                    if (v.is_number_float())
                        return static_cast<LONG>(std::llround(v.get<double>()));
                    return 0;
                };
                if (!j.contains("rect") || j["rect"].is_null()) {
                    m_impl->film_strip_rect_valid = false;
                    m_impl->film_strip_rect_css   = {};
                } else if (j["rect"].is_object()) {
                    const auto& r = j["rect"];
                    RECT rc{};
                    rc.left   = json_to_long(r.value("left", 0.0));
                    rc.top    = json_to_long(r.value("top", 0.0));
                    rc.right  = json_to_long(r.value("right", 0.0));
                    rc.bottom = json_to_long(r.value("bottom", 0.0));
                    if (rc.right > rc.left && rc.bottom > rc.top) {
                        m_impl->film_strip_rect_css   = rc;
                        m_impl->film_strip_rect_valid = true;
                    } else {
                        m_impl->film_strip_rect_valid = false;
                        m_impl->film_strip_rect_css   = {};
                    }
                }
                return 0;
            } else if (kind == "pickContextPaths") {
                const std::string mode = j.value("mode", std::string{ "files" });
                if (mode == "folder") {
                    BROWSEINFOW bi{};
                    bi.hwndOwner      = GetHwnd();
                    bi.lpszTitle      = L"Select a folder to add to chat context";
                    bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                    wchar_t           dn[MAX_PATH]{};
                    bi.pszDisplayName = dn;
                    PIDLIST_ABSOLUTE pidl = ::SHBrowseForFolderW(&bi);
                    if (pidl) {
                        wchar_t path[MAX_PATH * 4]{};
                        if (::SHGetPathFromIDListW(pidl, path)) {
                            std::vector<std::wstring> one;
                            one.emplace_back(path);
                            m_impl->AppendContextPathsFromShell(std::move(one));
                        }
                        ::CoTaskMemFree(pidl);
                    }
                } else {
                    try {
                        CFileDialog dlg(TRUE, nullptr, nullptr,
                                        OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER,
                                        pmui::image_dialog_filter());
                        dlg.SetTitle(L"Add files to chat context");
                        if (dlg.DoModal(*this) == IDOK) {
                            std::vector<std::wstring> files;
                            int                       pos = 0;
                            CString                   path = dlg.GetNextPathName(pos);
                            while (!path.IsEmpty()) {
                                files.emplace_back(path.c_str());
                                if (pos < 0) break;
                                path = dlg.GetNextPathName(pos);
                            }
                            if (!files.empty())
                                m_impl->AppendContextPathsFromShell(std::move(files));
                        }
                    } catch (...) {
                        /* CFileDialog can throw CWinException on CommDlg failure */
                    }
                }
                return 0;
            } else if (kind == "removeContextPaths") {
                if (j.contains("paths") && j["paths"].is_array()) {
                    for (const auto& el : j["paths"]) {
                        if (!el.is_string()) continue;
                        std::wstring p = pmui::utf8_to_wide(el.get<std::string>());
                        while (!p.empty() && (p.back() == L' ' || p.back() == L'\t' || p.back() == L'\n' || p.back() == L'\r'))
                            p.pop_back();
                        size_t s0 = 0;
                        while (s0 < p.size() && (p[s0] == L' ' || p[s0] == L'\t')) ++s0;
                        if (s0) p.erase(0, s0);
                        if (p.empty()) continue;
                        m_impl->context_extra.erase(
                            std::remove_if(m_impl->context_extra.begin(), m_impl->context_extra.end(),
                                           [&](const std::wstring& x) { return ::_wcsicmp(x.c_str(), p.c_str()) == 0; }),
                            m_impl->context_extra.end());
                        if (!path_in_list_ci(p, m_impl->context_removed))
                            m_impl->context_removed.push_back(std::move(p));
                    }
                    m_impl->FlushContextToWeb();
                }
            } else if (kind == "openPathDefault" || kind == "openFolderInExplorer"
                       || kind == "selectPathInExplorer" || kind == "openPathInternal") {
                if (m_impl->busManager)
                    m_impl->busManager->HandleExternalMessage("chat", rawUtf8);
                else
                    logger::warn("[chat-web] shared host command dropped (bus not wired): " + kind);
                return 0;
            } else if (kind == "openPathFullscreen") {
                std::vector<std::wstring> paths;
                auto trim_path = [](std::wstring p) {
                    while (!p.empty() && (p.back() == L' ' || p.back() == L'\t' || p.back() == L'\n' || p.back() == L'\r'))
                        p.pop_back();
                    size_t s0 = 0;
                    while (s0 < p.size() && (p[s0] == L' ' || p[s0] == L'\t')) ++s0;
                    if (s0) p.erase(0, s0);
                    return p;
                };
                auto path_allowed = [](const std::wstring& p, const char* tag) {
                    const std::string deny = media::llm::llm_fs_guard_deny_reason(
                        std::filesystem::path(p));
                    if (!deny.empty()) {
                        logger::warn(std::string("[chat-web] openPathFullscreen blocked ")
                            + tag + ": " + deny + " path='" + pmui::wide_to_utf8(p) + "'");
                        return false;
                    }
                    return true;
                };
                std::wstring requestedPath;
                if (j.contains("paths") && j["paths"].is_array()) {
                    paths.reserve(j["paths"].size());
                    for (const auto& el : j["paths"]) {
                        if (!el.is_string()) continue;
                        std::wstring p = trim_path(pmui::utf8_to_wide(el.get<std::string>()));
                        if (!p.empty() && path_allowed(p, "list path")) paths.push_back(std::move(p));
                    }
                }
                if (j.contains("path") && j["path"].is_string()) {
                    requestedPath = trim_path(pmui::utf8_to_wide(j["path"].get<std::string>()));
                    if (!requestedPath.empty() && !path_allowed(requestedPath, "requested path"))
                        return 0;
                    if (paths.empty() && !requestedPath.empty())
                        paths.push_back(requestedPath);
                }
                if (!paths.empty()) {
                    size_t idx = 0;
                    if (j.contains("index")) {
                        if (j["index"].is_number_unsigned())
                            idx = static_cast<size_t>(j["index"].get<std::uint64_t>());
                        else if (j["index"].is_number_integer()) {
                            const auto iv = j["index"].get<std::int64_t>();
                            if (iv > 0)
                                idx = static_cast<size_t>(iv);
                        }
                    }
                    if (!requestedPath.empty()) {
                        auto it = std::find_if(paths.begin(), paths.end(),
                                               [&](const std::wstring& p) {
                                                   return ::_wcsicmp(p.c_str(), requestedPath.c_str()) == 0;
                                               });
                        if (it == paths.end()) {
                            paths.push_back(requestedPath);
                            idx = paths.size() - 1;
                        } else {
                            idx = static_cast<size_t>(std::distance(paths.begin(), it));
                        }
                    }
                    if (idx >= paths.size()) idx = 0;
                    bool constrainFrame = false;
                    if (j.contains("constrainToMainFrame") && j["constrainToMainFrame"].is_boolean())
                        constrainFrame = j["constrainToMainFrame"].get<bool>();
                    pmui::chat_image_fullscreen_show(GetHwnd(), std::move(paths), idx, constrainFrame);
                }
                return 0;
            } else if (kind == "providerRpc") {
                const std::string rpcMethod = j.value("method", std::string{});
                if (rpcMethod == "listChatSessions" || rpcMethod == "loadChatSession"
                    || rpcMethod == "saveChatSession" || rpcMethod == "deleteChatSession") {
                    if (m_impl->busManager)
                        m_impl->busManager->HandleExternalMessage("chat", rawUtf8);
                    else
                        logger::warn("[chat-web] shared providerRpc dropped (bus not wired): " + rpcMethod);
                    return 0;
                }
                nlohmann::json res;
                try {
                    res = provider_rpc_dispatch(j);
                } catch (const std::exception& e) {
                    res = nlohmann::json{{"ok", false}, {"error", e.what()}};
                }
                nlohmann::json out;
                out["kind"] = "hostProviderRpc";
                for (auto it = res.begin(); it != res.end(); ++it) out[it.key()] = it.value();
                // After merging `res`, set correlation id last so a stray `id` in `res` cannot win.
                if (j.contains("rpcId"))
                    out["id"] = j["rpcId"];
                else if (j.contains("id"))
                    out["id"] = j["id"];
                if (m_impl->webview) {
                    m_impl->webview->PostWebMessageAsString(pmui::utf8_to_wide(out.dump()).c_str());
                }
                return 0;
            } else if (kind == "toggleFileTree" || kind == "settings") {
                if (m_impl->busManager)
                    m_impl->busManager->HandleExternalMessage("chat", rawUtf8);
                else
                    logger::warn("[chat-web] shared host command dropped (bus not wired): " + kind);
            }
            else if (kind == "stop") {
                if (m_impl->cancel) m_impl->cancel->store(true);
                // `run_turn` only observes `media::cli::cancel_requested()` (Ctrl+C / tools),
                // not the callback return value; mirror the UI stop button onto that flag.
                media::cli::test_request_cancel();
                // Also abort any in-progress TTS / shell child, stop the scheduler loop,
                // and wipe all scheduled tasks + their memory so the agent starts clean.
                media::llm::path::abort_active_speak();
                media::llm::path::abort_active_run();
                media::llm::agent::global_scheduler().stop();
                media::llm::agent::task_store_clear_all();
                media::llm::agent::global_scheduler().start();
#if defined(FEATURE_STT) && FEATURE_STT
                // Stop any audio file playback from audioPlay IPC.
                std::lock_guard<std::mutex> lk(m_impl->audio_play_mu);
                if (m_impl->audio_play_out) {
                    m_impl->audio_play_out->stop();
                }
#endif
            }
#if defined(FEATURE_STT) && FEATURE_STT
            else if (kind == "sttStart") {
                if (m_impl->stt_active.load()) return 0; // already running

                // Resolve ElevenLabs API key: explicit in message → providers["elevenlabs"] → env var.
                std::string el_key = j.value("api_key", std::string{});
                if (el_key.empty()) {
                    std::string perr;
                    media::settings::ProviderMap pmap;
                    if (media::settings::load_providers(pmap, perr)) {
                        auto it = pmap.find("elevenlabs");
                        if (it != pmap.end()) el_key = it->second.api_key;
                    }
                }
                if (el_key.empty()) {
                    if (const char* env = std::getenv("ELEVENLABS_API_KEY")) el_key = env;
                }
                if (el_key.empty()) {
                    m_impl->RunJsNow(L"if(window.pmChat)window.pmChat.sttError('No ElevenLabs API key. Set it in App Settings \u2192 ElevenLabs.');");
                    return 0;
                }

                m_impl->stt_active.store(true);
                m_impl->stt_last_committed.clear();
                m_impl->stt_session = std::make_unique<pm::stt::ElevenLabsSTT>();
                m_impl->stt_mic     = std::make_unique<pm::audio::AudioInput>();

                Impl* impl  = m_impl.get();

                // Helper: post a JS call from any thread via the existing EnqueueScript path.
                auto post_js = [impl](const std::wstring& js) {
                    impl->EnqueueScript(js);
                };

                impl->stt_session->on_partial = [impl, post_js](const std::string& t) {
                    if (!impl->stt_active.load()) return;
                    post_js(L"if(window.pmChat)window.pmChat.sttPartial("
                        + pmui::utf8_to_wide(nlohmann::json(t).dump()) + L");");
                };

                impl->stt_session->on_committed = [impl, post_js](const std::string& t) {
                    if (t.empty() || t == impl->stt_last_committed) return;
                    std::string seg;
                    if (t.size() > impl->stt_last_committed.size() &&
                        t.compare(0, impl->stt_last_committed.size(), impl->stt_last_committed) == 0) {
                        seg = t.substr(impl->stt_last_committed.size());
                        const auto p = seg.find_first_not_of(" \t");
                        if (p != std::string::npos && p > 0) seg = seg.substr(p);
                    } else {
                        seg = t; // correction / transcript reset
                    }
                    impl->stt_last_committed = t;
                    if (seg.empty()) return;
                    post_js(L"if(window.pmChat)window.pmChat.sttCommitted("
                        + pmui::utf8_to_wide(nlohmann::json(seg).dump()) + L");");
                };

                impl->stt_session->on_error = [post_js](const std::string& e) {
                    post_js(L"if(window.pmChat)window.pmChat.sttError("
                        + pmui::utf8_to_wide(nlohmann::json(e).dump()) + L");");
                };

                impl->stt_session->on_close = [post_js]() {
                    post_js(L"if(window.pmChat)window.pmChat.sttDone();");
                };

                // Connect + capture on a background thread so the UI stays responsive.
                std::string el_key_copy = el_key;
                std::thread([impl, el_key_copy, post_js]() mutable {
                    // ── Connect ───────────────────────────────────────────────
                    pm::stt::ElevenLabsSTT::Config scfg;
                    scfg.api_key     = el_key_copy;
                    scfg.sample_rate = 16000;
                    try {
                        impl->stt_session->connect(scfg);
                    } catch (const std::exception& ex) {
                        impl->stt_active.store(false);
                        post_js(L"if(window.pmChat)window.pmChat.sttError("
                            + pmui::utf8_to_wide(nlohmann::json(std::string(ex.what())).dump()) + L");");
                        return;
                    }
                    if (!impl->stt_active.load()) return;

                    post_js(L"if(window.pmChat)window.pmChat.sttReady();");

                    // ── VAD state (audio callback thread only) ────────────────
                    const double k_vad_rms    = 200.0;
                    const int    k_silence_ms = 1500;
                    bool   vad_has_speech = false;
                    bool   vad_committed  = false;
                    using  clk = std::chrono::steady_clock;
                    auto   vad_last = clk::now();
                    auto   level_last = clk::now() - std::chrono::milliseconds(100);

                    auto on_pcm = [impl, k_vad_rms, k_silence_ms,
                                   &vad_has_speech, &vad_committed, &vad_last, &level_last, post_js]
                                  (const int16_t* data, size_t frames) {
                        if (!impl->stt_active.load()) return;
                        impl->stt_session->send_pcm(data, frames);
                        if (frames == 0) return;
                        double sum = 0.0;
                        for (size_t i = 0; i < frames; ++i)
                            sum += static_cast<double>(data[i]) * data[i];
                        const double rms = std::sqrt(sum / static_cast<double>(frames));
                        const auto   now = clk::now();
                        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - level_last).count() >= 80) {
                            level_last = now;
                            const double level = std::min(1.0, rms / 2500.0);
                            post_js(L"if(window.pmChat&&window.pmChat.sttLevel)window.pmChat.sttLevel("
                                + pmui::utf8_to_wide(nlohmann::json(level).dump()) + L");");
                        }
                        if (rms >= k_vad_rms) {
                            vad_last = now;
                            if (!vad_has_speech) { vad_has_speech = true; vad_committed = false; }
                        } else if (vad_has_speech && !vad_committed) {
                            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now - vad_last).count();
                            if (ms >= k_silence_ms) {
                                vad_committed = true; vad_has_speech = false;
                                impl->stt_session->commit();
                            }
                        }
                    };

                    // ── Start mic ─────────────────────────────────────────────
                    try { impl->stt_mic->start(on_pcm, ""); }
                    catch (const std::exception& ex) {
                        impl->stt_active.store(false);
                        impl->stt_session->close();
                        post_js(L"if(window.pmChat)window.pmChat.sttError("
                            + pmui::utf8_to_wide(nlohmann::json(std::string(ex.what())).dump()) + L");");
                        return;
                    }

                    // ── Wait until sttStop clears the flag ────────────────────
                    while (impl->stt_active.load())
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));

                    impl->stt_mic->stop();
                    impl->stt_session->commit(); // flush any pending partial
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                    impl->stt_session->close();   // on_close fires sttDone
                }).detach();
            }
            else if (kind == "sttStop") {
                m_impl->stt_active.store(false); // background thread sees this and shuts down
            }
            else if (kind == "audioPlay") {
                const std::string path_str = j.value("path", std::string{});
                if (path_str.empty()) return 0;

                Impl* impl = m_impl.get();
                std::thread([impl, path_str]() {
                    // Resolve path: absolute as-is, relative from process cwd.
                    fs::path p(pmui::utf8_to_wide(path_str));
                    if (p.is_relative()) p = fs::current_path() / p;

                    // Read file into memory.
                    std::ifstream f(p, std::ios::binary);
                    if (!f) return;
                    const std::vector<uint8_t> data(
                        (std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>{});
                    if (data.empty()) return;

                    // Reuse a single AudioOutput instance for all UI playback.
                    // Creating a new device per clip causes cumulative latency drift.
                    {
                        std::lock_guard<std::mutex> lk(impl->audio_play_mu);
                        if (!impl->audio_play_out) {
                            impl->audio_play_out = std::make_shared<pm::audio::AudioOutput>();
                        } else {
                            impl->audio_play_out->stop(); // stop any previous playback
                        }
                    }

                    try { impl->audio_play_out->play_sync(data.data(), data.size()); }
                    catch (...) {}
                }).detach();
            }
            else if (kind == "audioStop") {
                std::lock_guard<std::mutex> lk(m_impl->audio_play_mu);
                if (m_impl->audio_play_out) {
                    m_impl->audio_play_out->stop();
                    // Keep the instance alive for reuse; don't reset() here.
                }
            }
            else if (kind == "speakStop") {
                media::llm::path::abort_active_speak();
            }
#endif // FEATURE_STT
#if defined(FEATURE_VIDEO) && FEATURE_VIDEO
            else if (kind == "videoSnapshot") {
                if (m_impl->video_snap_busy.exchange(true)) return 0; // already in progress

                Impl* impl = m_impl.get();
                auto post_js = [impl](const std::wstring& js) {
                    impl->EnqueueScript(js);
                };

                std::thread([impl, post_js]() {
                    // Build a unique temp path: %TEMP%\pm-webcam-YYYYMMDD-HHMMSS-mmm.jpg
                    wchar_t tmpDir[MAX_PATH + 2];
                    GetTempPathW(MAX_PATH, tmpDir);

                    SYSTEMTIME st{};
                    GetLocalTime(&st);
                    wchar_t fname[64];
                    swprintf_s(fname, L"pm-webcam-%04u%02u%02u-%02u%02u%02u-%03u.jpg",
                               st.wYear, st.wMonth, st.wDay,
                               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
                    const std::wstring wpath = std::wstring(tmpDir) + fname;
                    const std::string  path  = pmui::wide_to_utf8(wpath);

                    // Capture: highest resolution → let score-based picker choose
                    // (width=0, height=0 → area-tiebreak picks largest native mode).
                    pm::video::VideoFrame frame;
                    try {
                        frame = pm::video::capture_still("", 0, 0, 6000);
                    } catch (const std::exception& ex) {
                        impl->video_snap_busy.store(false);
                        post_js(L"if(window.pmChat)window.pmChat.videoSnapshotError("
                            + pmui::utf8_to_wide(nlohmann::json(std::string(ex.what())).dump())
                            + L");");
                        return;
                    }

                    std::string err;
                    if (!pm::video::save_frame(frame, path, err, 90)) {
                        impl->video_snap_busy.store(false);
                        post_js(L"if(window.pmChat)window.pmChat.videoSnapshotError("
                            + pmui::utf8_to_wide(nlohmann::json(err).dump()) + L");");
                        return;
                    }

                    impl->video_snap_busy.store(false);
                    post_js(L"if(window.pmChat)window.pmChat.videoSnapshotDone("
                        + pmui::utf8_to_wide(nlohmann::json(path).dump()) + L");");
                }).detach();
            }
#endif // FEATURE_VIDEO
            else if (kind == "clear") {
                // No-op on the host side: the JS already cleared its transcript.
            }
            else if (kind == "send") {
                if (m_impl->busy) return 0;
                const std::string prompt = j.value("prompt", std::string{});
                if (prompt.empty()) return 0;

                // Pull fresh selection from the frame BEFORE starting the
                // worker (matches the native panel's behaviour).
                if (HWND owner = ::GetAncestor(GetHwnd(), GA_ROOT); owner) {
                    ::SendMessageW(owner, UWM_CHAT_REQUEST_CONTEXT, 0, 0);
                }

                // Clean up any prior worker.
                if (m_impl->cancel) m_impl->cancel->store(true);
                if (m_impl->worker.joinable()) m_impl->worker.join();
                media::cli::test_clear_cancel();
                m_impl->cancel = std::make_shared<std::atomic<bool>>(false);
                m_impl->busy   = true;
                m_impl->transformFileProgressTally = 0;

                // setBusy(true) on the JS side so Send disables / Stop enables.
                m_impl->RunJsNow(L"if(window.pmChat)window.pmChat.setBusy(true);");

                // Snapshot context for the worker thread (Explorer + any paths added via web DnD).
                const auto combinedSel =
                    merge_paths_for_web_chat(m_impl->selection, m_impl->context_extra, m_impl->context_removed);
                std::vector<std::string> selection_utf8;
                selection_utf8.reserve(combinedSel.size());
                for (const auto& w : combinedSel) selection_utf8.push_back(pmui::wide_to_utf8(w));
                const std::string folder_utf8 = pmui::wide_to_utf8(m_impl->folder);

                media::llm::agent::Turn turn;
                turn.user_prompt = prompt;
                turn.selection   = std::move(selection_utf8);
                turn.folder_hint = folder_utf8;
                turn.mcp_tools_enabled = j.value("mcp_tools_enabled", true);
                if (j.contains("disabled_mcp_servers") && j["disabled_mcp_servers"].is_array()) {
                    for (const auto& el : j["disabled_mcp_servers"]) {
                        if (el.is_string()) {
                            const std::string srv = el.get<std::string>();
                            if (!srv.empty()) turn.disabled_mcp_servers.push_back(srv);
                        }
                    }
                }
                if (j.contains("disable_tools") && j["disable_tools"].is_array()) {
                    for (const auto& el : j["disable_tools"]) {
                        if (!el.is_string()) continue;
                        std::string t = el.get<std::string>();
                        while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\n' || t.back() == '\r'))
                            t.pop_back();
                        size_t s0 = 0;
                        while (s0 < t.size() && (t[s0] == ' ' || t[s0] == '\t')) ++s0;
                        if (s0) t.erase(0, s0);
                        if (!t.empty()) turn.disabled_path_tools.push_back(std::move(t));
                    }
                }
                {
                    std::string gerr;
                    media::runtime_settings::apply_global_tool_policy_to_values(
                        turn.disabled_path_tools, turn.mcp_tools_enabled, turn.disabled_mcp_servers, gerr);
                    if (!gerr.empty())
                        logger::warn("[chat-web] global tool policy load: " + gerr);
                }
                if (j.contains("create_video_ui") && j["create_video_ui"].is_object())
                    turn.create_video_ui_overrides = j["create_video_ui"];

                auto prov = make_provider_for_turn(&j);
                if (prov.api_key.empty()) {
                    AppendTranscript(L"error",
                        L"\u26A0 No API key for this provider. Set it under App Settings \u2192 AI Provider Keys (not in the chat settings block).");
                    m_impl->busy = false;
                    m_impl->RunJsNow(L"if(window.pmChat)window.pmChat.setBusy(false);");
                    return 0;
                }

                // Ensure a session memory container exists for this chat panel.
                // Created lazily on the first turn; reused for all subsequent turns.
                if (m_impl->session_task_id.empty())
                    m_impl->session_task_id = media::llm::agent::session_create();

                // Snapshot the session_task_id so the worker closure owns a stable copy.
                const std::string session_task_id = m_impl->session_task_id;

                // Populate turn with session memory (memory_state + recent past turns).
                // is_scheduled_tick stays false so build_system_prompt uses the session branch.
                {
                    nlohmann::json mem_state, recent_evs;
                    media::llm::agent::session_load(session_task_id, mem_state, recent_evs, 20);
                    turn.task_id      = session_task_id;
                    turn.memory_state = std::move(mem_state);
                    turn.recent_events = std::move(recent_evs);
                    // is_scheduled_tick = false (default) — selects the session memory prompt branch.
                }

                HWND target  = GetHwnd();
                HWND frameTarget = ::GetAncestor(GetHwnd(), GA_ROOT);
                auto cancel  = m_impl->cancel;
                Impl* impl   = m_impl.get();

                // Compute agent-HH-MM.json path now (same directory as pm-image.log) so the
                // worker thread has a stable snapshot of the override at turn start.
                const std::filesystem::path agent_log_path = [&]() {
                    std::time_t t = std::time(nullptr);
                    std::tm tm_buf{};
                    localtime_s(&tm_buf, &t);
                    char buf[32];
                    std::strftime(buf, sizeof(buf), "agent-%H-%M.json", &tm_buf);
                    return pmui::get_pm_image_log_file_directory() / buf;
                }();

                // Spin the worker. `media::llm::agent::run_turn` — same as CLI `llm agent`:
                // `build_system_prompt(turn)` + `path::tool_catalog_openai()` and path tools.
                m_impl->worker = std::thread([turn, prov, target, frameTarget, cancel, impl, agent_log_path, session_task_id]() {
                    // ── Log-panel helper (same ownership contract as the outer plog) ─────
                    auto plog = [frameTarget](const std::wstring& s) {
                        if (!frameTarget) return;
                        auto* buf = new wchar_t[s.size() + 1];
                        std::wmemcpy(buf, s.c_str(), s.size() + 1);
                        if (!::PostMessageW(frameTarget, UWM_LOG_MESSAGE,
                                            reinterpret_cast<WPARAM>(buf), 0))
                            delete[] buf;
                    };

                    // ── Media / speech trackers for auto-memory ──────────────────────
                    std::vector<std::string> turn_tool_names;
                    // TTS: text passed to each `speak` call (concatenated if multiple).
                    std::vector<std::string> turn_spoken_texts;
                    // Images/video/camera: output file paths produced by media tools.
                    std::vector<std::string> turn_media_outputs;

                    // ── Agent JSON event accumulator (mirrors CLI llm agent) ──────────
                    nlohmann::json log_events = nlohmann::json::array();
                    auto agent_append_log_event = [](nlohmann::json& events,
                                                     const media::llm::agent::Event& e) {
                        nlohmann::json j;
                        using EK = media::llm::agent::Event::Kind;
                        switch (e.kind) {
                        case EK::TurnStarted:      j["kind"] = "turn_started";      j["detail"] = e.payload; break;
                        case EK::LlmRound:         j["kind"] = "llm_round";         j["detail"] = e.payload; break;
                        case EK::ToolCall:         j["kind"] = "tool_call";         j["tool"] = e.tool_name; j["detail"] = e.payload; break;
                        case EK::ToolResult:       j["kind"] = "tool_result";       j["tool"] = e.tool_name; j["detail"] = e.payload; break;
                        case EK::ToolFileProgress: j["kind"] = "tool_file_progress"; j["tool"] = e.tool_name; j["detail"] = e.payload; break;
                        case EK::Thinking:         j["kind"] = "thinking";          j["text"] = e.text; break;
                        case EK::AssistantText:    j["kind"] = "assistant_text";    j["text"] = e.text; break;
                        case EK::Error:            j["kind"] = "error";             j["text"] = e.text; break;
                        case EK::Done:             j["kind"] = "done";              j["text"] = e.text; break;
                        default:                   j["kind"] = "unknown"; break;
                        }
                        events.push_back(std::move(j));
                    };

                    // Compact one-liner for tool arguments (transcript bubble + log hint).
                    auto args_summary = [](const nlohmann::json& args) -> std::wstring {
                        std::ostringstream ss;
                        namespace fs2 = std::filesystem;
                        auto first_str = [](const nlohmann::json& arr) -> std::string {
                            if (arr.is_array() && !arr.empty() && arr[0].is_string())
                                return arr[0].get<std::string>();
                            return {};
                        };
                        if (args.contains("paths") && args["paths"].is_array()) {
                            const auto& pa = args["paths"];
                            ss << pa.size() << " file(s)";
                            auto f = first_str(pa);
                            if (!f.empty()) ss << ": " << fs2::path(f).filename().string();
                            if (pa.size() > 1) ss << " +" << (pa.size()-1) << " more";
                        } else if (args.contains("inputs") && args["inputs"].is_array()) {
                            const auto& ia = args["inputs"];
                            ss << ia.size() << " input(s)";
                            auto f = first_str(ia);
                            if (!f.empty()) ss << ": " << f;
                        } else if (args.contains("path") && args["path"].is_string()) {
                            ss << args["path"].get<std::string>();
                            if (args.contains("content") && args["content"].is_string())
                                ss << " (" << args["content"].get<std::string>().size() << " B)";
                        }
                        const auto& opts = args.contains("options") && args["options"].is_object()
                                           ? args["options"] : nlohmann::json::object();
                        if (opts.contains("prompt") && opts["prompt"].is_string()) {
                            std::string p = opts["prompt"].get<std::string>();
                            if (p.size() > 55)
                                p = utf8_safe_prefix(p, 52) + "...";
                            ss << " \u00BB\"" << p << "\"";
                        }
                        std::string s = ss.str();
                        if (s.empty())
                            s = utf8_safe_prefix(args.dump(), 80);
                        return pmui::utf8_to_wide(s);
                    };
                    // Full args JSON for Log panel (same idea as `llm agent` stderr and native ChatPanel).
                    auto args_dump_for_log = [](const nlohmann::json& args) -> std::wstring {
                        std::string s = args.dump();
                        if (s.size() > 12000)
                            s = utf8_safe_prefix(s, 12000) + "...";
                        return pmui::utf8_to_wide(s);
                    };

                    {
                        std::wostringstream s;
                        s << L"[chat] Sending: router=" << pmui::utf8_to_wide(prov.router)
                          << L" model=" << pmui::utf8_to_wide(prov.model)
                          << L" sel=" << turn.selection.size();
                        if (!prov.base_url.empty())
                            s << L" base_url=" << pmui::utf8_to_wide(prov.base_url);
                        if (!turn.selection.empty()) {
                            s << L" ("
                              << pmui::utf8_to_wide(fs::path(turn.selection[0]).filename().string());
                            if (turn.selection.size() > 1)
                                s << L" +" << std::to_wstring(turn.selection.size() - 1);
                            s << L")";
                        } else if (!turn.folder_hint.empty()) {
                            s << L" folder=" << pmui::utf8_to_wide(turn.folder_hint);
                        }
                        plog(s.str());
                    }
                    {
                        const std::string& p = turn.user_prompt;
                        const std::string clipped = p.size() > 200 ? utf8_safe_prefix(p, 200) + "…" : p;
                        std::wstring prev = pmui::utf8_to_wide(clipped);
                        plog(L"[chat] Prompt: " + prev);
                    }

                    using K = media::llm::agent::Event::Kind;
                    auto cb = [target, impl, cancel, frameTarget, &plog, &args_summary, &args_dump_for_log,
                               &log_events, &agent_append_log_event,
                               &turn_tool_names, &turn_spoken_texts, &turn_media_outputs]
                              (const media::llm::agent::Event& e) -> bool {
                        agent_append_log_event(log_events, e);
                        switch (e.kind) {
                            case K::TurnStarted: {
                                std::string folder_str;
                                try { folder_str = e.payload.value("folder", std::string{}); } catch (...) {}
                                if (!folder_str.empty()) {
                                    plog(L"[chat] Turn started \u2014 folder=" + pmui::utf8_to_wide(folder_str));
                                    // Show scoped folder in chat so the user knows what the agent operates on.
                                    impl->EnqueueScript(
                                        L"if(window.pmChat&&window.pmChat.setRunFolder)"
                                        L"window.pmChat.setRunFolder("
                                        + pmui::utf8_to_wide(nlohmann::json(folder_str).dump()) + L");");
                                } else {
                                    plog(L"[chat] Turn started \u2014 contacting LLM\u2026");
                                }
                                break;
                            }
                            case K::ToolCall: {
                                // Collect tool name for auto-memory.
                                if (!e.tool_name.empty())
                                    turn_tool_names.push_back(e.tool_name);
                                // Capture spoken text for TTS auto-memory.
                                if (e.tool_name == "speak") {
                                    try {
                                        const auto& args = e.payload.value("arguments", nlohmann::json{});
                                        std::string txt;
                                        if (args.contains("text") && args["text"].is_string())
                                            txt = args["text"].get<std::string>();
                                        else if (args.is_string())
                                            txt = args.get<std::string>();
                                        if (!txt.empty())
                                            turn_spoken_texts.push_back(std::move(txt));
                                    } catch (...) {}
                                }
                                const auto& args = e.payload.value("arguments", nlohmann::json{});
                                const std::wstring summary = args_summary(args);
                                // Chat UI bubble: short line (not full tool JSON)
                                {
                                    std::string text = "\u26A1 " + e.tool_name + " ";
                                    text += summary.empty() ? utf8_safe_prefix(args.dump(), 200)
                                                            : pmui::wide_to_utf8(summary);
                                    impl->EnqueueScript(L"if(window.pmChat)window.pmChat.appendText({role:'tool',text:" +
                                                         js_quote(pmui::utf8_to_wide(text)) + L"});");
                                }
                                // Log panel — same shape as native ChatPanel / CLI `llm agent`
                                plog(L"[chat] Tool call: " + pmui::utf8_to_wide(e.tool_name) + L" args="
                                     + args_dump_for_log(args));
                                {
                                    std::wstring qn  = L"\u26A1 " + pmui::utf8_to_wide(e.tool_name);
                                    if (qn.size() > 128) qn.resize(128);
                                    std::wstring qop = pmui::utf8_to_wide(e.tool_name);
                                    std::wstring qs  = L"call";
                                    if (!summary.empty()) {
                                        qs += L" \u00B7 ";
                                        qs += summary;
                                    }
                                    if (qs.size() > 500) qs.resize(500);
                                    (void)pmui::PmPostQueueToolCallRow(frameTarget, std::move(qn), std::move(qop), std::move(qs), std::wstring{});
                                }
                                break;
                            }
                            case K::ToolFileProgress: {
                                // Run tool: stream stdout/stderr chunks to the chat UI.
                                if (e.tool_name == "run") {
                                    try {
                                        const std::string run_id = e.payload.value("id", std::string{});
                                        const std::string evt    = e.payload.value("event", std::string{});

                                        if (evt == "start" && !run_id.empty()) {
                                            const std::string cmd = e.payload.value("command", std::string{});
                                            impl->EnqueueScript(
                                                L"if(window.pmChat&&window.pmChat.startRun)"
                                                L"window.pmChat.startRun("
                                                + pmui::utf8_to_wide(nlohmann::json(run_id).dump()) + L","
                                                + pmui::utf8_to_wide(nlohmann::json(cmd).dump()) + L");");
                                        } else if (evt == "done" && !run_id.empty()) {
                                            const int exit_code = e.payload.value("exit_code", -1);
                                            const auto dur_ms   = e.payload.value("duration_ms", int64_t{0});
                                            impl->EnqueueScript(
                                                L"if(window.pmChat&&window.pmChat.finishRun)"
                                                L"window.pmChat.finishRun("
                                                + pmui::utf8_to_wide(nlohmann::json(run_id).dump()) + L","
                                                + pmui::utf8_to_wide(nlohmann::json(exit_code).dump()) + L","
                                                + pmui::utf8_to_wide(nlohmann::json(dur_ms).dump()) + L");");
                                        } else {
                                            const std::string stream = e.payload.value("stream", std::string{"stdout"});
                                            const std::string chunk  = e.payload.value("chunk", std::string{});
                                            if (!chunk.empty() && !run_id.empty()) {
                                                impl->EnqueueScript(
                                                    L"if(window.pmChat&&window.pmChat.appendRunChunk)"
                                                    L"window.pmChat.appendRunChunk("
                                                    + pmui::utf8_to_wide(nlohmann::json(run_id).dump()) + L","
                                                    + pmui::utf8_to_wide(nlohmann::json(chunk).dump()) + L","
                                                    + pmui::utf8_to_wide(nlohmann::json(stream).dump()) + L");");
                                            }
                                        }
                                    } catch (...) {}
                                    break;
                                }
                                // One `image_transform` input finished — inline chat preview; the
                                // file queue is updated from `ToolResult` (covers multi-file and
                                // any case where per-file progress did not run).
                                std::vector<std::pair<std::string, std::string>> src_out;
                                try {
                                    auto env_j = e.payload.value("envelope", nlohmann::json{});
                                    for (const auto& r : env_j.value("results", nlohmann::json::array())) {
                                        if (r.value("ok", false) && r.contains("output_path")
                                            && r["output_path"].is_string()) {
                                            src_out.emplace_back(r.value("path", std::string{}),
                                                                 r["output_path"].get<std::string>());
                                        }
                                    }
                                } catch (...) {}
                                for (const auto& sp : src_out) {
                                    plog(L"[chat] Tool progress: " + pmui::utf8_to_wide(e.tool_name) + L"  \u2192 "
                                         + pmui::utf8_to_wide(
                                             std::filesystem::path(sp.second).filename().string()));
                                }
                                for (const auto& sp : src_out) {
                                    const auto& p = sp.second;
                                    std::string ext;
                                    auto        dot = p.rfind('.');
                                    if (dot != std::string::npos) ext = p.substr(dot + 1);
                                    for (auto& c : ext)
                                        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                                    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" || ext == "gif" || ext == "avif") {
                                        impl->EnqueueScript(
                                            L"if(window.pmChat)window.pmChat.appendText({role:'image',text:" +
                                            js_quote(pmui::utf8_to_wide(p)) + L"});");
                                    } else if (!ext.empty()) {
                                        impl->EnqueueScript(
                                            L"if(window.pmChat)window.pmChat.appendText({role:'file',text:" +
                                            js_quote(pmui::utf8_to_wide(p)) + L"});");
                                    }
                                }
                                if (!src_out.empty()) ++impl->transformFileProgressTally;
                                break;
                            }
                            case K::ToolResult: {
                                // Build a one-line tool summary. For image_transform /
                                // image_create, collect the (source, output_path) pairs so we can
                                // (a) render thumbnails inline in chat,
                                // (b) wire each output into the queue + GenPreview,
                                // (c) adopt the outputs as the implicit selection
                                //     for the next turn (image iteration: "now make it warmer").
                                std::string summary;
                                std::string replicate_web_url;
                                std::string tool_exec_provider;
                                std::string tool_exec_model;
                                std::vector<std::pair<std::string,std::string>> src_out;
                                try {
                                    auto env_j = e.payload.value("envelope", nlohmann::json{});
                                    auto sum_j = env_j.value("summary",  nlohmann::json{});
                                    const int total = sum_j.value("total",     0);
                                    const int ok    = sum_j.value("succeeded", 0);
                                    const int fail  = sum_j.value("failed",    0);
                                    summary = " \u2713 " + std::to_string(ok) + "/" + std::to_string(total);
                                    if (fail > 0) summary += " (" + std::to_string(fail) + " failed)";

                                    for (const auto& r : env_j.value("results", nlohmann::json::array())) {
                                        if (r.value("ok", false) && r.contains("output_path")
                                            && r["output_path"].is_string()) {
                                            src_out.emplace_back(r.value("path", std::string{}),
                                                                 r["output_path"].get<std::string>());
                                        }
                                    }
                                    // Single pass over results: collect replicate_web_url and
                                    // execution provider/model (stamped by image_understand etc.)
                                    for (const auto& r : env_j.value("results", nlohmann::json::array())) {
                                        if (!r.contains("result") || !r["result"].is_object())
                                            continue;
                                        const auto& res_o = r["result"];
                                        if (replicate_web_url.empty()
                                            && res_o.contains("replicate_web_url")
                                            && res_o["replicate_web_url"].is_string()) {
                                            replicate_web_url = res_o["replicate_web_url"].get<std::string>();
                                        }
                                        if (tool_exec_provider.empty()
                                            && res_o.contains("provider")
                                            && res_o["provider"].is_string()) {
                                            tool_exec_provider = res_o["provider"].get<std::string>();
                                        }
                                        if (tool_exec_model.empty()
                                            && res_o.contains("model")
                                            && res_o["model"].is_string()) {
                                            tool_exec_model = res_o["model"].get<std::string>();
                                        }
                                    }
                                } catch (...) { summary = " (done)"; }

                                // Auto-memory: capture output paths for media tools.
                                {
                                    static const char* k_media_tools[] = {
                                        "image_transform", "transform",
                                        "image_create", "create_video",
                                        "image_from_camera", "image_resize",
                                    };
                                    bool is_media = false;
                                    for (const auto* mt : k_media_tools)
                                        if (e.tool_name == mt) { is_media = true; break; }
                                    if (is_media) {
                                        for (const auto& sp : src_out)
                                            if (!sp.second.empty())
                                                turn_media_outputs.push_back(sp.second);
                                    }
                                }

                                const bool multi_transform_done =
                                    (e.tool_name == "image_transform" || e.tool_name == "transform")
                                    && impl->transformFileProgressTally > 0;

                                const auto tool_duration_ms_val = e.payload.value("duration_ms", std::int64_t{-1});
                                std::string text = "\u26A1 " + e.tool_name + summary;
                                if (!replicate_web_url.empty())
                                    text += " \u2014 Replicate: " + replicate_web_url;
                                {
                                    std::wstring append_extra;
                                    if (!tool_exec_provider.empty())
                                        append_extra += L",toolProvider:" + js_quote(pmui::utf8_to_wide(tool_exec_provider));
                                    if (!tool_exec_model.empty())
                                        append_extra += L",toolModel:" + js_quote(pmui::utf8_to_wide(tool_exec_model));
                                    if (tool_duration_ms_val >= 0)
                                        append_extra += L",durationMs:" + std::to_wstring(tool_duration_ms_val);
                                    impl->EnqueueScript(L"if(window.pmChat)window.pmChat.appendText({role:'tool',text:" +
                                                         js_quote(pmui::utf8_to_wide(text)) + append_extra + L"});");
                                }

                                // Log panel — same phrasing as native ChatPanel.
                                {
                                    std::wstring logline = L"[chat] Tool result: "
                                        + pmui::utf8_to_wide(e.tool_name)
                                        + pmui::utf8_to_wide(summary);
                                    if (!src_out.empty()) {
                                        logline += L"  \u2192";
                                        int shown = 0;
                                        for (const auto& sp : src_out) {
                                            if (shown++ > 2) { logline += L" +more"; break; }
                                            logline += L" " + pmui::utf8_to_wide(
                                                std::filesystem::path(sp.second).filename().string());
                                        }
                                    }
                                    if (!replicate_web_url.empty())
                                        logline += L"  Replicate " + pmui::utf8_to_wide(replicate_web_url);
                                    plog(logline);
                                    {
                                        std::wstring qn  = pmui::utf8_to_wide(e.tool_name);
                                        if (qn.size() > 100) qn.resize(100);
                                        std::wstring qop = pmui::utf8_to_wide(e.tool_name);
                                        std::wstring qs  = L"result" + pmui::utf8_to_wide(summary);
                                        if (qs.size() > 450) qs.resize(450);
                                        (void)pmui::PmPostQueueToolCallRow(frameTarget, std::move(qn), std::move(qop), std::move(qs), std::wstring{});
                                    }
                                    // Also log per-file errors if any.
                                    try {
                                        auto env_j = e.payload.value("envelope", nlohmann::json{});
                                        for (const auto& r : env_j.value("results", nlohmann::json::array())) {
                                            if (!r.value("ok", true)) {
                                                plog(L"[chat]   \u2717 "
                                                     + pmui::utf8_to_wide(r.value("path", std::string{}))
                                                     + L": "
                                                     + pmui::utf8_to_wide(r.value("error", std::string{})));
                                            }
                                        }
                                    } catch (...) {}
                                }

                                // image_find — same matches as ribbon Find, in the Find results dock.
                                if (e.tool_name == "image_find" && frameTarget) {
                                    try {
                                        auto env_if = e.payload.value("envelope", nlohmann::json{});
                                        const auto& rlist = env_if.value("results", nlohmann::json::array());
                                        if (!rlist.empty() && rlist[0].value("ok", false)) {
                                            const auto& inner = rlist[0].value("result", nlohmann::json::object());
                                            const auto& marr  = inner.value("matches", nlohmann::json::array());
                                            auto* batch = new std::vector<FindProgressRow>();
                                            batch->reserve(marr.size());
                                            for (const auto& mj : marr) {
                                                if (!mj.is_object() || !mj.contains("path")
                                                    || !mj["path"].is_string()) continue;
                                                FindProgressRow row;
                                                row.path   = pmui::utf8_to_wide(mj["path"].get<std::string>());
                                                row.score  = mj.value("score", 0.0);
                                                row.source = pmui::utf8_to_wide(mj.value("source", std::string{}));
                                                row.reason = pmui::utf8_to_wide(mj.value("reason", std::string{}));
                                                batch->push_back(std::move(row));
                                            }
                                            const int n = static_cast<int>(batch->size());
                                            if (!::PostMessageW(frameTarget, UWM_FIND_PROGRESS,
                                                                reinterpret_cast<WPARAM>(batch), 1)) {
                                                delete batch;
                                            } else {
                                                ::PostMessageW(frameTarget, UWM_FIND_DONE, (WPARAM)1, (LPARAM)n);
                                            }
                                        }
                                    } catch (...) { /* best-effort */ }
                                }

                                // For each output, render as an inline image (for
                                // image outputs) or a clickable file link (for
                                // write_file markdown / SVG / text outputs).
                                if (!multi_transform_done) {
                                for (const auto& sp : src_out) {
                                    const auto& p = sp.second;
                                    std::string ext;
                                    auto dot = p.rfind('.');
                                    if (dot != std::string::npos) ext = p.substr(dot + 1);
                                    for (auto& c : ext) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
                                    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" || ext == "gif" || ext == "avif") {
                                        impl->EnqueueScript(L"if(window.pmChat)window.pmChat.appendText({role:'image',text:" +
                                                             js_quote(pmui::utf8_to_wide(p)) + L"});");
                                    } else if (!ext.empty()) {
                                        // Non-image output (md, svg, txt, json…) — show as a
                                        // file link the user can click to open.
                                        impl->EnqueueScript(L"if(window.pmChat)window.pmChat.appendText({role:'file',text:" +
                                                             js_quote(pmui::utf8_to_wide(p)) + L"});");
                                    }
                                }
                                }

                                // Iteration + preview integration. Wire transform and
                                // create outputs (compress / meta / list_images / find
                                // aren't the same "generated raster to iterate" flow).
                                if ((e.tool_name == "image_transform" || e.tool_name == "transform"
                                     || e.tool_name == "image_create")
                                    && !src_out.empty()) {
                                    for (const auto& sp : src_out) {
                                        auto* pair = new std::pair<std::wstring, std::wstring>(
                                            pmui::utf8_to_wide(sp.first), pmui::utf8_to_wide(sp.second));
                                        // SendMessage: `OnGeneratedFile` on the UI thread so the
                                        // queue updates before the next tool / LLM round.
                                        ::SendMessageW(frameTarget, UWM_GENERATED_FILE,
                                                       reinterpret_cast<WPARAM>(pair), 0);
                                    }
                                    if (multi_transform_done)
                                        impl->transformFileProgressTally = 0;
                                    {
                                        auto* outs = new std::vector<std::wstring>();
                                        outs->reserve(src_out.size());
                                        for (const auto& sp : src_out) outs->push_back(pmui::utf8_to_wide(sp.second));
                                        if (!::PostMessageW(frameTarget, UWM_CHAT_GENERATED,
                                                            reinterpret_cast<WPARAM>(outs), 0)) {
                                            delete outs;
                                        }
                                    }
                                }

                                // File-writing tools: post UWM_TOOL_FILE_WRITTEN so the frame
                                // can reload the centre viewer if the written path is currently open.
                                if (e.tool_name == "write_file"
                                    && frameTarget && !src_out.empty()) {
                                    for (const auto& sp : src_out) {
                                        if (sp.second.empty()) continue;
                                        plog(L"[chat] viewer-reload: notify frame for "
                                             + pmui::utf8_to_wide(
                                                 std::filesystem::path(sp.second).filename().string()));
                                        auto* pw = new std::wstring(pmui::utf8_to_wide(sp.second));
                                        if (!::PostMessageW(frameTarget, UWM_TOOL_FILE_WRITTEN,
                                                            reinterpret_cast<WPARAM>(pw), 0))
                                            delete pw;
                                    }
                                }                                break;
                            }
                            case K::AssistantText: {
                                impl->EnqueueScript(L"if(window.pmChat)window.pmChat.appendText({role:'assistant',text:" +
                                                     js_quote(pmui::utf8_to_wide(e.text)) + L"});");
                                plog(L"[chat] Assistant text received ("
                                     + std::to_wstring(static_cast<int>(e.text.size())) + L" chars)");
                                {
                                    std::wstring preview = pmui::utf8_to_wide(
                                        e.text.size() > 120 ? e.text.substr(0, 117) + "..." : e.text);
                                    for (auto& c : preview) if (c == L'\n' || c == L'\r') c = L' ';
                                    if (!preview.empty()) plog(L"[chat] \u2726 " + preview);
                                }
                                break;
                            }
                            case K::Error:
                                impl->EnqueueScript(L"if(window.pmChat)window.pmChat.appendText({role:'error',text:" +
                                                     js_quote(pmui::utf8_to_wide(e.text)) + L"});");
                                plog(L"[chat] \u2717 ERROR: " + pmui::utf8_to_wide(e.text));
                                break;
                            case K::Done:
                                break;
                        }
                        return !(cancel && cancel->load());
                    };

                    auto res = media::llm::agent::run_turn(turn, prov, cb);

                    // ── Auto-memory: append a compact turn summary to the session store ──
                    if (pm::llm::k_auto_memory_enabled
                            && !session_task_id.empty()
                            && (res.ok || !res.final_text.empty())) {
                        try {
                            nlohmann::json entry = nlohmann::json::object();
                            entry["ts"] = media::llm::agent::now_iso8601();

                            // Truncate user prompt.
                            std::string u = turn.user_prompt;
                            if (u.size() > pm::llm::k_session_memory_user_chars) {
                                u.resize(pm::llm::k_session_memory_user_chars - 1);
                                u += "\u2026";
                            }
                            entry["user"] = std::move(u);

                            // Truncate assistant reply.
                            std::string a = res.final_text;
                            if (a.size() > pm::llm::k_session_memory_asst_chars) {
                                a.resize(pm::llm::k_session_memory_asst_chars - 1);
                                a += "\u2026";
                            }
                            if (!a.empty()) entry["assistant"] = std::move(a);

                            // Tool names (deduplicated, preserving first-seen order).
                            if (!turn_tool_names.empty()) {
                                nlohmann::json tools = nlohmann::json::array();
                                std::vector<std::string> seen;
                                for (const auto& tn : turn_tool_names) {
                                    bool dup = false;
                                    for (const auto& s : seen)
                                        if (s == tn) { dup = true; break; }
                                    if (!dup) { seen.push_back(tn); tools.push_back(tn); }
                                }
                                entry["tools"] = std::move(tools);
                            }

                            // TTS: spoken text(s).
                            if (!turn_spoken_texts.empty()) {
                                nlohmann::json spoken = nlohmann::json::array();
                                for (const auto& t : turn_spoken_texts) spoken.push_back(t);
                                entry["spoken"] = std::move(spoken);
                            }

                            // Media outputs: image / video / camera paths.
                            if (!turn_media_outputs.empty()) {
                                nlohmann::json media = nlohmann::json::array();
                                for (const auto& p : turn_media_outputs) media.push_back(p);
                                entry["media_outputs"] = std::move(media);
                            }

                            media::llm::agent::session_append_turn(session_task_id, entry);
                        } catch (...) {}
                    }

                    // ── Write agent.json next to pm-image.log ─────────────────────────
                    try {
                        auto agent_log_utc_now = []() -> std::string {
                            std::time_t t = std::time(nullptr);
                            std::tm tm_buf{};
                            if (gmtime_s(&tm_buf, &t) != 0) return {};
                            char buf[40];
                            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
                            return buf;
                        };

                        nlohmann::json result_j{
                            {"ok",         res.ok},
                            {"final_text", res.final_text},
                            {"iterations", res.iterations},
                            {"transcript", res.transcript},
                        };
                        if (res.cancelled) result_j["cancelled"] = true;
                        if (!res.error.empty()) result_j["error"] = res.error;
                        if (res.llm_usage_aggregate.is_object()) {
                            const int tt = res.llm_usage_aggregate.value("total_tokens", 0);
                            const int pr = res.llm_usage_aggregate.value("prompt_tokens", 0);
                            const int co = res.llm_usage_aggregate.value("completion_tokens", 0);
                            const auto rounds = res.llm_usage_aggregate.value("llm_rounds", nlohmann::json::array());
                            const bool has_cost = res.llm_usage_aggregate.contains("cost")
                                                  && res.llm_usage_aggregate["cost"].is_number();
                            if ((rounds.is_array() && !rounds.empty()) || tt > 0 || pr > 0 || co > 0 || has_cost)
                                result_j["llm_usage"] = res.llm_usage_aggregate;
                        }

                        nlohmann::json sel_j = nlohmann::json::array();
                        for (const auto& p : turn.selection) sel_j.push_back(p);

                        nlohmann::json logj{
                            {"schema_version", "1"},
                            {"generated_at",   agent_log_utc_now()},
                            {"source",         "chat_workbench"},
                            {"provider", {
                                {"router",            prov.router},
                                {"model",             prov.model},
                                {"base_url",          prov.base_url.empty()
                                    ? nlohmann::json(nullptr) : nlohmann::json(prov.base_url)},
                                {"timeout_ms",        prov.timeout_ms},
                                {"max_iterations",    prov.max_iterations},
                                {"api_key_configured", true},
                            }},
                            {"turn", {
                                {"user_prompt",         turn.user_prompt},
                                {"selection",           sel_j},
                                {"folder_hint",         turn.folder_hint},
                                {"disabled_path_tools", nlohmann::json(turn.disabled_path_tools)},
                            }},
                            {"events", std::move(log_events)},
                            {"result", std::move(result_j)},
                        };

                        std::ofstream f(agent_log_path, std::ios::out | std::ios::trunc);
                        if (f) {
                            f << logj.dump(2) << "\n";
                        } else {
                            plog(L"[chat] Warning: could not write agent.json to "
                                 + agent_log_path.wstring());
                        }
                    } catch (...) {}

                    if (res.llm_usage_aggregate.is_object()) {
                        const int tt = res.llm_usage_aggregate.value("total_tokens", 0);
                        const int pr = res.llm_usage_aggregate.value("prompt_tokens", 0);
                        const int co = res.llm_usage_aggregate.value("completion_tokens", 0);
                        const auto rounds = res.llm_usage_aggregate.value("llm_rounds", nlohmann::json::array());
                        const bool has_cost = res.llm_usage_aggregate.contains("cost")
                                                && res.llm_usage_aggregate["cost"].is_number();
                        const bool has_usage = (rounds.is_array() && !rounds.empty()) || tt > 0 || pr > 0 || co > 0
                                               || has_cost;
                        if (has_usage) {
                            const std::wstring ujson =
                                js_quote(pmui::utf8_to_wide(res.llm_usage_aggregate.dump()));
                            impl->EnqueueScript(
                                L"(function(){var u;try{u=JSON.parse(" + ujson
                                + L");}catch(e){return;}"
                                L"if(window.pmChat&&typeof window.pmChat.setTurnLlmUsage==='function')"
                                L"window.pmChat.setTurnLlmUsage(u);"
                                L"else if(window.pmChat)console.warn('[pm-image] pmChat.setTurnLlmUsage "
                                L"missing; rebuild: npm run build:chat-next:embed');})();");
                            std::wstring costw;
                            if (has_cost) {
                                try {
                                    const double c = res.llm_usage_aggregate["cost"].get<double>();
                                    costw = L" cost=" + pmui::utf8_to_wide(std::to_string(c));
                                } catch (...) {}
                            }
                            const std::wstring wline = L"[chat] LLM usage (aggregated): prompt="
                                + std::to_wstring(pr) + L" completion=" + std::to_wstring(co) + L" total="
                                + std::to_wstring(tt) + costw;
                            plog(wline);
                            logger::debug(pmui::wide_to_utf8(wline));
                        }
                    }

                    if (res.ok) {
                        plog(L"[chat] Turn done \u2014 iterations=" + std::to_wstring(res.iterations)
                             + L", final_text_chars=" + std::to_wstring((int)res.final_text.size()));
                    } else {
                        plog(L"[chat] Turn FAILED after " + std::to_wstring(res.iterations)
                             + L" iteration(s): " + pmui::utf8_to_wide(res.error));
                    }

                    auto* err = new std::wstring(res.ok ? L"" : pmui::utf8_to_wide(res.error));
                    if (!::PostMessageW(target, UWM_WEB_TURN_DONE,
                                        static_cast<WPARAM>(res.ok ? 1 : 0),
                                        reinterpret_cast<LPARAM>(err))) {
                        delete err;
                    }
                });
            }
            return 0;
        }
        case UWM_WEB_TURN_DONE: {
            auto* err = reinterpret_cast<std::wstring*>(lp);
            if (err) delete err;
            m_impl->busy = false;
            if (m_impl->ready) m_impl->RunJsNow(L"if(window.pmChat)window.pmChat.setBusy(false);");
            if (m_impl->worker.joinable()) m_impl->worker.detach();
            m_impl->cancel.reset();
            return 0;
        }
        }
        return WndProcDefault(msg, wp, lp);
    }
    catch (const CException& e) {
        CString s; s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

// ────────────────────────────────────────────────────────────────────────
// CChatWebContainer / CDockChatWeb
// ────────────────────────────────────────────────────────────────────────

void CChatWebContainer::PreCreate(CREATESTRUCT& cs)
{
    CDockContainer::PreCreate(cs);
    // Comctl tab + Win32++ container can still pick up sunken edge styles from
    // persisted layouts — those read as bright 2–4px rules around WebView2 in dark mode.
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    cs.style &= ~(WS_BORDER | WS_DLGFRAME);
}

void CChatWebContainer::RefreshTabTheme()
{
    if (!IsWindow()) return;
    const auto& pal = pmui::theme_palette();
    // Win32++ CTab::Paint fills the tab-strip rim with this; must match chat embed `surface`.
    SetBlankPageColor(pal.web_surface_bg);
    SetPadding(DpiScaleInt(1), 0);
    ::InvalidateRect(GetHwnd(), nullptr, TRUE);
}

void CChatWebContainer::DrawTabBorders(CDC& dc, RECT& rc)
{
    const auto& pal      = pmui::theme_palette();
    const COLORREF page  = pal.web_surface_bg;
    const bool     isBot = (GetStyle() & TCS_BOTTOM) != 0;

    CRect rcItem;
    const int gap = 1;
    if (GetItemCount() == 0) return;
    GetItemRect(0, rcItem);
    if (rcItem.IsRectEmpty()) return;

    const int left  = rcItem.left;
    const int right = rc.right;
    int       top    = rc.bottom;
    int       bottom = top + gap;

    if (!isBot) {
        const int rcTop = rc.top;
        bottom = std::max<int>(rcTop, GetTabHeight() + gap);
        top    = bottom - gap;
    }

    dc.CreateSolidBrush(page);
    dc.CreatePen(PS_SOLID, 1, page);
    dc.Rectangle(left, top, right, bottom);

    dc.CreatePen(PS_SOLID, 1, pal.caption_pen);
    if (isBot) {
        dc.MoveTo(left - 1, bottom);
        dc.LineTo(right - 1, bottom);
    } else {
        dc.MoveTo(left - 1, top - 1);
        dc.LineTo(right - 1, top - 1);
    }

    dc.CreatePen(PS_SOLID, 1, page);
    GetItemRect(GetCurSel(), rcItem);
    ::OffsetRect(&rcItem, 0, 1);
    if (isBot) {
        dc.MoveTo(rcItem.left, bottom);
        dc.LineTo(rcItem.right, bottom);
    } else {
        dc.MoveTo(rcItem.left, top - 1);
        dc.LineTo(rcItem.right, top - 1);
    }
}

void CChatWebContainer::DrawTabs(CDC& dc)
{
    const auto& pal = pmui::theme_palette();
    // One visible tab: same fill as WebView `web_surface_bg` — avoid a 1px mismatch vs control_bg.
    const COLORREF tabBg = pal.web_surface_bg;
    for (int i = 0; i < GetItemCount(); ++i) {
        CRect rcItem;
        GetItemRect(i, rcItem);
        if (rcItem.IsRectEmpty()) continue;

        dc.CreateSolidBrush(tabBg);
        dc.SetBkColor(tabBg);
        dc.SetTextColor(pal.window_fg);

        dc.CreatePen(PS_SOLID, 1, pal.caption_pen);
        dc.RoundRect(rcItem.left, rcItem.top, rcItem.right + 1, rcItem.bottom, 3, 3);

        CSize szImage = GetImages().GetIconSize();
        const int padding = DpiScaleInt(2);
        if (rcItem.Width() < szImage.cx + 2 * padding) continue;

        CDockContainer* pTab = GetContainerFromIndex(static_cast<size_t>(i));
        CString str   = pTab ? pTab->GetTabText() : CString();
        int     image = (pTab && pTab->GetTabIcon()) ? GetImages().Add(pTab->GetTabIcon()) : -1;
        int yOffset = (rcItem.Height() - szImage.cy) / 2;
        int drawleft = rcItem.left + padding;
        int drawtop  = rcItem.top  + yOffset;
        GetImages().Draw(dc, image, CPoint(drawleft, drawtop), ILD_NORMAL);

        CRect rcText = rcItem;
        if (image >= 0) rcText.left += szImage.cx + padding;
        rcText.left += padding;

        dc.SelectObject(GetTabFont());
        dc.SetBkMode(TRANSPARENT);
        dc.DrawText(str, -1, rcText,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

LRESULT CChatWebContainer::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    try {
        if (msg == WM_ERASEBKGND) {
            HDC hdc = reinterpret_cast<HDC>(wp);
            if (hdc && IsWindow()) {
                const CRect rc = GetClientRect();
                const COLORREF bg = pmui::theme_palette().web_surface_bg;
                if (HBRUSH br = ::CreateSolidBrush(bg)) {
                    ::FillRect(hdc, &rc, br);
                    ::DeleteObject(br);
                }
            }
            return 1;
        }
        return CDockContainerBase::WndProc(msg, wp, lp);
    }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

CChatWebContainer::CChatWebContainer() {
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    SetTabText(d.chat_tab);
    SetDockCaption(d.chat_caption);
    SetView(m_view);
}

CDockChatWeb::CDockChatWeb() {
    SetView(m_container);
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug snapshot (View → Debug → debug.json + log)
// ─────────────────────────────────────────────────────────────────────────────

namespace pmui {

nlohmann::json debug_snapshot_chat_web_ui(const CChatWebView& view)
{
    nlohmann::json root;
    const HWND     host = view.GetHwnd();
    root["host"]          = hwnd_snapshot_json(host);
    root["host_created"]  = (host && ::IsWindow(host));
    root["webview2_tree_max_depth"] = 10;

    {
        const auto& pal = pmui::theme_palette();
        root["theme_at_capture"] = {
            {"dark",           pal.dark},
            {"window_bg",      colorref_json(pal.window_bg)},
            {"web_surface_bg", colorref_json(pal.web_surface_bg)},
        };
    }
/*
    nlohmann::json chain = nlohmann::json::array();
    for (HWND h = host; h; h = ::GetParent(h))
        chain.push_back(hwnd_snapshot_json(h));
    root["parent_chain_leaf_to_root"] = std::move(chain);

    if (host && ::IsWindow(host))
        root["descendants_of_host"] = pmui::hwnd_descendants_json(host, 10);
    else
        root["descendants_of_host"] = nlohmann::json::array();
*/
    return root;
}

} // namespace pmui
