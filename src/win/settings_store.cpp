#include "settings_store.hpp"
#include "constants.hpp"
#include "logger/logger.h"

#include <sodium.h>

#include <Windows.h>
#include <wincrypt.h>

#include <cctype>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <shlobj.h>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
thread_local std::vector<const char*> g_settings_load_label_stack;
/// @c --ui-reset: ignore persisted workbench UI from settings.json for this process (see set_ui_reset_session).
bool g_ui_reset_session = false;

void (*g_settings_load_trace)(const char* reason) = nullptr;

// Settings file read cache + optional CLI `--settings` read path (see set_settings_read_path_override).
std::mutex              g_settings_load_mutex;
std::optional<fs::path> g_settings_read_override;
std::optional<fs::path> g_config_dir_override;
std::string             g_settings_cache_json;
bool                    g_settings_cache_valid = false;
fs::path                g_settings_cache_sp;
fs::file_time_type      g_settings_cache_mtime{};
bool                    g_settings_cache_has_mtime = false;
bool                    g_settings_exe_auto_import_checked = false;

void invalidate_settings_cache()
{
    g_settings_cache_valid     = false;
    g_settings_cache_json.clear();
    g_settings_cache_has_mtime = false;
    g_settings_cache_sp.clear();
}
} // namespace

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace media::settings {

bool defer_dock_container_load()
{
    return media::layout::LayoutStore::DeferDockContainerLoad();
}

SettingsLoadLabelScope::SettingsLoadLabelScope(const char* reason)
{
    if (reason && reason[0]) {
        g_settings_load_label_stack.push_back(reason);
        pushed_ = true;
    }
}

SettingsLoadLabelScope::~SettingsLoadLabelScope()
{
    if (pushed_ && !g_settings_load_label_stack.empty())
        g_settings_load_label_stack.pop_back();
}

namespace {

constexpr char kMagic[4] = {'P', 'M', 'E', '1'};
/** Must match installer / docs: %APPDATA%\<k_config_subpath_w> */
constexpr const wchar_t* kAppDataSubdir = pm::brand::k_config_subpath_w;
constexpr const wchar_t kKeyFileName[] = L".settings-key.dat";

bool read_file_bytes(const fs::path& p, std::vector<unsigned char>& out) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open())
        return false;
    ifs.seekg(0, std::ios::end);
    const auto sz = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    out.resize(sz);
    if (sz)
        ifs.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
    return static_cast<bool>(ifs);
}

bool write_file_bytes(const fs::path& p, const unsigned char* data, size_t len) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open())
        return false;
    if (len)
        ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
    return static_cast<bool>(ofs);
}

bool dpapi_protect(const std::vector<unsigned char>& plain, std::vector<unsigned char>& out) {
    DATA_BLOB in{(DWORD)plain.size(), const_cast<BYTE*>(plain.data())};
    DATA_BLOB out_blob{};
    if (!CryptProtectData(&in, pm::brand::k_settings_dpapi_label_w, nullptr, nullptr, nullptr, 0, &out_blob))
        return false;
    out.assign(out_blob.pbData, out_blob.pbData + out_blob.cbData);
    LocalFree(out_blob.pbData);
    return true;
}

bool dpapi_unprotect(const std::vector<unsigned char>& enc, std::vector<unsigned char>& plain) {
    DATA_BLOB in{(DWORD)enc.size(), const_cast<BYTE*>(enc.data())};
    DATA_BLOB out_blob{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out_blob))
        return false;
    plain.assign(out_blob.pbData, out_blob.pbData + out_blob.cbData);
    LocalFree(out_blob.pbData);
    return true;
}

bool ensure_sodium(std::string& err) {
    static bool done = false;
    if (done)
        return true;
    if (sodium_init() < 0) {
        err = "sodium_init failed";
        return false;
    }
    done = true;
    return true;
}

bool load_or_create_secret_key(std::vector<unsigned char>& key32, std::string& err) {
    if (!ensure_sodium(err))
        return false;
    key32.resize(crypto_secretbox_KEYBYTES);

    const fs::path key_path = get_config_dir() / kKeyFileName;
    std::vector<unsigned char> file_bytes;
    if (fs::exists(key_path)) {
        if (!read_file_bytes(key_path, file_bytes) || file_bytes.empty()) {
            err = "failed to read settings key file";
            return false;
        }
        std::vector<unsigned char> plain;
        if (!dpapi_unprotect(file_bytes, plain) || plain.size() != crypto_secretbox_KEYBYTES) {
            err = "CryptUnprotectData(settings key) failed";
            return false;
        }
        std::copy(plain.begin(), plain.end(), key32.begin());
        return true;
    }

    randombytes_buf(key32.data(), key32.size());
    std::vector<unsigned char> prot;
    if (!dpapi_protect(key32, prot)) {
        err = "CryptProtectData(settings key) failed";
        return false;
    }
    if (!write_file_bytes(key_path, prot.data(), prot.size())) {
        err = "failed to write settings key file";
        return false;
    }
    return true;
}

bool looks_like_json_plaintext(const std::vector<unsigned char>& raw) {
    size_t i = 0;
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i])))
        ++i;
    return i < raw.size() && raw[i] == '{';
}

bool decode_settings_raw_bytes(const std::vector<unsigned char>& raw, std::string& out_json, std::string& err_out) {
    out_json.clear();
    err_out.clear();
    if (raw.empty())
        return true;
    std::string err;
    if (!ensure_sodium(err)) {
        err_out = err;
        return false;
    }
    if (raw.size() >= sizeof(kMagic) && std::memcmp(raw.data(), kMagic, sizeof(kMagic)) == 0) {
        if (raw.size() < sizeof(kMagic) + crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES) {
            err_out = "encrypted settings truncated";
            return false;
        }
        std::vector<unsigned char> key(crypto_secretbox_KEYBYTES);
        if (!load_or_create_secret_key(key, err)) {
            err_out = err;
            return false;
        }
        const unsigned char* nonce = raw.data() + sizeof(kMagic);
        const unsigned char* cipher = nonce + crypto_secretbox_NONCEBYTES;
        const size_t cipher_len = raw.size() - sizeof(kMagic) - crypto_secretbox_NONCEBYTES;
        if (cipher_len < crypto_secretbox_MACBYTES) {
            err_out = "encrypted settings ciphertext too short";
            return false;
        }
        std::vector<unsigned char> plain(cipher_len - crypto_secretbox_MACBYTES);
        if (crypto_secretbox_open_easy(plain.data(), cipher, cipher_len, nonce, key.data()) != 0) {
            err_out = "crypto_secretbox_open_easy failed (wrong key or corrupt file)";
            return false;
        }
        out_json.assign(reinterpret_cast<const char*>(plain.data()), plain.size());
        return true;
    }
    if (looks_like_json_plaintext(raw)) {
        out_json.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
        return true;
    }
    err_out = "not PME1 encrypted and not valid plaintext JSON";
    return false;
}

bool encrypt_settings_blob(const std::string& utf8_json, std::vector<unsigned char>& out, std::string& err_out) {
    out.clear();
    err_out.clear();
    std::string err;
    if (!ensure_sodium(err)) {
        err_out = err;
        return false;
    }
    std::vector<unsigned char> key(crypto_secretbox_KEYBYTES);
    if (!load_or_create_secret_key(key, err)) {
        err_out = err;
        return false;
    }
    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof nonce);
    const size_t msg_len = utf8_json.size();
    std::vector<unsigned char> cipher(msg_len + crypto_secretbox_MACBYTES);
    if (crypto_secretbox_easy(cipher.data(), reinterpret_cast<const unsigned char*>(utf8_json.data()), msg_len, nonce,
                              key.data()) != 0) {
        err_out = "crypto_secretbox_easy failed";
        return false;
    }
    out.reserve(sizeof(kMagic) + sizeof nonce + cipher.size());
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    out.insert(out.end(), nonce, nonce + sizeof nonce);
    out.insert(out.end(), cipher.begin(), cipher.end());
    return true;
}

bool write_utf8_plaintext_file(const fs::path& p, const std::string& utf8, std::string& err_out) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        err_out = "failed to open file for writing";
        return false;
    }
    ofs.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(ofs);
}

void maybe_auto_import_settings_from_cwd() {
    {
        std::lock_guard<std::mutex> lock(g_settings_load_mutex);
        if (g_settings_read_override || g_settings_exe_auto_import_checked)
            return;
        g_settings_exe_auto_import_checked = true;
    }

    // Only import when the roaming profile has no settings yet.
    std::error_code ec;
    const fs::path live_settings = get_settings_json_path();
    if (fs::exists(live_settings, ec) && !ec)
        return;

    // Check cwd for a settings.json to seed the profile from.
    const fs::path cwd_settings = fs::current_path() / "settings.json";
    if (cwd_settings == live_settings)
        return;
    if (!fs::is_regular_file(cwd_settings, ec) || ec)
        return;

    std::string err;
    if (media::settings::import_settings_file(cwd_settings, err)) {
        append_explorer_shell_correlation_log_utf8(
            "auto-imported settings.json from cwd: " + cwd_settings.string());
    } else if (!err.empty()) {
        append_explorer_shell_correlation_log_utf8(
            "settings auto-import from cwd failed: " + cwd_settings.string() + ": " + err);
    }
}

} // namespace

fs::path get_config_dir() {
    if (g_config_dir_override.has_value()) {
        std::error_code ec;
        fs::create_directories(g_config_dir_override.value(), ec);
        return g_config_dir_override.value();
    }
    PWSTR path_tmp = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path_tmp))) {
        throw std::runtime_error("SHGetKnownFolderPath(RoamingAppData) failed");
    }
    fs::path path(path_tmp);
    CoTaskMemFree(path_tmp);
    path /= kAppDataSubdir;
    std::error_code ec;
    fs::create_directories(path, ec);
    return path;
}

void set_config_dir_override(const fs::path& dir) {
    std::lock_guard<std::mutex> lock(g_settings_load_mutex);
    g_config_dir_override = fs::absolute(dir);
    invalidate_settings_cache();
}

fs::path get_settings_json_path() {
    return get_config_dir() / "settings.json";
}

fs::path get_command_json_path() {
    return get_config_dir() / "commands.json";
}

fs::path get_daemon_json_path() {
    return get_config_dir() / "daemon.json";
}

fs::path get_settings_effective_read_path() {
    std::lock_guard<std::mutex> lock(g_settings_load_mutex);
    if (g_settings_read_override.has_value()) {
        return g_settings_read_override.value();
    }
    // Priority 1: settings.json in cwd (--cwd or original cwd)
    const fs::path cwd_settings = fs::current_path() / "settings.json";
    std::error_code ec;
    if (fs::is_regular_file(cwd_settings, ec) && !ec) {
        return cwd_settings;
    }
    // Priority 2: profile — %APPDATA%\PolyMech\pm-image\settings.json
    return get_settings_json_path();
}

bool has_settings_read_path_override() {
    std::lock_guard<std::mutex> lock(g_settings_load_mutex);
    return g_settings_read_override.has_value();
}

void set_settings_read_path_override(const fs::path& absolute_path) {
    std::lock_guard<std::mutex> lock(g_settings_load_mutex);
    g_settings_read_override = absolute_path;
    invalidate_settings_cache();
}

void clear_settings_read_path_override() {
    std::lock_guard<std::mutex> lock(g_settings_load_mutex);
    g_settings_read_override.reset();
    invalidate_settings_cache();
}

namespace {
std::mutex g_explorer_correlation_log_mutex;
constexpr const wchar_t kExplorerCorrelationLogName[] = L"pm-image-iexecute.log";
} // namespace

void append_explorer_shell_correlation_log_utf8(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_explorer_correlation_log_mutex);
    try {
        fs::path dir;
        try {
            dir = get_config_dir();
        } catch (...) {
            return;
        }
        const fs::path logf = dir / kExplorerCorrelationLogName;
        std::ofstream      out(logf, std::ios::app | std::ios::binary);
        if (!out)
            return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char ts[48]{};
        sprintf_s(ts, "%04u-%02u-%02u %02u:%02u:%02u  ", (unsigned)st.wYear, (unsigned)st.wMonth,
                  (unsigned)st.wDay, (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        out << ts << "[pm-image] " << line << '\n';
        out.flush();
    } catch (...) {
    }
}

bool load_settings_utf8(std::string& out_json, std::string& err_out) {
    err_out.clear();
    out_json.clear();

    maybe_auto_import_settings_from_cwd();

    std::lock_guard<std::mutex> lock(g_settings_load_mutex);
    // Priority: 1) --settings override, 2) cwd/settings.json, 3) profile (%APPDATA%\PolyMech\pm-image)
    fs::path sp;
    const char* sp_source = "roaming profile";
    bool sp_selected_from_cwd = false;
    if (g_settings_read_override.has_value()) {
        sp = g_settings_read_override.value();
        sp_source = "--settings override";
    } else {
        std::error_code ec;
        const fs::path cwd_settings = fs::current_path() / "settings.json";
        if (fs::is_regular_file(cwd_settings, ec) && !ec) {
            sp = cwd_settings;
            sp_source = "cwd";
            sp_selected_from_cwd = true;
        } else {
            sp = get_settings_json_path();
        }
    }
    if (g_settings_cache_valid) {
        std::error_code mtec;
        if (g_settings_cache_sp == sp && fs::exists(sp, mtec) && !mtec) {
            const auto m = fs::last_write_time(sp, mtec);
            if (!mtec && g_settings_cache_has_mtime && m == g_settings_cache_mtime) {
                out_json = g_settings_cache_json;
                return true;
            }
        }
        invalidate_settings_cache();
    }
    if (g_settings_load_trace) {
        const char* tag
            = g_settings_load_label_stack.empty() ? "(unlabeled)" : g_settings_load_label_stack.back();
        g_settings_load_trace(tag);
    }

    auto fallback_to_roaming_if_cwd = [&]() -> bool {
        if (!sp_selected_from_cwd)
            return false;
        sp = get_settings_json_path();
        sp_source = "roaming profile (fallback from cwd)";
        sp_selected_from_cwd = false;
        return true;
    };

    if (!fs::exists(sp)) {
        if (!fallback_to_roaming_if_cwd() || !fs::exists(sp))
            return true;
    }

    std::vector<unsigned char> raw;
    if (!read_file_bytes(sp, raw) || raw.empty()) {
        if (!fallback_to_roaming_if_cwd())
            return true;
        if (!fs::exists(sp))
            return true;
        raw.clear();
        if (!read_file_bytes(sp, raw) || raw.empty())
            return true;
    }

    // Cache-miss path probe: show the effective source that was actually loaded.
    logger::info(std::string("settings: loaded from ") + sp_source + ": " + sp.string());

    if (!decode_settings_raw_bytes(raw, out_json, err_out))
        return false;
    g_settings_cache_json    = out_json;
    g_settings_cache_valid   = true;
    g_settings_cache_sp      = sp;
    {
        std::error_code mtec;
        g_settings_cache_mtime     = fs::last_write_time(sp, mtec);
        g_settings_cache_has_mtime = !mtec;
    }
    return true;
}

bool load_command_json_utf8(std::string& out_json, std::string& err_out) {
    err_out.clear();
    out_json.clear();
    const fs::path p = get_command_json_path();
    if (!fs::exists(p))
        return true;
    std::vector<unsigned char> raw;
    if (!read_file_bytes(p, raw)) {
        err_out = "failed to read " + p.string();
        return false;
    }
    out_json.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
    return true;
}

bool save_command_json_utf8(const std::string& utf8_json, std::string& err_out) {
    return write_utf8_plaintext_file(get_command_json_path(), utf8_json, err_out);
}

void set_settings_load_tracing(void (*fn)(const char*)) { g_settings_load_trace = fn; }

void clear_settings_load_tracing() { g_settings_load_trace = nullptr; }

bool save_settings_utf8(const std::string& utf8_json, std::string& err_out) {
    err_out.clear();
    std::vector<unsigned char> out;
    if (!encrypt_settings_blob(utf8_json, out, err_out))
        return false;
    const fs::path sp = get_settings_json_path();
    if (!write_file_bytes(sp, out.data(), out.size())) {
        err_out = "failed to write settings.json";
        return false;
    }
    {
        std::lock_guard<std::mutex> c_lock(g_settings_load_mutex);
        invalidate_settings_cache();
    }
    return true;
}

/// Ensures @c workbench.<slot>.chrome exists for exports (and any missing sub-keys).
static void ensure_workbench_chrome_keys_for_export(
    json& j, const char* workbench_slot, bool def_menu, bool def_status, bool def_ribbon)
{
    const char* slot = (workbench_slot && workbench_slot[0]) ? workbench_slot : "main";
    if (!j.is_object())
        j = json::object();
    if (!j.contains("workbench") || !j["workbench"].is_object())
        j["workbench"] = json::object();
    if (!j["workbench"].contains(slot) || !j["workbench"][slot].is_object())
        j["workbench"][slot] = json::object();
    json& w = j["workbench"][slot];
    if (!w.contains("chrome") || !w["chrome"].is_object())
        w["chrome"] = json::object();
    json& c = w["chrome"];
    if (!c.contains("show_menu") || !c["show_menu"].is_boolean())
        c["show_menu"] = def_menu;
    if (!c.contains("show_status_bar") || !c["show_status_bar"].is_boolean())
        c["show_status_bar"] = def_status;
    if (!c.contains("show_ribbon_strip") || !c["show_ribbon_strip"].is_boolean())
        c["show_ribbon_strip"] = def_ribbon;
}

static void ensure_workbench_window_defaults_for_export(json& j, const char* workbench_slot, int def_w, int def_h)
{
    const char* slot = (workbench_slot && workbench_slot[0]) ? workbench_slot : "main";
    if (!j.is_object())
        j = json::object();
    if (!j.contains("workbench") || !j["workbench"].is_object())
        j["workbench"] = json::object();
    if (!j["workbench"].contains(slot) || !j["workbench"][slot].is_object())
        j["workbench"][slot] = json::object();
    json& w = j["workbench"][slot];
    if (!w.contains("window_defaults") || !w["window_defaults"].is_object())
        w["window_defaults"] = json::object();
    json& d = w["window_defaults"];
    if (!d.contains("width") || !d["width"].is_number_integer())
        d["width"]  = def_w;
    if (!d.contains("height") || !d["height"].is_number_integer())
        d["height"] = def_h;
}

static void ensure_ui_workbench_key_for_export(json& j)
{
    if (!j.is_object())
        j = json::object();
    if (!j.contains("ui") || !j["ui"].is_object())
        j["ui"] = json::object();
    if (!j["ui"].contains("workbench") || !j["ui"]["workbench"].is_string())
        j["ui"]["workbench"] = "main";
}

bool export_settings_file(const fs::path& path, bool encrypted, std::string& err_out) {
    const SettingsLoadLabelScope _ls("export_settings_file");
    err_out.clear();
    // Export always reads from the roaming profile (the central store), never from cwd.
    std::string raw;
    const fs::path profile_path = get_settings_json_path();
    if (fs::exists(profile_path)) {
        std::vector<unsigned char> raw_bytes;
        if (!read_file_bytes(profile_path, raw_bytes) || raw_bytes.empty()) {
            err_out = "failed to read " + profile_path.string();
            return false;
        }
        if (!decode_settings_raw_bytes(raw_bytes, raw, err_out))
            return false;
    }
    if (raw.empty())
        raw = "{}";
    try {
        json jdoc = json::parse(raw);
        ensure_workbench_chrome_keys_for_export(jdoc, "main", true, true, true);
        ensure_workbench_window_defaults_for_export(jdoc, "main", 1280, 860);
        // Chat-first workbench (`CChatSimpleWorkbench`); `ui.workbench` in live settings selects at startup.
        ensure_workbench_chrome_keys_for_export(jdoc, "chat", true, true, true);
        ensure_workbench_window_defaults_for_export(jdoc, "chat", 640, 720);
        ensure_workbench_chrome_keys_for_export(jdoc, "viewer", true, true, true);
        ensure_workbench_window_defaults_for_export(jdoc, "viewer", 960, 720);
        ensure_ui_workbench_key_for_export(jdoc);
        if (jdoc.contains("chat") && jdoc["chat"].is_object()) {
            jdoc["chat"].erase("api_key");
            jdoc["chat"].erase("base_url");
        }
        raw = jdoc.dump();
    } catch (const std::exception& e) {
        err_out = std::string("JSON: ") + e.what();
        return false;
    }
    if (encrypted) {
        std::vector<unsigned char> blob;
        if (!encrypt_settings_blob(raw, blob, err_out))
            return false;
        if (!write_file_bytes(path, blob.data(), blob.size())) {
            err_out = "failed to write export file";
            return false;
        }
        return true;
    }
    std::string pretty;
    try {
        pretty = nlohmann::json::parse(raw).dump(2);
    } catch (const std::exception& e) {
        err_out = std::string("JSON: ") + e.what();
        return false;
    }
    if (!write_utf8_plaintext_file(path, pretty, err_out))
        return false;
    return true;
}

bool import_settings_file(const fs::path& path, std::string& err_out) {
    err_out.clear();
    std::vector<unsigned char> raw;
    if (!read_file_bytes(path, raw) || raw.empty()) {
        err_out = "file is empty or could not be read";
        return false;
    }
    std::string json;
    if (!decode_settings_raw_bytes(raw, json, err_out))
        return false;
    if (json.empty())
        json = "{}";
    return save_settings_utf8(json, err_out);
}

// ---------------------------------------------------------------------------
// Subtrees (custom / history / any top-level key)
// ---------------------------------------------------------------------------

bool load_subtree(const std::string& key, nlohmann::json& out, std::string& err) {
    const SettingsLoadLabelScope _ls("load_subtree");
    out = json();
    err.clear();
    std::string raw;
    if (!load_settings_utf8(raw, err))
        return false;
    if (raw.empty())
        return true;
    try {
        const auto j = json::parse(raw);
        if (j.contains(key))
            out = j[key];
    } catch (const std::exception& e) {
        err = std::string("load_subtree: ") + e.what();
        return false;
    }
    return true;
}

bool save_subtree(const std::string& key, const nlohmann::json& value, std::string& err) {
    const SettingsLoadLabelScope _ls("save_subtree");
    err.clear();
    std::string raw;
    json j = json::object();
    if (load_settings_utf8(raw, err) && !raw.empty()) {
        try {
            j = json::parse(raw);
        } catch (const std::exception&) {
            j = json::object();
        }
    } else if (!err.empty()) {
        return false;
    }
    j[key] = value;
    return save_settings_utf8(j.dump(2), err);
}

bool merge_subtree_object(const std::string& key, const nlohmann::json& fields, std::string& err) {
    const SettingsLoadLabelScope _ls("merge_subtree_object");
    err.clear();
    if (!fields.is_object()) {
        err = "merge_subtree_object: `fields` must be a JSON object";
        return false;
    }
    std::string raw;
    json j = json::object();
    if (load_settings_utf8(raw, err) && !raw.empty()) {
        try {
            j = json::parse(raw);
        } catch (const std::exception&) {
            j = json::object();
        }
    } else if (!err.empty()) {
        return false;
    }
    if (!j.contains(key) || !j[key].is_object())
        j[key] = json::object();
    for (const auto& [k, v] : fields.items())
        j[key][k] = v;
    return save_settings_utf8(j.dump(2), err);
}

bool load_custom_data(nlohmann::json& out, std::string& err) {
    return load_subtree(SettingsSubtreeKeys::custom, out, err);
}

bool save_custom_data(const nlohmann::json& value, std::string& err) {
    return save_subtree(SettingsSubtreeKeys::custom, value, err);
}

bool load_history_data(nlohmann::json& out, std::string& err) {
    return load_subtree(SettingsSubtreeKeys::history, out, err);
}

bool save_history_data(const nlohmann::json& value, std::string& err) {
    return save_subtree(SettingsSubtreeKeys::history, value, err);
}

// ---------------------------------------------------------------------------
// Explorer shell presets (settings.json["explorer_presets"])
// ---------------------------------------------------------------------------

namespace {

bool json_to_explorer_preset(const json& o, ExplorerPreset& out) {
    out = ExplorerPreset{};
    if (!o.is_object())
        return false;
    if (!o.contains("id") || !o["id"].is_string())
        return false;
    if (!o.contains("label") || !o["label"].is_string())
        return false;
    if (!o.contains("op") || !o["op"].is_string())
        return false;
    out.id    = o["id"].get<std::string>();
    out.label = o["label"].get<std::string>();
    out.op    = o["op"].get<std::string>();
    if (out.id.empty() || out.op.empty())
        return false;
    if (o.contains("args") && o["args"].is_object())
        out.args = o["args"];
    return true;
}

} // namespace

bool load_explorer_presets(std::vector<ExplorerPreset>& out, std::string& err) {
    out.clear();
    err.clear();
    json j;
    if (!load_subtree(SettingsSubtreeKeys::explorer_presets, j, err))
        return false;
    if (j.is_null() || j.empty())
        return true;
    try {
        if (!j.is_object())
            return true;
        const auto it = j.find("items");
        if (it == j.end() || !it->is_array())
            return true;
        for (const auto& el : *it) {
            ExplorerPreset p;
            if (json_to_explorer_preset(el, p))
                out.push_back(std::move(p));
        }
    } catch (const std::exception& e) {
        err = std::string("load_explorer_presets: ") + e.what();
        return false;
    }
    return true;
}

bool save_explorer_presets(const std::vector<ExplorerPreset>& items, std::string& err) {
    err.clear();
    json arr = json::array();
    for (const auto& p : items) {
        if (p.id.empty() || p.op.empty())
            continue;
        json o{{"id", p.id}, {"label", p.label}, {"op", p.op}};
        if (!p.args.is_null() && p.args.is_object() && !p.args.empty())
            o["args"] = p.args;
        else
            o["args"] = json::object();
        arr.push_back(std::move(o));
    }
    const json doc{{"version", 1}, {"items", std::move(arr)}};
    return save_subtree(SettingsSubtreeKeys::explorer_presets, doc, err);
}

// ---------------------------------------------------------------------------
// Web chat (WebView2) — prompt history + quick actions
// ---------------------------------------------------------------------------

bool load_chat_web(json& out, std::string& err) {
    err.clear();
    if (!load_subtree(SettingsSubtreeKeys::chat_web, out, err))
        return false;
    if (out.is_null() || !out.is_object())
        out = json::object();
    return true;
}

bool save_chat_web(const json& doc, std::string& err) {
    err.clear();
    return save_subtree(SettingsSubtreeKeys::chat_web, doc, err);
}

// ---------------------------------------------------------------------------
// Provider management
// ---------------------------------------------------------------------------

static const ProviderDefaults kProviders[] = {
    { "google",     "Google / Gemini",  "https://generativelanguage.googleapis.com/v1beta",
      { "gemini-3-pro-image-preview", "gemini-3.1-flash-image-preview", "gemini-2.0-flash-exp" } },
    { "openai",     "OpenAI",           "https://api.openai.com/v1",
      { "gpt-image-1", "dall-e-3", "gpt-4o" } },
    { "replicate",  "Replicate",        "https://api.replicate.com/v1",
      { "google/nano-banana-pro", "google/nano-banana", "black-forest-labs/flux-schnell" } },
    { "openrouter", "OpenRouter",        "https://openrouter.ai/api/v1",
      { "openai/gpt-4o-mini", "anthropic/claude-3.5-sonnet", "google/gemini-2.0-flash-001" } },
    { "pixlwiz",    "PixlWiz",           "https://llm.polymech.info/v1",
      {} },
    { "elevenlabs", "ElevenLabs",        "https://api.elevenlabs.io/v1",
      {} },
};

const ProviderDefaults* known_providers(int* count_out) {
    if (count_out)
        *count_out = static_cast<int>(std::size(kProviders));
    return kProviders;
}

bool load_providers(ProviderMap& out, std::string& err) {
    const SettingsLoadLabelScope _ls("load_providers");
    out.clear();

    // Initialise known rows for UI display. Runtime selection comes from explicit
    // chat/tool settings, not from provider-row default_model.
    int n = 0;
    for (const auto* p = known_providers(&n); n--; ++p) {
        ProviderEntry e;
        e.base_url      = p->base_url;
        out[p->name]    = std::move(e);
    }

    std::string raw_json;
    if (!load_settings_utf8(raw_json, err))
        return false;
    if (raw_json.empty())
        return true;

    try {
        auto j = json::parse(raw_json);
        if (j.contains("providers") && j["providers"].is_object()) {
            for (auto& [name, val] : j["providers"].items()) {
                ProviderEntry& e = out[name];
                if (val.contains("api_key")       && val["api_key"].is_string())
                    e.api_key       = val["api_key"].get<std::string>();
                if (val.contains("base_url")      && val["base_url"].is_string())
                    e.base_url      = val["base_url"].get<std::string>();
                if (val.contains("default_model") && val["default_model"].is_string())
                    e.default_model = val["default_model"].get<std::string>();
            }
        }
    } catch (const std::exception& ex) {
        err = std::string("JSON parse error: ") + ex.what();
        return false;
    }
    return true;
}

bool save_providers(const ProviderMap& providers, std::string& err) {
    const SettingsLoadLabelScope _ls("save_providers");
    // Load existing JSON to preserve other keys (e.g. prompt_presets)
    std::string raw_json;
    json j = json::object();
    if (load_settings_utf8(raw_json, err) && !raw_json.empty()) {
        try { j = json::parse(raw_json); } catch (...) { j = json::object(); }
    }

    auto& jproviders     = j["providers"];
    if (!jproviders.is_object())
        jproviders = json::object();

    for (auto& [name, e] : providers) {
        jproviders[name]["api_key"]       = e.api_key;
        jproviders[name]["base_url"]      = e.base_url;
        jproviders[name]["default_model"] = e.default_model;
    }

    // Remove legacy active_provider key (no longer used per design).
    j.erase("active_provider");

    return save_settings_utf8(j.dump(2), err);
}

// ---------------------------------------------------------------------------
// Window / dock layout
// ---------------------------------------------------------------------------

namespace {

const char* workbench_slot_or_main(const char* s)
{
    return (s && s[0]) ? s : "main";
}

void workbench_chrome_apply_slot_defaults(media::settings::WorkbenchChromeSettings& out, const char* workbench_slot)
{
    const char* slot = workbench_slot_or_main(workbench_slot);
    if (std::strcmp(slot, "chat") == 0) {
        out.show_main_menu     = false;
        out.show_status_bar    = false;
        out.show_ribbon_strip  = true;
    } else if (std::strcmp(slot, "viewer") == 0) {
        out.show_main_menu    = true;
        out.show_status_bar   = true;
        out.show_ribbon_strip = true;
    }
}

} // namespace

bool load_workbench_window_defaults(WorkbenchWindowDefaults& out, std::string& err, const char* workbench_slot)
{
    return media::layout::LayoutStore::LoadWorkbenchWindowDefaults(out, err, workbench_slot);
}

bool load_window_layout(WindowLayout& out, std::string& err, const char* workbench_slot) {
    return media::layout::LayoutStore::LoadWindowLayout(out, err, workbench_slot);
}

bool save_window_layout(const WindowLayout& layout, std::string& err, const char* workbench_slot) {
    return media::layout::LayoutStore::SaveWindowLayout(layout, err, workbench_slot);
}

bool set_filetree_show_shell_frames(bool show, std::string& err, const char* workbench_slot) {
    return media::layout::LayoutStore::SetFiletreeShowShellFrames(show, err, workbench_slot);
}

bool set_filetree_filter_mask(const std::string& mask, std::string& err, const char* workbench_slot) {
    return media::layout::LayoutStore::SetFiletreeFilterMask(mask, err, workbench_slot);
}

bool load_workbench_chrome(WorkbenchChromeSettings& out, std::string& err, const char* workbench_slot)
{
    const SettingsLoadLabelScope _ls("workbench_chrome");
    out  = WorkbenchChromeSettings{};
    workbench_chrome_apply_slot_defaults(out, workbench_slot);
    err.clear();
    if (g_ui_reset_session)
        return true;
    std::string raw;
    if (!load_settings_utf8(raw, err))
        return false;
    if (raw.empty())
        return true;
    try {
        const auto  j     = json::parse(raw);
        const char* slot  = workbench_slot_or_main(workbench_slot);
        if (!j.contains("workbench") || !j["workbench"].is_object())
            return true;
        if (!j["workbench"].contains(slot) || !j["workbench"][slot].is_object())
            return true;
        const auto& w = j["workbench"][slot];
        if (!w.contains("chrome") || !w["chrome"].is_object())
            return true;
        const auto& c   = w["chrome"];
        bool def_menu = true, def_status = true, def_ribbon = true;
        if (std::strcmp(slot, "chat") == 0) {
            def_menu = false;
            def_status = false;
        }
        out.show_main_menu    = c.value("show_menu", def_menu);
        out.show_status_bar   = c.value("show_status_bar", def_status);
        out.show_ribbon_strip = c.value("show_ribbon_strip", def_ribbon);
    } catch (const std::exception& ex) {
        err = std::string("workbench_chrome: ") + ex.what();
        return false;
    }
    return true;
}

static std::string g_workbench_id_cli_once;

void set_ui_reset_session(bool active) noexcept
{
    g_ui_reset_session = active;
}

bool ui_reset_session() noexcept
{
    return g_ui_reset_session;
}

void set_ui_workbench_id_cli_override(const char* id)
{
    g_workbench_id_cli_once.clear();
    if (id && id[0]) {
        g_workbench_id_cli_once = id;
    }
}

// Viewer app CLI override (e.g., --app agent-flow)
static std::string g_viewer_app_cli_once;

void set_ui_viewer_app_cli_override(const char* app)
{
    g_viewer_app_cli_once.clear();
    if (app && app[0]) {
        g_viewer_app_cli_once = app;
    }
}

bool peek_ui_viewer_app_cli_override(std::string& out) noexcept
{
    if (g_viewer_app_cli_once.empty())
        return false;
    out = g_viewer_app_cli_once;
    return true;
}

static bool workbench_id_from_settings_utf8(const std::string& raw, std::string& out, std::string& err)
{
    out = "main";
    if (raw.empty())
        return true;
    try {
        const auto j = json::parse(raw);
        if (!j.is_object() || !j.contains("ui") || !j["ui"].is_object() || !j["ui"].contains("workbench"))
            return true;
        const auto& w = j["ui"]["workbench"];
        if (!w.is_string())
            return true;
        const std::string id = w.get<std::string>();
        if (id == "chat")
            out = "chat";
        else if (id == "viewer")
            out = "viewer";
    } catch (const std::exception& ex) {
        err = std::string("ui.workbench: ") + ex.what();
        return false;
    }
    return true;
}

bool peek_settings_ui_workbench_id(std::string& out, std::string& err)
{
    out = "main";
    err.clear();
    if (!g_workbench_id_cli_once.empty()) {
        out = g_workbench_id_cli_once;
        return true;
    }
    if (g_ui_reset_session) {
        out = "main";
        return true;
    }
    std::string raw;
    if (!load_settings_utf8(raw, err))
        return false;
    return workbench_id_from_settings_utf8(raw, out, err);
}

bool load_settings_ui_workbench_id(std::string& out, std::string& err)
{
    out = "main";
    err.clear();
    if (!g_workbench_id_cli_once.empty()) {
        out = g_workbench_id_cli_once;
        g_workbench_id_cli_once.clear();
        return true;
    }
    if (g_ui_reset_session) {
        out = "main";
        return true;
    }
    std::string raw;
    if (!load_settings_utf8(raw, err))
        return false;
    return workbench_id_from_settings_utf8(raw, out, err);
}

// ── Appearance ───────────────────────────────────────────────────────────────

static const char* theme_to_str(Theme t) {
    switch (t) {
    case Theme::Light: return "light";
    case Theme::Dark:  return "dark";
    case Theme::System:
    default:           return "system";
    }
}
static Theme theme_from_str(const std::string& s) {
    if (s == "light") return Theme::Light;
    if (s == "dark")  return Theme::Dark;
    return Theme::System;
}

static const char* kDisplayLangCodes[] = {"en", "es", "de", "it", "fr"};

static void normalize_display_language(std::string& s) {
    if (s.empty()) {
        s = "en";
        return;
    }
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    for (const char* ok : kDisplayLangCodes) {
        if (s == ok)
            return;
    }
    s = "en";
}

bool load_appearance(AppearanceSettings& out, std::string& err) {
    const SettingsLoadLabelScope _ls("appearance");
    out = AppearanceSettings{};
    std::string raw;
    if (!load_settings_utf8(raw, err) || raw.empty())
        return true;          // first run / missing file → defaults
    try {
        auto j = json::parse(raw);
        if (j.contains("appearance") && j["appearance"].is_object()) {
            const auto& a = j["appearance"];
            if (a.contains("theme") && a["theme"].is_string())
                out.theme = theme_from_str(a["theme"].get<std::string>());
            if (a.contains("font_size_extra_pt") && a["font_size_extra_pt"].is_number_integer()) {
                int v = a["font_size_extra_pt"].get<int>();
                if (v < 0) v = 0; if (v > 8) v = 8;
                out.font_size_extra_pt = v;
            }
            if (a.contains("display_language") && a["display_language"].is_string())
                out.display_language = a["display_language"].get<std::string>();
            normalize_display_language(out.display_language);
        }
    } catch (const std::exception& e) {
        err = std::string("appearance parse: ") + e.what();
        return false;
    }
    return true;
}

bool save_appearance(const AppearanceSettings& a, std::string& err) {
    const SettingsLoadLabelScope _ls("save_appearance");
    std::string raw;
    json j = json::object();
    if (load_settings_utf8(raw, err) && !raw.empty()) {
        try { j = json::parse(raw); } catch (...) { j = json::object(); }
    }
    auto& ap = j["appearance"];
    ap["theme"]              = theme_to_str(a.theme);
    int v = a.font_size_extra_pt;
    if (v < 0) v = 0; if (v > 8) v = 8;
    ap["font_size_extra_pt"] = v;
    {
        std::string lang = a.display_language;
        normalize_display_language(lang);
        ap["display_language"] = lang;
    }
    return save_settings_utf8(j.dump(2), err);
}

std::string get_active_api_key(std::string& provider_name_out) {
    provider_name_out.clear();
    return {};
}

bool get_active_image_provider(ProviderEntry& out, std::string& provider_name_out) {
    provider_name_out.clear();
    out = ProviderEntry{};
    return false;
}

std::string get_google_provider_api_key() {
    ProviderMap pm;
    std::string err;
    if (!media::settings::load_providers(pm, err))
        return {};
    auto it = pm.find("google");
    if (it == pm.end())
        return {};
    return it->second.api_key;
}

// ---------------------------------------------------------------------------
// Chat provider (settings.json["chat"])
// ---------------------------------------------------------------------------

bool load_chat_provider(ChatProviderSettings& out, std::string& err) {
    const SettingsLoadLabelScope _ls("load_chat_provider");
    out = ChatProviderSettings{};

    std::string raw;
    if (!load_settings_utf8(raw, err))
        return false;
    if (raw.empty())
        return true;

    try {
        auto j = json::parse(raw);
        if (!j.contains("chat") || !j["chat"].is_object())
            return true;
        const auto& c = j["chat"];
        if (c.contains("router")         && c["router"].is_string())          out.router         = c["router"].get<std::string>();
        if (c.contains("model")          && c["model"].is_string())           out.model          = c["model"].get<std::string>();
        if (c.contains("timeout_ms")     && c["timeout_ms"].is_number_integer())     out.timeout_ms     = c["timeout_ms"].get<int>();
        if (c.contains("max_iterations") && c["max_iterations"].is_number_integer()) out.max_iterations = c["max_iterations"].get<int>();
        if (c.contains("image_provider") && c["image_provider"].is_string()) out.image_provider = c["image_provider"].get<std::string>();
        if (c.contains("image_model") && c["image_model"].is_string()) out.image_model = c["image_model"].get<std::string>();
        if (c.contains("image_recognition_provider") && c["image_recognition_provider"].is_string())
            out.image_recognition_provider = c["image_recognition_provider"].get<std::string>();
        if (c.contains("image_recognition_model") && c["image_recognition_model"].is_string())
            out.image_recognition_model = c["image_recognition_model"].get<std::string>();
        if (c.contains("video_provider") && c["video_provider"].is_string())
            out.video_provider = c["video_provider"].get<std::string>();
        if (c.contains("video_model") && c["video_model"].is_string())
            out.video_model = c["video_model"].get<std::string>();
        if (c.contains("stt_provider") && c["stt_provider"].is_string())
            out.stt_provider = c["stt_provider"].get<std::string>();
        if (c.contains("stt_model") && c["stt_model"].is_string())
            out.stt_model = c["stt_model"].get<std::string>();
        if (c.contains("tts_provider") && c["tts_provider"].is_string())
            out.tts_provider = c["tts_provider"].get<std::string>();
        if (c.contains("tts_model") && c["tts_model"].is_string())
            out.tts_model = c["tts_model"].get<std::string>();
        if (c.contains("tts_voice_id") && c["tts_voice_id"].is_string())
            out.tts_voice_id = c["tts_voice_id"].get<std::string>();
        if (c.contains("api_mode") && c["api_mode"].is_string())
            out.api_mode = c["api_mode"].get<std::string>();
        // If the user has never set these in JSON, do not use struct defaults
        // ("replicate") as an implicit pick — `pm image meta` and image tools use
        // per-provider `default_model` for `chat.image_provider` only, not
        // top-level `active_provider` (see `apply_image_ai_cli_defaults_from_app` /
        // `apply_chat_image_defaults_for_tool_options`).
        if (!c.contains("image_provider") || !c["image_provider"].is_string()) out.image_provider.clear();
        if (!c.contains("image_model") || !c["image_model"].is_string()) out.image_model.clear();
        if (!c.contains("image_recognition_provider") || !c["image_recognition_provider"].is_string())
            out.image_recognition_provider.clear();
        if (!c.contains("image_recognition_model") || !c["image_recognition_model"].is_string()) out.image_recognition_model.clear();
        if (!c.contains("video_provider") || !c["video_provider"].is_string()) out.video_provider.clear();
        if (!c.contains("video_model") || !c["video_model"].is_string()) out.video_model.clear();
    } catch (const std::exception& e) {
        err = std::string("chat parse: ") + e.what();
        return false;
    }
    return true;
}

bool save_chat_provider(const ChatProviderSettings& s, std::string& err) {
    const SettingsLoadLabelScope _ls("save_chat_provider");
    std::string raw;
    json j = json::object();
    if (load_settings_utf8(raw, err) && !raw.empty()) {
        try { j = json::parse(raw); } catch (...) { j = json::object(); }
    }
    auto& c = j["chat"];
    if (!c.is_object()) c = json::object();
    c["router"]         = s.router;
    c["model"]          = s.model;
    c["timeout_ms"]     = s.timeout_ms;
    c["max_iterations"] = s.max_iterations;
    c["image_provider"]              = s.image_provider;
    c["image_model"]                 = s.image_model;
    c["image_recognition_provider"]  = s.image_recognition_provider;
    c["image_recognition_model"]     = s.image_recognition_model;
    c["video_provider"]              = s.video_provider;
    c["video_model"]                 = s.video_model;
    c["stt_provider"]                = s.stt_provider;
    c["stt_model"]                   = s.stt_model;
    c["tts_provider"]                = s.tts_provider;
    c["tts_model"]                   = s.tts_model;
    c["tts_voice_id"]                = s.tts_voice_id;
    // API keys and base URLs for chat are stored under `settings.json["providers"][id]`,
    // not in `chat` (see API Provider / keys dialog). Drop legacy fields from older files.
    c.erase("api_key");
    c.erase("base_url");
    return save_settings_utf8(j.dump(2), err);
}

// ---------------------------------------------------------------------------
// Batch sessions (sessions.json — plain JSON; FEATURE_ENCRYPT_SESSIONS later)
// ---------------------------------------------------------------------------

namespace {

/// Read sessions.json as a UTF-8 string.  Returns true even when the file
/// does not exist (out_json is empty in that case).
bool load_sessions_raw(std::string& out_json, std::string& err)
{
    out_json.clear();
    const fs::path p = get_sessions_json_path();
    if (!fs::exists(p))
        return true;

#ifdef FEATURE_ENCRYPT_SESSIONS
    // Future: reuse the PME1 pipeline.
    return load_settings_utf8(out_json, err);   // placeholder
#else
    std::ifstream ifs(p);
    if (!ifs.is_open()) {
        err = "sessions: cannot open " + p.string();
        return false;
    }
    out_json.assign(std::istreambuf_iterator<char>(ifs),
                    std::istreambuf_iterator<char>());
    return true;
#endif
}

bool save_sessions_raw(const std::string& utf8_json, std::string& err)
{
    const fs::path p = get_sessions_json_path();
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);

#ifdef FEATURE_ENCRYPT_SESSIONS
    return save_settings_utf8(utf8_json, err);  // placeholder
#else
    // Atomic write: write to a temp file then rename.
    const fs::path tmp = p.parent_path() / "sessions.json.tmp";
    {
        std::ofstream ofs(tmp, std::ios::trunc);
        if (!ofs.is_open()) {
            err = "sessions: cannot write " + tmp.string();
            return false;
        }
        ofs << utf8_json;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        err = "sessions: rename failed: " + ec.message();
        return false;
    }
    return true;
#endif
}

SessionItem item_from_json(const json& j)
{
    SessionItem it;
    if (j.contains("path")   && j["path"].is_string())   it.path   = j["path"].get<std::string>();
    if (j.contains("sha256") && j["sha256"].is_string())  it.sha256 = j["sha256"].get<std::string>();
    if (j.contains("status") && j["status"].is_string())  it.status = j["status"].get<std::string>();
    if (j.contains("error")  && j["error"].is_string())   it.error  = j["error"].get<std::string>();
    return it;
}

json item_to_json(const SessionItem& it)
{
    return json{{"path", it.path}, {"sha256", it.sha256},
                {"status", it.status}, {"error", it.error}};
}

PersistedSession session_from_json(const json& j)
{
    PersistedSession s;
    if (j.contains("session_id")  && j["session_id"].is_string())   s.session_id  = j["session_id"].get<std::string>();
    if (j.contains("op")          && j["op"].is_string())           s.op          = j["op"].get<std::string>();
    if (j.contains("options"))                                       s.options     = j["options"];
    if (j.contains("created_at")  && j["created_at"].is_string())   s.created_at  = j["created_at"].get<std::string>();
    if (j.contains("updated_at")  && j["updated_at"].is_string())   s.updated_at  = j["updated_at"].get<std::string>();
    if (j.contains("items") && j["items"].is_array()) {
        for (const auto& it : j["items"])
            s.items.push_back(item_from_json(it));
    }
    return s;
}

json session_to_json(const PersistedSession& s)
{
    json items = json::array();
    for (const auto& it : s.items)
        items.push_back(item_to_json(it));
    return json{
        {"session_id",  s.session_id},
        {"op",          s.op},
        {"options",     s.options},
        {"items",       items},
        {"created_at",  s.created_at},
        {"updated_at",  s.updated_at}
    };
}

} // namespace

fs::path get_sessions_json_path()
{
    return get_config_dir() / "sessions.json";
}

bool load_sessions(std::vector<PersistedSession>& out, std::string& err)
{
    out.clear();
    std::string raw;
    if (!load_sessions_raw(raw, err))
        return false;
    if (raw.empty())
        return true;
    try {
        auto j = json::parse(raw);
        if (!j.contains("sessions") || !j["sessions"].is_array())
            return true;
        for (const auto& sj : j["sessions"])
            out.push_back(session_from_json(sj));
    } catch (const std::exception& e) {
        err = std::string("sessions parse: ") + e.what();
        return false;
    }
    return true;
}

bool save_session(const PersistedSession& s, std::string& err)
{
    std::vector<PersistedSession> all;
    if (!load_sessions(all, err))
        return false;

    bool found = false;
    for (auto& existing : all) {
        if (existing.session_id == s.session_id) {
            existing = s;
            found = true;
            break;
        }
    }
    if (!found)
        all.push_back(s);

    json root = json::object();
    root["sessions"] = json::array();
    for (const auto& sess : all)
        root["sessions"].push_back(session_to_json(sess));

    return save_sessions_raw(root.dump(2), err);
}

bool delete_session(const std::string& session_id, std::string& err)
{
    std::vector<PersistedSession> all;
    if (!load_sessions(all, err))
        return false;

    const auto before = all.size();
    all.erase(std::remove_if(all.begin(), all.end(),
        [&](const PersistedSession& s) { return s.session_id == session_id; }),
        all.end());

    if (all.size() == before)
        return true; // nothing to delete — not an error

    json root = json::object();
    root["sessions"] = json::array();
    for (const auto& sess : all)
        root["sessions"].push_back(session_to_json(sess));

    return save_sessions_raw(root.dump(2), err);
}

std::vector<PersistedSession> list_sessions(const std::string& op_filter)
{
    std::string err;
    std::vector<PersistedSession> all;
    load_sessions(all, err);
    if (op_filter.empty())
        return all;
    std::vector<PersistedSession> out;
    for (const auto& s : all)
        if (s.op == op_filter) out.push_back(s);
    return out;
}

} // namespace media::settings
