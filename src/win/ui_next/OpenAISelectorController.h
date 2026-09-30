#pragma once

#include <Windows.h>
#include "widgets/SearchableCombo.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pmui {

struct OpenAIModelInfo {
    std::string id;
    std::string owned_by;
};

struct OpenAISelectorState {
    std::vector<OpenAIModelInfo>            models;
    std::unordered_map<std::string, size_t> index_by_id;
};

/// Fetches, caches (24 h disk cache), and populates a SearchableCombo with
/// OpenAI models from `GET {base_url}/models`.
///
/// Usage:
///   OpenAISelectorController ctl;
///   ctl.set_display_language("en");
///   // on dialog init or router switch to "openai":
///   ctl.reload(api_key, base_url, hModel, st, cur_model, /*force*/false, &err);
///   // on Save:
///   std::string model = ctl.selected_model_id(hModel);
class OpenAISelectorController {
public:
    void set_display_language(std::string_view display_language);

    /// Fetch + populate combo.  Pass `force_refresh = true` on manual refresh.
    bool reload(const std::string&              api_key,
                const std::string&              base_url,
                pmui::widgets::SearchableCombo& combo,
                OpenAISelectorState&            st,
                const std::string&              keep_model_id,
                bool                            force_refresh,
                std::wstring*                   err_out) const;

    /// Show model's owned_by in the meta edit (pass nullptr to skip).
    void update_meta_text(HWND h_meta_edit,
                          const OpenAISelectorState& st,
                          const std::string&         model_id) const;

    std::string selected_model_id(const pmui::widgets::SearchableCombo& combo) const;

private:
    std::string display_language_{"en"};
};

} // namespace pmui
