#include "stdafx.h"
#include "SettingsPanel.h"
#include "Resource.h"

#include <commctrl.h>
#include "helpers/settings_panel_i18n.hpp"
#include "settings_panel_helpers.hpp"

using settings_panel_internals::kKernelCount;
using settings_panel_internals::kResDims;
using settings_panel_internals::kResPresetCount;
using settings_panel_internals::kRatioPresetCount;

void CSettingsView::CreateResizeControls(HWND hwnd, HINSTANCE inst, int x0, int cx, int rh, int dy, int sepW, int cw, int trackW)
{
    namespace sc = pmui::settings_controls;
    auto& G    = m_resizeControls;
    const auto& rs = pmui::settings_panel_i18n::resize_strings_for(m_display_language);
    const wchar_t* browse_caption
        = pmui::settings_panel_i18n::browse_button_caption(m_display_language);
    auto S = [&](int v) { return DpiScaleInt(v); };

    auto combo = [&](int id, int y, int w, int dropH = 200) -> HWND {
        HWND h = sc::create_preset_combo(hwnd, inst, id, cx, y, w, S(dropH));
        G.push_back(h);
        return h;
    };
    auto edit = [&](int id, LPCWSTR txt, int x, int y, int w, DWORD extra = 0) -> HWND {
        HWND h
            = sc::create_single_line_edit(hwnd, inst, id, x, y, w, rh, txt, extra);
        G.push_back(h);
        return h;
    };
    auto check = [&](int id, LPCWSTR txt, int y, bool chk = false) -> HWND {
        HWND h = sc::create_autocheck(hwnd, inst, id, x0, y, sepW, rh, txt, chk);
        G.push_back(h);
        return h;
    };

    int y = S(6);
    AddThemedSep(G, hwnd, inst, x0, sepW, y);
    y += S(10);
    G.push_back(::CreateWindowExW(0, L"STATIC", rs.sec_dimensions,
        WS_CHILD | WS_VISIBLE, x0, y, sepW, S(16), hwnd, nullptr, inst, nullptr));
    y += S(18);

    MakeLabel(rs.lbl_resolution, y);
    m_hResPreset = combo(IDC_COMBO_RES_PRESET, y, cw, 280);
    for (int i = 0; i < kResPresetCount; ++i)
        ::SendMessageW(m_hResPreset, CB_ADDSTRING, 0, (LPARAM)rs.res_preset[i]);
    ::SendMessageW(m_hResPreset, CB_SETCURSEL, 0, 0);
    y += dy;

    MakeLabel(rs.lbl_ratio, y);
    m_hRatio = combo(IDC_COMBO_RATIO, y, cw, 220);
    for (int i = 0; i < kRatioPresetCount; ++i)
        ::SendMessageW(m_hRatio, CB_ADDSTRING, 0, (LPARAM)rs.ratio_preset[i]);
    ::SendMessageW(m_hRatio, CB_SETCURSEL, 0, 0);
    y += dy;

    MakeLabel(rs.lbl_size, y);
    m_hMaxW = edit(IDC_EDIT_MAX_W, L"0", cx, y, S(54), ES_NUMBER);
    G.push_back(::CreateWindowExW(0, L"STATIC", L"\u00D7",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        cx + S(62), y + S(3), S(16), S(16), hwnd, nullptr, inst, nullptr));
    m_hMaxH = edit(IDC_EDIT_MAX_H, L"0", cx + S(86), y, S(54), ES_NUMBER);
    y += dy;

    MakeLabel(rs.lbl_fit, y);
    m_hFit = combo(IDC_COMBO_FIT, y, cw, 150);
    for (int i = 0; i < 5; ++i)
        ::SendMessageW(m_hFit, CB_ADDSTRING, 0, (LPARAM)rs.fit[i]);
    ::SendMessageW(m_hFit, CB_SETCURSEL, 0, 0);
    y += dy + S(4);

    AddThemedSep(G, hwnd, inst, x0, sepW, y);
    y += S(10);
    G.push_back(::CreateWindowExW(0, L"STATIC", rs.sec_output,
        WS_CHILD | WS_VISIBLE, x0, y, sepW, S(16), hwnd, nullptr, inst, nullptr));
    y += S(18);

    MakeLabel(rs.lbl_destination, y);
    m_hPreset = combo(IDC_COMBO_PRESET_OUT, y, cw, 140);
    for (int i = 0; i < 3; ++i)
        ::SendMessageW(m_hPreset, CB_ADDSTRING, 0, (LPARAM)rs.dest[i]);
    ::SendMessageW(m_hPreset, CB_SETCURSEL, 1, 0);
    y += dy;

    MakeLabel(rs.lbl_folder, y);
    m_hOutDir = edit(IDC_EDIT_OUT_DIR, L"", cx, y, sc::field_width_with_browse(hwnd, cw));
    m_hBtnBrowse = sc::create_browse_button(
        hwnd, inst, IDC_BTN_BROWSE_OUT, sc::browse_button_x(hwnd, cx, cw), y, rh, browse_caption);
    G.push_back(m_hBtnBrowse);
    y += dy;

    MakeLabel(rs.lbl_format, y);
    m_hFormat = combo(IDC_COMBO_FORMAT, y, cw, 160);
    for (int i = 0; i < 6; ++i)
        ::SendMessageW(m_hFormat, CB_ADDSTRING, 0, (LPARAM)rs.format[i]);
    ::SendMessageW(m_hFormat, CB_SETCURSEL, 0, 0);
    G.push_back(m_hFormat);
    y += dy + S(4);

    AddThemedSep(G, hwnd, inst, x0, sepW, y);
    y += S(10);
    G.push_back(::CreateWindowExW(0, L"STATIC", rs.sec_quality,
        WS_CHILD | WS_VISIBLE, x0, y, sepW, S(16), hwnd, nullptr, inst, nullptr));
    y += S(18);

    m_hSliderQuality = ::CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        x0, y, trackW, S(26), hwnd, (HMENU)(UINT_PTR)IDC_SLIDER_QUALITY, inst, nullptr);
    ::SendMessageW(m_hSliderQuality, TBM_SETRANGE, TRUE, MAKELONG(1, 100));
    ::SendMessageW(m_hSliderQuality, TBM_SETPOS, TRUE, 85);
    G.push_back(m_hSliderQuality);
    m_hLblQuality = ::CreateWindowExW(0, L"STATIC", L"85",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        x0 + trackW + S(4), y + S(4), S(34), S(18), hwnd, (HMENU)(UINT_PTR)IDC_LBL_QUALITY, inst, nullptr);
    G.push_back(m_hLblQuality);
    y += dy;

    MakeLabel(rs.lbl_kernel, y);
    m_hKernel = combo(IDC_COMBO_KERNEL, y, cw, 160);
    for (int i = 0; i < kKernelCount; ++i)
        ::SendMessageW(m_hKernel, CB_ADDSTRING, 0, (LPARAM)rs.kernel[i]);
    ::SendMessageW(m_hKernel, CB_SETCURSEL, 0, 0);
    y += dy + S(4);

    AddThemedSep(G, hwnd, inst, x0, sepW, y);
    y += S(10);
    G.push_back(::CreateWindowExW(0, L"STATIC", rs.sec_options,
        WS_CHILD | WS_VISIBLE, x0, y, sepW, S(16), hwnd, nullptr, inst, nullptr));
    y += S(18);
    m_hAutorot = check(IDC_CHK_AUTOROT, rs.chk_autorot, y, true);
    y += dy - S(6);
    m_hEnlarge = check(IDC_CHK_ENLARGE, rs.chk_enlarge, y, false);
    y += dy - S(6);
    m_hStrip = check(IDC_CHK_STRIP, rs.chk_strip, y, true);
    // Section cards (§15) were removed: full-size GDI statics + z-order risked opaque overlays
    // and “hover to reveal” on sibling combos. Fluent shell colour stays in `theme_palette` / WM_ERASEBKGND.
}

