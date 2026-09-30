#pragma once

#include "polymech_export.h"

#include <string>

namespace media {

/**
 * Snapshot of one row in `settings.json` → `providers` (api_key, base_url, default_model).
 */
struct ActiveImageProviderFromApp {
    std::string api_key;
    std::string base_url;
    std::string default_model;
};

/// Loads `providers[chat.image_provider]` when `chat.image_provider` is set (Windows + portable).
/// @p provider_name_out receives the matched map key. Stub behavior: returns false when chat has no image provider.
POLYMECH_API bool try_load_active_image_provider_from_app(ActiveImageProviderFromApp& out,
                                                         std::string& provider_name_out);

/// If @p base_url is empty, fill from `providers[chat.image_provider]` when available.
POLYMECH_API void fill_image_provider_base_url_from_app(std::string& base_url);

/// Fills empty @p api_key / @p base_url from `providers[provider]` (case-insensitive key match).
/// @p provider must be non-empty (caller's explicit or chat-defaulted id). Does not read env vars.
POLYMECH_API void fill_image_provider_credentials_from_app(const std::string& provider, bool dry_run,
                                                           std::string& api_key, std::string& base_url);

} // namespace media
