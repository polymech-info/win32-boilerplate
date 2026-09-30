#include "stdafx.h"
#include "SettingsPanel.h"
#include "Resource.h"
#include "core/meta.hpp"
#include <commctrl.h> // TBM_*, etc.
#include <shlobj.h>
#include <algorithm>
#include <thread>
#pragma comment(lib, "comctl32.lib")

#include "settings_panel_helpers.hpp"
#include "helpers/settings_panel_i18n.hpp"
#include "win/settings_store.hpp"
#include "ProviderModelRegistry.h"
#include <cstring>
#include <nlohmann/json.hpp>

using pmui::wide_to_utf8;
using pmui::utf8_to_wide;
using settings_panel_internals::add_settings_tooltip;
using settings_panel_internals::get_edit_int;
using settings_panel_internals::get_edit_text;
using settings_panel_internals::kKernelCount;
using settings_panel_internals::kKernelValues;
using nlohmann::json;

namespace {
constexpr const char* kCommandProviderOverridesKey = "command_provider_overrides";

void select_provider(HWND h_provider, const std::string& provider_id) {
    if (!h_provider) return;
    pmui::provider_models::populate_provider_combo(h_provider, provider_id);
}

void select_model(HWND h_model, const std::string& provider_id, const std::string& model_id) {
    if (!h_model || model_id.empty()) return;
    std::wstring target = utf8_to_wide(model_id);
    if (provider_id == "replicate") {
        const std::string grouped = std::string(1, (char)std::tolower((unsigned char)model_id[0])) + "/" + model_id;
        target = utf8_to_wide(grouped);
    }
    if (::SendMessageW(h_model, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)target.c_str()) != CB_ERR) return;
    const int n = (int)::SendMessageW(h_model, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; ++i) {
        const int len = (int)::SendMessageW(h_model, CB_GETLBTEXTLEN, (WPARAM)i, 0);
        if (len <= 0) continue;
        std::wstring buf((size_t)len + 1, L'\0');
        ::SendMessageW(h_model, CB_GETLBTEXT, (WPARAM)i, (LPARAM)buf.data());
        buf.resize((size_t)len);
        const std::string text = pmui::wide_to_utf8(buf);
        if (text == model_id || (provider_id == "replicate" && text.size() > model_id.size() && text.rfind("/" + model_id) != std::string::npos)) {
            ::SendMessageW(h_model, CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }
}

struct CmdOverrideModelSlugs {
    std::string tf;
    std::string meta;
    std::string find;
};

static CmdOverrideModelSlugs read_cmd_override_model_slugs()
{
    CmdOverrideModelSlugs out;
    std::string           err;
    json                  j;
    if (!media::settings::load_subtree(kCommandProviderOverridesKey, j, err) || !j.is_object())
        return out;
    if (j.contains("transform") && j["transform"].is_object())
        out.tf = j["transform"].value("model", std::string{});
    if (j.contains("meta") && j["meta"].is_object())
        out.meta = j["meta"].value("model", std::string{});
    if (j.contains("find") && j["find"].is_object())
        out.find = j["find"].value("model", std::string{});
    return out;
}

static void settings_repinit_worker(HWND                              hwnd,
                                    std::shared_ptr<std::atomic<bool>> cancel,
                                    bool                              want_tf,
                                    bool                              want_meta,
                                    bool                              want_find,
                                    std::string                       keep_tf,
                                    std::string                       keep_meta,
                                    std::string                       keep_find,
                                    pmui::ReplicateSelectorState      snap_tf,
                                    pmui::ReplicateSelectorState      snap_meta,
                                    pmui::ReplicateSelectorState      snap_find,
                                    std::string                       api_key,
                                    std::string                       base_url) {
    pmui::ReplicateSelectorController ctl;
    auto*                             p = new SettingsRepinitPayload{};
    std::string                       err;
    if (want_tf) {
        p->tf   = std::move(snap_tf);
        p->ok_tf = ctl.reload_fetch_state(api_key, base_url, p->tf, keep_tf, true, false, err);
        if (cancel->load()) {
            delete p;
            return;
        }
    }
    if (want_meta) {
        p->meta    = std::move(snap_meta);
        p->ok_meta = ctl.reload_fetch_state(api_key, base_url, p->meta, keep_meta, true, false, err);
        if (cancel->load()) {
            delete p;
            return;
        }
    }
    if (want_find) {
        p->find    = std::move(snap_find);
        p->ok_find = ctl.reload_fetch_state(api_key, base_url, p->find, keep_find, true, false, err);
        if (cancel->load()) {
            delete p;
            return;
        }
    }
    if (!::IsWindow(hwnd)) {
        delete p;
        return;
    }
    (void)::PostMessageW(hwnd, UWM_SETTINGS_REPLICATE_INIT_DONE, 0, reinterpret_cast<LPARAM>(p));
}
} // namespace

// ── ReadOptions / WriteOptions ────────────────────────────────────────────────

void CSettingsView::ReadOptions(media::ResizeOptions& opt, std::string& out_dir) const
{
    // Output destination
    int preset = (int)::SendMessageW(m_hPreset, CB_GETCURSEL, 0, 0);
    opt.output_stem_suffix.clear();
    if (preset == 1) opt.output_stem_suffix = "_resized";

    out_dir.clear();
    std::wstring dir = get_edit_text(m_hOutDir);
    if (preset == 2 || !dir.empty())
        out_dir = wide_to_utf8(dir);

    // Dimensions
    opt.max_width  = std::max(0, get_edit_int(m_hMaxW));
    opt.max_height = std::max(0, get_edit_int(m_hMaxH));

    // Fit
    int fi = (int)::SendMessageW(m_hFit, CB_GETCURSEL, 0, 0);
    const char* fitNames[] = { "inside", "cover", "contain", "fill", "outside" };
    if (fi >= 0 && fi < 5) opt.fit = fitNames[fi];

    // Quality (read from trackbar)
    int q = (int)::SendMessageW(m_hSliderQuality, TBM_GETPOS, 0, 0);
    opt.quality = std::clamp(q, 1, 100);

    // Kernel
    int ki = (int)::SendMessageW(m_hKernel, CB_GETCURSEL, 0, 0);
    if (ki >= 0 && ki < kKernelCount)
        opt.kernel = kKernelValues[ki];

    // Output format
    static const char* kFormats[] = { "", "jpg", "png", "webp", "tiff", "avif" };
    int fmti = (int)::SendMessageW(m_hFormat, CB_GETCURSEL, 0, 0);
    opt.format = (fmti > 0 && fmti < 6) ? kFormats[fmti] : "";

    // Options
    opt.autorotate        = ::SendMessageW(m_hAutorot, BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt.without_enlargement = ::SendMessageW(m_hEnlarge, BM_GETCHECK, 0, 0) != BST_CHECKED;
    opt.strip_metadata    = ::SendMessageW(m_hStrip,   BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void CSettingsView::WriteOptions(const media::ResizeOptions& opt, const std::string& out_dir)
{
    // Output destination
    int preset = 0;
    if (!out_dir.empty()) preset = 2;
    else if (opt.output_stem_suffix == "_resized") preset = 1;
    ::SendMessageW(m_hPreset, CB_SETCURSEL, preset, 0);
    ::SetWindowTextW(m_hOutDir, utf8_to_wide(out_dir).c_str());

    // Dimensions
    wchar_t buf[16]{};
    swprintf_s(buf, L"%d", opt.max_width);  ::SetWindowTextW(m_hMaxW, buf);
    swprintf_s(buf, L"%d", opt.max_height); ::SetWindowTextW(m_hMaxH, buf);

    // Fit
    const char* fitNames[] = { "inside", "cover", "contain", "fill", "outside" };
    int fi = 0;
    for (int i = 0; i < 5; ++i)
        if (opt.fit == fitNames[i]) { fi = i; break; }
    ::SendMessageW(m_hFit, CB_SETCURSEL, fi, 0);

    // Quality slider
    int q = std::clamp(opt.quality, 1, 100);
    ::SendMessageW(m_hSliderQuality, TBM_SETPOS, TRUE, q);
    swprintf_s(buf, L"%d", q); ::SetWindowTextW(m_hLblQuality, buf);

    // Kernel
    for (int i = 0; i < kKernelCount; ++i)
        if (opt.kernel == kKernelValues[i]) { ::SendMessageW(m_hKernel, CB_SETCURSEL, i, 0); break; }

    // Format
    {
        static const char* kFormats[] = { "", "jpg", "png", "webp", "tiff", "avif" };
        int fi2 = 0;
        for (int i = 1; i < 6; ++i)
            if (opt.format == kFormats[i]) { fi2 = i; break; }
        ::SendMessageW(m_hFormat, CB_SETCURSEL, fi2, 0);
    }

    // Options
    ::SendMessageW(m_hAutorot, BM_SETCHECK, opt.autorotate ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessageW(m_hEnlarge, BM_SETCHECK, opt.without_enlargement ? BST_UNCHECKED : BST_CHECKED, 0);
    ::SendMessageW(m_hStrip,   BM_SETCHECK, opt.strip_metadata ? BST_CHECKED : BST_UNCHECKED, 0);
}

void CSettingsView::OnCmpBrowse()
{
    BROWSEINFOW bi{};
    bi.hwndOwner = GetHwnd();
    bi.lpszTitle = pmui::settings_panel_i18n::compress_strings_for(m_display_language).browse_folder_title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    wchar_t dn[MAX_PATH]{};
    bi.pszDisplayName = dn;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH * 4]{};
    if (SHGetPathFromIDListW(pidl, path))
        ::SetWindowTextW(m_hCmpDir, path);
    CoTaskMemFree(pidl);
}

void CSettingsView::ReadCompressSettings(CompressSettings& out) const
{
    out = {};

    // Output
    out.output_preset = std::max(0, (int)::SendMessageW(m_hCmpDest, CB_GETCURSEL, 0, 0));
    out.out_dir       = wide_to_utf8(get_edit_text(m_hCmpDir));

    // Compressor
    out.use_mozjpeg = m_hCmpFormat &&
                      ::SendMessageW(m_hCmpFormat, CB_GETCURSEL, 0, 0) == 1;

    // MozJPEG
    out.jpeg_quality     = m_hCmpJpegSlider ?
        std::clamp((int)::SendMessageW(m_hCmpJpegSlider, TBM_GETPOS, 0, 0), 1, 100) : 85;
    out.jpeg_progressive = m_hCmpProgressive &&
        ::SendMessageW(m_hCmpProgressive, BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.jpeg_trellis     = m_hCmpTrellis &&
        ::SendMessageW(m_hCmpTrellis,     BM_GETCHECK, 0, 0) == BST_CHECKED;

    // PNG
    out.level = m_hCmpLevel ?
        std::clamp((int)::SendMessageW(m_hCmpLevel, TBM_GETPOS, 0, 0), 1, 9) : 9;

#ifdef FEATURE_PNG_COMPRESSOR
    out.quantize = m_hCmpQuantize &&
                   ::SendMessageW(m_hCmpQuantize, BM_GETCHECK, 0, 0) == BST_CHECKED;
    static const int kColorVals[] = { 256, 128, 64, 32 };
    int ci = m_hCmpColors ? (int)::SendMessageW(m_hCmpColors, CB_GETCURSEL, 0, 0) : 0;
    out.colors    = (ci >= 0 && ci < 4) ? kColorVals[ci] : 256;
    out.q_quality = m_hCmpQualSlider ?
        std::clamp((int)::SendMessageW(m_hCmpQualSlider, TBM_GETPOS, 0, 0), 60, 100) : 85;
#endif

#ifdef FEATURE_PNG_ZOPFLI
    out.zopfli = m_hCmpZopfli &&
                 ::SendMessageW(m_hCmpZopfli, BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.zopfli_iter = 15;
#endif

    // Common
    out.strip_metadata = m_hCmpStrip &&
                         ::SendMessageW(m_hCmpStrip, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

// ── Transform panel accessors ────────────────────────────────────────────────

void CSettingsView::ReadTransformSettings(TransformSettings& out) const
{
    out = {};
    out.provider = pmui::provider_models::selected_provider_id(m_hTfProvider);
    out.model = (out.provider == "replicate")
        ? m_repCtl.selected_model_slug(m_hTfModel)
        : pmui::provider_models::combo_text(m_hTfModel);

    static const char* kAspects[] = { "", "1:1", "3:2", "4:3", "16:9", "9:16" };
    int ai = m_hTfAspect ? (int)::SendMessageW(m_hTfAspect, CB_GETCURSEL, 0, 0) : 0;
    out.aspect_ratio = (ai >= 0 && ai < 6) ? kAspects[ai] : "";

    static const char* kSizes[] = { "", "512", "1K", "2K", "4K" };
    int si = m_hTfSize ? (int)::SendMessageW(m_hTfSize, CB_GETCURSEL, 0, 0) : 0;
    out.image_size = (si >= 0 && si < 5) ? kSizes[si] : "";

    out.prompt = wide_to_utf8(get_edit_text(m_hTfPrompt));

    // Reference images — read each entry from the listbox.
    out.reference_images.clear();
    if (m_hTfRefList) {
        const int n = (int)::SendMessageW(m_hTfRefList, LB_GETCOUNT, 0, 0);
        for (int i = 0; i < n; ++i) {
            const int len = (int)::SendMessageW(m_hTfRefList, LB_GETTEXTLEN, i, 0);
            if (len <= 0) continue;
            std::wstring buf(len + 1, L'\0');
            ::SendMessageW(m_hTfRefList, LB_GETTEXT, i, (LPARAM)buf.data());
            buf.resize(len);
            out.reference_images.push_back(wide_to_utf8(buf));
        }
    }
    out.preresize_in_memory.enabled = m_hTfPreresizeFirst
        && ::SendMessageW(m_hTfPreresizeFirst, BM_GETCHECK, 0, 0) == BST_CHECKED;
    {
        const int wi = m_hTfPreresizeW ? (int)::SendMessageW(m_hTfPreresizeW, CB_GETCURSEL, 0, 0) : 1;
        out.preresize_in_memory.width
            = settings_panel_internals::preresize_width_from_cb_sel(wi);
    }
    out.preresize_in_memory.raw_only = m_hTfPreresizeRawOnly
        && ::SendMessageW(m_hTfPreresizeRawOnly, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void CSettingsView::OnTfRefAdd()
{
    if (!m_hTfRefList) return;
    OPENFILENAMEW ofn{};
    wchar_t buf[MAX_PATH * 16] = {};   // multi-select buffer
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetHwnd();
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.gif\0All\0*.*\0";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!::GetOpenFileNameW(&ofn)) return;

    // Multi-select fills buf as: dir\0 file1\0 file2\0 ... \0\0
    // Single-select fills it as: full_path\0\0
    std::wstring first(buf);
    wchar_t* p = buf + first.size() + 1;
    if (*p == L'\0') {
        ::SendMessageW(m_hTfRefList, LB_ADDSTRING, 0, (LPARAM)first.c_str());
    } else {
        while (*p) {
            std::wstring full = first + L"\\" + p;
            ::SendMessageW(m_hTfRefList, LB_ADDSTRING, 0, (LPARAM)full.c_str());
            p += wcslen(p) + 1;
        }
    }
}

void CSettingsView::OnTfRefClear()
{
    if (m_hTfRefList) ::SendMessageW(m_hTfRefList, LB_RESETCONTENT, 0, 0);
}

void CSettingsView::SyncReplicateRowVisibilityForMode()
{
    if (m_hTfProvider) {
        const std::string p  = pmui::provider_models::selected_provider_id(m_hTfProvider);
        const bool        show = (p == "replicate") && (m_mode == MODE_TRANSFORM);
        m_repCtl.set_row_visible({m_hTfCollection, m_hTfRefresh, m_hTfCollectionLbl}, show);
    }
    if (m_hMetaProvider) {
        const std::string p  = pmui::provider_models::selected_provider_id(m_hMetaProvider);
        const bool        show = (p == "replicate") && (m_mode == MODE_META);
        m_repCtl.set_row_visible({m_hMetaCollection, m_hMetaRefresh, m_hMetaCollectionLbl}, show);
    }
    if (m_hFindProvider) {
        const std::string p  = pmui::provider_models::selected_provider_id(m_hFindProvider);
        const bool        show = (p == "replicate") && (m_mode == MODE_FIND);
        m_repCtl.set_row_visible({m_hFindCollection, m_hFindRefresh, m_hFindCollectionLbl}, show);
    }
}

void CSettingsView::OnTfProviderChanged()
{
    if (!m_hTfProvider || !m_hTfModel) return;
    const std::string provider = pmui::provider_models::selected_provider_id(m_hTfProvider);
    const bool is_replicate = provider == "replicate";
    m_repCtl.set_row_visible(
        {m_hTfCollection, m_hTfRefresh, m_hTfCollectionLbl}, is_replicate && (m_mode == MODE_TRANSFORM));
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find(provider);
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    if (is_replicate) {
        if (m_deferReplicateNetworkInit) return;
        const std::string keep = m_repCtl.selected_model_slug(m_hTfModel);
        std::wstring rep_err;
        if (m_repCtl.reload(api_key, base_url, m_hTfCollection, m_hTfModel,
                            m_tfReplicateState, keep, true, false, &rep_err)) {
            m_repCtl.update_meta_text(nullptr, m_tfReplicateState,
                                      m_repCtl.selected_model_slug(m_hTfModel));
        }
    } else {
        std::string pop_err;
        if (!pmui::provider_models::populate_model_combo(
                m_hTfModel, provider, api_key, base_url, "", pop_err)) {
            std::string ignore;
            (void)pmui::provider_models::populate_model_combo(
                m_hTfModel, provider, "", "", "", ignore);
        }
    }
}

void CSettingsView::OnTfCollectionChanged()
{
    if (!m_hTfProvider || !m_hTfModel || !m_hTfCollection) return;
    if (pmui::provider_models::selected_provider_id(m_hTfProvider) != "replicate") return;
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find("replicate");
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    if (!m_repCtl.apply_collection_from_combo(m_hTfCollection, m_tfReplicateState)) return;
    const std::string keep = m_repCtl.selected_model_slug(m_hTfModel);
    std::wstring rep_err;
    if (m_repCtl.reload(api_key, base_url, m_hTfCollection, m_hTfModel,
                        m_tfReplicateState, keep, false, false, &rep_err)) {
        m_repCtl.update_meta_text(nullptr, m_tfReplicateState,
                                  m_repCtl.selected_model_slug(m_hTfModel));
    }
}

void CSettingsView::OnTfRefreshReplicateModels()
{
    if (!m_hTfProvider || pmui::provider_models::selected_provider_id(m_hTfProvider) != "replicate") return;
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find("replicate");
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    const std::string keep = m_repCtl.selected_model_slug(m_hTfModel);
    std::wstring rep_err;
    if (m_repCtl.reload(api_key, base_url, m_hTfCollection, m_hTfModel,
                        m_tfReplicateState, keep, true, true, &rep_err)) {
        m_repCtl.update_meta_text(nullptr, m_tfReplicateState,
                                  m_repCtl.selected_model_slug(m_hTfModel));
    }
}

void CSettingsView::OnFindRefAdd()
{
    if (!m_hFindRefList) return;
    OPENFILENAMEW ofn{};
    wchar_t buf[MAX_PATH * 16] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetHwnd();
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.webp;*.bmp;*.gif\0All\0*.*\0";
    ofn.lpstrTitle  = L"Pick reference image(s) for the LLM judge";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!::GetOpenFileNameW(&ofn)) return;

    std::wstring first(buf);
    wchar_t* p = buf + first.size() + 1;
    if (*p == L'\0') {
        ::SendMessageW(m_hFindRefList, LB_ADDSTRING, 0, (LPARAM)first.c_str());
    } else {
        while (*p) {
            std::wstring full = first + L"\\" + p;
            ::SendMessageW(m_hFindRefList, LB_ADDSTRING, 0, (LPARAM)full.c_str());
            p += wcslen(p) + 1;
        }
    }
}

void CSettingsView::OnFindRefClear()
{
    if (m_hFindRefList) ::SendMessageW(m_hFindRefList, LB_RESETCONTENT, 0, 0);
}

void CSettingsView::SetTransformPrompt(const std::string& utf8_text)
{
    if (m_hTfPrompt) ::SetWindowTextW(m_hTfPrompt, utf8_to_wide(utf8_text).c_str());
}

std::string CSettingsView::GetTransformPrompt() const
{
    return wide_to_utf8(get_edit_text(m_hTfPrompt));
}

// ── Meta panel handlers ──────────────────────────────────────────────────────

void CSettingsView::OnMetaBrowse()
{
    BROWSEINFOW bi{};
    bi.hwndOwner = GetHwnd();
    bi.lpszTitle = pmui::settings_panel_i18n::meta_strings_for(m_display_language).browse_folder_title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    wchar_t dn[MAX_PATH]{};
    bi.pszDisplayName = dn;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH * 4]{};
    if (SHGetPathFromIDListW(pidl, path))
        ::SetWindowTextW(m_hMetaOutDir, path);
    CoTaskMemFree(pidl);
}

void CSettingsView::OnMetaPresetChanged()
{
    int sel = (int)::SendMessageW(m_hMetaPreset, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel == 3) return;   // 3 = (custom): keep whatever the user typed
    std::string p;
    switch (sel) {
    case 0: // Default cataloguer
        p = media::default_meta_prompt();
        break;
    case 1: // Ecommerce product
        p = "You are an ecommerce product photographer's assistant. Inspect the image and reply "
            "with a single JSON object using these keys: title (<=80 chars, product-led), "
            "alt (<=125 chars, SEO friendly), description (2 sentences focused on materials, "
            "colour, key features), scene (\"studio\", \"in-context\", etc.), "
            "estimated_location (\"unknown\" unless obvious), characters (rare for products, "
            "usually []), objects (array of {type,count} for the main product + accessories), "
            "tags (5-12 lowercase keywords for marketplace search: brand, material, colour, use), "
            "confidence (0-1). Be factual, never invent specs, no text outside the JSON.";
        break;
    case 2: // Photo journal
        p = "You are a photo journal cataloguer. Reply with a single JSON object using these keys: "
            "title (short evocative title), alt (<=125 chars factual), description (3-4 sentences "
            "describing scene, light, mood, key elements), scene (one short phrase), "
            "estimated_location (best guess from architecture/landscape/text or \"unknown\"), "
            "characters (array of {type,count,notes}), objects (array of {type,count} for "
            "prominent objects), tags (5-12 lowercase keywords), confidence (0-1). "
            "Plain JSON only, no prose.";
        break;
    }
    ::SetWindowTextW(m_hMetaPrompt, utf8_to_wide(p).c_str());
}

void CSettingsView::OnMetaProviderChanged()
{
    if (!m_hMetaProvider || !m_hMetaModel) return;
    const std::string provider = pmui::provider_models::selected_provider_id(m_hMetaProvider);
    const bool is_replicate = provider == "replicate";
    m_repCtl.set_row_visible(
        {m_hMetaCollection, m_hMetaRefresh, m_hMetaCollectionLbl}, is_replicate && (m_mode == MODE_META));
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find(provider);
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    if (is_replicate) {
        if (m_deferReplicateNetworkInit) return;
        std::wstring rep_err;
        const std::string keep = pmui::provider_models::combo_text(m_hMetaModel);
        if (m_repCtl.reload(api_key, base_url, m_hMetaCollection, m_hMetaModel,
                            m_metaReplicateState, keep, true, false, &rep_err)) {
            m_repCtl.update_meta_text(nullptr, m_metaReplicateState,
                                      m_repCtl.selected_model_slug(m_hMetaModel));
        }
        return;
    }
    std::string pop_err;
    if (!pmui::provider_models::populate_model_combo(
            m_hMetaModel, provider, api_key, base_url, "", pop_err)) {
        // fall back to provider-only defaults
        std::string ignore;
        (void)pmui::provider_models::populate_model_combo(
            m_hMetaModel, provider, "", "", "", ignore);
    }
}

void CSettingsView::OnMetaCollectionChanged()
{
    if (!m_hMetaProvider || !m_hMetaModel || !m_hMetaCollection) return;
    if (pmui::provider_models::selected_provider_id(m_hMetaProvider) != "replicate") return;
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find("replicate");
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    if (!m_repCtl.apply_collection_from_combo(m_hMetaCollection, m_metaReplicateState)) return;
    const std::string keep = m_repCtl.selected_model_slug(m_hMetaModel);
    std::wstring rep_err;
    if (m_repCtl.reload(api_key, base_url, m_hMetaCollection, m_hMetaModel,
                        m_metaReplicateState, keep, false, false, &rep_err)) {
        m_repCtl.update_meta_text(nullptr, m_metaReplicateState,
                                  m_repCtl.selected_model_slug(m_hMetaModel));
    }
}

void CSettingsView::OnMetaRefreshReplicateModels()
{
    if (!m_hMetaProvider || pmui::provider_models::selected_provider_id(m_hMetaProvider) != "replicate") return;
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find("replicate");
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    const std::string keep = m_repCtl.selected_model_slug(m_hMetaModel);
    std::wstring rep_err;
    if (m_repCtl.reload(api_key, base_url, m_hMetaCollection, m_hMetaModel,
                        m_metaReplicateState, keep, true, true, &rep_err)) {
        m_repCtl.update_meta_text(nullptr, m_metaReplicateState,
                                  m_repCtl.selected_model_slug(m_hMetaModel));
    }
}

void CSettingsView::ReadMetaSettings(MetaSettings& out) const
{
    out = {};
    out.out_dir = wide_to_utf8(get_edit_text(m_hMetaOutDir));
    out.out_md      = m_hMetaOutMd      && ::SendMessageW(m_hMetaOutMd,      BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.out_json    = m_hMetaOutJson    && ::SendMessageW(m_hMetaOutJson,    BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.update_exif = m_hMetaUpdateExif && ::SendMessageW(m_hMetaUpdateExif, BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.resize_first= m_hMetaResizeFirst&& ::SendMessageW(m_hMetaResizeFirst,BM_GETCHECK, 0, 0) == BST_CHECKED;

    int wi = m_hMetaResizeW ? (int)::SendMessageW(m_hMetaResizeW, CB_GETCURSEL, 0, 0) : 1;
    out.resize_width = settings_panel_internals::preresize_width_from_cb_sel(wi);

    out.provider = pmui::provider_models::selected_provider_id(m_hMetaProvider);
    out.model = (out.provider == "replicate")
        ? m_repCtl.selected_model_slug(m_hMetaModel)
        : pmui::provider_models::combo_text(m_hMetaModel);

    out.prompt = wide_to_utf8(get_edit_text(m_hMetaPrompt));
}

// ── Find panel ───────────────────────────────────────────────────────────────

void CSettingsView::ReadFindSettings(FindSettings& out) const
{
    out = {};
    out.prompt = wide_to_utf8(get_edit_text(m_hFindPrompt));
    out.llm           = m_hFindLlm           && ::SendMessageW(m_hFindLlm,           BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.bypass_cache  = m_hFindBypassCache   && ::SendMessageW(m_hFindBypassCache,   BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.generate      = !(m_hFindNoGenerate  && ::SendMessageW(m_hFindNoGenerate,    BM_GETCHECK, 0, 0) == BST_CHECKED);
    out.match_folders = m_hFindMatchFolders  && ::SendMessageW(m_hFindMatchFolders,  BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.recursive     = m_hFindRecursive     && ::SendMessageW(m_hFindRecursive,     BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.use_md        = m_hFindUseMd         && ::SendMessageW(m_hFindUseMd,         BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.use_json      = m_hFindUseJson       && ::SendMessageW(m_hFindUseJson,       BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.use_exif      = m_hFindUseExif       && ::SendMessageW(m_hFindUseExif,       BM_GETCHECK, 0, 0) == BST_CHECKED;
    out.max_results   = std::max(0, get_edit_int(m_hFindMax));

    out.provider = pmui::provider_models::selected_provider_id(m_hFindProvider);
    out.model = (out.provider == "replicate")
        ? m_repCtl.selected_model_slug(m_hFindModel)
        : pmui::provider_models::combo_text(m_hFindModel);

    int wi = m_hFindResizeW ? (int)::SendMessageW(m_hFindResizeW, CB_GETCURSEL, 0, 0) : 1;
    out.resize_width = settings_panel_internals::preresize_width_from_cb_sel(wi);
    out.resize_first = m_hFindResizeFirst
                           && ::SendMessageW(m_hFindResizeFirst, BM_GETCHECK, 0, 0) == BST_CHECKED;

    if (m_hFindRefList) {
        int n = (int)::SendMessageW(m_hFindRefList, LB_GETCOUNT, 0, 0);
        for (int i = 0; i < n; ++i) {
            int len = (int)::SendMessageW(m_hFindRefList, LB_GETTEXTLEN, (WPARAM)i, 0);
            if (len <= 0) continue;
            std::wstring buf((size_t)len + 1, L'\0');
            ::SendMessageW(m_hFindRefList, LB_GETTEXT, (WPARAM)i, (LPARAM)buf.data());
            buf.resize(len);
            out.reference_images.push_back(wide_to_utf8(buf));
        }
    }
}

void CSettingsView::OnFindProviderChanged()
{
    if (!m_hFindProvider || !m_hFindModel) return;
    const std::string provider = pmui::provider_models::selected_provider_id(m_hFindProvider);
    const bool is_replicate = provider == "replicate";
    m_repCtl.set_row_visible(
        {m_hFindCollection, m_hFindRefresh, m_hFindCollectionLbl}, is_replicate && (m_mode == MODE_FIND));
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find(provider);
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    if (is_replicate) {
        if (m_deferReplicateNetworkInit) return;
        const std::string keep = m_repCtl.selected_model_slug(m_hFindModel);
        std::wstring rep_err;
        if (m_repCtl.reload(api_key, base_url, m_hFindCollection, m_hFindModel,
                            m_findReplicateState, keep, true, false, &rep_err)) {
            m_repCtl.update_meta_text(nullptr, m_findReplicateState,
                                      m_repCtl.selected_model_slug(m_hFindModel));
        }
    } else {
        std::string pop_err;
        if (!pmui::provider_models::populate_model_combo(
                m_hFindModel, provider, api_key, base_url, "", pop_err)) {
            std::string ignore;
            (void)pmui::provider_models::populate_model_combo(
                m_hFindModel, provider, "", "", "", ignore);
        }
    }
}

void CSettingsView::OnFindCollectionChanged()
{
    if (!m_hFindProvider || !m_hFindModel || !m_hFindCollection) return;
    if (pmui::provider_models::selected_provider_id(m_hFindProvider) != "replicate") return;
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find("replicate");
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    if (!m_repCtl.apply_collection_from_combo(m_hFindCollection, m_findReplicateState)) return;
    const std::string keep = m_repCtl.selected_model_slug(m_hFindModel);
    std::wstring rep_err;
    if (m_repCtl.reload(api_key, base_url, m_hFindCollection, m_hFindModel,
                        m_findReplicateState, keep, false, false, &rep_err)) {
        m_repCtl.update_meta_text(nullptr, m_findReplicateState,
                                  m_repCtl.selected_model_slug(m_hFindModel));
    }
}

void CSettingsView::OnFindRefreshReplicateModels()
{
    if (!m_hFindProvider || pmui::provider_models::selected_provider_id(m_hFindProvider) != "replicate") return;
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find("replicate");
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    const std::string keep = m_repCtl.selected_model_slug(m_hFindModel);
    std::wstring rep_err;
    if (m_repCtl.reload(api_key, base_url, m_hFindCollection, m_hFindModel,
                        m_findReplicateState, keep, true, true, &rep_err)) {
        m_repCtl.update_meta_text(nullptr, m_findReplicateState,
                                  m_repCtl.selected_model_slug(m_hFindModel));
    }
}

CommandProviderOverridesKeysPresent CSettingsView::LoadCommandProviderOverrides()
{
    CommandProviderOverridesKeysPresent got{};
    std::string err;
    json j;
    if (!media::settings::load_subtree(kCommandProviderOverridesKey, j, err) || !j.is_object())
        return got;

    auto apply_one = [&](const char* key, HWND h_provider, HWND h_model,
                         pmui::ReplicateSelectorState& rep_st, auto on_provider_changed) {
        if (!h_provider || !h_model || !j.contains(key) || !j[key].is_object()) return;
        if (std::strcmp(key, "transform") == 0) got.transform = true;
        else if (std::strcmp(key, "meta") == 0) got.meta = true;
        else if (std::strcmp(key, "find") == 0) got.find = true;
        const auto& c = j[key];
        const std::string provider = c.value("provider", std::string{});
        const std::string model = c.value("model", std::string{});
        if (provider == "replicate" && c.contains("collection") && c["collection"].is_string())
            rep_st.selected_collection = c["collection"].get<std::string>();
        if (!provider.empty()) select_provider(h_provider, provider);
        on_provider_changed();
        if (!model.empty()) select_model(h_model, provider.empty() ? pmui::provider_models::selected_provider_id(h_provider) : provider, model);
    };

    apply_one("transform", m_hTfProvider, m_hTfModel, m_tfReplicateState, [this]() { OnTfProviderChanged(); });
    apply_one("meta", m_hMetaProvider, m_hMetaModel, m_metaReplicateState, [this]() { OnMetaProviderChanged(); });
    apply_one("find", m_hFindProvider, m_hFindModel, m_findReplicateState, [this]() { OnFindProviderChanged(); });
    return got;
}

void CSettingsView::ApplyDeferredCommandProviderOverrides()
{
    m_deferReplicateNetworkInit = true;
    const auto ov = LoadCommandProviderOverrides();
    if (!ov.transform) OnTfProviderChanged();
    if (!ov.meta) OnMetaProviderChanged();
    if (!ov.find) OnFindProviderChanged();
    m_deferReplicateNetworkInit = false;
    startReplicateNetworkBatchAsync();
}

void CSettingsView::startReplicateNetworkBatchAsync()
{
    HWND hwnd = GetHwnd();
    if (!hwnd || !m_replicateAsyncCancel) return;
    const bool want_tf = m_hTfProvider && pmui::provider_models::selected_provider_id(m_hTfProvider) == "replicate";
    const bool want_meta
        = m_hMetaProvider && pmui::provider_models::selected_provider_id(m_hMetaProvider) == "replicate";
    const bool want_find
        = m_hFindProvider && pmui::provider_models::selected_provider_id(m_hFindProvider) == "replicate";
    if (!want_tf && !want_meta && !want_find) return;

    const CmdOverrideModelSlugs keeps = read_cmd_override_model_slugs();
    std::string                 api_key;
    std::string                 base_url;
    std::string                 load_err;
    media::settings::ProviderMap pm;
    (void)media::settings::load_providers(pm, load_err);
    if (auto it = pm.find("replicate"); it != pm.end()) {
        api_key  = it->second.api_key;
        base_url = it->second.base_url;
    }

    pmui::ReplicateSelectorState snap_tf   = m_tfReplicateState;
    pmui::ReplicateSelectorState snap_meta = m_metaReplicateState;
    pmui::ReplicateSelectorState snap_find = m_findReplicateState;
    const std::shared_ptr<std::atomic<bool>> cancel = m_replicateAsyncCancel;

    std::thread(settings_repinit_worker, hwnd, cancel, want_tf, want_meta, want_find, keeps.tf, keeps.meta, keeps.find,
        std::move(snap_tf), std::move(snap_meta), std::move(snap_find), std::move(api_key), std::move(base_url))
        .detach();
}

void CSettingsView::handleReplicateNetworkBatchDone(SettingsRepinitPayload* p)
{
    if (!p) return;
    const CmdOverrideModelSlugs keeps = read_cmd_override_model_slugs();
    if (p->ok_tf && m_hTfCollection && m_hTfModel) {
        m_tfReplicateState = std::move(p->tf);
        m_repCtl.reload_apply_ui(m_hTfCollection, m_hTfModel, m_tfReplicateState, keeps.tf);
        m_repCtl.update_meta_text(nullptr, m_tfReplicateState, m_repCtl.selected_model_slug(m_hTfModel));
    }
    if (p->ok_meta && m_hMetaCollection && m_hMetaModel) {
        m_metaReplicateState = std::move(p->meta);
        m_repCtl.reload_apply_ui(m_hMetaCollection, m_hMetaModel, m_metaReplicateState, keeps.meta);
        m_repCtl.update_meta_text(nullptr, m_metaReplicateState, m_repCtl.selected_model_slug(m_hMetaModel));
    }
    if (p->ok_find && m_hFindCollection && m_hFindModel) {
        m_findReplicateState = std::move(p->find);
        m_repCtl.reload_apply_ui(m_hFindCollection, m_hFindModel, m_findReplicateState, keeps.find);
        m_repCtl.update_meta_text(nullptr, m_findReplicateState, m_repCtl.selected_model_slug(m_hFindModel));
    }
    reapplyCommandOverrideModelSelections();
    delete p;
}

void CSettingsView::reapplyCommandOverrideModelSelections()
{
    std::string err;
    json        j;
    if (!media::settings::load_subtree(kCommandProviderOverridesKey, j, err) || !j.is_object()) return;
    auto apply_one = [&](const char* key, HWND h_provider, HWND h_model) {
        if (!h_provider || !h_model || !j.contains(key) || !j[key].is_object()) return;
        const auto&     c        = j[key];
        const std::string provider = c.value("provider", std::string{});
        const std::string model    = c.value("model", std::string{});
        if (model.empty()) return;
        const std::string pid = provider.empty() ? pmui::provider_models::selected_provider_id(h_provider) : provider;
        select_model(h_model, pid, model);
    };
    apply_one("transform", m_hTfProvider, m_hTfModel);
    apply_one("meta", m_hMetaProvider, m_hMetaModel);
    apply_one("find", m_hFindProvider, m_hFindModel);
}

void CSettingsView::SaveCommandProviderOverrides() const
{
    TransformSettings ts;
    MetaSettings ms;
    FindSettings fs;
    ReadTransformSettings(ts);
    ReadMetaSettings(ms);
    ReadFindSettings(fs);

    json j = json::object();
    auto jt = json{{"provider", ts.provider}, {"model", ts.model}};
    if (ts.provider == "replicate" && !m_tfReplicateState.selected_collection.empty())
        jt["collection"] = m_tfReplicateState.selected_collection;
    j["transform"] = jt;
    auto jm = json{{"provider", ms.provider}, {"model", ms.model}};
    if (ms.provider == "replicate" && !m_metaReplicateState.selected_collection.empty())
        jm["collection"] = m_metaReplicateState.selected_collection;
    j["meta"] = jm;
    auto jf = json{{"provider", fs.provider}, {"model", fs.model}};
    if (fs.provider == "replicate" && !m_findReplicateState.selected_collection.empty())
        jf["collection"] = m_findReplicateState.selected_collection;
    j["find"] = jf;

    std::string err;
    (void)media::settings::save_subtree(kCommandProviderOverridesKey, j, err);
}

void CSettingsView::InstallDuplicateTooltips()
{
    HWND p = GetHwnd();
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupHowSec,
        L"Choose what “duplicate” means: same size only, images that look alike, or text and camera data read from side files and EXIF.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupMode,
        L"Identical file size: only files with the same number of bytes.\n\n"
        L"Similar looks: compares a compact image fingerprint. The value under “How different allowed” is a distance (0 = strictest).\n\n"
        L"Text & EXIF: build a key from the options in the “Text & EXIF” block (or from AI, if you turn that on).");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupMinGroup,
        L"Only show a group if it has at least this many matching files. Use 2 for the usual “pairs and larger” behaviour.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupRecursive,
        L"When checked, every subfolder is scanned under the folder you picked in the Explorer or queue. Turn off to look at one folder only.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupVisSec,
        L"Used when you pick “Similar looks”. Pairs and groups are found from picture fingerprints, not from filename.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupMaxHam,
        L"For fingerprints, how far apart two images are allowed to be. 0 = only a near-exact match; "
        L"higher numbers allow recompressed, rescaled, or more edited images to still count as the same.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupFpSameSize,
        L"Extra safety: we only compare fingerprints for files that already have the same byte size. Cuts false matches a bit, but you might miss a resaved file with a different file size.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupTextSec,
        L"Used when you pick “Text & EXIF”. We build a key from the files and fields you check here, next to each image. Turn on AI to compare .json in depth.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupUseMd,
        L"If a Markdown (.md) file exists with the same base name as the image, its text is read and mixed into the metadata key. Turn off the AI option above to use this path.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupUseJson,
        L"Looks for a .json file next to the image and includes that content in the key. Often used for XMP- or export-style sidecars.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupUseExif,
        L"Embeds key camera and date fields from the file’s EXIF block in the key (when the format stores EXIF).");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupMetaPrompt,
        L"Optional line of text put in front of your text sidecar content when the hash is built. Leave blank to use the default from the app.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupPairSec,
        L"LLM API used only to score how similar two .json sidecars are. This is not the same as the Meta tab, which runs vision on images to build sidecars.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupMetaLlm,
        L"When on, a cloud model compares each pair of .json files and links similar images. “Default: Chat” uses your Chat router; named entries are pair-compare only.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupImplicitMeta,
        L"If a file has no .json, run the same Meta pipeline as the Meta tab (resize, vision model, your prompt). "
        L"Uses the Meta tab provider/keys (image/vision), not the pair-compare API above.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupGenSec,
        L"Image operations: the Meta tab’s Google (or other) provider generates sidecars. This is not the same account as the JSON pair-compare list.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupMetaGenHint,
        L"Pair-compare and Meta cataloguer are two different services. Confusing them is a common source of “wrong key” issues.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupLlmSource,
        L"“Default: Chat” uses the Chat / pair-compare service. Pick a provider name to force a saved key for pair calls only. Image cataloguing is configured on the Meta tab.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupLlmInfo,
        L"Read-only: shows which service and key status will be used for the next AI comparison.");
    add_settings_tooltip(
        p, &m_hTooltipDup, m_hDupMinSim,
        L"The AI returns a similarity from 0 to 10. Two files are only linked if the score is at least this value. "
        L"Higher = stricter (fewer matches). Lower = more lenient. Same as the JSON field meta_json_min_similarity.");
}

void CSettingsView::RefreshDupLlmInfoText()
{
    if (!m_hDupLlmInfo || !m_hDupLlmSource) return;
    int sel = (int)::SendMessageW(m_hDupLlmSource, CB_GETCURSEL, 0, 0);
    if (sel < 0) sel = 0;
    std::wstring line;
    if (sel == 0) {
        std::string            err;
        media::settings::ChatProviderSettings cs;
        if (media::settings::load_chat_provider(cs, err)) {
            line = L"Same as Chat: ";
            line += utf8_to_wide(cs.router);
            line += L" / ";
            line += utf8_to_wide(cs.model.empty() ? "—" : cs.model);
            line += L". API key: from AI Provider Keys (or env) — not stored under settings.json[\"chat\"].";
        } else {
            line = L"Chat settings unavailable. Use App keys or pick a provider below.";
        }
    } else if (sel < (int)m_dupLlmProviderKeys.size()) {
        const std::string& key = m_dupLlmProviderKeys[static_cast<size_t>(sel)];
        std::string        perr;
        media::settings::ProviderMap pm;
        if (key.empty())
            line = L"(internal)";
        else if (media::settings::load_providers(pm, perr)) {
            auto it = pm.find(key);
            if (it != pm.end()) {
                line = utf8_to_wide(key);
                if (!it->second.default_model.empty()) {
                    line += L" — model ";
                    line += utf8_to_wide(it->second.default_model);
                }
                line += it->second.api_key.empty() ? L". API key missing (set in App keys / env)."
                                                    : L". API key saved.";
            } else
                line = L"Provider not found in settings.";
        }
    }
    if (line.size() > 110) {
        line.resize(107);
        line += L"\u2026";
    }
    ::SetWindowTextW(m_hDupLlmInfo, line.c_str());
}

void CSettingsView::RefreshDupControlStates()
{
    if (!m_hDupMode) return;
    int mi = (int)::SendMessageW(m_hDupMode, CB_GETCURSEL, 0, 0);
    if (mi < 0) mi = 1;
    const bool isFp   = (mi == 1);
    const bool isMeta = (mi == 2);
    const bool llm    = m_hDupMetaLlm
                     && ::SendMessageW(m_hDupMetaLlm, BM_GETCHECK, 0, 0) == BST_CHECKED;
    auto en = [](HWND h, bool on) {
        if (h) ::EnableWindow(h, on ? TRUE : FALSE);
    };
    en(m_hDupVisSec, isFp);
    en(m_hDupMaxHam, isFp);
    en(m_hDupFpSameSize, isFp);
    en(m_hDupTextSec, isMeta);
    en(m_hDupUseMd, isMeta && !llm);
    en(m_hDupUseJson, isMeta && !llm);
    en(m_hDupUseExif, isMeta && !llm);
    en(m_hDupMetaPrompt, isMeta && !llm);
    en(m_hDupPairSec, isMeta);
    en(m_hDupMetaLlm, isMeta);
    en(m_hDupImplicitMeta, isMeta && llm);
    en(m_hDupLlmSource, isMeta && llm);
    en(m_hDupLlmInfo, isMeta && llm);
    en(m_hDupMinSim, isMeta && llm);
    en(m_hDupGenSec, isMeta && llm);
    en(m_hDupMetaGenHint, isMeta && llm);
    if (isMeta && llm) RefreshDupLlmInfoText();
}

void CSettingsView::ReadDuplicatesSettings(media::DuplicatesOptions& o) const
{
    o = {};
    o.detailed_report = true;
    int modeIdx = m_hDupMode ? (int)::SendMessageW(m_hDupMode, CB_GETCURSEL, 0, 0) : 1;
    o.mode    = (modeIdx <= 0) ? media::DuplicatesMode::Size
             : (modeIdx == 1) ? media::DuplicatesMode::Fingerprint
                              : media::DuplicatesMode::Meta;
    o.min_group_size = 2;
    if (m_hDupMinGroup) {
        int s = (int)::SendMessageW(m_hDupMinGroup, CB_GETCURSEL, 0, 0);
        o.min_group_size = (s >= 0) ? (s + 2) : 2;
    }
    o.min_group_size = std::max(2, o.min_group_size);
    o.recursive
        = m_hDupRecursive && ::SendMessageW(m_hDupRecursive, BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.max_hamming     = m_hDupMaxHam ? (int)::SendMessageW(m_hDupMaxHam, CB_GETCURSEL, 0, 0) : 0;
    o.max_hamming     = std::clamp(o.max_hamming, 0, 64);
    o.fingerprint_same_size_only
        = m_hDupFpSameSize && ::SendMessageW(m_hDupFpSameSize, BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.use_md  = m_hDupUseMd && ::SendMessageW(m_hDupUseMd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.use_json= m_hDupUseJson && ::SendMessageW(m_hDupUseJson, BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.use_exif= m_hDupUseExif && ::SendMessageW(m_hDupUseExif, BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.meta_prompt = m_hDupMetaPrompt ? wide_to_utf8(get_edit_text(m_hDupMetaPrompt)) : std::string{};
    o.meta_json_llm_compare
        = m_hDupMetaLlm && ::SendMessageW(m_hDupMetaLlm, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int sim = m_hDupMinSim ? (int)::SendMessageW(m_hDupMinSim, CB_GETCURSEL, 0, 0) : 7;
    o.meta_json_min_similarity = std::clamp(sim, 0, 10);
    if (m_hDupLlmSource) {
        int sel = (int)::SendMessageW(m_hDupLlmSource, CB_GETCURSEL, 0, 0);
        if (sel > 0 && sel < (int)m_dupLlmProviderKeys.size()) {
            const std::string& key = m_dupLlmProviderKeys[static_cast<size_t>(sel)];
            if (!key.empty()) {
                std::string perr;
                media::settings::ProviderMap pm;
                if (media::settings::load_providers(pm, perr)) {
                    auto it = pm.find(key);
                    if (it != pm.end()) {
                        o.llm_router   = key;
                        o.llm_base_url = it->second.base_url;
                        o.llm_model    = it->second.default_model;
                    }
                }
            }
        }
    }
    o.meta_json_implicit_generate
        = m_hDupImplicitMeta && ::SendMessageW(m_hDupImplicitMeta, BM_GETCHECK, 0, 0) == BST_CHECKED;
    {
        MetaSettings ms;
        ReadMetaSettings(ms);
        o.meta.provider     = ms.provider;
        o.meta.model        = ms.model;
        o.meta.prompt       = ms.prompt;
        o.meta.resize_first = ms.resize_first;
        o.meta.resize_width = ms.resize_width;
        o.meta.out_md       = ms.out_md;
        o.meta.out_json     = ms.out_json;
        o.meta.update_exif  = ms.update_exif;
        o.meta.out_dir      = ms.out_dir;
    }
}

void CSettingsView::InstallSettingsPanelTooltips()
{
    const auto& trs = pmui::settings_panel_i18n::transform_strings_for(m_display_language);
    const auto& fnd = pmui::settings_panel_i18n::find_strings_for(m_display_language);
    const auto& met = pmui::settings_panel_i18n::meta_strings_for(m_display_language);
    HWND        p   = GetHwnd();
    if (m_hTfRefresh)
        add_settings_tooltip(p, &m_hTooltipSettings, m_hTfRefresh, trs.tt_refresh);
    if (m_hTfPresets)
        add_settings_tooltip(p, &m_hTooltipSettings, m_hTfPresets, trs.tt_presets);
    if (m_hTfRefAdd)
        add_settings_tooltip(p, &m_hTooltipSettings, m_hTfRefAdd, trs.tt_add_ref);
    if (m_hTfRefClear)
        add_settings_tooltip(p, &m_hTooltipSettings, m_hTfRefClear, trs.tt_clear_ref);
    if (m_hMetaRefresh)
        add_settings_tooltip(p, &m_hTooltipSettings, m_hMetaRefresh, met.tt_refresh);
    if (m_hFindRefresh)
        add_settings_tooltip(p, &m_hTooltipSettings, m_hFindRefresh, fnd.tt_refresh);
    if (m_hFindRefAdd)
        add_settings_tooltip(p, &m_hTooltipSettings, m_hFindRefAdd, fnd.tt_add_ref);
    if (m_hFindRefClear)
        add_settings_tooltip(p, &m_hTooltipSettings, m_hFindRefClear, fnd.tt_clear_ref);
}
