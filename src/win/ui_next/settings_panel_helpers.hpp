#pragma once
// Shared constexpr tables + small helpers for CSettingsView (split across SettingsPanel_*.cpp).

#include <Windows.h>
#include <commctrl.h> // TTM_*, TOOLINFOW
#include <string>
#include <vector>

namespace settings_panel_internals {

/// Long-edge presets for Meta / Find / Transform in-memory pre-resize (libvips).
inline constexpr int kPreresizeWidthPresets[] = {256, 512, 768, 1024};
inline constexpr int kPreresizeWidthPresetCount
    = (int)(sizeof(kPreresizeWidthPresets) / sizeof(kPreresizeWidthPresets[0]));

/** Combo selection index (0..3) → pixel width. Default: 512 (index 1). */
inline int preresize_width_from_cb_sel(int sel) {
    if (sel >= 0 && sel < kPreresizeWidthPresetCount) return kPreresizeWidthPresets[sel];
    return 512;
}

inline constexpr LONG_PTR kSettingsSepUserData = (LONG_PTR)(0x50535345u); // 'PSSE'
/// GDI-painted `STATIC` for §15 “section card” backdrops in `CSettingsView::AddSectionCard`.
inline constexpr LONG_PTR kSettingsCardUserData = (LONG_PTR)(0x44414352u); // 'RCAD'

// Resize preset geometry (labels → settings_panel_i18n::resize_strings_for)
struct ResDim {
    int w, h;
};
inline constexpr ResDim kResDims[] = {
    {0, 0},
    {1920, 1080},
    {1280, 720},
    {1024, 768},
    {800, 600},
    {1080, 1080},
    {1200, 628},
    {1080, 1920},
    {3840, 2160},
    {2560, 1440},
    {2000, 0},
    {1500, 0},
    {1200, 0},
    {800, 0},
    {400, 0},
    {512, 512},
    {256, 256},
    {128, 128},
};
inline constexpr int kResPresetCount = (int)(sizeof(kResDims) / sizeof(kResDims[0]));

struct RatioVal {
    int rw, rh;
};
inline constexpr RatioVal kRatioVals[] = {
    {0, 0},
    {1, 1},
    {16, 9},
    {9, 16},
    {4, 3},
    {3, 4},
    {3, 2},
    {2, 3},
    {21, 9},
    {5, 4},
};
inline constexpr int kRatioPresetCount = (int)(sizeof(kRatioVals) / sizeof(kRatioVals[0]));

inline const char* const kKernelValues[] = {
    "lanczos3", "mitchell", "lanczos2", "cubic", "nearest",
};
inline constexpr int kKernelCount = (int)(sizeof(kKernelValues) / sizeof(kKernelValues[0]));

inline std::wstring get_edit_text(HWND h)
{
    int n = ::GetWindowTextLengthW(h);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n) + 1, L'\0');
    ::GetWindowTextW(h, w.data(), n + 1);
    w.resize(static_cast<size_t>(n));
    return w;
}

inline int get_edit_int(HWND h, int def = 0)
{
    std::wstring t = get_edit_text(h);
    if (t.empty()) return def;
    return _wtoi(t.c_str());
}

/// Tooltip helper — used from CreateControls (core) and InstallDuplicateTooltips (read cpp).
inline void add_settings_tooltip(HWND panel, HWND* pTip, HWND child, const wchar_t* text)
{
    if (!child || !text) return;
    if (!*pTip) {
        *pTip = ::CreateWindowExW(WS_EX_TOPMOST, L"tooltips_class32", nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, panel, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!*pTip) return;
        (void)::SetWindowPos(*pTip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    TOOLINFOW ti{};
    ti.cbSize   = sizeof(TOOLINFOW);
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = panel;
    ti.uId      = (UINT_PTR)child;
    ti.lpszText = const_cast<LPWSTR>(text);
    (void)::SendMessageW(*pTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    (void)::SendMessageW(*pTip, TTM_SETMAXTIPWIDTH, 0, 480);
}

} // namespace settings_panel_internals
