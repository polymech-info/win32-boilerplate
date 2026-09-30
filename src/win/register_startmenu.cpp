#include "register_startmenu.hpp"
#include "constants.hpp"
#include "settings_store.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")

namespace media::win {
namespace {

namespace fs = std::filesystem;

void startmenu_log_line(const std::string& msg) {
    const std::string full = "[register-startmenu] " + msg;
    std::cerr << full << "\n";
    try {
        std::error_code ec;
        const fs::path dir = media::settings::get_config_dir();
        fs::create_directories(dir, ec);
        std::ofstream out(dir / "register-startmenu.log", std::ios::app | std::ios::binary);
        if (!out) return;
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        char ts[48]{};
        sprintf_s(ts, "%04u-%02u-%02u %02u:%02u:%02u  ", (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                  (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        out << ts << msg << "\n";
    } catch (...) {
    }
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring current_exe_path() {
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1));
    return n == 0 ? std::wstring{} : std::wstring(buf.data(), n);
}

fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(id, 0, nullptr, &raw)) || !raw)
        return {};
    fs::path out(raw);
    ::CoTaskMemFree(raw);
    return out;
}

fs::path infer_install_root(const fs::path& media_exe, const std::wstring& override_root) {
    if (!override_root.empty())
        return fs::path(override_root);
    fs::path dir = media_exe.parent_path();
    if (_wcsicmp(dir.filename().c_str(), L"win-x64") == 0)
        return dir.parent_path();
    return dir;
}

bool write_shortcut(const fs::path& link_path,
                    const fs::path& target,
                    const std::wstring& args,
                    const fs::path& working_dir,
                    const fs::path& icon_path,
                    bool dry) {
    startmenu_log_line("shortcut " + wide_to_utf8(link_path.wstring()) + " -> " + wide_to_utf8(target.wstring())
                       + (args.empty() ? "" : (" args=" + wide_to_utf8(args))));
    if (dry)
        return true;

    std::error_code ec;
    fs::create_directories(link_path.parent_path(), ec);

    IShellLinkW* shell_link = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell_link));
    if (FAILED(hr) || !shell_link) {
        startmenu_log_line("CoCreateInstance(IShellLink) failed hr=" + std::to_string(static_cast<unsigned long>(hr)));
        return false;
    }

    shell_link->SetPath(target.c_str());
    if (!args.empty())
        shell_link->SetArguments(args.c_str());
    if (!working_dir.empty())
        shell_link->SetWorkingDirectory(working_dir.c_str());
    if (!icon_path.empty())
        shell_link->SetIconLocation(icon_path.c_str(), 0);

    IPersistFile* persist = nullptr;
    hr = shell_link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(hr) && persist) {
        hr = persist->Save(link_path.c_str(), TRUE);
        persist->Release();
    }
    shell_link->Release();
    if (FAILED(hr)) {
        startmenu_log_line("IPersistFile::Save failed hr=" + std::to_string(static_cast<unsigned long>(hr)));
        return false;
    }
    return true;
}

bool write_url_shortcut(const fs::path& link_path, const std::wstring& url, bool dry) {
    startmenu_log_line("url shortcut " + wide_to_utf8(link_path.wstring()) + " -> " + wide_to_utf8(url));
    if (dry)
        return true;

    std::error_code ec;
    fs::create_directories(link_path.parent_path(), ec);
    std::ofstream out(link_path, std::ios::binary);
    if (!out)
        return false;
    out << "[InternetShortcut]\r\nURL=" << wide_to_utf8(url) << "\r\n";
    return true;
}

void delete_if_exists(const fs::path& path, bool dry) {
    startmenu_log_line("delete " + wide_to_utf8(path.wstring()));
    if (dry) return;
    std::error_code ec;
    fs::remove(path, ec);
}

fs::path first_existing(const std::vector<fs::path>& candidates) {
    std::error_code ec;
    for (const auto& p : candidates) {
        if (!p.empty() && fs::exists(p, ec) && !ec)
            return p;
        ec.clear();
    }
    return {};
}

struct ShortcutSpec {
    std::wstring name;
    std::wstring kind;
    std::wstring target;
    std::wstring args;
    std::wstring working_dir;
    std::wstring icon;
    bool optional = false;
};

struct MenuSpec {
    std::wstring folder;
    std::vector<ShortcutSpec> shortcuts;
};

std::string trim_json(std::string s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

std::string replace_all(std::string s, const std::string& needle, const std::string& value) {
    if (needle.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) {
        s.replace(pos, needle.size(), value);
        pos += value.size();
    }
    return s;
}

std::string expand_menu_value(std::string value, const fs::path& root, const fs::path& media_exe, const fs::path& docs_html) {
    value = replace_all(std::move(value), "${root}", wide_to_utf8(root.wstring()));
    value = replace_all(std::move(value), "${exe}", wide_to_utf8(media_exe.wstring()));
    value = replace_all(std::move(value), "${exeDir}", wide_to_utf8(media_exe.parent_path().wstring()));
    value = replace_all(std::move(value), "${docs}", docs_html.empty() ? "" : wide_to_utf8(docs_html.wstring()));
    return value;
}

fs::path menu_json_path(const fs::path& media_exe, const fs::path& root) {
    return first_existing({media_exe.parent_path() / L"menu.json", root / L"menu.json"});
}

MenuSpec default_menu_spec(const std::wstring& folder_name, const fs::path& root, const fs::path& media_exe, const fs::path& docs_html) {
    auto path_u8 = [](const fs::path& p) { return wide_to_utf8(p.wstring()); };
    const std::string exe = path_u8(media_exe);
    const std::string exe_dir = path_u8(media_exe.parent_path());
    const std::string docs = docs_html.empty() ? std::string{} : path_u8(docs_html);
    const std::string uninstall = path_u8(root / L"Uninstall.exe");

    MenuSpec spec;
    spec.folder = folder_name;
    spec.shortcuts = {
        {L"PM-Image", L"app", utf8_to_wide(exe), L"--ui-preset=main", utf8_to_wide(exe_dir), utf8_to_wide(exe), false},
        {L"PM-Image Chat", L"app", utf8_to_wide(exe), L"--ui-preset=chat", utf8_to_wide(exe_dir), utf8_to_wide(exe), false},
        {L"PM Viewer", L"app", utf8_to_wide(exe), L"--ui-preset=viewer", utf8_to_wide(exe_dir), utf8_to_wide(exe), false},
        {L"PM-Image Resize", L"app", utf8_to_wide(exe), L"resize --ui-next", utf8_to_wide(exe_dir), utf8_to_wide(exe), false},
        {L"Overview", L"app", utf8_to_wide(docs + "\\product.html"), L"", utf8_to_wide(docs), utf8_to_wide(exe), true},
        {L"Help", L"app", utf8_to_wide(docs + "\\readme.html"), L"", utf8_to_wide(docs), utf8_to_wide(exe), true},
        {L"Help Integration", L"app", utf8_to_wide(docs + "\\integration.html"), L"", utf8_to_wide(docs), utf8_to_wide(exe), true},
        {L"Documentation", L"url", L"https://pixlwiz.com/user/cgo/pages/pixlwiz-documentation", L"", L"", L"", false},
        {L"Uninstall", L"app", utf8_to_wide(uninstall), L"", path_u8(root).empty() ? L"" : utf8_to_wide(path_u8(root)), utf8_to_wide(uninstall), true},
    };
    return spec;
}

MenuSpec load_menu_spec(const fs::path& media_exe, const fs::path& root, const std::wstring& fallback_folder, const fs::path& docs_html) {
    const fs::path menu_path = menu_json_path(media_exe, root);
    if (menu_path.empty()) {
        startmenu_log_line("menu.json not found; using built-in defaults");
        return default_menu_spec(fallback_folder, root, media_exe, docs_html);
    }

    try {
        std::ifstream in(menu_path, std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto doc = nlohmann::json::parse(trim_json(std::move(raw)));
        MenuSpec spec;
        const std::string folder = doc.value("folder", std::string{});
        spec.folder = folder.empty() ? fallback_folder : utf8_to_wide(folder);
        if (spec.folder.empty())
            spec.folder = pm::brand::k_vendor_w;

        const auto shortcuts = doc.value("shortcuts", nlohmann::json::array());
        if (!shortcuts.is_array()) {
            startmenu_log_line("menu.json shortcuts is not an array; using built-in defaults");
            return default_menu_spec(fallback_folder, root, media_exe, docs_html);
        }
        for (const auto& item : shortcuts) {
            if (!item.is_object())
                continue;
            const std::string name = item.value("name", std::string{});
            const std::string target = item.value("target", std::string{});
            if (name.empty() || target.empty())
                continue;
            ShortcutSpec s;
            s.name = utf8_to_wide(name);
            s.kind = utf8_to_wide(item.value("kind", std::string{"app"}));
            s.target = utf8_to_wide(expand_menu_value(target, root, media_exe, docs_html));
            s.args = utf8_to_wide(expand_menu_value(item.value("args", std::string{}), root, media_exe, docs_html));
            s.working_dir = utf8_to_wide(expand_menu_value(item.value("workingDir", std::string{"${exeDir}"}), root, media_exe, docs_html));
            s.icon = utf8_to_wide(expand_menu_value(item.value("icon", std::string{"${exe}"}), root, media_exe, docs_html));
            s.optional = item.value("optional", false);
            spec.shortcuts.push_back(std::move(s));
        }
        startmenu_log_line("loaded menu.json " + wide_to_utf8(menu_path.wstring())
                           + " shortcuts=" + std::to_string(spec.shortcuts.size()));
        if (spec.shortcuts.empty())
            return default_menu_spec(fallback_folder, root, media_exe, docs_html);
        return spec;
    } catch (const std::exception& e) {
        startmenu_log_line(std::string("menu.json parse failed; using built-in defaults: ") + e.what());
        return default_menu_spec(fallback_folder, root, media_exe, docs_html);
    }
}

} // namespace

int register_startmenu_run(const RegisterStartMenuOptions& opt) {
    const HRESULT hr_co = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool co_uninit = SUCCEEDED(hr_co);
    if (FAILED(hr_co) && hr_co != RPC_E_CHANGED_MODE) {
        std::cerr << "register-startmenu: CoInitializeEx failed\n";
        return 1;
    }

    const std::wstring media_w = opt.media_bin.empty() ? current_exe_path() : utf8_to_wide(opt.media_bin);
    if (media_w.empty()) {
        std::cerr << "register-startmenu: cannot resolve pm-image.exe path\n";
        if (co_uninit) ::CoUninitialize();
        return 1;
    }

    const fs::path media_exe = fs::absolute(fs::path(media_w));
    const fs::path root = infer_install_root(media_exe, utf8_to_wide(opt.install_root));
    const fs::path docs_html = first_existing({root / L"docs" / L"html", root / L"docs-html"});
    const fs::path programs = known_folder(FOLDERID_Programs);
    if (programs.empty()) {
        std::cerr << "register-startmenu: cannot resolve current-user Start Menu Programs folder\n";
        if (co_uninit) ::CoUninitialize();
        return 1;
    }

    const std::wstring fallback_folder = opt.folder.empty() ? std::wstring(pm::brand::k_vendor_w) : utf8_to_wide(opt.folder);
    const MenuSpec spec = load_menu_spec(media_exe, root, fallback_folder, docs_html);
    const fs::path menu_dir = programs / spec.folder;

    startmenu_log_line(std::string("run begin ")
                       + (opt.unregister ? "unregister" : "register")
                       + " media=" + wide_to_utf8(media_exe.wstring())
                       + " root=" + wide_to_utf8(root.wstring())
                       + " menu=" + wide_to_utf8(menu_dir.wstring())
                       + (opt.dry ? " dry=yes" : ""));

    if (opt.unregister) {
        for (const auto& shortcut : spec.shortcuts) {
            if (!shortcut.name.empty()) {
                const bool is_url = _wcsicmp(shortcut.kind.c_str(), L"url") == 0;
                delete_if_exists(menu_dir / (shortcut.name + (is_url ? L".url" : L".lnk")), opt.dry);
            }
        }
        // Legacy hardcoded shortcuts from the NSIS-only path before register-startmenu read menu.json.
        delete_if_exists(menu_dir / (std::wstring(pm::brand::k_app_display_w) + L".lnk"), opt.dry);
        delete_if_exists(menu_dir / L"Overview.lnk", opt.dry);
        delete_if_exists(menu_dir / L"Help.lnk", opt.dry);
        delete_if_exists(menu_dir / L"Help Integration.lnk", opt.dry);
        delete_if_exists(menu_dir / L"Uninstall.lnk", opt.dry);
        if (!opt.dry) {
            std::error_code ec;
            fs::remove(menu_dir, ec);
        }
        std::cout << "Removed Start Menu shortcuts from " << wide_to_utf8(menu_dir.wstring()) << "\n";
        if (co_uninit) ::CoUninitialize();
        return 0;
    }

    std::error_code ec;
    if (!fs::exists(media_exe, ec) || ec) {
        std::cerr << "register-startmenu: pm-image.exe not found: " << wide_to_utf8(media_exe.wstring()) << "\n";
        if (co_uninit) ::CoUninitialize();
        return 1;
    }

    bool ok = true;
    for (const auto& shortcut : spec.shortcuts) {
        if (shortcut.name.empty() || shortcut.target.empty())
            continue;
        const bool is_url = _wcsicmp(shortcut.kind.c_str(), L"url") == 0;
        if (is_url) {
            ok = write_url_shortcut(menu_dir / (shortcut.name + L".url"), shortcut.target, opt.dry) && ok;
            continue;
        }
        const fs::path target(shortcut.target);
        ec.clear();
        const bool target_exists = fs::exists(target, ec) && !ec;
        if (!target_exists && shortcut.optional) {
            startmenu_log_line("optional target missing; skip " + wide_to_utf8(shortcut.name)
                               + " target=" + wide_to_utf8(target.wstring()));
            continue;
        }
        if (!target_exists) {
            startmenu_log_line("required target missing: " + wide_to_utf8(target.wstring()));
            ok = false;
            continue;
        }
        fs::path work(shortcut.working_dir);
        if (work.empty())
            work = target.parent_path();
        fs::path icon(shortcut.icon);
        if (icon.empty())
            icon = media_exe;
        ok = write_shortcut(menu_dir / (shortcut.name + L".lnk"), target, shortcut.args, work, icon, opt.dry) && ok;
    }

    if (!ok) {
        std::cerr << "register-startmenu: failed to write one or more shortcuts\n";
        if (co_uninit) ::CoUninitialize();
        return 1;
    }
    std::cout << "Registered Start Menu shortcuts in " << wide_to_utf8(menu_dir.wstring()) << "\n";
    if (co_uninit) ::CoUninitialize();
    return 0;
}

} // namespace media::win
