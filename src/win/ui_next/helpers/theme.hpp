#pragma once
//
// pmui theme helpers — light / dark / system colour palette, dark-titlebar
// helper (DWMWA_USE_IMMERSIVE_DARK_MODE) and a registry probe for the user's
// Windows-wide preference.  Decoupled from any specific window class so any
// panel can paint correctly via a shared `theme_palette()` lookup.
//
#include <Windows.h>
#include "win/settings_store.hpp"

struct IUIFramework; // Windows SDK (uiribbon.h)

namespace pmui {

struct ThemePalette {
    bool     dark = false;
    COLORREF window_bg;        // panel/dialog background
    /// Matches `apps/chat` Tailwind `surface` / `surface-dark` — WebView2 chat uses this so
    /// `DefaultBackgroundColor`, WM_ERASEBKGND, and DWM edge tint align with the HTML shell.
    COLORREF web_surface_bg;
    COLORREF window_fg;        // text on window_bg
    COLORREF control_bg;       // EDIT / LISTBOX / LISTVIEW background
    COLORREF control_fg;
    COLORREF caption_bg;       // dock caption strip
    COLORREF caption_fg;
    COLORREF caption_bg_inactive;
    COLORREF caption_fg_inactive;
    COLORREF caption_pen;      // 1px caption outline
    COLORREF bar;              // splitter / dock bar colour
    COLORREF accent;           // selection / focus
};

/// Configure the theme module. Reads `appearance` from settings.json on first
/// call and re-evaluates whenever the user changes the preference.
void theme_init_from_settings();

/// Manually override the resolved theme (System|Light|Dark). Triggers a cache
/// refresh; caller is responsible for re-applying to open windows.
void theme_set(media::settings::Theme t);

/// True if the *resolved* theme is dark (System mode reads the registry).
bool is_dark_active();

/// Whether Windows itself is in dark mode (registry probe — read once and
/// refreshed on WM_SETTINGCHANGE in Mainfrm).
bool windows_is_dark_mode();

/// Resolved palette for the currently configured theme.
const ThemePalette& theme_palette();

//
// Windows Ribbon Framework (UIRibbon.dll) tints global chrome via
// UI_PKEY_Global* on the Quick Access Toolbar command (`cmdQAT`, id 701).
// `RibbonGlobalTint` packs the same DWORD layout as UI_HSBCOLOR.
struct RibbonGlobalTint {
    uint32_t background = 0; ///< UI_PKEY_GlobalBackgroundColor
    uint32_t highlight  = 0; ///< UI_PKEY_GlobalHighlightColor
    uint32_t text         = 0; ///< UI_PKEY_GlobalTextColor
};

/// Derive ribbon global HSB tints from the active palette (light: Win10-like;
/// dark: caption / control / foreground brightness aligned with `ThemePalette`).
RibbonGlobalTint ribbon_global_tint_from_palette(const ThemePalette& pal);

/// Push global ribbon colours through `IUIFramework` (no-op if @p fw is null).
void apply_ribbon_global_tint(IUIFramework* fw, const ThemePalette& pal);

/// Walk @p root and every descendant HWND and call `AllowDarkModeForWindow` (uxtheme
/// ordinal 133) so hidden UIRibbon / NetUI hosts under the main frame client area opt
/// into dark rendering. `apply_window_theme_recursive` only runs on dock panels, so
/// without this pass the stock ribbon often stays bright while the rest of the app is dark.
void allow_dark_mode_for_window_tree(HWND root, bool dark);

/// Apply DWMWA_USE_IMMERSIVE_DARK_MODE to a top-level window's titlebar.
/// Safe no-op on Windows < 10 1903.
void apply_dark_titlebar(HWND hwnd, bool dark);

/// Tints the DWM-drawn border to match @p border_rgb and restores the OS default
/// window-corner policy (Win11) — use instead of `SetWindowRgn`, which clashes with
/// the caption and produces ugly edges on top-level windows.
void apply_dwm_toplevel_frame_tint(HWND hwnd, COLORREF border_rgb);

/// Process-level "allow dark mode" toggle (uxtheme ordinal 135). Must be
/// called BEFORE applying per-window dark themes for the standard controls
/// to actually pick up DarkMode_Explorer and related dark style parts.
/// Safe no-op on Windows builds that don't expose the ordinal.
/// Internally calls `sync_preferred_app_mode()` — the bool is ignored.
void enable_app_dark_mode(bool allow);

/// Align uxtheme `SetPreferredAppMode` with App Settings (Light / Dark / System).
/// **Light** → `ForceLight`; **Dark** and resolved System-dark → `ForceDark` so
/// shell controls (including UIRibbon NetUI) can pick up dark rendering; light System → `Default`.
void sync_preferred_app_mode();

/// Theme the report-mode listview column header (`SysHeader32` child) to match
/// caption / control colours for the active palette (fixes bright Win32 headers in dark UI).
void theme_listview_report_header(HWND hListView);

/// Listview `WM_NOTIFY` helper: for the embedded `SysHeader32`, handle
/// `NM_CUSTOMDRAW` so column titles use `ThemePalette::control_fg` in dark
/// mode (HDM_SETTEXTCOLOR is ignored for some `SetWindowTheme` header styles).
/// If this returns `true`, the caller should return @p out from `WndProc`.
bool theme_header_customdraw_notify(HWND hListView, LPARAM notify_lparam, LRESULT& out);

/// Walk @p root and every descendant; for each known control class
/// (BUTTON, EDIT, COMBOBOX, LISTBOX, ComboLBox, SysListView32, SysHeader32,
///  SysTreeView32, msctls_trackbar32) call SetWindowTheme so the control
/// repaints with the right uxtheme parts for the active theme.
/// After theming, `COMBOBOX` is subclassed for embedded ctlcolors; single-line `EDIT` gets
/// a small rounded `SetWindowRgn` clip (combos skip the clip so the border is not
/// clashed with region). Multiline and scrollable fields are left square so scrollbars are
/// not cut off. Idempotent.
/// **Not** used on the File tree / `IExplorerBrowser` docker — Shell owns that subtree
/// (see `FileTreePanel.cpp`, `CMainFrame::ApplyAppearance`).
void apply_window_theme_recursive(HWND root, bool dark);

/// Filter-row `CBS_DROPDOWNLIST` helpers: strip classic light borders, opt into dark mode,
/// `SetWindowTheme`, and the same `WM_PAINT` / ctl-color subclass path as settings combos.
void polish_flat_combo_chrome(HWND combo_hwnd);

/// Strip Win32 WS_BORDER / NC edge bits and (on Win11+) DWM border tint so
/// embedded browser hosts (WebView2 or legacy `IWebBrowser2` / Trident) match
/// flat panels. Re-run after async child HWNDs appear (navigation, resize).
void nuke_webview2_host_chrome(HWND root_host, COLORREF border_rgb);

/// Walk parents from @p leaf (e.g. `CChatWebView`) — TabPage, `SysTabControl32`,
/// `DockClient` — and strip the same frame bits + clear uxtheme so comctl/dock
/// chrome does not paint a 2–4px light frame around embedded WebView2 in dark UI.
void flatten_webview_host_parent_chain(HWND leaf, COLORREF border_rgb, int max_depth = 8);

/// Strip classic `WS_BORDER` / `WS_EX_CLIENTEDGE` (and related) plus optional Win11
/// DWM border tint on a single HWND. Used for dock content roots where
/// `apply_window_theme_recursive` only walks children, not @p host itself.
void flatten_win32_host_frame(HWND host, COLORREF border_rgb);

} // namespace pmui
