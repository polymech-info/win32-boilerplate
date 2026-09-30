#include "stdafx.h"
#include "OpenRouterSelectorController.h"
#include "core/openrouter_provider_models_cli.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace pmui {
namespace {

struct OpenRouterSelectorStrings {
    const char* context;
    const char* tokens;
    const char* modality;
    const char* in_label;
    const char* out_label;
    const char* tools;
    const char* function_tools;
    const char* supported_suffix;
    const char* not_listed;
    const char* select_refresh;
    const wchar_t* err_no_model_combo;
    const wchar_t* err_bad_model_list;
};

static std::string norm_lang(std::string_view v) {
    std::string s(v);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static const OpenRouterSelectorStrings& openrouter_strings_for(std::string_view display_language) {
    static const OpenRouterSelectorStrings en{
        "Context", "tokens", "Modality", "In", "Out", "Tools", "function/tools",
        " (supported on OpenRouter)", "not listed",
        "Select Refresh to load the catalog, or type a model id the provider returned.",
        L"OpenRouter: internal error (no model combo).",
        L"OpenRouter: model list is not a JSON object with a data array.",
    };
    static const OpenRouterSelectorStrings es{
        "Contexto", "tokens", "Modalidad", "Entrada", "Salida", "Herramientas", "funciones/herramientas",
        " (compatible en OpenRouter)", "no indicado",
        "Seleccione Actualizar para cargar el catálogo, o escriba un id de modelo devuelto por el proveedor.",
        L"OpenRouter: error interno (sin combo de modelos).",
        L"OpenRouter: la lista de modelos no es un objeto JSON con un array data.",
    };
    static const OpenRouterSelectorStrings de{
        "Kontext", "Token", "Modalität", "Ein", "Aus", "Tools", "Funktionen/Tools",
        " (von OpenRouter unterstützt)", "nicht aufgeführt",
        "Wählen Sie Aktualisieren, um den Katalog zu laden, oder geben Sie eine vom Anbieter zurückgegebene Modell-ID ein.",
        L"OpenRouter: interner Fehler (keine Modell-Auswahl).",
        L"OpenRouter: Modellliste ist kein JSON-Objekt mit data-Array.",
    };
    static const OpenRouterSelectorStrings it{
        "Contesto", "token", "Modalità", "Input", "Output", "Strumenti", "funzioni/strumenti",
        " (supportato su OpenRouter)", "non indicato",
        "Selezionare Aggiorna per caricare il catalogo oppure digitare un id modello restituito dal provider.",
        L"OpenRouter: errore interno (combo modelli mancante).",
        L"OpenRouter: l'elenco modelli non è un oggetto JSON con array data.",
    };
    static const OpenRouterSelectorStrings fr{
        "Contexte", "tokens", "Modalité", "Entrée", "Sortie", "Outils", "fonctions/outils",
        " (pris en charge sur OpenRouter)", "non listé",
        "Sélectionnez Actualiser pour charger le catalogue, ou saisissez un id de modèle renvoyé par le fournisseur.",
        L"OpenRouter : erreur interne (liste de modèles absente).",
        L"OpenRouter : la liste des modèles n'est pas un objet JSON avec un tableau data.",
    };
    const std::string c = norm_lang(display_language);
    if (c == "es") return es;
    if (c == "de") return de;
    if (c == "it") return it;
    if (c == "fr") return fr;
    return en;
}

static std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string join_str_array(const nlohmann::json& arr) {
    if (!arr.is_array()) return "";
    std::string out;
    for (const auto& v : arr) {
        if (!v.is_string()) continue;
        if (!out.empty()) out += ", ";
        out += v.get<std::string>();
    }
    return out;
}

static bool has_supported(const nlohmann::json& m, const char* p) {
    if (!m.contains("supported_parameters") || !m["supported_parameters"].is_array()) return false;
    for (const auto& x : m["supported_parameters"]) {
        if (x.is_string() && x.get<std::string>() == p) return true;
    }
    return false;
}

static std::optional<std::int64_t> context_tokens(const nlohmann::json& m) {
    if (m.contains("context_length") && m["context_length"].is_number_integer()) return m["context_length"].get<std::int64_t>();
    if (m.contains("top_provider") && m["top_provider"].is_object() && m["top_provider"].contains("context_length")
        && m["top_provider"]["context_length"].is_number_integer())
        return m["top_provider"]["context_length"].get<std::int64_t>();
    return std::nullopt;
}

static std::string format_detail_head(const nlohmann::json& m, const OpenRouterSelectorStrings& tr) {
    std::ostringstream o;
    if (const auto ctx = context_tokens(m)) o << tr.context << ": " << *ctx << " " << tr.tokens << "\n";
    if (m.contains("architecture") && m["architecture"].is_object()) {
        const auto& a  = m["architecture"];
        const std::string in  = a.contains("input_modalities") ? join_str_array(a["input_modalities"]) : std::string{};
        const std::string out = a.contains("output_modalities") ? join_str_array(a["output_modalities"]) : std::string{};
        if (a.contains("modality") && a["modality"].is_string()) o << tr.modality << ": " << a["modality"].get<std::string>() << "\n";
        o << tr.in_label << ": " << (in.empty() ? "-" : in) << "  |  " << tr.out_label << ": " << (out.empty() ? "-" : out) << "\n";
    }
    std::string tool_line;
    const bool  tools  = has_supported(m, "tools");
    const bool  tch    = has_supported(m, "tool_choice");
    if (tools || tch) {
        o << tr.tools << ": ";
        if (tools) o << tr.function_tools;
        if (tools && tch) o << ", ";
        if (tch) o << "tool_choice";
        o << tr.supported_suffix << "\n";
    } else {
        o << tr.tools << ": " << tr.not_listed << "\n";
    }
    return o.str();
}

static std::string make_open_url(const std::string& id) {
    if (id.empty()) return "https://openrouter.ai";
    // Public model pages: https://openrouter.ai/<provider/model...>
    return "https://openrouter.ai/" + id;
}

static void sort_models_by_name(std::vector<OpenRouterModelInfo>& models) {
    std::sort(models.begin(), models.end(), [](const OpenRouterModelInfo& a, const OpenRouterModelInfo& b) {
        return to_lower(a.name) < to_lower(b.name);
    });
}

} // namespace

void OpenRouterSelectorController::set_display_language(std::string_view display_language) {
    display_language_ = std::string(display_language.empty() ? std::string_view("en") : display_language);
}

bool OpenRouterSelectorController::reload(const std::string&              api_key,
                                          const std::string&              base_url,
                                          pmui::widgets::SearchableCombo& combo,
                                          OpenRouterSelectorState&         st,
                                          const std::string&              keep_model_id,
                                          const bool                      force_refresh,
                                          std::wstring*                   err_out) const {
    if (err_out) err_out->clear();
    st.models.clear();
    st.index_by_id.clear();
    st.selected_model_url.clear();
    const auto& tr = openrouter_strings_for(display_language_);
    if (!combo.get()) {
        if (err_out) *err_out = tr.err_no_model_combo;
        return false;
    }

    std::string json, err;
    if (!media::openrouter_cli::list_models_openrouter_http(api_key, base_url, json, err, force_refresh)) {
        if (err_out) *err_out = utf8_to_wide(err);
        return false;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        if (err_out) *err_out = utf8_to_wide(std::string("OpenRouter: ") + e.what());
        return false;
    }
    nlohmann::json* data = nullptr;
    if (root.is_object() && root["data"].is_array()) data = &root["data"];
    if (!data) {
        if (err_out) *err_out = tr.err_bad_model_list;
        return false;
    }
    for (const auto& el : *data) {
        if (!el.is_object()) continue;
        if (!el.contains("id") || !el["id"].is_string()) continue;
        OpenRouterModelInfo mi;
        mi.id   = el["id"].get<std::string>();
        mi.name = el.contains("name") && el["name"].is_string() ? el["name"].get<std::string>() : mi.id;
        mi.description =
            el.contains("description") && el["description"].is_string() ? el["description"].get<std::string>() : "";
        mi.open_url    = make_open_url(mi.id);
        mi.detail_head = format_detail_head(el, tr);
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

void OpenRouterSelectorController::update_meta_text(HWND    h_meta_edit,
                                                    OpenRouterSelectorState& st,
                                                    const std::string&  model_id) const {
    st.selected_model_url.clear();
    if (!h_meta_edit) return;
    if (model_id.empty()) {
        ::SetWindowTextW(h_meta_edit, L"");
        return;
    }
    const auto it = st.index_by_id.find(model_id);
    if (it == st.index_by_id.end() || it->second >= st.models.size()) {
        const auto& tr = openrouter_strings_for(display_language_);
        std::string t = "id: " + model_id + "\r\n(" + tr.select_refresh + ")";
        ::SetWindowTextW(h_meta_edit, utf8_to_wide(t).c_str());
        return;
    }
    const OpenRouterModelInfo& m = st.models[it->second];
    st.selected_model_url        = m.open_url;
    std::string                 text;
    if (!m.name.empty() && m.name != m.id) text = "name: " + m.name + "\r\n";
    text += "id: " + m.id + "\r\n";
    text += m.detail_head;
    text += "---\r\n";
    if (!m.description.empty()) text += m.description;
    if (text.size() > 12000) text.resize(12000);
    ::SetWindowTextW(h_meta_edit, utf8_to_wide(text).c_str());
}

std::string OpenRouterSelectorController::selected_model_id(const pmui::widgets::SearchableCombo& combo) const
{
    return wide_to_utf8(combo.get_value());
}

} // namespace pmui
