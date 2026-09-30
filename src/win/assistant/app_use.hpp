#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace media::assistant::app_use {

struct Point {
    int x = 0;
    int y = 0;
};

struct ScreenInfo {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct LaunchOptions {
    std::wstring exe;
    std::wstring args;
    std::wstring cwd;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int wait_ms = 1000;
};

struct LaunchResult {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

bool open_app(const LaunchOptions& opts, LaunchResult& out, std::string& err);
ScreenInfo virtual_screen();
Point cursor_position();
Point scale_point_from_api(Point api_point, int api_width, int api_height);
Point scale_point_to_api(Point screen_point, int api_width, int api_height);

/// Instant cursor move (teleport + a synthetic absolute MOUSEEVENTF_MOVE so apps receive WM_MOUSEMOVE
/// at the final position). Cheapest, used internally by smooth_move per frame.
bool move_mouse(int x, int y, std::string& err);

/// Animated cursor move from current position to (x, y). `duration_ms == 0` teleports (equivalent
/// to `move_mouse`); otherwise we walk a linear path at roughly one frame (16 ms) per intermediate
/// point. If `duration_ms < 0`, a reasonable default is computed from travel distance:
///   ~4000 px/sec, clamped to [60, 350] ms. This is the default for click/drag pre-movements
/// because it lets sticky-hover apps (menus, tooltips, custom drag handles) react to the cursor.
bool smooth_move(int x, int y, int duration_ms, std::string& err);

bool click_point(int x, int y, const std::wstring& button, int count, bool virtual_click, std::string& err);

/// Point-to-point drag. Convenience for the common case. Internally just builds a 2-point path and
/// delegates to `drag_path`.
bool drag_point(int x1, int y1, int x2, int y2, const std::wstring& button, int steps, int duration_ms, std::string& err);

/// Drag along an arbitrary polyline. The first point is where the button goes down, the last is
/// where it comes up. Total time across all intermediate moves is `duration_ms`; pacing is
/// distance-proportional so a long-then-short segment doesn't suddenly speed up. Pass at least
/// 2 points. If fewer than 2, returns an error.
bool drag_path(const std::vector<Point>& path, const std::wstring& button, int duration_ms, std::string& err);

/// Mouse wheel. `clicks > 0` scrolls "up" (toward content top) by convention. `horizontal=true`
/// uses MOUSEEVENTF_HWHEEL (positive = right). The wheel event lands on the window currently under
/// the cursor — call `smooth_move`/`move_mouse` first to target a specific control.
bool scroll_wheel(int clicks, bool horizontal, std::string& err);

bool mouse_button_down(const std::wstring& button, std::string& err);
bool mouse_button_up(const std::wstring& button, std::string& err);
bool activate_window(HWND hwnd, std::string& err);
bool find_window(DWORD pid, HWND hwnd, const std::wstring& title_contains, HWND& out, std::string& err);
bool type_text(const std::wstring& text, std::string& err);
bool send_hotkey(const std::vector<std::wstring>& keys, std::string& err);

// Low-level single-key control. Useful for melodic / rhythmic input where
// the duration the key is held matters (e.g. virtual pianos like FreePiano:
// the host treats key-down as NoteOn and key-up as NoteOff, so the time
// between them is the note's sustain length).
bool key_down(const std::wstring& key, std::string& err);
bool key_up  (const std::wstring& key, std::string& err);

// Press a key while holding `modifiers` (any of ctrl/shift/alt/win), wait
// `hold_ms` milliseconds, then release the key and the modifiers in reverse
// order. `hold_ms` is clamped to [0, 30000] -- the caller decides the rest
// gap between keystrokes (see app_batch's `delayMs`).
bool press_key(const std::wstring& key,
               const std::vector<std::wstring>& modifiers,
               int hold_ms,
               std::string& err);
bool set_clipboard_text(const std::wstring& text, std::string& err);
bool get_clipboard_text(std::wstring& out, std::string& err);
std::vector<std::wstring> split_keys(const std::wstring& spec);

} // namespace media::assistant::app_use
