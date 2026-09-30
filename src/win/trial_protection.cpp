// Trial persistence: registry + hidden file + optional NTFS ADS, HMAC-SHA256 (libsodium).
// Windows only; compiled when FEATURE_TRIAL_CHECK is ON.

#if defined(_WIN32) && defined(FEATURE_TRIAL_CHECK)

#include "trial_protection.hpp"
#include "constants.hpp"
#include "machine_fingerprint.hpp"

#include <picosha2.h>
#include <sodium.h>
#include <nlohmann/json.hpp>

#include <Windows.h>
#include <KnownFolders.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib") // SHGetKnownFolderPath

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace media::win {
namespace {

static std::string bmsg(const char* tail) { return std::string(pm::brand::k_app_id_u8) + ": " + tail; }

constexpr int kTrialDays = 14;
constexpr int64_t kTrialSeconds = static_cast<int64_t>(kTrialDays) * 24 * 3600;

// HKCU\Software\<vendor>\<app-id> — value names are intentionally non-obvious.
constexpr wchar_t kValCfgBlob[] = L"CfgBlob";
constexpr wchar_t kValGdm[] = L"Gdm";

// Secondary store: %APPDATA%\Microsoft\clr.dat (+ :cache ADS on NTFS).
constexpr wchar_t kHiddenRelative[] = L"Microsoft\\clr.dat";
constexpr wchar_t kAdsStream[] = L":cache";

// Embedded HMAC key (32 bytes). Casual reset resistance only — not a secret against RE.
constexpr unsigned char kHmacKey[32] = {
    0x4b, 0x91, 0xe2, 0x07, 0x63, 0xac, 0x14, 0xf8, 0x22, 0x5d, 0x3c, 0x01, 0x9e, 0x77, 0xb0, 0x44,
    0xde, 0xfa, 0x58, 0x6a, 0xc3, 0x11, 0x29, 0x8f, 0x50, 0xa6, 0x73, 0x2e, 0x9b, 0x04, 0xd7, 0x66,
};

std::mutex g_sodium_init_mu;

bool ensure_sodium(std::string& err) {
    std::lock_guard<std::mutex> lock(g_sodium_init_mu);
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

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring utf8_to_wide(const std::string& u) {
    if (u.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, w.data(), n);
    return w;
}

fs::path appdata_microsoft_hidden_dat() {
    PWSTR path = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path);
    if (FAILED(hr) || path == nullptr) {
        if (path)
            CoTaskMemFree(path);
        return {};
    }
    fs::path base(path);
    CoTaskMemFree(path);
    return base / kHiddenRelative;
}

std::wstring ads_path(const fs::path& main_file) {
    return main_file.wstring() + kAdsStream;
}

bool read_file_bytes_w(const std::wstring& wpath, std::vector<std::uint8_t>& out) {
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD rd = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &rd, nullptr);
    CloseHandle(h);
    return ok && rd == out.size();
}

bool write_file_bytes_w(const std::wstring& wpath, const std::vector<std::uint8_t>& blob,
                        DWORD extra_attrs) {
    fs::path p(wpath);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | extra_attrs, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, blob.data(), static_cast<DWORD>(blob.size()), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == blob.size();
}

bool set_hidden_system(const fs::path& file) {
    const std::wstring w = file.wstring();
    DWORD a = GetFileAttributesW(w.c_str());
    if (a == INVALID_FILE_ATTRIBUTES)
        return false;
    a |= FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    return SetFileAttributesW(w.c_str(), a) != 0;
}

bool write_ads_optional(const fs::path& main_file, const std::vector<std::uint8_t>& blob) {
    const std::wstring w = ads_path(main_file);
    HANDLE h = CreateFileW(w.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, blob.data(), static_cast<DWORD>(blob.size()), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == blob.size();
}

bool read_ads_optional(const fs::path& main_file, std::vector<std::uint8_t>& out) {
    const std::wstring w = ads_path(main_file);
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    DWORD rd = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &rd, nullptr);
    CloseHandle(h);
    return ok && rd == out.size();
}

bool read_registry_cfg(std::vector<std::uint8_t>& out) {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_sw_vendor_app_w, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD cb = 0;
    if (RegQueryValueExW(hkey, kValCfgBlob, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS
        || type != REG_BINARY || cb == 0 || cb > 16 * 1024 * 1024) {
        RegCloseKey(hkey);
        return false;
    }
    out.resize(cb);
    LSTATUS st = RegQueryValueExW(hkey, kValCfgBlob, nullptr, &type, out.data(), &cb);
    RegCloseKey(hkey);
    return st == ERROR_SUCCESS;
}

bool write_registry_cfg(const std::vector<std::uint8_t>& blob) {
    HKEY hkey = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_sw_vendor_app_w, 0, nullptr, 0, KEY_SET_VALUE,
                                 nullptr, &hkey, nullptr);
    if (st != ERROR_SUCCESS)
        return false;
    st = RegSetValueExW(hkey, kValCfgBlob, 0, REG_BINARY, blob.data(),
                        static_cast<DWORD>(blob.size()));
    RegCloseKey(hkey);
    return st == ERROR_SUCCESS;
}

void delete_registry_cfg_value() {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_sw_vendor_app_w, 0, KEY_SET_VALUE, &hkey) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(hkey, kValCfgBlob);
    RegCloseKey(hkey);
}

bool read_godmode_dword(DWORD& out) {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_sw_vendor_app_w, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD cb = sizeof(DWORD);
    out = 0;
    LSTATUS st = RegQueryValueExW(hkey, kValGdm, nullptr, &type, reinterpret_cast<LPBYTE>(&out), &cb);
    RegCloseKey(hkey);
    return st == ERROR_SUCCESS && type == REG_DWORD;
}

bool write_godmode_dword(DWORD v) {
    HKEY hkey = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_sw_vendor_app_w, 0, nullptr, 0, KEY_SET_VALUE,
                                 nullptr, &hkey, nullptr);
    if (st != ERROR_SUCCESS)
        return false;
    st = RegSetValueExW(hkey, kValGdm, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(hkey);
    return st == ERROR_SUCCESS;
}

void delete_godmode_value() {
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, pm::brand::k_reg_sw_vendor_app_w, 0, KEY_SET_VALUE, &hkey) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(hkey, kValGdm);
    RegCloseKey(hkey);
}

struct TrialInner {
    int version = 1;
    std::int64_t install_time = 0;
    std::int64_t last_run_time = 0;
    std::string machine_hash;
    std::string trial_id;
};

bool inner_from_json(const json& j, TrialInner& out) {
    try {
        if (!j.contains("version") || !j.contains("install_time") || !j.contains("last_run_time")
            || !j.contains("machine_hash") || !j.contains("trial_id"))
            return false;
        out.version = j.at("version").get<int>();
        out.install_time = j.at("install_time").get<std::int64_t>();
        out.last_run_time = j.at("last_run_time").get<std::int64_t>();
        out.machine_hash = j.at("machine_hash").get<std::string>();
        out.trial_id = j.at("trial_id").get<std::string>();
        return out.version == 1 && !out.machine_hash.empty() && !out.trial_id.empty();
    } catch (...) {
        return false;
    }
}

json inner_to_json(const TrialInner& in) {
    json j;
    j["version"] = in.version;
    j["install_time"] = in.install_time;
    j["last_run_time"] = in.last_run_time;
    j["machine_hash"] = in.machine_hash;
    j["trial_id"] = in.trial_id;
    return j;
}

std::string wrap_outer(const std::string& data_utf8) {
    unsigned char mac[crypto_auth_hmacsha256_BYTES];
    crypto_auth_hmacsha256(mac, reinterpret_cast<const unsigned char*>(data_utf8.data()),
                           static_cast<unsigned long long>(data_utf8.size()), kHmacKey);
    char hex[crypto_auth_hmacsha256_BYTES * 2 + 1];
    sodium_bin2hex(hex, sizeof(hex), mac, sizeof(mac));
    json outer;
    outer["data"] = data_utf8;
    outer["mac"] = std::string(hex, crypto_auth_hmacsha256_BYTES * 2);
    return outer.dump();
}

bool unwrap_outer(const std::string& outer_utf8, TrialInner& inner) {
    json outer;
    try {
        outer = json::parse(outer_utf8);
    } catch (...) {
        return false;
    }
    if (!outer.contains("data") || !outer.contains("mac"))
        return false;
    std::string data;
    try {
        if (outer["data"].is_string())
            data = outer["data"].get<std::string>();
        else
            data = outer["data"].dump();
    } catch (...) {
        return false;
    }
    std::string mac_hex = outer["mac"].get<std::string>();
    if (mac_hex.size() != crypto_auth_hmacsha256_BYTES * 2)
        return false;
    unsigned char mac[crypto_auth_hmacsha256_BYTES];
    if (sodium_hex2bin(mac, sizeof(mac), mac_hex.c_str(), mac_hex.size(), nullptr, nullptr, nullptr)
        != 0)
        return false;
    if (crypto_auth_hmacsha256_verify(mac, reinterpret_cast<const unsigned char*>(data.data()),
                                      static_cast<unsigned long long>(data.size()), kHmacKey)
        != 0)
        return false;
    json jinner;
    try {
        jinner = json::parse(data);
    } catch (...) {
        return false;
    }
    return inner_from_json(jinner, inner);
}

std::vector<std::uint8_t> utf8_to_blob(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

std::string blob_to_utf8(const std::vector<std::uint8_t>& b) {
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

void schedule_registry_write(std::vector<std::uint8_t> blob) {
    std::thread([blob = std::move(blob)]() {
        const unsigned delay = 5u + randombytes_uniform(26u);
        std::this_thread::sleep_for(std::chrono::seconds(static_cast<int>(delay)));
        write_registry_cfg(blob);
    }).detach();
}

bool persist_all_stores(const std::string& outer_utf8, bool delay_registry) {
    const std::vector<std::uint8_t> blob = utf8_to_blob(outer_utf8);
    const fs::path hidden = appdata_microsoft_hidden_dat();
    if (hidden.empty())
        return false;
    const std::wstring wmain = hidden.wstring();
    if (!write_file_bytes_w(wmain, blob,
                            FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM
                                | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED))
        return false;
    set_hidden_system(hidden);
    (void)write_ads_optional(hidden, blob);
    if (delay_registry)
        schedule_registry_write(blob);
    else
        write_registry_cfg(blob);
    return true;
}

enum class ValidateStep { Ok, WrongMachine, ClockRollback, Expired };

ValidateStep validate_inner_steps(const TrialInner& in, std::int64_t now, std::string& err) {
    const std::string mh = machine_fingerprint_hex();
    if (in.machine_hash != mh) {
        err = bmsg("trial is not valid on this machine.");
        return ValidateStep::WrongMachine;
    }
    if (now < in.last_run_time - 120) {
        err = bmsg("system clock appears to have been set back; trial is no longer valid.");
        return ValidateStep::ClockRollback;
    }
    if (now > in.install_time + kTrialSeconds) {
        err = bmsg("the 14-day trial period has expired.");
        return ValidateStep::Expired;
    }
    return ValidateStep::Ok;
}

bool validate_inner_logic(const TrialInner& in, std::int64_t now, std::string& err) {
    return validate_inner_steps(in, now, err) == ValidateStep::Ok;
}

bool run_trial_flow(std::string& message_out) {
    std::string err;
    if (!ensure_sodium(err)) {
        message_out = err;
        return false;
    }

    const fs::path hidden = appdata_microsoft_hidden_dat();
    if (hidden.empty()) {
        message_out = bmsg("could not resolve %APPDATA%.");
        return false;
    }
    const std::wstring wmain = hidden.wstring();

    std::vector<std::uint8_t> reg_blob;
    std::vector<std::uint8_t> file_blob;
    const bool has_reg = read_registry_cfg(reg_blob) && !reg_blob.empty();
    const bool has_file = read_file_bytes_w(wmain, file_blob) && !file_blob.empty();

    std::vector<std::uint8_t> ads_blob;
    const bool has_ads = read_ads_optional(hidden, ads_blob) && !ads_blob.empty();

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    auto blobs_equal = [](const std::vector<std::uint8_t>& a,
                          const std::vector<std::uint8_t>& b) { return a == b; };

    // Both primary stores missing → first run (fresh install).
    if (!has_reg && !has_file) {
        TrialInner fresh{};
        fresh.version = 1;
        fresh.install_time = now;
        fresh.last_run_time = now;
        fresh.machine_hash = machine_fingerprint_hex();
        unsigned char tid[16];
        randombytes_buf(tid, sizeof(tid));
        char tid_hex[sizeof(tid) * 2 + 1];
        sodium_bin2hex(tid_hex, sizeof(tid_hex), tid, sizeof(tid));
        fresh.trial_id.assign(tid_hex, sizeof(tid) * 2);

        const std::string data = inner_to_json(fresh).dump();
        const std::string outer = wrap_outer(data);
        if (!persist_all_stores(outer, true)) {
            message_out = bmsg("could not persist trial state.");
            return false;
        }
        return true;
    }

    // One primary store missing: restore from the other after verification.
    std::vector<std::uint8_t> primary;
    if (has_reg && !has_file) {
        primary = reg_blob;
    } else if (!has_reg && has_file) {
        primary = file_blob;
    } else if (has_reg && has_file) {
        if (!blobs_equal(reg_blob, file_blob)) {
            message_out
                = bmsg("trial data is inconsistent between storage locations (tampering suspected).");
            return false;
        }
        primary = reg_blob;
    } else {
        message_out = bmsg("internal trial state error.");
        return false;
    }

    if (has_ads && !ads_blob.empty() && !blobs_equal(primary, ads_blob)) {
        message_out = bmsg("trial data is inconsistent (tampering suspected).");
        return false;
    }

    TrialInner inner{};
    if (!unwrap_outer(blob_to_utf8(primary), inner)) {
        message_out = bmsg("trial data could not be verified.");
        return false;
    }
    if (!validate_inner_logic(inner, now, message_out))
        return false;

    inner.last_run_time = now;
    const std::string data2 = inner_to_json(inner).dump();
    const std::string outer2 = wrap_outer(data2);
    if (!persist_all_stores(outer2, true)) {
        message_out = bmsg("could not update trial state.");
        return false;
    }
    return true;
}

} // namespace

bool trial_query_status(TrialStatus& st, std::string& err_out) {
    // return true;

    st = TrialStatus{};
    if (!ensure_sodium(err_out))
        return false;
    st.now = static_cast<std::int64_t>(std::time(nullptr));

    DWORD gdm = 0;
    if (read_godmode_dword(gdm) && gdm != 0) {
        st.godmode = true;
        st.state = TrialStatus::State::Godmode;
        return true;
    }

    const fs::path hidden = appdata_microsoft_hidden_dat();
    if (hidden.empty()) {
        err_out = bmsg("could not resolve %APPDATA%.");
        return false;
    }
    const std::wstring wmain = hidden.wstring();

    std::vector<std::uint8_t> reg_blob;
    std::vector<std::uint8_t> file_blob;
    const bool has_reg = read_registry_cfg(reg_blob) && !reg_blob.empty();
    const bool has_file = read_file_bytes_w(wmain, file_blob) && !file_blob.empty();

    std::vector<std::uint8_t> ads_blob;
    const bool has_ads = read_ads_optional(hidden, ads_blob) && !ads_blob.empty();

    auto blobs_equal = [](const std::vector<std::uint8_t>& a,
                          const std::vector<std::uint8_t>& b) { return a == b; };

    if (!has_reg && !has_file) {
        st.state = TrialStatus::State::PendingFirstRun;
        st.detail = "No trial data yet; the next normal launch will start the 14-day trial.";
        return true;
    }

    std::vector<std::uint8_t> primary;
    if (has_reg && !has_file) {
        primary = reg_blob;
    } else if (!has_reg && has_file) {
        primary = file_blob;
    } else if (has_reg && has_file) {
        if (!blobs_equal(reg_blob, file_blob)) {
            st.state = TrialStatus::State::Invalid;
            st.detail = "trial data is inconsistent between storage locations (tampering suspected).";
            return true;
        }
        primary = reg_blob;
    } else {
        st.state = TrialStatus::State::Invalid;
        st.detail = "internal trial state error.";
        return true;
    }

    if (has_ads && !ads_blob.empty() && !blobs_equal(primary, ads_blob)) {
        st.state = TrialStatus::State::Invalid;
        st.detail = "trial data is inconsistent (tampering suspected).";
        return true;
    }

    TrialInner inner{};
    if (!unwrap_outer(blob_to_utf8(primary), inner)) {
        st.state = TrialStatus::State::Invalid;
        st.detail = "trial data could not be verified (HMAC or format).";
        return true;
    }

    st.install_time = inner.install_time;
    st.last_run_time = inner.last_run_time;
    st.trial_id = inner.trial_id;
    st.expiry_time = inner.install_time + kTrialSeconds;

    std::string msg;
    const ValidateStep vs = validate_inner_steps(inner, st.now, msg);
    switch (vs) {
    case ValidateStep::Ok:
        st.state = TrialStatus::State::Active;
        st.remaining_seconds =
            static_cast<int>(std::max<std::int64_t>(0, st.expiry_time - st.now));
        break;
    case ValidateStep::Expired:
        st.state = TrialStatus::State::Expired;
        st.remaining_seconds = 0;
        st.detail = msg;
        break;
    case ValidateStep::WrongMachine:
    case ValidateStep::ClockRollback:
        st.state = TrialStatus::State::Invalid;
        st.detail = msg;
        break;
    }
    return true;
}

bool trial_is_godmode() {
    DWORD v = 0;
    if (!read_godmode_dword(v))
        return false;
    return v != 0;
}

bool trial_set_godmode(bool enable, std::string& err_out) {
    if (!ensure_sodium(err_out))
        return false;
    return write_godmode_dword(enable ? 1u : 0u);
}

bool trial_purge_all(std::string& err_out) {
    if (!ensure_sodium(err_out))
        return false;
    delete_registry_cfg_value();
    delete_godmode_value();

    const fs::path hidden = appdata_microsoft_hidden_dat();
    if (!hidden.empty()) {
        const std::wstring ads = ads_path(hidden);
        DeleteFileW(ads.c_str());
        DeleteFileW(hidden.wstring().c_str());
    }
    return true;
}

bool trial_enforce_or_exit(std::string& message_out) {
    return run_trial_flow(message_out);
}

} // namespace media::win

#else

// Non-Windows or trial disabled: empty translation unit.
namespace {
char trial_protection_dummy;
} // namespace

#endif // _WIN32 && FEATURE_TRIAL_CHECK
