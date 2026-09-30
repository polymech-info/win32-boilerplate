#pragma once

#include "core/settings_types.hpp"

#include <string>
#include <vector>

namespace media::runtime_settings {

using ProviderEntry = media::settings_types::ProviderEntry;
using ProviderMap = media::settings_types::ProviderMap;
using ChatProviderSettings = media::settings_types::ChatProviderSettings;

bool load_chat_provider(ChatProviderSettings& out, std::string& err);
bool load_providers(ProviderMap& out, std::string& err);
bool load_command_json_utf8(std::string& out_json, std::string& err);
bool save_command_json_utf8(const std::string& utf8_json, std::string& err);
void set_command_json_path_override(const std::string& path);

/// Fill API key / base URL from `providers[provider]`. Provider/model selection must already be
/// explicit from chat settings or command/tool overrides; provider rows are credential rows only.
void merge_provider_credentials(const std::string& provider,
                                bool dry_run,
                                std::string& api_key,
                                std::string& base_url);

/// Write `api_key` into `providers["pixlwiz"]["api_key"]` in settings.json (and clear it when
/// `api_key` is empty). No-op on platforms where settings save is not implemented. Never fatal —
/// Option B (zitadel-oauth.json fallback in merge_provider_credentials) remains the authoritative
/// source; this only keeps ProviderDlg in sync.
void update_pixlwiz_api_key(const std::string& api_key, std::string& err);

struct GlobalToolSettings {
    std::vector<std::string> disabled_path_tools;
    bool mcp_tools_enabled = true;
    std::vector<std::string> disabled_mcp_servers;
};

bool load_global_tool_settings(GlobalToolSettings& out, std::string& err);
bool save_global_tool_settings(const GlobalToolSettings& in, std::string& err);

struct AgentSkillsSettings {
    bool enabled = true;
    bool roaming_enabled = true;
    bool workspace_enabled = true;
    std::vector<std::string> pinned;
    std::vector<std::string> disabled;
};

bool load_agent_skills_settings(AgentSkillsSettings& out, std::string& err);
bool save_agent_skills_settings(const AgentSkillsSettings& in, std::string& err);

/// Global settings win: merges persistent tool policy from settings.json into per-turn values.
void apply_global_tool_policy_to_values(std::vector<std::string>& disabled_path_tools,
                                        bool& mcp_tools_enabled,
                                        std::vector<std::string>& disabled_mcp_servers,
                                        std::string& err);

} // namespace media::runtime_settings
