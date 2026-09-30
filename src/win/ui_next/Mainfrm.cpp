// CMainFrame — frame core, ribbon dispatch, WndProc, UWM handlers.
// Heavy responsibilities have been extracted:
//   Mainfrm_layout.cpp   — dock topology (build / restore / reset / save / load)
//   Mainfrm_commands.cpp — user actions (add files, resize, AI transform, save-as)
//   Mainfrm_presets.cpp  — prompt preset management + menu
//   Mainfrm_Web.cpp      — WebView2 popups and shared CWebViewManager bus wiring
#include "stdafx.h"
#include "constants.hpp"
#include <cstdint>
#include <filesystem>
#ifndef FEATURE_CUSTOM_COMMANDS
#define FEATURE_CUSTOM_COMMANDS 0
#endif
#if (defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH) \
    || (defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE)
#  include "lib/pm_zitadel_oauth.hpp"
#endif
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
#  include "lib/pm_service_posts.hpp"
#endif
#include "Mainfrm.h"
#include <string>
#include "DuplicatePanel.h"
#include "ProviderDlg.h"
#include "AppSettingsDlg.h"
#include "ChatWebResource.h"
#include "file_extensions.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/chat_context_attach.hpp"
#include "helpers/default_shell.hpp"
#include "helpers/ui_font.hpp"
#include "helpers/theme.hpp"
#include "win/app_commands.hpp"
#if defined(FEATURE_VIEWER_WEB)
#  include "win/viewers/text/ViewerWebPanel.h"
#endif
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
#  include "win/pixlwiz_login_spawn.hpp"
#endif
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
#  include "PixlwizSharePostDlg.h"
#endif
#include "win/session_replay/session_replay.hpp"
#include "win/session_replay/session_input_hooks.hpp"
#if defined(FEATURE_SESSION_VIDEO_RECORDER)
#  include "session/recorder/pm_session_video_recorder.hpp"
#  include <KnownFolders.h>
#  include <ShlObj.h>
#  include <iomanip>
#  include <sstream>
#  pragma comment(lib, "Shell32.lib")
#endif
#include "win/screenshot.hpp"
#include "win/settings_store.hpp"
#include "helpers/settings_panel_i18n.hpp"
#include "ui_log_file.hpp"
#include "log_sink.h"
#include "win/ui_singleton.hpp"
#include "helpers/preview_file_info.hpp"
#include "helpers/splash_window.hpp"
#include "core/command_variables.hpp"
#include "core/duplicates.hpp"
#include "logger/logger.h"
#ifdef FEATURE_USE_OWN_RIBBON
#include "OwnRibbonLayout.h"
#endif
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <fstream>
#include <sstream>
#include <vector>
#include <dwmapi.h>
#include <psapi.h>
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Psapi.lib")
#ifndef FEATURE_USE_OWN_RIBBON
#include <UIRibbon.h>
#endif

#pragma comment(lib, "Comctl32.lib")

namespace fs = std::filesystem;
using json = nlohmann::json;

using pmui::wide_to_utf8;
using pmui::utf8_to_wide;

#ifndef MOD_NOREPEAT
#  define MOD_NOREPEAT 0x4000
#endif

namespace {

// RegisterHotKey IDs (WM_HOTKEY wParam) — used while the main frame is *foreground* so
// shortcuts still fire when hosted controls (WebView2 / Chromium) eat WM_KEY* before the queue.
constexpr int kHkAltP_Screenshot     = 9010;
constexpr int kHkCtrlR_SessionRec    = 9011;
constexpr int kHkCtrlH_SessionStop = 9012;
constexpr int kHkCtrlAltR_Video      = 9013;
constexpr int kHkCtrlAltH_VideoStop  = 9014;
constexpr int kHkCtrlAltP_VideoPause = 9017;
constexpr int kHkF11_Fullscreen      = 9015;
constexpr int kHkAltF_Fullscreen     = 9016;
constexpr int kHkFirst               = kHkAltP_Screenshot;
constexpr int kHkLast                = kHkCtrlAltP_VideoPause;
constexpr UINT UWM_CONSOLE_OPEN_URL  = WM_APP + 92;

// Must match `IDW_MAIN` → File popup order in `Resource.rc` / `Resource_ui_i18n.rc`.
constexpr int kFileMenuRecentFilesSubmenuPos   = 2;
constexpr int kFileMenuRecentFoldersSubmenuPos = 3;

#if FEATURE_CUSTOM_COMMANDS
std::wstring quote_process_arg(std::wstring_view arg)
{
    if (arg.empty())
        return L"\"\"";
    const bool needsQuote = arg.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
    if (!needsQuote)
        return std::wstring(arg);
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

void ribbon_trace_resolve(const std::string& id, const std::string& field, std::wstring_view src, const std::wstring& final)
{
    if (src.empty() && final.empty())
        return;
    const std::string srcUtf8 = wide_to_utf8(std::wstring(src));
    const std::string finalUtf8 = wide_to_utf8(final);
    if (srcUtf8 == finalUtf8)
        logger::trace("[ribbon-custom] " + id + " " + field + " = " + srcUtf8);
    else
        logger::trace("[ribbon-custom] " + id + " " + field + ": " + srcUtf8 + " => " + finalUtf8);
}

void ribbon_trace_launch(const own_ribbon::CustomRibbonCommand& custom,
                         const char* mode,
                         std::wstring_view cmdLine)
{
    logger::trace(std::string("[ribbon-custom] ") + custom.id + " launch " + mode
        + " cwd=" + wide_to_utf8(custom.externalCwd)
        + " cmd=" + wide_to_utf8(std::wstring(cmdLine)));
}

own_ribbon::CustomRibbonCommand resolve_custom_command_vars(const own_ribbon::CustomRibbonCommand& custom,
                                                            const media::commands::VariableContext& context)
{
    logger::trace("[ribbon-custom] " + custom.id + " resolve begin");
    logger::trace("[ribbon-custom] " + custom.id + " context cwd=" + context.cwd
        + " source=" + context.source_file
        + " selection=" + std::to_string(context.selection_paths.size()));
    for (size_t i = 0; i < context.selection_paths.size(); ++i) {
        logger::trace("[ribbon-custom] " + custom.id + " context selection[" + std::to_string(i) + "]="
            + context.selection_paths[i]);
    }

    own_ribbon::CustomRibbonCommand resolved = custom;
    auto vars = media::commands::make_variable_map(context);
    auto resolve_field = [&](const std::string& field, std::wstring_view src) -> std::wstring {
        if (src.empty())
            return std::wstring{};
        std::string err;
        const std::string srcUtf8 = wide_to_utf8(std::wstring(src));
        const std::string out = media::commands::resolve_variables(srcUtf8, vars, &err);
        if (!err.empty())
            logger::warn("[ribbon-custom] variable resolve failed: " + custom.id + " " + field + " err=" + err);
        const std::wstring resolvedWide = utf8_to_wide(out);
        ribbon_trace_resolve(custom.id, field, src, resolvedWide);
        return resolvedWide;
    };
    resolved.externalCwd = resolve_field("externalCwd", custom.externalCwd);
    if (!resolved.externalCwd.empty())
        vars["CWD"] = wide_to_utf8(resolved.externalCwd);
    resolved.externalShellLine = resolve_field("externalShellLine", custom.externalShellLine);
    resolved.externalCommand = resolve_field("externalCommand", custom.externalCommand);
    for (size_t i = 0; i < custom.externalArgs.size(); ++i)
        resolved.externalArgs[i] = resolve_field("externalArgs[" + std::to_string(i) + "]", custom.externalArgs[i]);
    if (!resolved.externalCwd.empty()) {
        for (size_t i = 0; i + 1 < resolved.externalArgs.size(); ++i) {
            if (resolved.externalArgs[i] == L"--cwd") {
                const std::wstring before = resolved.externalArgs[i + 1];
                resolved.externalArgs[i + 1] = resolved.externalCwd;
                ribbon_trace_resolve(custom.id, "externalArgs[--cwd]", before, resolved.externalCwd);
                break;
            }
        }
    }
    resolved.openUrl = resolve_field("openUrl", custom.openUrl);
    resolved.openPath = resolve_field("openPath", custom.openPath);
    logger::trace("[ribbon-custom] " + custom.id + " resolve end");
    return resolved;
}

std::vector<std::string> wide_paths_to_utf8(const std::vector<std::wstring>& paths)
{
    std::vector<std::string> out;
    out.reserve(paths.size());
    for (const auto& path : paths) {
        if (!path.empty())
            out.push_back(wide_to_utf8(path));
    }
    return out;
}

std::wstring custom_console_command_line(const own_ribbon::CustomRibbonCommand& custom);

bool launch_custom_external_command(const own_ribbon::CustomRibbonCommand& custom)
{
    if (custom.runNewShellWindow) {
        // Direct console spawn for native executables. pm-image-cli is CONSOLE subsystem;
        // CREATE_NEW_CONSOLE makes it the console host so CTRL_CLOSE_EVENT stops recording.
        // PowerShell -NoExit kept the child alive as an orphan after the host console closed.
        if (!custom.externalShellMode && !custom.externalCommand.empty()) {
            std::wstring cmdLine = quote_process_arg(custom.externalCommand);
            for (const auto& arg : custom.externalArgs) {
                cmdLine.push_back(L' ');
                cmdLine += quote_process_arg(arg);
            }
            if (!custom.closeOnExit) {
                cmdLine += L" --pause-on-exit";
            }
            ribbon_trace_launch(custom, "newConsole", cmdLine);
            std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
            buf.push_back(L'\0');
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            const wchar_t* cwd = custom.externalCwd.empty() ? nullptr : custom.externalCwd.c_str();
            if (!::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                                  CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT, nullptr, cwd, &si, &pi)) {
                logger::warn("[ribbon-custom] console window failed: " + custom.id + " err=" + std::to_string(::GetLastError()));
                return true;
            }
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
            return true;
        }
        const std::wstring script = custom_console_command_line(custom);
        if (script.empty())
            return false;
        std::wstring cmdLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass ";
        if (!custom.closeOnExit)
            cmdLine += L"-NoExit ";
        cmdLine += L"-Command ";
        cmdLine += quote_process_arg(script);
        ribbon_trace_launch(custom, "newShellWindow", cmdLine);
        std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
        buf.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        const wchar_t* cwd = custom.externalCwd.empty() ? nullptr : custom.externalCwd.c_str();
        if (!::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                              CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT, nullptr, cwd, &si, &pi)) {
            logger::warn("[ribbon-custom] shell window failed: " + custom.id + " err=" + std::to_string(::GetLastError()));
            return true;
        }
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        return true;
    }
    if (custom.externalShellMode && !custom.externalShellLine.empty()) {
        std::wstring cmdLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ";
        std::wstring script = custom.externalShellLine;
        if (!custom.externalCwd.empty()) {
            script = L"Push-Location -LiteralPath " + quote_process_arg(custom.externalCwd)
                + L"; try { " + script + L" } finally { Pop-Location }";
        }
        cmdLine += quote_process_arg(script);
        ribbon_trace_launch(custom, "shell", cmdLine);
        std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
        buf.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
        if (!::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
            logger::warn("[ribbon-custom] shell command failed: " + custom.id + " err=" + std::to_string(::GetLastError()));
            return true;
        }
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        return true;
    }
    if (custom.externalCommand.empty())
        return false;
    std::wstring cmdLine = quote_process_arg(custom.externalCommand);
    for (const auto& arg : custom.externalArgs) {
        cmdLine.push_back(L' ');
        cmdLine += quote_process_arg(arg);
    }
    ribbon_trace_launch(custom, "process", cmdLine);
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const wchar_t* cwd = custom.externalCwd.empty() ? nullptr : custom.externalCwd.c_str();
    const DWORD flags = CREATE_NO_WINDOW;
    if (!::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, cwd, &si, &pi)) {
        logger::warn("[ribbon-custom] external command failed: " + custom.id + " err=" + std::to_string(::GetLastError()));
        return true;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

std::wstring custom_console_command_line(const own_ribbon::CustomRibbonCommand& custom)
{
    if (custom.externalShellMode && !custom.externalShellLine.empty()) {
        if (custom.externalCwd.empty())
            return custom.externalShellLine;
        return L"Push-Location -LiteralPath " + quote_process_arg(custom.externalCwd)
            + L"; try { " + custom.externalShellLine + L" } finally { Pop-Location }";
    }
    if (custom.externalCommand.empty())
        return {};
    // The internal console defaults to PowerShell on Windows. A quoted executable
    // path needs the call operator there, otherwise it is parsed as a string.
    std::wstring invocation = L"& ";
    invocation += quote_process_arg(custom.externalCommand);
    for (const auto& arg : custom.externalArgs) {
        invocation.push_back(L' ');
        invocation += quote_process_arg(arg);
    }
    if (custom.externalCwd.empty())
        return invocation;
    return L"Push-Location -LiteralPath " + quote_process_arg(custom.externalCwd)
        + L"; try { " + invocation + L" } finally { Pop-Location }";
}
#endif

std::wstring ellipsize_path_for_menu(const std::wstring& path, size_t maxChars)
{
    if (path.size() <= maxChars)
        return path;
    if (maxChars < 4)
        return path.substr(0, maxChars);
    return std::wstring(L"\x2026") + path.substr(path.size() - (maxChars - 1));
}

} // namespace

void CMainFrame::SyncRecentFilesMenuPopup(HMENU hPopup)
{
    while (::GetMenuItemCount(hPopup) > 0)
        ::RemoveMenu(hPopup, 0, MF_BYPOSITION);
    if (m_recentFiles.empty()) {
        ::AppendMenuW(hPopup, MF_STRING | MF_GRAYED, 0, L"(empty)");
        return;
    }
    for (int i = 0; i < static_cast<int>(m_recentFiles.size()); ++i) {
        const UINT     id    = static_cast<UINT>(IDM_RECENT_FILE_FIRST + i);
        const std::wstring t = ellipsize_path_for_menu(m_recentFiles[static_cast<size_t>(i)], 56);
        ::AppendMenuW(hPopup, MF_STRING, id, t.c_str());
    }
}

void CMainFrame::SyncRecentFoldersMenuPopup(HMENU hPopup)
{
    while (::GetMenuItemCount(hPopup) > 0)
        ::RemoveMenu(hPopup, 0, MF_BYPOSITION);
    if (m_recentFolders.empty()) {
        ::AppendMenuW(hPopup, MF_STRING | MF_GRAYED, 0, L"(empty)");
        return;
    }
    for (int i = 0; i < static_cast<int>(m_recentFolders.size()); ++i) {
        const UINT         id = static_cast<UINT>(IDM_RECENT_FOLDER_FIRST + i);
        const std::wstring t  = ellipsize_path_for_menu(m_recentFolders[static_cast<size_t>(i)], 56);
        ::AppendMenuW(hPopup, MF_STRING, id, t.c_str());
    }
}

HMENU CMainFrame::AppMenuHandle() const
{
    if (GetMenuBar().IsWindow()) {
        HMENU h = GetMenuBar().GetBarMenu();
        if (h)
            return h;
    }
    HMENU hFrame = ::GetMenu(*this);
    if (hFrame)
        return hFrame;
    HMENU hf = GetFrameMenu();
    return (hf && ::IsMenu(hf)) ? hf : nullptr;
}

bool CMainFrame::IsChatWorkbench() const
{
    return m_workbench.get() && std::strcmp(m_workbench->workbenchSettingsId(), "chat") == 0;
}

bool CMainFrame::IsViewerWorkbench() const
{
    return m_workbench.get() && std::strcmp(m_workbench->workbenchSettingsId(), "viewer") == 0;
}

// @todo : main frame : split base from app level handlers

// ── Constructor / frame init ──────────────────────────────────────────────────

CMainFrame::CMainFrame()
{
    m_viewerManager.Attach(m_fileViewer);
    m_viewerManager.AttachHost(m_viewerTabsHost);
    {
        std::string wbench, wberr;
        (void)media::settings::load_settings_ui_workbench_id(wbench, wberr);
        if (wbench == "chat")
            m_workbench = std::make_unique<pmui::CChatSimpleWorkbench>();
        else if (wbench == "viewer")
            m_workbench = std::make_unique<pmui::CViewerSimpleWorkbench>();
        else
            m_workbench = std::make_unique<pmui::CDefaultMainWorkbench>();
    }
#ifdef FEATURE_USE_OWN_RIBBON
    // Keep the Win32++ ReBar + CMenuBar (toolbar-style menu) so popups and the strip respect
    // `UseDarkMenu` + `CustomDrawMenuBar` — a classic `SetMenu` bar stays OS-drawn and light.
    // Place `m_ownRibbon` just below the ReBar (OnCreate / WM_SIZE / GetViewRect).
    UseToolBar(FALSE);
#endif
    try {
        m_settingsPath = media::settings::get_settings_json_path().string();
    } catch (...) {
        m_settingsPath = (fs::current_path() / "settings.json").string();
    }
    {
        std::string cherr;
        (void)media::settings::load_workbench_chrome(m_workbenchChrome, cherr, m_workbench->workbenchSettingsId());
    }
    // When false, Win32++ skips creating the status control (`wxx_frame` OnCreate).
    UseStatusBar(m_workbenchChrome.show_status_bar ? TRUE : FALSE);
    LoadPresets();
    pmui::ui_log_file_event("CMainFrame ctor after LoadPresets (parse settings for presets)");
}

void CMainFrame::LogMessage(const CString& msg)
{
    if (m_pDockLog && m_pDockLog->IsWindow() && m_pDockLog->GetLogContainer().GetLogView().IsWindow()) {
        m_pDockLog->GetLogContainer().GetLogView().AppendLine(msg);
        return;
    }
    if (!msg.IsEmpty()) {
        pmui::append_pm_image_log_file_line_wide(
            std::wstring(static_cast<LPCWSTR>(msg), static_cast<size_t>(msg.GetLength())));
    } else {
        pmui::append_pm_image_log_file_line_wide(std::wstring{});
    }
}

HWND CMainFrame::Create(HWND parent)
{
    m_workbench->AttachClientView(*this);
    LoadRegistrySettings(pm::brand::k_reg_ui_state_subpath_w);
    // workbench.*.chrome overrides Win32++ HKCU Frame Settings for initial status visibility.
    {
        auto v = GetInitValues();
        v.showStatusBar = m_workbenchChrome.show_status_bar ? TRUE : FALSE;
        SetInitValues(v);
    }
#ifdef FEATURE_USE_OWN_RIBBON
    return CDockFrame::Create(parent);
#else
    return CRibbonDockFrame::Create(parent);
#endif
}

// TRIAL: CWnd::Create only calls ShowWindow if cs.style has WS_VISIBLE (wxx_wincore.h);
// clearing it defers the first show to OnInitialUpdate (see Mainfrm_layout.cpp).
// CDockFrame::PreCreate is the CFrameT<CDocker> setup for both own-ribbon and UIRibbon hosts.
void CMainFrame::PreCreate(CREATESTRUCT& cs)
{
    CDockFrame::PreCreate(cs);
    cs.style &= ~WS_VISIBLE;
}

// Win32++ dock state: `settings.json` `workbench.main.win32_dock` (legacy: top-level `win32_dock` on load).
BOOL CMainFrame::LoadDockLayout()
{
    return m_workbench->LoadDockFromSettings(*this);
}

BOOL CMainFrame::SaveDockLayout()
{
    return m_workbench->SaveDockToSettings(*this);
}

BOOL CMainFrame::LoadDockContainers()
{
    return m_workbench->LoadDockContainersFromSettings(*this);
}

void CMainFrame::ApplyWorkbenchFrameChromeOnce()
{
    if (m_workbenchFrameChromeApplied)
        return;
    m_workbenchFrameChromeApplied = true;
    if (GetReBar().IsWindow())
        ShowMenu(m_workbenchChrome.show_main_menu ? TRUE : FALSE);
    if (GetStatusBar().IsWindow())
        ShowStatusBar(m_workbenchChrome.show_status_bar ? TRUE : FALSE);
    RecalcLayout();
}

// ── Appearance (theme + font) ─────────────────────────────────────────────────
//
// Applied once after OnInitialUpdate and again whenever the user clicks Save
// in the App Settings dialog.  Steps:
//   1. Re-read settings.json → refresh the cached pmui::ui_font + theme palette.
//   2. Set the dark titlebar via DWM, toggle UseDarkMenu on the frame.
//   3. Push the new caption colours into every dock panel.
//   4. Re-broadcast WM_SETTINGCHANGE so child controls (EDIT/COMBOBOX) repaint.
//   5. Resend WM_SETFONT to every panel tree (Settings / Find / Log / …).
//
void CMainFrame::ApplyAppearance(bool reread_settings)
{
    if (reread_settings) {
        pmui::ui_log_file_event("ApplyAppearance enter (re-reads font+theme, full panel refresh)");
        pmui::ui_font_init_from_settings();
        pmui::theme_init_from_settings();
    } else {
        pmui::ui_log_file_event("ApplyAppearance enter (cached font+theme, full panel refresh)");
    }
    const auto& pal = pmui::theme_palette();

    // Main status bar gradient + text — default frame uses light grey in all modes.
    {
        StatusBarTheme sbt{};
        sbt.UseThemes = TRUE;
        sbt.clrBkgnd1 = sbt.clrBkgnd2 = pal.window_bg;
        sbt.clrText   = pal.window_fg;
        SetStatusBarTheme(sbt);
    }

    // Top-level frame
    pmui::apply_dark_titlebar(GetHwnd(), pal.dark);
    UseDarkMenu(pal.dark ? TRUE : FALSE);

#ifdef FEATURE_USE_OWN_RIBBON
    {
        // Owner-drawn popups: flat gutter + accent selection (Win32 defaults use a blue gradient
        // gutter and `#000` fill — clashes with our palette). Do not call SetTheme() — it resets
        // statusbar/rebar colours we already customised above.
        Win32xx::MenuTheme mt{};
        mt.UseThemes = TRUE;
        if (pal.dark) {
            mt.clrText      = pal.window_fg;
            mt.clrOutline   = pal.caption_pen;
            mt.clrHot1      = pal.accent;
            mt.clrHot2      = pal.accent;
            mt.clrPressed1  = pal.control_bg;
            mt.clrPressed2  = pal.control_bg;
        } else {
            // Win11 preset from Win32++ `CFrameT::SetTheme` (menu strip only).
            mt.clrHot1      = RGB(180, 250, 255);
            mt.clrHot2      = RGB(140, 190, 255);
            mt.clrPressed1  = RGB(240, 250, 255);
            mt.clrPressed2  = RGB(120, 170, 220);
            mt.clrOutline   = RGB(127, 127, 255);
            mt.clrText      = RGB(0, 0, 0);
        }
        SetMenuTheme(mt);
        {
            Win32xx::ReBarTheme rbt{};
            rbt.UseThemes     = TRUE;
            rbt.FlatStyle     = TRUE;
            rbt.BandsLeft     = TRUE;
            rbt.LockMenuBand  = TRUE;
            rbt.RoundBorders  = FALSE;
            rbt.ShortBands    = FALSE;
            rbt.UseLines      = FALSE;
            if (pal.dark) {
                rbt.clrBkgnd1 = rbt.clrBkgnd2 = pal.window_bg;
                rbt.clrBand1  = rbt.clrBand2  = pal.window_bg;
            } else {
                rbt.clrBkgnd1 = rbt.clrBkgnd2 = RGB(235, 237, 250);
                rbt.clrBand1  = rbt.clrBand2  = RGB(235, 237, 250);
            }
            SetReBarTheme(rbt);
        }
        if (IsWindow()) {
            ::DrawMenuBar(GetHwnd());
            if (GetReBar().IsWindow())
                GetReBar().Invalidate();
        }
    }
#endif

    // Process-level dark-mode opt-in is always on; the per-window state below
    // toggles each control between Light/Dark via SetWindowTheme.
    pmui::enable_app_dark_mode(true);

#ifndef FEATURE_USE_OWN_RIBBON
    // UIRibbon lives under the main frame (not inside a docker) — walk the entire
    // HWND tree so NetUI / ribbon hosts get AllowDarkModeForWindow; otherwise the
    // ribbon often stays bright on Win10/11 while docks respect app dark mode.
#endif
    pmui::allow_dark_mode_for_window_tree(GetHwnd(), pal.dark);

    // Floating tool windows (undocked docks) keep the default light title bar
    // unless we sync DWM immersive dark on each docker HWND.
    for (const Win32xx::DockPtr& ptr : GetAllDockChildren()) {
        if (!ptr || !ptr->IsWindow()) continue;
        if (ptr->IsUndocked())
            pmui::apply_dark_titlebar(ptr->GetHwnd(), pal.dark);
    }

    // Registry restores may set DS_CLIENTEDGE; Win32++'s DockBar::SendNotify
    // still nudges drag coordinates when that bit is set even though DockClient
    // PreCreate strips the visual edge — skews splitter math / hit-testing.
    for (const Win32xx::DockPtr& ptr : GetAllDockChildren()) {
        if (!ptr || !ptr->IsWindow()) continue;
        DWORD ds = ptr->GetDockStyle();
        if (ds & Win32xx::DS_CLIENTEDGE)
            ptr->SetDockStyle(ds & static_cast<DWORD>(~Win32xx::DS_CLIENTEDGE));
    }

    // Dock captions + bar colour for every panel (no InvalidateRect here — refreshPanel
    // below RedrawWindow's the same HWND and a prior erase was causing a double flash).
    auto styleDocker = [&](CDocker* d) {
        if (!d) return;
        const COLORREF activeCaptionBg = IsViewerTabChromeDocker(d)
            ? pal.caption_bg_inactive
            : pal.caption_bg;
        d->SetCaptionColors(pal.caption_fg, activeCaptionBg,
                            pal.caption_fg_inactive, pal.caption_bg_inactive,
                            pal.caption_pen);
        d->SetBarColor(pal.bar);
    };
    RefreshViewerDockTabs();
    styleDocker(GetDockAncestor());
    styleDocker(m_pDockQueue);
    styleDocker(m_pDockLog);
    styleDocker(m_pDockSettings);
    styleDocker(m_pDockFindResults);
    styleDocker(m_pDockDuplicateResults);
    styleDocker(m_pDockFileTree);
    styleDocker(m_pDockViewerPanel);
    for (CDockViewerPanel* panel : m_viewerDockTabs)
        styleDocker(panel);
#ifdef FEATURE_NODES
    styleDocker(m_pDockNodes);
#endif
#ifdef FEATURE_CHAT_WEB
    styleDocker(m_pDockChatWeb);
#endif
#ifdef FEATURE_CONSOLE
    styleDocker(m_pDockConsole);
#endif

    // Re-apply the UI font + force a full repaint to every panel hierarchy
    // (settings combos / find list / log edit / file info / queue list).
    // TRIAL: dark mode — skip RDW_ERASE to reduce a white/flash erase before themed paint; revert if
    // any child leaves stale bits (rare; eyeball all docks after theme/font change).
    const UINT k_refresh_rw = (UINT)(RDW_INVALIDATE | RDW_ALLCHILDREN
                                     | (pal.dark ? 0U : (UINT)RDW_ERASE));
    auto refreshPanel = [&](CDocker* d) {
        if (!d) return;
        pmui::apply_font_to_tree(d->GetHwnd());
        // Invalidate all descendants without RDW_UPDATENOW — synchronous
        // repaints here made theme/font changes feel like a "global flicker"
        // and could dismiss open combo dropdowns (activation + paint order).
        ::RedrawWindow(d->GetHwnd(), nullptr, nullptr, k_refresh_rw);
    };
    refreshPanel(m_pDockQueue);
    refreshPanel(m_pDockLog);
    refreshPanel(m_pDockSettings);
    if (m_pDockSettings && m_pDockSettings->GetSettingsContainer().GetSettingsView().GetHwnd())
        m_pDockSettings->GetSettingsContainer().GetSettingsView().RelayoutForUiFont();
    refreshPanel(m_pDockFindResults);
    refreshPanel(m_pDockDuplicateResults);
    refreshPanel(m_pDockViewerPanel);
    for (CDockViewerPanel* panel : m_viewerDockTabs)
        refreshPanel(panel);
#ifdef FEATURE_CHAT_WEB
    refreshPanel(m_pDockChatWeb);
#endif
#ifdef FEATURE_CONSOLE
    refreshPanel(m_pDockConsole);
#endif

    // Push the right uxtheme parts to every standard child control so
    // BUTTON/EDIT/COMBOBOX/LISTVIEW/etc. actually paint dark (without this
    // the visual style ignores our WM_CTLCOLOR* overrides).
    //
    // EXCLUDED: m_pDockFileTree. The Explorer panel hosts an IExplorerBrowser
    // (Shell COM) which creates its own child windows (file list, header,
    // breadcrumb bar). Pushing SetWindowTheme / stripping WS_EX_CLIENTEDGE on
    // those Shell-owned children disturbs their internal hit-testing and
    // leaves the splitter on the Explorer side un-draggable. The Shell already
    // honours the OS dark mode natively, so we skip our extra theming there.
    auto themePanel = [&](CDocker* d) {
        if (!d) return;
        pmui::apply_window_theme_recursive(d->GetHwnd(), pal.dark);
    };
    themePanel(m_pDockQueue);
    themePanel(m_pDockLog);
    themePanel(m_pDockSettings);
    themePanel(m_pDockFindResults);
    themePanel(m_pDockDuplicateResults);
    themePanel(m_pDockViewerPanel);
    for (CDockViewerPanel* panel : m_viewerDockTabs)
        themePanel(panel);
#ifdef FEATURE_NODES
    themePanel(m_pDockNodes);
#endif
#ifdef FEATURE_CHAT_WEB
    themePanel(m_pDockChatWeb);
    if (m_pDockChatWeb)
        m_pDockChatWeb->GetChatWebContainer().GetChatWebView().RefreshChromeForTheme();
    if (m_pDockChatWeb && reread_settings) {
        media::settings::AppearanceSettings chat_lang_a{};
        std::string                            chat_err;
        if (media::settings::load_appearance(chat_lang_a, chat_err) && !chat_lang_a.display_language.empty())
            m_pDockChatWeb->GetChatWebContainer().GetChatWebView().SetDisplayLanguage(
                chat_lang_a.display_language);
    }
    if (IsChatWorkbench() && m_workbenchClientChatWeb.IsWindow()) {
        pmui::apply_window_theme_recursive(m_workbenchClientChatWeb.GetHwnd(), pal.dark);
        m_workbenchClientChatWeb.RefreshChromeForTheme();
        if (reread_settings) {
            media::settings::AppearanceSettings chat_lang_a{};
            std::string                         chat_err;
            if (media::settings::load_appearance(chat_lang_a, chat_err) && !chat_lang_a.display_language.empty())
                m_workbenchClientChatWeb.SetDisplayLanguage(
                    chat_lang_a.display_language);
        }
    }
#endif
#ifdef FEATURE_CONSOLE
    themePanel(m_pDockConsole);
    if (m_pDockConsole)
        m_pDockConsole->GetConsoleContainer().RefreshChromeForTheme();
#endif
    if (IsChatWorkbench() && m_workbenchClientChatWeb.IsWindow())
        pmui::apply_window_theme_recursive(m_workbenchClientChatWeb.GetHwnd(), pal.dark);
#ifdef FEATURE_BROWSER
    m_webViews.RefreshThemeFromHost();
#endif

    // Listview-class panels need their non-paint-message colours re-applied.
    if (m_pDockQueue)
        m_pDockQueue->GetQueueContainer().GetListView().RefreshThemeColors();
    if (m_pDockFindResults)
        m_pDockFindResults->GetFindResultsContainer().GetView().RefreshThemeColors();
    if (m_pDockDuplicateResults)
        m_pDockDuplicateResults->GetDuplicateResultsContainer().GetView().RefreshThemeColors();

    // Refresh dock-container tab strips (Win32++'s default uses hard-coded
    // light RGB; CDockContainerBase::DrawTabs honours the theme palette).
    if (m_pDockQueue)       m_pDockQueue->GetQueueContainer().RefreshTabTheme();
    if (m_pDockLog)         m_pDockLog->GetLogContainer().RefreshTabTheme();
    if (m_pDockSettings)    m_pDockSettings->GetSettingsContainer().RefreshTabTheme();
    if (m_pDockFindResults) m_pDockFindResults->GetFindResultsContainer().RefreshTabTheme();
    if (m_pDockDuplicateResults) m_pDockDuplicateResults->GetDuplicateResultsContainer().RefreshTabTheme();
    if (m_centerViewerTabs.IsWindow()) m_centerViewerTabs.RefreshTabTheme();
    for (CDockViewerPanel* panel : m_viewerDockTabs) {
        if (panel && panel->GetViewerPanelContainer().IsWindow())
            panel->GetViewerPanelContainer().RefreshTabTheme();
    }
    if (m_pDockFileTree) {
        m_pDockFileTree->GetFileTreeContainer().RefreshTabTheme();
        m_pDockFileTree->GetFileTreeContainer().GetBrowserView().RefreshThemeChrome();
    }
#ifdef FEATURE_CHAT_WEB
    if (m_pDockChatWeb)     m_pDockChatWeb->GetChatWebContainer().RefreshTabTheme();
#endif
#ifdef FEATURE_CONSOLE
    if (m_pDockConsole)     m_pDockConsole->GetConsoleContainer().RefreshTabTheme();
#endif
    if (m_viewerManager.ActiveView().IsWindow() && !IsChatWorkbench())
        m_viewerManager.ActiveView().RefreshThemeChrome();
    refreshPanel(m_pDockFileTree);
#ifdef FEATURE_NODES
    refreshPanel(m_pDockNodes);
#endif

#ifdef FEATURE_USE_OWN_RIBBON
    if (m_ownRibbon.IsWindow()) {
        // Walk children first, then ApplyChrome last — `ApplyChrome` pins toolbar/pager
        // `SetWindowTheme("")` so `NMTBCUSTOMDRAW` label colours win in dark mode (the recursive
        // pass would otherwise re-apply `DarkMode_Explorer` to `ToolbarWindow32` and force black ink).
        pmui::apply_font_to_tree(m_ownRibbon.GetHwnd());
        pmui::apply_window_theme_recursive(m_ownRibbon.GetHwnd(), pal.dark);
        pmui::allow_dark_mode_for_window_tree(m_ownRibbon.GetHwnd(), pal.dark);
        m_ownRibbon.ApplyChrome(pal.dark, pal.window_bg);
        SyncViewMenuChecks();
        m_ownRibbon.m_strip.SyncToggle(IDC_CMD_TOGGLE_THEME, pal.dark);
        m_ownRibbon.SyncBatchControls(*this);
    }
#endif

    // Repaint frame chrome (status bar, etc.). NOTE: do NOT call
    // CFrame::RecalcLayout() here — colour/font changes don't change panel
    // sizes, and triggering a frame relayout while Win32++'s dock state was
    // already settled breaks splitter hit-testing on the floating bars.
    ReapplyCachedStatusBarTexts();
    ::InvalidateRect(GetHwnd(), nullptr, TRUE);
    pmui::ui_log_file_event("ApplyAppearance leave");
}

void CMainFrame::ToggleGlobalThemeOverride()
{
    media::settings::AppearanceSettings app{};
    std::string err;
    media::settings::load_appearance(app, err);

    const bool wasDark = pmui::theme_palette().dark;
    app.theme = wasDark ? media::settings::Theme::Light : media::settings::Theme::Dark;
    if (!media::settings::save_appearance(app, err)) {
        logger::warn(std::string("[theme] toggle failed: ") + (err.empty() ? "save_appearance failed" : err));
        return;
    }

    ApplyAppearance();
    InvalidateToggle(IDC_CMD_TOGGLE_THEME);
}

#ifdef FEATURE_USE_OWN_RIBBON
void CMainFrame::DrawMenuItemBkgnd(LPDRAWITEMSTRUCT pDrawItem)
{
    // Win32++ uses `#000` for dark menu rows — swap to [`ThemePalette::control_bg`] so popups
    // match docks; selection still uses [`MenuTheme::clrHot1`] from `SetMenuTheme` above.
    const auto& pal = pmui::theme_palette();
    if (pal.dark && IsUsingDarkMenu()) {
        const bool isDisabled = (pDrawItem->itemState & ODS_GRAYED) != 0;
        const bool isSelected = (pDrawItem->itemState & ODS_SELECTED) != 0;
        CRect       drawRect  = pDrawItem->rcItem;
        Win32xx::CDC drawDC(pDrawItem->hDC);

        if (isSelected && !isDisabled) {
            if (IsUsingThemes()) {
                const Win32xx::MenuTheme& mbt = GetMenuBarTheme();
                drawDC.CreateSolidBrush(mbt.clrHot1);
                drawDC.CreatePen(PS_SOLID, 1, mbt.clrOutline);
            } else {
                drawDC.CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
                drawDC.CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_BTNFACE));
            }
            drawDC.Rectangle(drawRect.left, drawRect.top, drawRect.right, drawRect.bottom);
        } else {
            drawRect.left = GetMenuMetrics().GetGutterRect(pDrawItem->rcItem).Width();
            drawDC.SolidFill(pal.control_bg, drawRect);
        }
        return;
    }

    Win32xx::CFrameT<Win32xx::CDocker>::DrawMenuItemBkgnd(pDrawItem);
}

LRESULT CMainFrame::CustomDrawMenuBar(NMHDR* pNMHDR)
{
    auto*            lp  = reinterpret_cast<NMTBCUSTOMDRAW*>(pNMHDR);
    const auto&      pal = pmui::theme_palette();
    if (pal.dark && pNMHDR->hwndFrom) {
        if (lp->nmcd.dwDrawStage == CDDS_PREPAINT) {
            CRect       cr;
            (void)::GetClientRect(pNMHDR->hwndFrom, &cr);
            Win32xx::CDC dc(lp->nmcd.hdc);
            dc.SolidFill(pal.window_bg, cr);
        } else if (lp->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            const UINT st = lp->nmcd.uItemState;
            if (!(st & (CDIS_HOT | CDIS_SELECTED))) {
                CRect rc = lp->nmcd.rc;
                if (IsUsingVistaMenu())
                    rc.InflateRect(0, -2);
                else
                    rc.InflateRect(0, -1);
                Win32xx::CDC dc(lp->nmcd.hdc);
                dc.SolidFill(pal.window_bg, rc);
            }
        }
    }
    return CDockFrame::CustomDrawMenuBar(pNMHDR);
}
#endif

#if defined(_WIN32)
void CMainFrame::SetScreenshotProbe(std::wstring out_path, int wait_ms, int win_w, int win_h)
{
    m_screenshotProbeOut    = std::move(out_path);
    m_screenshotProbeWaitMs = (std::max)(0, wait_ms);
    m_screenshotProbeWinW     = (win_w > 0 && win_h > 0) ? win_w : 0;
    m_screenshotProbeWinH     = (win_w > 0 && win_h > 0) ? win_h : 0;
}

void CMainFrame::SetSessionReplayPath(std::wstring session_json_path)
{
    m_sessionReplayPath = std::move(session_json_path);
}

void CMainFrame::SetLayoutOverridePath(std::wstring layout_json_path)
{
    m_layoutOverridePath = std::move(layout_json_path);
}

void CMainFrame::SetCliCwdStartupFolder(std::wstring cwd_path)
{
    pmui::ui_log_file_eventf(
        "SetCliCwdStartupFolder: old=\"%s\" new=\"%s\"",
        pmui::wide_to_utf8(m_cliCwdStartupFolder).c_str(),
        pmui::wide_to_utf8(cwd_path).c_str());
    m_cliCwdStartupFolder = std::move(cwd_path);
}

void CMainFrame::StartSessionRecording()
{
    if (m_sessionRecording) {
        LogMessage(L"[session] already recording (Ctrl+H to save and stop)");
        return;
    }
    m_sessionRecordOutPath = (media::win::session_replay::default_sessions_dir() /
                              media::win::session_replay::make_session_filename())
                                 .wstring();
    std::string hook_err;
    if (!media::win::session_replay::input::start_input_recording(GetHwnd(), hook_err)) {
        CString m(L"[session] input capture failed: ");
        m += CString(hook_err.c_str());
        LogMessage(m);
        m_sessionRecordOutPath.clear();
        return;
    }
    m_sessionRecording = true;
    CString m;
    m.Format(L"[session] recording \u2014 will write: %s (Ctrl+H or app recordstop to save)", m_sessionRecordOutPath.c_str());
    LogMessage(m);
    SetStatusBarPartText(0, L"Session record ON (Ctrl+H to stop)");
}

void CMainFrame::StopSessionRecording()
{
    if (!m_sessionRecording) {
        LogMessage(L"[session] not recording (Ctrl+R to start)");
        return;
    }
    nlohmann::json input_events;
    media::win::session_replay::input::stop_input_recording(input_events);
    m_sessionRecording = false;
#if defined(_WIN32)
    // Match WM_CLOSE: restore non-fullscreen rect, then flush dock topology to settings.json
    // before reading placement + dock sizes for the session file.
    if (m_isFullscreen)
        ToggleFullscreen();
    SaveDockLayout();
#endif
    media::settings::WindowLayout wl{};
    if (!FillWindowLayoutFromState(wl)) {
        LogMessage(L"[session] stop: could not read window placement");
        return;
    }
    nlohmann::json dock_snap = SnapshotDockLayoutForSession();
    std::string      err;
    nlohmann::json   root =
        media::win::session_replay::build_session_json(wl, GetHwnd(), &input_events, &dock_snap);
    if (!media::win::session_replay::write_session_file(std::filesystem::path(m_sessionRecordOutPath), root, err)) {
        CString m(L"[session] save failed: ");
        m += CString(err.c_str());
        LogMessage(m);
    } else {
        CString m;
        m.Format(L"[session] saved: %s", m_sessionRecordOutPath.c_str());
        LogMessage(m);
        SetStatusBarPartText(0, L"Drop files or use Add Files to begin.");
    }
    m_sessionRecordOutPath.clear();
}

#if defined(FEATURE_SESSION_VIDEO_RECORDER)
static std::filesystem::path make_session_video_mp4_path()
{
    using namespace std::chrono;
    const auto  t  = system_clock::to_time_t(system_clock::now());
    std::tm     tm = {};
    (void)localtime_s(&tm, &t);
    std::wostringstream w;
    w << L"session-" << std::put_time(&tm, L"%H%M%S") << L".mp4";
    const std::wstring        fname = w.str();
    std::filesystem::path     dir;
    PWSTR                     pVideos = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &pVideos)) && pVideos) {
        dir = pVideos;
        CoTaskMemFree(pVideos);
    } else
        dir = std::filesystem::current_path();
    return dir / fname;
}
#endif

void CMainFrame::StartSessionVideoRecording()
{
#if defined(FEATURE_SESSION_VIDEO_RECORDER)
    if (media::win::recorder::is_recording()) {
        if (media::win::recorder::is_paused()) {
            media::win::recorder::toggle_pause();
            SetStatusBarPartText(0, L"Session video ON (Ctrl+Alt+H stop, Ctrl+Alt+P pause)");
            return;
        }
        LogMessage(L"[session video] already recording (Ctrl+Alt+H to stop)");
        return;
    }
    const auto p   = make_session_video_mp4_path();
    std::string err;
    if (!media::win::recorder::start(GetHwnd(), p, err)) {
        CString m(L"[session video] start failed: ");
        m += CString(pmui::utf8_to_wide(err).c_str());
        LogMessage(m);
        return;
    }
    CString m;
    m.Format(L"[session video] recording to %s (Ctrl+Alt+H stop; Ctrl+Alt+P pause/resume; Ctrl+Alt+R resume when paused)", p.c_str());
    LogMessage(m);
    SetStatusBarPartText(0, L"Session video ON (Ctrl+Alt+H stop, Ctrl+Alt+P pause)");
#else
    LogMessage(
        L"[session video] not in this build (enable FEATURE_SESSION_VIDEO_RECORDER with MSVC and Windows SDK).");
#endif
}

void CMainFrame::StopSessionVideoRecording()
{
#if defined(FEATURE_SESSION_VIDEO_RECORDER)
    if (!media::win::recorder::is_recording()) {
        LogMessage(L"[session video] not recording (Ctrl+Alt+R to start)");
        return;
    }
    media::win::recorder::stop();
    LogMessage(L"[session video] stopped — file saved under your Videos folder (see log for full path).");
    SetStatusBarPartText(0, L"Session video saved");
#else
    LogMessage(
        L"[session video] not in this build (enable FEATURE_SESSION_VIDEO_RECORDER with MSVC and Windows SDK).");
#endif
}

void CMainFrame::ToggleSessionVideoPause()
{
#if defined(FEATURE_SESSION_VIDEO_RECORDER)
    media::win::recorder::toggle_pause();
    if (media::win::recorder::is_recording()) {
        if (media::win::recorder::is_paused())
            SetStatusBarPartText(0, L"Session video paused (Ctrl+Alt+P to resume)");
        else
            SetStatusBarPartText(0, L"Session video ON (Ctrl+Alt+H stop, Ctrl+Alt+P pause)");
    }
#else
    LogMessage(
        L"[session video] pause toggle: not in this build (enable FEATURE_SESSION_VIDEO_RECORDER with MSVC and Windows SDK).");
#endif
}

void CMainFrame::ApplySessionReplayFromPath()
{
    if (m_sessionReplayPath.empty())
        return;
    pmui::ui_log_file_eventf("SessionReplay: begin \"%s\"",
                            pmui::wide_to_utf8(m_sessionReplayPath).c_str());
    nlohmann::json j;
    std::string      err;
    if (!media::win::session_replay::read_session_file(std::filesystem::path(m_sessionReplayPath), j, err)) {
        CString m(L"[session] replay read failed: ");
        m += CString(err.c_str());
        LogMessage(m);
        pmui::ui_log_file_eventf("SessionReplay: read failed: %s", err.c_str());
        return;
    }
    const int ver = j.value("version", 0);
    if (ver != media::win::session_replay::kSessionFileVersion) {
        CString w;
        w.Format(L"[session] replay: file version %d (app expects %d) — trying anyway", ver, media::win::session_replay::kSessionFileVersion);
        LogMessage(w);
    }
    if (!j.contains("snapshot") || !j["snapshot"].contains("window_layout")) {
        LogMessage(L"[session] replay: missing snapshot.window_layout");
        pmui::ui_log_file_event("SessionReplay: abort — missing snapshot.window_layout");
        return;
    }
    media::settings::WindowLayout wl;
    if (!media::win::session_replay::json_to_window_layout(j["snapshot"]["window_layout"], wl, err)) {
        CString m(L"[session] replay: ");
        m += CString(err.c_str());
        LogMessage(m);
        pmui::ui_log_file_eventf("SessionReplay: abort — json_to_window_layout: %s", err.c_str());
        return;
    }
    if (!wl.filetree_folder.empty()) {
        pmui::ui_log_file_eventf("SessionReplay: ApplyWindowLayoutData filetree_folder=\"%s\"",
                                 wl.filetree_folder.c_str());
    } else
        pmui::ui_log_file_event("SessionReplay: ApplyWindowLayoutData (filetree_folder empty)");
    ApplyWindowLayoutData(wl);
    pmui::ui_log_file_eventf("SessionReplay: applied layout from \"%s\"",
                            pmui::wide_to_utf8(m_sessionReplayPath).c_str());
    CString ok(L"[session] replay: applied layout from ");
    ok += m_sessionReplayPath.c_str();
    LogMessage(ok);
}
#endif

LRESULT CMainFrame::OnDockActivated(UINT msg, WPARAM wparam, LPARAM lparam)
{
    PruneOrphanedViewerDockTabs();
    RefreshViewerDockTabs();

    // Keep docker caption bars aligned with the current palette. Some Win32++
    // dock activation/re-parent flows can restore default (light) caption colors.
    // Re-apply only caption/bar colors here (cheap) and avoid full ApplyAppearance().
    {
        const auto& pal = pmui::theme_palette();
        CDocker* root = GetDockAncestor();
        if (root && root->IsWindow()) {
            const COLORREF activeCaptionBg = IsViewerTabChromeDocker(root)
                ? pal.caption_bg_inactive
                : pal.caption_bg;
            root->SetCaptionColors(pal.caption_fg, activeCaptionBg,
                                   pal.caption_fg_inactive, pal.caption_bg_inactive,
                                   pal.caption_pen);
            root->SetBarColor(pal.bar);
        }
        for (const Win32xx::DockPtr& ptr : GetAllDockChildren()) {
            if (!ptr || !ptr->IsWindow())
                continue;
            const COLORREF activeCaptionBg = IsViewerTabChromeDocker(ptr.get())
                ? pal.caption_bg_inactive
                : pal.caption_bg;
            ptr->SetCaptionColors(pal.caption_fg, activeCaptionBg,
                                  pal.caption_fg_inactive, pal.caption_bg_inactive,
                                  pal.caption_pen);
            ptr->SetBarColor(pal.bar);
            if (ptr->IsUndocked()) {
                pmui::apply_dark_titlebar(ptr->GetHwnd(), pal.dark);
                pmui::apply_dwm_toplevel_frame_tint(ptr->GetHwnd(), pal.caption_bg_inactive);
            }
        }
    }

    if (m_viewerManager.IsTabbed()) {
        if (CDockViewerPanel* panel = ActiveDockableViewerTab()) {
            m_activeViewerDockTab = panel;
            m_viewerManager.SetActive(panel->GetFileViewer());
        }
    }

    // Do not call ApplyAppearance() here. Win32++ posts UWM_DOCKACTIVATE when
    // dock focus/activation changes (including clicking another panel's title
    // bar). Running the full theme pass + RDW_INVALIDATE on every docker was
    // effectively a repaint loop: heavy CPU, visible flicker in unrelated
    // panes, and combo/dropdown menus closing (focus + full-tree paint).
    // Theme refresh stays in ApplyAppearance (settings, system theme change,
    // initial layout, etc.); caption redraw is handled in CDocker::OnDockActivated.
    return CDockFrame::OnDockActivated(msg, wparam, lparam);
}

LRESULT CMainFrame::OnDockDestroyed(UINT msg, WPARAM wparam, LPARAM lparam)
{
    auto* destroyed = reinterpret_cast<CDocker*>(wparam);
    m_viewerDockTabs.erase(
        std::remove(m_viewerDockTabs.begin(), m_viewerDockTabs.end(), destroyed),
        m_viewerDockTabs.end());
    if (m_activeViewerDockTab == destroyed) {
        m_activeViewerDockTab = nullptr;
        m_viewerManager.SetActive(m_fileViewer);
    }

    LRESULT result = CDockFrame::OnDockDestroyed(msg, wparam, lparam);
    RefreshViewerDockTabs();
    if (m_viewerManager.IsTabbed()) {
        if (CDockViewerPanel* panel = ActiveDockableViewerTab()) {
            m_activeViewerDockTab = panel;
            m_viewerManager.SetActive(panel->GetFileViewer());
        }
    }
    return result;
}

void CMainFrame::NudgeLayoutAfterDockChange()
{
    // `ResetLayout` does this: recenters the `CFrame` view (image preview) inside the
    // dock client and re-runs `RecalcDockLayout` on the root. Without it, only
    // `CDocker::Dock` → `RecalcDockLayout` can leave the frame view rect stale after
    // a panel is re-shown, and `IExplorerBrowser` paints garbage over siblings.
    RecalcLayout();
    if (m_pDockFileTree && m_pDockFileTree->IsWindowVisible()) {
        // `SyncShellHostLayout` = `SetRect` + invalidate — **not** a full COM re-init. Queuing
        // `RequestRebuildAfterReDock` on every nudge (including first paint after `OnInitialUpdate`)
        // stacked `WM_EB_REDOCK` behind `WM_EB_INIT` and back-to-back Shell teardown/rebuild,
        // which could exit the process. Full re-init only after a real re-dock: `EnsurePanelVisible`
        // for the file tree, `OnDockEnd` after drag, `OnDeferredPostLayoutInit` with restored JSON, or
        // explicit re-float→dock in the chat workbench path.
        m_pDockFileTree->GetFileTreeContainer().GetBrowserView().SyncShellHostLayout();
    }
}

LRESULT CMainFrame::OnDockEnd(Win32xx::DragPos* pDragPos)
{
    if (pDragPos && pDragPos->pDocker) {
        // Win32++ outer dock zones (LEFTMOST/RIGHTMOST/TOPMOST/BOTTOMMOST) go through
        // DockOuter(), which can leave an empty docker/splitter shell in this app. The
        // ordinary cross-arm side zones use Dock() and are stable, so normalize to them.
        switch (pDragPos->dockZone) {
        case DS_DOCKED_LEFTMOST:   pDragPos->dockZone = DS_DOCKED_LEFT; break;
        case DS_DOCKED_RIGHTMOST:  pDragPos->dockZone = DS_DOCKED_RIGHT; break;
        case DS_DOCKED_TOPMOST:    pDragPos->dockZone = DS_DOCKED_TOP; break;
        case DS_DOCKED_BOTTOMMOST: pDragPos->dockZone = DS_DOCKED_BOTTOM; break;
        default: break;
        }
    }
    LRESULT r = CDocker::OnDockEnd(pDragPos);
    PruneOrphanedViewerDockTabs();
    RefreshViewerDockTabs();
    if (pDragPos && pDragPos->pDocker && pDragPos->pDocker->IsWindow()) {
        const auto& pal = pmui::theme_palette();
        const COLORREF activeCaptionBg = IsViewerTabChromeDocker(pDragPos->pDocker)
            ? pal.caption_bg_inactive
            : pal.caption_bg;
        pDragPos->pDocker->SetCaptionColors(pal.caption_fg, activeCaptionBg,
                                            pal.caption_fg_inactive, pal.caption_bg_inactive,
                                            pal.caption_pen);
        pDragPos->pDocker->SetBarColor(pal.bar);
        if (pDragPos->pDocker->IsUndocked()) {
            pmui::apply_dark_titlebar(pDragPos->pDocker->GetHwnd(), pal.dark);
            pmui::apply_dwm_toplevel_frame_tint(pDragPos->pDocker->GetHwnd(), pal.caption_bg_inactive);
            ::RedrawWindow(pDragPos->pDocker->GetHwnd(), nullptr, nullptr,
                RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        }
    }
    if (pDragPos && pDragPos->pDocker == m_pDockFileTree) {
        NudgeLayoutAfterDockChange();
        m_pDockFileTree->GetFileTreeContainer().GetBrowserView().RequestRebuildAfterReDock();
    }
    return r;
}

void CMainFrame::OnAppSettings()
{
    bool restarting = false;
    if (ShowAppSettingsDlg(GetHwnd(), &restarting) && !restarting) {
        ApplyAppearance();
        std::string wlerr;
        media::settings::WindowLayout wl{};
        if (m_pDockFileTree
            && media::settings::load_window_layout(wl, wlerr, m_workbench->workbenchSettingsId()))
            m_pDockFileTree->GetFileTreeContainer().GetBrowserView().SetShowShellFrames(
                wl.filetree_show_shell_frames);
    }
}

void CMainFrame::SwitchSettingsMode(CSettingsView::Mode mode)
{
    // Always make sure the Settings dock is visible when the user picks an
    // Action. This is the core of the "visible on demand" UX — by default
    // Settings is hidden and only Explorer + Queue + Log are open; clicking
    // Resize / Compress / Meta / Transform / Find pops Settings into the
    // right-hand slot. No-op when it's already visible.
    EnsurePanelVisible(m_pDockSettings, DS_DOCKED_RIGHT, GetDockAncestor(),
                       DpiScaleInt(280), IDC_CMD_VIEW_SETTINGS);
    if (!m_pDockSettings) return;

    auto& sv = m_pDockSettings->GetSettingsContainer().GetSettingsView();
    if (sv.GetMode() == mode) return;
    sv.SetMode(mode);

    media::settings::AppearanceSettings appearance{};
    std::string                     aerr;
    media::settings::load_appearance(appearance, aerr);
    const pmui::settings_panel_i18n::DockStrings& dk =
        pmui::settings_panel_i18n::dock_strings_for(appearance.display_language);

    const wchar_t* caption = dk.tab_generic;
    const wchar_t* tab     = dk.tab_generic;
    switch (mode) {
    case CSettingsView::MODE_RESIZE:
        caption = dk.caption_resize;
        tab     = dk.tab_resize;
        break;
    case CSettingsView::MODE_COMPRESS:
        caption = dk.caption_compress;
        tab     = dk.tab_compress;
        break;
    case CSettingsView::MODE_META:
        caption = dk.caption_meta;
        tab     = dk.tab_meta;
        break;
    case CSettingsView::MODE_TRANSFORM:
        caption = dk.caption_transform;
        tab     = dk.tab_transform;
        break;
    case CSettingsView::MODE_FIND:
        caption = dk.caption_find;
        tab     = dk.tab_find;
        break;
    case CSettingsView::MODE_DUPLICATES:
        caption = dk.caption_duplicates;
        tab     = dk.tab_duplicates;
        break;
    default: break;
    }

    m_pDockSettings->GetSettingsContainer().SetDockCaption(caption);
    m_pDockSettings->GetSettingsContainer().SetTabText(tab);
    m_pDockSettings->GetSettingsContainer().Invalidate();
    m_pDockSettings->RedrawWindow();
}

#ifdef FEATURE_NODES
void CMainFrame::SwitchWorkbench(Workbench wb)
{
    if (wb == m_activeWorkbench) return;
    m_activeWorkbench = wb;

    CDocker* ancestor = GetDockAncestor();

    auto showPanel = [this](CDocker* p, UINT style, CDocker* parent,
                             int size, UINT32 cmdID) {
        if (!p || !parent) return;
        if (!IsPanelVisible(p)) {
            parent->Dock(p, style);
            if (size > 0) p->SetDockSize(size);
        }
        InvalidateToggle(cmdID);
    };
    auto hidePanel = [this](CDocker* p, UINT32 cmdID) {
        if (!p) return;
        if (IsPanelVisible(p)) p->Hide();
        InvalidateToggle(cmdID);
    };

    if (wb == Workbench::Nodes) {
        hidePanel(m_pDockSettings,   IDC_CMD_VIEW_SETTINGS);
        hidePanel(m_pDockQueue,      IDC_CMD_VIEW_QUEUE);
        hidePanel(m_pDockFileTree,   IDC_CMD_VIEW_FILETREE);
        showPanel(m_pDockNodes, DS_DOCKED_TOP,    ancestor,    0,                IDC_CMD_VIEW_NODES);
        showPanel(m_pDockLog,   DS_DOCKED_BOTTOM, ancestor, DpiScaleInt(160),   IDC_CMD_VIEW_LOG);
    } else {
        hidePanel(m_pDockNodes, IDC_CMD_VIEW_NODES);
        showPanel(m_pDockFileTree,   DS_DOCKED_LEFT,   ancestor,        DpiScaleInt(240), IDC_CMD_VIEW_FILETREE);
        showPanel(m_pDockQueue,      DS_DOCKED_BOTTOM, ancestor,        DpiScaleInt(220), IDC_CMD_VIEW_QUEUE);
        showPanel(m_pDockLog,
                  (m_pDockQueue && IsPanelVisible(m_pDockQueue)) ? DS_DOCKED_RIGHT : DS_DOCKED_BOTTOM,
                  (m_pDockQueue && IsPanelVisible(m_pDockQueue)) ? static_cast<CDocker*>(m_pDockQueue) : ancestor,
                  (m_pDockQueue && IsPanelVisible(m_pDockQueue)) ? DpiScaleInt(360) : DpiScaleInt(220),
                  IDC_CMD_VIEW_LOG);
        // Settings are user-on-demand under the standard workbench;
        // do NOT auto-show them here either.
    }
    RecalcLayout();
}
#endif // FEATURE_NODES

// ── Ribbon IUIApplication (stock UIRibbon host only) ─────────────────────────
#ifndef FEATURE_USE_OWN_RIBBON

STDMETHODIMP CMainFrame::Execute(UINT32 cmdID, UI_EXECUTIONVERB verb,
    const PROPERTYKEY*, const PROPVARIANT*, IUISimplePropertySet*)
{
    if (verb == UI_EXECUTIONVERB_EXECUTE) {
        switch (cmdID) {
        // ── File queue ────────────────────────────────────────────────────
        case IDC_CMD_ADD_FILES:  OnAddFiles();   break;
        case IDC_CMD_ADD_FOLDER: OnAddFolder();  break;
        case IDC_CMD_CLEAR:      OnClearQueue(); break;
        case IDC_CMD_SAVE_AS:    OnSaveAs();     break;

        // ── Action mode switches (Home tab) ──────────────────────────────
        // Buttons only switch the Settings panel mode so the user can adjust
        // options first; the unified Run button executes the active mode.
        case IDC_CMD_RESIZE:
            m_homeTabLastMode = CSettingsView::MODE_RESIZE;
            SwitchSettingsMode(CSettingsView::MODE_RESIZE);
            break;
        case IDC_CMD_COMPRESS:
            m_homeTabLastMode = CSettingsView::MODE_COMPRESS;
            SwitchSettingsMode(CSettingsView::MODE_COMPRESS);
            break;
        case IDC_CMD_META:
            m_homeTabLastMode = CSettingsView::MODE_META;
            SwitchSettingsMode(CSettingsView::MODE_META);
            break;
        case IDC_CMD_TRANSFORM:
            m_homeTabLastMode = CSettingsView::MODE_TRANSFORM;
            SwitchSettingsMode(CSettingsView::MODE_TRANSFORM);
            break;
        case IDC_CMD_FIND:
            m_homeTabLastMode = CSettingsView::MODE_FIND;
            SwitchSettingsMode(CSettingsView::MODE_FIND);
            break;
        case IDC_CMD_DUPLICATES:
            m_homeTabLastMode = CSettingsView::MODE_DUPLICATES;
            SwitchSettingsMode(CSettingsView::MODE_DUPLICATES);
            break;
        case IDC_CMD_CHAT:
            OnChat();
            break;

        // ── Run — executes whichever Action mode is currently active ────
#if FEATURE_COMMAND_UI_COMMAND_CONTROL
        case IDC_CMD_RUN:
            switch (m_homeTabLastMode) {
            case CSettingsView::MODE_RESIZE:    OnResize();   break;
            case CSettingsView::MODE_COMPRESS:  OnCompress(); break;
            case CSettingsView::MODE_META:      OnMeta();     break;
            case CSettingsView::MODE_TRANSFORM: OnRun();      break;
            case CSettingsView::MODE_FIND:      OnFind();     break;
            case CSettingsView::MODE_DUPLICATES: OnDuplicates(); break;
            }
            break;
#endif

        // ── Batch queue control ──────────────────────────────────────────────
#if FEATURE_COMMAND_UI_COMMAND_CONTROL
        case IDC_CMD_PAUSE:        OnPauseBatch();   break;
        case IDC_CMD_RESUME:       OnResumeBatch();  break;
        case IDC_CMD_CANCEL:       OnCancelBatch();  break;
#endif
#if FEATURE_COMMAND_SESSION_PERSISTENCE
        case IDC_CMD_SAVE_SESSION: OnSaveSession();  break;
        case IDC_CMD_LOAD_SESSION: OnLoadSession();  break;
#endif
        case IDC_CMD_PRESETS:       OnPresets();                   break;
        case IDC_CMD_PROVIDER_KEYS: ShowProviderSettingsDlg(GetHwnd()); break;
        case IDC_CMD_ABOUT:         OnHelp();                      break;
        case IDC_CMD_UPDATE_EXPLORER: OnUpdateExplorer();          break;
        case IDC_CMD_UNREGISTER_EXPLORER: OnUnregisterExplorer();    break;
        case IDC_CMD_EXIT:          OnExit();                      break;
        case IDC_RIBBONHELP:        OnHelp();                      break;

        // ── View panel toggles ───────────────────────────────────────────
#if FEATURE_COMMAND_QUEUE_VIEW
        case IDC_CMD_VIEW_QUEUE:
            TogglePanelView(m_pDockQueue, DS_DOCKED_BOTTOM,
                GetDockAncestor(), DpiScaleInt(220), IDC_CMD_VIEW_QUEUE);
            break;
#endif
#if FEATURE_COMMAND_LOG_VIEW
        case IDC_CMD_VIEW_LOG:
            TogglePanelView(m_pDockLog, DS_DOCKED_BOTTOM,
                GetDockAncestor(), DpiScaleInt(220), IDC_CMD_VIEW_LOG);
            break;
#endif
        case IDC_CMD_VIEW_SETTINGS:
            TogglePanelView(m_pDockSettings, DS_DOCKED_RIGHT,
                GetDockAncestor(), DpiScaleInt(280), IDC_CMD_VIEW_SETTINGS);
            break;
        // case IDC_CMD_VIEW_GENPREVIEW removed — panel was retired.
        case IDC_CMD_VIEW_FILETREE:
            TogglePanelView(m_pDockFileTree, DS_DOCKED_LEFT,
                GetDockAncestor(), DpiScaleInt(240), IDC_CMD_VIEW_FILETREE);
            break;
        case IDC_CMD_VIEW_TABBED:
            ToggleCentreViewerTabs(true);
            break;
        case IDC_CMD_VIEW_FINDRESULTS:
            TogglePanelView(m_pDockFindResults, DS_DOCKED_RIGHT,
                GetDockAncestor(), DpiScaleInt(360), IDC_CMD_VIEW_FINDRESULTS);
            break;
        case IDC_CMD_VIEW_DUPLICATERESULTS:
            TogglePanelView(m_pDockDuplicateResults, DS_DOCKED_RIGHT,
                GetDockAncestor(), DpiScaleInt(360), IDC_CMD_VIEW_DUPLICATERESULTS);
            break;
        case IDC_CMD_VIEW_CHAT: {
            if (IsChatWorkbench()) {
                OnChat();
                break;
            }
            CDocker* d = nullptr;
            int sz = DpiScaleInt(420);
#ifdef FEATURE_CHAT_WEB
            if (m_pDockChatWeb) { d = m_pDockChatWeb; }
#endif
            TogglePanelView(d, DS_DOCKED_RIGHT, GetDockAncestor(), sz, IDC_CMD_VIEW_CHAT);
            break;
        }
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
        case IDC_CMD_VIEW_HOME:
            m_viewerManager.ActiveView().LoadHome();
            break;
#endif
#ifdef FEATURE_NODES
        case IDC_CMD_VIEW_NODES:
            TogglePanelView(m_pDockNodes, DS_DOCKED_BOTTOM,
                GetDockAncestor(), DpiScaleInt(300), IDC_CMD_VIEW_NODES);
            break;
#endif

        case IDC_CMD_RESET_LAYOUT: ResetLayout();    break;
        case IDC_CMD_DEBUG_STATE:  DebugDockState(); break;
        case IDC_CMD_APP_SETTINGS: OnAppSettings();  break;
        case IDC_CMD_FILE_OPTIONS: OnAppSettings();   break;

        default: break;
        }
    }

    // Tab switches: Home restores the last-active action mode.
    if (cmdID == cmdTabHome) {
#ifdef FEATURE_NODES
        SwitchWorkbench(Workbench::Standard);
#endif
        SwitchSettingsMode(m_homeTabLastMode);
    }
#ifdef FEATURE_NODES
    else if (cmdID == cmdTabNodes) {
        SwitchWorkbench(Workbench::Nodes);
    }
#endif

    return S_OK;
}

STDMETHODIMP CMainFrame::OnViewChanged(UINT32, UI_VIEWTYPE typeId,
    IUnknown* pView, UI_VIEWVERB verb, INT32)
{
    if (typeId == UI_VIEWTYPE_RIBBON) {
        switch (verb) {
        case UI_VIEWVERB_CREATE:
            m_pIUIRibbon = reinterpret_cast<IUIRibbon*>(pView);
            // Appearance often ran before the framework bound the ribbon view.
            {
                const auto& pal = pmui::theme_palette();
                pmui::allow_dark_mode_for_window_tree(GetHwnd(), pal.dark);
                ::InvalidateRect(GetHwnd(), nullptr, TRUE);
            }
            return S_OK;
        case UI_VIEWVERB_SIZE:    RecalcLayout(); return S_OK;
        case UI_VIEWVERB_DESTROY: m_pIUIRibbon = nullptr; return S_OK;
        case UI_VIEWVERB_ERROR:   return E_FAIL;
        }
    }
    return E_NOTIMPL;
}

STDMETHODIMP CMainFrame::UpdateProperty(UINT32 cmdID, REFPROPERTYKEY key,
    const PROPVARIANT*, PROPVARIANT* newValue)
{
    // Ribbon global chrome — UI_PKEY_Global* (HSB in VT_UI4).
    // The framework requests these on whatever command id it uses for that query
    // (not always the QAT id 701), so we never gate on cmdID here.
    if (key == UI_PKEY_GlobalBackgroundColor ||
        key == UI_PKEY_GlobalHighlightColor ||
        key == UI_PKEY_GlobalTextColor)
    {
        const auto& pal = pmui::theme_palette();
        const auto  t   = pmui::ribbon_global_tint_from_palette(pal);
        newValue->vt = VT_UI4;
        if (key == UI_PKEY_GlobalBackgroundColor) newValue->ulVal = t.background;
        else if (key == UI_PKEY_GlobalHighlightColor) newValue->ulVal = t.highlight;
        else /* UI_PKEY_GlobalTextColor */ newValue->ulVal = t.text;
        return S_OK;
    }

    if (key == UI_PKEY_BooleanValue) {
        newValue->vt      = VT_BOOL;
        newValue->boolVal = IsToggleSelected(cmdID) ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    if (key == UI_PKEY_Enabled) {
        newValue->vt      = VT_BOOL;
        const bool paused   = m_batchCtrl && m_batchCtrl->paused.load();
        const bool hasQueue = m_pDockQueue &&
                              m_pDockQueue->GetQueueContainer().GetListView().QueueCount() > 0;
        switch (cmdID) {
        case IDC_CMD_PAUSE:
            newValue->boolVal = (m_processing && !paused) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case IDC_CMD_RESUME:
            newValue->boolVal = paused ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case IDC_CMD_CANCEL:
            // Enable whenever a batch is active (running OR paused).
            newValue->boolVal = m_processing ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case IDC_CMD_SAVE_SESSION:
            // Enable as long as there are files in the queue — session is
            // built from the queue + current mode + current settings on demand.
            newValue->boolVal = hasQueue ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        default:
            break;
        }
    }
    return CRibbonDockFrame::UpdateProperty(cmdID, key, nullptr, newValue);
}

#endif // !FEATURE_USE_OWN_RIBBON

void CMainFrame::RunViewPanelCommand(UINT32 cmdID)
{
    switch (cmdID) {
#if FEATURE_COMMAND_QUEUE_VIEW
    case IDC_CMD_VIEW_QUEUE:
        TogglePanelView(m_pDockQueue, DS_DOCKED_BOTTOM,
                        GetDockAncestor(), DpiScaleInt(220), IDC_CMD_VIEW_QUEUE);
        break;
#endif
#if FEATURE_COMMAND_LOG_VIEW
    case IDC_CMD_VIEW_LOG:
        TogglePanelView(m_pDockLog, DS_DOCKED_BOTTOM,
                        GetDockAncestor(), DpiScaleInt(220), IDC_CMD_VIEW_LOG);
        break;
#endif
    case IDC_CMD_VIEW_SETTINGS:
        TogglePanelView(m_pDockSettings, DS_DOCKED_RIGHT,
                        GetDockAncestor(), DpiScaleInt(280), IDC_CMD_VIEW_SETTINGS);
        break;
    case IDC_CMD_VIEW_FILETREE:
        TogglePanelView(m_pDockFileTree, DS_DOCKED_LEFT,
                        GetDockAncestor(), DpiScaleInt(240), IDC_CMD_VIEW_FILETREE);
        break;
    case IDC_CMD_VIEW_FINDRESULTS:
        TogglePanelView(m_pDockFindResults, DS_DOCKED_RIGHT,
                        GetDockAncestor(), DpiScaleInt(360), IDC_CMD_VIEW_FINDRESULTS);
        break;
    case IDC_CMD_VIEW_DUPLICATERESULTS:
        TogglePanelView(m_pDockDuplicateResults, DS_DOCKED_RIGHT,
                        GetDockAncestor(), DpiScaleInt(360), IDC_CMD_VIEW_DUPLICATERESULTS);
        break;
    case IDC_CMD_VIEW_CHAT: {
        if (IsChatWorkbench()) {
            OnChat();
            break;
        }
        CDocker* d = nullptr;
        int      sz = DpiScaleInt(420);
#ifdef FEATURE_CHAT_WEB
        if (m_pDockChatWeb) {
            d  = m_pDockChatWeb;
        }
#endif
        TogglePanelView(d, DS_DOCKED_RIGHT, GetDockAncestor(), sz, IDC_CMD_VIEW_CHAT);
        break;
    }
    case IDC_CMD_VIEW_VIEWER_PANEL:
        TogglePanelView(m_pDockViewerPanel, DS_DOCKED_BOTTOM,
                        m_pDockFileTree ? m_pDockFileTree : GetDockAncestor(),
                        DpiScaleInt(240), IDC_CMD_VIEW_VIEWER_PANEL);
        break;
    case IDC_CMD_VIEW_TABBED:
        ToggleCentreViewerTabs(true);
        break;
    case IDC_CMD_VIEW_PIN_TAB:
        ToggleActiveViewerTabPinned(true);
        break;
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    case IDC_CMD_VIEW_HOME:
        m_viewerManager.ActiveView().LoadHome();
        break;
#endif
#ifdef FEATURE_NODES
    case IDC_CMD_VIEW_NODES:
        TogglePanelView(m_pDockNodes, DS_DOCKED_BOTTOM,
                        GetDockAncestor(), DpiScaleInt(300), IDC_CMD_VIEW_NODES);
        break;
#endif
#ifdef FEATURE_CONSOLE
    case IDC_CMD_VIEW_CONSOLE:
    {
        if (!pmui::web_console::available()) {
            TogglePanelView(m_pDockLog, DS_DOCKED_BOTTOM,
                            GetDockAncestor(), DpiScaleInt(220), IDC_CMD_VIEW_LOG);
            break;
        }
        if (!m_pDockConsole) {
            if (CDocker* existing = GetDockFromID(DOCK_ID_CONSOLE))
                m_pDockConsole = static_cast<CDockWebConsole*>(existing);
        }
#ifdef FEATURE_BROWSER
        WireConsoleBus();
#endif
        const bool wasVisible = IsPanelVisible(m_pDockConsole);
        const DWORD64 t0 = ::GetTickCount64();
        logger::info(std::string("[console-open] RunViewPanelCommand enter visible=")
            + (IsPanelVisible(m_pDockConsole) ? "1" : "0")
            + " dockPtr=" + std::to_string(reinterpret_cast<std::uintptr_t>(m_pDockConsole)));
        TogglePanelView(m_pDockConsole, DS_DOCKED_BOTTOM,
                        GetDockAncestor(), DpiScaleInt(260), IDC_CMD_VIEW_CONSOLE);
        if (wasVisible || IsPanelVisible(m_pDockConsole)) {
            if (m_pDockLog && IsPanelVisible(m_pDockLog)) {
                m_pDockLog->Hide();
                InvalidateToggle(IDC_CMD_VIEW_LOG);
            }
            if (m_pDockQueue && IsPanelVisible(m_pDockQueue)) {
                m_pDockQueue->Hide();
                InvalidateToggle(IDC_CMD_VIEW_QUEUE);
            }
        }
        logger::info(std::string("[console-open] RunViewPanelCommand leave visible=")
            + (IsPanelVisible(m_pDockConsole) ? "1" : "0")
            + " elapsed=" + std::to_string(::GetTickCount64() - t0) + " ms");
        break;
    }
#endif
    default:
        break;
    }
}

void CMainFrame::ToggleCentreViewerTabs(bool persist)
{
    ApplyCentreViewerTabbed(!m_viewerManager.IsTabbed(), persist);
}

void CMainFrame::ApplyCentreViewerTabbed(bool tabbed, bool persist)
{
    if (IsChatWorkbench())
        return;

    if (tabbed == m_viewerManager.IsTabbed()) {
        InvalidateToggle(IDC_CMD_VIEW_TABBED);
        return;
    }

    const pmui::PreviewState carryPreview = m_previewCoord.State();
    std::vector<std::wstring> carryPaths = carryPreview.paths;
    if (carryPaths.empty() && !carryPreview.active.empty())
        carryPaths.push_back(carryPreview.active);
    if (carryPaths.empty()) {
        const std::wstring currentPath = m_fileViewer.PreviewPathW();
        if (!currentPath.empty())
            carryPaths.push_back(currentPath);
    }

    if (tabbed) {
        SetView(m_centerViewerTabs);
        if (CDockViewerPanel* panel = EnsureDockableViewerTab(false)) {
            m_activeViewerDockTab = panel;
            m_viewerManager.SetActive(panel->GetFileViewer());
            if (!carryPaths.empty() && panel->GetFileViewer().PreviewPathW().empty()) {
                const pmui::PreviewSource source = carryPreview.source == pmui::PreviewSource::None
                    ? pmui::PreviewSource::AppBrowse
                    : carryPreview.source;
                panel->GetFileViewer().OpenFile(carryPaths, source);
                panel->SetTitle(CString(fs::path(carryPaths.front()).filename().wstring().c_str()));
            }
        }
    } else {
        SetView(m_fileViewer);
        m_activeViewerDockTab = nullptr;
        m_viewerManager.SetActive(m_fileViewer);
    }

    m_viewerManager.SetTabbed(tabbed);
    InvalidateToggle(IDC_CMD_VIEW_TABBED);
    RecalcLayout();

    if (persist && GetHwnd() && ::IsWindow(GetHwnd()))
        (void)::PostMessageW(GetHwnd(), UWM_PM_SAVE_WORKBENCH_LAYOUT, 0, 0);
}

void CMainFrame::ToggleActiveViewerTabPinned(bool persist)
{
    if (!m_viewerManager.IsTabbed() || !m_centerViewerTabs.IsWindow())
        return;

    m_centerViewerTabs.ToggleActivePinned();
    if (CDockViewerPanel* panel = ActiveDockableViewerTab()) {
        m_activeViewerDockTab = panel;
        m_viewerManager.SetActive(panel->GetFileViewer());
    }
    InvalidateToggle(IDC_CMD_VIEW_PIN_TAB);
    if (persist && GetHwnd() && ::IsWindow(GetHwnd()))
        (void)::PostMessageW(GetHwnd(), UWM_PM_SAVE_WORKBENCH_LAYOUT, 0, 0);
}

bool CMainFrame::IsViewerTabDocker(const CDocker* docker) const
{
    if (!docker)
        return false;
    const int dockID = docker->GetDockID();
    return dockID >= DOCK_ID_VIEWER_TAB_FIRST
        && dockID <= DOCK_ID_VIEWER_TAB_LAST
        && dynamic_cast<const CDockViewerPanel*>(docker) != nullptr;
}

bool CMainFrame::IsViewerTabChromeDocker(const CDocker* docker) const
{
    if (IsViewerTabDocker(docker))
        return true;

    if (!docker || !docker->IsWindow())
        return false;

    CDockContainer* container = docker->GetContainer();
    if (!container)
        return false;

    for (const ContainerInfo& ci : container->GetAllContainers()) {
        if (dynamic_cast<CViewerPanelContainer*>(ci.pContainer) != nullptr)
            return true;
        if (IsViewerTabDocker(GetDockFromView(ci.pContainer)))
            return true;
    }
    return false;
}

bool CMainFrame::IsViewerTabReferencedByGroup(const CDockViewerPanel* panel) const
{
    if (!panel)
        return false;

    for (CDocker* docker : GetAllDockers()) {
        if (!docker || docker == panel || !docker->IsWindow())
            continue;
        CDockContainer* container = docker->GetContainer();
        if (!container)
            continue;
        for (const ContainerInfo& ci : container->GetAllContainers()) {
            if (GetDockFromView(ci.pContainer) == panel)
                return true;
        }
    }
    return false;
}

void CMainFrame::RefreshViewerDockTabs()
{
    std::vector<CDockViewerPanel*> live;
    for (const Win32xx::DockPtr& ptr : GetAllDockChildren()) {
        if (!ptr || !IsViewerTabDocker(ptr.get()))
            continue;
        auto* panel = static_cast<CDockViewerPanel*>(ptr.get());
#ifdef FEATURE_BROWSER
        panel->GetFileViewer().SetBusManager(&m_webViews);
#endif
        live.push_back(panel);
    }
    m_viewerDockTabs = std::move(live);
    if (m_activeViewerDockTab &&
        std::find(m_viewerDockTabs.begin(), m_viewerDockTabs.end(), m_activeViewerDockTab) == m_viewerDockTabs.end()) {
        m_activeViewerDockTab = nullptr;
    }
}

void CMainFrame::PruneOrphanedViewerDockTabs()
{
    std::map<int, int> idCounts;
    for (const Win32xx::DockPtr& ptr : GetAllDockChildren()) {
        if (ptr && IsViewerTabDocker(ptr.get()))
            ++idCounts[ptr->GetDockID()];
    }

    std::vector<CDockViewerPanel*> prune;
    for (const Win32xx::DockPtr& ptr : GetAllDockChildren()) {
        if (!ptr || !IsViewerTabDocker(ptr.get()))
            continue;
        auto* panel = static_cast<CDockViewerPanel*>(ptr.get());
        if (panel == m_activeViewerDockTab)
            continue;
        if (IsViewerTabReferencedByGroup(panel))
            continue;

        const CRect rc = panel->IsWindow() ? panel->GetWindowRect() : CRect{};
        const bool emptyRect = rc.Width() <= 0 || rc.Height() <= 0;
        const bool duplicateId = idCounts[panel->GetDockID()] > 1;
        if (!panel->IsWindowVisible() || emptyRect || duplicateId)
            prune.push_back(panel);
    }

    for (CDockViewerPanel* panel : prune) {
        if (panel && panel->IsWindow())
            panel->SendMessage(WM_CLOSE);
    }
    if (!prune.empty())
        RefreshViewerDockTabs();
}

int CMainFrame::AllocateViewerDockTabId()
{
    std::set<int> used;
    for (const Win32xx::DockPtr& ptr : GetAllDockChildren()) {
        if (ptr && IsViewerTabDocker(ptr.get()))
            used.insert(ptr->GetDockID());
    }

    for (int i = 0; i <= DOCK_ID_VIEWER_TAB_LAST - DOCK_ID_VIEWER_TAB_FIRST; ++i) {
        const int candidate = m_nextViewerDockTabId;
        ++m_nextViewerDockTabId;
        if (m_nextViewerDockTabId > DOCK_ID_VIEWER_TAB_LAST)
            m_nextViewerDockTabId = DOCK_ID_VIEWER_TAB_FIRST;
        if (used.count(candidate) == 0)
            return candidate;
    }

    return DOCK_ID_VIEWER_TAB_FIRST;
}

CDockViewerPanel* CMainFrame::ActiveDockableViewerTab()
{
    RefreshViewerDockTabs();

    if (CDocker* activeDocker = GetActiveDocker()) {
        if (IsViewerTabDocker(activeDocker))
            return static_cast<CDockViewerPanel*>(activeDocker);
    }

    if (!m_centerViewerTabs.IsWindow())
        return m_activeViewerDockTab;
    CDockContainer* active = m_centerViewerTabs.GetActiveContainer();
    if (!active)
        return m_activeViewerDockTab;
    CDocker* docker = GetDockFromView(active);
    if (!IsViewerTabDocker(docker))
        return m_activeViewerDockTab;
    return static_cast<CDockViewerPanel*>(docker);
}

CDockViewerPanel* CMainFrame::EnsureDockableViewerTab(bool forceNew)
{
    if (!m_centerViewerTabs.IsWindow())
        return nullptr;

    if (CDockViewerPanel* active = ActiveDockableViewerTab()) {
        if (!forceNew) {
            m_activeViewerDockTab = active;
#ifdef FEATURE_BROWSER
            active->GetFileViewer().SetBusManager(&m_webViews);
#endif
            return active;
        }
    }

    PruneOrphanedViewerDockTabs();
    const int dockId = AllocateViewerDockTabId();

    auto docker = std::make_unique<CDockViewerPanel>();
    auto* panel = static_cast<CDockViewerPanel*>(
        AddDockedChild(std::move(docker), DS_DOCKED_CONTAINER | DS_CLIENTEDGE, 0, dockId));
    if (!panel)
        return ActiveDockableViewerTab();

    panel->SetCaptionHeight(DpiScaleInt(26));
#ifdef FEATURE_BROWSER
    panel->GetFileViewer().SetBusManager(&m_webViews);
#endif
    if (std::find(m_viewerDockTabs.begin(), m_viewerDockTabs.end(), panel) == m_viewerDockTabs.end())
        m_viewerDockTabs.push_back(panel);
    m_activeViewerDockTab = panel;
    m_viewerManager.SetActive(panel->GetFileViewer());
    ApplyAppearance(false);
    return panel;
}

CFileViewer& CMainFrame::LiveDockableViewerForExplorer(bool* created)
{
    if (created)
        *created = false;

    CDockViewerPanel* panel = EnsureDockableViewerTab(false);
    if (panel && panel->IsPinned()) {
        panel = EnsureDockableViewerTab(true);
        if (created)
            *created = true;
    }
    if (!panel)
        return m_viewerManager.ActiveView();
    m_activeViewerDockTab = panel;
    m_viewerManager.SetActive(panel->GetFileViewer());
    return panel->GetFileViewer();
}

void CMainFrame::SyncViewMenuChecks()
{
    HMENU h = AppMenuHandle();
    if (!h)
        return;
    auto mark = [&](UINT cmd, bool on) {
        ::CheckMenuItem(h, cmd, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };
#if FEATURE_COMMAND_QUEUE_VIEW
    mark(IDC_CMD_VIEW_QUEUE, IsToggleSelected(IDC_CMD_VIEW_QUEUE));
#endif
#if FEATURE_COMMAND_LOG_VIEW
    mark(IDC_CMD_VIEW_LOG, IsToggleSelected(IDC_CMD_VIEW_LOG));
#endif
    mark(IDC_CMD_VIEW_SETTINGS, IsToggleSelected(IDC_CMD_VIEW_SETTINGS));
    mark(IDC_CMD_VIEW_FINDRESULTS, IsToggleSelected(IDC_CMD_VIEW_FINDRESULTS));
    mark(IDC_CMD_VIEW_DUPLICATERESULTS, IsToggleSelected(IDC_CMD_VIEW_DUPLICATERESULTS));
    mark(IDC_CMD_VIEW_CHAT, IsToggleSelected(IDC_CMD_VIEW_CHAT));
    mark(IDC_CMD_VIEW_FILETREE, IsToggleSelected(IDC_CMD_VIEW_FILETREE));
    mark(IDC_CMD_VIEW_VIEWER_PANEL, IsToggleSelected(IDC_CMD_VIEW_VIEWER_PANEL));
    mark(IDC_CMD_VIEW_TABBED, IsToggleSelected(IDC_CMD_VIEW_TABBED));
    mark(IDC_CMD_VIEW_PIN_TAB, IsToggleSelected(IDC_CMD_VIEW_PIN_TAB));
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    mark(IDC_CMD_VIEW_HOME, IsToggleSelected(IDC_CMD_VIEW_HOME));
#endif
#ifdef FEATURE_NODES
    mark(IDC_CMD_VIEW_NODES, IsToggleSelected(IDC_CMD_VIEW_NODES));
#endif
#ifdef FEATURE_CONSOLE
    mark(IDC_CMD_VIEW_CONSOLE, IsToggleSelected(IDC_CMD_VIEW_CONSOLE));
#endif
#ifdef FEATURE_BROWSER
    mark(IDC_CMD_VIEW_BROWSER, IsToggleSelected(IDC_CMD_VIEW_BROWSER));
#endif
}

#ifdef FEATURE_USE_OWN_RIBBON

void CMainFrame::RunRibbonCommandId(UINT32 cmdID)
{
#if FEATURE_CUSTOM_COMMANDS
    if (const auto* custom = own_ribbon::FindCustomRibbonCommand(cmdID)) {
        logger::trace("[ribbon-custom] run id=" + custom->id + " cmdId=" + std::to_string(cmdID));
        if (!custom->enabled)
            return;
        if (!custom->userDataJson.empty()) {
            logger::trace("[ribbon-custom] " + custom->id + " userData=" + custom->userDataJson);
        }
        if (custom->targetRibbonCommandId != 0 && custom->targetRibbonCommandId != cmdID) {
            logger::trace("[ribbon-custom] " + custom->id + " delegate => ribbonCommandId="
                + std::to_string(custom->targetRibbonCommandId));
            RunRibbonCommandId(custom->targetRibbonCommandId);
            return;
        }
        if (!custom->appCommand.empty()) {
            logger::trace("[ribbon-custom] " + custom->id + " appCommand=" + custom->appCommand);
            RunAppCommand(custom->appCommand);
            return;
        }
        std::wstring commandSourcePath;
        if (!m_explorerSelectionPaths.empty())
            commandSourcePath = m_explorerSelectionPaths.front();
        if (commandSourcePath.empty())
            commandSourcePath = m_viewerManager.ActiveView().PreviewPathW();
#if defined(FEATURE_VIEWER_WEB)
        if (commandSourcePath.empty()) {
            if (auto* pv = m_viewerManager.ActiveView().ViewerWebPanel())
                commandSourcePath = pv->SelectionPath();
        }
#endif
        std::wstring commandCwd;
        if (m_pDockFileTree)
            commandCwd = m_pDockFileTree->GetFileTreeContainer().GetBrowserView().GetCurrentFolder();
        if (commandCwd.empty()) {
            std::error_code ec;
            const auto cwd = fs::current_path(ec);
            if (!ec)
                commandCwd = cwd.wstring();
        }
        const own_ribbon::CustomRibbonCommand resolvedCustom =
            resolve_custom_command_vars(*custom, media::commands::VariableContext{
                wide_to_utf8(commandCwd),
                wide_to_utf8(commandSourcePath),
                wide_paths_to_utf8(m_explorerSelectionPaths),
            });

        if (!resolvedCustom.externalCommand.empty()
            || (resolvedCustom.externalShellMode && !resolvedCustom.externalShellLine.empty())) {
#if defined(FEATURE_BROWSER) && defined(FEATURE_CONSOLE)
            if (!resolvedCustom.runNewShellWindow && pmui::web_console::available()) {
                const std::wstring line = custom_console_command_line(resolvedCustom);
                if (!line.empty()) {
                    ribbon_trace_launch(resolvedCustom, "console", line);
                    if (RunInWebConsole(pmui::wide_to_utf8(line), /*new_shell=*/true,
                                          resolvedCustom.closeOnExit))
                        return;
                }
            }
#endif
            (void)launch_custom_external_command(resolvedCustom);
            return;
        }
        if (!resolvedCustom.openUrl.empty()) {
            logger::trace("[ribbon-custom] " + resolvedCustom.id + " openUrl="
                + wide_to_utf8(resolvedCustom.openUrl));
            pmui::shell::open_url(GetHwnd(), resolvedCustom.openUrl);
            return;
        }
        if (!resolvedCustom.openPath.empty()) {
            logger::trace("[ribbon-custom] " + resolvedCustom.id + " openPath="
                + wide_to_utf8(resolvedCustom.openPath));
            pmui::shell::open_path(GetHwnd(), resolvedCustom.openPath);
            return;
        }
        logger::trace("[ribbon-custom] " + custom->id + " no executable action after resolve");
    }
#endif

    // Same command routing as Execute(UI_EXECUTIONVERB_EXECUTE) + ribbon tab ids.
    switch (cmdID) {
    case IDC_CMD_ADD_FILES:  OnAddFiles();   break;
    case IDC_CMD_ADD_FOLDER: OnAddFolder();  break;
    case IDC_CMD_CLEAR:      OnClearQueue(); break;
    case IDC_CMD_SAVE_AS:    OnSaveAs();     break;

    case IDC_CMD_RESIZE:
        m_homeTabLastMode = CSettingsView::MODE_RESIZE;
        SwitchSettingsMode(CSettingsView::MODE_RESIZE);
        break;
    case IDC_CMD_COMPRESS:
        m_homeTabLastMode = CSettingsView::MODE_COMPRESS;
        SwitchSettingsMode(CSettingsView::MODE_COMPRESS);
        break;
    case IDC_CMD_META:
        m_homeTabLastMode = CSettingsView::MODE_META;
        SwitchSettingsMode(CSettingsView::MODE_META);
        break;
    case IDC_CMD_TRANSFORM:
        m_homeTabLastMode = CSettingsView::MODE_TRANSFORM;
        SwitchSettingsMode(CSettingsView::MODE_TRANSFORM);
        break;
    case IDC_CMD_FIND:
        m_homeTabLastMode = CSettingsView::MODE_FIND;
        SwitchSettingsMode(CSettingsView::MODE_FIND);
        break;
    case IDC_CMD_DUPLICATES:
        m_homeTabLastMode = CSettingsView::MODE_DUPLICATES;
        SwitchSettingsMode(CSettingsView::MODE_DUPLICATES);
        break;
    case IDC_CMD_CHAT:
        OnChat();
        break;

#if FEATURE_COMMAND_UI_COMMAND_CONTROL
    case IDC_CMD_RUN:
        switch (m_homeTabLastMode) {
        case CSettingsView::MODE_RESIZE:    OnResize();   break;
        case CSettingsView::MODE_COMPRESS:  OnCompress(); break;
        case CSettingsView::MODE_META:      OnMeta();     break;
        case CSettingsView::MODE_TRANSFORM: OnRun();      break;
        case CSettingsView::MODE_FIND:      OnFind();     break;
        case CSettingsView::MODE_DUPLICATES: OnDuplicates(); break;
        }
        break;

    case IDC_CMD_PAUSE:        OnPauseBatch();   break;
    case IDC_CMD_RESUME:       OnResumeBatch();  break;
    case IDC_CMD_CANCEL:       OnCancelBatch();  break;
#endif
#if FEATURE_COMMAND_SESSION_PERSISTENCE
    case IDC_CMD_SAVE_SESSION: OnSaveSession();  break;
    case IDC_CMD_LOAD_SESSION: OnLoadSession();  break;
#endif
    case IDC_CMD_PRESETS:       OnPresets();                   break;
    case IDC_CMD_PROVIDER_KEYS: ShowProviderSettingsDlg(GetHwnd()); break;
    case IDC_CMD_ABOUT:         OnHelp();                      break;
    case IDC_CMD_UPDATE_EXPLORER: OnUpdateExplorer();          break;
    case IDC_CMD_UNREGISTER_EXPLORER: OnUnregisterExplorer();  break;
    case IDC_CMD_EXIT:          OnExit();                      break;

#if FEATURE_COMMAND_QUEUE_VIEW
    case IDC_CMD_VIEW_QUEUE:
#endif
#if FEATURE_COMMAND_LOG_VIEW
    case IDC_CMD_VIEW_LOG:
#endif
    case IDC_CMD_VIEW_SETTINGS:
    case IDC_CMD_VIEW_FILETREE:
    case IDC_CMD_VIEW_FINDRESULTS:
    case IDC_CMD_VIEW_DUPLICATERESULTS:
    case IDC_CMD_VIEW_CHAT:
    case IDC_CMD_VIEW_VIEWER_PANEL:
    case IDC_CMD_VIEW_TABBED:
    case IDC_CMD_VIEW_PIN_TAB:
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    case IDC_CMD_VIEW_HOME:
#endif
#ifdef FEATURE_NODES
    case IDC_CMD_VIEW_NODES:
#endif
#ifdef FEATURE_CONSOLE
    case IDC_CMD_VIEW_CONSOLE:
#endif
        RunViewPanelCommand(cmdID);
        SyncViewMenuChecks();
        break;
#ifdef FEATURE_BROWSER
    case IDC_CMD_VIEW_BROWSER:
        ShowBrowserPopup();
        break;
    case IDC_CMD_VIEW_VIEWER_BROWSER:
        ShowViewerBrowserPopup();
        break;
#endif

    case IDC_CMD_RESET_LAYOUT: ResetLayout();    break;
    case IDC_CMD_DEBUG_STATE:  DebugDockState(); break;
    case IDC_CMD_TOGGLE_THEME: ToggleGlobalThemeOverride(); break;
    case IDC_CMD_APP_SETTINGS: OnAppSettings();  break;
    case IDC_CMD_FILE_OPTIONS: OnAppSettings();  break;
#ifdef FEATURE_BROWSER
    case IDC_CMD_FILE_SETTINGS: ShowSettingsPopup(); break;
#endif
    default: break;
    }

    if (cmdID == cmdTabHome) {
#ifdef FEATURE_NODES
        SwitchWorkbench(Workbench::Standard);
#endif
        SwitchSettingsMode(m_homeTabLastMode);
    }
}

int CMainFrame::OnCreate(CREATESTRUCT& cs)
{
    const int r = CDockFrame::OnCreate(cs);
    if (r != 0)
        return r;
#ifdef FEATURE_BROWSER
    m_webViews.ConfigureHost(GetHwnd(),
        CWebViewManager::HostCallbacks{
            [](const std::string& id, const std::string& lvl, const std::string& msg) {
                const std::string line = "[" + id + "] js:" + lvl + " " + msg;
                if (lvl == "error")      logger::error(line);
                else if (lvl == "warn")  logger::warn(line);
                else                     logger::info(line);
            },
            [this](const std::string& id, const std::string& json) {
                if (HandleWebHostCommand(id, json))
                    return;
                logger::info("[" + id + "] msg: " + json.substr(0, 400));
            }
        });
#ifdef FEATURE_CHAT_WEB
    WireChatBus();
#endif
#if defined(FEATURE_VIEWER_WEB) || defined(FEATURE_HOME_PAGE)
    WireFileViewerBus();
#endif
#endif
    if (m_workbenchChrome.show_ribbon_strip) {
        if (!m_ownRibbon.Create(*this))
            return -1;
        if (!m_ownRibbon.BuildRibbonLayout()) {
            CString msg = L"The toolbar could not be loaded.\n\n";
            const wchar_t* detail = own_ribbon::LastRibbonLayoutErrorW();
            if (detail && detail[0] != 0)
                msg += detail;
            else
                msg += L"No detail is available.";
            ::MessageBox(*this, msg, pm::brand::k_app_id_w, MB_OK | MB_ICONERROR);
            return -1;
        }
        const CRect crc = GetClientRect();
        const int  h  = m_ownRibbon.PreferredHeight();
        const int  y0 = OwnRibbonClientTopY();
        m_ownRibbon.SetWindowPos(nullptr, 0, y0, crc.Width(), h, SWP_NOZORDER);
    }
    RecalcLayout();
    return 0;
}

int CMainFrame::OwnRibbonClientTopY() const
{
    if (!GetReBar().IsWindow() || !GetReBar().IsWindowVisible())
        return 0;
    CRect r = GetReBar().GetWindowRect();
    ::MapWindowPoints(HWND_DESKTOP, *const_cast<CMainFrame*>(this), (LPPOINT)&r, 2);
    return r.bottom;
}

CRect CMainFrame::GetViewRect() const
{
    CRect clientRect = CDockFrame::GetClientRect();
    if (GetStatusBar().IsWindow() && GetStatusBar().IsWindowVisible())
        clientRect = ExcludeChildRect(clientRect, GetStatusBar());
    if (GetReBar().IsWindow() && GetReBar().IsWindowVisible())
        clientRect = ExcludeChildRect(clientRect, GetReBar());
    else if (GetToolBar().IsWindow() && GetToolBar().IsWindowVisible())
        clientRect = ExcludeChildRect(clientRect, GetToolBar());
    if (m_ownRibbon.IsWindow() && m_ownRibbon.IsWindowVisible())
        clientRect = ExcludeChildRect(clientRect, m_ownRibbon);
    return clientRect;
}

bool CMainFrame::RibbonHasQueueItems() const
{
    if (!m_pDockQueue)
        return false;
    return m_pDockQueue->GetQueueContainer().GetListView().QueueCount() > 0;
}

#endif // FEATURE_USE_OWN_RIBBON

void CMainFrame::InvalidateBatchUi()
{
#ifdef FEATURE_USE_OWN_RIBBON
    if (m_ownRibbon.IsWindow())
        m_ownRibbon.SyncBatchControls(*this);
#endif
}

void CMainFrame::InvalidateToggle(UINT32 cmdID)
{
#ifdef FEATURE_USE_OWN_RIBBON
    if (m_ownRibbon.IsWindow()) {
        SyncViewMenuChecks();
        m_ownRibbon.m_strip.SyncToggle(cmdID, IsToggleSelected(cmdID));
    }
#endif
}

bool CMainFrame::IsToggleSelected(UINT32 cmdID) const
{
    switch (cmdID) {
#if FEATURE_COMMAND_QUEUE_VIEW
    case IDC_CMD_VIEW_QUEUE:      return IsPanelVisible(m_pDockQueue);
#endif
#if FEATURE_COMMAND_LOG_VIEW
    case IDC_CMD_VIEW_LOG:        return IsPanelVisible(m_pDockLog);
#endif
    case IDC_CMD_VIEW_SETTINGS:   return IsPanelVisible(m_pDockSettings);
    case IDC_CMD_VIEW_FINDRESULTS:      return IsPanelVisible(m_pDockFindResults);
    case IDC_CMD_VIEW_DUPLICATERESULTS: return IsPanelVisible(m_pDockDuplicateResults);
    case IDC_CMD_VIEW_CHAT:
        if (IsChatWorkbench())
            return true;
#ifdef FEATURE_CHAT_WEB
        if (m_pDockChatWeb) return IsPanelVisible(m_pDockChatWeb);
#endif
        return false;
    case IDC_CMD_VIEW_FILETREE:   return IsPanelVisible(m_pDockFileTree);
    case IDC_CMD_VIEW_VIEWER_PANEL: return IsPanelVisible(m_pDockViewerPanel);
    case IDC_CMD_VIEW_TABBED:     return m_viewerManager.IsTabbed();
    case IDC_CMD_VIEW_PIN_TAB:    return m_viewerManager.IsTabbed() && m_centerViewerTabs.IsActivePinned();
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    case IDC_CMD_VIEW_HOME:       return false;
#endif
#ifdef FEATURE_NODES
    case IDC_CMD_VIEW_NODES:      return IsPanelVisible(m_pDockNodes);
#endif
#ifdef FEATURE_CONSOLE
    case IDC_CMD_VIEW_CONSOLE:    return IsPanelVisible(m_pDockConsole);
#endif
#ifdef FEATURE_BROWSER
    case IDC_CMD_VIEW_BROWSER:
        return m_webViews.IsVisible("browser");
#endif
    case IDC_CMD_TOGGLE_THEME:  return pmui::theme_palette().dark;
    }
    return false;
}

// ── Classic menu fallback + panel-button forward ─────────────────────────────

BOOL CMainFrame::OnCommand(WPARAM wparam, LPARAM)
{
#ifdef FEATURE_USE_OWN_RIBBON
    {
        const UINT id = LOWORD(wparam);
        const UINT hi = HIWORD(wparam);
        if (hi == BN_CLICKED || hi == 0) {
            // Toolbar command ids (RibbonUI.h; own-ribbon strip).
            if ((id >= 300 && id <= 430)
#if FEATURE_CUSTOM_COMMANDS
                || own_ribbon::IsCustomRibbonCommandId(id)
#endif
            ) {
                RunRibbonCommandId(id);
                return TRUE;
            }
        }
    }
#endif
    const UINT cmdId = LOWORD(wparam);
    if (cmdId == IDM_RECENT_MENU_DUMMY_FILE || cmdId == IDM_RECENT_MENU_DUMMY_FOLDER)
        return TRUE;
    if (cmdId >= IDM_RECENT_FILE_FIRST && cmdId <= IDM_RECENT_FILE_LAST) {
        OnRecentFileMenu(cmdId);
        return TRUE;
    }
    if (cmdId >= IDM_RECENT_FOLDER_FIRST && cmdId <= IDM_RECENT_FOLDER_LAST) {
        OnRecentFolderMenu(cmdId);
        return TRUE;
    }

    switch (cmdId) {
    case IDM_ADD_FILES:      OnAddFiles();   return TRUE;
    case IDM_ADD_FOLDER:     OnAddFolder();  return TRUE;
    case IDM_CLEAR_QUEUE:    OnClearQueue(); return TRUE;
    case IDM_RESIZE:         OnResize();     return TRUE;
    case IDM_FILE_SAVE:      OnFileSaveImagePreview(); return TRUE;
    case IDM_FILE_SAVE_AS:   OnFileSaveImagePreviewAs(); return TRUE;
    case IDM_FILE_SAVE_LAYOUT: OnFileSaveLayout(); return TRUE;
    case IDM_FILE_LOAD_LAYOUT: OnFileLoadLayout(); return TRUE;
    case IDC_CMD_RESET_LAYOUT: ResetLayout(); return TRUE;
    case IDM_EXIT:           OnExit();       return TRUE;
    case IDM_ABOUT:          return OnHelp();
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
    case IDM_PIXLWIZ_LOGIN:  OnPixlwizLogin(); return TRUE;
    case IDM_PIXLWIZ_LOGOUT: OnPixlwizLogout(); return TRUE;
#endif
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    case IDM_PIXLWIZ_SHARE:  OnPixlwizShare(); return TRUE;
#endif
    case IDC_CMD_FILE_OPTIONS: OnAppSettings(); return TRUE;
#ifdef FEATURE_BROWSER
    case IDC_CMD_FILE_SETTINGS: ShowSettingsPopup(); return TRUE;
#endif
    case IDC_CMD_VIEW_TABBED: RunViewPanelCommand(IDC_CMD_VIEW_TABBED); return TRUE;
    case IDC_CMD_VIEW_PIN_TAB: RunViewPanelCommand(IDC_CMD_VIEW_PIN_TAB); return TRUE;
    case IDC_LOG_PANEL_COPY:
        if (m_pDockLog)
            m_pDockLog->GetLogContainer().GetLogView().CopyAllToClipboard();
        return TRUE;
    case IDC_LOG_PANEL_CLEAR:
        if (m_pDockLog)
            m_pDockLog->GetLogContainer().ClearLog();
        return TRUE;
    case IDC_CMD_UPDATE_EXPLORER: OnUpdateExplorer(); return TRUE;
    case IDC_CMD_UNREGISTER_EXPLORER: OnUnregisterExplorer(); return TRUE;
    // View / ribbon commands when the classic menu dispatches them (not 300–430 own-ribbon band).
#if FEATURE_COMMAND_QUEUE_VIEW
    case IDC_CMD_VIEW_QUEUE:
#endif
#if FEATURE_COMMAND_LOG_VIEW
    case IDC_CMD_VIEW_LOG:
#endif
    case IDC_CMD_VIEW_SETTINGS:
    case IDC_CMD_VIEW_FINDRESULTS:
    case IDC_CMD_VIEW_DUPLICATERESULTS:
    case IDC_CMD_VIEW_CHAT:
    case IDC_CMD_VIEW_FILETREE:
    case IDC_CMD_VIEW_VIEWER_PANEL:
#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
    case IDC_CMD_VIEW_HOME:
#endif
#ifdef FEATURE_NODES
    case IDC_CMD_VIEW_NODES:
#endif
#ifdef FEATURE_CONSOLE
    case IDC_CMD_VIEW_CONSOLE:
#endif
        RunViewPanelCommand(cmdId);
        SyncViewMenuChecks();
        return TRUE;
#ifdef FEATURE_BROWSER
    case IDC_CMD_VIEW_BROWSER:
        ShowBrowserPopup();
        return TRUE;
    case IDC_CMD_VIEW_VIEWER_BROWSER:
        ShowViewerBrowserPopup();
        return TRUE;
#endif
    // Buttons inside the Settings panel forward via SendMessage(ancestor):
    case IDC_CMD_PRESETS:       OnPresets();                       return TRUE;
    case IDC_CMD_PROVIDER_KEYS: ShowProviderSettingsDlg(GetHwnd()); return TRUE;
    case IDW_VIEW_STATUSBAR: return OnViewStatusBar();
    case IDW_VIEW_TOOLBAR:   return OnViewToolBar();
    }
    return FALSE;
}

BOOL CMainFrame::OnHelp()
{
    ::MessageBox(GetHwnd(), pm::brand::k_w_about_dialog_text_w, pm::brand::k_w_about_caption_w, MB_ICONINFORMATION | MB_OK);
    return TRUE;
}

void CMainFrame::OnExit() { Close(); }

void CMainFrame::OnFileSaveImagePreview()
{
    CString err;
    if (!m_viewerManager.ActiveView().SaveImagePreviewOverwrite(err) && !err.IsEmpty())
        ::MessageBoxW(GetHwnd(), err, L"Save", MB_OK | MB_ICONINFORMATION);
}

void CMainFrame::OnFileSaveImagePreviewAs()
{
    CString err;
    if (!m_viewerManager.ActiveView().SaveImagePreviewSaveAs(GetHwnd(), err) && !err.IsEmpty())
        ::MessageBoxW(GetHwnd(), err, L"Save As", MB_OK | MB_ICONINFORMATION);
}

#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
void CMainFrame::OnPixlwizLogin()
{
    if (m_pixlwizLoginBusy) {
        ::MessageBoxW(GetHwnd(), L"Login is already running. Wait for it to finish, then try again.", L"Pixlwiz",
                      MB_OK | MB_ICONINFORMATION);
        return;
    }
    m_pixlwizLoginBusy = true;
    pmui::StartPixlwizLoginAsync(GetHwnd());
    LogMessage(L"[Pixlwiz] Login: same ZITADEL PKCE flow as pm-image login (hidden child). Issuer and OAuth client "
               L"id default from built-in constants when .env is absent — optional. The system browser opens; "
               L"[zitadel-login] lines appear in this log when the run finishes.");
}

LRESULT CMainFrame::OnPixlwizLoginDone(WPARAM, LPARAM lparam)
{
    m_pixlwizLoginBusy = false;
    pmui::ui_log_file_event("pixlwiz: menu login worker finished");

    auto* raw = reinterpret_cast<pmui::PixlwizLoginDonePayload*>(lparam);
    if (!raw)
        return 0;
    std::unique_ptr<pmui::PixlwizLoginDonePayload> p(raw);

    if (!p->create_process_ok) {
        CString msg(L"[Pixlwiz] Login: could not start — ");
        msg += p->spawn_error.c_str();
        if (p->win32_error != 0)
            msg.AppendFormat(L" (Win32 error %lu)", static_cast<unsigned long>(p->win32_error));
        LogMessage(msg);
        ::MessageBoxW(GetHwnd(), msg, L"Pixlwiz", MB_OK | MB_ICONERROR);
        return 0;
    }

    if (!p->stdio_log_utf16.empty()) {
        const std::wstring& blob = p->stdio_log_utf16;
        for (size_t i = 0; i < blob.size();) {
            const size_t j = blob.find(L'\n', i);
            std::wstring line = (j == std::wstring::npos) ? blob.substr(i) : blob.substr(i, j - i);
            if (!line.empty() && line.back() == L'\r')
                line.pop_back();
            if (!line.empty())
                LogMessage(CString(L"[Pixlwiz login] ") + line.c_str());
            if (j == std::wstring::npos)
                break;
            i = j + 1;
        }
    }

    if (p->exit_code != 0) {
        CString summary;
        summary.Format(L"[Pixlwiz] Login: failed (exit code %lu). See [Pixlwiz login] lines above.",
                       static_cast<unsigned long>(p->exit_code));
        LogMessage(summary);
        CString box = summary + L"\n\n";
        std::wstring tailw = p->stdio_log_utf16;
        constexpr size_t k_max_tail = 2800;
        if (tailw.size() > k_max_tail)
            tailw = std::wstring(L"…\n") + tailw.substr(tailw.size() - k_max_tail);
        if (tailw.empty())
            box += L"(no stderr captured from the login child - run pm-image --no-mcp login in a terminal to see "
                   L"[zitadel-login] lines. Issuer and client id default from built-in constants when env is unset; "
                   L"if login still fails, check ZITADEL redirect URIs, firewall, and .env next to pm-image.exe.)";
        else
            box += tailw.c_str();
        ::MessageBoxW(GetHwnd(), box, L"Pixlwiz Login", MB_OK | MB_ICONWARNING);
        return 0;
    }

    LogMessage(L"[Pixlwiz] Login: signed in (tokens saved).");
#ifdef FEATURE_CHAT_WEB
    if (m_pDockChatWeb)
        m_pDockChatWeb->GetChatWebContainer().GetChatWebView().PostPixlwizAuthToWeb();
    if (m_workbenchClientChatWeb.IsWindow())
        m_workbenchClientChatWeb.PostPixlwizAuthToWeb();
#endif
    return 0;
}

void CMainFrame::OnPixlwizLogout()
{
    std::string err;
    if (!pm_zitadel_oauth_clear(err)) {
        const std::wstring werr = pmui::utf8_to_wide(err);
        LogMessage(CString(L"[Pixlwiz] Logout failed: ") + CString(werr.c_str()));
        ::MessageBoxW(GetHwnd(), werr.c_str(), L"Pixlwiz", MB_OK | MB_ICONWARNING);
        return;
    }
    LogMessage(L"[Pixlwiz] Logout: signed out (zitadel-oauth.json removed if it existed).");
#ifdef FEATURE_CHAT_WEB
    if (m_pDockChatWeb)
        m_pDockChatWeb->GetChatWebContainer().GetChatWebView().PostPixlwizAuthToWeb();
    if (m_workbenchClientChatWeb.IsWindow())
        m_workbenchClientChatWeb.PostPixlwizAuthToWeb();
#endif
}
#endif // FEATURE_PIXLWIZ_AUTH

#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
namespace {

struct PixlwizShareDoneMsg {
    bool         ok = false;
    std::wstring err_w;
    std::wstring url_w;
    size_t       pic_count = 0;
    std::wstring post_id_w;
};

} // namespace

namespace pixlwiz_share {

/** `https://host/post/{id}?view=compact&pic={first}` (first picture id optional but included when present). */
std::string build_post_compact_view_url_utf8(const std::string& base_no_slash, const std::string& post_id,
                                             const std::vector<std::string>& picture_ids)
{
    std::string u = base_no_slash + "/post/" + post_id + "?view=compact";
    if (!picture_ids.empty() && !picture_ids[0].empty())
        u += "&pic=" + picture_ids[0];
    return u;
}

std::wstring path_ext_lower_w(const fs::path& p)
{
    std::wstring e = p.extension().wstring();
    for (wchar_t& c : e)
        c = static_cast<wchar_t>(towlower(c));
    return e;
}

/** Explorer selection: image files plus images one level deep inside selected folders. */
void collect_pixlwiz_share_image_files(const std::vector<std::wstring>& sel, std::vector<fs::path>& out)
{
    out.clear();
    for (const std::wstring& w : sel) {
        std::error_code ec;
        const fs::path  p(w);
        if (fs::is_regular_file(p, ec)) {
            if (pmui::is_image_ext(path_ext_lower_w(p))) {
                const fs::path abs = fs::absolute(p, ec);
                if (!ec)
                    out.push_back(abs);
            }
            continue;
        }
        if (!fs::is_directory(p, ec))
            continue;
        std::error_code                     d_ec;
        const auto                          dir_opts = fs::directory_options::skip_permission_denied;
        const fs::directory_iterator        end_it;
        for (fs::directory_iterator it(p, dir_opts, d_ec); it != end_it; it.increment(d_ec)) {
            if (d_ec)
                break;
            const fs::path f = it->path();
            if (!it->is_regular_file())
                continue;
            if (!pmui::is_image_ext(path_ext_lower_w(f)))
                continue;
            std::error_code e2;
            const fs::path abs2 = fs::absolute(f, e2);
            if (!e2)
                out.push_back(abs2);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

inline bool post_pixlwiz_share_progress(HWND hwnd, int row, std::string_view u8)
{
    if (!hwnd)
        return false;
    auto* p = new std::wstring(pmui::utf8_to_wide(std::string(u8)));
    if (!::PostMessageW(hwnd, UWM_PIXLWIZ_SHARE_PROGRESS, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(p))) {
        delete p;
        return false;
    }
    return true;
}

} // namespace pixlwiz_share

void CMainFrame::OnPixlwizShare()
{
    std::string base = pm_resolve_service_server_base("");
    if (base.empty())
        base = pm_trim_trailing_slash(std::string(pm::k_pixlwiz_service_server_base_default_u8));
    if (base.empty()) {
        LogMessage(L"[Pixlwiz] Share: aborted — SERVER_URL / VITE_SERVER_IMAGE_API_URL not set.");
        ::MessageBoxW(GetHwnd(),
                      L"Set SERVER_URL or VITE_SERVER_IMAGE_API_URL (e.g. https://pixlwiz.com) for your environment.",
                      L"Pixlwiz Share", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string bearer;
    std::string aerr;
    if (!pm_zitadel_oauth_read_access_token(bearer, aerr)) {
        LogMessage(CString(L"[Pixlwiz] Share: no Bearer token — ") + CString(pmui::utf8_to_wide(aerr).c_str()));
        ::MessageBoxW(GetHwnd(), pmui::utf8_to_wide(aerr).c_str(), L"Pixlwiz Share", MB_OK | MB_ICONWARNING);
        return;
    }

    if (m_explorerSelectionPaths.empty()) {
        LogMessage(L"[Pixlwiz] Share: aborted — nothing selected in Explorer.");
        ::MessageBoxW(GetHwnd(),
                      L"Select one or more images (or a folder of images) in Explorer, then try Share again.",
                      L"Pixlwiz Share", MB_OK | MB_ICONINFORMATION);
        if (HWND r = ::GetAncestor(GetHwnd(), GA_ROOT))
            ::SendMessageW(r, WM_COMMAND, MAKEWPARAM(IDC_CMD_VIEW_FILETREE, 0), 0);
        return;
    }

    PixlwizSharePostFields share_fields;
    if (!RunPixlwizSharePostDialog(GetHwnd(), share_fields))
        return;

    if (m_pixlwizShareInProgress) {
        ::MessageBoxW(GetHwnd(), L"A Pixlwiz share is already running. Wait for it to finish.", L"Pixlwiz Share",
                      MB_OK | MB_ICONINFORMATION);
        return;
    }

    HWND hwnd = GetHwnd();
    int  qrow = -1;
    if (m_pDockQueue) {
        EnsurePanelVisible(m_pDockQueue, DS_DOCKED_BOTTOM, GetDockAncestor(), DpiScaleInt(220), IDC_CMD_VIEW_QUEUE);
        qrow = m_pDockQueue->GetQueueContainer().GetListView().AddToolCallRow(L"Pixlwiz Share", L"Share", L"Starting\u2026", L"");
    }
    m_pixlwizShareQueueRow     = qrow;
    m_pixlwizShareInProgress   = true;

    LogMessage(CString(L"[Pixlwiz] Share: started (background) to ") + CString(pmui::utf8_to_wide(base).c_str()) + L" …");

    std::vector<std::wstring> sel_paths = m_explorerSelectionPaths;
    const std::wstring        title_w   = share_fields.title;
    const std::wstring        desc_w    = share_fields.description;
    const std::string         vis       = share_fields.visibility;

    std::thread([hwnd, qrow, base, bearer, sel_paths = std::move(sel_paths), title_w, desc_w, vis]() mutable {
        const auto post = [hwnd, qrow](std::string_view u8) { pixlwiz_share::post_pixlwiz_share_progress(hwnd, qrow, u8); };

        post("Gathering images from selection…");
        std::vector<fs::path> images;
        pixlwiz_share::collect_pixlwiz_share_image_files(sel_paths, images);
        if (images.empty()) {
            post("No image files found in selection.");
            auto* done     = new PixlwizShareDoneMsg;
            done->ok       = false;
            done->err_w    = L"No image files in the current Explorer selection.";
            done->pic_count = 0;
            (void)::PostMessageW(hwnd, UWM_PIXLWIZ_SHARE_DONE, 0, reinterpret_cast<LPARAM>(done));
            return;
        }

        {
            std::string msg = "Found ";
            msg += std::to_string(images.size());
            msg += " image(s).";
            post(msg);
        }

        const PmServiceUploadLogLine log = [](std::string_view) {};
        const PmServicePostProgressLine prog = [post](std::string_view line) { post(line); };
        const PmServiceCreatePostPicturesResult r =
            pm_service_create_post_with_pictures(base, bearer, images, wide_to_utf8(title_w), wide_to_utf8(desc_w), vis,
                                                 log, prog);

        auto* done = new PixlwizShareDoneMsg;
        done->ok   = r.ok;
        if (!r.ok) {
            done->err_w = pmui::utf8_to_wide(r.err);
        } else {
            done->post_id_w = pmui::utf8_to_wide(r.post_id);
            done->pic_count = r.picture_ids.size();
            done->url_w =
                pmui::utf8_to_wide(pixlwiz_share::build_post_compact_view_url_utf8(base, r.post_id, r.picture_ids));
        }
        (void)::PostMessageW(hwnd, UWM_PIXLWIZ_SHARE_DONE, 0, reinterpret_cast<LPARAM>(done));
    }).detach();
}

LRESULT CMainFrame::OnPixlwizShareProgress(WPARAM wparam, LPARAM lparam)
{
    auto* text = reinterpret_cast<std::wstring*>(lparam);
    if (!text)
        return 0;
    std::wstring s = std::move(*text);
    delete text;

    constexpr size_t kStatusMax = 200;
    if (s.size() > kStatusMax) {
        s.resize(kStatusMax - 1);
        s += L"\u2026";
    }

    const int row = static_cast<int>(wparam);
    if (row >= 0 && m_pDockQueue && row == m_pixlwizShareQueueRow && row < m_pDockQueue->GetQueueContainer().GetListView().QueueCount())
        m_pDockQueue->GetQueueContainer().GetListView().SetItemStatus(row, s.c_str());
    else
        SetStatusBarPartText(0, s.c_str());
    return 0;
}

LRESULT CMainFrame::OnPixlwizShareDone(WPARAM, LPARAM lparam)
{
    auto* payload = reinterpret_cast<PixlwizShareDoneMsg*>(lparam);
    if (!payload)
        return 0;
    std::unique_ptr<PixlwizShareDoneMsg> holder(payload);

    m_pixlwizShareInProgress = false;
    const int                qrow = m_pixlwizShareQueueRow;
    m_pixlwizShareQueueRow   = -1;

    if (m_pDockQueue && qrow >= 0 && qrow < m_pDockQueue->GetQueueContainer().GetListView().QueueCount()) {
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        lv.SetItemStatus(qrow, holder->ok ? L"\u2713 Shared" : L"\u2717 Share failed");
    }

    if (!holder->ok) {
        LogMessage(CString(L"[Pixlwiz] Share failed: ") + CString(holder->err_w.c_str()));
        ::MessageBoxW(GetHwnd(), holder->err_w.c_str(), L"Pixlwiz Share", MB_OK | MB_ICONERROR);
        return 0;
    }

    const std::wstring& url_w = holder->url_w;

    {
        CString okLine(L"[Pixlwiz] Share: post created, post_id=");
        okLine += CString(holder->post_id_w.c_str());
        okLine += L", pictures=";
        okLine += CString(std::to_wstring(holder->pic_count).c_str());
        okLine += L", url=";
        okLine += CString(url_w.c_str());
        LogMessage(okLine);
    }

    RunPixlwizShareSuccessDialog(GetHwnd(), url_w, holder->pic_count);
    return 0;
}
#endif // FEATURE_PIXLWIZ_SHARE

void CMainFrame::OnUpdateExplorer()
{
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        ::MessageBoxW(GetHwnd(), L"Could not get the application path.",
                        L"Update Explorer", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring cmd = L"\"";
    cmd.append(path);
    cmd += L"\" register-explorer";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        const DWORD err = GetLastError();
        wchar_t line[256]{};
        swprintf_s(line, L"Could not start register-explorer (error %lu).", err);
        ::MessageBoxW(GetHwnd(), line, L"Update Explorer", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    (void)GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    if (exit_code == 0) {
        ::MessageBoxW(GetHwnd(),
                      pm::brand::k_w_reg_explorer_shell_ok_w,
                      L"Update Explorer", MB_OK | MB_ICONINFORMATION);
    } else {
        ::MessageBoxW(
            GetHwnd(),
            pm::brand::k_w_reg_explorer_run_fail_w,
            L"Update Explorer", MB_OK | MB_ICONWARNING);
    }
}

void CMainFrame::OnUnregisterExplorer()
{
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        ::MessageBoxW(GetHwnd(), L"Could not get the application path.",
                      L"Remove Explorer shell", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring cmd = L"\"";
    cmd.append(path);
    cmd += L"\" register-explorer --unregister";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        const DWORD err = GetLastError();
        wchar_t line[256]{};
        swprintf_s(line, L"Could not start register-explorer --unregister (error %lu).", err);
        ::MessageBoxW(GetHwnd(), line, L"Remove Explorer shell", MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    (void)GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    if (exit_code == 0) {
        ::MessageBoxW(GetHwnd(),
                      L"PM Media was removed from the Windows Explorer context menu.",
                      L"Remove Explorer shell", MB_OK | MB_ICONINFORMATION);
    } else {
        ::MessageBoxW(
            GetHwnd(),
            pm::brand::k_w_reg_explorer_unreg_fail_w,
            L"Remove Explorer shell", MB_OK | MB_ICONWARNING);
    }
}

// ── Fullscreen ────────────────────────────────────────────────────────────────
// Removes the window chrome and covers the nearest monitor.
// A second call (or ESC / F11 / ALT+F) restores the previous state.
void CMainFrame::ToggleFullscreen()
{
    HWND hwnd = GetHwnd();
    if (!m_isFullscreen) {
        // Save current state.
        m_savedWinPlacement = {};
        m_savedWinPlacement.length = sizeof(m_savedWinPlacement);
        ::GetWindowPlacement(hwnd, &m_savedWinPlacement);
        m_savedWinStyle   = ::GetWindowLongW(hwnd, GWL_STYLE);
        m_savedWinExStyle = ::GetWindowLongW(hwnd, GWL_EXSTYLE);

        // Remove all chrome: caption, resize border, scroll-bar bits.
        LONG style = m_savedWinStyle;
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                   WS_MAXIMIZEBOX | WS_SYSMENU);
        ::SetWindowLongW(hwnd, GWL_STYLE,   style);
        ::SetWindowLongW(hwnd, GWL_EXSTYLE, m_savedWinExStyle & ~WS_EX_WINDOWEDGE);

        // Cover the full monitor (not just the work area).
        HMONITOR hMon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        ::GetMonitorInfoW(hMon, &mi);
        ::SetWindowPos(hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right  - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        m_isFullscreen = true;
    } else {
        // Restore chrome and placement.
        ::SetWindowLongW(hwnd, GWL_STYLE,   m_savedWinStyle);
        ::SetWindowLongW(hwnd, GWL_EXSTYLE, m_savedWinExStyle);
        ::SetWindowPos(hwnd, HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
        ::SetWindowPlacement(hwnd, &m_savedWinPlacement);
        m_isFullscreen = false;
    }
}

// ── UWM message handlers ──────────────────────────────────────────────────────

LRESULT CMainFrame::OnGetMinMaxInfo(UINT msg, WPARAM wparam, LPARAM lparam)
{
    LPMINMAXINFO lpMMI = reinterpret_cast<LPMINMAXINFO>(lparam);
    lpMMI->ptMinTrackSize.x = DpiScaleInt(600);
    lpMMI->ptMinTrackSize.y = DpiScaleInt(400);
    return FinalWindowProc(msg, wparam, lparam);
}

LRESULT CMainFrame::OnQueueProgress(WPARAM wparam, LPARAM lparam)
{
    int idx   = static_cast<int>(wparam);
    int state = static_cast<int>(lparam);

    // States 2,3 (resize done/err), 5,6 (compress), 8,9 (meta) = item finished.
    if (state == 2 || state == 3 || state == 5 || state == 6 || state == 8 || state == 9) {
        ++m_queueDone;
        UpdateQueueStatusPart();
    }

    // idx < 0  →  Explorer-sourced item; no queue row to update.
    if (idx < 0) {
        // Explorer-sourced item — status bar only.
        if      (state == 1) SetStatusBarPartText(0, L"Resizing\u2026");
        else if (state == 2) SetStatusBarPartText(0, L"\u2713 Resize done");
        else if (state == 3) SetStatusBarPartText(0, L"\u2717 Resize error");
        else if (state == 4) SetStatusBarPartText(0, L"Compressing\u2026");
        else if (state == 5) SetStatusBarPartText(0, L"\u2713 Compress done");
        else if (state == 6) SetStatusBarPartText(0, L"\u2717 Compress error");
        else if (state == 7) SetStatusBarPartText(0, L"Meta\u2026");
        else if (state == 8) SetStatusBarPartText(0, L"\u2713 Meta done");
        else if (state == 9) SetStatusBarPartText(0, L"\u2717 Meta error");
        return 0;
    }
    if (!m_pDockQueue) return 0;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
    if (idx >= lv.QueueCount()) return 0;
    switch (state) {
    case 1: lv.SetItemStatus(idx, L"Resizing\u2026");      break;
    case 2: lv.SetItemStatus(idx, L"\u2713 Done");          break;
    case 3: lv.SetItemStatus(idx, L"\u2717 Error");         break;
    case 4: lv.SetItemStatus(idx, L"Compressing\u2026");   break;
    case 5: lv.SetItemStatus(idx, L"\u2713 Compressed");   break;
    case 6: lv.SetItemStatus(idx, L"\u2717 Compress Err"); break;
    case 7: lv.SetItemStatus(idx, L"Meta\u2026");          break;
    case 8: lv.SetItemStatus(idx, L"\u2713 Meta");         break;
    case 9: lv.SetItemStatus(idx, L"\u2717 Meta Err");     break;
    }
    return 0;
}

LRESULT CMainFrame::OnQueueOpStatus(WPARAM wparam, LPARAM lparam)
{
    auto* s = reinterpret_cast<std::wstring*>(lparam);
    if (!s) return 0;
    std::wstring text = std::move(*s);
    delete s;
    // Wider Status column: keep more of log lines; still cap for list stability.
    constexpr size_t kStatusMax = 200;
    if (text.size() > kStatusMax) {
        text.resize(kStatusMax - 1);
        text += L"\u2026";
    }
    if (!m_pDockQueue) return 0;
    const int row = (int)(INT_PTR)wparam;
    if (row < 0) return 0;
    bool allowed = false;
    for (int r : m_queueOpRowIndices) {
        if (r == row) {
            allowed = true;
            break;
        }
    }
    if (!allowed) return 0;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
    if (row >= lv.QueueCount()) return 0;
    lv.SetItemStatus(row, text.c_str());
    return 0;
}

LRESULT CMainFrame::OnQueueDone(WPARAM wparam, LPARAM lparam)
{
    m_processing    = false;
    m_queueTotal    = 0;
    m_queueDone     = 0;
    UpdateQueueStatusPart();
    InvalidateBatchUi();
    CString s;
    s.Format(L"Done: %d succeeded, %d failed", (int)wparam, (int)lparam);
    SetStatusBarPartText(0, s);
    LogMessage(s);
    if (lparam > 0)
        ::MessageBox(GetHwnd(), s, pm::brand::k_app_id_w, MB_ICONWARNING);
    return 0;
}

LRESULT CMainFrame::OnTransformProgress(WPARAM wparam, LPARAM lparam)
{
    int idx   = static_cast<int>(wparam);
    int state = static_cast<int>(lparam);

    if (state == 2 || state == 3) { ++m_queueDone; UpdateQueueStatusPart(); }

    if (idx < 0) {
        if      (state == 1) SetStatusBarPartText(0, L"Transforming\u2026");
        else if (state == 2) SetStatusBarPartText(0, L"\u2713 Transform done");
        else                 SetStatusBarPartText(0, L"\u2717 AI Error");
        return 0;
    }
    if (!m_pDockQueue) return 0;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
    if      (state == 1) lv.SetItemStatus(idx, L"Transforming\u2026");
    else if (state == 2) lv.SetItemStatus(idx, L"\u2713 Transformed");
    else                 lv.SetItemStatus(idx, L"\u2717 AI Error");
    return 0;
}

LRESULT CMainFrame::OnTransformDone(WPARAM wparam, LPARAM lparam)
{
    m_processing = false;
    m_queueTotal = 0; m_queueDone = 0; UpdateQueueStatusPart();
    InvalidateBatchUi();
    CString s;
    s.Format(L"Transform done: %d succeeded, %d failed", (int)wparam, (int)lparam);
    SetStatusBarPartText(0, s);
    LogMessage(s);
    if (lparam > 0)
        ::MessageBox(GetHwnd(), s, pm::brand::k_app_id_w, MB_ICONWARNING);
    return 0;
}

LRESULT CMainFrame::OnLogMessage(WPARAM wparam)
{
    auto* ws = reinterpret_cast<wchar_t*>(wparam);
    if (ws) { LogMessage(CString(ws)); delete[] ws; }
    return 0;
}

// ── Find panel handlers ──────────────────────────────────────────────────────

LRESULT CMainFrame::OnFindProgress(WPARAM wparam, LPARAM lparam)
{
    auto* batch = reinterpret_cast<std::vector<FindProgressRow>*>(wparam);
    EnsurePanelVisible(m_pDockFindResults, DS_DOCKED_RIGHT,
                       GetDockAncestor(), DpiScaleInt(360),
                       IDC_CMD_VIEW_FINDRESULTS);
    if (!m_pDockFindResults) {
        delete batch;
        return 0;
    }
    // lparam != 0: replace list (web chat `image_find`). Ribbon Find clears up-front.
    if (lparam) {
        m_pDockFindResults->GetFindResultsContainer().GetView().ClearAll();
    }
    if (batch) {
        auto& v = m_pDockFindResults->GetFindResultsContainer().GetView();
        for (const auto& r : *batch)
            v.AddResult(r.path.c_str(), r.score, r.source.c_str(), r.reason.c_str());
        delete batch;
    }
    return 0;
}

LRESULT CMainFrame::OnFindDone(WPARAM wparam, LPARAM lparam)
{
    const bool ok    = wparam != 0;
    const int  count = (int)lparam;
    m_processing = false;
    m_queueTotal = 0; m_queueDone = 0; UpdateQueueStatusPart();
    InvalidateBatchUi();
    if (m_pDockQueue && !m_queueOpRowIndices.empty()) {
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        for (int idx : m_queueOpRowIndices) {
            if (idx < 0 || idx >= lv.QueueCount()) continue;
            lv.SetItemStatus(idx, ok ? L"\u2713 Find" : L"\u2717 Find");
        }
        m_queueOpRowIndices.clear();
    }
    CString s; s.Format(L"Find %s — %d match%s",
                        ok ? L"finished" : L"failed",
                        count, count == 1 ? L"" : L"es");
    SetStatusBarPartText(0, s);
    LogMessage(s);
    return 0;
}

void CMainFrame::OnFindItemClicked(int item)
{
    if (item < 0 || !m_pDockFindResults) return;
    auto& v = m_pDockFindResults->GetFindResultsContainer().GetView();
    CString path = v.GetItemPath(item);
    if (path.IsEmpty()) return;

    namespace fs = std::filesystem;
    auto ext = fs::path(path.c_str()).extension().wstring();
    for (auto& c : ext) c = (wchar_t)towlower(c);
    if (!IsChatWorkbench() && pmui::is_image_ext(ext)) {
        m_viewerManager.ActiveView().LoadPicture(path.c_str());
    }
    CString status; status.Format(L"Find: %s", fs::path(path.c_str()).filename().c_str());
    SetStatusBarPartText(0, status);
}

void CMainFrame::OnFindRevealInExplorer(const std::wstring& path)
{
    if (!m_pDockFileTree) return;
    if (!IsPanelVisible(m_pDockFileTree))
        TogglePanelView(m_pDockFileTree, DS_DOCKED_LEFT,
                        GetDockAncestor(), DpiScaleInt(240), IDC_CMD_VIEW_FILETREE);
    ScheduleFileTreeBrowse({ path });
}

LRESULT CMainFrame::OnDuplicatesDone(WPARAM wparam, LPARAM)
{
    auto* res = reinterpret_cast<DuplicatesUiResult*>(wparam);
    m_processing = false;
    m_queueTotal = 0;
    m_queueDone  = 0;
    UpdateQueueStatusPart();
    InvalidateBatchUi();
    if (m_pDockQueue && !m_queueOpRowIndices.empty()) {
        auto&        lv = m_pDockQueue->GetQueueContainer().GetListView();
        const bool   ok = res && res->ok;
        for (int idx : m_queueOpRowIndices) {
            if (idx < 0 || idx >= lv.QueueCount()) continue;
            lv.SetItemStatus(idx, ok ? L"\u2713 Duplicates" : L"\u2717 Duplicates");
        }
        m_queueOpRowIndices.clear();
    }
    if (!res) {
        SetStatusBarPartText(0, L"Duplicates: no result");
        return 0;
    }
    EnsurePanelVisible(m_pDockDuplicateResults, DS_DOCKED_RIGHT, GetDockAncestor(), DpiScaleInt(360),
        IDC_CMD_VIEW_DUPLICATERESULTS);
    if (m_pDockDuplicateResults) {
        auto& v = m_pDockDuplicateResults->GetDuplicateResultsContainer().GetView();
        if (res->ok) {
            v.SetRows(res->rows);
            m_lastDupReport = res->report;
        } else {
            v.ClearAll();
            m_lastDupReport = nlohmann::json::object();
            if (!res->error.empty()) {
                LogMessage(CString(res->error.c_str()));
                ::MessageBoxW(GetHwnd(), res->error.c_str(), L"Duplicates", MB_ICONWARNING | MB_OK);
            }
        }
    }
    if (res->ok) {
        const int n = (int)res->rows.size();
        CString s;
        s.Format(L"Duplicates: %d group%s", n, n == 1 ? L"" : L"s");
        SetStatusBarPartText(0, s);
        LogMessage(s);
    } else
        SetStatusBarPartText(0, L"Duplicates failed");
    delete res;
    return 0;
}

void CMainFrame::OnDuplicateItemClicked(UINT_PTR itemData)
{
    if (itemData == 0 || !m_pDockDuplicateResults) return;
    auto& v = m_pDockDuplicateResults->GetDuplicateResultsContainer().GetView();
    const std::wstring wpath = v.GetPathForItemData(itemData);
    if (wpath.empty()) return;
    const CString path(wpath.c_str());
    if (path.IsEmpty()) return;
    auto ext = fs::path(path.c_str()).extension().wstring();
    for (auto& c : ext) c = (wchar_t)towlower(c);
    if (!IsChatWorkbench() && pmui::is_image_ext(ext)) m_viewerManager.ActiveView().LoadPicture(path.c_str());
    const DupListRow* row   = v.GetRowForItemData(itemData);
    const std::wstring  pairBlock
        = (row && !m_lastDupReport.is_null() && m_lastDupReport.is_object())
            ? build_duplicate_report_detail_for_selection(m_lastDupReport, wpath, row->paths)
            : std::wstring();
    if (!IsChatWorkbench()) {
        if (row && !row->paths.empty()) {
            m_viewerManager.ActiveView().SetFileInfoText(
                pmui::format_duplicate_group_file_info_text(
                    path.c_str(), row->paths, row->method.c_str(), row->key.c_str(),
                    pairBlock.empty() ? nullptr : pairBlock.c_str())
                    .c_str());
        } else
            m_viewerManager.ActiveView().SetFileInfoFromPath(path.c_str());
    }
    CString status; status.Format(L"Duplicates: %s", fs::path(path.c_str()).filename().c_str());
    if (row) {
        for (size_t i = 0; i < row->paths.size(); ++i) {
            if (row->paths[i] != wpath) continue;
            if (i < row->pathSubtext.size() && !row->pathSubtext[i].empty()) {
                status.AppendFormat(L"  \u00b7  %s", row->pathSubtext[i].c_str());
            }
            break;
        }
    }
    SetStatusBarPartText(0, status);
}

void CMainFrame::OnDuplicatesSaveSession()
{
    if (m_lastDupReport.is_null() || m_lastDupReport.empty()) {
        ::MessageBoxW(GetHwnd(), L"Run Duplicates or open a session first.", L"Save session", MB_OK | MB_ICONINFORMATION);
        return;
    }
    CFileDialog dlg(FALSE, L"json", L"duplicates.pm-duplicates.json",
                    OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
                    L"JSON (*.json)\0*.json\0All files\0*.*\0\0");
    dlg.SetTitle(L"Save duplicates session");
    if (dlg.DoModal(*this) != IDOK) return;
    CString p = dlg.GetPathName();
    try {
        std::ofstream f(p.GetString());
        f << m_lastDupReport.dump(2) << "\n";
    } catch (const std::exception& e) {
        CString m;
        m.Format(L"Write failed: %S", e.what());
        ::MessageBoxW(GetHwnd(), m, L"Save session", MB_OK | MB_ICONERROR);
        return;
    }
    LogMessage(CString(L"Duplicates session saved: ") + p);
}

void CMainFrame::OnDuplicatesOpenSession()
{
    CFileDialog dlg(TRUE, L"json", nullptr, OFN_FILEMUSTEXIST,
                    L"JSON / session (*.json;*.pm-duplicates.json)\0*.json;*.pm-duplicates.json\0"
                    L"All files\0*.*\0\0");
    dlg.SetTitle(L"Open duplicates session");
    if (dlg.DoModal(*this) != IDOK) return;
    std::ifstream ifs(dlg.GetPathName().GetString());
    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const std::exception& e) {
        CString m;
        m.Format(L"JSON read failed: %S", e.what());
        ::MessageBoxW(GetHwnd(), m, L"Open session", MB_OK | MB_ICONERROR);
        return;
    }
    std::string err;
    if (!media::validate_duplicates_session_json(j, err)) {
        CString m;
        m.Format(L"Not a valid duplicates file: %S", err.c_str());
        ::MessageBoxW(GetHwnd(), m, L"Open session", MB_OK | MB_ICONERROR);
        return;
    }
    m_lastDupReport = j;
    std::vector<DupListRow> rows;
    if (j.contains("groups") && j["groups"].is_array()) {
        for (const auto& g : j["groups"]) {
            DupListRow row;
            row.method = utf8_to_wide(g.value("method", std::string{}));
            row.key    = utf8_to_wide(g.value("key", std::string{}));
            row.count  = g.value("count", 0);
            if (g.contains("paths") && g["paths"].is_array()) {
                for (const auto& p : g["paths"]) {
                    if (p.is_string())
                        row.paths.push_back(utf8_to_wide(p.get<std::string>()));
                }
            }
            if (!row.paths.empty()) row.samplePath = row.paths[0];
            rows.push_back(std::move(row));
        }
    }
    enrich_dup_rows_from_report(rows, j);
    EnsurePanelVisible(m_pDockDuplicateResults, DS_DOCKED_RIGHT, GetDockAncestor(), DpiScaleInt(360),
                       IDC_CMD_VIEW_DUPLICATERESULTS);
    if (m_pDockDuplicateResults) {
        m_pDockDuplicateResults->GetDuplicateResultsContainer().GetView().SetRows(rows);
    }
    LogMessage(L"Duplicates session opened from file.");
    CString s;
    s.Format(L"Duplicates: loaded %d group%s", (int)rows.size(), rows.size() == 1 ? L"" : L"s");
    SetStatusBarPartText(0, s);
}

// ── Batch queue control UWM handlers ─────────────────────────────────────────
//
// These are posted by worker threads (not from the UI thread) so that ribbon /
// own-strip batch buttons update on the UI thread (InvalidateBatchUi).

LRESULT CMainFrame::OnBatchPaused(WPARAM wparam, LPARAM)
{
    // wparam = new BatchState* with the partial state at the moment of pause.
    if (wparam) {
        auto* state = reinterpret_cast<media::BatchState*>(wparam);
        m_currentSession = std::move(*state);
        delete state;
    }
    SetStatusBarPartText(0, L"Paused \u23f8");
    LogMessage(L"[batch] paused — click Resume to continue, or Save Session to keep progress");
    InvalidateBatchUi();
    return 0;
}

LRESULT CMainFrame::OnBatchResumed(WPARAM, LPARAM)
{
    SetStatusBarPartText(0, L"Resumed\u2026");
    LogMessage(L"[batch] resumed");
    InvalidateBatchUi();
    return 0;
}

LRESULT CMainFrame::OnBatchCancelled(WPARAM wparam, LPARAM)
{
    // wparam = new BatchState* with partial state. Ownership transferred to us.
    if (wparam) {
        auto* state = reinterpret_cast<media::BatchState*>(wparam);
        m_currentSession = std::move(*state);
        delete state;
    }
    if (m_pDockQueue && !m_queueOpRowIndices.empty()) {
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        for (int idx : m_queueOpRowIndices) {
            if (idx >= 0 && idx < lv.QueueCount()) lv.SetItemStatus(idx, L"\u2717 Cancelled");
        }
        m_queueOpRowIndices.clear();
    }
    m_queueTotal = 0;
    m_queueDone  = 0;
    UpdateQueueStatusPart();
    m_processing = false;
    const int done = m_currentSession.count_done();
    const int err  = m_currentSession.count_error();
    const int pend = m_currentSession.count_pending();
    CString msg;
    msg.Format(L"[batch] cancelled — %d done, %d error(s), %d pending (Save Session to resume later)",
               done, err, pend);
    LogMessage(msg);
    SetStatusBarPartText(0, L"Cancelled");
    InvalidateBatchUi();
    return 0;
}

LRESULT CMainFrame::OnBatchStateUpdate(WPARAM wparam, LPARAM)
{
    // Posted by worker on clean completion. wparam = new BatchState*.
    if (wparam) {
        auto* state = reinterpret_cast<media::BatchState*>(wparam);
        m_currentSession = std::move(*state);
        delete state;
    }
    InvalidateBatchUi();
    return 0;
}

// ── Status bar stats helpers ──────────────────────────────────────────────────

void CMainFrame::SetStatusBarPartText(int part, const CString& text)
{
    if (part < 0 || part > 4) return;
    CString* sp = nullptr;
    switch (part) {
    case 0: sp = &m_sbar0; break;
    case 1: sp = &m_sbar1; break;
    case 2: sp = &m_sbar2; break;
    case 3: sp = &m_sbar3; break;
    case 4: sp = &m_sbar4; break;
    }
    *sp = text;
    if (!GetStatusBar().IsWindow()) return;
    if (part < GetStatusBar().GetParts())
        GetStatusBar().SetPartText(part, *sp, SBT_OWNERDRAW);
}

void CMainFrame::ReapplyCachedStatusBarTexts()
{
    if (!GetStatusBar().IsWindow()) return;
    const int n = GetStatusBar().GetParts();
    for (int p = 0; p < n && p < 5; ++p) {
        CString* sp = (p == 0 ? &m_sbar0
                       : p == 1   ? &m_sbar1
                       : p == 2   ? &m_sbar2
                       : p == 3   ? &m_sbar3
                                  : &m_sbar4);
        GetStatusBar().SetPartText(p, *sp, SBT_OWNERDRAW);
    }
}

void CMainFrame::ApplyExplorerFolderToStatusBar(const std::wstring& folder)
{
    m_statusExplorerFolder = folder;
    if (!GetStatusBar().IsWindow()) return;
    CString t;
    if (folder.empty())
        t = L"";
    else
        t.Format(L"\U0001F4C1 %s", folder.c_str());
    SetStatusBarPartText(1, t);
}

void CMainFrame::RebuildStatusBarParts()
{
    // §7c+§7d: delegate to workbench status-bar model.
    m_workbench->statusBarModel().RebuildStatusBarParts(*this);
}

void CMainFrame::UpdateQueueStatusPart()
{
    if (!GetStatusBar().IsWindow()) return;
    // §7c+§7d: queue part index from workbench status-bar model.
    const int kQueuePart = m_workbench->statusBarModel().GetStatusBarLayout().queue;
    if (kQueuePart < 0) return;

    if (m_queueTotal == 0) {
        SetStatusBarPartText(kQueuePart, L"");
        return;
    }

    const DWORD now     = static_cast<DWORD>(::GetTickCount64());
    const DWORD elapsed = now - m_batchStartTick;
    const DWORD elSec   = elapsed / 1000;

    wchar_t buf[128]{};
    if (m_queueDone == 0) {
        swprintf_s(buf, L"%d / %d files", 0, m_queueTotal);
    } else {
        const DWORD etaMs  = (DWORD)(static_cast<double>(elapsed)
                               * (m_queueTotal - m_queueDone) / m_queueDone);
        const DWORD etaSec = etaMs / 1000;
        swprintf_s(buf, L"%d/%d  \u23F1 %02d:%02d  ETA %02d:%02d",
                   m_queueDone, m_queueTotal,
                   elSec / 60, elSec % 60,
                   etaSec / 60, etaSec % 60);
    }
    SetStatusBarPartText(kQueuePart, buf);
}

void CMainFrame::UpdateSystemStatusPart()
{
    if (!GetStatusBar().IsWindow()) return;

    const ULONGLONG nowTick = ::GetTickCount64();

    // CPU % via GetSystemTimes delta.
    int cpuPct = 0;
    {
        FILETIME idleFT{}, kernelFT{}, userFT{};
        if (::GetSystemTimes(&idleFT, &kernelFT, &userFT)) {
            auto toULL = [](const FILETIME& ft) -> ULONGLONG {
                return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
            };
            const ULONGLONG idle   = toULL(idleFT);
            const ULONGLONG kernel = toULL(kernelFT);
            const ULONGLONG user   = toULL(userFT);
            const ULONGLONG dK = kernel - m_cpuPrevKernel;
            const ULONGLONG dU = user   - m_cpuPrevUser;
            const ULONGLONG dI = idle   - m_cpuPrevIdle;
            const ULONGLONG dTotal = dK + dU;
            if (dTotal > 0)
                cpuPct = static_cast<int>((dTotal - dI) * 100 / dTotal);
            m_cpuPrevKernel = kernel;
            m_cpuPrevUser   = user;
            m_cpuPrevIdle   = idle;
        }
    }

    // System physical RAM in use.
    double ramUsedGB = 0.0;
    {
        MEMORYSTATUSEX msx{};
        msx.dwLength = sizeof(msx);
        if (::GlobalMemoryStatusEx(&msx))
            ramUsedGB = (msx.ullTotalPhys - msx.ullAvailPhys) / (1024.0 * 1024 * 1024);
    }

    // This process (resident working set) — "memory usage" of the app.
    unsigned procMb = 0;
    {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        pmc.cb = sizeof(pmc);
        if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))
            && pmc.WorkingSetSize > 0)
            procMb = static_cast<unsigned>(pmc.WorkingSetSize / (1024U * 1024U));
    }

    const ULONGLONG barDt = (m_statusPrevBarTickMs > 0) ? (nowTick - m_statusPrevBarTickMs) : 0U;

    // DWM: derive on-screen “FPS” from per-HWND counters (fallback chain — GDI UIs may not move `cFramesComplete`).
    int fps = 0;
    {
        DWM_TIMING_INFO dti{};
        dti.cbSize = sizeof(dti);
        if (SUCCEEDED(::DwmGetCompositionTimingInfo(GetHwnd(), &dti))) {
            if (barDt > 0U && barDt < 300000U) { // ~5s poll, ignore suspend / long pauses
                const auto toFps = [barDt](ULONGLONG cur, ULONGLONG prev) -> int {
                    if (cur < prev) return 0; // DWM / driver reset
                    return static_cast<int>((cur - prev) * 1000ULL / barDt);
                };
                int a = 0, b = 0, c = 0;
                if (m_dwmPrevFrameComplete)
                    a = toFps(dti.cFrameComplete, m_dwmPrevFrameComplete);
                if (m_dwmPrevFramesDisplayed)
                    b = toFps(dti.cFramesDisplayed, m_dwmPrevFramesDisplayed);
                if (m_dwmPrevCFramesComplete)
                    c = toFps(dti.cFramesComplete, m_dwmPrevCFramesComplete);
                if (a > 0)
                    fps = a;
                else if (b > 0)
                    fps = b;
                else
                    fps = c;
            }
            m_dwmPrevFrameComplete   = dti.cFrameComplete;
            m_dwmPrevFramesDisplayed = dti.cFramesDisplayed;
            m_dwmPrevCFramesComplete = dti.cFramesComplete;
        }
    }
    if (barDt > 0U && barDt < 300000U) {
        if (fps == 0 && m_statusWmPaintTally > 0)
            fps = static_cast<int>(m_statusWmPaintTally * 1000ULL / barDt);
        m_statusWmPaintTally = 0; // new 5s window; count WM_PAINT until next sample
    }
    m_statusPrevBarTickMs = nowTick;
    if (fps > 999) fps = 999;

    wchar_t buf[128]{};
    swprintf_s(buf, L"CPU %d%%  Sys %.1f GB  %u MB  %d fps", cpuPct, ramUsedGB, procMb, fps);
    // §7c+§7d: system stats part from workbench status-bar model.
    SetStatusBarPartText(m_workbench->statusBarModel().GetStatusBarLayout().system, buf);
}

namespace {
std::wstring FormatExplorerSelBytes(ULONGLONG bytes)
{
    wchar_t buf[32]{};
    if (bytes < 1024ULL)
        swprintf_s(buf, L"%llu B", bytes);
    else if (bytes < 1024ULL * 1024)
        swprintf_s(buf, L"%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ULL * 1024 * 1024)
        swprintf_s(buf, L"%.1f MB", bytes / (1024.0 * 1024));
    else
        swprintf_s(buf, L"%.2f GB", bytes / (1024.0 * 1024 * 1024));
    return buf;
}
} // namespace

void CMainFrame::UpdateExplorerSelectionStatusPart()
{
    if (!GetStatusBar().IsWindow()) return;
    CString s;
    if (m_explorerStatusPaths.empty()) {
        s = L"";
    } else {
        std::error_code ec;
        ULONGLONG       total = 0;
        const int       n     = static_cast<int>(m_explorerStatusPaths.size());
        for (const auto& p : m_explorerStatusPaths) {
            if (fs::is_regular_file(fs::path(p), ec)) {
                ec.clear();
                const auto fsz = fs::file_size(p, ec);
                if (!ec) total += fsz;
            }
        }
        const std::wstring bytesW = FormatExplorerSelBytes(total);
        if (n == 1)
            s.Format(L"1 item  \u2022  %s", bytesW.c_str());
        else
            s.Format(L"%d items  \u2022  %s", n, bytesW.c_str());
    }
    SetStatusBarPartText(2, s);
}

namespace {
void append_semicolon_utf8_paths(const std::string& extra, std::vector<std::wstring>& out)
{
    if (extra.empty()) return;
    std::string seg;
    for (char c : extra) {
        if (c == ';') {
            if (!seg.empty()) {
                out.push_back(pmui::utf8_to_wide(seg));
                seg.clear();
            }
        } else
            seg.push_back(c);
    }
    if (!seg.empty()) out.push_back(pmui::utf8_to_wide(seg));
}
} // namespace

// ── App-command dispatch (ALT+P + `<app> app <verb>`) ────────────────────
//
// Two callers funnel into RunAppCommand():
//   1. WM_SYSKEYDOWN with wparam = 'P' (ALT+P) — direct in-app shortcut.
//   2. UWM_APP_COMMAND posted by the WM_COPYDATA bridge in
//      src/win/ui_singleton.cpp after `<app> app <verb>` forwards a
//      command name to the running primary instance.
//
// Both run on the UI thread of the main frame; per-verb work that touches
// libvips / IFileOperation / IShellItem stays on this thread (no extra
// CoInitialize, no thread shutdown). Long-running verbs should hand off to a
// worker — none currently need to.

LRESULT CMainFrame::OnAppCommand(WPARAM wparam)
{
    auto* name = reinterpret_cast<std::string*>(wparam);
    if (!name) return 0;
    RunAppCommand(*name);
    delete name;
    return 0;
}

void CMainFrame::RunAppCommand(const std::string& utf8_name)
{
    // Always log on entry so a "did the shortcut even reach me?" diagnostic
    // is one Log-panel scroll away. Cheap and costs nothing on the hot path.
    {
        CString msg(L"[app] running command: ");
        msg += CString(pmui::utf8_to_wide(utf8_name).c_str());
        LogMessage(msg);
    }

    std::string base = utf8_name;
    std::string extra;
    const size_t pipe = utf8_name.find('|');
    if (pipe != std::string::npos) {
        base = utf8_name.substr(0, pipe);
        extra = utf8_name.substr(pipe + 1);
    }

    using media::win::app_cmd::Command;
    const Command cmd = media::win::app_cmd::parse(base);

    if (cmd == Command::Unknown) {
        CString msg(L"[app] unknown command: ");
        msg += CString(pmui::utf8_to_wide(utf8_name).c_str());
        LogMessage(msg);
        return;
    }

    switch (cmd) {
        case Command::PauseBatch:
            OnPauseBatch();
            return;
        case Command::ResumeBatch:
            OnResumeBatch();
            return;
        case Command::CancelBatch:
            OnCancelBatch();
            return;
        case Command::TakeScreenshot: {
            const auto dir  = media::win::app_cmd::default_screenshots_dir();
            const auto fn   = media::win::app_cmd::make_screenshot_filename();
            const auto path = (dir / fn).wstring();

            std::string err;
            const bool ok = media::win::capture_window_to_png(GetHwnd(), path, err);

            if (ok) {
                CString msg(L"[app] screenshot saved: ");
                msg += CString(path.c_str());
                LogMessage(msg);
                CString status(L"Screenshot \u2192 ");
                status += CString(std::filesystem::path(path).filename().c_str());
                SetStatusBarPartText(0, status);
            } else {
                CString msg(L"[app] screenshot failed: ");
                msg += CString(err.c_str());
                LogMessage(msg);
            }
            return;
        }
        case Command::StartSessionRecord:
            StartSessionRecording();
            return;
        case Command::StopSessionRecord:
            StopSessionRecording();
            return;
        case Command::StartSessionVideoRecord:
            StartSessionVideoRecording();
            return;
        case Command::StopSessionVideoRecord:
            StopSessionVideoRecording();
            return;
        case Command::ToggleSessionVideoPause:
            ToggleSessionVideoPause();
            return;
        case Command::SessionReplay: {
            if (extra.empty()) {
                LogMessage(CStringW(
                    (std::wstring(L"[app] replay: no path; use e.g. `") + std::wstring(pm::brand::k_app_id_w)
                     + L" app replay --path=<session.json>`.")
                        .c_str()));
                return;
            }
            SetSessionReplayPath(pmui::utf8_to_wide(extra));
            ApplySessionReplayFromPath();
            return;
        }
        case Command::OpenChat: {
            std::vector<std::wstring> wpaths;
            append_semicolon_utf8_paths(extra, wpaths);
            OnAppOpenChat(wpaths);
            return;
        }
        case Command::BrowseToPaths: {
            std::vector<std::wstring> wpaths;
            append_semicolon_utf8_paths(extra, wpaths);
            if (wpaths.empty()) {
                LogMessage(
                    CStringW(
                        (std::wstring(L"[app] browse: no paths in payload (e.g. `") + std::wstring(pm::brand::k_app_id_w)
                         + L" app browse --paths \"C:\\a;C:\\b\"`).")
                            .c_str()));
                return;
            }
            OnAppBrowseToPaths(wpaths);
            return;
        }
        case Command::Unknown:
        default:
            return;
    }
}

LRESULT CMainFrame::OnReleasePreviewForPaths(WPARAM wparam, LPARAM lparam)
{
    auto* paths = reinterpret_cast<const std::vector<std::wstring>*>(wparam);
    if (!paths || paths->empty()) return 0;

    // Always release every preview mode (image / text / markdown). The user
    // is about to delete files; an over-eager clear just means the next
    // selection re-loads. The cost of NOT clearing is a sharing-violation
    // that silently aborts the Shell delete — far worse UX. The 250 ms
    // selection-poll in CExplorerBrowserView::CheckSelection re-fires once
    // the new selection settles, restoring the preview automatically.
    if (!IsChatWorkbench()) {
        m_viewerManager.ActiveView().ClearPicture();
        m_viewerManager.ActiveView().ClearText();
        m_viewerManager.ActiveView().ClearMarkdown();
#if defined(FEATURE_VIEWER_WEB)
        m_viewerManager.ActiveView().ReleaseViewerWebFolderMapping();
#endif
    }

    // lparam==0: in-place batch I/O (resize/compress) — only drop handles; do
    // NOT clear m_explorerSelectionPaths. Clearing it here made the Shell
    // re-poll look like a fresh selection and could reload the preview while
    // libvips was still writing, which races and can crash. Delete/recycle
    // uses lparam!=0 so we still drop vanished paths from the cache.
    if (lparam != 0) {
        auto matches = [&](const std::wstring& a) {
            for (const auto& b : *paths) {
                if (lstrcmpiW(a.c_str(), b.c_str()) == 0) return true;
            }
            return false;
        };
        auto& sel = m_explorerSelectionPaths;
        sel.erase(std::remove_if(sel.begin(), sel.end(), matches), sel.end());
        if (sel.empty())
            RefreshChatContext();
    }
    return 0;
}

LRESULT CMainFrame::OnGeneratedFile(WPARAM wparam)
{
    auto* pair = reinterpret_cast<std::pair<std::wstring, std::wstring>*>(wparam);
    if (pair) {
        if (!pair->second.empty())
            PushRecentFilesFromUserAdd({ pair->second });
        m_generatedMap[pair->first] = pair->second;
        if (m_pDockQueue) {
            auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
            int idx = lv.AddFile(CString(pair->second.c_str()));
            lv.SetItemStatus(idx, L"\u2728 Generated");
            LogMessage(CString(L"Generated: ") + pair->second.c_str());
        }
        delete pair;
    }
    return 0;
}

void CMainFrame::OnToolFileWritten(const std::wstring& writtenPath)
{
    if (writtenPath.empty())
        return;

    // Determine the path currently open in the centre viewer.
    // ActiveView().PreviewPathW() only tracks raster-image previews; for
    // text / markdown / editor views the path lives in the web panel's selection.
    std::wstring cur = m_viewerManager.ActiveView().PreviewPathW();
#if defined(FEATURE_VIEWER_WEB)
    if (cur.empty()) {
        if (auto* pv = m_viewerManager.ActiveView().ViewerWebPanel())
            cur = pv->SelectionPath();
    }
#endif

    if (!cur.empty() && ::_wcsicmp(cur.c_str(), writtenPath.c_str()) == 0) {
        LogMessage(CString(L"[chat] viewer-reload: reloading for '")
                   + writtenPath.c_str() + L"'");
        m_previewCoord.Request(pmui::PreviewSource::ChatGenerated, { writtenPath },
                               m_viewerManager.ActiveView(), *m_workbench, *this);

        // Post a side-channel notification so the React layer can log / show a
        // "reloaded by agent" banner without waiting for the setStatus content diff.
#if defined(FEATURE_VIEWER_WEB)
        if (auto* pv = m_viewerManager.ActiveView().ViewerWebPanel()) {
            nlohmann::json o;
            o["t"]    = "vw_tool_file_reloaded";
            o["path"] = pmui::wide_to_utf8(writtenPath);
            pv->PostRawJson(o.dump());
        }
#endif
    } else {
        LogMessage(CString(L"[chat] viewer-reload: no match (open='")
                   + cur.c_str() + L"' written='" + writtenPath.c_str() + L"')");
    }

    // Also refresh the dock viewer panel when it is visible and showing the same file.
    if (m_pDockViewerPanel && IsPanelVisible(m_pDockViewerPanel)) {
        const std::wstring& dv = m_pDockViewerPanel->GetFileViewer().PreviewPathW();
        if (!dv.empty() && ::_wcsicmp(dv.c_str(), writtenPath.c_str()) == 0) {
            LogMessage(CString(L"[chat] viewer-reload: reloading dock viewer for '")
                       + writtenPath.c_str() + L"'");
            m_pDockViewerPanel->GetFileViewer().OpenFile({ writtenPath },
                                                         pmui::PreviewSource::ChatGenerated);
        }
    }
}

LRESULT CMainFrame::OnQueueToolCall(WPARAM wparam)
{
    using pmui::QueueToolCallRowW;
    auto* p = reinterpret_cast<QueueToolCallRowW*>(wparam);
    if (p) {
        if (m_pDockQueue) {
            m_pDockQueue->GetQueueContainer()
                .GetListView()
                .AddToolCallRow(CString(p->col_name.c_str()), CString(p->col_operation.c_str()),
                                CString(p->col_status.c_str()), CString(p->col_path.c_str()));
        }
        delete p;
    }
    return 0;
}

void CMainFrame::ApplyCentralFileViewerPreviewFromPaths(const std::vector<std::wstring>& paths)
{
    if (IsChatWorkbench())
        return;
    // §8: Delegate to CFileViewer::OpenFile (unified dispatch).
    // The source tag is Explorer by default here — callers that know better
    // (queue-row click, recent-file, etc.) should migrate to
    // m_previewCoord.Request(source, paths, ...) directly.
    m_viewerManager.ActiveView().OpenFile(paths, pmui::PreviewSource::Explorer);
}

LRESULT CMainFrame::OnExplorerSelection(WPARAM wparam, LPARAM lparam)
{
    // §12: ExplorerSelectionMsg carries paths + navGeneration + ctrlDown.
    auto* msg = reinterpret_cast<ExplorerSelectionMsg*>(wparam);
    if (!msg) return 0;
    auto picked = std::move(msg->paths);
    const uint32_t selNavGen = msg->navGeneration;
    const bool explorer_ctrl_additive = msg->ctrlDown;
    delete msg;
    (void)lparam; // unused after §12 migration

    if (picked.empty()) {
        m_selRouter.ClearFromEmptyExplorerPick();
        RefreshChatContext(false);
        // §12: generation guard discards stale empty posts that were already
        // in the message queue before the timer was killed (§10).
        const bool stale_generation =
            (m_startupNavGeneration > 0 && selNavGen <= m_startupNavGeneration);
        const bool activeDockPinned =
            m_viewerManager.IsTabbed() && ActiveDockableViewerTab()
            && ActiveDockableViewerTab()->IsPinned();
        if (!stale_generation && !m_viewerManager.IsActivePinned() && !activeDockPinned)
            // §8: route through coordinator — policy gate + logging inside.
            m_previewCoord.Request(pmui::PreviewSource::Explorer, {},
                                   m_viewerManager.ActiveView(), *m_workbench, *this);
        UpdateExplorerSelectionStatusPart();
        return 0;
    }

    // §7: delegate path filtering to the active workbench policy.
    const auto filtered = m_workbench->previewPolicy().FilterExplorerSelection(picked);
    m_selRouter.SetFromExplorerPick(picked, filtered);

    // Push the new selection into the chat dock so the agent sees it on the
    // next Send (no need to click the Chat ribbon button again).
    RefreshChatContext(explorer_ctrl_additive);

    // §8: route through coordinator — policy gate + logging inside.
    // Viewer: startup latch + generation guard clear in `OnPreviewChanged` when
    // an Explorer-sourced preview actually loads (Ok/Failed), not on every
    // non-empty pick (folder restores would clear `--src` — selection.md).
    bool createdLiveTab = false;
    CFileViewer& explorerViewer = m_viewerManager.IsTabbed()
        ? LiveDockableViewerForExplorer(&createdLiveTab)
        : m_viewerManager.LiveViewForExplorer(&createdLiveTab);
#if defined(FEATURE_BROWSER) && (defined(FEATURE_VIEWER_WEB) || defined(FEATURE_HOME_PAGE))
    if (createdLiveTab)
        explorerViewer.SetBusManager(&m_webViews);
#else
    (void)createdLiveTab;
#endif
    if (m_viewerManager.IsTabbed()) {
        if (CDockViewerPanel* panel = ActiveDockableViewerTab()) {
            if (!picked.empty())
                panel->SetTitle(CString(fs::path(picked.front()).filename().wstring().c_str()));
        }
    }
    m_previewCoord.Request(pmui::PreviewSource::Explorer, picked,
                           explorerViewer, *m_workbench, *this);

    // §FileViewerPanel: chat workbench has no centre preview (coordinator gate
    // suppresses it above); route explorer selection directly to the dock panel
    // when it is visible so the user gets a live preview alongside the chat.
    if (!m_viewerManager.IsTabbed() && m_pDockViewerPanel && IsPanelVisible(m_pDockViewerPanel))
        m_pDockViewerPanel->GetFileViewer().OpenFile(picked, pmui::PreviewSource::Explorer);

    UpdateExplorerSelectionStatusPart();
    return 0;
}

void CMainFrame::UpdateFileInfoForSelection(int idx)
{
    if (!m_pDockQueue) return;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
    if (idx < 0 || idx >= lv.QueueCount()) return;
    CString path = lv.GetItemPath(idx);
    if (path.IsEmpty()) return;

    if (!IsChatWorkbench())
        m_viewerManager.ActiveView().SetFileInfoFromPath(path.c_str());

    // GenPreview removed. AI-Transform outputs land in the queue as new rows
    // and the central preview shows them when the user clicks the row; the
    // mapping in m_generatedMap stays available for future use.
}

void CMainFrame::UnregisterForegroundHotkeys()
{
    HWND const h = GetHwnd();
    if (!h) return;
    for (int id = kHkFirst; id <= kHkLast; ++id)
        (void)::UnregisterHotKey(h, id);
}

void CMainFrame::SelectPathInFileTree(const std::wstring& path)
{
    if (path.empty()) return;
    
    // Extract parent folder and filename
    std::filesystem::path fsPath(path);
    std::wstring parentDir = fsPath.parent_path().wstring();
    std::wstring fileName = fsPath.filename().wstring();
    
    if (parentDir.empty()) return;
    
    // Store the filename to select after navigation completes
    m_pendingFileToSelect = fileName;
    
    // Schedule navigation to parent folder (§11 pattern)
    ScheduleFileTreeBrowse({ parentDir });
}

void CMainFrame::SelectFileInExplorerView(const std::wstring& filename)
{
    if (filename.empty() || !m_pDockFileTree) return;
    CExplorerBrowserView& browserView = m_pDockFileTree->GetFileTreeContainer().GetBrowserView();
    browserView.SelectFile(filename);
}

void CMainFrame::RegisterForegroundHotkeys()
{
    HWND const h = GetHwnd();
    if (!h) return;
    UnregisterForegroundHotkeys();
    const UINT rpt = MOD_NOREPEAT;
    (void)::RegisterHotKey(h, kHkAltP_Screenshot, MOD_ALT | rpt, static_cast<UINT>('P'));
    (void)::RegisterHotKey(h, kHkCtrlR_SessionRec, MOD_CONTROL | rpt, static_cast<UINT>('R'));
    (void)::RegisterHotKey(h, kHkCtrlH_SessionStop, MOD_CONTROL | rpt, static_cast<UINT>('H'));
    (void)::RegisterHotKey(h, kHkCtrlAltR_Video, MOD_CONTROL | MOD_ALT | rpt, static_cast<UINT>('R'));
    (void)::RegisterHotKey(h, kHkCtrlAltH_VideoStop, MOD_CONTROL | MOD_ALT | rpt, static_cast<UINT>('H'));
    (void)::RegisterHotKey(h, kHkCtrlAltP_VideoPause, MOD_CONTROL | MOD_ALT | rpt, static_cast<UINT>('P'));
    (void)::RegisterHotKey(h, kHkF11_Fullscreen, rpt, VK_F11);
    (void)::RegisterHotKey(h, kHkAltF_Fullscreen, MOD_ALT | rpt, static_cast<UINT>('F'));
}

// ── Global hotkeys (see `TryProcessGlobalHotkeys` + `CPmImageApp::PreTranslateMessage`) ──
//
// The frame's WndProc only sees WM_SYSKEYDOWN when the *frame* has focus. The
// ribbon can also take ALT+letter for KeyTips. We must run before the default
// CMessagePump parent walk: some hosted controls (WebView2 / Chromium) use an
// HWND that is not a GetParent chain ancestor of the main frame, so the walk
// never reached CMainFrame::PreTranslateMessage and all app hotkeys were dead.
// `RegisterForegroundHotkeys` + WM_HOTKEY covers the case where WM_KEY* never
// reaches the thread queue at all (common with WebView2 focus).

BOOL CMainFrame::TryProcessGlobalHotkeys(MSG& msg)
{
    if (msg.message == WM_XBUTTONDOWN) {
        wchar_t clsXb[64]{};
        if (msg.hwnd && ::GetClassNameW(msg.hwnd, clsXb, 64) > 0) {
            if (std::wcscmp(clsXb, L"Edit") == 0 || std::wcsncmp(clsXb, L"RichEdit", 8) == 0 ||
                std::wcscmp(clsXb, L"RICHEDIT") == 0 || std::wcscmp(clsXb, L"RICHEDIT_CLASS") == 0) {
                return FALSE;
            }
        }
        if (m_viewerManager.ActiveView().TryConsumeImageNavXButtons(GET_XBUTTON_WPARAM(msg.wParam)))
            return TRUE;
        return FALSE;
    }

    if (msg.message != WM_SYSKEYDOWN && msg.message != WM_KEYDOWN)
        return FALSE;

    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    // For SYSKEYDOWN, ALT is implied. For KEYDOWN we test it explicitly.
    const bool alt  = (msg.message == WM_SYSKEYDOWN) || ((::GetKeyState(VK_MENU) & 0x8000) != 0);

    // Don't hijack typing inside an EDIT / RICHEDIT — those need the raw
    // keys for inline edit, search boxes, the chat input, etc.
    wchar_t cls[64]{};
    if (msg.hwnd && ::GetClassNameW(msg.hwnd, cls, 64) > 0) {
        if (std::wcscmp(cls, L"Edit") == 0 || std::wcsncmp(cls, L"RichEdit", 8) == 0 ||
            std::wcscmp(cls, L"RICHEDIT") == 0 || std::wcscmp(cls, L"RICHEDIT_CLASS") == 0) {
#ifdef FEATURE_USE_OWN_RIBBON
            return CDockFrame::PreTranslateMessage(msg);
#else
            return CRibbonDockFrame::PreTranslateMessage(msg);
#endif
        }
    }

    // Crop overlay: Enter applies the crop even when a sibling (e.g. Explorer SysListView32)
    // still has focus — otherwise VK_RETURN "opens" the shell selection instead of committing.
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
        if (m_viewerManager.ActiveView().HasLoadedImagePreview() && m_viewerManager.ActiveView().ImageToolConsumesEnterKey()) {
            m_viewerManager.ActiveView().OnImageToolFinishRequest();
            return TRUE;
        }
    }

    // Centre image prev/next: route here when focus is on ribbon / dock / Explorer, not the viewer.
    // Yield to the File Tree panel when it has focus — VK_LEFT / VK_RIGHT / VK_HOME / VK_END
    // all drive shell-list navigation (collapse/expand nodes, move selection, jump to ends) and
    // must not be consumed by the image navigator while the user is browsing the file tree.
    const bool focus_in_filetree = m_pDockFileTree
        && m_pDockFileTree->IsWindow()
        && IsPanelVisible(m_pDockFileTree)
        && msg.hwnd
        && ::IsChild(m_pDockFileTree->GetHwnd(), msg.hwnd);
    if (!focus_in_filetree && msg.message == WM_KEYDOWN
        && m_viewerManager.ActiveView().TryConsumeImageNavKeys(msg.wParam, ctrl, alt))
        return TRUE;

    // ── Ctrl+R / Ctrl+H — session JSON record (see `docs/session-replay.md`) ──
    if (ctrl && !alt && msg.message == WM_KEYDOWN) {
        if (msg.wParam == 'R') {
            StartSessionRecording();
            return TRUE;
        }
        if (msg.wParam == 'H') {
            StopSessionRecording();
            return TRUE;
        }
    }
    // ── Ctrl+Alt+R / Ctrl+Alt+H — main-window MP4 (WGC+MF when built) ──
    if (ctrl && alt && msg.message == WM_KEYDOWN) {
        if (msg.wParam == 'R') {
            StartSessionVideoRecording();
            return TRUE;
        }
        if (msg.wParam == 'H') {
            StopSessionVideoRecording();
            return TRUE;
        }
        if (msg.wParam == 'P') {
            ToggleSessionVideoPause();
            return TRUE;
        }
    }

    // ── ALT+P → take screenshot (also routed via WM_COPYDATA bridge) ──
    if (alt && !ctrl && msg.wParam == 'P') {
        LogMessage(L"[app] ALT+P intercepted in PreTranslateMessage");
        RunAppCommand("takescreenshot");
        return TRUE;
    }

    // ── Fullscreen: F11 / ALT+F / ESC (when fullscreen) ──
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_F11) {
        ToggleFullscreen();
        return TRUE;
    }
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && m_isFullscreen) {
        // Only toggle app fullscreen when ESC is destined for a window that
        // belongs to our main frame.  If a popup with its own top-level root
        // (e.g. CChatImageFullscreenHost) has focus, let the message through so
        // the popup can handle ESC itself.
        const HWND msgRoot = msg.hwnd ? ::GetAncestor(msg.hwnd, GA_ROOT) : nullptr;
        if (msgRoot == GetHwnd()) {
            ToggleFullscreen();
            return TRUE;
        }
    }
    if (alt && !ctrl && msg.wParam == 'F') {
        ToggleFullscreen();
        return TRUE;
    }
    return FALSE;
}

BOOL CMainFrame::PreTranslateMessage(MSG& msg)
{
#ifdef FEATURE_USE_OWN_RIBBON
    return CDockFrame::PreTranslateMessage(msg);
#else
    return CRibbonDockFrame::PreTranslateMessage(msg);
#endif
}

LRESULT CMainFrame::OnPmLoadDockContainers()
{
    if (!media::settings::defer_dock_container_load())
        return 0; // not deferred — either already applied in `LoadDockLayout` or disabled in settings
    pmui::ui_log_file_event("UWM_PM_LOAD_DOCK_CONTAINERS: LoadDockContainers (deferred tab order)");
    if (!LoadDockContainers())
        pmui::ui_log_file_event("UWM_PM_LOAD_DOCK_CONTAINERS: LoadDockContainers returned false");
    return 0;
}

// ── WndProc ───────────────────────────────────────────────────────────────────

LRESULT CMainFrame::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_PAINT) {
        LRESULT r = WndProcDefault(msg, wparam, lparam);
        ++m_statusWmPaintTally;
        return r;
    }
    try {
        if (msg == UWM_CONSOLE_OPEN_URL) {
#ifdef FEATURE_BROWSER
            std::unique_ptr<std::string> url(reinterpret_cast<std::string*>(lparam));
            if (url)
                ShowBrowserPopupUrl(*url);
#else
            delete reinterpret_cast<std::string*>(lparam);
#endif
            return 0;
        }
        switch (msg) {
        case WM_CLOSE:
            UnregisterForegroundHotkeys();
#ifdef FEATURE_BROWSER
            m_webViews.CloseAll();
#endif
#if defined(FEATURE_SESSION_VIDEO_RECORDER)
            if (media::win::recorder::is_recording()) media::win::recorder::stop();
#endif
            ::KillTimer(GetHwnd(), kStatsTimerId);
#if defined(_WIN32)
            ::KillTimer(GetHwnd(), kScreenshotProbeTimerId);
            ::KillTimer(GetHwnd(), kPostLayoutInitTimerId);
            ::KillTimer(GetHwnd(), kFileTreeNavTimerId);
#endif
            if (m_worker.joinable()) {
                if (m_batchCtrl) m_batchCtrl->request_cancel();
                m_worker.join();
            }
            if (m_isFullscreen) ToggleFullscreen(); // restore before saving layout
            SaveDockLayout();
            SaveLayout();
            media::win::clear_ui_command_target();
            return WndProcDefault(msg, wparam, lparam);

        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE)
                UnregisterForegroundHotkeys();
            else
                RegisterForegroundHotkeys();
            break;

        case WM_DPICHANGED: {
            LRESULT const lr = WndProcDefault(msg, wparam, lparam);
#ifdef FEATURE_USE_OWN_RIBBON
            if (m_ownRibbon.IsWindow() && m_workbenchChrome.show_ribbon_strip) {
                if (!m_ownRibbon.BuildRibbonLayout()) {
                    std::wstring line = L"[";
                    line += pm::brand::k_app_id_w;
                    line += L"] own ribbon: rebuild after DPI change failed: ";
                    const wchar_t* detail = own_ribbon::LastRibbonLayoutErrorW();
                    line += (detail && detail[0]) ? detail : L"(no detail)";
                    line += L"\n";
                    ::OutputDebugStringW(line.c_str());
                }
                const auto& pal = pmui::theme_palette();
                m_ownRibbon.ApplyChrome(pal.dark, pal.window_bg);
                SyncViewMenuChecks();
                m_ownRibbon.SyncBatchControls(*this);

                CRect crc    = GetClientRect();
                const int h  = m_ownRibbon.PreferredHeight();
                const int y0 = OwnRibbonClientTopY();
                m_ownRibbon.SetWindowPos(nullptr, 0, y0, crc.Width(), h, SWP_NOZORDER);
            }
#endif
            RebuildStatusBarParts();
            RecalcLayout();
            RedrawWindow();
            return lr;
        }

        case WM_HOTKEY: {
            const int hk = static_cast<int>(wparam);
            if (hk == kHkAltP_Screenshot) {
                RunAppCommand("takescreenshot");
                return 0;
            }
            if (hk == kHkCtrlR_SessionRec) {
                StartSessionRecording();
                return 0;
            }
            if (hk == kHkCtrlH_SessionStop) {
                StopSessionRecording();
                return 0;
            }
            if (hk == kHkCtrlAltR_Video) {
                StartSessionVideoRecording();
                return 0;
            }
            if (hk == kHkCtrlAltH_VideoStop) {
                StopSessionVideoRecording();
                return 0;
            }
            if (hk == kHkCtrlAltP_VideoPause) {
                ToggleSessionVideoPause();
                return 0;
            }
            if (hk == kHkF11_Fullscreen) {
                ToggleFullscreen();
                return 0;
            }
            if (hk == kHkAltF_Fullscreen) {
                ToggleFullscreen();
                return 0;
            }
            return 0;
        }

        case WM_SIZE: {
#ifdef FEATURE_USE_OWN_RIBBON
            if (m_ownRibbon.IsWindow() && m_workbenchChrome.show_ribbon_strip && m_ownRibbon.IsWindowVisible()) {
                CRect crc     = GetClientRect();
                const int h   = m_ownRibbon.PreferredHeight();
                const int y0  = OwnRibbonClientTopY();
                m_ownRibbon.SetWindowPos(nullptr, 0, y0, crc.Width(), h, SWP_NOZORDER);
            }
#endif
            // Base frame layout resizes the status bar first; part widths must use that rect.
            LRESULT const lr = WndProcDefault(msg, wparam, lparam);
            RebuildStatusBarParts();
            if (wparam != SIZE_MINIMIZED && m_workbench && !m_inResponsiveFrameSize) {
                CRect rc = GetClientRect();
                SIZE current{rc.Width(), rc.Height()};
                const bool havePrevious = m_lastResponsiveClientSize.cx > 0 && m_lastResponsiveClientSize.cy > 0;
                const bool isMaximized = (wparam == SIZE_MAXIMIZED);
                const bool maximizeTransition = havePrevious && (isMaximized != m_lastResponsiveWasMaximized);
                if (maximizeTransition && current.cx > 0 && current.cy > 0) {
                    m_inResponsiveFrameSize = true;
                    m_workbench->OnFrameMaximizeTransition(*this, m_lastResponsiveClientSize, current);
                    m_inResponsiveFrameSize = false;
                }
                m_lastResponsiveClientSize = current;
                m_lastResponsiveWasMaximized = isMaximized;
            }
            return lr;
        }

        case WM_TIMER:
            if (wparam == kStatsTimerId) {
                UpdateSystemStatusPart();
                if (m_processing) UpdateQueueStatusPart();
                return 0;
            }
#if defined(_WIN32)
            if (wparam == pmui::kTimerChatWebComposerLoadTimeout) {
                pmui::splash_on_chat_workbench_composer_load_timeout();
                return 0;
            }
            if (wparam == kScreenshotProbeTimerId) {
                ::KillTimer(GetHwnd(), kScreenshotProbeTimerId);
                std::string err;
                if (!m_screenshotProbeOut.empty() &&
                    media::win::capture_window_to_png(GetHwnd(), m_screenshotProbeOut, err)) {
                    ::OutputDebugStringA((std::string(pm::brand::k_app_id_u8) + ": test screenshot probe wrote PNG\n").c_str());
                } else if (!err.empty()) {
                    const std::string line = std::string(pm::brand::k_app_id_u8) + ": test screenshot probe failed: " + err + "\n";
                    ::OutputDebugStringA(line.c_str());
                }
                m_screenshotProbeOut.clear();
                ::PostQuitMessage(0);
                return 0;
            }
            if (wparam == kPostLayoutInitTimerId) {
                OnDeferredPostLayoutInit();
                return 0;
            }
            if (wparam == kFileTreeNavTimerId) {
                OnDeferredFileTreeNavTimer();
                return 0;
            }
#endif
            break;

        // ── Fullscreen via keyboard (F11 / ALT+F / ESC) ───────────────────────
        case WM_KEYDOWN:
            if (wparam == VK_F11) { ToggleFullscreen(); return 0; }
            if (wparam == VK_ESCAPE && m_isFullscreen) { ToggleFullscreen(); return 0; }
            break;
        case WM_SYSKEYDOWN:
            if (wparam == 'F') { ToggleFullscreen(); return 0; }   // ALT+F
            if (wparam == 'P') { RunAppCommand("takescreenshot"); return 0; }  // ALT+P
            break;

        // ── Fullscreen via preview panel "Full" button ────────────────────────
        case UWM_TOGGLE_FULLSCREEN:
            ToggleFullscreen();
            return 0;
        case UWM_PM_LOAD_DOCK_CONTAINERS:
            return OnPmLoadDockContainers();
        case UWM_PM_SAVE_WORKBENCH_LAYOUT:
            // Same persistence as WM_CLOSE (dock topology + `workbench.<active>.window`); deferred off
            // panel `OnClose` / `TogglePanelView` so we are not inside Win32++ dock recursion.
            SaveDockLayout();
            SaveLayout();
            return 0;

        case WM_GETMINMAXINFO:       return OnGetMinMaxInfo(msg, wparam, lparam);

        // System theme switched (Settings → Personalization → dark mode).
        // Only re-evaluate when the user is on the System theme — Light/Dark
        // are explicit overrides that should NOT change.
        case WM_SETTINGCHANGE: {
            if (lparam) {
                LPCWSTR area = reinterpret_cast<LPCWSTR>(lparam);
                if (area && lstrcmpiW(area, L"ImmersiveColorSet") == 0) {
                    media::settings::AppearanceSettings a;
                    std::string err;
                    media::settings::load_appearance(a, err);
                    if (a.theme == media::settings::Theme::System)
                        ApplyAppearance();
                }
            }
            break;
        }
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED: {
            media::settings::AppearanceSettings a;
            std::string err;
            media::settings::load_appearance(a, err);
            if (a.theme == media::settings::Theme::System)
                ApplyAppearance();
            break;
        }
        case UWM_BATCH_PAUSED:        return OnBatchPaused(wparam, lparam);
        case UWM_BATCH_RESUMED:       return OnBatchResumed(wparam, lparam);
        case UWM_BATCH_CANCELLED:     return OnBatchCancelled(wparam, lparam);
        case UWM_BATCH_STATE_UPDATE:  return OnBatchStateUpdate(wparam, lparam);
        case UWM_QUEUE_PROGRESS:     return OnQueueProgress(wparam, lparam);
        case UWM_QUEUE_OP_STATUS:    return OnQueueOpStatus(wparam, lparam);
        case UWM_QUEUE_DONE:         return OnQueueDone(wparam, lparam);
        case UWM_TRANSFORM_PROGRESS: return OnTransformProgress(wparam, lparam);
        case UWM_TRANSFORM_DONE:     return OnTransformDone(wparam, lparam);
        case UWM_LOG_MESSAGE:        return OnLogMessage(wparam);
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
        case UWM_PIXLWIZ_LOGIN_DONE: return OnPixlwizLoginDone(wparam, lparam);
#endif
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
        case UWM_PIXLWIZ_SHARE_PROGRESS: return OnPixlwizShareProgress(wparam, lparam);
        case UWM_PIXLWIZ_SHARE_DONE:     return OnPixlwizShareDone(wparam, lparam);
#endif
        case UWM_APP_COMMAND:        return OnAppCommand(wparam);
        case UWM_RELEASE_PREVIEW_FOR_PATHS: return OnReleasePreviewForPaths(wparam, lparam);
        case UWM_CHAT_REQUEST_CONTEXT: RefreshChatContext(); return 0;
        case UWM_CHAT_GENERATED: {
            // image_transform finished — load the first generated file into
            // the central preview (no separate GenPreview dock anymore) and
            // adopt the full list as the chat panel's implicit selection so a
            // follow-up prompt ("now make it warmer", "add a tree") iterates
            // on the result.
            auto* paths = reinterpret_cast<std::vector<std::wstring>*>(wparam);
            if (!paths || paths->empty()) { delete paths; return 0; }

            if (!IsChatWorkbench())
                m_viewerManager.ActiveView().LoadPicture((*paths)[0].c_str());

            // Chat workbench: append generated outputs to the context strip
            // (film strip) while keeping prior references, so one session can
            // stack sources + results across turns. Non-workbench: replace
            // selection with outputs (iteration / preview).
            if (IsChatWorkbench()) {
                for (const auto& w : *paths) {
                    if (!pmui::chat_context_path_allowed(w)) continue;
                    bool dup = false;
                    for (const auto& x : m_explorerSelectionPaths) {
                        if (::_wcsicmp(x.c_str(), w.c_str()) == 0) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) m_explorerSelectionPaths.push_back(w);
                }
            } else {
                m_explorerSelectionPaths = *paths;
            }
            RefreshChatContext();
            delete paths;
            return 0;
        }
        case UWM_GENERATED_FILE:     return OnGeneratedFile(wparam);
        case UWM_QUEUE_TOOL_CALL:    return OnQueueToolCall(wparam);
        case UWM_COMPRESS_DONE: {
            m_processing = false;
            m_queueTotal = 0; m_queueDone = 0; UpdateQueueStatusPart();
            InvalidateBatchUi();
            CString s;
            s.Format(L"Compress done: %d succeeded, %d failed", (int)wparam, (int)lparam);
            SetStatusBarPartText(0, s);
            LogMessage(s);
            if (lparam > 0)
                ::MessageBox(GetHwnd(), s, pm::brand::k_app_id_w, MB_ICONWARNING);
            return 0;
        }
        case UWM_FIND_PROGRESS:      return OnFindProgress(wparam, lparam);
        case UWM_FIND_DONE:          return OnFindDone(wparam, lparam);
        case UWM_FIND_ITEM_CLICKED:  OnFindItemClicked((int)wparam); return 0;
        case UWM_FIND_DELETE_SELECTION:
            if (m_pDockFindResults)
                m_pDockFindResults->GetFindResultsContainer().GetView().RemoveSelectedItems();
            return 0;
        case UWM_FIND_REVEAL_IN_EXPLORER: {
            auto* path = reinterpret_cast<std::wstring*>(wparam);
            if (path) { OnFindRevealInExplorer(*path); delete path; }
            return 0;
        }
        case UWM_DUPLICATES_DONE: return OnDuplicatesDone(wparam, lparam);
        case UWM_DUPLICATES_ITEM_CLICKED: OnDuplicateItemClicked((UINT_PTR)wparam); return 0;
        case UWM_DUPLICATES_DELETE_SELECTION:
            if (m_pDockDuplicateResults)
                m_pDockDuplicateResults->GetDuplicateResultsContainer().GetView().RemoveSelectedItems();
            return 0;
        case UWM_DUPLICATES_SAVE_SESSION:  OnDuplicatesSaveSession();  return 0;
        case UWM_DUPLICATES_OPEN_SESSION:  OnDuplicatesOpenSession();  return 0;

        case UWM_META_DONE: {
            m_processing = false;
            m_queueTotal = 0; m_queueDone = 0; UpdateQueueStatusPart();
            InvalidateBatchUi();
            CString s;
            s.Format(L"Meta done: %d succeeded, %d failed", (int)wparam, (int)lparam);
            SetStatusBarPartText(0, s);
            LogMessage(s);
            if (lparam > 0)
                ::MessageBox(GetHwnd(), s, pm::brand::k_app_id_w, MB_ICONWARNING);
            return 0;
        }
        case UWM_EXPLORER_FOLDER_PATH: {
            auto* p = reinterpret_cast<std::wstring*>(wparam);
            if (p) {
                ApplyExplorerFolderToStatusBar(*p);
                PushRecentFolderFromShellPath(*p);
                // §7: Let the active workbench react to the folder navigation.
                // Viewer workbench auto-previews the first previewable file in the
                // new folder so the centre view tracks FileTreePanel changes.
                m_workbench->previewPolicy().OnExplorerFolderPath(*this, *p);
                delete p;
            }
            return 0;
        }
        case UWM_EXPLORER_SELECTION: return OnExplorerSelection(wparam, lparam);
        case UWM_SELECT_PATH_IN_EXPLORER: {
            auto* pPath = reinterpret_cast<std::wstring*>(wparam);
            if (pPath) {
                SelectPathInFileTree(*pPath);
                delete pPath;
            }
            return 0;
        }
        case UWM_TOOL_FILE_WRITTEN: {
            auto* pw = reinterpret_cast<std::wstring*>(wparam);
            if (pw) { OnToolFileWritten(*pw); delete pw; }
            return 0;
        }
#if defined(FEATURE_BROWSER) && defined(FEATURE_CONSOLE)
        case UWM_XBLOX_RUN_CONSOLE: {
            std::unique_ptr<std::string> raw(reinterpret_cast<std::string*>(wparam));
            if (!raw)
                return 0;
            try {
                const auto payload = nlohmann::json::parse(*raw);
                const std::string line = payload.value("line", std::string{});
                const bool new_shell = payload.value("newShell", true);
                const bool close_on_exit = payload.value("closeOnExit", false);
                if (!line.empty())
                    RunInWebConsole(line, new_shell, close_on_exit);
            } catch (const std::exception& e) {
                logger::warn(std::string("[xblox] console request failed: ") + e.what());
            } catch (...) {
                logger::warn("[xblox] console request failed");
            }
            return 0;
        }
#endif
        case UWM_OPEN_PATH_INTERNAL: {
            auto* pPath = reinterpret_cast<std::wstring*>(wparam);
            if (pPath) {
                // (no drive letter / backslash) means the folderHint was missing when
                // the linkifier ran — fall back to ShellExecute so something useful
                // happens rather than a silent preview failure.
                const bool isAbsolute = pPath->size() >= 3
                    && ((*pPath)[1] == L':' || (*pPath)[0] == L'\\');
                if (isAbsolute) {
                    m_previewCoord.Request(pmui::PreviewSource::ChatGenerated, { *pPath },
                                           m_viewerManager.ActiveView(), *m_workbench, *this);
                    // §FileViewerPanel: when the coordinator is suppressed (chat workbench),
                    // push the path to the dock viewer panel instead so chat-clicked links
                    // still land in a visible preview.
                    if (m_pDockViewerPanel && IsPanelVisible(m_pDockViewerPanel))
                        m_pDockViewerPanel->GetFileViewer().OpenFile({ *pPath }, pmui::PreviewSource::ChatGenerated);
                    // Also reveal the item in the FileTreePanel so the tree
                    // navigates to the clicked file/folder's location.
                    SelectPathInFileTree(*pPath);
                    logger::debug(std::string("[chat] openPathInternal: absolute path '")
                        + pmui::wide_to_utf8(*pPath) + "'");
                } else {
                    // Bare filename — folderHint was missing at linkification time.
                    // shell::open_path can't resolve it without a directory and the
                    // absolute-path call above already handled the real file.  Drop silently.
                    logger::debug(std::string("[chat] openPathInternal: bare filename '")
                        + pmui::wide_to_utf8(*pPath) + "' ignored (no absolute path)");
                }
                delete pPath;
            }else{
                logger::debug(std::string("[chat] openPathInternal: no path"));
            }
            return 0;
        }

        case UWM_QUEUE_DELETE_SELECTION:
            if (m_processing)
                return 0;
            if (m_pDockQueue) {
                auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
                lv.RemoveSelectedItems();
                CString status;
                status.Format(L"%d file(s) in queue", lv.QueueCount());
                SetStatusBarPartText(0, status);
            }
            return 0;

        case UWM_QUEUE_ITEM_CLICKED: {
            int idx = static_cast<int>(wparam);
            try {
                if (m_pDockQueue) {
                    CString path = m_pDockQueue->GetQueueContainer()
                                               .GetListView().GetItemPath(idx);
                    if (!path.IsEmpty() && !IsChatWorkbench())
                        m_viewerManager.ActiveView().LoadPicture(path.c_str());
                    if (!IsChatWorkbench())
                        UpdateFileInfoForSelection(idx);
                    // Queue click takes focus back from the Explorer selection.
                    m_explorerSelectionPaths.clear();
                    m_explorerStatusPaths.clear();
                    UpdateExplorerSelectionStatusPart();
                }
            }
            catch (const std::exception& ex) {
                LogMessage(CString(L"[Queue click] ") + ex.what());
            }
            catch (...) {
                LogMessage(L"[Queue click] Unknown exception — panel state may be invalid.");
            }
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wparam);
            UINT count  = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> paths;
            paths.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
                if (!len) continue;
                std::wstring w(len + 1, L'\0');
                DragQueryFileW(hDrop, i, w.data(), len + 1);
                w.resize(len);
                paths.push_back(std::move(w));
            }
            DragFinish(hDrop);
            AddFilesToQueue(paths);
            return 0;
        }

        case WM_INITMENUPOPUP: {
            const HMENU hMenuPopup = reinterpret_cast<HMENU>(wparam);
            if (!HIWORD(lparam)) {
                HMENU hBar = AppMenuHandle();
                if (hBar) {
                    HMENU hFile = ::GetSubMenu(hBar, 0);
                    if (hFile) {
                        HMENU hRf = ::GetSubMenu(hFile, kFileMenuRecentFilesSubmenuPos);
                        HMENU hRd = ::GetSubMenu(hFile, kFileMenuRecentFoldersSubmenuPos);
                        // ReBar menu: INIT is delivered to the frame; refresh nested MRU when File opens too.
                        if (hMenuPopup == hFile) {
                            if (hRf)
                                SyncRecentFilesMenuPopup(hRf);
                            if (hRd)
                                SyncRecentFoldersMenuPopup(hRd);
                            const UINT imgState =
                                m_viewerManager.ActiveView().HasLoadedImagePreview() ? MF_ENABLED : MF_GRAYED;
                            const UINT saveState =
                                m_viewerManager.ActiveView().CanSaveImagePreviewOverwrite() ? MF_ENABLED : MF_GRAYED;
                            ::EnableMenuItem(hFile, IDM_FILE_SAVE, MF_BYCOMMAND | saveState);
                            ::EnableMenuItem(hFile, IDM_FILE_SAVE_AS, MF_BYCOMMAND | imgState);
                        } else if (hMenuPopup == hRf)
                            SyncRecentFilesMenuPopup(hMenuPopup);
                        else if (hMenuPopup == hRd)
                            SyncRecentFoldersMenuPopup(hMenuPopup);
                    }
                }
            }
            SyncViewMenuChecks();
            break;
        }
        }
        return WndProcDefault(msg, wparam, lparam);
    }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    catch (const std::exception& e) {
        ::MessageBoxA(nullptr, e.what(), "Error", MB_ICONERROR);
    }
    return 0;
}
