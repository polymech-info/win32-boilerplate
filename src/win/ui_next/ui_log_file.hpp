#ifndef PM_UI_LOG_FILE_H
#define PM_UI_LOG_FILE_H

#include <cstdint>
#include <filesystem>

namespace pmui {

/// Startup / layout / selection trace: logs to the **default** spdlog logger with channel
/// `[startup]` (`pm::log` + `logger::*`). Registers settings-load miss tracing for the session.
/// Nested `UiLogFileSession` increments a depth counter; only the outermost session registers
/// tracing and emits the closing line.
void ui_log_file_begin_session();
void ui_log_file_end_session();
void ui_log_file_event(const char* message);
void ui_log_file_eventf(const char* fmt, ...);

/// Sets the directory for **`pm-image.log`** only (same as `set_pm_image_log_file_directory`).
void ui_log_file_set_log_directory(const std::filesystem::path& directory = std::filesystem::path{});

#if defined(_WIN32)
/// Call once at `pm_image_run` entry for process-entry timing in the first session banner.
void ui_log_file_set_process_t0();
/// Call once right after `CLI11_PARSE` to split “CLI/dispatch work” vs “up to CoInit” in the log.
void ui_log_file_mark_after_cli_parse();
/// Milliseconds from `ui_log_file_set_process_t0()` to now (0 if process t0 was not marked).
std::uint64_t ui_log_file_ms_from_process_entry();
/// Like `ui_log_file_event` but includes “since process entry” ms (standalone `--ui-chat` E2E).
void ui_log_file_event_from_process_entry(const char* message);
/// Set while `run_pm_chat_ui` hosts the standalone chat window (not embedded in `CMainFrame`).
void set_ui_chat_standalone_session(bool standalone);
bool is_ui_chat_standalone_session();
#endif

struct UiLogFileSession {
    UiLogFileSession() { ui_log_file_begin_session(); }
    ~UiLogFileSession() { ui_log_file_end_session(); }
    UiLogFileSession(const UiLogFileSession&)            = delete;
    UiLogFileSession& operator=(const UiLogFileSession&) = delete;
};

} // namespace pmui

#endif
