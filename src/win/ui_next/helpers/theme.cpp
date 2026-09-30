#include "theme.hpp"

#include <UIRibbon.h>
#include <UIRibbonPropertyHelpers.h>

#include <algorithm>

#include <commctrl.h> // ListView_GetHeader, HDM_*
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

#include <cstring>

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif
#ifndef DWMWCP_DEFAULT
#define DWMWCP_DEFAULT 0
#endif

namespace pmui {

void flatten_win32_host_frame(HWND h, COLORREF border_rgb)
{
    if (!h || !::IsWindow(h)) return;
    LONG_PTR st = ::GetWindowLongPtrW(h, GWL_STYLE);
    // Do not remove WS_THICKFRAME — only the classic thin border / dialog frame.
    const LONG_PTR newSt = st & ~(LONG_PTR)(WS_BORDER | WS_DLGFRAME);
    if (newSt != st) {
        ::SetWindowLongPtrW(h, GWL_STYLE, newSt);
        ::SetWindowPos(h, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    LONG_PTR ex = ::GetWindowLongPtrW(h, GWL_EXSTYLE);
    const LONG_PTR newEx = (ex & ~(LONG_PTR)(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
    if (newEx != ex) {
        ::SetWindowLongPtrW(h, GWL_EXSTYLE, newEx);
        ::SetWindowPos(h, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    // Win11+: kill light “window border” around Chromium surfaces.
    COLORREF b = border_rgb;
    if (::DwmSetWindowAttribute(h, DWMWA_BORDER_COLOR, &b, sizeof(b)) != S_OK) {
        // optional — older OS
    }
}

// `CBS_DROPDOWNLIST`’s read-only field can send `WM_CTLCOLORSTATIC` to the combo; the dropped
// listbox sends `WM_CTLCOLORLISTBOX`. An embedded edit (rare) sends `WM_CTLCOLOREDIT`. The panel
// never sees these — subclass the combo and return `control_bg` / `control_fg` for all three.
namespace {
static constexpr UINT_PTR kComboboxCtlColorSubId = 100u;
static constexpr UINT_PTR kPushButtonDarkSubId  = 102u;

// After the default `COMBOBOX` `WM_PAINT` (GDI still draws a light dropdown button, bright
// client edge, and selection tint), repaint the static field, button, and 1px outline from
// `ThemePalette` so the control matches `EDIT` rows in settings panels.
static void combobox_dark_postpaint_chrome(HWND w)
{
    const auto& pal = theme_palette();
    if (!pal.dark) return;
    if (!w || !::IsWindow(w)) return;

    HDC hdc = ::GetDC(w);
    if (!hdc) return;

    COMBOBOXINFO cbi{};
    cbi.cbSize = sizeof(cbi);
    if (!::GetComboBoxInfo(w, &cbi)) {
        (void)::ReleaseDC(w, hdc);
        return;
    }

    HBRUSH fill = ::CreateSolidBrush(pal.control_bg);
    if (fill) {
        (void)::FillRect(hdc, &cbi.rcItem, fill);
        if (cbi.rcButton.right > cbi.rcButton.left && cbi.rcButton.bottom > cbi.rcButton.top)
            (void)::FillRect(hdc, &cbi.rcButton, fill);
        (void)::DeleteObject(fill);
    }

    wchar_t buf[1024]{};
    (void)::GetWindowTextW(w, buf, static_cast<int>(sizeof(buf) / sizeof(buf[0])));
    (void)::SetBkMode(hdc, TRANSPARENT);
    (void)::SetTextColor(hdc, pal.control_fg);
    {
        HFONT  hf = (HFONT)::SendMessageW(w, WM_GETFONT, 0, 0);
        HGDIOBJ o = hf ? ::SelectObject(hdc, hf) : nullptr;
        RECT   tr  = cbi.rcItem;
        (void)::InflateRect(&tr, -2, -2);
        (void)::DrawTextW(
            hdc, buf, -1, &tr, (UINT)(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX));
        if (hf) (void)::SelectObject(hdc, o);
    }
    if (cbi.rcButton.right > cbi.rcButton.left) {
        const int  left   = (int)cbi.rcButton.left, right  = (int)cbi.rcButton.right;
        const int  top    = (int)cbi.rcButton.top,   bottom = (int)cbi.rcButton.bottom;
        const int  cx     = (left + right) / 2;
        const int  cy     = (top + bottom) / 2;
        HPEN         pen  = ::CreatePen(PS_SOLID, 1, pal.control_fg);
        const HGDIOBJ old = pen ? ::SelectObject(hdc, pen) : nullptr;
        if (pen) {
            (void)::MoveToEx(hdc, cx - 3, cy, nullptr);
            (void)::LineTo(hdc, cx,     cy + 3);
            (void)::LineTo(hdc, cx + 3, cy);
            (void)::SelectObject(hdc, old);
            (void)::DeleteObject(pen);
        }
    }

    RECT cr{};
    (void)::GetClientRect(w, &cr);
    // Dark: `caption_pen` on `control_bg` reads as a harsh inset; blend toward field fill
    // (same idea as `ChatProviderDlg` group strokes — low-contrast hairline).
    COLORREF edgeRgb = pal.caption_pen;
    if (pal.dark) {
        edgeRgb = RGB((GetRValue(pal.control_bg) * 5 + GetRValue(pal.caption_pen)) / 6,
            (GetGValue(pal.control_bg) * 5 + GetGValue(pal.caption_pen)) / 6,
            (GetBValue(pal.control_bg) * 5 + GetBValue(pal.caption_pen)) / 6);
    }
    HPEN edge = ::CreatePen(PS_SOLID, 1, edgeRgb);
    if (edge) {
        const HGDIOBJ o = ::SelectObject(hdc, edge);
        const int     r2 = (std::max)(cr.left, cr.right - 1);
        const int     b2 = (std::max)(cr.top, cr.bottom - 1);
        (void)::MoveToEx(hdc, cr.left, cr.top, nullptr);
        (void)::LineTo(hdc, r2,   cr.top);
        (void)::LineTo(hdc, r2,   b2);
        (void)::LineTo(hdc, cr.left,  b2);
        (void)::LineTo(hdc, cr.left,  cr.top);
        (void)::SelectObject(hdc, o);
        (void)::DeleteObject(edge);
    }
    (void)::ReleaseDC(w, hdc);
}

// Flatten classic push`BUTTON` in dark: default paint still comes through light gray + bevel
// from commctl, even with `L""` + `WM_CTLCOLORBTN` on the parent in some paths.
static void pushbutton_dark_postpaint_flat(HWND w)
{
    const auto& pal = theme_palette();
    if (!pal.dark || !w || !::IsWindow(w)) return;
    const int t = static_cast<int>(::GetWindowLongPtrW(w, GWL_STYLE) & BS_TYPEMASK);
    if (t != BS_PUSHBUTTON && t != BS_DEFPUSHBUTTON) return;

    HDC  hdc  = ::GetDC(w);
    if (!hdc) return;
    RECT rc{};
    (void)::GetClientRect(w, &rc);
    HBRUSH br = ::CreateSolidBrush(pal.control_bg);
    if (br) {
        (void)::FillRect(hdc, &rc, br);
        (void)::DeleteObject(br);
    }
    wchar_t cap[256]{};
    (void)::GetWindowTextW(w, cap, (int)(sizeof(cap) / sizeof(cap[0])));
    (void)::SetBkMode(hdc, TRANSPARENT);
    (void)::SetTextColor(
        hdc, ::IsWindowEnabled(w) ? pal.control_fg : pal.caption_fg_inactive);
    {
        HFONT  hf = (HFONT)::SendMessageW(w, WM_GETFONT, 0, 0);
        HGDIOBJ o = hf ? ::SelectObject(hdc, hf) : nullptr;
        (void)::DrawTextW(
            hdc, cap, -1, &rc, (UINT)(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX));
        if (hf) (void)::SelectObject(hdc, o);
    }
    HPEN edge = ::CreatePen(PS_SOLID, 1, pal.caption_pen);
    if (edge) {
        const HGDIOBJ o = ::SelectObject(hdc, edge);
        const int     r2 = (std::max)(rc.left, rc.right - 1);
        const int     b2 = (std::max)(rc.top, rc.bottom - 1);
        (void)::MoveToEx(hdc, rc.left, rc.top, nullptr);
        (void)::LineTo(hdc, r2,   rc.top);
        (void)::LineTo(hdc, r2,   b2);
        (void)::LineTo(hdc, rc.left,  b2);
        (void)::LineTo(hdc, rc.left,  rc.top);
        (void)::SelectObject(hdc, o);
        (void)::DeleteObject(edge);
    }
    if (::GetFocus() == w) {
        HPEN ap = ::CreatePen(PS_SOLID, 1, pal.accent);
        if (ap) {
            const HGDIOBJ o  = ::SelectObject(hdc, ap);
            RECT            fr = rc;
            (void)::InflateRect(&fr, -2, -2);
            (void)::MoveToEx(hdc, fr.left,  fr.top,    nullptr);
            (void)::LineTo(hdc,  fr.right - 1, fr.top);
            (void)::LineTo(hdc,  fr.right - 1, fr.bottom - 1);
            (void)::LineTo(hdc,  fr.left,   fr.bottom - 1);
            (void)::LineTo(hdc,  fr.left,   fr.top);
            (void)::SelectObject(hdc, o);
            (void)::DeleteObject(ap);
        }
    }
    (void)::ReleaseDC(w, hdc);
}

static LRESULT CALLBACK pushbutton_dark_gdi_subclass(
    HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (m == WM_PAINT) {
        const LRESULT r = ::DefSubclassProc(w, m, wp, lp);
        pushbutton_dark_postpaint_flat(w);
        return r;
    }
    if (m == WM_NCDESTROY)
        (void)::RemoveWindowSubclass(w, pushbutton_dark_gdi_subclass, kPushButtonDarkSubId);
    return ::DefSubclassProc(w, m, wp, lp);
}

static LRESULT CALLBACK combobox_ctlcolor_subclass(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR,
    DWORD_PTR) {
    if (m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX || m == WM_CTLCOLORSTATIC) {
        HDC                 hdc = reinterpret_cast<HDC>(wp);
        const auto&         pal = theme_palette();
        (void)::SetBkColor(hdc, pal.control_bg);
        (void)::SetTextColor(hdc, pal.control_fg);
        static HBRUSH s_br{nullptr};
        static COLORREF s_c = 0xFFFFFFFF;
        if (!s_br || s_c != pal.control_bg) {
            if (s_br)
                (void)::DeleteObject(s_br);
            s_br = ::CreateSolidBrush(pal.control_bg);
            s_c  = pal.control_bg;
        }
        return reinterpret_cast<LRESULT>(s_br);
    }
    if (m == WM_PAINT) {
        const LRESULT r = ::DefSubclassProc(w, m, wp, lp);
        combobox_dark_postpaint_chrome(w);
        return r;
    }
    const LRESULT r = ::DefSubclassProc(w, m, wp, lp);
    if (m == WM_NCDESTROY)
        (void)::RemoveWindowSubclass(w, combobox_ctlcolor_subclass, kComboboxCtlColorSubId);
    return r;
}

// Depth-first: theme walk may create a deep host tree; combos can live on inner pages.
static void apply_combobox_subclass_to_descendants(HWND parent)
{
    if (!parent) return;
    (void)::EnumChildWindows(
        parent,
        [](HWND c, LPARAM) -> BOOL {
            apply_combobox_subclass_to_descendants(c);
            wchar_t cls[32]{};
            (void)::GetClassNameW(c, cls, static_cast<int>(sizeof(cls) / sizeof(cls[0])));
            if (lstrcmpiW(cls, L"COMBOBOX") == 0) {
                (void)::SetWindowSubclass(c, combobox_ctlcolor_subclass, kComboboxCtlColorSubId, 0);
            }
            return TRUE;
        },
        0);
}
} // namespace

// File-local state (internal linkage) — all theme.cpp helpers in `namespace pmui`
// can access these without an outer anonymous-namespace scoping trap.
static ThemePalette       g_palette;
static media::settings::Theme g_choice = media::settings::Theme::System;
static bool             g_initialised = false;

// ── Palettes ────────────────────────────────────────────────────────────────

ThemePalette make_light()
{
    // Microsoft 365 / Fluent "light" — neutral grays, flat surfaces, M365 blue accent.
    ThemePalette p{};
    p.dark               = false;
    p.window_bg          = RGB(243, 242, 241); // #F3F2F1 shell background
    p.web_surface_bg     = RGB(255, 255, 255);
    p.window_fg          = RGB( 50,  49,  48); // #323130 primary text
    p.control_bg         = RGB(255, 255, 255);
    p.control_fg         = RGB( 32,  31,  30);
    p.caption_bg         = RGB(250, 249, 248);
    p.caption_fg         = RGB( 96,  94,  92);
    p.caption_bg_inactive= RGB(250, 249, 248);
    p.caption_fg_inactive= RGB(161, 159, 157);
    p.caption_pen        = RGB(225, 223, 221); // neutral hairline / dividers
    p.bar                = RGB(237, 235, 233); // neutralLightQuaternaryAlt
    p.accent             = RGB(  0, 120, 212); // #0078D4
    return p;
}

ThemePalette make_dark()
{
    ThemePalette p{};
    p.dark               = true;
    p.window_bg          = RGB( 30,  30,  30);
    p.web_surface_bg      = RGB( 31,  33,  37);   // tailwind `surface.dark` #1f2125 (chat HTML)
    p.window_fg          = RGB(225, 225, 225);
    p.control_bg         = RGB( 45,  45,  45);
    p.control_fg         = RGB(232, 232, 232);
    p.caption_bg         = RGB( 24,  24,  24);
    p.caption_fg         = RGB(225, 225, 225);
    p.caption_bg_inactive= RGB( 24,  24,  24);
    p.caption_fg_inactive= RGB(140, 140, 140);
    p.caption_pen        = RGB( 60,  60,  60);
    p.bar                = RGB( 60,  60,  60);
    p.accent             = RGB( 64, 156, 230);
    return p;
}

static bool windows_is_dark_mode_impl()
{
    HKEY hKey = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD value = 1, sz = sizeof(value), type = 0;
    bool dark = false;
    if (::RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, &type,
                            reinterpret_cast<BYTE*>(&value), &sz) == ERROR_SUCCESS &&
        type == REG_DWORD)
        dark = (value == 0);
    ::RegCloseKey(hKey);
    return dark;
}

void resolve_palette()
{
    bool dark = false;
    switch (g_choice) {
    case media::settings::Theme::Light:  dark = false; break;
    case media::settings::Theme::Dark:   dark = true;  break;
    case media::settings::Theme::System:
    default:                              dark = windows_is_dark_mode_impl(); break;
    }
    g_palette = dark ? make_dark() : make_light();
}

namespace {
void sync_preferred_app_mode_impl();
}

void theme_init_from_settings()
{
    media::settings::AppearanceSettings a;
    std::string err;
    media::settings::load_appearance(a, err);
    g_choice = a.theme;
    resolve_palette();
    g_initialised = true;
}

void theme_set(media::settings::Theme t)
{
    g_choice = t;
    resolve_palette();
    g_initialised = true;
    sync_preferred_app_mode_impl();
}

bool is_dark_active()
{
    if (!g_initialised) theme_init_from_settings();
    return g_palette.dark;
}

const ThemePalette& theme_palette()
{
    if (!g_initialised) theme_init_from_settings();
    return g_palette;
}

bool windows_is_dark_mode()
{
    return windows_is_dark_mode_impl();
}

void apply_dark_titlebar(HWND hwnd, bool dark)
{
    if (!hwnd) return;
    BOOL v = dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 build 19041+),
    // earlier builds used 19. Try 20 first, fall back to 19.
    if (::DwmSetWindowAttribute(hwnd, 20, &v, sizeof(v)) != S_OK)
        ::DwmSetWindowAttribute(hwnd, 19, &v, sizeof(v));
}

void apply_dwm_toplevel_frame_tint(HWND hwnd, COLORREF border_rgb)
{
    if (!hwnd || !::IsWindow(hwnd)) return;
    COLORREF b = border_rgb;
    (void)::DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &b, sizeof(b));
    // Win11+: `SetWindowRgn` fights this; use OS default corner rounding.
    const DWORD cornerPref = DWMWCP_DEFAULT;
    (void)::DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
}

// ── Undocumented uxtheme entry points (ordinal-only, Win10 1809+) ───────────
//
// Standard controls (BUTTON/COMBOBOX/LISTVIEW/etc.) only render dark when:
//   1. The process opts in via SetPreferredAppMode  (ordinal 135).
//   2. Each top-level window opts in via AllowDarkModeForWindow (ordinal 133).
//   3. Each control's theme is set to one of the DarkMode_* visual styles via
//      SetWindowTheme.
//
// These ordinals are not part of the public Win32 SDK; we resolve them at
// runtime, fall back to no-op when they're unavailable.
//
namespace {

enum class PreferredAppMode { Default = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3, Max = 4 };

using FnSetPreferredAppMode    = PreferredAppMode (WINAPI*)(PreferredAppMode);
using FnAllowDarkModeForWindow = BOOL             (WINAPI*)(HWND, BOOL);
using FnFlushMenuThemes        = void             (WINAPI*)();

struct UxThemeOrdinals {
    HMODULE                     mod = nullptr;
    FnSetPreferredAppMode       SetPreferredAppMode    = nullptr;
    FnAllowDarkModeForWindow    AllowDarkModeForWindow = nullptr;
    FnFlushMenuThemes           FlushMenuThemes        = nullptr;
};

UxThemeOrdinals& uxtheme()
{
    static UxThemeOrdinals u;
    if (u.mod) return u;
    u.mod = ::LoadLibraryW(L"uxtheme.dll");
    if (!u.mod) return u;
    u.SetPreferredAppMode    = reinterpret_cast<FnSetPreferredAppMode>(
        reinterpret_cast<void*>(::GetProcAddress(u.mod, MAKEINTRESOURCEA(135))));
    u.AllowDarkModeForWindow = reinterpret_cast<FnAllowDarkModeForWindow>(
        reinterpret_cast<void*>(::GetProcAddress(u.mod, MAKEINTRESOURCEA(133))));
    u.FlushMenuThemes        = reinterpret_cast<FnFlushMenuThemes>(
        reinterpret_cast<void*>(::GetProcAddress(u.mod, MAKEINTRESOURCEA(136))));
    return u;
}

void set_window_theme(HWND h, LPCWSTR sub_app, LPCWSTR sub_id)
{
    // SetWindowTheme is well-known and safe; ignore HRESULT — older themes
    // simply don't apply the requested parts.
    ::SetWindowTheme(h, sub_app, sub_id);
}

void sync_preferred_app_mode_impl()
{
    auto& u = uxtheme();
    if (!u.SetPreferredAppMode)
        return;
    PreferredAppMode mode = PreferredAppMode::Default;
    switch (g_choice) {
    case media::settings::Theme::Light:
        mode = PreferredAppMode::ForceLight;
        break;
    case media::settings::Theme::Dark:
        // AllowDark alone still left Windows’ UIRibbon.dll on a light Fluent path on many
        // Win10/11 builds; ForceDark aligns the process with shell dark controls so
        // in-app “Dark” can reach NetUI ribbon hosts (still best-effort — UIRibbon is closed).
        mode = PreferredAppMode::ForceDark;
        break;
    case media::settings::Theme::System:
    default:
        mode = windows_is_dark_mode_impl() ? PreferredAppMode::ForceDark
                                            : PreferredAppMode::Default;
        break;
    }
    u.SetPreferredAppMode(mode);
    if (u.FlushMenuThemes)
        u.FlushMenuThemes();
}

} // namespace

void sync_preferred_app_mode()
{
    if (!g_initialised)
        theme_init_from_settings();
    sync_preferred_app_mode_impl();
}

void enable_app_dark_mode(bool /*allow*/)
{
    sync_preferred_app_mode();
}

void theme_listview_report_header(HWND hListView)
{
    if (!hListView || !::IsWindow(hListView))
        return;
    HWND hHdr = ListView_GetHeader(hListView);
    if (!hHdr)
        return;
    const auto& pal = theme_palette();
    auto&       u   = uxtheme();
    if (u.AllowDarkModeForWindow)
        u.AllowDarkModeForWindow(hHdr, pal.dark ? TRUE : FALSE);

    // Disable Windows theme on header to allow custom drawing to work properly
    // in both light and dark modes. The custom draw handler fills the background.
    ::SetWindowTheme(hHdr, L"", L"");
#ifndef HDM_SETBKCOLOR
#define HDM_SETBKCOLOR (HDM_FIRST + 29)
#endif
#ifndef HDM_SETTEXTCOLOR
// Vista+ — column label text; without this, dark bg can still paint black ink.
#define HDM_SETTEXTCOLOR (HDM_FIRST + 38)
#endif
    // Use window_bg for seamless edge-to-edge look (was caption_bg)
    ::SendMessageW(hHdr, HDM_SETBKCOLOR, 0, (LPARAM)pal.window_bg);
    ::SendMessageW(hHdr, HDM_SETTEXTCOLOR, 0, (LPARAM)pal.control_fg);
    ::InvalidateRect(hHdr, nullptr, TRUE);
}

bool theme_header_customdraw_notify(HWND hListView, LPARAM notify_lparam, LRESULT& out)
{
    if (!hListView || !::IsWindow(hListView))
        return false;
    const auto* nh = reinterpret_cast<LPNMHDR>(notify_lparam);
    if (!nh)
        return false;
    const HWND hHdr = ListView_GetHeader(hListView);
    if (!hHdr || nh->hwndFrom != hHdr || nh->code != NM_CUSTOMDRAW)
        return false;
    const auto* pcd = reinterpret_cast<LPNMCUSTOMDRAW>(notify_lparam);
    const auto&   pal = theme_palette();
    if (pcd->dwDrawStage == CDDS_PREPAINT) {
        // Fill background for seamless edge-to-edge look
        HBRUSH br = ::CreateSolidBrush(pal.window_bg);
        if (br) {
            ::FillRect(pcd->hdc, &pcd->rc, br);
            ::DeleteObject(br);
        }
        ::SetTextColor(pcd->hdc, pal.control_fg);
        ::SetBkMode(pcd->hdc, TRANSPARENT);
        // Notify for each item to ensure they all get themed
        out = CDRF_NOTIFYITEMDRAW;
        return true;
    }
    // `SysHeader32` can send `CDDS_ITEMPREPAINT` with extra flags (e.g. subitem).
    if ((pcd->dwDrawStage & CDDS_ITEMPREPAINT) == CDDS_ITEMPREPAINT) {
        // Draw the item ourselves. Native SysHeader32 can repaint individual
        // columns with the light theme after dock restore/resize, leaving a
        // white strip until the next full invalidation.
        HBRUSH br = ::CreateSolidBrush(pal.window_bg);
        if (br) {
            ::FillRect(pcd->hdc, &pcd->rc, br);
            ::DeleteObject(br);
        }
        const int item = static_cast<int>(pcd->dwItemSpec);
        wchar_t text[256]{};
        HDITEMW hdi{};
        hdi.mask = HDI_TEXT | HDI_FORMAT;
        hdi.pszText = text;
        hdi.cchTextMax = static_cast<int>(sizeof(text) / sizeof(text[0]));
        (void)::SendMessageW(hHdr, HDM_GETITEMW, static_cast<WPARAM>(item), reinterpret_cast<LPARAM>(&hdi));

        RECT textRc = pcd->rc;
        const UINT dpi = static_cast<UINT>(::GetDpiForWindow(hHdr));
        const int pad = ::MulDiv(6, dpi == 0 ? 96 : static_cast<int>(dpi), 96);
        textRc.left += pad;
        textRc.right -= pad;
        ::SetTextColor(pcd->hdc, pal.control_fg);
        ::SetBkMode(pcd->hdc, TRANSPARENT);
        UINT align = DT_LEFT;
        if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_CENTER)
            align = DT_CENTER;
        else if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT)
            align = DT_RIGHT;
        ::DrawTextW(pcd->hdc, text, -1, &textRc,
            align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        HPEN pen = ::CreatePen(PS_SOLID, 1, pal.caption_pen);
        if (pen) {
            HGDIOBJ old = ::SelectObject(pcd->hdc, pen);
            ::MoveToEx(pcd->hdc, pcd->rc.right - 1, pcd->rc.top, nullptr);
            ::LineTo(pcd->hdc, pcd->rc.right - 1, pcd->rc.bottom);
            ::SelectObject(pcd->hdc, old);
            ::DeleteObject(pen);
        }
        out = CDRF_SKIPDEFAULT;
        return true;
    }
    return false;
}

// ── Rounded “pill” clip (native controls have no corner-radius style) ─────
// `SetWindowRgn` + `CreateRoundRectRgn`. Subclass keeps the region in sync on
// resize. Skips multiline EDIT: scrollbars sit in the client and would be clipped.
namespace {

enum : UINT_PTR { kInputFieldRoundSubId = 0x1F2E3D4C };

void input_field_recompute_client_round_region(HWND hWnd)
{
    if (!hWnd || !::IsWindow(hWnd)) return;
    RECT rc{};
    (void)::GetClientRect(hWnd, &rc);
    int w  = (int)(rc.right - rc.left);
    int ch = (int)(rc.bottom - rc.top);
    if (w < 2 || ch < 2) return;
    const UINT dpi   = (UINT)::GetDpiForWindow(hWnd);
    const int  dip   = (dpi == 0 ? 96 : (int)dpi);
    // GDI uses ellipse w/h for the corner; ~8@96DPI ≈ VS / Fluent 4px corner radius
    int ew = (std::max)(4, ::MulDiv(8, dip, 96));
    int eh = ew;
    const int cap = (std::min)(w, ch) / 2;
    if (cap < 1) return;
    ew = (std::min)(ew, (std::max)(2, cap * 2 - 1));
    eh = (std::min)(eh, (std::max)(2, cap * 2 - 1));
    HRGN rgn = ::CreateRoundRectRgn(0, 0, w, ch, ew, eh);
    if (!rgn) return;
    if (!::SetWindowRgn(hWnd, rgn, TRUE)) {
        (void)::DeleteObject(rgn);
    }
}

bool input_field_may_apply_round_rgn(HWND hWnd)
{
    wchar_t cls[32]{};
    if (!hWnd || !::GetClassNameW(hWnd, cls, 32)) return false;
    // Combobox: `SetWindowRgn` + unthemed paint can read as a bright outline; only clip EDITs.
    if (lstrcmpiW(cls, L"EDIT") == 0) {
        const LONG_PTR st = ::GetWindowLongPtrW(hWnd, GWL_STYLE);
        if ((st & ES_MULTILINE) != 0) return false;
        // Skip the embedded EDIT child of CBS_DROPDOWN: rounding it produces a
        // rounded pill inside the combo's square NC border (visible in light mode
        // as grey corners where window_bg shows through). The COMBOBOX itself owns
        // the border; rounding its internal edit makes it look wrong.
        // docs/combobox.md §4.2
        HWND par = ::GetParent(hWnd);
        if (par) {
            wchar_t pcls[32]{};
            ::GetClassNameW(par, pcls, 32);
            if (lstrcmpiW(pcls, L"COMBOBOX") == 0) return false;
        }
        return true;
    }
    return false;
}

LRESULT CALLBACK input_field_round_subclass(
    HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR /*subId*/, DWORD_PTR)
{
    switch (msg) {
    case WM_WINDOWPOSCHANGED: {
        const auto* p = reinterpret_cast<const WINDOWPOS*>(lp);
        if (p && (p->flags & SWP_NOSIZE) == 0) input_field_recompute_client_round_region(hWnd);
        break;
    }
    case WM_NCDESTROY:
        (void)::RemoveWindowSubclass(hWnd, input_field_round_subclass, kInputFieldRoundSubId);
        break;
    default:
        break;
    }
    return ::DefSubclassProc(hWnd, msg, wp, lp);
}

void input_field_apply_rounding_install(HWND hWnd)
{
    if (!input_field_may_apply_round_rgn(hWnd)) return;
    (void)::SetWindowSubclass(hWnd, input_field_round_subclass, kInputFieldRoundSubId, 0);
    input_field_recompute_client_round_region(hWnd);
}

} // namespace

void polish_flat_combo_chrome(HWND combo_hwnd)
{
    if (!combo_hwnd || !::IsWindow(combo_hwnd))
        return;

    auto& u = uxtheme();
    if (u.AllowDarkModeForWindow)
        u.AllowDarkModeForWindow(combo_hwnd, theme_palette().dark ? TRUE : FALSE);

    if (theme_palette().dark)
        set_window_theme(combo_hwnd, L"", L"");
    else
        set_window_theme(combo_hwnd, L"Explorer", nullptr);

    // Sunken / light 3D frame that survives generic theming on some builds.
    LONG_PTR st = ::GetWindowLongPtrW(combo_hwnd, GWL_STYLE);
    const LONG_PTR newSt = st & ~(LONG_PTR)(WS_BORDER | WS_DLGFRAME);
    if (newSt != st) {
        ::SetWindowLongPtrW(combo_hwnd, GWL_STYLE, newSt);
        ::SetWindowPos(combo_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    LONG_PTR ex = ::GetWindowLongPtrW(combo_hwnd, GWL_EXSTYLE);
    const LONG_PTR newEx = ex & ~(LONG_PTR)(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    if (newEx != ex) {
        ::SetWindowLongPtrW(combo_hwnd, GWL_EXSTYLE, newEx);
        ::SetWindowPos(combo_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    (void)::SetWindowSubclass(combo_hwnd, combobox_ctlcolor_subclass, kComboboxCtlColorSubId, 0);
    ::InvalidateRect(combo_hwnd, nullptr, TRUE);
}

// Does not run on the File tree / IExplorerBrowser docker — Shell owns children;
// that panel uses EBO_NOBORDER etc. in FileTreePanel.cpp (Mainfrm excludes it here).
void apply_window_theme_recursive(HWND root, bool dark)
{
    if (!root) return;

    auto& u = uxtheme();
    if (u.AllowDarkModeForWindow) u.AllowDarkModeForWindow(root, dark ? TRUE : FALSE);

    struct Visitor {
        bool dark;
        UxThemeOrdinals& u;
    } ctx{ dark, u };

    auto themer = [](HWND child, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Visitor*>(lp);

        wchar_t cls[64]{};
        ::GetClassNameW(child, cls, 64);

        // ── Skip only Win32++ dock splitter surfaces ─────────────────────────
        // Those depend on hit-testing + cursor state; `DockClient` strips edge
        // flags in PreCreate. Other Win32++ windows (e.g. "Win32++ Window" for
        // CChatWebView, "Win32++ TabPage") must still be themed — they were
        // incorrectly skipped as a block, leaving WebView2 hosts with a bright
        // WS_EX_CLIENTEDGE frame in dark mode.
        if (lstrcmpiW(cls, L"Win32++ Bar") == 0 || lstrcmpiW(cls, L"Win32++ DockClient") == 0)
            return TRUE;

        if (c->u.AllowDarkModeForWindow) c->u.AllowDarkModeForWindow(child, c->dark ? TRUE : FALSE);

        // Strip 3D sunken chrome on every descendant (not only EDIT/COMBO):
        // WebView2 / Edge child HWNDs often ship with WS_EX_CLIENTEDGE.
        {
            LONG_PTR       ex    = ::GetWindowLongPtrW(child, GWL_EXSTYLE);
            const LONG_PTR newEx = (ex & ~(LONG_PTR)(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
            if (newEx != ex) {
                ::SetWindowLongPtrW(child, GWL_EXSTYLE, newEx);
                ::SetWindowPos(child, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        }

        // Pick the right visual style class per control type.
        // - "DarkMode_Explorer" is the universal dark theme class for most
        //   common controls (EDIT listbox, LISTVIEW, …).
        // - COMBOBOX (dark): `DarkMode_Explorer` / `DarkMode_CFD` both paint a *light* field
        //   (white/gray + black text) in common configurations. Use unthemed `L""` like
        //   single-line `EDIT` so the combo’s `WM_CTLCOLOREDIT` / `WM_CTLCOLORLISTBOX` /
        //   `WM_CTLCOLORSTATIC` path (subclass in `apply_combobox_subclass_to_descendants`) wins.
        // - BUTTON push (dark): `DarkMode_Explorer` draws a strong outline; `L""` + parent
        //   `WM_CTLCOLORBTN` (control_bg) flattens — requires each dialog to handle push in
        //   WM_CTLCOLORBTN (Settings, AppSettings, Provider, ChatProvider).
        if (c->dark) {
            // Autocheck / autoradio / auto3state: uxtheme still draws the caption with
            // default (black) ink under DarkMode_Explorer, ignoring the panel's
            // WM_CTLCOLORBTN + NM_CUSTOMDRAW SetTextColor. Clearing the sub-app id
            // falls back to classic BUTTON rendering so parent-set colours win.
            if (lstrcmpiW(cls, L"BUTTON") == 0) {
                set_window_theme(child, L"", L"");
                const int bst = (int)(::GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK);
                if (bst == BS_PUSHBUTTON || bst == BS_DEFPUSHBUTTON) {
                    (void)::SetWindowSubclass(
                        child, pushbutton_dark_gdi_subclass, kPushButtonDarkSubId, 0);
                }
            }
            else if (lstrcmpiW(cls, L"EDIT") == 0) {
                // Single-line: themed EDIT can still render typed text with default black
                // ink on dark bg (multiline path respects WM_CTLCOLOREDIT consistently).
                const LONG_PTR est = ::GetWindowLongPtrW(child, GWL_STYLE);
                if ((est & ES_MULTILINE) == 0)
                    set_window_theme(child, L"", L"");
                else
                    set_window_theme(child, L"DarkMode_Explorer", nullptr);
            }
            else if (lstrcmpiW(cls, L"LISTBOX")  == 0) set_window_theme(child, L"DarkMode_Explorer", nullptr);
            else if (lstrcmpiW(cls, L"COMBOBOX") == 0)
                set_window_theme(child, L"", L"");
            else if (lstrcmpiW(cls, L"ComboLBox")== 0) set_window_theme(child, L"DarkMode_Explorer", nullptr);
            else if (lstrcmpiW(cls, L"SysListView32") == 0) {
                set_window_theme(child, L"DarkMode_Explorer", nullptr);
                // Header sub-control of listview
                HWND hHdr = ListView_GetHeader(child);
                if (hHdr) set_window_theme(hHdr, L"DarkMode_ItemsView", nullptr);
            }
            else if (lstrcmpiW(cls, L"SysHeader32")  == 0) set_window_theme(child, L"DarkMode_ItemsView", nullptr);
            else if (lstrcmpiW(cls, L"SysTreeView32")== 0) set_window_theme(child, L"DarkMode_Explorer", nullptr);
            // Explorer dark track still reads as a bright channel; use classic so the channel
            // matches the rest of the GDI-tinted settings controls.
            else if (lstrcmpiW(cls, L"msctls_trackbar32") == 0) set_window_theme(child, L"", L"");
            else if (lstrcmpiW(cls, L"msctls_progress32") == 0) set_window_theme(child, L"DarkMode_Explorer", nullptr);
            else if (lstrcmpiW(cls, L"ScrollBar")    == 0) set_window_theme(child, L"DarkMode_Explorer", nullptr);
            else if (lstrcmpiW(cls, L"ToolbarWindow32") == 0) set_window_theme(child, L"DarkMode_Explorer", nullptr);
            else if (lstrcmpiW(cls, L"SysPager") == 0) set_window_theme(child, L"DarkMode_Explorer", nullptr);
        } else {
            // Light: Explorer-themed parts — avoids classic “WinXP” defaults and CFD’s
            // thick combo frame. Static labels pick up Explorer text rendering.
            if (lstrcmpiW(cls, L"BUTTON")           == 0
             || lstrcmpiW(cls, L"EDIT")             == 0
             || lstrcmpiW(cls, L"LISTBOX")          == 0
             || lstrcmpiW(cls, L"ComboLBox")        == 0
             || lstrcmpiW(cls, L"SysListView32")    == 0
             || lstrcmpiW(cls, L"SysTreeView32")    == 0
             || lstrcmpiW(cls, L"msctls_trackbar32")== 0
             || lstrcmpiW(cls, L"msctls_progress32")== 0
             || lstrcmpiW(cls, L"ScrollBar")        == 0
             || lstrcmpiW(cls, L"ToolbarWindow32") == 0
             || lstrcmpiW(cls, L"SysPager") == 0)
                set_window_theme(child, L"Explorer", nullptr);
            else if (lstrcmpiW(cls, L"Static")       == 0)
                set_window_theme(child, L"Explorer", nullptr);
            else if (lstrcmpiW(cls, L"COMBOBOX")    == 0)
                set_window_theme(child, L"Explorer", nullptr);
            else if (lstrcmpiW(cls, L"SysHeader32") == 0)
                set_window_theme(child, L"ItemsView", nullptr);
            else if (lstrcmpiW(cls, L"SysListView32") == 0) {
                HWND hHdr = ListView_GetHeader(child);
                if (hHdr) set_window_theme(hHdr, L"ItemsView", nullptr);
            }
        }
        // Force a repaint so the change is visible immediately.
        ::SendMessageW(child, WM_THEMECHANGED, 0, 0);
        ::InvalidateRect(child, nullptr, TRUE);
        if (input_field_may_apply_round_rgn(child)) input_field_apply_rounding_install(child);
        return TRUE;
    };

    ::EnumChildWindows(root, themer, reinterpret_cast<LPARAM>(&ctx));
    apply_combobox_subclass_to_descendants(root);
    ::SendMessageW(root, WM_THEMECHANGED, 0, 0);
    ::InvalidateRect(root, nullptr, TRUE);
}

void nuke_webview2_host_chrome(HWND root_host, COLORREF border_rgb)
{
    if (!root_host) return;
    flatten_win32_host_frame(root_host, border_rgb);
    ::SetWindowTheme(root_host, L"", L"");
    auto visitor = [](HWND child, LPARAM lp) -> BOOL {
        const auto* bg = reinterpret_cast<COLORREF*>(lp);
        wchar_t cls[128]{};
        ::GetClassNameW(child, cls, 128);
        // Chromium / Edge WebView2: kill theme chrome that paints a pale frame.
        if (::wcsstr(cls, L"Chrome_RenderWidget") || ::wcsstr(cls, L"Chrome_WidgetWin") ||
            ::wcsstr(cls, L"Intermediate D3D")) {
            ::SetWindowTheme(child, L"", L"");
        }
        // Legacy IWebBrowser2 (Trident / IE Web OC): classic sunken inner border.
        if (lstrcmpiW(cls, L"Internet Explorer_Server") == 0
            || lstrcmpiW(cls, L"Shell DocObject View") == 0) {
            ::SetWindowTheme(child, L"", L"");
        }
        flatten_win32_host_frame(child, *bg);
        return TRUE;
    };
    COLORREF bg = border_rgb;
    ::EnumChildWindows(root_host, visitor, reinterpret_cast<LPARAM>(&bg));
    ::InvalidateRect(root_host, nullptr, TRUE);
}

void flatten_webview_host_parent_chain(HWND leaf, COLORREF border_rgb, int max_depth)
{
    if (!leaf) return;
    for (HWND h = ::GetParent(leaf); h && max_depth > 0; h = ::GetParent(h), --max_depth) {
        const LONG_PTR st = ::GetWindowLongPtrW(h, GWL_STYLE);
        // Top-level windows (main frame, floating docks) are NOT WS_CHILD. Never call
        // SetWindowTheme / strip chrome on them — it destroys caption rendering and NC
        // hit-testing (drag, min/max/close). Only true child HWNDs in the dock/WebView chain.
        if ((st & WS_CHILD) == 0)
            break;
        flatten_win32_host_frame(h, border_rgb);
        DWORD cornerPref = DWMWCP_DONOTROUND;
        ::DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
        ::SetWindowTheme(h, L"", L"");
        ::InvalidateRect(h, nullptr, TRUE);
    }
}

// QAT command id (RibbonUI.h; own-ribbon tab strip)
static constexpr UINT kRibbonQatCommandId = 701;

RibbonGlobalTint ribbon_global_tint_from_palette(const ThemePalette& pal)
{
    RibbonGlobalTint t{};
    if (!pal.dark) {
        // Win10-style blue-white ribbon (matches pre-theme-tint behaviour).
        t.background = static_cast<uint32_t>(UI_HSB(215, 80, 220));
        t.highlight  = static_cast<uint32_t>(UI_HSB(215, 90, 240));
        t.text       = static_cast<uint32_t>(UI_HSB(0, 0, 20));
        return t;
    }
    // Dark: anchor brightness to caption strip + control layer + readable text.
    const auto max_ch = [](COLORREF c) -> int {
        return (std::max)({ static_cast<int>(GetRValue(c)),
                            static_cast<int>(GetGValue(c)),
                            static_cast<int>(GetBValue(c)) });
    };
    const int br_cap = max_ch(pal.caption_bg);
    const int br_ctl = max_ch(pal.control_bg);
    const int br_txt = max_ch(pal.window_fg);
    const int hi_bri = (std::min)(255, br_ctl + 18);
    t.background = static_cast<uint32_t>(UI_HSB(160, 26, static_cast<BYTE>(br_cap)));
    t.highlight  = static_cast<uint32_t>(UI_HSB(160, 58, static_cast<BYTE>(hi_bri)));
    t.text       = static_cast<uint32_t>(UI_HSB(0, 0, static_cast<BYTE>(br_txt)));
    return t;
}

void apply_ribbon_global_tint(IUIFramework* fw, const ThemePalette& pal)
{
    if (!fw) return;
    const RibbonGlobalTint tin = ribbon_global_tint_from_palette(pal);
    // Push the same HSB triple on every id UIRibbon might associate with global chrome
    // (QAT 701, application menu 710, and 0 = “unspecified / root” in some builds).
    static constexpr UINT kTargetCmdIds[] = { kRibbonQatCommandId, 710, 0 };
    for (UINT32 cmd : kTargetCmdIds) {
        auto set = [&](REFPROPERTYKEY key, uint32_t hsb) {
            PROPVARIANT pv{};
            pv.vt    = VT_UI4;
            pv.ulVal = static_cast<ULONG>(hsb);
            fw->SetUICommandProperty(cmd, key, pv);
            ::PropVariantClear(&pv);
        };
        set(UI_PKEY_GlobalBackgroundColor, tin.background);
        set(UI_PKEY_GlobalHighlightColor, tin.highlight);
        set(UI_PKEY_GlobalTextColor, tin.text);
    }
    static const PROPERTYKEY* const kGlobalKeys[] = {
        &UI_PKEY_GlobalBackgroundColor,
        &UI_PKEY_GlobalHighlightColor,
        &UI_PKEY_GlobalTextColor,
    };
    for (UINT32 cmd : kTargetCmdIds) {
        for (const PROPERTYKEY* pk : kGlobalKeys)
            fw->InvalidateUICommand(cmd, UI_INVALIDATIONS_PROPERTY, pk);
    }
    fw->FlushPendingInvalidations();
}

namespace {

void allow_dark_mode_walk(HWND h, bool dark)
{
    if (!h || !::IsWindow(h)) return;
    auto& u = uxtheme();
    if (u.AllowDarkModeForWindow)
        u.AllowDarkModeForWindow(h, dark ? TRUE : FALSE);
    for (HWND c = ::GetWindow(h, GW_CHILD); c; c = ::GetWindow(c, GW_HWNDNEXT))
        allow_dark_mode_walk(c, dark);
}

} // namespace

void allow_dark_mode_for_window_tree(HWND root, bool dark)
{
    if (!root || !::IsWindow(root)) return;
    allow_dark_mode_walk(root, dark);
}

} // namespace pmui
