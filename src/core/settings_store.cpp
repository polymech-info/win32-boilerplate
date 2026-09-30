#include "core/settings_store.hpp"

#if !defined(_WIN32)

#include "core/settings_portable.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace media::settings {

fs::path get_config_dir()
{
    fs::path dir = media::portable_settings::settings_pixelwiz_profile_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

void set_config_dir_override(const fs::path& dir)
{
    media::portable_settings::set_profile_dir_override(dir);
}

fs::path get_settings_json_path()
{
    return media::portable_settings::settings_pixelwiz_profile_json_path();
}

fs::path get_command_json_path()
{
    return get_config_dir() / "commands.json";
}

fs::path get_settings_effective_read_path()
{
    return get_settings_json_path();
}

bool has_settings_read_path_override()
{
    return false;
}

bool load_settings_utf8(std::string& out_json, std::string& err_out)
{
    out_json.clear();
    err_out.clear();
    json root;
    if (!media::portable_settings::read_settings_profile_json(root, err_out))
        return false;
    out_json = root.dump();
    return true;
}

bool save_settings_utf8(const std::string& utf8_json, std::string& err_out)
{
    err_out.clear();
    try {
        json root = json::parse(utf8_json.empty() ? "{}" : utf8_json);
        if (!root.is_object()) {
            err_out = "settings JSON root must be an object";
            return false;
        }
        return media::portable_settings::write_settings_profile_json(root, err_out);
    } catch (const std::exception& e) {
        err_out = std::string("settings JSON parse: ") + e.what();
        return false;
    }
}

bool load_command_json_utf8(std::string& out_json, std::string& err_out)
{
    out_json.clear();
    err_out.clear();
    const fs::path p = get_command_json_path();
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || ec)
        return true;
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        err_out = "failed to read " + p.string();
        return false;
    }
    out_json.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool save_command_json_utf8(const std::string& utf8_json, std::string& err_out)
{
    err_out.clear();
    const fs::path p = get_command_json_path();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) {
        err_out = "create_directories: " + ec.message();
        return false;
    }
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) {
        err_out = "failed to write " + p.string();
        return false;
    }
    out << utf8_json;
    return out.good();
}

bool export_settings_file(const fs::path& path, bool encrypted, std::string& err_out)
{
    if (encrypted) {
        err_out = "--encrypted local-profile export is only supported on Windows; use --archive or plaintext export";
        return false;
    }
    json root;
    if (!media::portable_settings::read_profile_direct(root, err_out))
        return false;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        err_out = "create_directories: " + ec.message();
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err_out = "failed to write " + path.string();
        return false;
    }
    out << root.dump(2);
    return out.good();
}

bool import_settings_file(const fs::path& path, std::string& err_out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err_out = "failed to read " + path.string();
        return false;
    }
    try {
        json root;
        in >> root;
        if (!root.is_object()) {
            err_out = "settings JSON root must be an object";
            return false;
        }
        return media::portable_settings::write_settings_profile_json(root, err_out);
    } catch (const std::exception& e) {
        err_out = std::string("settings JSON parse: ") + e.what();
        return false;
    }
}

bool load_subtree(const std::string& key, json& out, std::string& err)
{
    out = json();
    json root;
    if (!media::portable_settings::read_settings_profile_json(root, err))
        return false;
    if (root.is_object() && root.contains(key))
        out = root[key];
    return true;
}

bool save_subtree(const std::string& key, const json& value, std::string& err)
{
    json root;
    if (!media::portable_settings::read_settings_profile_json(root, err))
        return false;
    if (!root.is_object())
        root = json::object();
    root[key] = value;
    return media::portable_settings::write_settings_profile_json(root, err);
}

bool merge_subtree_object(const std::string& key, const json& fields, std::string& err)
{
    if (!fields.is_object()) {
        err = "merge_subtree_object fields must be object";
        return false;
    }
    json current;
    if (!load_subtree(key, current, err))
        return false;
    if (!current.is_object())
        current = json::object();
    for (auto it = fields.begin(); it != fields.end(); ++it)
        current[it.key()] = it.value();
    return save_subtree(key, current, err);
}

bool load_providers(ProviderMap& out, std::string& err)
{
    return media::portable_settings::load_providers(out, err);
}

bool save_providers(const ProviderMap& providers, std::string& err)
{
    json root;
    if (!media::portable_settings::read_settings_profile_json(root, err))
        return false;
    if (!root.is_object())
        root = json::object();
    json jp = json::object();
    for (const auto& kv : providers) {
        json row = json::object();
        if (!kv.second.api_key.empty()) row["api_key"] = kv.second.api_key;
        if (!kv.second.base_url.empty()) row["base_url"] = kv.second.base_url;
        if (!kv.second.default_model.empty()) row["default_model"] = kv.second.default_model;
        jp[kv.first] = std::move(row);
    }
    root["providers"] = std::move(jp);
    root.erase("active_provider");
    return media::portable_settings::write_settings_profile_json(root, err);
}

bool load_chat_provider(ChatProviderSettings& out, std::string& err)
{
    return media::portable_settings::load_chat_provider(out, err);
}

bool save_chat_provider(const ChatProviderSettings& s, std::string& err)
{
    json root;
    if (!media::portable_settings::read_settings_profile_json(root, err))
        return false;
    if (!root.is_object())
        root = json::object();
    json c = json::object();
    if (!s.router.empty()) c["router"] = s.router;
    if (!s.model.empty()) c["model"] = s.model;
    if (s.timeout_ms > 0) c["timeout_ms"] = s.timeout_ms;
    if (s.max_iterations > 0) c["max_iterations"] = s.max_iterations;
    if (!s.image_provider.empty()) c["image_provider"] = s.image_provider;
    if (!s.image_model.empty()) c["image_model"] = s.image_model;
    if (!s.image_recognition_provider.empty()) c["image_recognition_provider"] = s.image_recognition_provider;
    if (!s.image_recognition_model.empty()) c["image_recognition_model"] = s.image_recognition_model;
    if (!s.video_provider.empty()) c["video_provider"] = s.video_provider;
    if (!s.video_model.empty()) c["video_model"] = s.video_model;
    if (!s.stt_provider.empty()) c["stt_provider"] = s.stt_provider;
    if (!s.stt_model.empty()) c["stt_model"] = s.stt_model;
    if (!s.tts_provider.empty()) c["tts_provider"] = s.tts_provider;
    if (!s.tts_model.empty()) c["tts_model"] = s.tts_model;
    if (!s.api_mode.empty()) c["api_mode"] = s.api_mode;
    root["chat"] = std::move(c);
    return media::portable_settings::write_settings_profile_json(root, err);
}

} // namespace media::settings

#endif
