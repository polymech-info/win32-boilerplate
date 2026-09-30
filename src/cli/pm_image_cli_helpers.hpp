#ifndef PM_IMAGE_CLI_HELPERS_HPP
#define PM_IMAGE_CLI_HELPERS_HPP

#include <string>
#include <vector>

#include "core/settings_runtime.hpp"
#include "core/transform.hpp"

namespace media_cli {

std::string join_src_semicolons(const std::vector<std::string> &v);

// Valid names must match `media::llm::path::tool_catalog` entries.
void parse_agent_disable_tools(const std::string &raw, bool no_tools, std::vector<std::string> &out);

bool resolve_transform_preset_id(const std::string &id_in, media::TransformOptions &topts, std::string &err);

void trim_surrounding_quotes(std::string &s);

void apply_image_ai_cli_defaults_from_app(std::string &provider, std::string &model, bool user_set_provider,
                                          bool user_set_model);

void apply_image_recognition_cli_defaults_from_app(std::string &provider, std::string &model,
                                                   bool user_set_provider, bool user_set_model);

/// After CLI `--api-key` (if any) is applied: resolve api_key/base_url from app settings only (no environment).
/// No-op when @p dry_run is true. Delegates to `media::fill_image_provider_credentials_from_app`.
void apply_image_ai_credentials_from_app(const std::string &provider, bool dry_run, std::string &api_key,
                                         std::string &base_url);

/// Fills empty chat/text LLM credentials from `settings.json["providers"][router]`.
/// The app does not save API keys or base URLs into the `chat` object. No environment.
void fill_chat_llm_credentials_from_app_settings(std::string &api_key, std::string &base_url,
                                                 const std::string &router);

/// Compatibility wrapper for callers that only need the key.
void fill_chat_llm_api_key_from_app_settings(std::string &api_key, const std::string &router);

int fail_image_ai_requires_provider_model(const char *subcmd, const std::string &provider,
                                         const std::string &model);

} // namespace media_cli

#endif
