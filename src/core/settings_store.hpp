#pragma once

#if defined(_WIN32)
#include "win/settings_store.hpp"
#else

#include "core/settings_types.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace media::settings {

using ProviderEntry = media::settings_types::ProviderEntry;
using ProviderMap = media::settings_types::ProviderMap;
using ChatProviderSettings = media::settings_types::ChatProviderSettings;

std::filesystem::path get_config_dir();
void set_config_dir_override(const std::filesystem::path& dir);
std::filesystem::path get_settings_json_path();
std::filesystem::path get_command_json_path();
std::filesystem::path get_settings_effective_read_path();
bool has_settings_read_path_override();

bool load_settings_utf8(std::string& out_json, std::string& err_out);
bool save_settings_utf8(const std::string& utf8_json, std::string& err_out);
bool load_command_json_utf8(std::string& out_json, std::string& err_out);
bool save_command_json_utf8(const std::string& utf8_json, std::string& err_out);

bool export_settings_file(const std::filesystem::path& path, bool encrypted, std::string& err_out);
bool import_settings_file(const std::filesystem::path& path, std::string& err_out);

struct SettingsSubtreeKeys {
    static constexpr const char* custom = "custom";
    static constexpr const char* history = "history";
    static constexpr const char* explorer_presets = "explorer_presets";
    static constexpr const char* chat_web = "chat_web";
};

bool load_subtree(const std::string& key, nlohmann::json& out, std::string& err);
bool save_subtree(const std::string& key, const nlohmann::json& value, std::string& err);
bool merge_subtree_object(const std::string& key, const nlohmann::json& fields, std::string& err);

bool load_providers(ProviderMap& out, std::string& err);
bool save_providers(const ProviderMap& providers, std::string& err);
bool load_chat_provider(ChatProviderSettings& out, std::string& err);
bool save_chat_provider(const ChatProviderSettings& s, std::string& err);

} // namespace media::settings

#endif
