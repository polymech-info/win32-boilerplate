#include "stdafx.h"
#include "constants.hpp"
#include "launch_ui_next.h"
#include "App.h"
#include "helpers/ui_font.hpp"
#include "helpers/theme.hpp"
#include "helpers/ui_language.hpp"
#include "helpers/text_conv.hpp"
#include "log_sink.h"
#include "helpers/splash_window.hpp"
#include "ui_log_file.hpp"
#include "win/ui_singleton.hpp"
#include <objbase.h>
#include <functional>
#include <string>

// vips_init / vips_shutdown live in the vips DLL, already on the include path
// via stdafx.h → (Vips::vips via stdafx.h's transitive includes).
// Include explicitly in case the transitive pull doesn't expose the C API here.
#include <vips/vips.h>

namespace media::win {

int run_pm_image_ui(const std::function<void(CMainFrame&)>& configure)
{
    pmui::ui_log_file_event("run_pm_image_ui: start (next: CoInitEx, vips, then frame Create)");

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    pmui::ui_log_file_event("after CoInitializeEx");

    // Display language for Win32 resources (ribbon / menus): must run on the UI thread
    // before any HWND is created so LoadMenu / ribbon pick the right LANGUAGE blocks.
    pmui::apply_display_language_from_settings();
    pmui::ui_log_file_event("after apply_display_language_from_settings");

    // ── Initialize libvips on the MAIN thread ────────────────────────────────
    // This MUST happen before any background thread calls into vips.
    //
    // Without it the first vips call (ARW preview thread) triggers GLib's lazy
    // type-system initialisation from a worker thread.  GLib's GObject then
    // tries to register its types while the main-thread type lock hasn't been
    // taken yet, producing:
    //   GLib-GObject-CRITICAL: cannot retrieve class for invalid (unclassed)
    //                          type '<invalid>'
    //
    // vips_init() is idempotent — safe to call even if pm-media already called
    // it via VIPS_INIT() for a resize operation.
    if (vips_init("") < 0) {
        // Non-fatal: vips will try to lazy-init later; log but continue.
        (void)::OutputDebugStringA(
            (std::string(pm::brand::k_app_id_u8) + ": vips_init() failed at startup\n").c_str());
    }
    pmui::ui_log_file_event("after vips_init");

    // Load appearance preferences (theme + font extra-pt) BEFORE any window is
    // created so every panel picks up the right font on first paint.
    pmui::ui_font_init_from_settings();
    pmui::ui_log_file_event("after ui_font_init_from_settings (loads appearance)");
    pmui::theme_init_from_settings();
    pmui::ui_log_file_event("after theme_init_from_settings (loads appearance again)");

    // Process-level dark-mode opt-in: must come BEFORE the first window is
    // created so the standard controls render with their dark theme parts on
    // first paint. Cheap call (no-op on Windows < 10 1809).
    pmui::enable_app_dark_mode(true);
    pmui::ui_log_file_event("after enable_app_dark_mode");

    // Mirror every spdlog message into the in-app Log panel via UWM_LOG_MESSAGE
    // (the sink no-ops until the frame registers itself as the target). This
    // surfaces kbot HTTP info / liboai errors / etc. that would otherwise be
    // lost on stderr in this Windows /SUBSYSTEM:WINDOWS app.
    pmui::install_ui_log_sink();
    // Splash centers on the monitor that contains the saved main-frame rect (same workbench slot as
    // `ui.workbench` → `workbench.<slot>.window.normal_rect`); see splash_window.cpp.
    pmui::splash_show();

    pmui::ui_log_file_event("before CPmImageApp (frame Create + OnInitialUpdate)");

    // ── App-command bridge (dwData == 2 in src/win/ui_singleton.cpp) ─────
    // Try to claim the singleton mutex and stand up the message-only bridge
    // window. If we get the mutex we are the primary instance — external
    // `<app> app <verb>` calls will SendMessage(WM_COPYDATA) us and hit
    // CMainFrame::OnAppCommand. If the mutex is already taken, we still
    // launch a UI but skip the bridge so we don't shadow the existing
    // primary's HWND. Cleanup is RAII-tied to this scope so we always
    // release on normal exit AND on the exception paths below.
    struct UiSingletonOwner {
        bool mutex  = false;
        bool bridge = false;
        ~UiSingletonOwner() {
            if (bridge) media::win::destroy_ui_singleton_bridge();
            if (mutex)  media::win::release_ui_singleton_mutex();
        }
    } ui_singleton;

    if (media::win::try_acquire_ui_singleton_mutex()) {
        ui_singleton.mutex = true;
        if (media::win::create_ui_singleton_bridge())
            ui_singleton.bridge = true;
        else
            (void)::OutputDebugStringA(
                (std::string(pm::brand::k_app_id_u8) + ": ui_singleton bridge creation failed; external app commands disabled\n")
                    .c_str());
    } else {
        (void)::OutputDebugStringA(
            (std::string(pm::brand::k_app_id_u8) + ": another " + std::string(pm::brand::k_app_id_u8)
             + " UI is primary; external app commands will route to it\n")
                .c_str());
    }

    try {
        // Same CWinApp / message pump for workbench launches, default seed UI, probes, replay.
        // Global hotkeys: `CPmImageApp::PreTranslateMessage` → `CMainFrame::TryProcessGlobalHotkeys`.
        //
        // `app` is scoped so its destructor (→ CMainFrame → CWebView / WebView2 COM teardown)
        // runs BEFORE CoUninitialize(). Calling COM methods (controller->Close() etc.) after
        // CoUninitialize() is undefined behaviour and causes the process to die before the
        // WebView2 browser processes receive their shutdown signal.
        int rc = -1;
        {
            CPmImageApp app;
            configure(app.GetMainFrame());
            rc = app.Run();
            pmui::ui_log_file_event("after CWinApp::Run (message pump exit)");
        } // ← CMainFrame + all WebView2 panels destroyed here, COM still live
        CoUninitialize();
        return rc;
    }
    catch (const CException& e) {
        pmui::splash_hide();
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, pm::brand::k_w_startup_error_title_w, MB_ICONERROR);
    }
    catch (const std::exception& e) {
        pmui::splash_hide();
        (void)::MessageBoxA(
            nullptr, e.what(), (std::string(pm::brand::k_app_id_u8) + " startup error").c_str(), MB_ICONERROR);
    }

    CoUninitialize();
    
    return -1;
}

int launch_ui_next(
    const std::vector<std::string>& initial_files, bool open_chat, const char* startup_note,
    const std::wstring& layout_override_path)
{
    (void)pmui::apply_pm_image_log_directory_from_env();
    pmui::UiLogFileSession ui_log;
    if (startup_note && startup_note[0] != '\0')
        pmui::ui_log_file_event(startup_note);
    
    pmui::ui_log_file_event("launch: launch_ui_next (workbench preset, default seed, or caller)");

    // Capture cwd before entering the message loop (it could theoretically change).
    std::wstring cli_cwd;
    {
        wchar_t buf[32768]{};
        if (::GetCurrentDirectoryW(static_cast<DWORD>(std::size(buf)), buf) > 0)
            cli_cwd = buf;
    }
    pmui::ui_log_file_eventf(
        "launch_ui_next: captured cwd=\"%s\" initial_files=%zu open_chat=%d",
        pmui::wide_to_utf8(cli_cwd).c_str(), initial_files.size(), open_chat ? 1 : 0);
    return run_pm_image_ui([&](CMainFrame& f) {
        if (!cli_cwd.empty()) {
            f.SetCliCwdStartupFolder(cli_cwd);
            pmui::ui_log_file_eventf(
                "launch_ui_next: SetCliCwdStartupFolder(\"%s\")",
                pmui::wide_to_utf8(cli_cwd).c_str());
        } else
            pmui::ui_log_file_event("launch_ui_next: cli_cwd empty; skipping SetCliCwdStartupFolder");
        if (!layout_override_path.empty())
            f.SetLayoutOverridePath(layout_override_path);
        if (!initial_files.empty() || open_chat) {
            std::vector<std::wstring> wpaths;
            wpaths.reserve(initial_files.size());
            for (auto& path : initial_files)
                wpaths.push_back(pmui::utf8_to_wide(path));
            f.SetPendingStartupEnqueue(wpaths, open_chat);
            pmui::ui_log_file_eventf(
                "launch_ui_next: SetPendingStartupEnqueue paths=%zu open_chat=%d",
                wpaths.size(), open_chat ? 1 : 0);
        }
    });
}

int launch_ui_next_screenshot_probe(const std::wstring& out_png_path, int wait_ms, int window_width, int window_height)
{
    (void)pmui::apply_pm_image_log_directory_from_env();
    pmui::UiLogFileSession ui_log;
    pmui::ui_log_file_event("launch: test screenshot probe (PNG, then exit)");
    const int w = (wait_ms < 0) ? 0 : wait_ms;
    return run_pm_image_ui([out_png_path, w, window_width, window_height](CMainFrame& frame) {
        frame.SetScreenshotProbe(out_png_path, w, window_width, window_height);
    });
}

int launch_ui_next_session_replay(const std::wstring& session_json_path)
{
    (void)pmui::apply_pm_image_log_directory_from_env();
    pmui::UiLogFileSession ui_log;
    pmui::ui_log_file_event("launch: session replay (load snapshot from JSON)");
    return run_pm_image_ui(
        [session_json_path](CMainFrame& frame) { frame.SetSessionReplayPath(session_json_path); });
}

} // namespace media::win
