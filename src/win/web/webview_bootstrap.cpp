#include "stdafx.h"
#include "win/web/webview_bootstrap.hpp"

#include "helpers/text_conv.hpp"
#include "logger/logger.h"
#include "win/settings_store.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <shlobj_core.h>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

namespace {

std::string wide_to_u8(const std::wstring& w)
{
    return pmui::wide_to_utf8(w);
}

fs::path module_exe_dir()
{
    std::wstring buf(MAX_PATH, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size())
        return {};
    buf.resize(n);
    return fs::path(buf).parent_path();
}

std::string path_to_u8(const fs::path& path)
{
    if (path.empty())
        return {};
    return wide_to_u8(path.wstring());
}

nlohmann::json load_app_features()
{
    const fs::path exe = module_exe_dir();
    std::vector<fs::path> candidates;
    if (!exe.empty()) {
        if (exe.filename() == L"win-x64")
            candidates.push_back(exe.parent_path() / L"shared" / L"features.json");
        candidates.push_back(exe / L"shared" / L"features.json");
        candidates.push_back(exe / L"features.json");
    }
    try {
        candidates.push_back(fs::current_path() / L"dist" / L"shared" / L"features.json");
    } catch (...) {
    }

    for (const auto& candidate : candidates) {
        try {
            std::ifstream in(candidate, std::ios::binary);
            if (!in)
                continue;
            nlohmann::json features;
            in >> features;
            if (features.is_object())
                return features;
        } catch (...) {
            logger::warn(std::string("[webview-bootstrap] failed to parse features.json: ")
                + wide_to_u8(candidate.wstring()));
        }
    }
    return nlohmann::json::object();
}

std::string known_folder_u8(const KNOWNFOLDERID& id)
{
    PWSTR path = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(hr) || !path)
        return {};
    std::wstring w(path);
    ::CoTaskMemFree(path);
    return wide_to_u8(w);
}

std::string env_u8(const wchar_t* name)
{
    DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0)
        return {};
    std::wstring value(n, L'\0');
    DWORD written = ::GetEnvironmentVariableW(name, value.data(), n);
    if (written == 0 || written >= n)
        return {};
    value.resize(written);
    return wide_to_u8(value);
}

std::string user_home_u8()
{
    std::string home = known_folder_u8(FOLDERID_Profile);
    if (!home.empty())
        return home;
    home = env_u8(L"USERPROFILE");
    if (!home.empty())
        return home;
    const std::string drive = env_u8(L"HOMEDRIVE");
    const std::string path = env_u8(L"HOMEPATH");
    return (!drive.empty() && !path.empty()) ? drive + path : std::string{};
}

std::string locale_name_u8()
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (::GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0)
        return wide_to_u8(locale);
    return {};
}

std::string ui_language_name_u8()
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    const LANGID lang = ::GetUserDefaultUILanguage();
    if (::LCIDToLocaleName(MAKELCID(lang, SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0) > 0)
        return wide_to_u8(locale);
    return {};
}

std::string processor_arch_name(WORD arch)
{
    switch (arch) {
    case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
    case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
    case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
    case PROCESSOR_ARCHITECTURE_ARM: return "arm";
    default: return "unknown";
    }
}

nlohmann::json os_version_json()
{
    nlohmann::json os;
    os["platform"] = "win32";
    os["name"] = "Windows";

    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto* ntdll = ::GetModuleHandleW(L"ntdll.dll");
    auto* rtlGetVersion = ntdll
        ? reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"))
        : nullptr;
    if (rtlGetVersion && rtlGetVersion(&version) == 0) {
        os["version"] = std::to_string(version.dwMajorVersion) + "."
            + std::to_string(version.dwMinorVersion) + "."
            + std::to_string(version.dwBuildNumber);
        os["major"] = version.dwMajorVersion;
        os["minor"] = version.dwMinorVersion;
        os["build"] = version.dwBuildNumber;
    }
    return os;
}

nlohmann::json launch_argv_json()
{
    nlohmann::json launch;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    nlohmann::json all = nlohmann::json::array();
    if (argv) {
        for (int i = 0; i < argc; ++i)
            all.push_back(wide_to_u8(argv[i] ? argv[i] : L""));
        ::LocalFree(argv);
    }
    launch["argv"] = all;
    launch["arguments"] = nlohmann::json::array();
    for (std::size_t i = 1; i < all.size(); ++i)
        launch["arguments"].push_back(all[i]);
    if (!all.empty())
        launch["executable"] = all[0];
    try {
        launch["cwd"] = wide_to_u8(fs::current_path().wstring());
    } catch (...) {
    }
    return launch;
}

nlohmann::json hardware_json()
{
    nlohmann::json hardware;

    SYSTEM_INFO si{};
    ::GetNativeSystemInfo(&si);
    hardware["cpu"] = {
        {"architecture", processor_arch_name(si.wProcessorArchitecture)},
        {"logicalProcessors", si.dwNumberOfProcessors},
        {"pageSize", si.dwPageSize},
    };

    MEMORYSTATUSEX msx{};
    msx.dwLength = sizeof(msx);
    if (::GlobalMemoryStatusEx(&msx)) {
        hardware["memory"] = {
            {"loadPercent", msx.dwMemoryLoad},
            {"totalPhysicalBytes", msx.ullTotalPhys},
            {"availablePhysicalBytes", msx.ullAvailPhys},
            {"totalVirtualBytes", msx.ullTotalVirtual},
            {"availableVirtualBytes", msx.ullAvailVirtual},
        };
    }
    return hardware;
}

nlohmann::json build_system_context()
{
    const fs::path exeDir = module_exe_dir();
    nlohmann::json ctx;
    ctx["schemaVersion"] = 1;
    ctx["os"] = os_version_json();
    ctx["paths"] = {
        {"configDir", path_to_u8(media::settings::get_config_dir())},
        {"installDir", path_to_u8(exeDir)},
    };
    ctx["user"] = {
        {"home", user_home_u8()},
        {"configDir", ctx["paths"]["configDir"]},
    };
    ctx["language"] = {
        {"locale", locale_name_u8()},
        {"uiLocale", ui_language_name_u8()},
    };
    try {
        media::settings::AppearanceSettings appearance;
        std::string err;
        if (media::settings::load_appearance(appearance, err))
            ctx["language"]["displayLanguage"] = appearance.display_language;
    } catch (...) {
    }
    ctx["launch"] = launch_argv_json();
    ctx["launch"]["installDir"] = ctx["paths"]["installDir"];
    ctx["hardware"] = hardware_json();
    return ctx;
}

} // namespace

namespace pmui {

std::wstring webview_app_features_bootstrap_js()
{
    const std::string json = load_app_features().dump();
    const std::string js =
        "(function(){"
        "var f=" + json + ";"
        "try{Object.freeze(f);}catch(e){}"
        "try{Object.defineProperty(window,'APP_FEATURES',{value:f,writable:false,configurable:false});}"
        "catch(e){window.APP_FEATURES=f;}"
        "})();";
    return pmui::utf8_to_wide(js);
}

std::wstring webview_system_context_bootstrap_js()
{
    const std::string json = build_system_context().dump();
    const std::string js =
        "(function(){"
        "var c=" + json + ";"
        "try{Object.freeze(c);if(c.os)Object.freeze(c.os);if(c.user)Object.freeze(c.user);"
        "if(c.language)Object.freeze(c.language);if(c.launch)Object.freeze(c.launch);"
        "if(c.hardware)Object.freeze(c.hardware);if(c.hardware&&c.hardware.cpu)Object.freeze(c.hardware.cpu);"
        "if(c.hardware&&c.hardware.memory)Object.freeze(c.hardware.memory);}catch(e){}"
        "try{Object.defineProperty(window,'SYSTEM_CONTEXT',{value:c,writable:false,configurable:false});}"
        "catch(e){window.SYSTEM_CONTEXT=c;}"
        "})();";
    return pmui::utf8_to_wide(js);
}

} // namespace pmui
