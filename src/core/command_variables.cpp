#include "command_variables.hpp"

#include <cctype>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace media::commands {
namespace {

std::string two_digits(int value)
{
    char buf[8]{};
    std::snprintf(buf, sizeof(buf), "%02d", value);
    return buf;
}

void resolve_json_string(nlohmann::json& item, const char* key, const VariableMap& vars, std::string* err)
{
    if (item.contains(key) && item[key].is_string())
        item[key] = resolve_variables(item[key].get<std::string>(), vars, err);
}

void resolve_json_string_array(nlohmann::json& item, const char* key, const VariableMap& vars, std::string* err)
{
    if (!item.contains(key) || !item[key].is_array())
        return;
    for (auto& value : item[key]) {
        if (value.is_string())
            value = resolve_variables(value.get<std::string>(), vars, err);
    }
}

bool is_variable_key_char(char ch)
{
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == ':';
}

std::string environment_variable(const std::string& name)
{
    if (name.empty())
        return {};
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : std::string{};
}

fs::path absolute_context_path(const std::string& value, const std::string& cwd)
{
    fs::path path = fs::u8path(value);
    if (path.is_absolute())
        return path.lexically_normal();
    if (!cwd.empty())
        return (fs::u8path(cwd) / path).lexically_normal();
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    return ec ? path.lexically_normal() : absolute.lexically_normal();
}

std::string quote_selection_part(const std::string& value)
{
    if (value.empty())
        return "\"\"";
    const bool needs_quote = value.find_first_of(" \t\n\v\"") != std::string::npos;
    if (!needs_quote)
        return value;
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char ch : value) {
        if (ch == '"')
            out.push_back('\\');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

std::string selection_list_value(const std::vector<std::string>& paths, const std::string& cwd)
{
    std::string out;
    for (const auto& value : paths) {
        if (value.empty())
            continue;
        const std::string path = absolute_context_path(value, cwd).string();
        if (!out.empty())
            out.push_back(' ');
        out += quote_selection_part(path);
    }
    return out;
}

std::string normalize_known_folder_name(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
        return ch == '-' || ch == ' ' ? '_' : static_cast<char>(std::toupper(ch));
    });
    return name;
}

#if defined(_WIN32)
std::wstring utf8_to_wide_local(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return std::wstring(s.begin(), s.end());
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8_local(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

const KNOWNFOLDERID* known_folder_id_for_name(const std::string& name)
{
    const std::string key = normalize_known_folder_name(name);
    struct KnownFolderEntry {
        const char* name;
        const KNOWNFOLDERID* id;
    };
    static const KnownFolderEntry entries[] = {
        {"HOME", &FOLDERID_Profile},
        {"PROFILE", &FOLDERID_Profile},
        {"DESKTOP", &FOLDERID_Desktop},
        {"DOCUMENTS", &FOLDERID_Documents},
        {"DOWNLOADS", &FOLDERID_Downloads},
        {"PICTURES", &FOLDERID_Pictures},
        {"MUSIC", &FOLDERID_Music},
        {"VIDEOS", &FOLDERID_Videos},
        {"TEMPLATES", &FOLDERID_Templates},
        {"FAVORITES", &FOLDERID_Favorites},
        {"LINKS", &FOLDERID_Links},
        {"SAVED_GAMES", &FOLDERID_SavedGames},
        {"SCREENSHOTS", &FOLDERID_Screenshots},
        {"LOCAL_APP_DATA", &FOLDERID_LocalAppData},
        {"DATA", &FOLDERID_LocalAppData},
        {"ROAMING_APP_DATA", &FOLDERID_RoamingAppData},
        {"CONFIG", &FOLDERID_RoamingAppData},
        {"PROGRAM_DATA", &FOLDERID_ProgramData},
        {"CACHE", &FOLDERID_LocalAppData},
        {"PUBLIC", &FOLDERID_Public},
        {"PUBLIC_DESKTOP", &FOLDERID_PublicDesktop},
        {"PUBLIC_DOCUMENTS", &FOLDERID_PublicDocuments},
        {"PUBLIC_DOWNLOADS", &FOLDERID_PublicDownloads},
        {"PUBLIC_PICTURES", &FOLDERID_PublicPictures},
        {"PUBLIC_MUSIC", &FOLDERID_PublicMusic},
        {"PUBLIC_VIDEOS", &FOLDERID_PublicVideos},
        {"WINDOWS", &FOLDERID_Windows},
        {"SYSTEM", &FOLDERID_System},
        {"PROGRAM_FILES", &FOLDERID_ProgramFiles},
        {"PROGRAM_FILES_X86", &FOLDERID_ProgramFilesX86},
        {"FONTS", &FOLDERID_Fonts},
        {"START_MENU", &FOLDERID_StartMenu},
        {"PROGRAMS", &FOLDERID_Programs},
        {"STARTUP", &FOLDERID_Startup},
    };
    for (const auto& entry : entries) {
        if (key == entry.name)
            return entry.id;
    }
    return nullptr;
}

std::string known_folder_path(const std::string& name)
{
    const std::string key = normalize_known_folder_name(name);
    if (key == "TEMP" || key == "TMP") {
        std::string tmp = environment_variable("TEMP");
        if (tmp.empty())
            tmp = environment_variable("TMP");
        return tmp;
    }
    const KNOWNFOLDERID* id = known_folder_id_for_name(name);
    if (id) {
        PWSTR path = nullptr;
        const HRESULT hr = ::SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &path);
        if (SUCCEEDED(hr) && path) {
            std::wstring w(path);
            ::CoTaskMemFree(path);
            return wide_to_utf8_local(w);
        }
    }

    HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(init);
    if (init == RPC_E_CHANGED_MODE)
        init = S_OK;
    if (FAILED(init))
        return {};

    std::string out;
    IKnownFolderManager* manager = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_KnownFolderManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager))) && manager) {
        IKnownFolder* folder = nullptr;
        const std::wstring wname = utf8_to_wide_local(name);
        if (SUCCEEDED(manager->GetFolderByName(wname.c_str(), &folder)) && folder) {
            PWSTR path = nullptr;
            if (SUCCEEDED(folder->GetPath(KF_FLAG_DEFAULT, &path)) && path) {
                out = wide_to_utf8_local(path);
                ::CoTaskMemFree(path);
            }
            folder->Release();
        }
        manager->Release();
    }
    if (uninit)
        ::CoUninitialize();
    return out;
}
#else
std::string home_dir()
{
    return environment_variable("HOME");
}

std::string path_or_home_child(const std::string& path)
{
    if (!path.empty())
        return path;
    return {};
}

std::string home_child(const char* child)
{
    const std::string home = home_dir();
    if (home.empty())
        return {};
    return (fs::path(home) / child).string();
}

std::string xdg_base_dir(const char* env_name, const char* fallback_child)
{
    const std::string configured = environment_variable(env_name);
    if (!configured.empty())
        return configured;
    return home_child(fallback_child);
}

#if defined(__APPLE__)
std::string known_folder_path(const std::string& name)
{
    const std::string key = normalize_known_folder_name(name);
    if (key == "HOME" || key == "PROFILE") return home_dir();
    if (key == "DESKTOP") return home_child("Desktop");
    if (key == "DOCUMENTS") return home_child("Documents");
    if (key == "DOWNLOADS") return home_child("Downloads");
    if (key == "PICTURES") return home_child("Pictures");
    if (key == "MUSIC") return home_child("Music");
    if (key == "VIDEOS" || key == "MOVIES") return home_child("Movies");
    if (key == "CONFIG") return home_child("Library/Preferences");
    if (key == "DATA" || key == "APPLICATION_SUPPORT") return home_child("Library/Application Support");
    if (key == "CACHE") return home_child("Library/Caches");
    if (key == "TEMP" || key == "TMP") {
        const std::string tmp = environment_variable("TMPDIR");
        return tmp.empty() ? std::string("/tmp") : tmp;
    }
    return {};
}
#else
std::string unquote_xdg_value(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    const std::string home = home_dir();
    if (!home.empty()) {
        const std::string var1 = "$HOME";
        const std::string var2 = "${HOME}";
        if (value.rfind(var1, 0) == 0)
            value.replace(0, var1.size(), home);
        if (value.rfind(var2, 0) == 0)
            value.replace(0, var2.size(), home);
    }
    return value;
}

std::string xdg_user_dir(const char* key_name, const char* fallback_child)
{
    const std::string config_home = xdg_base_dir("XDG_CONFIG_HOME", ".config");
    if (!config_home.empty()) {
        std::ifstream in(fs::path(config_home) / "user-dirs.dirs");
        std::string line;
        const std::string prefix = std::string("XDG_") + key_name + "_DIR=";
        while (std::getline(in, line)) {
            if (line.rfind(prefix, 0) == 0)
                return path_or_home_child(unquote_xdg_value(line.substr(prefix.size())));
        }
    }
    return home_child(fallback_child);
}

std::string known_folder_path(const std::string& name)
{
    const std::string key = normalize_known_folder_name(name);
    if (key == "HOME" || key == "PROFILE") return home_dir();
    if (key == "DESKTOP") return xdg_user_dir("DESKTOP", "Desktop");
    if (key == "DOCUMENTS") return xdg_user_dir("DOCUMENTS", "Documents");
    if (key == "DOWNLOADS") return xdg_user_dir("DOWNLOAD", "Downloads");
    if (key == "PICTURES") return xdg_user_dir("PICTURES", "Pictures");
    if (key == "MUSIC") return xdg_user_dir("MUSIC", "Music");
    if (key == "VIDEOS") return xdg_user_dir("VIDEOS", "Videos");
    if (key == "PUBLIC") return xdg_user_dir("PUBLICSHARE", "Public");
    if (key == "TEMPLATES") return xdg_user_dir("TEMPLATES", "Templates");
    if (key == "CONFIG") return xdg_base_dir("XDG_CONFIG_HOME", ".config");
    if (key == "DATA") return xdg_base_dir("XDG_DATA_HOME", ".local/share");
    if (key == "CACHE") return xdg_base_dir("XDG_CACHE_HOME", ".cache");
    if (key == "STATE") return xdg_base_dir("XDG_STATE_HOME", ".local/state");
    if (key == "TEMP" || key == "TMP") {
        const std::string tmp = environment_variable("TMPDIR");
        return tmp.empty() ? std::string("/tmp") : tmp;
    }
    return {};
}
#endif
#endif

} // namespace

VariableMap make_variable_map(const VariableContext& context)
{
    VariableMap vars;

    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    vars["YYYY"] = std::to_string(tm.tm_year + 1900);
    vars["MM"] = two_digits(tm.tm_mon + 1);
    vars["DD"] = two_digits(tm.tm_mday);
    vars["HH"] = two_digits(tm.tm_hour);
    vars["SS"] = two_digits(tm.tm_sec);

    vars["CWD"] = context.cwd;
    if (!context.cwd.empty())
        vars["CURRENT_PATH"] = fs::u8path(context.cwd).lexically_normal().string();
    const std::string current_selection = selection_list_value(context.selection_paths, context.cwd);
    if (!current_selection.empty())
        vars["CURRENT_SELECTION"] = current_selection;
    vars["PATH_SEP"] = std::string(1, fs::path::preferred_separator);
#if defined(_WIN32)
    vars["PATH_LIST_SEP"] = ";";
#else
    vars["PATH_LIST_SEP"] = ":";
#endif

    if (!context.source_file.empty()) {
        const fs::path src = fs::u8path(context.source_file);
        const fs::path current = absolute_context_path(context.source_file, context.cwd);
        std::error_code current_ec;
        const bool current_is_directory = fs::is_directory(current, current_ec);
        std::string ext = src.extension().string();
        if (current_is_directory) {
            vars["CURRENT_PATH"] = current.string();
        } else {
            vars["CURRENT_FILE"] = current.string();
            vars["CURRENT_FILE_NAME"] = current.filename().string();
            vars["CURRENT_PATH"] = current.parent_path().string();
        }
        vars["SRC_FILE"] = context.source_file;
        vars["SRC_DIR"] = src.parent_path().string();
        vars["SRC_NAME"] = src.stem().string();
        vars["SRC_FILE_EXT"] = ext;
        if (!ext.empty() && ext.front() == '.')
            ext.erase(ext.begin());
        vars["SRC_EXT"] = ext;
    }

    return vars;
}

std::string resolve_variables(std::string_view value, const VariableMap& vars, std::string* err)
{
    if (err)
        err->clear();
    std::string out;
    out.reserve(value.size() + 32);
    for (std::size_t i = 0; i < value.size();) {
        if (i + 1 < value.size() && value[i] == '$' && value[i + 1] == '{') {
            const std::size_t key_start = i + 2;
            std::size_t k = key_start;
            while (k < value.size() && is_variable_key_char(value[k]))
                ++k;
            if (k >= value.size() || value[k] != '}') {
                if (err)
                    *err = "command_variables: unclosed `${`";
                return std::string(value);
            }
            const std::string key(value.substr(key_start, k - key_start));
            if (key.empty()) {
                if (err)
                    *err = "command_variables: empty ${} key";
                return std::string(value);
            }
            if (key.rfind("ENV:", 0) == 0) {
                const std::string env = environment_variable(key.substr(4));
                if (!env.empty()) {
                    out += env;
                } else {
                    out += "${";
                    out += key;
                    out += "}";
                }
            } else if (key.rfind("KNOWNFOLDER:", 0) == 0) {
                const std::string folder = known_folder_path(key.substr(12));
                if (!folder.empty()) {
                    out += folder;
                } else {
                    out += "${";
                    out += key;
                    out += "}";
                }
            } else if (const auto it = vars.find(key); it != vars.end()) {
                out += it->second;
            } else {
                out += "${";
                out += key;
                out += "}";
            }
            i = k + 1;
            continue;
        }
        if (value[i] == '\0') {
            if (err)
                *err = "command_variables: null byte in template";
            return std::string(value);
        }
        out += value[i];
        ++i;
    }
    return out;
}

void resolve_custom_command_item_variables(nlohmann::json& item,
                                           const VariableContext& context,
                                           std::string* err)
{
    VariableMap vars = make_variable_map(context);

    resolve_json_string(item, "cwd", vars, err);
    if (item.contains("cwd") && item["cwd"].is_string())
        vars["CWD"] = item["cwd"].get<std::string>();

    resolve_json_string_array(item, "globalArgs", vars, err);
    resolve_json_string_array(item, "args", vars, err);
    resolve_json_string(item, "url", vars, err);
    resolve_json_string(item, "path", vars, err);

    if (item.contains("externalCommand") && item["externalCommand"].is_object()) {
        auto& ext = item["externalCommand"];
        resolve_json_string(ext, "cwd", vars, err);
        if (ext.contains("cwd") && ext["cwd"].is_string() && !ext["cwd"].get<std::string>().empty())
            vars["CWD"] = ext["cwd"].get<std::string>();
        resolve_json_string(ext, "command", vars, err);
        resolve_json_string(ext, "shellLine", vars, err);
        resolve_json_string_array(ext, "args", vars, err);
    }

    if (item.contains("source") && item["source"].is_object()) {
        auto& source = item["source"];
        resolve_json_string_array(source, "files", vars, err);
        resolve_json_string_array(source, "folders", vars, err);
    }

    if (item.contains("output") && item["output"].is_object()) {
        auto& output = item["output"];
        resolve_json_string(output, "directory", vars, err);
        resolve_json_string(output, "file", vars, err);
    }
}

} // namespace media::commands
