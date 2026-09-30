#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace media::pixlwiz_cli {

/// Disk + in-process cache TTL for PixlWiz `/v1/models` JSON (seconds).
constexpr std::int64_t kPixlWizModelsCacheTtlSeconds = 86400; // 1 day

/// One row from PixlWiz `GET /v1/models` for UIs.
struct PixlWizModelRow {
    std::string id;
    std::string name;
    std::string description;
    std::string open_url;
};

/// HTTP GET `{base_url}/models` with optional disk/in-memory cache.
/// @param api_key  Optional; if non-empty, sends `Authorization: Bearer …`.
/// @param base_url Root like `https://api.pixlwiz.com/v1` or full `.../models`, or empty for default.
/// Raw JSON body is returned in @p json_out on success.
bool list_models_pixlwiz_http(const std::string& api_key,
                               const std::string& base_url,
                               std::string&       json_out,
                               std::string&       err_out,
                               bool               force_refresh = false);

/// Fetches (with disk cache) and parses model rows for pickers.
bool list_pixlwiz_catalog_models(const std::string&             api_key,
                                  const std::string&             base_url,
                                  std::vector<PixlWizModelRow>&  out,
                                  std::string&                   err_out,
                                  bool                           force_refresh = false);

} // namespace media::pixlwiz_cli
