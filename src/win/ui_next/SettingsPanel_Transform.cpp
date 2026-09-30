#include "stdafx.h"
#include "SettingsPanel.h"
#include "Resource.h"

#include "helpers/settings_panel_i18n.hpp"
#include "helpers/ui_font.hpp"
#include "ProviderModelRegistry.h"
#include "settings_controls.hpp"

void CSettingsView::CreateTransformControls(HWND hwnd, HINSTANCE inst, int x0, int lw, int cx, int rh, int dy, int sepW, int cw)
{
    namespace sc = pmui::settings_controls;
    auto& G = m_transformControls;
    const auto& trs = pmui::settings_panel_i18n::transform_strings_for(m_display_language);
    const auto& ms = pmui::settings_panel_i18n::meta_strings_for(m_display_language);
    auto S = [&](int v) { return DpiScaleInt(v); };

    const int tfAfterSep  = S(sc::Layout::after_sep_pad);
    const int tfAfterHdr  = S(sc::Layout::after_header_pad);
    const int tfPromptH   = S(76);
    const int tfRefListH  = S(48);
    const int tfBeforeBtn = S(10);
    const int tfBtnH      = S(26);

    auto tfLabelG = [&](LPCWSTR t, int rowY) {
        G.push_back(sc::create_field_label(
            {hwnd, inst, x0, rowY, lw, t, S(sc::Layout::label_h)}));
    };
    auto tfComboG = [&](int id, int rowY, int w, int dropH) -> HWND {
        HWND h = sc::create_preset_combo({hwnd, inst, id, cx, rowY, w, S(dropH)});
        G.push_back(h);
        return h;
    };
    const int chkW = sepW - S(sc::Layout::scrollbar_right_pad) - S(8); ///< Checkbox width clear of scrollbar
    auto tfCheckG = [&](int id, LPCWSTR t, int rowY, bool initial_chk) -> HWND {
        HWND h = sc::create_autocheck({hwnd, inst, id, x0, rowY, chkW, rh, t, initial_chk});
        G.push_back(h);
        return h;
    };

    int ty = S(6);
    AddThemedSep(G, hwnd, inst, x0, sepW, ty);
    ty += tfAfterSep;
    G.push_back(::CreateWindowExW(0, L"STATIC", trs.sec_model,
        WS_CHILD | WS_VISIBLE, x0, ty, sepW, S(16), hwnd, nullptr, inst, nullptr));
    ty += tfAfterHdr;
    tfLabelG(trs.lbl_provider, ty);
    m_hTfProvider = tfComboG(IDC_TF_PROVIDER, ty, cw, 220);
    pmui::provider_models::populate_provider_combo(m_hTfProvider, "replicate");
    ty += dy;
    {
        pmui::ReplicateSelectorRowRenderSpec rep_row{};
        rep_row.parent = hwnd;
        rep_row.inst = inst;
        rep_row.font = pmui::ui_font();
        rep_row.label_x = x0;
        rep_row.label_w = lw;
        rep_row.field_x = cx;
        rep_row.y = ty;
        rep_row.row_h = tfBtnH;
        rep_row.combo_w = cw;
        rep_row.gap = S(6);
        rep_row.refresh_w = S(56);
        rep_row.collection_id = IDC_TF_COLLECTION;
        rep_row.refresh_id = IDC_TF_REFRESH;
        rep_row.label_text = trs.lbl_collection;
        rep_row.refresh_text = L"\x21BB";
        rep_row.show_refresh = true;
        rep_row.initially_visible = false;
        const auto rep_handles = m_repCtl.render_collection_row(rep_row);
        m_hTfCollection = rep_handles.h_collection;
        m_hTfRefresh = rep_handles.h_refresh;
        m_hTfCollectionLbl = rep_handles.h_label;
        if (m_hTfCollectionLbl) G.push_back(m_hTfCollectionLbl);
        if (m_hTfCollection) G.push_back(m_hTfCollection);
        if (m_hTfRefresh) G.push_back(m_hTfRefresh);
        m_repCtl.set_row_visible(rep_handles, false);
    }
    ty += dy;
    tfLabelG(trs.lbl_model, ty);
    m_hTfModel = tfComboG(IDC_COMBO_AI_MODEL, ty, cw, 300);
    // Replicate model list: filled in ApplyDeferredCommandProviderOverrides / OnTfProviderChanged — not
    // here (would duplicate work with that path and add seconds to NewDocker+AddDockedChild(settings)).
    ty += dy;

    AddThemedSep(G, hwnd, inst, x0, sepW, ty);
    ty += tfAfterSep;
    G.push_back(::CreateWindowExW(0, L"STATIC", trs.sec_output,
        WS_CHILD | WS_VISIBLE, x0, ty, sepW, S(16), hwnd, nullptr, inst, nullptr));
    ty += tfAfterHdr;
    tfLabelG(trs.lbl_aspect, ty);
    m_hTfAspect = tfComboG(IDC_COMBO_AI_ASPECT, ty, cw, 220);
    for (int i = 0; i < 6; ++i) ::SendMessageW(m_hTfAspect, CB_ADDSTRING, 0, (LPARAM)trs.aspect[i]);
    ::SendMessageW(m_hTfAspect, CB_SETCURSEL, 0, 0);
    ty += dy;
    tfLabelG(trs.lbl_size, ty);
    m_hTfSize = tfComboG(IDC_COMBO_AI_SIZE, ty, cw, 200);
    for (int i = 0; i < 5; ++i) ::SendMessageW(m_hTfSize, CB_ADDSTRING, 0, (LPARAM)trs.size[i]);
    ::SendMessageW(m_hTfSize, CB_SETCURSEL, 0, 0);
    ty += dy;

    AddThemedSep(G, hwnd, inst, x0, sepW, ty);
    ty += tfAfterSep;
    G.push_back(::CreateWindowExW(0, L"STATIC", ms.sec_preresize,
        WS_CHILD | WS_VISIBLE, x0, ty, sepW, S(16), hwnd, nullptr, inst, nullptr));
    ty += tfAfterHdr;
    m_hTfPreresizeFirst = tfCheckG(IDC_TF_RESIZE_FIRST, ms.chk_resize_first, ty, false);
    ty += dy;
    // Indent width row to show it's dependent on the checkbox above
    const int indent = S(18);
    tfLabelG(ms.lbl_width, ty);
    // Move combo to account for indented label
    m_hTfPreresizeW = ::CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        cx + indent, ty - S(1), cw - indent, S(200), hwnd, (HMENU)(UINT_PTR)IDC_TF_RESIZE_W, inst, nullptr);
    G.push_back(m_hTfPreresizeW);
    for (int i = 0; i < 4; ++i) ::SendMessageW(m_hTfPreresizeW, CB_ADDSTRING, 0, (LPARAM)ms.width_presets[i]);
    ::SendMessageW(m_hTfPreresizeW, CB_SETCURSEL, 1, 0);
    ty += dy;
    m_hTfPreresizeRawOnly = tfCheckG(IDC_TF_RESIZE_RAW, trs.chk_preresize_raw_only, ty, true);
    ty += dy;

    AddThemedSep(G, hwnd, inst, x0, sepW, ty);
    ty += tfAfterSep;
    G.push_back(::CreateWindowExW(0, L"STATIC", trs.sec_prompt,
        WS_CHILD | WS_VISIBLE, x0, ty, sepW, S(16), hwnd, nullptr, inst, nullptr));
    ty += tfAfterHdr;
    m_hTfPrompt = ::CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        x0, ty, sepW, tfPromptH, hwnd, (HMENU)(UINT_PTR)IDC_EDIT_PROMPT, inst, nullptr);
    G.push_back(m_hTfPrompt);
    ty += tfPromptH + tfBeforeBtn;
    {
        // Presets button only - full width centered
        const int wPresets = (std::min)(S(132), sepW);
        const int xPresets = x0 + (sepW - wPresets) / 2;
        m_hTfPresets = ::CreateWindowExW(0, L"BUTTON", trs.btn_presets,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            xPresets, ty, wPresets, tfBtnH, hwnd, (HMENU)(UINT_PTR)IDC_CMD_PRESETS, inst, nullptr);
        G.push_back(m_hTfPresets);
    }
    ty += tfBtnH + S(8);

    AddThemedSep(G, hwnd, inst, x0, sepW, ty);
    ty += tfAfterSep;
    G.push_back(::CreateWindowExW(0, L"STATIC", trs.sec_ref,
        WS_CHILD | WS_VISIBLE, x0, ty, sepW, S(16), hwnd, nullptr, inst, nullptr));
    ty += tfAfterHdr;
    m_hTfRefList = ::CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_DISABLENOSCROLL,
        x0, ty, sepW, tfRefListH, hwnd, (HMENU)(UINT_PTR)IDC_TF_REF_LIST, inst, nullptr);
    G.push_back(m_hTfRefList);
    ty += tfRefListH + tfBeforeBtn;
    {
        const int g     = S(8);
        const int wCol  = (sepW - g) / 2;
        const int wRest = sepW - wCol - g;
        m_hTfRefAdd = ::CreateWindowExW(0, L"BUTTON", trs.btn_add_ref,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x0, ty, wCol, tfBtnH, hwnd, (HMENU)(UINT_PTR)IDC_TF_REF_ADD, inst, nullptr);
        G.push_back(m_hTfRefAdd);
        m_hTfRefClear = ::CreateWindowExW(0, L"BUTTON", trs.btn_clear,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x0 + wCol + g, ty, wRest, tfBtnH, hwnd, (HMENU)(UINT_PTR)IDC_TF_REF_CLEAR, inst, nullptr);
        G.push_back(m_hTfRefClear);
    }
}

