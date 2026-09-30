#include "stdafx.h"
#include "SettingsPanel.h"
#include "Resource.h"

#include "helpers/settings_panel_i18n.hpp"
#include "helpers/ui_font.hpp"
#include "ProviderModelRegistry.h"
#include "settings_controls.hpp"

void CSettingsView::CreateFindControls(HWND hwnd, HINSTANCE inst, int x0, int lw, int cx, int rh, int sepW, int cw)
{
    namespace sc = pmui::settings_controls;
    auto& G = m_findControls;
    const auto& fs = pmui::settings_panel_i18n::find_strings_for(m_display_language);
    auto S = [&](int v) { return DpiScaleInt(v); };

    auto findLabelG = [&](LPCWSTR t, int sy) {
        G.push_back(sc::create_field_label({hwnd, inst, x0, sy, lw, t, S(sc::Layout::label_h)}));
    };
    const int chkW = sepW - S(sc::Layout::scrollbar_right_pad) - S(8); ///< Checkbox width clear of scrollbar (+extra safety)
    auto findCheckG = [&](int id, LPCWSTR t, int sy, bool chk = false) -> HWND {
        HWND h = sc::create_autocheck({hwnd, inst, id, x0, sy, chkW, rh, t, chk});
        G.push_back(h);
        return h;
    };
    auto findComboG = [&](int id, int sy, int w) -> HWND {
        HWND h = sc::create_preset_combo({hwnd, inst, id, cx, sy, w, S(200)});
        G.push_back(h);
        return h;
    };

    AddThemedSep(G, hwnd, inst, x0, sepW, S(6));
    G.push_back(::CreateWindowExW(0, L"STATIC", fs.sec_search,
        WS_CHILD | WS_VISIBLE, x0, S(12), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hFindPrompt = ::CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        x0, S(32), sepW, S(70), hwnd, (HMENU)(UINT_PTR)IDC_FIND_PROMPT, inst, nullptr);
    G.push_back(m_hFindPrompt);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(112));
    G.push_back(::CreateWindowExW(0, L"STATIC", fs.sec_mode,
        WS_CHILD | WS_VISIBLE, x0, S(118), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hFindLlm = findCheckG(IDC_FIND_LLM, fs.llm_semantic, S(138), false);
    m_hFindRecursive = findCheckG(IDC_FIND_RECURSIVE, fs.recurse, S(166), true);
    m_hFindMatchFolders = findCheckG(IDC_FIND_MATCH_FOLDERS, fs.match_parents, S(194), true);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(220));
    G.push_back(::CreateWindowExW(0, L"STATIC", fs.sec_llm_cache,
        WS_CHILD | WS_VISIBLE, x0, S(226), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hFindBypassCache = findCheckG(IDC_FIND_BYPASS_CACHE, fs.bypass_cache, S(246), false);
    m_hFindNoGenerate = findCheckG(IDC_FIND_NO_GENERATE, fs.no_generate, S(274), false);
    m_hFindUseMd = findCheckG(IDC_FIND_USE_MD, fs.use_md, S(302), true);
    m_hFindUseJson = findCheckG(IDC_FIND_USE_JSON, fs.use_json, S(330), true);
    m_hFindUseExif = findCheckG(IDC_FIND_USE_EXIF, fs.use_exif, S(358), true);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(384));
    G.push_back(::CreateWindowExW(0, L"STATIC", fs.sec_llm,
        WS_CHILD | WS_VISIBLE, x0, S(390), sepW, S(16), hwnd, nullptr, inst, nullptr));
    findLabelG(L"Provider", S(410));
    m_hFindProvider = findComboG(IDC_FIND_PROVIDER, S(410), cw);
    pmui::provider_models::populate_provider_combo(m_hFindProvider, "replicate");
    {
        pmui::ReplicateSelectorRowRenderSpec rep_row{};
        rep_row.parent = hwnd;
        rep_row.inst = inst;
        rep_row.font = pmui::ui_font();
        rep_row.label_x = x0;
        rep_row.label_w = lw;
        rep_row.field_x = cx;
        rep_row.y = S(441);
        rep_row.row_h = rh;
        rep_row.combo_w = cw;
        rep_row.gap = S(6);
        rep_row.refresh_w = S(56);
        rep_row.collection_id = IDC_FIND_COLLECTION;
        rep_row.refresh_id = IDC_FIND_REFRESH;
        rep_row.label_text = L"Collection";
        rep_row.refresh_text = L"\x21BB";
        rep_row.show_refresh = true;
        rep_row.initially_visible = false;
        const auto rep_handles = m_repCtl.render_collection_row(rep_row);
        m_hFindCollection = rep_handles.h_collection;
        m_hFindRefresh = rep_handles.h_refresh;
        m_hFindCollectionLbl = rep_handles.h_label;
        if (m_hFindCollectionLbl) G.push_back(m_hFindCollectionLbl);
        if (m_hFindCollection) G.push_back(m_hFindCollection);
        if (m_hFindRefresh) G.push_back(m_hFindRefresh);
        m_repCtl.set_row_visible(rep_handles, false);
    }
    findLabelG(fs.lbl_model, S(472));
    m_hFindModel = findComboG(IDC_FIND_MODEL, S(472), cw);
    // Replicate: populated with collections in ApplyDeferredCommandProviderOverrides / OnFindProviderChanged.
    m_hFindResizeFirst = findCheckG(IDC_FIND_RESIZE_FIRST, fs.resize_first, S(503), true);
    // Indent resize width row to show it's dependent on the checkbox above
    const int indent = S(18);
    findLabelG(fs.lbl_resize, S(535));
    m_hFindResizeW = ::CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        cx + indent, S(534), cw - indent, S(200), hwnd, (HMENU)(UINT_PTR)IDC_FIND_RESIZE_W, inst, nullptr);
    G.push_back(m_hFindResizeW);
    for (int i = 0; i < 4; ++i) ::SendMessageW(m_hFindResizeW, CB_ADDSTRING, 0, (LPARAM)fs.resize_width[i]);
    ::SendMessageW(m_hFindResizeW, CB_SETCURSEL, 1, 0);
    findLabelG(fs.lbl_max, S(570));
    m_hFindMax = sc::create_single_line_edit(
        {hwnd, inst, IDC_FIND_MAX, cx, S(570), cw, rh, L"0", ES_NUMBER});
    G.push_back(m_hFindMax);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(605));
    G.push_back(::CreateWindowExW(0, L"STATIC", fs.sec_ref,
        WS_CHILD | WS_VISIBLE, x0, S(611), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hFindRefList = ::CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_EXTENDEDSEL,
        x0, S(631), sepW, S(70), hwnd, (HMENU)(UINT_PTR)IDC_FIND_REF_LIST, inst, nullptr);
    G.push_back(m_hFindRefList);
    m_hFindRefAdd = ::CreateWindowExW(0, L"BUTTON", fs.btn_add_ref,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x0, S(706), S(160), rh, hwnd, (HMENU)(UINT_PTR)IDC_FIND_REF_ADD, inst, nullptr);
    G.push_back(m_hFindRefAdd);
    m_hFindRefClear = ::CreateWindowExW(0, L"BUTTON", fs.btn_clear,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x0 + S(166), S(706), S(84), rh, hwnd, (HMENU)(UINT_PTR)IDC_FIND_REF_CLEAR, inst, nullptr);
    G.push_back(m_hFindRefClear);
}

