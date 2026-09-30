#pragma once

#include <string_view>

namespace pmui::app_settings_i18n {

/** UI strings for the modal App Settings dialog (View → App Settings), keyed by `appearance.display_language`. */
struct Strings {
    const wchar_t* window_title;

    const wchar_t* lbl_display_language;
    const wchar_t* lang_combo[5];

    const wchar_t* lbl_theme;
    const wchar_t* theme_system;
    const wchar_t* theme_light;
    const wchar_t* theme_dark;

    const wchar_t* lbl_font_size;
    const wchar_t* font_sz[5];

    const wchar_t* note_paragraph;

    const wchar_t* btn_ai_provider;
    const wchar_t* btn_chat_provider;
    const wchar_t* btn_export;
    const wchar_t* btn_import;

    const wchar_t* chk_encrypt;

   
    const wchar_t* chk_filetree_shell_frames;

    /** Used when FEATURE_LICENSE_FILE; otherwise ignored. */
    const wchar_t* fp_label;

    const wchar_t* btn_copy;

    const wchar_t* btn_save;
    const wchar_t* btn_cancel;

    const wchar_t* msg_export_fail;
    const wchar_t* msg_export_ok;
    const wchar_t* msg_import_confirm;
    const wchar_t* msg_import_fail;
    const wchar_t* msg_import_ok;

    const wchar_t* msg_fp_copied;
    const wchar_t* msg_fp_copy_fail;

    const wchar_t* msg_save_fail;
    /// MessageBox body = `msg_restart_pre` + k_app_id_w + `msg_restart_post`
    const wchar_t* msg_restart_pre;
    const wchar_t* msg_restart_post;
    /// `msg_restart_fail_pre` + k_app_id_w + `msg_restart_fail_post`
    const wchar_t* msg_restart_fail_pre;
    const wchar_t* msg_restart_fail_post;
};

[[nodiscard]] const Strings& strings_for(std::string_view display_language);

} // namespace pmui::app_settings_i18n
