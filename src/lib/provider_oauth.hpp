#pragma once

#include <string>

namespace media::provider_oauth {

// Supported provider ids: openrouter, openai, github.
std::string normalize_provider_id(std::string provider_id);
bool        is_supported_provider(const std::string& provider_id);

// Reads token from oauth-tokens.json. Returns false when missing/unavailable.
bool read_access_token(const std::string& provider_id, std::string& token_out, std::string& err);
bool has_access_token(const std::string& provider_id, bool& has_token_out, std::string& err);
bool clear_access_token(const std::string& provider_id, std::string& err);

// Starts interactive login for provider:
// - openrouter/openai: opens browser to API key page (no token write).
// - github: starts device-code flow worker (writes token on success).
bool start_login_async(const std::string& provider_id, std::string& message_out, std::string& err);

// Runtime status for settings UI.
bool login_in_progress(const std::string& provider_id);
void read_last_login_message(const std::string& provider_id, std::string& info_out, std::string& error_out);

} // namespace media::provider_oauth
