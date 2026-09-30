#pragma once
// Shared control factories for CSettingsView (docs/win32xx-next.md §4, §7, §17).
// Prefer struct parameters + pmui::settings_widgets::* for new code (Policy).

#include "helpers/ui_constants.hpp"
#include <Windows.h>
#include <algorithm>

namespace pmui::settings_controls {

using Layout = pmui::ui::SettingsPaneLayout;

inline int dpi_scale(HWND hwnd, int value)
{
    UINT dpi = 96;
    if (hwnd && ::IsWindow(hwnd))
        dpi = ::GetDpiForWindow(hwnd);
    return ::MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

// ── Parameter structs (avoid long positional argument lists) ─────────────────

struct PresetComboParams {
    HWND     parent{};
    HINSTANCE inst{};
    int      id{};
    int      x{};
    int      y{};
    int      combo_w{};
    int      drop_height{200};
};

struct LineEditParams {
    HWND      parent{};
    HINSTANCE inst{};
    int       id{};
    int       x{};
    int       y{};
    int       field_w{};
    int       field_h{};
    LPCWSTR   initial{L""};
    DWORD     extra_style{0};
};

struct AutocheckParams {
    HWND      parent{};
    HINSTANCE inst{};
    int       id{};
    int       x{};
    int       y{};
    int       row_w{};
    int       row_h{};
    LPCWSTR   text{};
    bool      checked{false};
};

struct BrowseButtonParams {
    HWND      parent{};
    HINSTANCE inst{};
    int       id{};
    int       x{};
    int       y{};
    int       row_h{};
    LPCWSTR   caption{};
};

struct FieldLabelParams {
    HWND      parent{};
    HINSTANCE inst{};
    int       x0{};
    int       y_row{};
    int       label_w{}; ///< Full label column width (text area = label_w - 6)
    LPCWSTR   text{};
    int       text_h{}; ///< Usually Layout::label_h
};

// ── Geometry helpers (browse + edit row) ───────────────────────────────────

inline int field_width_with_browse(int cw)
{
    return (std::max)(0, cw - Layout::browse_min_w - Layout::browse_field_gap);
}

inline int field_width_with_browse(HWND hwnd, int cw)
{
    return (std::max)(0, cw - dpi_scale(hwnd, Layout::browse_min_w) - dpi_scale(hwnd, Layout::browse_field_gap));
}

inline int browse_button_x(int cx, int cw) { return cx + (std::max)(0, cw - Layout::browse_min_w); }

inline int browse_button_x(HWND hwnd, int cx, int cw)
{
    return cx + (std::max)(0, cw - dpi_scale(hwnd, Layout::browse_min_w));
}

// ── Create functions ─────────────────────────────────────────────────────────

inline HWND create_field_label(const FieldLabelParams& p)
{
    return ::CreateWindowExW(0, L"STATIC", p.text, WS_CHILD | WS_VISIBLE | SS_RIGHT, p.x0,
        p.y_row + dpi_scale(p.parent, 4), p.label_w - dpi_scale(p.parent, 6), p.text_h, p.parent, nullptr,
        p.inst, nullptr);
}

inline HWND create_preset_combo(const PresetComboParams& p)
{
    // No WS_EX_CLIENTEDGE: `apply_window_theme_recursive` (dark combo = `L""` + subclass) supplies
    // `control_bg` for the listbox/static/edit paths; avoids Explorer/CFD light field fill.
    return ::CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL, p.x,
        p.y - dpi_scale(p.parent, 1), p.combo_w, p.drop_height, p.parent, (HMENU)(UINT_PTR)p.id, p.inst, nullptr);
}

inline HWND create_single_line_edit(const LineEditParams& p)
{
    return ::CreateWindowExW(0, L"EDIT", p.initial,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | p.extra_style, p.x, p.y, p.field_w, p.field_h, p.parent,
        (HMENU)(UINT_PTR)p.id, p.inst, nullptr);
}

inline HWND create_autocheck(const AutocheckParams& p)
{
    HWND h = ::CreateWindowExW(0, L"BUTTON", p.text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, p.x, p.y,
        p.row_w, p.row_h, p.parent, (HMENU)(UINT_PTR)p.id, p.inst, nullptr);
    if (p.checked)
        ::SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
    return h;
}

inline HWND create_browse_button(const BrowseButtonParams& p)
{
    return ::CreateWindowExW(0, L"BUTTON", p.caption, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, p.x, p.y,
        dpi_scale(p.parent, Layout::browse_min_w), p.row_h, p.parent, (HMENU)(UINT_PTR)p.id, p.inst, nullptr);
}

// Legacy flat overloads (existing call sites) ──────────────────────────────

inline HWND create_preset_combo(HWND parent, HINSTANCE inst, int id, int x, int y, int w, int drop_h = 200)
{
    return create_preset_combo(PresetComboParams{parent, inst, id, x, y, w, drop_h});
}

inline HWND create_single_line_edit(
    HWND parent, HINSTANCE inst, int id, int x, int y, int w, int h, LPCWSTR initial, DWORD extra_style = 0)
{
    return create_single_line_edit(LineEditParams{parent, inst, id, x, y, w, h, initial, extra_style});
}

inline HWND create_autocheck(HWND parent, HINSTANCE inst, int id, int x, int y, int row_w, int row_h, LPCWSTR text,
    bool checked = false)
{
    return create_autocheck(AutocheckParams{parent, inst, id, x, y, row_w, row_h, text, checked});
}

inline HWND create_browse_button(
    HWND parent, HINSTANCE inst, int id, int x, int y, int row_h, LPCWSTR caption)
{
    return create_browse_button(BrowseButtonParams{parent, inst, id, x, y, row_h, caption});
}

} // namespace pmui::settings_controls
