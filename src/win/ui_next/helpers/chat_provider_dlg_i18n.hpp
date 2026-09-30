#pragma once

#include <string_view>

namespace pmui::chat_provider_dlg_i18n {

struct Strings {
    const wchar_t* window_title;
    const wchar_t* lbl_router;
    const wchar_t* lbl_model;
    const wchar_t* lbl_api_key;
    const wchar_t* lbl_base_url;
    const wchar_t* lbl_max_iter;
    const wchar_t* lbl_image_provider;
    const wchar_t* lbl_image_model;
    const wchar_t* lbl_video_provider;
    const wchar_t* lbl_video_model;
    const wchar_t* lbl_recognition_provider;
    const wchar_t* lbl_recognition_model;
    const wchar_t* lbl_collection;
    const wchar_t* group_image_creation;
    const wchar_t* group_video_generation;
    const wchar_t* group_image_recognition;
    const wchar_t* group_chat;
    const wchar_t* group_agent;
    const wchar_t* group_audio;
    const wchar_t* lbl_stt_provider;
    const wchar_t* lbl_stt_model;
    const wchar_t* lbl_tts_provider;
    const wchar_t* lbl_tts_model;
    const wchar_t* lbl_tts_voice;
    const wchar_t* btn_refresh;
    const wchar_t* hint_default_prefix;
    const wchar_t* hint_custom;
    /// `note_path_pre` + k_config_subpath + `note_path_post` (%APPDATA%…\\settings.json…)
    const wchar_t* note_path_pre;
    const wchar_t* note_path_post;
    const wchar_t* btn_show;
    const wchar_t* btn_hide;
    const wchar_t* btn_save;
    const wchar_t* btn_cancel;
    const wchar_t* msg_save_fail;
};

[[nodiscard]] const Strings& strings_for(std::string_view display_language);

} // namespace pmui::chat_provider_dlg_i18n
