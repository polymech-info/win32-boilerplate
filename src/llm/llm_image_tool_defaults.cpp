#include "llm_image_tool_defaults.hpp"

#include "core/settings_runtime.hpp"

#include <string>

namespace media::llm {

void apply_chat_image_defaults_for_tool_options(std::string& provider, std::string& model,
                                                const nlohmann::json& options_json) {
    const bool have_provider = options_json.contains("provider") && options_json["provider"].is_string();
    const bool have_model    = options_json.contains("model") && options_json["model"].is_string();
    const bool user_set_provider = have_provider;
    const bool user_set_model    = have_model;

    std::string err;
    media::runtime_settings::ChatProviderSettings cs;
    if (media::runtime_settings::load_chat_provider(cs, err)) {
        if (!user_set_provider && provider.empty() && !cs.image_provider.empty())
            provider = cs.image_provider;
        if (!user_set_model && model.empty() && !cs.image_model.empty())
            model = cs.image_model;
    }
}

void apply_chat_image_recognition_defaults_for_tool_options(std::string& provider, std::string& model,
                                                            const nlohmann::json& options_json) {
    const bool have_provider = options_json.contains("provider") && options_json["provider"].is_string();
    const bool have_model    = options_json.contains("model") && options_json["model"].is_string();
    const bool user_set_provider = have_provider;
    const bool user_set_model    = have_model;

    std::string err;
    media::runtime_settings::ChatProviderSettings cs;
    if (media::runtime_settings::load_chat_provider(cs, err)) {
        if (!user_set_provider && provider.empty() && !cs.image_recognition_provider.empty())
            provider = cs.image_recognition_provider;
        if (!user_set_model && model.empty() && !cs.image_recognition_model.empty())
            model = cs.image_recognition_model;
    }
}

void apply_chat_video_defaults_for_tool_options(std::string& provider, std::string& model,
                                                const nlohmann::json& options_json) {
    const bool have_provider = options_json.contains("provider") && options_json["provider"].is_string();
    const bool have_model    = options_json.contains("model") && options_json["model"].is_string();
    const bool user_set_provider = have_provider;
    const bool user_set_model    = have_model;

    std::string err;
    media::runtime_settings::ChatProviderSettings cs;
    if (!media::runtime_settings::load_chat_provider(cs, err))
        cs = {};
    if (!user_set_provider && provider.empty() && !cs.video_provider.empty())
        provider = cs.video_provider;
    if (!user_set_model && model.empty() && !cs.video_model.empty())
        model = cs.video_model;
}

void repair_meta_google_model_if_replicate_slug(std::string& provider, std::string& model) {
    (void)provider;
    (void)model;
}

void ensure_api_key_for_image_provider(const std::string& provider, std::string& api_key) {
    if (!api_key.empty())
        return;
    std::string base_url;
    media::runtime_settings::merge_provider_credentials(provider, false, api_key, base_url);
}

void ensure_base_url_for_image_provider(const std::string& provider, std::string& base_url) {
    if (!base_url.empty())
        return;
    if (provider.empty())
        return;
    std::string api_key;
    media::runtime_settings::merge_provider_credentials(provider, false, api_key, base_url);
}

} // namespace media::llm
