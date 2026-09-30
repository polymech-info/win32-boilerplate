// Offline license file (Ed25519) — optional activation path alongside trial.

#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE)

#include "license_file.hpp"
#include "license_pub_key.hpp"
#include "machine_fingerprint.hpp"

#include <sodium.h>

#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "settings_store.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace media::win {
namespace {

constexpr unsigned char kDatMagic[4] = {'P', 'M', 'K', '1'};
constexpr std::size_t kDatHeaderSize = 32;

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

bool read_file_binary(const fs::path& p, std::string& out) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open())
        return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

bool write_file_bytes(const fs::path& p, const std::string& utf8) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open())
        return false;
    ofs.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(ofs);
}

bool write_file_binary(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open())
        return false;
    ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(ofs);
}

bool hex_to_bin(const std::string& hex, std::vector<unsigned char>& out, std::string& err) {
    if (hex.size() % 2 != 0) {
        err = "invalid hex length";
        return false;
    }
    out.resize(hex.size() / 2);
    if (sodium_hex2bin(out.data(), out.size(), hex.c_str(), hex.size(), nullptr, nullptr, nullptr)
        != 0) {
        err = "sodium_hex2bin failed";
        return false;
    }
    return true;
}

static uint32_t read_le32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
           | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/** Extract UTF-8 JSON envelope from license.dat v1; @p raw is full file bytes. */
bool parse_license_dat_envelope(const std::string& raw, std::string& json_utf8_out, std::string& err) {
    json_utf8_out.clear();
    if (raw.size() < kDatHeaderSize) {
        err = "license.dat too small";
        return false;
    }
    const unsigned char* b = reinterpret_cast<const unsigned char*>(raw.data());
    if (std::memcmp(b, kDatMagic, 4) != 0) {
        err = "license.dat: bad magic (expected PMK1)";
        return false;
    }
    const uint32_t ver = read_le32(b + 4);
    if (ver != 1) {
        err = "license.dat: unsupported format version";
        return false;
    }
    const uint32_t json_off = read_le32(b + 8);
    const uint32_t json_len = read_le32(b + 12);
    if (json_off < kDatHeaderSize || json_len == 0) {
        err = "license.dat: invalid json offset/length";
        return false;
    }
    const std::size_t end = static_cast<std::size_t>(json_off) + static_cast<std::size_t>(json_len);
    if (end > raw.size()) {
        err = "license.dat: truncated (json extends past end of file)";
        return false;
    }
    json_utf8_out.assign(raw.data() + json_off, json_len);
    return true;
}

bool is_license_dat_magic(const std::string& raw) {
    return raw.size() >= 4 && std::memcmp(raw.data(), kDatMagic, 4) == 0;
}

/** Verify envelope JSON (same schema as legacy license.json file body). */
bool verify_license_envelope_json_string(const std::string& raw_json, std::string& err_out) {
    err_out.clear();
    if (!ensure_sodium(err_out))
        return false;

    json doc;
    try {
        doc = json::parse(raw_json);
    } catch (...) {
        err_out = "license JSON parse failed";
        return false;
    }

    if (!doc.contains("payload_hex") || !doc.contains("signature_hex")) {
        err_out = "license file missing payload_hex or signature_hex";
        return false;
    }
    const std::string payload_hex = doc["payload_hex"].get<std::string>();
    const std::string sig_hex = doc["signature_hex"].get<std::string>();

    std::vector<unsigned char> msg;
    std::vector<unsigned char> sig;
    if (!hex_to_bin(payload_hex, msg, err_out))
        return false;
    if (!hex_to_bin(sig_hex, sig, err_out))
        return false;
    if (sig.size() != 64) {
        err_out = "invalid signature length";
        return false;
    }

    if (crypto_sign_verify_detached(sig.data(), msg.data(), static_cast<unsigned long long>(msg.size()),
                                    kLicenseIssuerPublicKeyEd25519)
        != 0) {
        err_out = "Ed25519 signature verification failed";
        return false;
    }

    json inner;
    try {
        inner = json::parse(std::string(reinterpret_cast<const char*>(msg.data()), msg.size()));
    } catch (...) {
        err_out = "license payload JSON parse failed";
        return false;
    }

    if (!inner.contains("v") || !inner.contains("machine_hash")) {
        err_out = "license payload missing fields";
        return false;
    }
    if (inner["v"].get<int>() != 1) {
        err_out = "unsupported license payload version";
        return false;
    }
    const std::string mh = inner["machine_hash"].get<std::string>();
    if (mh != machine_fingerprint_hex()) {
        err_out = "license machine_hash does not match this PC";
        return false;
    }
    if (inner.contains("expiry") && !inner["expiry"].is_null()) {
        const std::int64_t exp = inner["expiry"].get<std::int64_t>();
        const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
        if (now > exp) {
            err_out = "license expired";
            return false;
        }
    }
    return true;
}

} // namespace

fs::path license_json_path() {
    return media::settings::get_config_dir() / "license.json";
}

fs::path license_dat_path() {
    return media::settings::get_config_dir() / "license.dat";
}

fs::path license_active_storage_path() {
    const fs::path dat = license_dat_path();
    if (fs::exists(dat))
        return dat;
    return license_json_path();
}

bool license_is_valid(std::string& err_out) {
    err_out.clear();
    const fs::path dat = license_dat_path();
    const fs::path json = license_json_path();

    if (fs::exists(dat)) {
        std::string raw;
        if (!read_file_binary(dat, raw) || raw.empty()) {
            err_out = "could not read license.dat";
            return false;
        }
        std::string env;
        if (!parse_license_dat_envelope(raw, env, err_out))
            return false;
        if (!verify_license_envelope_json_string(env, err_out))
            return false;
        return true;
    }

    if (fs::exists(json)) {
        std::string raw;
        if (!read_file_binary(json, raw) || raw.empty()) {
            err_out = "could not read license file";
            return false;
        }
        return verify_license_envelope_json_string(raw, err_out);
    }

    err_out = "no license file";
    return false;
}

bool license_import_from_file(const fs::path& src, std::string& err_out) {
    err_out.clear();
    std::string raw;
    if (!read_file_binary(src, raw) || raw.empty()) {
        err_out = "could not read source license file";
        return false;
    }

    if (is_license_dat_magic(raw)) {
        std::string env;
        if (!parse_license_dat_envelope(raw, env, err_out))
            return false;
        if (!verify_license_envelope_json_string(env, err_out)) {
            err_out = "imported license.dat does not verify on this machine: " + err_out;
            return false;
        }
        const fs::path dest = license_dat_path();
        if (!write_file_binary(dest, raw)) {
            err_out = "could not write license.dat";
            return false;
        }
        std::error_code ec;
        fs::remove(license_json_path(), ec);
        return true;
    }

    try {
        (void)json::parse(raw);
    } catch (...) {
        err_out = "source file is not valid license.dat or JSON";
        return false;
    }
    if (!verify_license_envelope_json_string(raw, err_out)) {
        err_out = "imported file does not verify on this machine: " + err_out;
        return false;
    }
    const fs::path dest = license_json_path();
    if (!write_file_bytes(dest, raw)) {
        err_out = "could not write license.json";
        return false;
    }
    std::error_code ec;
    fs::remove(license_dat_path(), ec);
    return true;
}

} // namespace media::win

#endif
