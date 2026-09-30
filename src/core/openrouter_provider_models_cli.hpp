#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace media::openrouter_cli {

/// Disk + in-process cache TTL for OpenRouter `/v1/models` JSON (seconds).
constexpr std::int64_t kOpenRouterModelsCacheTtlSeconds = 86400; // 1 day

/// HTTP GET `GET {base}/models` (OpenAI-compatible `{"data":[{...}]}`) with optional cache.
/// @param base_url Root like `https://openrouter.ai/api/v1` or full `.../v1/models`, or empty for default.
/// @param api_key Optional; if non-empty, sends `Authorization: Bearer …` (public list works without a key).
/// The JSON in @p json_out matches the live API body (model objects include @c top_provider, @c architecture, @c
/// supported_parameters, @c context_length, @c pricing, etc., for later UI use).
bool list_models_openrouter_http(const std::string& api_key,
                                 const std::string& base_url,
                                 std::string&       json_out,
                                 std::string&       err_out,
                                 bool               force_refresh = false);

/// One row from OpenRouter `GET /v1/models` for UIs (Win32 `OpenRouterModelInfo` / native Swift bridge).
struct OpenRouterCatalogModelRow {
    std::string id;
    std::string name;
    std::string description;
    std::string open_url;
    std::string detail_head;
};

/// Fetches (with disk cache, same as `list_models_openrouter_http`) and parses model metadata for pickers.
bool list_openrouter_catalog_models(const std::string& api_key,
                                    const std::string& base_url,
                                    std::vector<OpenRouterCatalogModelRow>& out,
                                    std::string& err_out,
                                    bool force_refresh = false);

} // namespace media::openrouter_cli
