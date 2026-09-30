#include "stdafx.h"
#include "SettingsPanel.h"
#include "Resource.h"

#include "core/meta.hpp"
#include "helpers/settings_panel_i18n.hpp"
#include "helpers/ui_font.hpp"
#include "ProviderModelRegistry.h"

void CSettingsView::CreateMetaControls(HWND hwnd, HINSTANCE inst, int x0, int lw, int cx, int rh, int sepW, int cw)
{
    namespace sc = pmui::settings_controls;
    auto& G    = m_metaControls;
    const auto& ms = pmui::settings_panel_i18n::meta_strings_for(m_display_language);
    const wchar_t* browse_caption
        = pmui::settings_panel_i18n::browse_button_caption(m_display_language);
    auto S = [&](int v) { return DpiScaleInt(v); };

    auto metaLabelG = [&](LPCWSTR t, int sy) {
        G.push_back(sc::create_field_label({hwnd, inst, x0, sy, lw, t, S(sc::Layout::label_h)}));
    };
    auto metaCheckG = [&](int id, LPCWSTR t, int sy, bool chk = false) -> HWND {
        HWND h = sc::create_autocheck({hwnd, inst, id, x0, sy, sepW, rh, t, chk});
        G.push_back(h);
        return h;
    };
    auto metaComboG = [&](int id, int sy, int w) -> HWND {
        HWND h = sc::create_preset_combo({hwnd, inst, id, cx, sy, w, S(200)});
        G.push_back(h);
        return h;
    };

    AddThemedSep(G, hwnd, inst, x0, sepW, S(6));
    G.push_back(::CreateWindowExW(0, L"STATIC", ms.sec_output,
        WS_CHILD | WS_VISIBLE, x0, S(12), sepW, S(16), hwnd, nullptr, inst, nullptr));
    metaLabelG(ms.lbl_folder, S(32));
    m_hMetaOutDir = sc::create_single_line_edit(
        hwnd, inst, IDC_META_OUT_DIR, cx, S(32), sc::field_width_with_browse(hwnd, cw), rh, L"");
    G.push_back(m_hMetaOutDir);
    m_hMetaBrowse
        = sc::create_browse_button(hwnd, inst, IDC_META_BROWSE, sc::browse_button_x(hwnd, cx, cw), S(32), rh, browse_caption);
    G.push_back(m_hMetaBrowse);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(70));
    G.push_back(::CreateWindowExW(0, L"STATIC", ms.sec_write,
        WS_CHILD | WS_VISIBLE, x0, S(76), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hMetaOutMd = metaCheckG(IDC_META_OUT_MD, ms.out_md, S(96), true);
    m_hMetaOutJson = metaCheckG(IDC_META_OUT_JSON, ms.out_json, S(120), true);
    m_hMetaUpdateExif = metaCheckG(IDC_META_UPDATE_EXIF, ms.update_exif, S(144), false);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(174));
    G.push_back(::CreateWindowExW(0, L"STATIC", ms.sec_preresize,
        WS_CHILD | WS_VISIBLE, x0, S(180), sepW, S(16), hwnd, nullptr, inst, nullptr));
    m_hMetaResizeFirst = metaCheckG(IDC_META_RESIZE_FIRST, ms.chk_resize_first, S(200), true);
    // Indent width row to show it's dependent on the checkbox above
    const int indent = S(18);
    metaLabelG(ms.lbl_width, S(226));
    m_hMetaResizeW = ::CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        cx + indent, S(225), cw - indent, S(200), hwnd, (HMENU)(UINT_PTR)IDC_META_RESIZE_W, inst, nullptr);
    G.push_back(m_hMetaResizeW);
    for (int i = 0; i < 4; ++i) ::SendMessageW(m_hMetaResizeW, CB_ADDSTRING, 0, (LPARAM)ms.width_presets[i]);
    ::SendMessageW(m_hMetaResizeW, CB_SETCURSEL, 1, 0);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(258));
    G.push_back(::CreateWindowExW(0, L"STATIC", ms.sec_provider,
        WS_CHILD | WS_VISIBLE, x0, S(264), sepW, S(16), hwnd, nullptr, inst, nullptr));
    metaLabelG(ms.lbl_provider, S(284));
    m_hMetaProvider = metaComboG(IDC_META_PROVIDER, S(284), cw);
    pmui::provider_models::populate_provider_combo(m_hMetaProvider, "google");
    const int kMetaRefreshW = S(56);
    pmui::ReplicateSelectorRowRenderSpec rep_row{};
    rep_row.parent = hwnd;
    rep_row.inst = inst;
    rep_row.font = pmui::ui_font();
    rep_row.label_x = x0;
    rep_row.label_w = lw;
    rep_row.field_x = cx;
    rep_row.y = S(315);
    rep_row.row_h = rh;
    rep_row.combo_w = cw;
    rep_row.gap = S(6);
    rep_row.refresh_w = kMetaRefreshW;
    rep_row.collection_id = IDC_META_COLLECTION;
    rep_row.refresh_id = IDC_META_REFRESH;
    rep_row.label_text = ms.lbl_collection;
    rep_row.refresh_text = L"\x21BB";
    rep_row.show_refresh = true;
    rep_row.initially_visible = false;
    const auto rep_handles = m_repCtl.render_collection_row(rep_row);
    m_hMetaCollection = rep_handles.h_collection;
    m_hMetaRefresh = rep_handles.h_refresh;
    m_hMetaCollectionLbl = rep_handles.h_label;
    if (m_hMetaCollectionLbl) G.push_back(m_hMetaCollectionLbl);
    if (m_hMetaCollection) G.push_back(m_hMetaCollection);
    if (m_hMetaRefresh) G.push_back(m_hMetaRefresh);
    metaLabelG(ms.lbl_model, S(346));
    m_hMetaModel = metaComboG(IDC_META_MODEL, S(346), cw);
    {
        std::string err;
        (void)pmui::provider_models::populate_model_combo(m_hMetaModel, "google", "", "", "gemini-3-pro-image-preview", err);
    }
    m_repCtl.set_row_visible(rep_handles, false);

    AddThemedSep(G, hwnd, inst, x0, sepW, S(377));
    G.push_back(::CreateWindowExW(0, L"STATIC", ms.sec_prompt,
        WS_CHILD | WS_VISIBLE, x0, S(383), sepW, S(16), hwnd, nullptr, inst, nullptr));
    metaLabelG(ms.lbl_preset, S(403));
    m_hMetaPreset = metaComboG(IDC_META_PRESET, S(403), cw);
    for (int i = 0; i < 4; ++i) ::SendMessageW(m_hMetaPreset, CB_ADDSTRING, 0, (LPARAM)ms.preset[i]);
    ::SendMessageW(m_hMetaPreset, CB_SETCURSEL, 0, 0);
    m_hMetaPrompt = ::CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        x0, S(433), sepW, S(130), hwnd, (HMENU)(UINT_PTR)IDC_META_PROMPT, inst, nullptr);
    G.push_back(m_hMetaPrompt);
    {
        std::wstring def = pmui::utf8_to_wide(media::default_meta_prompt());
        ::SetWindowTextW(m_hMetaPrompt, def.c_str());
    }
}

