#include "stdafx.h"
#include "win/web/CWebConsole.h"
#include "helpers/text_conv.hpp"
#include "helpers/theme.hpp"
#include "logger/logger.h"
#include "pty/PtySession.h"
#include "win/web/CWebViewManager.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr UINT UWM_CONSOLE_PTY_EVENT = WM_APP + 91;
constexpr UINT UWM_CONSOLE_OPEN_URL = WM_APP + 92;

enum class ConsolePtyEventKind {
    Output,
    Exit,
    Error,
};

struct ConsolePtyEvent {
    ConsolePtyEventKind kind = ConsolePtyEventKind::Output;
    std::string shell_id;
    std::string text;
    int code = 0;
};

DWORD64 console_now_ms()
{
    return ::GetTickCount64();
}

fs::path module_exe_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0u)
        return {};
    buf.resize(n);
    return fs::path(buf).parent_path();
}

std::wstring getenv_w(const wchar_t* name)
{
    const DWORD needed = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
        return {};
    std::wstring value(needed, L'\0');
    const DWORD n = ::GetEnvironmentVariableW(name, value.data(), needed);
    if (n == 0)
        return {};
    value.resize(n);
    return value;
}

std::string read_utf8_file(const fs::path& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return {};
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

fs::path find_shell_html()
{
    const DWORD64 t0 = console_now_ms();
    const fs::path exe = module_exe_dir();
    if (exe.empty()) {
        logger::warn("[console-open] shell bundle lookup: module exe dir is empty");
        return {};
    }

    std::vector<fs::path> candidates;
    if (exe.filename() == L"win-x64")
        candidates.push_back(exe.parent_path() / L"shared" / L"shell.html");
    candidates.push_back(exe / L"shared" / L"shell.html");
    candidates.push_back(exe / L"shell.html");

    // Dev fallback when running from the repo root or a build tree.
    candidates.push_back(fs::current_path() / L"dist" / L"shared" / L"shell.html");
    candidates.push_back(fs::current_path() / L"apps" / L"shell" / L"dist" / L"index.html");

    for (const auto& candidate : candidates) {
        std::error_code ec;
        const bool found = fs::is_regular_file(candidate, ec) && !ec;
        logger::trace(std::string("[console-open] shell bundle candidate ")
            + pmui::wide_to_utf8(candidate.wstring())
            + (found ? " found" : " missing")
            + " (" + std::to_string(console_now_ms() - t0) + " ms)");
        if (found)
            return candidate;
    }
    logger::warn(std::string("[console-open] shell bundle lookup failed after ")
        + std::to_string(console_now_ms() - t0) + " ms");
    return {};
}

std::string fallback_console_html()
{
    return R"(<!doctype html><html><head><meta charset="utf-8">
<style>
html,body{margin:0;height:100%;background:#1e1e1e;color:#d4d4d4;font:13px "Segoe UI",sans-serif}
.wrap{box-sizing:border-box;height:100%;display:grid;place-items:center;padding:24px;text-align:center}
code{background:#2d2d30;border:1px solid #3c3c3c;border-radius:4px;padding:2px 6px}
</style></head><body><div class="wrap"><div>
<h2>Console bundle not found</h2>
<p>Build the shell web bundle and ship it as <code>dist/shared/shell.html</code>.</p>
<p>For development, set <code>PM_CONSOLE_URL=http://localhost:5173/</code>.</p>
</div></div></body></html>)";
}

void open_shell_link(const std::string& path_utf8)
{
    if (path_utf8.empty())
        return;
    fs::path path = pmui::utf8_to_wide(path_utf8);
    if (path.is_relative()) {
        try {
            path = fs::current_path() / path;
        } catch (...) {
        }
    }
    (void)::ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_shell_url_in_app(HWND source, const std::string& url)
{
    if (url.empty())
        return;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        return;

    HWND target = source ? ::GetAncestor(source, GA_ROOT) : nullptr;
    if (!target || !::IsWindow(target))
        return;

    auto* payload = new std::string(url);
    if (!::PostMessageW(target, UWM_CONSOLE_OPEN_URL, 0, reinterpret_cast<LPARAM>(payload)))
        delete payload;
}

void open_shell_url_external(const std::string& url)
{
    if (url.empty())
        return;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        return;
    const std::wstring wide = pmui::utf8_to_wide(url);
    (void)::ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string read_clipboard_text_utf8(HWND owner)
{
    if (!::IsClipboardFormatAvailable(CF_UNICODETEXT))
        return {};
    if (!::OpenClipboard(owner))
        return {};

    std::string out;
    if (HANDLE h = ::GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* text = static_cast<const wchar_t*>(::GlobalLock(h))) {
            out = pmui::wide_to_utf8(text);
            ::GlobalUnlock(h);
        }
    }
    ::CloseClipboard();
    return out;
}

CWebViewOptions make_options(CWebViewOptions::Mode mode, bool devTools)
{
    const DWORD64 t0 = console_now_ms();
    CWebViewOptions opts;
    opts.mode = mode;
    opts.devToolsEnabled = devTools;
    opts.defaultContextMenusEnabled = false;
    opts.allowExternalNav = false;
    opts.vhostName = L"pm-console.local";
    opts.mapFixedDriveFileHosts = true;
    opts.onConsole = [](const std::string& level, const std::string& msg) {
        const std::string line = "[console-web] " + level + ": " + msg.substr(0, 1200);
        if (level == "error")
            logger::error(line);
        else if (level == "warn")
            logger::warn(line);
        else
            logger::trace(line);
    };
    opts.onMessage = [](const std::string& json_utf8) {
        logger::trace(std::string("[console-web] msg: ") + json_utf8.substr(0, 1200));
        // Native ConPTY bridge will be attached here: shell_data, shell_resize, shell_exit.
    };

    const std::wstring devUrl = getenv_w(L"PM_CONSOLE_URL");
    if (!devUrl.empty()) {
        opts.url = devUrl;
        opts.allowExternalNav = true;
        logger::trace(std::string("[console-open] using PM_CONSOLE_URL ")
            + pmui::wide_to_utf8(devUrl)
            + " (" + std::to_string(console_now_ms() - t0) + " ms)");
        return opts;
    }

    const fs::path shellHtml = find_shell_html();
    if (!shellHtml.empty()) {
        std::error_code ec;
        const auto bytes = fs::file_size(shellHtml, ec);
        const DWORD64 readStart = console_now_ms();
        opts.html = read_utf8_file(shellHtml);
        if (opts.html.empty()) {
            logger::warn(std::string("[console-open] shell bundle read failed; using fallback HTML path=")
                + pmui::wide_to_utf8(shellHtml.wstring()));
            opts.html = fallback_console_html();
        }
        logger::trace(std::string("[console-open] using shell bundle ")
            + pmui::wide_to_utf8(shellHtml.wstring())
            + " size=" + (ec ? std::string("?") : std::to_string(static_cast<unsigned long long>(bytes)))
            + " via NavigateToString read=" + std::to_string(console_now_ms() - readStart) + " ms"
            + " (" + std::to_string(console_now_ms() - t0) + " ms)");
        return opts;
    }

    opts.html = fallback_console_html();
    logger::warn(std::string("[console-open] using fallback console HTML (")
        + std::to_string(console_now_ms() - t0) + " ms)");
    return opts;
}

} // namespace

namespace pmui::web_console {

bool available()
{
    return !getenv_w(L"PM_CONSOLE_URL").empty() || !find_shell_html().empty();
}

CWebViewOptions MakeDockOptions(bool devTools)
{
    return make_options(CWebViewOptions::Mode::Docked, devTools);
}

CWebViewOptions MakePopupOptions(bool devTools)
{
    CWebViewOptions opts = make_options(CWebViewOptions::Mode::Popup, devTools);
    opts.allowWebResizeHostWindow = false;
    opts.close_on_esc = true;
    return opts;
}

} // namespace pmui::web_console

CWebConsoleContainer::CWebConsoleContainer()
{
    const DWORD64 t0 = console_now_ms();
    logger::trace("[console-open] CWebConsoleContainer ctor begin");
    SetTabText(L"Console");
    SetDockCaption(L"Console");
    auto opts = pmui::web_console::MakeDockOptions(/*devTools=*/true);
    opts.onMessage = [this](const std::string& json_utf8) {
        HandleWebMessage(json_utf8);
    };
    m_view.SetOptions(std::move(opts));
    SetView(m_view);
    logger::trace(std::string("[console-open] CWebConsoleContainer ctor end ")
        + std::to_string(console_now_ms() - t0) + " ms");
}

CWebConsoleContainer::~CWebConsoleContainer()
{
    SetBusManager(nullptr);
    CloseAllSessions();
}

void CWebConsoleContainer::SetBusManager(CWebViewManager* manager)
{
    if (m_busManager == manager)
        return;
    if (m_busManager)
        m_busManager->UnregisterExternal("console");
    m_busManager = manager;
    if (m_busManager)
        m_busManager->RegisterExternal("console", &m_view);
}

void CWebConsoleContainer::RefreshTabTheme()
{
    RefreshChromeForTheme();
}

void CWebConsoleContainer::RefreshChromeForTheme()
{
    if (IsWindow()) {
        const auto& pal = pmui::theme_palette();
        SetBlankPageColor(pal.web_surface_bg);
        SetPadding(DpiScaleInt(1), 0);
        ::InvalidateRect(GetHwnd(), nullptr, TRUE);
    }
    m_view.RefreshChromeForTheme();
}

void CWebConsoleContainer::PreCreate(CREATESTRUCT& cs)
{
    CDockContainer::PreCreate(cs);
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    cs.style &= ~(WS_BORDER | WS_DLGFRAME);
}

LRESULT CWebConsoleContainer::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        if (msg == WM_ERASEBKGND) {
            HDC hdc = reinterpret_cast<HDC>(wparam);
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
        if (msg == UWM_CONSOLE_PTY_EVENT) {
            std::unique_ptr<ConsolePtyEvent> ev(reinterpret_cast<ConsolePtyEvent*>(lparam));
            if (!ev)
                return 0;
            switch (ev->kind) {
            case ConsolePtyEventKind::Output:
                PostShellOutput(std::move(ev->shell_id), std::move(ev->text));
                break;
            case ConsolePtyEventKind::Exit:
                m_sessions.erase(ev->shell_id);
                PostShellExit(std::move(ev->shell_id), ev->code);
                break;
            case ConsolePtyEventKind::Error:
                PostShellError(std::move(ev->shell_id), std::move(ev->text));
                break;
            }
            (void)wparam;
            return 0;
        }
        return CDockContainerBase::WndProc(msg, wparam, lparam);
    } catch (...) {}
    return 0;
}

void CWebConsoleContainer::HandleWebMessage(const std::string& json_utf8)
{
    logger::trace(std::string("[console-web] msg @") + std::to_string(console_now_ms())
        + "ms: " + json_utf8.substr(0, 1200));

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_utf8);
    } catch (...) {
        return;
    }

    const std::string t = j.value("t", std::string{});
    if (t == "cweb_console")
        return;
    if (t == "cweb_bus") {
        if (m_busManager)
            m_busManager->HandleExternalMessage("console", json_utf8);
        return;
    }

    if (t == "shell_open_link") {
        open_shell_link(j.value("path", std::string{}));
        return;
    }
    if (t == "shell_open_url") {
        const std::string url = j.value("url", std::string{});
        if (j.value("inApp", false))
            open_shell_url_in_app(GetHwnd(), url);
        else
            open_shell_url_external(url);
        return;
    }

    const std::string shell_id = j.value("shellId", std::string{});
    if (shell_id.empty())
        return;

    auto ensure_session = [&]() -> pm::pty::PtySession* {
        if (auto it = m_sessions.find(shell_id); it != m_sessions.end())
            return it->second.get();

        pm::pty::SpawnOptions opts;
        opts.shell_id = shell_id;
        opts.shell_type = j.value("shellType", std::string{"powershell"});
        opts.cols = j.value("cols", 80);
        opts.rows = j.value("rows", 24);
        opts.one_shot = j.value("oneShot", false);
        try {
            opts.cwd = fs::current_path().wstring();
        } catch (...) {
        }

        HWND hwnd = GetHwnd();
        auto post_event = [hwnd](ConsolePtyEvent* ev) {
            if (!ev)
                return;
            if (!hwnd || !::IsWindow(hwnd) ||
                !::PostMessageW(hwnd, UWM_CONSOLE_PTY_EVENT, 0, reinterpret_cast<LPARAM>(ev))) {
                delete ev;
            }
        };

        pm::pty::EventSink sink;
        sink.on_output = [post_event](const std::string& id, const std::string& data) {
            auto* ev = new ConsolePtyEvent;
            ev->kind = ConsolePtyEventKind::Output;
            ev->shell_id = id;
            ev->text = data;
            post_event(ev);
        };
        sink.on_exit = [post_event](const std::string& id, int code) {
            auto* ev = new ConsolePtyEvent;
            ev->kind = ConsolePtyEventKind::Exit;
            ev->shell_id = id;
            ev->code = code;
            post_event(ev);
        };
        sink.on_error = [post_event](const std::string& id, const std::string& error) {
            auto* ev = new ConsolePtyEvent;
            ev->kind = ConsolePtyEventKind::Error;
            ev->shell_id = id;
            ev->text = error;
            post_event(ev);
        };

        auto session = pm::pty::create_session(opts, std::move(sink));
        if (!session)
            return nullptr;

        auto [it, inserted] = m_sessions.emplace(shell_id, std::move(session));
        (void)inserted;
        return it->second.get();
    };

    if (t == "shell_start") {
        if (ensure_session())
            PostShellReady(shell_id);
        else
            PostShellError(shell_id, "Failed to start shell session");
        return;
    }
    if (t == "shell_data") {
        if (auto* s = ensure_session())
            s->write(j.value("data", std::string{}));
        return;
    }
    if (t == "shell_paste") {
        const std::string text = read_clipboard_text_utf8(GetHwnd());
        if (!text.empty()) {
            if (auto* s = ensure_session())
                s->write(text);
        }
        return;
    }
    if (t == "shell_resize") {
        if (auto* s = ensure_session())
            s->resize(j.value("cols", 80), j.value("rows", 24));
        return;
    }
    if (t == "shell_close" || t == "shell_kill") {
        if (auto it = m_sessions.find(shell_id); it != m_sessions.end()) {
            it->second->close();
            m_sessions.erase(it);
        }
        PostShellExit(shell_id, 0);
        return;
    }
}

void CWebConsoleContainer::PostShellReady(const std::string& shell_id)
{
    nlohmann::json out;
    out["t"] = "shell_ready";
    out["shellId"] = shell_id;
    m_view.PostToWeb(out.dump());
}

void CWebConsoleContainer::PostShellOutput(std::string shell_id, std::string data)
{
    nlohmann::json out;
    out["t"] = "shell_output";
    out["shellId"] = std::move(shell_id);
    out["data"] = std::move(data);
    m_view.PostToWeb(out.dump());
}

void CWebConsoleContainer::PostShellExit(std::string shell_id, int code)
{
    logger::trace(std::string("[console-pty] exit shell=") + shell_id + " code=" + std::to_string(code));
    nlohmann::json out;
    out["t"] = "shell_exit";
    out["shellId"] = std::move(shell_id);
    out["code"] = code;
    m_view.PostToWeb(out.dump());
}

void CWebConsoleContainer::PostShellError(std::string shell_id, std::string error)
{
    logger::error(std::string("[console-pty] ") + error);
    PostShellOutput(std::move(shell_id), "\r\n\x1b[31m" + error + "\x1b[0m\r\n");
}

void CWebConsoleContainer::CloseAllSessions()
{
    for (auto& kv : m_sessions) {
        if (kv.second)
            kv.second->close();
    }
    m_sessions.clear();
}

CDockWebConsole::CDockWebConsole()
{
    SetView(m_container);
}
