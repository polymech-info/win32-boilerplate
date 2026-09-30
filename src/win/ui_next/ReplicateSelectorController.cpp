#include "stdafx.h"
#include "ReplicateSelectorController.h"
#include "win/settings_store.hpp"

#include <algorithm>
#include <cctype>

namespace pmui {

namespace {

struct ReplicateSelectorStrings {
    const wchar_t* err_key_required;
    const char* err_fetch_collections;
    const char* err_fetch_models;
    const char* visibility;
    const char* unknown;
    const char* official;
    const char* yes;
    const char* no;
    const char* description;
    const char* url;
};

static std::string norm_lang(std::string_view v) {
    std::string s(v);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static const ReplicateSelectorStrings& replicate_strings_for(std::string_view display_language) {
    static const ReplicateSelectorStrings en{
        L"Replicate API key is required (App keys, or set IMAGE_TRANSFORM_REPLICATE_API_KEY / REPLICATE_API_TOKEN).",
        "Failed to fetch Replicate collections: ",
        "Failed to fetch Replicate models: ",
        "visibility", "unknown", "official", "true", "false", "description", "url",
    };
    static const ReplicateSelectorStrings es{
        L"Se requiere la clave API de Replicate (claves de la app, o defina IMAGE_TRANSFORM_REPLICATE_API_KEY / REPLICATE_API_TOKEN).",
        "No se pudieron obtener las colecciones de Replicate: ",
        "No se pudieron obtener los modelos de Replicate: ",
        "visibilidad", "desconocida", "oficial", "sí", "no", "descripción", "url",
    };
    static const ReplicateSelectorStrings de{
        L"Replicate-API-Schlüssel erforderlich (App-Schlüssel, oder IMAGE_TRANSFORM_REPLICATE_API_KEY / REPLICATE_API_TOKEN setzen).",
        "Replicate-Sammlungen konnten nicht abgerufen werden: ",
        "Replicate-Modelle konnten nicht abgerufen werden: ",
        "Sichtbarkeit", "unbekannt", "offiziell", "ja", "nein", "Beschreibung", "URL",
    };
    static const ReplicateSelectorStrings it{
        L"È richiesta la chiave API Replicate (chiavi app, oppure impostare IMAGE_TRANSFORM_REPLICATE_API_KEY / REPLICATE_API_TOKEN).",
        "Impossibile recuperare le raccolte Replicate: ",
        "Impossibile recuperare i modelli Replicate: ",
        "visibilità", "sconosciuta", "ufficiale", "sì", "no", "descrizione", "url",
    };
    static const ReplicateSelectorStrings fr{
        L"La clé API Replicate est requise (clés de l'app, ou définir IMAGE_TRANSFORM_REPLICATE_API_KEY / REPLICATE_API_TOKEN).",
        "Impossible de récupérer les collections Replicate : ",
        "Impossible de récupérer les modèles Replicate : ",
        "visibilité", "inconnue", "officiel", "oui", "non", "description", "url",
    };
    const std::string c = norm_lang(display_language);
    if (c == "es") return es;
    if (c == "de") return de;
    if (c == "it") return it;
    if (c == "fr") return fr;
    return en;
}

std::string get_text(HWND h) {
    wchar_t b[2048]{};
    if (h) ::GetWindowTextW(h, b, (int)std::size(b));
    return wide_to_utf8(b);
}

std::string strip_group_prefix(const std::string& s) {
    if (s.size() > 2 && s[1] == '/') return s.substr(2);
    return s;
}

void ensure_combo_drop_width(HWND h_combo) {
    if (!h_combo) return;
    const int item_count = (int)::SendMessageW(h_combo, CB_GETCOUNT, 0, 0);
    if (item_count <= 0) return;

    RECT rc{};
    ::GetWindowRect(h_combo, &rc);
    int max_width = rc.right - rc.left;

    HDC hdc = ::GetDC(h_combo);
    if (!hdc) return;
    HFONT hfont = (HFONT)::SendMessageW(h_combo, WM_GETFONT, 0, 0);
    HGDIOBJ old = nullptr;
    if (hfont) old = ::SelectObject(hdc, hfont);

    for (int i = 0; i < item_count; ++i) {
        const int text_len = (int)::SendMessageW(h_combo, CB_GETLBTEXTLEN, (WPARAM)i, 0);
        if (text_len <= 0) continue;
        std::wstring text((size_t)text_len + 1, L'\0');
        ::SendMessageW(h_combo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)text.data());
        text.resize((size_t)text_len);
        SIZE sz{};
        if (::GetTextExtentPoint32W(hdc, text.c_str(), (int)text.size(), &sz)) {
            max_width = (std::max)(max_width, (int)sz.cx + 40);
        }
    }

    if (old) (void)::SelectObject(hdc, old);
    ::ReleaseDC(h_combo, hdc);
    ::SendMessageW(h_combo, CB_SETDROPPEDWIDTH, (WPARAM)max_width, 0);
}

void fill_collection_combo(HWND h, const ReplicateSelectorState& st) {
    if (!h) return;
    ::SendMessageW(h, CB_RESETCONTENT, 0, 0);
    int set_idx = 0;
    int i = 0;
    for (const auto& c : st.collections) {
        const std::wstring line = utf8_to_wide(c.name.empty() ? c.slug : c.name);
        ::SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)line.c_str());
        if (c.slug == st.selected_collection) set_idx = i;
        ++i;
    }
    ::SendMessageW(h, CB_SETCURSEL, set_idx, 0);
    ensure_combo_drop_width(h);
}

} // namespace

void ReplicateSelectorController::set_display_language(std::string_view display_language) {
    display_language_ = std::string(display_language.empty() ? std::string_view("en") : display_language);
}

ReplicateSelectorRowHandles ReplicateSelectorController::render_collection_row(
    const ReplicateSelectorRowRenderSpec& spec) const {
    ReplicateSelectorRowHandles out{};
    if (!spec.parent || !spec.inst || spec.collection_id == 0) return out;

    const int label_h = (std::max)(16, spec.row_h);
    out.h_label = ::CreateWindowExW(0, L"STATIC", spec.label_text.c_str(),
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        spec.label_x, spec.y, spec.label_w, label_h,
        spec.parent, nullptr, spec.inst, nullptr);

    const int combo_w = spec.show_refresh ? (spec.combo_w - spec.refresh_w - spec.gap) : spec.combo_w;
    const int combo_cy = spec.row_h > 12 ? spec.row_h : 12;
    out.h_collection = ::CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        spec.field_x, spec.y, combo_w, combo_cy, spec.parent,
        (HMENU)(UINT_PTR)spec.collection_id, spec.inst, nullptr);

    if (spec.show_refresh && spec.refresh_id != 0) {
        out.h_refresh = ::CreateWindowExW(0, L"BUTTON", spec.refresh_text.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            spec.field_x + combo_w + spec.gap, spec.y, spec.refresh_w, spec.row_h, spec.parent,
            (HMENU)(UINT_PTR)spec.refresh_id, spec.inst, nullptr);
    }

    if (spec.font) {
        if (out.h_label) ::SendMessageW(out.h_label, WM_SETFONT, (WPARAM)spec.font, TRUE);
        if (out.h_collection) ::SendMessageW(out.h_collection, WM_SETFONT, (WPARAM)spec.font, TRUE);
        if (out.h_refresh) ::SendMessageW(out.h_refresh, WM_SETFONT, (WPARAM)spec.font, TRUE);
    }
    set_row_visible(out, spec.initially_visible);
    return out;
}

void ReplicateSelectorController::set_row_visible(const ReplicateSelectorRowHandles& h, bool visible) const {
    const int sw = visible ? SW_SHOW : SW_HIDE;
    if (h.h_label) ::ShowWindow(h.h_label, sw);
    if (h.h_collection) {
        ::ShowWindow(h.h_collection, sw);
        ::EnableWindow(h.h_collection, visible ? TRUE : FALSE);
    }
    if (h.h_refresh) ::ShowWindow(h.h_refresh, sw);
}

bool ReplicateSelectorController::reload_fetch_state(const std::string& api_key,
                                                     const std::string& base_url,
                                                     ReplicateSelectorState& st,
                                                     const std::string& keep_model_slug,
                                                     bool allow_collection_guess_from_model,
                                                     bool force_refresh,
                                                     std::string& err_utf8) const {
    err_utf8.clear();
    std::string err;
    if (!provider_dlg_replicate::fetch_collections(api_key, base_url, st.collections, err, force_refresh)) {
        err_utf8 = err;
        return false;
    }
    provider_dlg_replicate::sort_collections_alpha(st.collections);
    if (st.selected_collection.empty()) st.selected_collection = "official";
    bool has_selection = false;
    for (const auto& c : st.collections) if (c.slug == st.selected_collection) { has_selection = true; break; }
    if (!has_selection && !st.collections.empty()) st.selected_collection = st.collections.front().slug;

    if (allow_collection_guess_from_model && !keep_model_slug.empty()) {
        std::string guessed;
        if (provider_dlg_replicate::resolve_collection_for_model_cached(keep_model_slug, guessed)) {
            for (const auto& c : st.collections) {
                if (c.slug == guessed) { st.selected_collection = guessed; break; }
            }
        }
    }

    if (!provider_dlg_replicate::fetch_collection_models(
            api_key, base_url, st.selected_collection, st.models, err, force_refresh)) {
        err_utf8 = err;
        return false;
    }
    provider_dlg_replicate::sort_models_alpha(st.models);
    st.model_by_slug.clear();
    for (const auto& m : st.models) st.model_by_slug[m.slug] = m;
    return true;
}

void ReplicateSelectorController::reload_apply_ui(HWND h_collection_combo,
                                                  HWND h_model_combo,
                                                  ReplicateSelectorState& st,
                                                  const std::string& keep_model_slug) const {
    if (!h_model_combo) return;
    fill_collection_combo(h_collection_combo, st);
    ::SendMessageW(h_model_combo, CB_RESETCONTENT, 0, 0);
    for (const auto& m : st.models) {
        const char g = m.slug.empty() ? '?' : (char)std::tolower((unsigned char)m.slug[0]);
        const std::wstring line = utf8_to_wide(std::string(1, g) + "/" + m.slug);
        ::SendMessageW(h_model_combo, CB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    ensure_combo_drop_width(h_model_combo);
    std::string selected = keep_model_slug;
    if (selected.empty() || !st.model_by_slug.count(selected)) {
        if (!st.models.empty()) selected = st.models.front().slug;
    }
    if (!selected.empty()) {
        const char g = (char)std::tolower((unsigned char)selected[0]);
        const std::wstring grouped = utf8_to_wide(std::string(1, g) + "/" + selected);
        if (::SendMessageW(h_model_combo, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)grouped.c_str()) == CB_ERR) {
            ::SetWindowTextW(h_model_combo, grouped.c_str());
        }
    }
}

bool ReplicateSelectorController::reload(const std::string& api_key,
                                         const std::string& base_url,
                                         HWND h_collection_combo,
                                         HWND h_model_combo,
                                         ReplicateSelectorState& st,
                                         const std::string& keep_model_slug,
                                         bool allow_collection_guess_from_model,
                                         bool force_refresh,
                                         std::wstring* err_out) const {
    if (err_out) err_out->clear();
    if (!h_model_combo) return false;

    std::string err;
    if (!reload_fetch_state(api_key, base_url, st, keep_model_slug, allow_collection_guess_from_model, force_refresh, err)) {
        if (err_out) {
            const auto& tr = replicate_strings_for(display_language_);
            if (err == "api key is empty")
                *err_out = tr.err_key_required;
            else if (st.collections.empty())
                *err_out = utf8_to_wide(std::string(tr.err_fetch_collections) + err);
            else
                *err_out = utf8_to_wide(std::string(tr.err_fetch_models) + err);
        }
        return false;
    }
    reload_apply_ui(h_collection_combo, h_model_combo, st, keep_model_slug);
    return true;
}

bool ReplicateSelectorController::reload_using_saved_settings(HWND h_collection_combo,
                                                              HWND h_model_combo,
                                                              ReplicateSelectorState& st,
                                                              const std::string& keep_model_slug,
                                                              bool allow_collection_guess_from_model,
                                                              bool force_refresh,
                                                              std::wstring* err_out) const {
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
    return reload(api_key, base_url, h_collection_combo, h_model_combo, st, keep_model_slug,
                  allow_collection_guess_from_model, force_refresh, err_out);
}

bool ReplicateSelectorController::apply_collection_from_combo(HWND h_collection_combo, ReplicateSelectorState& st) const {
    if (!h_collection_combo) return false;
    const int sel = (int)::SendMessageW(h_collection_combo, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)st.collections.size()) return false;
    st.selected_collection = st.collections[(size_t)sel].slug;
    return true;
}

std::string ReplicateSelectorController::selected_model_slug(HWND h_model_combo) const {
    return strip_group_prefix(get_text(h_model_combo));
}

void ReplicateSelectorController::update_meta_text(HWND h_meta_edit,
                                                   ReplicateSelectorState& st,
                                                   const std::string& model_slug) const {
    auto it = st.model_by_slug.find(model_slug);
    if (it == st.model_by_slug.end()) {
        if (h_meta_edit) ::SetWindowTextW(h_meta_edit, L"");
        st.selected_model_url.clear();
        return;
    }
    const auto& m = it->second;
    st.selected_model_url = m.url;
    if (!h_meta_edit) return;
    const auto& tr = replicate_strings_for(display_language_);
    std::string text = std::string(tr.visibility) + ": " + (m.visibility.empty() ? tr.unknown : m.visibility)
        + " | " + tr.official + ": " + std::string(m.is_official ? tr.yes : tr.no);
    if (!m.description.empty()) text += "\r\n" + std::string(tr.description) + ": " + m.description;
    if (!m.url.empty()) text += "\r\n" + std::string(tr.url) + ": " + m.url;
    ::SetWindowTextW(h_meta_edit, utf8_to_wide(text).c_str());
}

} // namespace pmui

