#pragma once

#include <functional>
#include <string>
#include <vector>

class CMainFrame;

namespace media::win {

/// Core UI run loop (CoInit, vips, `CPmImageApp` / `CWinApp::Run` message pump).
/// App-wide keyboard shortcuts (session record, video, screenshot, fullscreen) are
/// applied in `CPmImageApp::PreTranslateMessage` + `CMainFrame::TryProcessGlobalHotkeys`
/// so they work for `--ui-next` and every other caller. Callers often wrap with
/// `pmui::UiLogFileSession` and `ui_log_file_event` (see `pm_image_run`).
int run_pm_image_ui(const std::function<void(CMainFrame&)>& configure);

/// Launch the Win32++ "next" UI. Runs the message loop; returns when window is closed.
/// @param initial_files  Optional pre-seeded file paths to add to the queue on launch.
/// @param open_chat      If true, open/focus the Chat dock after startup (uses @p initial_files as context).
/// @param startup_note   Optional first `[startup]` line after session open (e.g. no-subcommand path).
/// @return 0 on normal exit.
int launch_ui_next(const std::vector<std::string>& initial_files = {}, bool open_chat = false,
                   const char* startup_note = nullptr, const std::wstring& layout_override_path = {});

/// Open the UI, wait @p wait_ms, save a PNG of the main window, then exit.
/// Optional @p window_width / @p window_height (both > 0): outer frame size after layout restore.
int launch_ui_next_screenshot_probe(const std::wstring& out_png_path, int wait_ms = 2000,
    int window_width = 0, int window_height = 0);

/// After startup, apply `snapshot.window_layout` from this session JSON (see `docs/session-replay.md`).
int launch_ui_next_session_replay(const std::wstring& session_json_path);

} // namespace media::win
