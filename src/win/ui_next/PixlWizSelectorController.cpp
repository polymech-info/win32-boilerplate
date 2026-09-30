#include "stdafx.h"
#include "PixlWizSelectorController.h"
#include "core/pixlwiz_provider_models_cli.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace pmui {
namespace {

struct PixlWizStrings {
    const wchar_t* err_no_model_combo;
    const wchar_t* err_bad_model_list;
    const char*    select_refresh;
    const char*    no_key;
};

static std::string norm_lang(std::string_view v) {
    std::string s(v);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static const PixlWizStrings& pixlwiz_strings_for(std::string_view display_language) {
    static const PixlWizStrings en{
        L"PixlWiz: internal error (no model combo).",
        L"PixlWiz: model list is not a JSON object with a data array.",
        "Select Refresh to load the catalog, or type a model id the provider returned.",
        "PixlWiz API key required (set in App Provider Settings).",
    };
    static const PixlWizStrings es{
        L"PixlWiz: error interno (sin combo de modelos).",
        L"PixlWiz: la lista de modelos no es un objeto JSON con un array data.",
        "Seleccione Actualizar para cargar el catálogo, o escriba un id de modelo.",
        "Se requiere la clave API de PixlWiz (configúrela en Ajustes del proveedor).",
    };
    static const PixlWizStrings de{
        L"PixlWiz: interner Fehler (keine Modell-Auswahl).",
        L"PixlWiz: Modellliste ist kein JSON-Objekt mit data-Array.",
        "Wählen Sie Aktualisieren, um den Katalog zu laden, oder geben Sie eine Modell-ID ein.",
        "PixlWiz-API-Schlüssel erforderlich (in den App-Anbietereinstellungen konfigurieren).",
    };
    static const PixlWizStrings it{
        L"PixlWiz: errore interno (combo modelli mancante).",
        L"PixlWiz: l'elenco modelli non è un oggetto JSON con array data.",
        "Selezionare Aggiorna per caricare il catalogo oppure digitare un id modello.",
        "Chiave API PixlWiz richiesta (configurare nelle impostazioni del provider).",
    };
    static const PixlWizStrings fr{
        L"PixlWiz : erreur interne (liste de modèles absente).",
        L"PixlWiz : la liste des modèles n'est pas un objet JSON avec un tableau data.",
        "Sélectionnez Actualiser pour charger le catalogue, ou saisissez un id de modèle.",
        "Clé API PixlWiz requise (à configurer dans les paramètres du fournisseur).",
    };
    const std::string c = norm_lang(display_language);
    if (c == "es") return es;
    if (c == "de") return de;
    if (c == "it") return it;
    if (c == "fr") return fr;
    return en;
}

static std::string to_lower_pw(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static void sort_models_by_name(std::vector<PixlWizModelInfo>& models) {
    std::sort(models.begin(), models.end(), [](const PixlWizModelInfo& a, const PixlWizModelInfo& b) {
        return to_lower_pw(a.name) < to_lower_pw(b.name);
    });
}

} // namespace

void PixlWizSelectorController::set_display_language(std::string_view display_language) {
    display_language_ = std::string(display_language.empty() ? std::string_view("en") : display_language);
}

bool PixlWizSelectorController::reload(const std::string&              api_key,
                                        const std::string&              base_url,
                                        pmui::widgets::SearchableCombo& combo,
                                        PixlWizSelectorState&           st,
                                        const std::string&              keep_model_id,
                                        bool                            force_refresh,
                                        std::wstring*                   err_out) const {
    if (err_out) err_out->clear();
    st.models.clear();
    st.index_by_id.clear();
    st.selected_model_url.clear();
    const auto& tr = pixlwiz_strings_for(display_language_);
    if (!combo.get()) {
        if (err_out) *err_out = tr.err_no_model_combo;
        return false;
    }

    std::vector<media::pixlwiz_cli::PixlWizModelRow> rows;
    std::string err;
    if (!media::pixlwiz_cli::list_pixlwiz_catalog_models(api_key, base_url, rows, err, force_refresh)) {
        if (err_out) *err_out = utf8_to_wide(err);
        return false;
    }

    for (auto& r : rows) {
        PixlWizModelInfo mi;
        mi.id          = std::move(r.id);
        mi.name        = std::move(r.name);
        mi.description = std::move(r.description);
        mi.open_url    = std::move(r.open_url);
        st.models.push_back(std::move(mi));
    }
    sort_models_by_name(st.models);
    for (size_t i = 0; i < st.models.size(); ++i) st.index_by_id[st.models[i].id] = i;

    std::vector<std::wstring> labels;
    labels.reserve(st.models.size());
    for (const auto& m : st.models)
        labels.push_back(utf8_to_wide(m.id));
    combo.set_items(std::move(labels));

    if (!st.models.empty()) {
        const std::string want = !keep_model_id.empty() ? keep_model_id : st.models.front().id;
        combo.set_value(utf8_to_wide(want));
    }
    return true;
}

void PixlWizSelectorController::update_meta_text(HWND                  h_meta_edit,
                                                  PixlWizSelectorState& st,
                                                  const std::string&    model_id) const {
    st.selected_model_url.clear();
    if (!h_meta_edit) return;
    if (model_id.empty()) {
        ::SetWindowTextW(h_meta_edit, L"");
        return;
    }
    const auto it = st.index_by_id.find(model_id);
    if (it == st.index_by_id.end() || it->second >= st.models.size()) {
        const auto& tr = pixlwiz_strings_for(display_language_);
        std::string t  = "id: " + model_id + "\r\n(" + tr.select_refresh + ")";
        ::SetWindowTextW(h_meta_edit, utf8_to_wide(t).c_str());
        return;
    }
    const PixlWizModelInfo& m = st.models[it->second];
    st.selected_model_url     = m.open_url;
    std::string text;
    if (!m.name.empty() && m.name != m.id) text = "name: " + m.name + "\r\n";
    text += "id: " + m.id + "\r\n";
    if (!m.description.empty()) text += "---\r\n" + m.description;
    if (text.size() > 12000) text.resize(12000);
    ::SetWindowTextW(h_meta_edit, utf8_to_wide(text).c_str());
}

std::string PixlWizSelectorController::selected_model_id(const pmui::widgets::SearchableCombo& combo) const
{
    return wide_to_utf8(combo.get_value());
}

} // namespace pmui
