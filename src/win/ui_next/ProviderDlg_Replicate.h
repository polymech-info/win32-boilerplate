#pragma once

#include "core/replicate_provider_models_cli.hpp"

namespace pmui {
namespace provider_dlg_replicate {

using CollectionInfo = media::replicate_cli::CollectionInfo;
using ModelInfo      = media::replicate_cli::ModelInfo;

using media::replicate_cli::normalize_replicate_base_url;
using media::replicate_cli::fetch_collections;
using media::replicate_cli::fetch_collection_models;
using media::replicate_cli::resolve_collection_for_model_cached;
using media::replicate_cli::sort_collections_alpha;
using media::replicate_cli::sort_models_alpha;

} // namespace provider_dlg_replicate
} // namespace pmui
