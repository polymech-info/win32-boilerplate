#include "core/settings_runtime.hpp"

#include "core/settings_store.hpp"
#include "lib/pm_zitadel_oauth.hpp"
#include "lib/provider_oauth.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace media::runtime_settings {

namespace {

std::optional<fs::path> g_command_json_path_override;

std::string ascii_lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const ProviderEntry* find_provider_ci(const ProviderMap& providers, const std::string& provider) {
    if (provider.empty())
        return nullptr;
    const std::string needle = ascii_lower(provider);
    for (const auto& kv : providers) {
        if (ascii_lower(kv.first) == needle)
            return &kv.second;
    }
    return nullptr;
}

void dedupe_lower_trim(std::vector<std::string>& inout)
{
    std::unordered_set<std::string> seen;
    std::vector<std::string> out;
    out.reserve(inout.size());
    for (std::string s : inout) {
        size_t b = 0;
        while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
            ++b;
        size_t e = s.size();
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
            --e;
        s = s.substr(b, e - b);
        if (s.empty())
            continue;
        s = ascii_lower(std::move(s));
        if (seen.insert(s).second)
            out.push_back(std::move(s));
    }
    inout = std::move(out);
}

bool read_utf8_file(const fs::path& path, std::string& out_json, std::string& err)
{
    out_json.clear();
    err.clear();
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        err = "failed to open commands file: " + path.string();
        return false;
    }
    out_json.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

bool load_chat_provider(ChatProviderSettings& out, std::string& err) {
    return media::settings::load_chat_provider(out, err);
}

bool load_providers(ProviderMap& out, std::string& err) {
    return media::settings::load_providers(out, err);
}

bool load_command_json_utf8(std::string& out_json, std::string& err) {
    if (g_command_json_path_override.has_value())
        return read_utf8_file(g_command_json_path_override.value(), out_json, err);
    return media::settings::load_command_json_utf8(out_json, err);
}

bool save_command_json_utf8(const std::string& utf8_json, std::string& err) {
    return media::settings::save_command_json_utf8(utf8_json, err);
}

void set_command_json_path_override(const std::string& path) {
    if (path.empty()) {
        g_command_json_path_override.reset();
    } else {
        g_command_json_path_override = fs::absolute(fs::path(path));
    }
}

void merge_provider_credentials(const std::string& provider,
                                bool dry_run,
                                std::string& api_key,
                                std::string& base_url) {
    if (dry_run || provider.empty())
        return;

    // Pixlwiz: prefer the ZITADEL access token from zitadel-oauth.json over
    // the manually-entered settings.json api_key. The token is always fresh
    // after `pm-image login`; settings.json key acts as a fallback only.
    const std::string provider_lc = ascii_lower(provider);
    const bool is_pixlwiz = (provider_lc == "pixlwiz");
    if (is_pixlwiz && api_key.empty()) {
        std::string token, terr;
        if (pm_zitadel_oauth_read_access_token(token, terr) && !token.empty())
            api_key = std::move(token);
    }
    // Optional OAuth token fallback for providers that support web login.
    if (api_key.empty() && media::provider_oauth::is_supported_provider(provider_lc)) {
        std::string token, terr;
        if (media::provider_oauth::read_access_token(provider_lc, token, terr) && !token.empty())
            api_key = std::move(token);
    }

    ProviderMap providers;
    std::string err;
    if (!load_providers(providers, err))
        return;

    const ProviderEntry* row = find_provider_ci(providers, provider);
    if (!row)
        return;

    if (api_key.empty() && !row->api_key.empty())
        api_key = row->api_key;
    if (base_url.empty() && !row->base_url.empty())
        base_url = row->base_url;
}

void update_pixlwiz_api_key(const std::string& api_key, std::string& err) {
    err.clear();
    ProviderMap providers;
    if (!media::settings::load_providers(providers, err))
        return;

    auto& row = providers["pixlwiz"];
    row.api_key = api_key;

    media::settings::save_providers(providers, err);
}

bool load_global_tool_settings(GlobalToolSettings& out, std::string& err)
{
    out = GlobalToolSettings{};
    nlohmann::json node;
    if (!media::settings::load_subtree(media::settings::SettingsSubtreeKeys::custom, node, err))
        return false;
    if (!node.is_object())
        return true;
    const auto& g = node.contains("global_tools") ? node["global_tools"] : nlohmann::json();
    if (!g.is_object())
        return true;
    if (g.contains("mcp_tools_enabled") && g["mcp_tools_enabled"].is_boolean())
        out.mcp_tools_enabled = g["mcp_tools_enabled"].get<bool>();
    if (g.contains("disabled_path_tools") && g["disabled_path_tools"].is_array()) {
        for (const auto& el : g["disabled_path_tools"])
            if (el.is_string())
                out.disabled_path_tools.push_back(el.get<std::string>());
    }
    if (g.contains("disabled_mcp_servers") && g["disabled_mcp_servers"].is_array()) {
        for (const auto& el : g["disabled_mcp_servers"])
            if (el.is_string())
                out.disabled_mcp_servers.push_back(el.get<std::string>());
    }
    dedupe_lower_trim(out.disabled_path_tools);
    dedupe_lower_trim(out.disabled_mcp_servers);
    return true;
}

bool save_global_tool_settings(const GlobalToolSettings& in, std::string& err)
{
    GlobalToolSettings c = in;
    dedupe_lower_trim(c.disabled_path_tools);
    dedupe_lower_trim(c.disabled_mcp_servers);
    nlohmann::json g = nlohmann::json::object();
    g["mcp_tools_enabled"] = c.mcp_tools_enabled;
    g["disabled_path_tools"] = c.disabled_path_tools;
    g["disabled_mcp_servers"] = c.disabled_mcp_servers;
    nlohmann::json custom;
    if (!media::settings::load_subtree(media::settings::SettingsSubtreeKeys::custom, custom, err))
        return false;
    if (!custom.is_object())
        custom = nlohmann::json::object();
    custom["global_tools"] = std::move(g);
    return media::settings::save_subtree(media::settings::SettingsSubtreeKeys::custom, custom, err);
}

bool load_agent_skills_settings(AgentSkillsSettings& out, std::string& err)
{
    out = AgentSkillsSettings{};
    nlohmann::json custom;
    if (!media::settings::load_subtree(media::settings::SettingsSubtreeKeys::custom, custom, err))
        return false;
    if (!custom.is_object())
        return true;
    const auto& g = custom.contains("agent_skills") ? custom["agent_skills"] : nlohmann::json();
    if (!g.is_object())
        return true;
    if (g.contains("enabled") && g["enabled"].is_boolean())
        out.enabled = g["enabled"].get<bool>();
    if (g.contains("roaming_enabled") && g["roaming_enabled"].is_boolean())
        out.roaming_enabled = g["roaming_enabled"].get<bool>();
    if (g.contains("workspace_enabled") && g["workspace_enabled"].is_boolean())
        out.workspace_enabled = g["workspace_enabled"].get<bool>();
    if (g.contains("pinned") && g["pinned"].is_array()) {
        for (const auto& el : g["pinned"])
            if (el.is_string())
                out.pinned.push_back(el.get<std::string>());
    }
    if (g.contains("disabled") && g["disabled"].is_array()) {
        for (const auto& el : g["disabled"])
            if (el.is_string())
                out.disabled.push_back(el.get<std::string>());
    }
    dedupe_lower_trim(out.pinned);
    dedupe_lower_trim(out.disabled);
    return true;
}

bool save_agent_skills_settings(const AgentSkillsSettings& in, std::string& err)
{
    AgentSkillsSettings c = in;
    dedupe_lower_trim(c.pinned);
    dedupe_lower_trim(c.disabled);
    nlohmann::json g = nlohmann::json::object();
    g["enabled"] = c.enabled;
    g["roaming_enabled"] = c.roaming_enabled;
    g["workspace_enabled"] = c.workspace_enabled;
    g["pinned"] = c.pinned;
    g["disabled"] = c.disabled;
    nlohmann::json custom;
    if (!media::settings::load_subtree(media::settings::SettingsSubtreeKeys::custom, custom, err))
        return false;
    if (!custom.is_object())
        custom = nlohmann::json::object();
    custom["agent_skills"] = std::move(g);
    return media::settings::save_subtree(media::settings::SettingsSubtreeKeys::custom, custom, err);
}

void apply_global_tool_policy_to_values(std::vector<std::string>& disabled_path_tools,
                                        bool& mcp_tools_enabled,
                                        std::vector<std::string>& disabled_mcp_servers,
                                        std::string& err)
{
    GlobalToolSettings gs;
    if (!load_global_tool_settings(gs, err))
        return;

    // Merge with per-turn disables; global wins and is always present.
    std::vector<std::string> merged = disabled_path_tools;
    merged.insert(merged.end(), gs.disabled_path_tools.begin(), gs.disabled_path_tools.end());
    dedupe_lower_trim(merged);
    disabled_path_tools = std::move(merged);

    // Global MCP master is authoritative false.
    if (!gs.mcp_tools_enabled)
        mcp_tools_enabled = false;

    std::vector<std::string> mcp_merged = disabled_mcp_servers;
    mcp_merged.insert(mcp_merged.end(), gs.disabled_mcp_servers.begin(), gs.disabled_mcp_servers.end());
    dedupe_lower_trim(mcp_merged);
    disabled_mcp_servers = std::move(mcp_merged);
}

} // namespace media::runtime_settings
