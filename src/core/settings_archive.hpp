#pragma once

#include <filesystem>
#include <string>

namespace media::settings_archive {

/// Write a ZIP of the app profile (portable settings.json + profile files; skips web* dirs and .settings-key.dat).
bool export_settings_archive_zip(const std::filesystem::path& out, std::string& err_out);

/// Restore a profile ZIP produced by @ref export_settings_archive_zip.
bool import_settings_archive_zip(const std::filesystem::path& zip_path, std::string& err_out);

} // namespace media::settings_archive
