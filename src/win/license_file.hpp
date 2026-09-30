#pragma once

#include <filesystem>
#include <string>

#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE)

namespace media::win {

/** `%APPDATA%\\PolyMech\\pm-image\\license.json` (legacy envelope). */
std::filesystem::path license_json_path();

/** `%APPDATA%\\PolyMech\\pm-image\\license.dat` (noise-wrapped envelope from issuer). */
std::filesystem::path license_dat_path();

/** `license.dat` if it exists, else `license.json` (for display after import). */
std::filesystem::path license_active_storage_path();

/**
 * Verify Ed25519 detached signature on `payload_hex` (UTF-8 JSON), check machine_hash and expiry.
 * Prefers `license.dat` if present, else `license.json`.
 * @return true if a license file exists and is valid for this machine.
 */
bool license_is_valid(std::string& err_out);

/**
 * Copy @p src into the live license path. Accepts issuer `license.dat` (PMK1 blob) or legacy
 * `license.json`; verifies on this machine before keeping the file.
 */
bool license_import_from_file(const std::filesystem::path& src, std::string& err_out);

} // namespace media::win

#endif
