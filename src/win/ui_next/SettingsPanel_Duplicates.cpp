#include "stdafx.h"
#include "SettingsPanel.h"
#include "Resource.h"

#include "helpers/settings_panel_i18n.hpp"
#include "win/settings_store.hpp"
#include "settings_controls.hpp"

void CSettingsView::CreateDuplicatesControls(HWND hwnd, HINSTANCE inst, int x0, int lw, int cx, int rh, int sepW, int cw)
{
    namespace sc = pmui::settings_controls;
    auto& G = m_dupControls;
    const auto& fs = pmui::settings_panel_i18n::find_strings_for(m_display_language);
    auto S = [&](int v) { return DpiScaleInt(v); };

    auto label = [&](LPCWSTR t, int sy) {
        G.push_back(sc::create_field_label({hwnd, inst, x0, sy, lw, t, S(sc::Layout::label_h)}));
    };
    const int chkW = sepW - S(sc::Layout::scrollbar_right_pad) - S(8); ///< Checkbox width clear of scrollbar
    auto check = [&](int id, LPCWSTR t, int sy, bool chk = false) -> HWND {
        HWND h = sc::create_autocheck({hwnd, inst, id, x0, sy, chkW, rh, t, chk});
        G.push_back(h);
        return h;
    };
    auto combo = [&](int id, int sy, int w) -> HWND {
        HWND h = sc::create_preset_combo({hwnd, inst, id, cx, sy, w, S(200)});
        G.push_back(h);
        return h;
    };

    AddThemedSep(G, hwnd, inst, x0, sepW, S(6));
    m_hDupHowSec = ::CreateWindowExW(0, L"STATIC", L"How to compare", WS_CHILD | WS_VISIBLE, x0, S(12), sepW, S(16), hwnd, nullptr, inst, nullptr);
    G.push_back(m_hDupHowSec);
    m_hDupMode = combo(IDC_DUP_MODE, S(32), sepW);
    ::SendMessageW(m_hDupMode, CB_ADDSTRING, 0, (LPARAM)L"Identical file size only");
    ::SendMessageW(m_hDupMode, CB_ADDSTRING, 0, (LPARAM)L"Similar looks (fingerprint)");
    ::SendMessageW(m_hDupMode, CB_ADDSTRING, 0, (LPARAM)L"Text & EXIF (side files)");
    ::SendMessageW(m_hDupMode, CB_SETCURSEL, 1, 0);
    AddThemedSep(G, hwnd, inst, x0, sepW, S(64));
    label(L"Minimum files in a group", S(70));
    m_hDupMinGroup = combo(IDC_DUP_MIN_GROUP, S(70), cw);
    for (int v = 2; v <= 8; ++v) {
        wchar_t b[8]; swprintf_s(b, L"%d", v);
        ::SendMessageW(m_hDupMinGroup, CB_ADDSTRING, 0, (LPARAM)b);
    }
    ::SendMessageW(m_hDupMinGroup, CB_SETCURSEL, 0, 0);
    m_hDupRecursive = check(IDC_DUP_RECURSIVE, L"Include subfolders", S(96), true);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(128));
    m_hDupVisSec = ::CreateWindowExW(0, L"STATIC", L"Visual match (images)", WS_CHILD | WS_VISIBLE, x0, S(132), sepW, S(16), hwnd, nullptr, inst, nullptr);
    G.push_back(m_hDupVisSec);
    label(L"How different allowed (0=strict)", S(156));
    m_hDupMaxHam = combo(IDC_DUP_MAX_HAM, S(156), cw);
    for (int d = 0; d <= 64; ++d) {
        wchar_t b[8]; swprintf_s(b, L"%d", d);
        ::SendMessageW(m_hDupMaxHam, CB_ADDSTRING, 0, (LPARAM)b);
    }
    ::SendMessageW(m_hDupMaxHam, CB_SETCURSEL, 0, 0);
    m_hDupFpSameSize = check(IDC_DUP_FP_SAME_SIZE, L"Only when file size is also the same", S(178), false);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(208));
    m_hDupTextSec = ::CreateWindowExW(0, L"STATIC", L"Text & EXIF (side files)", WS_CHILD | WS_VISIBLE, x0, S(212), sepW, S(16), hwnd, nullptr, inst, nullptr);
    G.push_back(m_hDupTextSec);
    m_hDupUseMd = check(IDC_DUP_USE_MD, L"Include .md text next to the image", S(232), true);
    m_hDupUseJson = check(IDC_DUP_USE_JSON, L"Include .json next to the image", S(260), true);
    m_hDupUseExif = check(IDC_DUP_USE_EXIF, L"Include camera (EXIF) data", S(288), true);
    G.push_back(::CreateWindowExW(0, L"STATIC", L"Optional: extra line mixed into the text+EXIF hash (blank = default).",
        WS_CHILD | WS_VISIBLE, x0, S(312), sepW, S(32), hwnd, nullptr, inst, nullptr));
    m_hDupMetaPrompt = sc::create_single_line_edit(
        {hwnd, inst, IDC_DUP_META_PROMPT, x0, S(346), sepW, rh, L"", 0});
    G.push_back(m_hDupMetaPrompt);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(376));
    m_hDupPairSec = ::CreateWindowExW(0, L"STATIC", L"JSON pair-compare (LLM, not image Meta)", WS_CHILD | WS_VISIBLE, x0, S(380), sepW, S(16), hwnd, nullptr, inst, nullptr);
    G.push_back(m_hDupPairSec);
    m_hDupMetaLlm = check(IDC_DUP_META_LLM, L"LLM: compare .json in pairs", S(402), false);
    label(L"LLM for pairs", S(424));
    m_hDupLlmSource = combo(IDC_DUP_LLM_SOURCE, S(424), cw);
    m_dupLlmProviderKeys.clear();
    m_dupLlmProviderKeys.push_back(std::string{});
    ::SendMessageW(m_hDupLlmSource, CB_ADDSTRING, 0, (LPARAM)L"Default: Chat (pair-compare API)");
    std::string perr;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, perr)) {
        for (const auto& kv : pm) {
            std::wstring wname = pmui::utf8_to_wide(kv.first);
            ::SendMessageW(m_hDupLlmSource, CB_ADDSTRING, 0, (LPARAM)wname.c_str());
            m_dupLlmProviderKeys.push_back(kv.first);
        }
    }
    ::SendMessageW(m_hDupLlmSource, CB_SETCURSEL, 0, 0);
    m_hDupLlmInfo = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        x0, S(446), sepW, S(40), hwnd, (HMENU)(UINT_PTR)IDC_DUP_LLM_INFO, inst, nullptr);
    G.push_back(m_hDupLlmInfo);
    label(L"Min. score 0-10", S(492));
    m_hDupMinSim = combo(IDC_DUP_MIN_SIM, S(492), cw);
    for (int s = 0; s <= 10; ++s) {
        wchar_t b[8]; swprintf_s(b, L"%d", s);
        ::SendMessageW(m_hDupMinSim, CB_ADDSTRING, 0, (LPARAM)b);
    }
    ::SendMessageW(m_hDupMinSim, CB_SETCURSEL, 7, 0);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(520));
    m_hDupGenSec = ::CreateWindowExW(0, L"STATIC", L"Image Meta (missing .json sidecar)", WS_CHILD | WS_VISIBLE, x0, S(524), sepW, S(16), hwnd, nullptr, inst, nullptr);
    G.push_back(m_hDupGenSec);
    m_hDupImplicitMeta = check(IDC_DUP_IMPLICIT_META, L"Create .json with Meta (vision) if missing", S(544), false);
    m_hDupMetaGenHint = ::CreateWindowExW(0, L"STATIC",
        L"Uses the Meta tab provider/keys (images). The LLM list above is only for pair scores.",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        x0, S(566), sepW, S(36), hwnd, nullptr, inst, nullptr);
    G.push_back(m_hDupMetaGenHint);

    InstallDuplicateTooltips();
}

