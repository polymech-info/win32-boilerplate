#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace media::llm {

/// `settings.json` key `chat` (`image_provider` / `image_model`) only.
/// Does not use top-level `active_provider` or provider-row `default_model`.
void apply_chat_image_defaults_for_tool_options(std::string&           provider,
                                                std::string&           model,
                                                const nlohmann::json&  options_json);

/// `settings.json` key `chat` (`image_recognition_provider` / `image_recognition_model`).
/// Does not use provider-row `default_model`; vision/meta model selection must be explicit.
void apply_chat_image_recognition_defaults_for_tool_options(std::string&          provider,
                                                            std::string&          model,
                                                            const nlohmann::json& options_json);

/// `chat.video_provider` / `chat.video_model` only. Video model selection must be explicit.
void apply_chat_video_defaults_for_tool_options(std::string&          provider,
                                                std::string&          model,
                                                const nlohmann::json& options_json);

void ensure_api_key_for_image_provider(const std::string& provider, std::string& api_key);
void ensure_base_url_for_image_provider(const std::string& provider, std::string& base_url);

void repair_meta_google_model_if_replicate_slug(std::string& provider, std::string& model);

} // namespace media::llm
