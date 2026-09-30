#pragma once

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace media::win {

/** Create named mutex; returns true if this process is the primary UI instance. */
bool try_acquire_ui_singleton_mutex();

/** Release mutex from try_acquire_ui_singleton_mutex (primary only). */
void release_ui_singleton_mutex();

/** Message-only window that receives WM_COPYDATA from secondary instances. */
bool create_ui_singleton_bridge();
void destroy_ui_singleton_bridge();

/** Target edit control for merging forwarded paths (resize dialog input row). */
void set_ui_merge_input_edit(HWND h);
void clear_ui_merge_input_edit();

/**
 * Find the bridge window and send UTF-8 paths (semicolon-separated) to the primary instance.
 * Retries briefly to avoid races right after mutex creation.
 */
bool forward_resize_ui_paths_to_primary(const std::string &utf8_paths);

// ── App-command channel (used by `pm-image app <verb>`) ───────────────────
// The bridge supports a second WM_COPYDATA channel: dwData = 2 carries a
// UTF-8 command name (e.g. "takescreenshot"). When a command target is
// registered, the bridge wraps the payload in a heap-allocated std::string*
// and PostMessages it to the target HWND with the supplied message id.
// The handler on the target side OWNS the heap pointer and must `delete`
// it.

/// Register the HWND that should receive forwarded app commands and the
/// message id to use. Typically called from CMainFrame::OnInitialUpdate
/// once the frame's HWND is known. Pass a `WM_USER + N` value for
/// @p message_id (e.g. UWM_APP_COMMAND from Resource.h).
void set_ui_command_target(HWND target, UINT message_id);

/// Drop the registered target (call from WM_DESTROY on the frame).
void clear_ui_command_target();

/// Find the bridge in the primary instance and send a UTF-8 command name.
/// Returns false if no bridge is reachable within ~2 seconds (e.g. when no
/// pm-image UI is running).
bool forward_app_command_to_primary(const std::string &utf8_command);

} // namespace media::win
