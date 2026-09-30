#include "pm_image_settings.hpp"

#include "core/settings_archive.hpp"
#include "core/settings_store.hpp"
#include "core/settings_portable.hpp"
#include "logger/logger.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

void pm_image_register_settings(CLI::App& app, PmImageCliState& s) {
    s.settings_cmd = app.add_subcommand(
        "settings",
        "Import / export the app settings profile (UTF-8 JSON). "
        "macOS / Linux: portable profile (`~/Library/…/settings.json`, `~/.pm-image/settings.json`, or `--config-dir`; "
        "fallback `./config/settings.json` if HOME unset). "
        "Windows: live store path (see `settings path`); `export --encrypted` writes PME1. "
        "Top-level --settings=… (Windows) is a one-off read path for the process, not `settings import`.");
    s.settings_cmd->require_subcommand(1);
    s.settings_path_cmd = s.settings_cmd->add_subcommand(
        "path",
        "Print the canonical on-disk settings.json path for this OS (no file I/O) and exit.");
    s.settings_import_cmd = s.settings_cmd->add_subcommand(
        "import",
        "Replace the live profile store with the given UTF-8 JSON file (full document replace). "
        "Windows: accepts PME1 or JSON (same as in-app). Other OS: JSON object only. "
        "All OSes: --archive restores a profile ZIP.");
    s.settings_import_cmd->add_option("path", s.settings_import_path, "Source file (relative paths are from cwd)")
        ->required(true);
    s.settings_export_cmd = s.settings_cmd->add_subcommand(
        "export",
        "Write the current profile settings to a UTF-8 JSON file (default: settings.json in cwd). "
        "Windows: `--encrypted` writes PME1 instead of JSON. All OSes: `--archive` writes a profile ZIP.");
    s.settings_export_cmd->add_option("path", s.settings_export_path, "Output file (relative paths are from cwd)")
        ->default_str("settings.json");
    s.settings_import_cmd->add_flag(
        "--archive", s.settings_archive,
        "Read a profile ZIP exported by `settings export --archive`. Skips web* folders and never imports .settings-key.dat.");
#if defined(_WIN32)
    s.settings_export_cmd->add_flag(
        "--encrypted", s.settings_export_encrypted,
        "Write PME1 binary (DPAPI-bound key on this profile) instead of UTF-8 JSON.");
#endif
    s.settings_export_cmd->add_flag(
        "--archive", s.settings_archive,
        "Write a ZIP of the app profile. Skips web* WebView folders; Windows settings.json is portable JSON; .settings-key.dat is omitted.");
}

int pm_image_cmd_settings(CLI::App& app, PmImageCliState& st) {
    (void)app;

    if (st.settings_path_cmd && st.settings_path_cmd->parsed()) {
        std::cout << media::settings::get_settings_json_path().string() << "\n";
        return 0;
    }

    if (st.settings_export_cmd && st.settings_export_cmd->parsed()) {
        namespace fs = std::filesystem;
        const std::string export_path = (st.settings_archive && st.settings_export_path == "settings.json")
            ? std::string{"settings.zip"}
            : st.settings_export_path;
        const fs::path out = fs::absolute(fs::path(export_path));
        std::string    err;
#if defined(_WIN32)
        if (st.settings_archive && st.settings_export_encrypted) {
            std::cerr << "settings export: --archive and --encrypted cannot be combined\n";
            return 1;
        }
#else
        if (st.settings_export_encrypted) {
            std::cerr << "settings export: --encrypted is only supported on Windows (PME1 profile store)\n";
            return 1;
        }
#endif
        if (st.settings_archive) {
            if (!media::settings_archive::export_settings_archive_zip(out, err)) {
                std::cerr << "settings export --archive: " << err << "\n";
                return 1;
            }
            std::error_code canon_ec;
            const fs::path    shown   = fs::weakly_canonical(out, canon_ec);
            const std::string out_str = (canon_ec ? out : shown).string();
            const std::string msg = "settings export --archive: wrote " + out_str;
            std::cout << msg << "\n";
            logger::info(msg);
            return 0;
        }
#if defined(_WIN32)
        if (!media::settings::export_settings_file(out, st.settings_export_encrypted, err)) {
            std::cerr << "settings export: " << err << "\n";
            return 1;
        }
        std::error_code canon_ec;
        const fs::path    shown   = fs::weakly_canonical(out, canon_ec);
        const std::string out_str = (canon_ec ? out : shown).string();
        const std::string msg     = std::string("settings export: wrote ") + out_str
            + (st.settings_export_encrypted ? " (PME1)" : "");
        std::cout << msg << "\n";
        logger::info(msg);
        return 0;
#else
        nlohmann::json root;
        // Export reads directly from profile (not cwd), same as Windows export.
        if (!media::portable_settings::read_profile_direct(root, err)) {
            std::cerr << "settings export: " << err << "\n";
            return 1;
        }
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        if (ec) {
            std::cerr << "settings export: create_directories: " << ec.message() << "\n";
            return 1;
        }
        try {
            std::ofstream ofs(out, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                std::cerr << "settings export: cannot open " << out.string() << "\n";
                return 1;
            }
            ofs << root.dump(2);
            if (!ofs.good()) {
                std::cerr << "settings export: write failed: " << out.string() << "\n";
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "settings export: " << e.what() << "\n";
            return 1;
        }
        std::error_code canon_ec;
        const fs::path    shown   = fs::weakly_canonical(out, canon_ec);
        const std::string out_str = (canon_ec ? out : shown).string();
        std::cout << "settings export: wrote " << out_str << "\n";
        return 0;
#endif
    }

    if (st.settings_import_cmd && st.settings_import_cmd->parsed()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path abs_in = fs::absolute(fs::path(st.settings_import_path));
        if (!fs::exists(abs_in, ec) || !fs::is_regular_file(abs_in, ec)) {
            std::cerr << "settings import: not a file: " << abs_in.string() << "\n";
            return 1;
        }
        std::error_code canon_ec;
        const fs::path src = fs::weakly_canonical(abs_in, canon_ec);
        const fs::path in  = canon_ec ? abs_in : src;
        std::string err;
        if (st.settings_archive) {
            if (!media::settings_archive::import_settings_archive_zip(in, err)) {
                std::cerr << "settings import --archive: " << err << "\n";
                return 1;
            }
            std::cout << "settings import --archive: restored " << media::settings::get_config_dir().string() << "\n";
            return 0;
        }
#if defined(_WIN32)
        if (!media::settings::import_settings_file(in, err)) {
            std::cerr << "settings import: " << err << "\n";
            return 1;
        }
        std::cout << "settings import: wrote " << media::settings::get_settings_json_path().string() << "\n";
        return 0;
#else
        std::vector<unsigned char> raw(static_cast<size_t>(fs::file_size(in, ec)));
        if (ec || raw.empty()) {
            std::cerr << "settings import: empty or unreadable: " << in.string() << "\n";
            return 1;
        }
        {
            std::ifstream ifs(in, std::ios::binary);
            if (!ifs.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()))) {
                std::cerr << "settings import: read failed: " << in.string() << "\n";
                return 1;
            }
        }
        nlohmann::json root;
        try {
            root = nlohmann::json::parse(std::string(reinterpret_cast<const char*>(raw.data()), raw.size()));
        } catch (const std::exception& e) {
            std::cerr << "settings import: JSON parse: " << e.what() << "\n";
            return 1;
        }
        if (!root.is_object()) {
            std::cerr << "settings import: root must be a JSON object\n";
            return 1;
        }
        if (!media::portable_settings::write_settings_profile_json(root, err)) {
            std::cerr << "settings import: " << err << "\n";
            return 1;
        }
#if defined(__APPLE__)
        {
            std::string sync_err;
            if (!media::portable_settings::merge_settings_import_into_codeedit_prefs(root, sync_err)) {
                std::cerr << "settings import: " << sync_err << " (portable profile was written)\n";
            } else {
                const auto p = media::portable_settings::settings_codeedit_prefs_json_path();
                if (!p.empty()) {
                    std::cout << "settings import: merged pm-image keys into " << p.string() << "\n";
                }
            }
        }
#endif
        std::cout << "settings import: wrote " << media::settings::get_settings_json_path().string()
                  << "\n";
        return 0;
#endif
    }

    std::cerr << "settings: no settings subcommand parsed\n";
    return 1;
}
