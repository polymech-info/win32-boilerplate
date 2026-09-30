#include "stdafx.h"
#include "SettingsPanel.h"
#include "Resource.h"

#include <commctrl.h>
#include "helpers/settings_panel_i18n.hpp"

void CSettingsView::CreateCompressControls(HWND hwnd, HINSTANCE inst, int x0, int lw, int cx, int rh, int sepW, int cw, int cmpSliderW)
{
    namespace sc = pmui::settings_controls;
    auto& C    = m_compressControls;
    const auto& cs = pmui::settings_panel_i18n::compress_strings_for(m_display_language);
    const wchar_t* browse_caption
        = pmui::settings_panel_i18n::browse_button_caption(m_display_language);
    auto S = [&](int v) { return DpiScaleInt(v); };

    auto cmpFieldLabel = [&](std::vector<HWND>& g, LPCWSTR t, int sy) {
        g.push_back(sc::create_field_label(
            {hwnd, inst, x0, sy, lw, t, S(sc::Layout::label_h)}));
    };
    auto cmpCombo = [&](std::vector<HWND>& g, int id, int sy, int w) -> HWND {
        HWND h = sc::create_preset_combo(hwnd, inst, id, cx, sy, w, S(200));
        g.push_back(h);
        return h;
    };
    auto cmpCheck = [&](std::vector<HWND>& g, int id, LPCWSTR t, int sy, bool chk = false) -> HWND {
        HWND h = ::CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            x0, sy, sepW, S(22), hwnd, (HMENU)(UINT_PTR)id, inst, nullptr);
        if (chk) ::SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
        g.push_back(h);
        return h;
    };
    auto slider = [&](std::vector<HWND>& g, int id, int sy, int lo, int hi, int val) -> HWND {
        HWND h = ::CreateWindowExW(0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
            cx, sy, cmpSliderW, S(26), hwnd, (HMENU)(UINT_PTR)id, inst, nullptr);
        ::SendMessageW(h, TBM_SETRANGE, TRUE, MAKELONG(lo, hi));
        ::SendMessageW(h, TBM_SETPOS, TRUE, val);
        g.push_back(h);
        return h;
    };
    auto sliderLbl = [&](std::vector<HWND>& g, int id, LPCWSTR txt, int sy) -> HWND {
        HWND h = ::CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE | SS_CENTER,
            cx + cmpSliderW + S(4), sy + S(4), S(34), S(18), hwnd, (HMENU)(UINT_PTR)id, inst, nullptr);
        g.push_back(h);
        return h;
    };

    AddThemedSep(C, hwnd, inst, x0, sepW, S(6));
    C.push_back(::CreateWindowExW(0, L"STATIC", cs.sec_output, WS_CHILD | WS_VISIBLE, x0, S(12), sepW, S(16), hwnd, nullptr, inst, nullptr));
    cmpFieldLabel(C, cs.lbl_destination, S(32));
    m_hCmpDest = cmpCombo(C, IDC_CMP_DEST, S(32), cw);
    for (int i = 0; i < 3; ++i) ::SendMessageW(m_hCmpDest, CB_ADDSTRING, 0, (LPARAM)cs.dest[i]);
    ::SendMessageW(m_hCmpDest, CB_SETCURSEL, 1, 0);
    cmpFieldLabel(C, cs.lbl_folder, S(63));
    m_hCmpDir = sc::create_single_line_edit(
        hwnd, inst, IDC_CMP_DIR, cx, S(63), sc::field_width_with_browse(hwnd, cw), rh, L"");
    C.push_back(m_hCmpDir);
    m_hCmpBrowse = sc::create_browse_button(
        hwnd, inst, IDC_CMP_BROWSE, sc::browse_button_x(hwnd, cx, cw), S(63), rh, browse_caption);
    C.push_back(m_hCmpBrowse);

    AddThemedSep(C, hwnd, inst, x0, sepW, S(97));
    C.push_back(::CreateWindowExW(0, L"STATIC", cs.sec_format, WS_CHILD | WS_VISIBLE, x0, S(103), sepW, S(16), hwnd, nullptr, inst, nullptr));
    cmpFieldLabel(C, cs.lbl_compressor, S(122));
    m_hCmpFormat = cmpCombo(C, IDC_CMP_FORMAT, S(122), cw);
    ::SendMessageW(m_hCmpFormat, CB_ADDSTRING, 0, (LPARAM)cs.compressor[0]);
    ::SendMessageW(m_hCmpFormat, CB_ADDSTRING, 0, (LPARAM)cs.compressor[1]);
    ::SendMessageW(m_hCmpFormat, CB_SETCURSEL, 0, 0);

    auto& P = m_cmpPngControls;
    AddThemedSep(P, hwnd, inst, x0, sepW, S(154));
    P.push_back(::CreateWindowExW(0, L"STATIC", cs.sec_png, WS_CHILD | WS_VISIBLE, x0, S(160), sepW, S(16), hwnd, nullptr, inst, nullptr));
    cmpFieldLabel(P, cs.lbl_level, S(180));
    m_hCmpLevel = slider(P, IDC_CMP_LEVEL_SLIDER, S(180), 1, 9, 9);
    m_hCmpLevelLbl = sliderLbl(P, IDC_CMP_LEVEL_LBL, L"9", S(180));
#ifdef FEATURE_PNG_COMPRESSOR
    AddThemedSep(P, hwnd, inst, x0, sepW, S(218));
    P.push_back(::CreateWindowExW(0, L"STATIC", cs.sec_quantise, WS_CHILD | WS_VISIBLE, x0, S(224), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hCmpQuantize = cmpCheck(P, IDC_CMP_QUANTIZE, cs.chk_quantize, S(244));
    cmpFieldLabel(P, cs.lbl_colors, S(270));
    m_hCmpColors = cmpCombo(P, IDC_CMP_COLORS, S(270), cw);
    for (int i = 0; i < 4; ++i) ::SendMessageW(m_hCmpColors, CB_ADDSTRING, 0, (LPARAM)cs.colors[i]);
    ::SendMessageW(m_hCmpColors, CB_SETCURSEL, 0, 0);
    cmpFieldLabel(P, cs.lbl_png_quality, S(302));
    m_hCmpQualSlider = slider(P, IDC_CMP_QUAL_SLIDER, S(302), 60, 100, 85);
    m_hCmpQualLbl = sliderLbl(P, IDC_CMP_QUAL_LBL, L"85", S(302));
#endif
#ifdef FEATURE_PNG_ZOPFLI
    AddThemedSep(P, hwnd, inst, x0, sepW, S(340));
    P.push_back(::CreateWindowExW(0, L"STATIC", cs.sec_advanced, WS_CHILD | WS_VISIBLE, x0, S(346), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hCmpZopfli = cmpCheck(P, IDC_CMP_ZOPFLI, cs.chk_zopfli, S(366));
#endif

    auto& M = m_cmpMozjpegControls;
    AddThemedSep(M, hwnd, inst, x0, sepW, S(154));
    M.push_back(::CreateWindowExW(0, L"STATIC", cs.sec_mozjpeg, WS_CHILD | WS_VISIBLE, x0, S(160), sepW, S(16), hwnd, nullptr, inst, nullptr));
    cmpFieldLabel(M, cs.lbl_jpeg_quality, S(180));
    m_hCmpJpegSlider = slider(M, IDC_CMP_JPEG_SLIDER, S(180), 1, 100, 85);
    m_hCmpJpegLbl = sliderLbl(M, IDC_CMP_JPEG_LBL, L"85", S(180));
    m_hCmpProgressive = cmpCheck(M, IDC_CMP_PROGRESSIVE, cs.chk_progressive, S(218), true);
    m_hCmpTrellis = cmpCheck(M, IDC_CMP_TRELLIS, cs.chk_trellis, S(244));

    AddThemedSep(C, hwnd, inst, x0, sepW, S(394));
    C.push_back(::CreateWindowExW(0, L"STATIC", cs.sec_options, WS_CHILD | WS_VISIBLE, x0, S(400), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hCmpStrip = cmpCheck(C, IDC_CMP_STRIP, cs.chk_strip, S(420), true);
}

