#pragma once
//
// pmui::ui_font — single source of truth for the UI font used across every
// dock panel, dialog and child control.
//
// Built from NONCLIENTMETRICS::lfMessageFont (Segoe UI 9pt on Windows 10/11)
// with `extra_pt` points added on top.  `extra_pt` is read from settings.json
// `appearance.font_size_extra_pt` at startup (default = +2pt) and can be
// changed live via `set_extra_pt()` followed by re-applying the font on every
// open HWND.
//
// Replaces all `GetStockObject(DEFAULT_GUI_FONT)` and ad-hoc CreateFont calls
// across `ui_next/`.
//
#include <Windows.h>

namespace pmui {

/// Initialise the cached font from settings (called once at startup, before
/// any panel creates controls).  Safe to call again to reload from disk.
void ui_font_init_from_settings();

/// Current "extra points on top of the system message font" value.
int  ui_font_extra_pt();

/// Set the extra-pt value, rebuild the cached HFONT, and return the new HFONT.
/// Callers are responsible for re-broadcasting `WM_SETFONT` to their controls.
HFONT ui_font_set_extra_pt(int extra_pt);

/// Cached HFONT for the current configuration.
/// The handle is owned by this module (do NOT DeleteObject from callers).
HFONT ui_font();

/// Apply the current UI font to `hwnd` and every descendant (recursive).
void  apply_font_to_tree(HWND hwnd);

} // namespace pmui
