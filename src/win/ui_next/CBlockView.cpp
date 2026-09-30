#include "stdafx.h"
#include "CBlockView.hpp"

#include "ChatWebResource.h"
#include "cli/pm_image_register_cli.hpp"
#include "constants.hpp"
#include "core/command_variables.hpp"
#include "core/settings_runtime.hpp"
#include "helpers/default_shell.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/ui_font.hpp"
#include "llm/tools/run/RunTool.hpp"
#include "Resource.h"
#include "win/ribbon_commands.hpp"
#include "win/settings_store.hpp"
#include "xblox/blocks/builtin_blocks.hpp"
#include "xblox/utils/conv.hpp"
#include "xblox_commands.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const wchar_t* kXbloxVhost = L"pm-xblox.invalid";
constexpr const wchar_t* kXbloxHtmlName = L"xblox.html";

fs::path module_exe_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0u)
        return {};
    buf.resize(n);
    return fs::path(buf).parent_path();
}

std::wstring xblox_shared_dir()
{
    const fs::path exe = module_exe_dir();
    if (exe.empty())
        return {};
    if (exe.filename() == L"win-x64")
        return (exe.parent_path() / L"shared").wstring();
    return (exe / L"shared").wstring();
}

std::string json_string(const nlohmann::json& o, const char* key)
{
    if (o.contains(key) && o[key].is_string())
        return o[key].get<std::string>();
    return {};
}

UINT ribbon_command_id_from_json(const nlohmann::json& command)
{
    std::string id = json_string(command, "ribbonCommand");
    if (id.empty())
        id = json_string(command, "commandId");
    if (id.empty())
        return 0;
    if (const auto n = pm::cli::ribbon_command_id_from_name(id))
        return static_cast<UINT>(n);
    return 0;
}

HWND root_hwnd_from_owner(HWND owner)
{
    if (!owner || !::IsWindow(owner))
        return nullptr;
    return ::GetAncestor(owner, GA_ROOT);
}

std::string read_file_utf8(const fs::path& path, std::string& err)
{
    err.clear();
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            err = "cannot open " + path.string();
            return {};
        }
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    } catch (const std::exception& e) {
        err = e.what();
        return {};
    }
}

bool write_file_utf8(const fs::path& path, const std::string& text, std::string& err)
{
    err.clear();
    try {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            err = ec.message();
            return false;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "cannot write " + path.string();
            return false;
        }
        out << text;
        if (!out.good()) {
            err = "write failed: " + path.string();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

bool post_console_run(HWND owner, const std::string& line, bool new_shell, bool close_on_exit, std::string* message)
{
    HWND root = root_hwnd_from_owner(owner);
    if (!root) {
        if (message) *message = "main frame is unavailable";
        return false;
    }
    nlohmann::json payload = {
        {"line", line},
        {"newShell", new_shell},
        {"closeOnExit", close_on_exit},
    };
    auto* raw = new std::string(payload.dump());
    if (!::PostMessageW(root, UWM_XBLOX_RUN_CONSOLE, reinterpret_cast<WPARAM>(raw), 0)) {
        delete raw;
        if (message) *message = "failed to post console command";
        return false;
    }
    if (message) *message = "sent to terminal";
    return true;
}

fs::path current_cli_exe_path()
{
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1u));
    if (n == 0u)
        return {};
    const fs::path current = fs::path(std::wstring(buf.data(), n));
    const fs::path cli = current.parent_path() / L"pm-image-cli.exe";
    std::error_code ec;
    if (fs::is_regular_file(cli, ec) && !ec)
        return cli;
    return current;
}

nlohmann::json load_custom_commands_doc()
{
    std::string raw;
    std::string err;
    if (!media::runtime_settings::load_command_json_utf8(raw, err) || raw.empty())
        return nlohmann::json::object();
    try {
        return nlohmann::json::parse(raw);
    } catch (...) {
        return nlohmann::json::object();
    }
}

nlohmann::json load_custom_commands_document()
{
    std::string raw;
    std::string err;
    if (!media::runtime_settings::load_command_json_utf8(raw, err) || raw.empty())
        return nlohmann::json{{"version", 1}, {"ribbon", {{"groups", nlohmann::json::array()}}}};
    try {
        nlohmann::json doc = nlohmann::json::parse(raw);
        if (doc.is_object())
            return doc;
    } catch (...) {
    }
    return nlohmann::json{{"version", 1}, {"ribbon", {{"groups", nlohmann::json::array()}}}};
}

nlohmann::json xblox_commands_payload()
{
    nlohmann::json registered = nlohmann::json::array();
    for (const auto& c : pm::cli::registered_cli_commands()) {
        registered.push_back({
            {"id", c.id ? c.id : ""},
            {"label", c.label ? c.label : ""},
            {"available", c.available},
        });
    }

    nlohmann::json custom = nlohmann::json::array();
    for (const auto& c : pm::cli::visible_custom_commands()) {
        custom.push_back({
            {"group", c.group},
            {"id", c.id},
            {"label", c.label},
            {"type", c.type},
            {"action", c.action},
        });
    }

    nlohmann::json native_blocks = nlohmann::json::array();
    for (const auto& block : media::xblox::blocks::registered_block_definitions()) {
        native_blocks.push_back({
            {"kind", block.kind},
            {"label", block.label},
            {"description", block.description},
            {"group", block.group},
            {"block", block.default_block},
            {"params", block.params},
            {"flags", block.flags},
            {"platformMask", block.platform_mask},
        });
    }

    return {
        {"commandsPath", media::settings::get_command_json_path().string()},
        {"document", load_custom_commands_document()},
        {"registeredCommands", std::move(registered)},
        {"customCommands", std::move(custom)},
        {"nativeBlocks", std::move(native_blocks)},
    };
}

nlohmann::json live_event_message(const media::xblox::ExecutionEvent& event)
{
    return {
        {"kind", "hostXbloxRunEvent"},
        {"event", media::xblox::conv::event_to_json(event)},
    };
}

nlohmann::json run_xblox_document(const nlohmann::json& document,
                                  HWND owner,
                                  pmui::CBlockView* view = nullptr,
                                  const std::wstring& cwd = {})
{
    media::xblox::ExecutionOptions options;
    options.custom_commands = load_custom_commands_doc();
    options.cli_executable_path = current_cli_exe_path();
    if (view)
        options.cancel_requested = [view] { return !view->IsWindow() || view->XbloxStopRequested(); };
    if (!cwd.empty()) {
        options.default_cwd = fs::path(cwd);
        options.command_context.cwd = pmui::wide_to_utf8(cwd);
    }
    if (view) {
        const auto& selection = view->Selection();
        options.command_context.selection_paths.reserve(selection.size());
        for (const auto& path : selection) {
            if (!path.empty())
                options.command_context.selection_paths.push_back(pmui::wide_to_utf8(path));
        }
        if (!options.command_context.selection_paths.empty())
            options.command_context.source_file = options.command_context.selection_paths.front();
    }
    if (view) {
        options.collect_events = false;
        options.event_sink = [view](const media::xblox::ExecutionEvent& event) {
            if (!view->IsWindow())
                return;
            view->PostToWebFromAnyThread(live_event_message(event).dump());
        };
    }
    options.app_command_handler = [owner](const media::xblox::CommandInvocation& invocation, std::string* message) {
        const std::string app_command = json_string(invocation.command, "appCommand");
        if (app_command.empty()) {
            if (message) *message = "appCommand is empty";
            return 2;
        }
        HWND root = root_hwnd_from_owner(owner);
        if (!root) {
            if (message) *message = "main frame is unavailable";
            return 1;
        }
        ::PostMessageW(root, UWM_APP_COMMAND, reinterpret_cast<WPARAM>(new std::string(app_command)), 0);
        if (message) *message = "app command posted: " + app_command;
        return 0;
    };
    options.ribbon_command_handler = [owner](const media::xblox::CommandInvocation& invocation, std::string* message) {
        const UINT cmd_id = ribbon_command_id_from_json(invocation.command);
        if (cmd_id == 0) {
            if (message) *message = "unknown ribbonCommand";
            return 2;
        }
        HWND root = root_hwnd_from_owner(owner);
        if (!root) {
            if (message) *message = "main frame is unavailable";
            return 1;
        }
        ::PostMessageW(root, WM_COMMAND, MAKEWPARAM(cmd_id, 0), 0);
        if (message) *message = "ribbon command posted";
        return 0;
    };
    options.open_url_handler = [owner](const media::xblox::CommandInvocation& invocation, std::string* message) {
        const std::string url = json_string(invocation.command, "url");
        if (url.empty()) {
            if (message) *message = "url is empty";
            return 2;
        }
        const bool ok = pmui::shell::open_url(root_hwnd_from_owner(owner), pmui::utf8_to_wide(url));
        if (message) *message = ok ? "url opened" : "url open failed";
        return ok ? 0 : 1;
    };
    options.open_path_handler = [owner](const media::xblox::CommandInvocation& invocation, std::string* message) {
        const std::string path = json_string(invocation.command, "path");
        if (path.empty()) {
            if (message) *message = "path is empty";
            return 2;
        }
        const bool ok = pmui::shell::open_path(root_hwnd_from_owner(owner), pmui::utf8_to_wide(path));
        if (message) *message = ok ? "path opened" : "path open failed";
        return ok ? 0 : 1;
    };
    options.console_command_handler = [owner](const std::string& line, bool new_shell, bool close_on_exit, std::string* message) {
        return post_console_run(owner, line, new_shell, close_on_exit, message) ? 0 : 1;
    };

    auto result = media::xblox::run_blocks_file(document, options);
    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : result.events)
        events.push_back(media::xblox::conv::event_to_json(event));

    return {
        {"ok", result.ok},
        {"exitCode", result.exit_code},
        {"events", std::move(events)},
        {"cwd", options.default_cwd.empty() ? std::string{} : options.default_cwd.string()},
    };
}

nlohmann::json run_single_command(const nlohmann::json& command, HWND owner, pmui::CBlockView* view = nullptr)
{
    const nlohmann::json doc = {
        {"version", 1},
        {"roots", nlohmann::json::array({
            {
                {"kind", "runScript"},
                {"id", command.value("id", std::string{"command"})},
                {"method", "return host.runCustomCommand(" + command.dump() + ");"},
            },
        })},
    };
    return run_xblox_document(doc, owner, view);
}

void post_rpc(pmui::CBlockView& view, const std::string& id, bool ok, nlohmann::json data, std::string error = {})
{
    nlohmann::json out;
    out["kind"] = "hostProviderRpc";
    out["id"] = id;
    out["ok"] = ok;
    if (ok)
        out["data"] = std::move(data);
    else
        out["error"] = std::move(error);
    view.PostToWebFromAnyThread(out.dump());
}

void post_init(pmui::CBlockView& view, const std::wstring& path)
{
    nlohmann::json out;
    out["kind"] = "hostXblox";
    out["path"] = path.empty() ? std::string{} : pmui::wide_to_utf8(path);
    const int font_extra_pt = (std::max)(0, pmui::ui_font_extra_pt());
    out["fontExtraPt"] = font_extra_pt;
    out["fontSizePx"] = ((9.0 + static_cast<double>(font_extra_pt)) * 96.0) / 72.0;
    view.PostToWeb(out.dump());
}

void handle_xblox_rpc(pmui::CBlockView& view,
                      std::wstring& session_path,
                      const std::string& json)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (...) {
        return;
    }
    if (j.value("kind", std::string{}) != "providerRpc")
        return;

    const std::string method = j.value("method", std::string{});
    const std::string rpcId = j.value("rpcId", std::string{});
    if (rpcId.empty())
        return;

    try {
        if (method == "xbloxDocumentGet") {
            nlohmann::json document = nullptr;
            std::string err;
            if (!session_path.empty()) {
                const std::string raw = read_file_utf8(fs::path(session_path), err);
                if (!err.empty()) {
                    post_rpc(view, rpcId, false, {}, err);
                    return;
                }
                document = nlohmann::json::parse(raw);
            }
            post_rpc(view, rpcId, true, {
                {"path", !session_path.empty() ? pmui::wide_to_utf8(session_path) : std::string{}},
                {"document", std::move(document)},
                {"commands", xblox_commands_payload()},
            });
            return;
        }

        if (method == "xbloxDocumentSave") {
            std::wstring path = session_path;
            if (j.contains("path") && j["path"].is_string())
                path = pmui::utf8_to_wide(j["path"].get<std::string>());
            if (path.empty()) {
                post_rpc(view, rpcId, false, {}, "xbloxDocumentSave: missing path");
                return;
            }
            if (!j.contains("document") || !j["document"].is_object()) {
                post_rpc(view, rpcId, false, {}, "xbloxDocumentSave: missing document");
                return;
            }
            std::string err;
            if (!write_file_utf8(fs::path(path), j["document"].dump(2) + "\n", err)) {
                post_rpc(view, rpcId, false, {}, err);
                return;
            }
            session_path = path;
            post_rpc(view, rpcId, true, {{"path", pmui::wide_to_utf8(path)}});
            return;
        }

        if (method == "xbloxDocumentRun") {
            nlohmann::json document = j.value("document", nlohmann::json{});
            if (!document.is_object() && !session_path.empty()) {
                std::string err;
                const std::string raw = read_file_utf8(fs::path(session_path), err);
                if (!err.empty()) {
                    post_rpc(view, rpcId, false, {}, err);
                    return;
                }
                document = nlohmann::json::parse(raw);
            }
            if (!document.is_object()) {
                post_rpc(view, rpcId, false, {}, "xbloxDocumentRun: missing document");
                return;
            }
            const auto generation = view.BeginXbloxRun();
            const HWND owner = view.GetHwnd();
            const std::wstring folder = view.CurrentFolder();
            std::thread([document = std::move(document), owner, &view, rpcId, generation, folder]() mutable {
                try {
                    const auto result = run_xblox_document(document, owner, &view, folder);
                    if (view.IsWindow())
                        post_rpc(view, rpcId, true, result);
                } catch (const std::exception& e) {
                    if (view.IsWindow())
                        post_rpc(view, rpcId, false, {}, e.what());
                } catch (...) {
                    if (view.IsWindow())
                        post_rpc(view, rpcId, false, {}, "xbloxDocumentRun failed");
                }
                if (view.IsWindow())
                    view.FinishXbloxRun(generation);
            }).detach();
            return;
        }

        if (method == "xbloxDocumentStop") {
            view.RequestStopXbloxRun();
            post_rpc(view, rpcId, true, {{"stopped", true}});
            return;
        }

        if (method == "xbloxCommandsGet" || method == "settingsCustomCommandsGet") {
            post_rpc(view, rpcId, true, xblox_commands_payload());
            return;
        }

        if (method == "xbloxCommandRun") {
            if (!j.contains("command") || !j["command"].is_object()) {
                post_rpc(view, rpcId, false, {}, "xbloxCommandRun: missing command");
                return;
            }
            const auto generation = view.BeginXbloxRun();
            const HWND owner = view.GetHwnd();
            nlohmann::json command = j["command"];
            std::thread([command = std::move(command), owner, &view, rpcId, generation]() mutable {
                try {
                    const auto result = run_single_command(command, owner, &view);
                    if (view.IsWindow())
                        post_rpc(view, rpcId, true, result);
                } catch (const std::exception& e) {
                    if (view.IsWindow())
                        post_rpc(view, rpcId, false, {}, e.what());
                } catch (...) {
                    if (view.IsWindow())
                        post_rpc(view, rpcId, false, {}, "xbloxCommandRun failed");
                }
                if (view.IsWindow())
                    view.FinishXbloxRun(generation);
            }).detach();
            return;
        }

        post_rpc(view, rpcId, false, {}, "unknown xblox providerRpc method");
    } catch (const std::exception& e) {
        post_rpc(view, rpcId, false, {}, e.what());
    } catch (...) {
        post_rpc(view, rpcId, false, {}, "xblox providerRpc failed");
    }
}

} // namespace

namespace pmui {

CBlockView::CBlockView()
{
    CWebViewOptions opts = CWebViewOptions::ForDocked(/*devTools=*/true);
    opts.vhostName = kXbloxVhost;
    opts.localFolder = xblox_shared_dir();
    opts.url = std::wstring(L"https://") + kXbloxVhost + L"/" + kXbloxHtmlName;
    opts.mapFixedDriveFileHosts = true;
    opts.onMessage = [this](const std::string& json_utf8) {
        HandleHostMessage(json_utf8);
    };
    SetOptions(std::move(opts));
}

CBlockView::~CBlockView()
{
    RequestStopXbloxRun();
    SetBusManager(nullptr);
}

unsigned long long CBlockView::BeginXbloxRun()
{
    m_xbloxStopRequested.store(false, std::memory_order_release);
    return m_xbloxRunGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
}

void CBlockView::FinishXbloxRun(unsigned long long generation)
{
    if (m_xbloxRunGeneration.load(std::memory_order_acquire) == generation)
        m_xbloxStopRequested.store(false, std::memory_order_release);
}

void CBlockView::RequestStopXbloxRun()
{
    m_xbloxStopRequested.store(true, std::memory_order_release);
    media::llm::run::abort_active_run();
}

bool CBlockView::OpenXbloxFile(std::wstring xbloxPath, CWebViewManager* manager)
{
    const std::string xblox_html = pmui::load_xblox_web_html();
    if (xblox_html.empty()) {
        ::MessageBoxW(GetHwnd(),
            L"xblox.html is missing or empty (expected dist\\shared\\xblox.html).\n\nRun npm --prefix apps\\xblox run build:embed.",
            pm::brand::k_app_id_w,
            MB_ICONWARNING | MB_OK);
        return false;
    }

    m_path = std::move(xbloxPath);
    if (m_folder.empty() && !m_path.empty()) {
        std::error_code ec;
        const fs::path parent = fs::path(m_path).parent_path();
        if (!parent.empty())
            m_folder = parent.wstring();
        else {
            const auto cwd = fs::current_path(ec);
            if (!ec)
                m_folder = cwd.wstring();
        }
    }
    SetBusManager(manager);
    Navigate(std::wstring(L"https://") + kXbloxVhost + L"/" + kXbloxHtmlName);
    PostInit();
    return true;
}

void CBlockView::SetContext(const std::vector<std::wstring>& selection, const std::wstring& folder)
{
    m_selection = selection;
    m_folder = folder;
}

void CBlockView::SetBusManager(CWebViewManager* manager)
{
    if (m_busManager && m_busManager != manager)
        m_busManager->UnregisterExternal("xblox", this);
    m_busManager = manager;
    if (m_busManager) {
        m_busManager->RegisterExternal("xblox", this,
            [this]() {
                return IsWindow() != FALSE;
            },
            [this](const std::string& json_utf8) {
                PostToWeb(json_utf8);
            });
    }
}

void CBlockView::HandleHostMessage(const std::string& json_utf8)
{
    if (m_busManager) {
        try {
            const auto j = nlohmann::json::parse(json_utf8);
            if (j.value("t", std::string{}) == "cweb_bus")
                m_busManager->HandleExternalMessage("xblox", json_utf8);
        } catch (...) {
        }
    }
    handle_xblox_rpc(*this, m_path, json_utf8);
}

void CBlockView::PostInit()
{
    post_init(*this, m_path);
}

} // namespace pmui
