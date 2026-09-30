#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace media::replicate_cli {

/// Disk + in-process cache time-to-live for Replicate model/collection metadata (seconds).
constexpr std::int64_t kReplicateModelsCacheTtlSeconds = 259200; // 3 days

struct CollectionInfo {
    std::string name;
    std::string slug;
    std::string description;
};

struct ModelInfo {
    std::string slug;
    std::string description;
    std::string url;
    std::string visibility;
    bool is_official = false;
};

std::string normalize_replicate_base_url(const std::string& base_url);

bool fetch_collections(const std::string& api_key,
                       const std::string& base_url,
                       std::vector<CollectionInfo>& out_collections,
                       std::string& err_out,
                       bool force_refresh = false);

bool fetch_collection_models(const std::string& api_key,
                             const std::string& base_url,
                             const std::string& collection_slug,
                             std::vector<ModelInfo>& out_models,
                             std::string& err_out,
                             bool force_refresh = false);

bool resolve_collection_for_model_cached(const std::string& model_slug,
                                         std::string& collection_slug_out);

void sort_collections_alpha(std::vector<CollectionInfo>& collections);
void sort_models_alpha(std::vector<ModelInfo>& models);

/// HTTP GET for `pm-image provider models list` (Replicate collections or models API).
/// When @p base_url is empty, uses the default official collection endpoint.
/// Uses the same on-disk cache as @ref fetch_collection_models when the request matches
/// cached "official" collection data and the cache is within @ref kReplicateModelsCacheTtlSeconds.
bool list_models_replicate_http(const std::string& api_key,
                                const std::string& base_url,
                                int limit,
                                const std::string& cursor,
                                const std::string& sort_by,
                                const std::string& sort_direction,
                                std::string& json_out,
                                std::string& err_out,
                                bool force_refresh = false);

/// Parsed from `replicate-models-cache.json` → `latest_version.openapi_schema` (Cog OpenAPI 3).
struct ReplicateVideoInputFieldPlan {
    std::string first_still_key;
    std::string second_still_key;
    std::string reference_array_key;
    bool        use_image_input_array = false;
    bool        openapi_found         = false;

    bool valid() const
    {
        return openapi_found && ((!first_still_key.empty()) || use_image_input_array);
    }
};

/// Load cache, find @p model_slug (`owner/name`), flatten `components.schemas.Input.properties`, infer video still keys.
/// On success, @p flattened_input_properties_out maps property name → simplified schema (type, format, description, enum, …).
bool lookup_replicate_video_openapi_input(const std::string& model_slug,
                                          nlohmann::json& flattened_input_properties_out,
                                          ReplicateVideoInputFieldPlan& plan_out,
                                          std::string& err_out);

/// Same cache lookup as @ref lookup_replicate_video_openapi_input but **does not** infer frame keys;
/// succeeds for any model whose OpenAPI defines non-empty `components.schemas.Input.properties`.
bool lookup_replicate_openapi_input_flat(const std::string& model_slug,
                                         nlohmann::json& flattened_input_properties_out,
                                         nlohmann::json& input_required_array_out,
                                         std::string& err_out);

/// JSON Schema object suitable for a tool `parameters` field: mirrors flattened OpenAPI `Input` fields.
nlohmann::json openapi_flat_to_create_tool_parameters(const nlohmann::json& flat_input_properties,
                                                      const nlohmann::json& input_required_array);

} // namespace media::replicate_cli
