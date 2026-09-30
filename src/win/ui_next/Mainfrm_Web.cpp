// Mainfrm_Web.cpp — CMainFrame WebView2 popup helpers and shared bus wiring.
#include "stdafx.h"
#include "Mainfrm.h"

#include "CSettingsWebView.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/default_shell.hpp"
#include "llm/llm_fs_guard.hpp"
#include "logger/logger.h"
#include "win/settings_store.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <limits>
#include <string>
#include <utility>

namespace {

std::wstring webhost_trim_path(std::wstring p)
{
    while (!p.empty() && (p.back() == L' ' || p.back() == L'\t' || p.back() == L'\n' || p.back() == L'\r'))
        p.pop_back();
    size_t s0 = 0;
    while (s0 < p.size() && (p[s0] == L' ' || p[s0] == L'\t')) ++s0;
    if (s0) p.erase(0, s0);
    return p;
}

bool webhost_path_allowed_for_internal_action(const std::wstring& path, const std::string& fromId, const std::string& cmd)
{
    const std::string deny = media::llm::llm_fs_guard_deny_reason(std::filesystem::path(path));
    if (deny.empty())
        return true;
    logger::warn("[web-host] " + cmd + " from=" + fromId + " blocked: " + deny
        + " path='" + pmui::wide_to_utf8(path) + "'");
    return false;
}

bool webhost_path_from_json(const nlohmann::json& j, std::wstring& out)
{
    out.clear();
    if (!j.contains("path") || !j["path"].is_string())
        return false;
    out = webhost_trim_path(pmui::utf8_to_wide(j["path"].get<std::string>()));
    return !out.empty();
}

static constexpr const char* kWebHostChatSessionsKey = "sessions";
static constexpr int kWebHostChatSessionsMax = 50;

std::int64_t webhost_json_int64_loose(const nlohmann::json& j, const char* key)
{
    if (!j.is_object() || !j.contains(key))
        return 0;
    const auto& v = j[key];
    if (v.is_number_integer())
        return v.get<std::int64_t>();
    if (v.is_number_unsigned()) {
        const auto u = v.get<std::uint64_t>();
        return u > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())
            ? (std::numeric_limits<std::int64_t>::max)()
            : static_cast<std::int64_t>(u);
    }
    if (v.is_string()) {
        try {
            return std::stoll(v.get<std::string>());
        } catch (...) {
        }
    }
    return 0;
}

nlohmann::json& webhost_ensure_chat_sessions_array(nlohmann::json& cw)
{
    if (!cw.contains(kWebHostChatSessionsKey) || !cw[kWebHostChatSessionsKey].is_array())
        cw[kWebHostChatSessionsKey] = nlohmann::json::array();
    return cw[kWebHostChatSessionsKey];
}

void webhost_sort_chat_sessions_newest_first(nlohmann::json& arr)
{
    if (!arr.is_array())
        return;
    std::sort(arr.begin(), arr.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return webhost_json_int64_loose(a, "updatedAt") > webhost_json_int64_loose(b, "updatedAt");
    });
}

void webhost_cap_chat_sessions(nlohmann::json& arr)
{
    if (!arr.is_array())
        return;
    webhost_sort_chat_sessions_newest_first(arr);
    if (static_cast<int>(arr.size()) > kWebHostChatSessionsMax)
        arr.erase(arr.begin() + kWebHostChatSessionsMax, arr.end());
}

nlohmann::json webhost_session_meta_row(const nlohmann::json& sess)
{
    return nlohmann::json{{"id", sess.value("id", std::string{})},
                          {"title", sess.value("title", std::string{"Chat"})},
                          {"createdAt", webhost_json_int64_loose(sess, "createdAt")},
                          {"updatedAt", webhost_json_int64_loose(sess, "updatedAt")}};
}

nlohmann::json webhost_list_chat_sessions(std::string& err)
{
    err.clear();
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "load_chat_web" : err}};
    nlohmann::json out = nlohmann::json::array();
    if (cw.is_object() && cw.contains(kWebHostChatSessionsKey) && cw[kWebHostChatSessionsKey].is_array()) {
        for (const auto& el : cw[kWebHostChatSessionsKey]) {
            if (el.is_object())
                out.push_back(webhost_session_meta_row(el));
        }
    }
    webhost_sort_chat_sessions_newest_first(out);
    return nlohmann::json{{"ok", true}, {"data", nlohmann::json{{"sessions", std::move(out)}}}};
}

nlohmann::json webhost_load_chat_session(const std::string& id, std::string& err)
{
    err.clear();
    if (id.empty())
        return nlohmann::json{{"ok", false}, {"error", "missing id"}};
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "load_chat_web" : err}};
    if (!cw.is_object() || !cw.contains(kWebHostChatSessionsKey) || !cw[kWebHostChatSessionsKey].is_array())
        return nlohmann::json{{"ok", true}, {"data", nlohmann::json(nullptr)}};
    for (const auto& el : cw[kWebHostChatSessionsKey]) {
        if (el.is_object() && el.value("id", std::string{}) == id)
            return nlohmann::json{{"ok", true}, {"data", nlohmann::json{{"session", el}}}};
    }
    return nlohmann::json{{"ok", true}, {"data", nlohmann::json(nullptr)}};
}

nlohmann::json webhost_save_chat_session(const nlohmann::json& sess_in, std::string& err)
{
    err.clear();
    if (!sess_in.is_object())
        return nlohmann::json{{"ok", false}, {"error", "missing session"}};
    const std::string sid = sess_in.value("id", std::string{});
    if (sid.empty())
        return nlohmann::json{{"ok", false}, {"error", "missing session.id"}};
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "load_chat_web" : err}};
    if (!cw.is_object())
        cw = nlohmann::json::object();
    nlohmann::json& arr = webhost_ensure_chat_sessions_array(cw);
    bool replaced = false;
    for (auto& el : arr) {
        if (el.is_object() && el.value("id", std::string{}) == sid) {
            el = sess_in;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        arr.push_back(sess_in);
    webhost_cap_chat_sessions(arr);
    if (!media::settings::save_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "save_chat_web" : err}};
    return nlohmann::json{{"ok", true}};
}

nlohmann::json webhost_delete_chat_session(const std::string& id, std::string& err)
{
    err.clear();
    if (id.empty())
        return nlohmann::json{{"ok", false}, {"error", "missing id"}};
    nlohmann::json cw;
    if (!media::settings::load_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "load_chat_web" : err}};
    if (!cw.is_object() || !cw.contains(kWebHostChatSessionsKey) || !cw[kWebHostChatSessionsKey].is_array())
        return nlohmann::json{{"ok", true}};
    nlohmann::json& arr = cw[kWebHostChatSessionsKey];
    const auto end = std::remove_if(arr.begin(), arr.end(), [&id](const nlohmann::json& el) {
        return el.is_object() && el.value("id", std::string{}) == id;
    });
    if (end == arr.end())
        return nlohmann::json{{"ok", true}};
    arr.erase(end, arr.end());
    if (!media::settings::save_chat_web(cw, err))
        return nlohmann::json{{"ok", false}, {"error", err.empty() ? "save_chat_web" : err}};
    return nlohmann::json{{"ok", true}};
}

bool webhost_is_shared_provider_rpc_method(const std::string& method)
{
    return method == "listChatSessions" || method == "loadChatSession"
        || method == "saveChatSession" || method == "deleteChatSession";
}

nlohmann::json webhost_provider_rpc_dispatch_shared(const nlohmann::json& in)
{
    const std::string method = in.value("method", std::string{});
    std::string err;
    if (method == "listChatSessions")
        return webhost_list_chat_sessions(err);
    if (method == "loadChatSession")
        return webhost_load_chat_session(in.value("id", std::string{}), err);
    if (method == "saveChatSession")
        return webhost_save_chat_session(in.value("session", nlohmann::json{}), err);
    if (method == "deleteChatSession")
        return webhost_delete_chat_session(in.value("id", std::string{}), err);
    return nlohmann::json{{"ok", false}, {"error", "unsupported shared providerRpc method"}};
}

} // namespace

#ifdef FEATURE_BROWSER
bool CMainFrame::HandleWebHostCommand(const std::string& fromId, const std::string& json_utf8)
{
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_utf8);
    } catch (...) {
        return false;
    }

    const nlohmann::json* msg = &root;
    if (root.value("t", std::string{}) == "cweb_bus") {
        if (root.contains("payload") && root["payload"].is_object())
            msg = &root["payload"];
        else
            return false;
        if (msg->value("t", std::string{}) != "cweb_host")
            return false;
    }

    std::string cmd = msg->value("cmd", std::string{});
    if (cmd.empty())
        cmd = msg->value("kind", std::string{});
    if (cmd.empty())
        return false;

    auto run_frame_command = [&](UINT commandId) {
        (void)::SendMessageW(GetHwnd(), WM_COMMAND, MAKEWPARAM(commandId, 0), 0);
    };

    if (cmd == "providerRpc") {
        const std::string method = msg->value("method", std::string{});
        if (!webhost_is_shared_provider_rpc_method(method))
            return false;
        nlohmann::json res;
        try {
            res = webhost_provider_rpc_dispatch_shared(*msg);
        } catch (const std::exception& e) {
            res = nlohmann::json{{"ok", false}, {"error", e.what()}};
        } catch (...) {
            res = nlohmann::json{{"ok", false}, {"error", "providerRpc failed"}};
        }
        nlohmann::json out;
        out["kind"] = "hostProviderRpc";
        for (auto it = res.begin(); it != res.end(); ++it)
            out[it.key()] = it.value();
        if (msg->contains("rpcId"))
            out["id"] = (*msg)["rpcId"];
        else if (msg->contains("id"))
            out["id"] = (*msg)["id"];
        m_webViews.PostTo(fromId, out.dump());
        return true;
    }

    if (cmd == "openPathDefault") {
        std::wstring path;
        if (webhost_path_from_json(*msg, path))
            pmui::shell::open_path(GetHwnd(), path);
        else
            logger::warn("[web-host] openPathDefault from=" + fromId + ": missing path");
        return true;
    }

    if (cmd == "openFolderInExplorer") {
        std::wstring path;
        if (webhost_path_from_json(*msg, path))
            pmui::shell::explore_folder(GetHwnd(), path);
        else
            logger::warn("[web-host] openFolderInExplorer from=" + fromId + ": missing path");
        return true;
    }

    if (cmd == "selectPathInExplorer") {
        std::wstring path;
        if (webhost_path_from_json(*msg, path) && webhost_path_allowed_for_internal_action(path, fromId, cmd)) {
            auto pPath = std::make_unique<std::wstring>(path);
            if (::PostMessageW(GetHwnd(), UWM_SELECT_PATH_IN_EXPLORER, reinterpret_cast<WPARAM>(pPath.get()), 0))
                (void)pPath.release();
        }
        return true;
    }

    if (cmd == "openPathInternal") {
        std::wstring path;
        if (webhost_path_from_json(*msg, path) && webhost_path_allowed_for_internal_action(path, fromId, cmd)) {
            auto pPath = std::make_unique<std::wstring>(path);
            if (::PostMessageW(GetHwnd(), UWM_OPEN_PATH_INTERNAL, reinterpret_cast<WPARAM>(pPath.get()), 0))
                (void)pPath.release();
        }
        return true;
    }

    if (cmd == "toggleFileTree" || cmd == "toggleExplorer") {
        run_frame_command(IDC_CMD_VIEW_FILETREE);
        return true;
    }
    if (cmd == "openFileTree" || cmd == "openInternalExplorer") {
        if (!IsPanelVisible(m_pDockFileTree))
            run_frame_command(IDC_CMD_VIEW_FILETREE);
        return true;
    }
    auto ensure_chat_visible = [&]() {
        if (IsChatWorkbench()) {
            OnChat();
            return;
        }
#if defined(FEATURE_CHAT_WEB)
        if (!m_pDockChatWeb || !IsPanelVisible(m_pDockChatWeb))
            run_frame_command(IDC_CMD_VIEW_CHAT);
#else
        run_frame_command(IDC_CMD_VIEW_CHAT);
#endif
    };

    if (cmd == "openChat") {
        ensure_chat_visible();
        return true;
    }
    if (cmd == "insertInChat" || cmd == "sendToChat") {
        const std::string text = msg->value("text", std::string{});
        if (text.empty())
            return true;
        ensure_chat_visible();
        nlohmann::json out;
        out["t"] = "cweb_bus";
        out["from"] = "host";
        out["payload"] = nlohmann::json{{"t", "chat_insert_draft"}, {"text", text}};
        m_webViews.PostTo("chat", out.dump());
        return true;
    }
    if (cmd == "openChatSession") {
        const std::string sessionId = msg->value("sessionId", std::string{});
        if (sessionId.empty()) {
            logger::warn("[web-host] openChatSession from=" + fromId + ": missing sessionId");
            return true;
        }
        ensure_chat_visible();
        nlohmann::json nav;
        nav["t"] = "cweb_bus";
        nav["from"] = "host";
        nav["payload"] = nlohmann::json{{"t", "chat_navigate_session"}, {"sessionId", sessionId}};
        m_webViews.PostTo("chat", nav.dump());
        return true;
    }
#ifdef FEATURE_CONSOLE
    auto ensure_console_visible = [&]() {
        if (!pmui::web_console::available()) {
            if (!IsPanelVisible(m_pDockLog))
                EnsurePanelVisible(m_pDockLog, DS_DOCKED_BOTTOM, GetDockAncestor(), DpiScaleInt(220), IDC_CMD_VIEW_LOG);
            return;
        }
        if (!m_pDockConsole) {
            if (CDocker* existing = GetDockFromID(DOCK_ID_CONSOLE))
                m_pDockConsole = static_cast<CDockWebConsole*>(existing);
        }
        EnsurePanelVisible(m_pDockConsole, DS_DOCKED_BOTTOM, GetDockAncestor(), DpiScaleInt(260), IDC_CMD_VIEW_CONSOLE);
        WireConsoleBus();
        if (m_pDockConsole && m_pDockConsole->IsWindow())
            ::SetFocus(m_pDockConsole->GetConsoleContainer().GetWebView().GetHwnd());
    };

    if (cmd == "openConsole" || cmd == "openTerminal") {
        ensure_console_visible();
        return true;
    }
    if (cmd == "pasteInConsole" || cmd == "insertInConsole") {
        const std::string text = msg->value("text", std::string{});
        if (text.empty())
            return true;
        ensure_console_visible();
        nlohmann::json out;
        out["t"] = "cweb_bus";
        out["from"] = "host";
        out["payload"] = nlohmann::json{{"t", "console_insert"}, {"text", text}};
        m_webViews.PostTo("console", out.dump());
        return true;
    }
    if (cmd == "runInConsole" || cmd == "runConsole") {
        const std::string text = msg->value("text", std::string{});
        if (text.empty())
            return true;
        const bool new_shell = msg->value("newShell", true);
        const bool close_on_exit = msg->value("closeOnExit", false);
        RunInWebConsole(text, new_shell, close_on_exit);
        return true;
    }
#endif
    if (cmd == "openAppSettings") {
        run_frame_command(IDC_CMD_APP_SETTINGS);
        return true;
    }
    if (cmd == "settings" || cmd == "openSettings") {
        ShowSettingsPopup();
        return true;
    }

    return false;
}

void CMainFrame::ShowBrowserPopup()
{
    logger::info("[browser] ShowBrowserPopup: enter");
    RECT wr{100, 100, 1000, 740};
    if (HWND mfh = GetHwnd(); mfh && ::IsWindow(mfh))
        (void)::GetWindowRect(mfh, &wr);
    const int pw = DpiScaleInt(900), ph = DpiScaleInt(640);
    const int px = wr.left + (wr.right  - wr.left - pw) / 2;
    const int py = wr.top  + (wr.bottom - wr.top  - ph) / 2;

    CWebViewManager::ShowRequest req{};
    req.id = "browser";
    req.title = L"Web Browser";
    req.opts = CWebViewOptions::ForPopup(/*devTools=*/true);
    req.style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE;
    req.exStyle = WS_EX_APPWINDOW;
    req.startupRect = RECT{px, py, px + pw, py + ph};
    m_webViews.ShowOrFocus(req);
}

void CMainFrame::ShowBrowserPopupUrl(const std::string& url)
{
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        return;

    logger::info(std::string("[browser] ShowBrowserPopupUrl: ") + url.substr(0, 500));
    RECT wr{100, 100, 1000, 740};
    if (HWND mfh = GetHwnd(); mfh && ::IsWindow(mfh))
        (void)::GetWindowRect(mfh, &wr);
    const int pw = DpiScaleInt(900), ph = DpiScaleInt(640);
    const int px = wr.left + (wr.right  - wr.left - pw) / 2;
    const int py = wr.top  + (wr.bottom - wr.top  - ph) / 2;

    static unsigned s_terminalLinkSeq = 0;
    CWebViewOptions opts = CWebViewOptions::ForPopup(/*devTools=*/true);
    opts.html.clear();
    opts.url = pmui::utf8_to_wide(url);
    opts.allowExternalNav = true;

    CWebViewManager::ShowRequest req{};
    req.id = "terminal-link-" + std::to_string(++s_terminalLinkSeq);
    req.title = L"Terminal Link";
    req.opts = std::move(opts);
    req.style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE;
    req.exStyle = WS_EX_APPWINDOW;
    req.startupRect = RECT{px, py, px + pw, py + ph};
    m_webViews.ShowOrFocus(req);
}

void CMainFrame::ShowSettingsPopup()
{
    auto apply_settings_appearance = [this]() {
        ApplyAppearance();
        std::string wlerr;
        media::settings::WindowLayout wl{};
        if (m_pDockFileTree
            && media::settings::load_window_layout(wl, wlerr, m_workbench->workbenchSettingsId()))
            m_pDockFileTree->GetFileTreeContainer().GetBrowserView().SetShowShellFrames(
                wl.filetree_show_shell_frames);
#if defined(FEATURE_USE_OWN_RIBBON)
        if (m_ownRibbon.IsWindow()) {
            if (!m_ownRibbon.BuildRibbonLayout()) {
                const wchar_t* detail = own_ribbon::LastRibbonLayoutErrorW();
                logger::warn("[settings] custom ribbon refresh failed: "
                    + pmui::wide_to_utf8(detail && detail[0] ? detail : L"unknown error"));
            } else {
                const CRect crc = GetClientRect();
                const int h = m_ownRibbon.PreferredHeight();
                const int y0 = OwnRibbonClientTopY();
                m_ownRibbon.SetWindowPos(nullptr, 0, y0, crc.Width(), h, SWP_NOZORDER);
                m_ownRibbon.SyncBatchControls(*this);
                RecalcLayout();
                m_ownRibbon.Invalidate();
            }
        }
#endif
    };
    pmui::CSettingsWebView::Show(m_webViews, GetHwnd(), apply_settings_appearance);
    apply_settings_appearance();
}

void CMainFrame::ShowViewerBrowserPopupInstance(const std::string& id, bool modal)
{
    auto opts = CWebViewOptions::ForPurePopup(/*devTools=*/true);
    opts.html = CWebViewOptions::cweb_playground_html_str();
    opts.frame = true;
    opts.clickthrough = false;
    opts.modal = modal;
    opts.onMessage = [this, id](const std::string& json) {
        if (json.find("\"cweb_close\"") != std::string::npos) {
            m_webViews.Close(id);
            return;
        }
        if (json.find("\"t\":\"debug\"") != std::string::npos) {
            logger::info(std::string("[viewer-browser] js-debug: ") + json.substr(0, 400));
            return;
        }
        logger::info(std::string("[viewer-browser] msg: ") + json.substr(0, 400));
    };

    RECT wr{100, 100, 1100, 800};
    if (HWND mfh = GetHwnd(); mfh && ::IsWindow(mfh))
        (void)::GetWindowRect(mfh, &wr);
    const int pw = DpiScaleInt(400), ph = DpiScaleInt(300);
    const int px = wr.left + (wr.right - wr.left - pw) / 2;
    const int py = wr.top  + (wr.bottom - wr.top - ph) / 2;

    CWebViewManager::ShowRequest req{};
    req.id = id;
    req.title = L"Viewer Browser";
    req.opts = std::move(opts);
    req.style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE;
    req.exStyle = WS_EX_APPWINDOW;
    req.startupRect = RECT{px, py, px + pw, py + ph};
    m_webViews.ShowOrFocus(req);
}

void CMainFrame::ShowViewerBrowserPopup()
{
    logger::info("[viewer-browser] ShowViewerBrowserPopup: enter");

    // Defensive: if previous modal popup path failed to restore owner enable,
    // make sure the main frame is interactive before trying to show/recreate.
    if (!::IsWindowEnabled(GetHwnd()))
        ::EnableWindow(GetHwnd(), TRUE);

    ShowViewerBrowserPopupInstance("viewer", /*modal=*/true);
}
#endif

#if defined(FEATURE_BROWSER) && defined(FEATURE_CONSOLE)
void CMainFrame::WireConsoleBus()
{
    if (!m_pDockConsole)
        return;
    m_pDockConsole->GetConsoleContainer().SetBusManager(&m_webViews);
}

bool CMainFrame::RunInWebConsole(const std::string& line_utf8, bool new_shell, bool close_on_exit)
{
    if (!pmui::web_console::available() || line_utf8.empty())
        return false;
    if (!m_pDockConsole) {
        if (CDocker* existing = GetDockFromID(DOCK_ID_CONSOLE))
            m_pDockConsole = static_cast<CDockWebConsole*>(existing);
    }
    EnsurePanelVisible(m_pDockConsole, DS_DOCKED_BOTTOM, GetDockAncestor(), DpiScaleInt(260), IDC_CMD_VIEW_CONSOLE);
    WireConsoleBus();
    if (!m_pDockConsole || !m_pDockConsole->IsWindow())
        return false;
    nlohmann::json out;
    out["t"] = "cweb_bus";
    out["from"] = "host";
    out["payload"] = nlohmann::json{
        {"t", "console_run"},
        {"text", line_utf8},
        {"newShell", new_shell},
        {"closeOnExit", close_on_exit},
    };
    m_webViews.PostTo("console", out.dump());
    ::SetFocus(m_pDockConsole->GetConsoleContainer().GetWebView().GetHwnd());
    return true;
}

#endif

#if defined(FEATURE_BROWSER) && defined(FEATURE_CHAT_WEB)
void CMainFrame::WireChatBus()
{
    if (IsChatWorkbench()) {
        m_workbenchClientChatWeb.SetBusManager(&m_webViews);
        return;
    }
    if (m_pDockChatWeb) {
        m_pDockChatWeb->GetChatWebContainer().GetChatWebView().SetBusManager(&m_webViews);
        return;
    }
    m_workbenchClientChatWeb.SetBusManager(&m_webViews);
}
#endif

#if defined(FEATURE_BROWSER) && (defined(FEATURE_VIEWER_WEB) || defined(FEATURE_HOME_PAGE))
void CMainFrame::WireFileViewerBus()
{
    m_viewerManager.ActiveView().SetBusManager(&m_webViews);
    RefreshViewerDockTabs();
    for (CDockViewerPanel* viewerTab : m_viewerDockTabs) {
        if (viewerTab)
            viewerTab->GetFileViewer().SetBusManager(&m_webViews);
    }
}
#endif
