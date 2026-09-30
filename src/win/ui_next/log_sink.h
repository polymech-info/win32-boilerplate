#ifndef PM_UI_LOG_SINK_H
#define PM_UI_LOG_SINK_H

#include <Windows.h>
#include <filesystem>
#include <string>

namespace pmui {

/// Install a spdlog sink on the default logger that mirrors every message
/// (info / warn / error) into the in-app Log panel via UWM_LOG_MESSAGE.
///
/// Safe to call before the main frame exists: the sink no-ops until
/// `set_ui_log_target()` provides a target HWND. The original stderr sink
/// installed by `logger::init_stderr` stays in place.
void install_ui_log_sink();

/// Provide the HWND that should receive UWM_LOG_MESSAGE. Call from
/// CMainFrame::OnInitialUpdate as soon as the Log dock exists (m_pDockLog) so
/// early startup logs are not dropped; avoid deferring until after webview or
/// other panels finish initializing. Pass nullptr to detach (e.g. on shutdown).
void set_ui_log_target(HWND hwnd);

/// Thread-safe: append one UTF-8 line to the in-app Log panel (same as the spdlog UI
/// sink — UWM_LOG_MESSAGE, then `CLogView::AppendLine`, including the cwd log file
/// hook in `LogPanel.cpp`). Safe from any thread; no-op if `set_ui_log_target` was
/// not called or the window is gone.
void post_log_panel_line_utf8(const std::string& utf8_line);

/// Append one UTF-8 line to `pm-image.log` (see `k_cwd_log_basename_w`). Thread-safe.
/// Directory is `current_path()` unless @ref set_pm_image_log_file_directory was set
/// (e.g. standalone chat beside the first `--src`).
void append_pm_image_log_file_line_wide(const std::wstring& line);
void set_pm_image_log_file_directory(const std::filesystem::path& directory = std::filesystem::path{});

/// Returns the effective pm-image.log directory (override or current_path()). Thread-safe.
/// Use to place sibling output files (e.g. agent.json) next to the log.
std::filesystem::path get_pm_image_log_file_directory();

/// If `PM_IMAGE_LOG_DIR` is set to a non-empty path, applies it to `pm-image.log` and returns @c true.
bool apply_pm_image_log_directory_from_env();

} // namespace pmui

#endif // PM_UI_LOG_SINK_H
