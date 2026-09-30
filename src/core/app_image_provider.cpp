#include "app_image_provider.hpp"

#include "settings_runtime.hpp"

#include <cctype>

namespace media {

namespace {

std::string ascii_lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

media::runtime_settings::ProviderMap::const_iterator find_provider_ci(
    const media::runtime_settings::ProviderMap& pm, const std::string& name) {
    const std::string nl = ascii_lower(name);
    for (auto it = pm.begin(); it != pm.end(); ++it) {
        if (ascii_lower(it->first) == nl)
            return it;
    }
    return pm.end();
}

} // namespace

bool try_load_active_image_provider_from_app(ActiveImageProviderFromApp& out, std::string& provider_name_out) {
    out               = ActiveImageProviderFromApp{};
    provider_name_out.clear();
    std::string err;
    media::runtime_settings::ChatProviderSettings cs;
    if (!media::runtime_settings::load_chat_provider(cs, err) || cs.image_provider.empty())
        return false;
    media::runtime_settings::ProviderMap pm;
    if (!media::runtime_settings::load_providers(pm, err))
        return false;
    const auto it = find_provider_ci(pm, cs.image_provider);
    if (it == pm.end())
        return false;
    out.api_key       = it->second.api_key;
    out.base_url      = it->second.base_url;
    out.default_model = it->second.default_model;
    provider_name_out = it->first;
    return true;
}

void fill_image_provider_base_url_from_app(std::string& base_url) {
    if (!base_url.empty())
        return;
    ActiveImageProviderFromApp o;
    std::string                n;
    if (try_load_active_image_provider_from_app(o, n) && !o.base_url.empty())
        base_url = o.base_url;
}

void fill_image_provider_credentials_from_app(const std::string& provider, bool dry_run, std::string& api_key,
                                              std::string& base_url) {
    if (dry_run || provider.empty())
        return;

    media::runtime_settings::merge_provider_credentials(provider, dry_run, api_key, base_url);
}

} // namespace media
