#include "ui_font.hpp"
#include "win/settings_store.hpp"

namespace pmui {

namespace {
HFONT g_font     = nullptr;
int   g_extra_pt = 2;        // default — overridden by ui_font_init_from_settings()

HFONT build_font(int extra_pt)
{
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        return static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));

    LOGFONTW lf = ncm.lfMessageFont;
    if (extra_pt > 0) {
        // lfHeight is in logical units. Negative => character height in points
        // converted by GDI; we just multiply by the same scale.
        // For Segoe UI 9pt, lfHeight is typically -12 at 96 DPI.
        // Bumping by N points means subtracting N * (96/72) ≈ N*1.33 logical units.
        // GDI accepts the resulting value directly.
        const int dpi = ::GetDeviceCaps(::GetDC(nullptr), LOGPIXELSY);
        const int delta_logical = -::MulDiv(extra_pt, dpi ? dpi : 96, 72);
        lf.lfHeight += delta_logical;
    }
    HFONT h = ::CreateFontIndirectW(&lf);
    if (!h) h = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    return h;
}
} // namespace

void ui_font_init_from_settings()
{
    media::settings::AppearanceSettings a;
    std::string err;
    media::settings::load_appearance(a, err);
    g_extra_pt = a.font_size_extra_pt;
    if (g_font) { ::DeleteObject(g_font); g_font = nullptr; }
    g_font = build_font(g_extra_pt);
}

int ui_font_extra_pt() { return g_extra_pt; }

HFONT ui_font_set_extra_pt(int extra_pt)
{
    if (extra_pt < 0) extra_pt = 0;
    if (extra_pt > 8) extra_pt = 8;
    g_extra_pt = extra_pt;
    if (g_font) { ::DeleteObject(g_font); g_font = nullptr; }
    g_font = build_font(g_extra_pt);
    return g_font;
}

HFONT ui_font()
{
    if (!g_font) g_font = build_font(g_extra_pt);
    return g_font;
}

void apply_font_to_tree(HWND hwnd)
{
    if (!hwnd) return;
    HFONT hf = ui_font();
    ::SendMessageW(hwnd, WM_SETFONT, (WPARAM)hf, TRUE);
    ::EnumChildWindows(hwnd, [](HWND h, LPARAM lp) -> BOOL {
        ::SendMessageW(h, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)hf);
}

} // namespace pmui
