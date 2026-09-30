#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_provider_models.hpp"

int pm_image_cmd_provider_models(CLI::App& app, PmImageCliState& st) {
    std::string provider = st.pm_provider;
    for (auto& c : provider) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (provider != "replicate" && provider != "openrouter" && provider != "pixlwiz") {
        std::cerr << "provider models list: unsupported provider: " << st.pm_provider
                  << " (currently supported: replicate, openrouter, pixlwiz)\n";
        return 1;
    }
    std::string payload;
    std::string err;
    if (provider == "openrouter") {
        media_cli::apply_image_ai_credentials_from_app("openrouter", false, st.pm_api_key, st.pm_base_url);
        if (!media::openrouter_cli::list_models_openrouter_http(st.pm_api_key, st.pm_base_url, payload, err, false)) {
            std::cerr << err << "\n";
            return 1;
        }
    } else if (provider == "pixlwiz") {
        media_cli::apply_image_ai_credentials_from_app("pixlwiz", false, st.pm_api_key, st.pm_base_url);
        if (!media::pixlwiz_cli::list_models_pixlwiz_http(st.pm_api_key, st.pm_base_url, payload, err, false)) {
            std::cerr << err << "\n";
            return 1;
        }
    } else {
        media_cli::apply_image_ai_credentials_from_app("replicate", false, st.pm_api_key, st.pm_base_url);
        if (st.pm_api_key.empty()) {
            std::cerr << "provider models list: API key required (configure the Replicate provider in app settings, or pass --api-key)\n";
            return 1;
        }
        if (!media::replicate_cli::list_models_replicate_http(
                st.pm_api_key, st.pm_base_url, st.pm_limit, st.pm_cursor, st.pm_sort_by, st.pm_sort_direction, payload,
                err, false)) {
            std::cerr << err << "\n";
            return 1;
        }
    }
    if (payload.empty()) {
        std::cerr << "provider models list: fetched empty payload\n";
        return 1;
    }
    std::cout.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    std::cout.put('\n');
    std::cout.flush();
    if (!std::cout.good()) {
        std::cerr << "provider models list: failed writing payload to stdout\n";
        return 1;
    }
    return 0;
}

void pm_image_register_provider_models(CLI::App& app, PmImageCliState& s) {
    s.provider_cmd = app.add_subcommand("provider", "Provider utilities (model catalog, etc.)");
    s.provider_cmd->require_subcommand(1);
    s.provider_models_cmd = s.provider_cmd->add_subcommand("models", "Provider model catalog operations");
    s.provider_models_cmd->require_subcommand(1);
    s.provider_models_list_cmd = s.provider_models_cmd->add_subcommand("list",
        "List provider models and return full JSON payload");
    s.provider_models_list_cmd->add_option("--provider", s.pm_provider, "Provider id (replicate|openrouter)")->default_val("replicate");
    s.provider_models_list_cmd->add_option("--api-key", s.pm_api_key,
        "API key (Replicate: required. OpenRouter: optional for public /v1/models; from app if set for openrouter)");
    s.provider_models_list_cmd->add_option(
        "--base-url", s.pm_base_url,
        "Replicate: catalog URL (default official collection). OpenRouter: API root (default https://openrouter.ai/api/v1)");
    s.provider_models_list_cmd->add_option("--limit", s.pm_limit, "Replicate /v1/models: optional page size (ignored for openrouter)");
    s.provider_models_list_cmd->add_option("--cursor", s.pm_cursor, "Replicate: optional pagination cursor (ignored for openrouter)");
    s.provider_models_list_cmd->add_option("--sort-by", s.pm_sort_by, "Replicate: optional sort field (ignored for openrouter)");
    s.provider_models_list_cmd->add_option(
        "--sort-direction", s.pm_sort_direction, "Replicate: optional sort direction (asc|desc) (ignored for openrouter)");
}
