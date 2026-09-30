#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace media::openai_cli {

/// Disk + in-process cache TTL for OpenAI `/v1/models` JSON (seconds).
constexpr std::int64_t kOpenAIModelsCacheTtlSeconds = 86400; // 1 day

/// HTTP GET `{base}/models` with `Authorization: Bearer {api_key}`.
/// @param base_url Root like `https://api.openai.com/v1`, or empty for default.
/// @param api_key  Required for authenticated access (OpenAI always requires a key).
/// On success, @p json_out contains `{"data":[{"id":"...","owned_by":"..."},...]}`.
bool list_models_openai_http(const std::string& api_key,
                              const std::string& base_url,
                              std::string&       json_out,
                              std::string&       err_out,
                              bool               force_refresh = false);

/// Parsed row from `GET /v1/models` for UI pickers.
struct OpenAIModelRow {
    std::string id;
    std::string owned_by;
};

/// Fetches (with disk cache) and returns a sorted model list for pickers.
bool list_openai_models(const std::string&         api_key,
                        const std::string&         base_url,
                        std::vector<OpenAIModelRow>& out,
                        std::string&               err_out,
                        bool                       force_refresh = false);

} // namespace media::openai_cli
