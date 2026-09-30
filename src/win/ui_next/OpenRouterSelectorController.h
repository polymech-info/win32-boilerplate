#pragma once

#include <Windows.h>
#include "widgets/SearchableCombo.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pmui {

struct OpenRouterModelInfo {
    std::string id;
    std::string name;
    /// Short capability line (context, modalities, tools) plus full @ref description in the meta panel.
    std::string description;
    /// Browser page for this model on openrouter.ai.
    std::string open_url;
    /// Pre-formatted first block (context, in/out, tools) — remainder is @ref description.
    std::string detail_head;
};

struct OpenRouterSelectorState {
    std::vector<OpenRouterModelInfo>          models;
    std::unordered_map<std::string, size_t>  index_by_id;
    std::string                                selected_model_url;
};

class OpenRouterSelectorController {
public:
    void set_display_language(std::string_view display_language);

    bool reload(const std::string&             api_key,
                const std::string&             base_url,
                pmui::widgets::SearchableCombo& combo,
                OpenRouterSelectorState&        st,
                const std::string&             keep_model_id,
                bool                           force_refresh,
                std::wstring*                  err_out) const;

    void        update_meta_text(HWND h_meta_edit, OpenRouterSelectorState& st, const std::string& model_id) const;
    std::string selected_model_id(const pmui::widgets::SearchableCombo& combo) const;

private:
    std::string display_language_{"en"};
};

} // namespace pmui
