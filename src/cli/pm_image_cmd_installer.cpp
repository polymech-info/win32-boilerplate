#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_installer.hpp"

#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
#include "win/register_explorer.hpp"
#include "win/register_startmenu.hpp"
#include "win/settings_store.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::wstring utf8_to_wide_installer(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string wide_to_utf8_installer(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

fs::path current_exe_path_installer() {
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1));
    return n == 0 ? fs::path{} : fs::path(std::wstring(buf.data(), n));
}

fs::path infer_install_root_installer(const PmImageCliState& st, const fs::path& exe) {
    if (!st.installer_root.empty())
        return fs::absolute(fs::path(utf8_to_wide_installer(st.installer_root)));
    fs::path dir = exe.parent_path();
    if (_wcsicmp(dir.filename().c_str(), L"win-x64") == 0)
        return dir.parent_path();
    return dir;
}

int seed_profile_data(const fs::path& root, bool dry) {
    const fs::path src = root / L"data";
    std::error_code ec;
    if (!fs::exists(src, ec) || ec) {
        std::cout << "installer: no seed data folder found at " << wide_to_utf8_installer(src.wstring()) << "\n";
        return 0;
    }

    const fs::path dst = media::settings::get_config_dir();
    std::cout << "installer: seed profile data " << wide_to_utf8_installer(src.wstring())
              << " -> " << wide_to_utf8_installer(dst.wstring()) << "\n";
    if (dry)
        return 0;

    fs::create_directories(dst, ec);
    if (ec) {
        std::cerr << "installer: failed to create profile dir: " << wide_to_utf8_installer(dst.wstring()) << "\n";
        return 1;
    }

    for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path rel = fs::relative(it->path(), src, ec);
        if (ec) break;
        const fs::path out = dst / rel;
        if (it->is_directory(ec)) {
            fs::create_directories(out, ec);
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        if (fs::exists(out, ec)) {
            ec.clear();
            continue; // Match NSIS SetOverwrite off: preserve user-edited profile files.
        }
        fs::create_directories(out.parent_path(), ec);
        ec.clear();
        fs::copy_file(it->path(), out, fs::copy_options::none, ec);
        if (ec) {
            std::cerr << "installer: failed to copy seed file: " << wide_to_utf8_installer(out.wstring()) << "\n";
            return 1;
        }
    }
    if (ec) {
        std::cerr << "installer: failed while reading seed data\n";
        return 1;
    }
    return 0;
}

} // namespace
#endif

int pm_image_cmd_installer(CLI::App& app, PmImageCliState& st) {
    (void)app;
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    const fs::path exe = current_exe_path_installer();
    const fs::path root = infer_install_root_installer(st, exe);
    const std::string exe_u8 = wide_to_utf8_installer(exe.wstring());
    const std::string root_u8 = wide_to_utf8_installer(root.wstring());

    std::cout << "installer: " << (st.installer_uninstall ? "uninstall" : "install")
              << " root=" << root_u8 << "\n";

    int rc = 0;
    if (st.installer_uninstall) {
        if (!st.installer_no_explorer) {
            media::win::RegisterExplorerOptions o;
            o.unregister = true;
            o.dry = st.installer_dry;
            o.media_bin = exe_u8;
            if (media::win::register_explorer_run(o) != 0)
                rc = 1;
        }
        if (!st.installer_no_startmenu) {
            media::win::RegisterStartMenuOptions o;
            o.unregister = true;
            o.dry = st.installer_dry;
            o.media_bin = exe_u8;
            o.install_root = root_u8;
            if (media::win::register_startmenu_run(o) != 0)
                rc = 1;
        }
        return rc;
    }

    if (!st.installer_no_seed && seed_profile_data(root, st.installer_dry) != 0)
        rc = 1;
    if (!st.installer_no_explorer) {
        media::win::RegisterExplorerOptions o;
        o.dry = st.installer_dry;
        o.media_bin = exe_u8;
        if (media::win::register_explorer_run(o) != 0)
            rc = 1;
    }
    if (!st.installer_no_startmenu) {
        media::win::RegisterStartMenuOptions o;
        o.dry = st.installer_dry;
        o.media_bin = exe_u8;
        o.install_root = root_u8;
        if (media::win::register_startmenu_run(o) != 0)
            rc = 1;
    }
    return rc;
#else
    std::cerr << "media-img: installer is only available on Windows GUI builds.\n";
    return 1;
#endif
}

void pm_image_register_installer(CLI::App& app, PmImageCliState& s) {
    s.installer_cmd = app.add_subcommand(
        "installer",
        "Zip/unpacked install helper: seed profile data, register Explorer integration, and register Start Menu shortcuts.");
    s.installer_cmd->add_flag("--uninstall,--uninistall", s.installer_uninstall,
                              "Unregister Explorer and Start Menu integration. Profile data is kept.");
    s.installer_cmd->add_flag("--dry", s.installer_dry);
    s.installer_cmd->add_flag("--no-seed", s.installer_no_seed, "Skip copying missing dist/data files into the roaming profile.");
    s.installer_cmd->add_flag("--no-explorer", s.installer_no_explorer, "Skip register-explorer / unregister.");
    s.installer_cmd->add_flag("--no-startmenu", s.installer_no_startmenu, "Skip register-startmenu / unregister.");
    s.installer_cmd->add_option("--root", s.installer_root, "Install/zip root. Default: parent of win-x64, or exe directory.");
}
