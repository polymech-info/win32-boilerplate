#include "stdafx.h"
#include "OpenAISelectorController.h"
#include "core/openai_models_cli.hpp"
#include "helpers/text_conv.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace pmui {
namespace {

static std::string oai_norm_lang(std::string_view v)
{
    std::string s(v);
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

struct OpenAISelectorStrings {
    const wchar_t* err_no_combo;
    const wchar_t* err_fetch_failed;
    const char*    hint_type_or_refresh;
};

static const OpenAISelectorStrings& oai_strings_for(std::string_view lang)
{
    static const OpenAISelectorStrings en{
        L"OpenAI: internal error (no model combo).",
        L"OpenAI: failed to fetch model list.",
        "Select Refresh to load the catalog, or type a model id directly.",
    };
    static const OpenAISelectorStrings es{
        L"OpenAI: error interno (sin combo de modelos).",
        L"OpenAI: no se pudo obtener la lista de modelos.",
        "Seleccione Actualizar para cargar el catálogo, o escriba un id de modelo directamente.",
    };
    static const OpenAISelectorStrings de{
        L"OpenAI: interner Fehler (keine Modell-Auswahl).",
        L"OpenAI: Modellliste konnte nicht abgerufen werden.",
        "Wählen Sie Aktualisieren, um den Katalog zu laden, oder geben Sie eine Modell-ID ein.",
    };
    static const OpenAISelectorStrings it{
        L"OpenAI: errore interno (combo modelli mancante).",
        L"OpenAI: impossibile recuperare l'elenco dei modelli.",
        "Selezionare Aggiorna per caricare il catalogo oppure digitare un id modello.",
    };
    static const OpenAISelectorStrings fr{
        L"OpenAI : erreur interne (liste de modèles absente).",
        L"OpenAI : impossible de récupérer la liste des modèles.",
        "Sélectionnez Actualiser pour charger le catalogue ou saisissez un id de modèle.",
    };
    const std::string c = oai_norm_lang(lang);
    if (c == "es") return es;
    if (c == "de") return de;
    if (c == "it") return it;
    if (c == "fr") return fr;
    return en;
}

} // namespace

void OpenAISelectorController::set_display_language(std::string_view display_language)
{
    display_language_ = std::string(display_language.empty() ? std::string_view("en") : display_language);
}

bool OpenAISelectorController::reload(const std::string&              api_key,
                                       const std::string&              base_url,
                                       pmui::widgets::SearchableCombo& combo,
                                       OpenAISelectorState&            st,
                                       const std::string&              keep_model_id,
                                       const bool                      force_refresh,
                                       std::wstring*                   err_out) const
{
    if (err_out) err_out->clear();
    st.models.clear();
    st.index_by_id.clear();
    const auto& tr = oai_strings_for(display_language_);
    if (!combo.get()) {
        if (err_out) *err_out = tr.err_no_combo;
        return false;
    }

    std::vector<media::openai_cli::OpenAIModelRow> rows;
    std::string err;
    if (!media::openai_cli::list_openai_models(api_key, base_url, rows, err, force_refresh)) {
        if (err_out) *err_out = pmui::utf8_to_wide(err);
        return false;
    }

    st.models.reserve(rows.size());
    for (auto& r : rows) {
        OpenAIModelInfo mi;
        mi.id       = std::move(r.id);
        mi.owned_by = std::move(r.owned_by);
        st.models.push_back(std::move(mi));
    }
    for (size_t i = 0; i < st.models.size(); ++i)
        st.index_by_id[st.models[i].id] = i;

    std::vector<std::wstring> labels;
    labels.reserve(st.models.size());
    for (const auto& m : st.models)
        labels.push_back(pmui::utf8_to_wide(m.id));
    combo.set_items(std::move(labels));

    if (!st.models.empty()) {
        const std::string want = !keep_model_id.empty() ? keep_model_id : st.models.front().id;
        combo.set_value(pmui::utf8_to_wide(want));
    }
    return true;
}

void OpenAISelectorController::update_meta_text(HWND                       h_meta_edit,
                                                 const OpenAISelectorState& st,
                                                 const std::string&         model_id) const
{
    if (!h_meta_edit) return;
    if (model_id.empty()) { ::SetWindowTextW(h_meta_edit, L""); return; }
    const auto it = st.index_by_id.find(model_id);
    if (it == st.index_by_id.end() || it->second >= st.models.size()) {
        const auto& tr = oai_strings_for(display_language_);
        std::string t  = "id: " + model_id + "\r\n(" + tr.hint_type_or_refresh + ")";
        ::SetWindowTextW(h_meta_edit, pmui::utf8_to_wide(t).c_str());
        return;
    }
    const OpenAIModelInfo& m = st.models[it->second];
    std::string            text = "id: " + m.id + "\r\n";
    if (!m.owned_by.empty()) text += "owned_by: " + m.owned_by + "\r\n";
    ::SetWindowTextW(h_meta_edit, pmui::utf8_to_wide(text).c_str());
}

std::string OpenAISelectorController::selected_model_id(const pmui::widgets::SearchableCombo& combo) const
{
    return pmui::wide_to_utf8(combo.get_value());
}

} // namespace pmui
