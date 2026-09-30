#pragma once

#include <string_view>

namespace pmui::provider_dlg_i18n {

struct Strings {
    const wchar_t* window_title;
    const wchar_t* lbl_api_key;
    const wchar_t* btn_show;
    const wchar_t* btn_hide;
    const wchar_t* lbl_base_url;
    /** `note_path_pre` + k_config_subpath + `note_path_post` (DPAPI + settings path) */
    const wchar_t* note_path_pre;
    const wchar_t* note_path_post;
    const wchar_t* btn_save;
    const wchar_t* btn_cancel;
    const wchar_t* msg_save_fail;
    const wchar_t* msg_load_fail;
};

[[nodiscard]] const Strings& strings_for(std::string_view display_language);

} // namespace pmui::provider_dlg_i18n
