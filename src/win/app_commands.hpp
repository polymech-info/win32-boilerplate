#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <string>

namespace media::win::app_cmd {

// ── App command surface ─────────────────────────────────────────────────────
// "App commands" are short, named verbs that callers — keyboard shortcuts in
// the running UI, the `pm-image app <verb>` CLI subcommand, or anything else
// that goes through the WM_COPYDATA bridge — can ask the running pm-image
// instance to run on its own UI thread.
//
// Extend via the `Command` enum, `parse()`, `name()`, and `CMainFrame::RunAppCommand`
// (Mainfrm.cpp). Session record / replay: `recordstart` / `recordstop` / `replay` (see `docs/session-replay.md`).
//
// For automated one-shot full-window captures (e.g. UI/theme checks), the CLI
// exposes `pm-image test screenshot --output <path> [--wait-ms n]` and
// `pm-image app takescreenshot` (fixed wait, default output under
// `default_screenshots_dir()`); those start a private UI instance, wait, write
// the PNG, and exit — they do not go through this WM_COPYDATA / Command enum.
// In-app shortcuts (e.g. ALT+P) and external sends to the primary still use
// `Command::TakeScreenshot` on the running instance.
//
/// Milliseconds to wait after UI show before capture for `pm-image app takescreenshot`.
inline constexpr int k_app_cli_take_screenshot_wait_ms = 5'000;
//
// All execution happens on the UI thread of the primary instance — these
// helpers do NOT spawn threads or talk to background workers.

enum class Command {
    Unknown = 0,
    TakeScreenshot,
    PauseBatch,
    ResumeBatch,
    CancelBatch,
    OpenChat,
    /// `browse|<paths>` — File tree panel goes to folder / sets selection (Explorer "Open in Workbench" when UI already running).
    BrowseToPaths,
    /// `pm-image app recordstart` — same as Ctrl+R: begin session JSON capture.
    StartSessionRecord,
    /// `pm-image app recordstop` — same as Ctrl+H: write session-*.json and stop.
    StopSessionRecord,
    /// `pm-image app videorecordstart` — start MP4 under Videos (Ctrl+Alt+R), or resume if already recording and paused (requires session video build).
    StartSessionVideoRecord,
    /// `pm-image app videorecordstop` — stop and finalize MP4 (Ctrl+Alt+H; requires session video build).
    StopSessionVideoRecord,
    /// `pm-image app videorecordpause` — toggle pause/resume while recording (Ctrl+Alt+P; requires session video build).
    ToggleSessionVideoPause,
    /// `pm-image app replay --path=…` — apply `snapshot.window_layout` (payload `replay|<utf8 path>`).
    SessionReplay,
};

/// Parse a human-typed command name (case-insensitive; underscores, dashes,
/// and spaces are ignored). Returns Command::Unknown for unrecognised names.
Command parse(const std::string& name);

/// Canonical lowercase name for @p cmd (the form the CLI accepts).
const char* name(Command cmd);

// ── takescreenshot helpers ──────────────────────────────────────────────────

/// Default destination folder for screenshots. v1 returns
/// `<cwd>/screenshots/` and ensures it exists (best-effort). The v2 home is
/// `%APPDATA%/Polymech/pm-image/screenshots` — left as a TODO so we don't
/// silently scatter PNGs into roaming profiles before the rest of the app
/// agrees on its AppData layout.
std::filesystem::path default_screenshots_dir();

/// Build a timestamped screenshot filename — `pm-image-YYYYMMDD-HHMMSS.png`
/// in local time. Two screenshots issued in the same second collide; that is
/// acceptable for a manual ALT+P workflow and is documented behaviour.
std::wstring make_screenshot_filename();

} // namespace media::win::app_cmd
