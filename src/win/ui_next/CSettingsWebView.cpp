#include "stdafx.h"
#include "CSettingsWebView.hpp"

#include "ChatWebResource.h"
#include "ProviderModelRegistry.h"
#include "ProviderDlg_Replicate.h"
#include "helpers/theme.hpp"
#include "constants.hpp"
#include "win/settings_store.hpp"
#include "win/pixlwiz_auth_payload.hpp"
#include "win/pixlwiz_login_spawn.hpp"
#include "core/openrouter_provider_models_cli.hpp"
#include "core/pixlwiz_provider_models_cli.hpp"
#include "core/openai_models_cli.hpp"
#include "core/pixlwiz_user_budget.hpp"
#include "core/settings_archive.hpp"
#include "core/settings_runtime.hpp"
#include "cli/pm_image_register_cli.hpp"
#include "win/ribbon_commands.hpp"
#include "win/custom_commands_host.hpp"
#ifndef FEATURE_HOME_LLM_TOOLS
#define FEATURE_HOME_LLM_TOOLS 1
#endif
#ifndef FEATURE_HOME_LLM_SKILLS
#define FEATURE_HOME_LLM_SKILLS 1
#endif
#ifndef FEATURE_MCP_CLIENT
#define FEATURE_MCP_CLIENT 1
#endif
#ifndef FEATURE_CUSTOM_COMMANDS
#define FEATURE_CUSTOM_COMMANDS 0
#endif
#if FEATURE_HOME_LLM_SKILLS
#include "llm/agent_skills.hpp"
#endif
#include "llm/mcp_probe.hpp"
#include "lib/pm_zitadel_oauth.hpp"
#include "lib/provider_oauth.hpp"

#include <algorithm>
#include <cctype>
#include <commdlg.h>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <nlohmann/json.hpp>
#include <shlobj.h>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#if FEATURE_CUSTOM_COMMANDS
#include <physfs.h>
#endif

namespace {

bool pick_save_export_path(HWND hwnd, bool encrypted_default, std::wstring& out_path)
{
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    static const wchar_t kFilter[] = L"JSON settings (*.json)\0*.json\0"
                                     L"Encrypted (*.pmsettings)\0*.pmsettings\0"
                                     L"All files\0*.*\0\0";
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = encrypted_default ? 2u : 1u;
    ofn.lpstrDefExt = encrypted_default ? L"pmsettings" : L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&ofn))
        return false;
    out_path.assign(buf);
    return true;
}

bool pick_open_import_path(HWND hwnd, std::wstring& out_path)
{
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    static const wchar_t kFilter[] = L"JSON (*.json)\0*.json\0"
                                     L"Encrypted (*.pmsettings)\0*.pmsettings\0"
                                     L"All files\0*.*\0\0";
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1u;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&ofn))
        return false;
    out_path.assign(buf);
    return true;
}

bool pick_save_archive_path(HWND hwnd, std::wstring& out_path)
{
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    static const wchar_t kFilter[] = L"Profile archive (*.zip)\0*.zip\0"
                                     L"All files\0*.*\0\0";
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1u;
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&ofn))
        return false;
    out_path.assign(buf);
    return true;
}

bool pick_open_archive_path(HWND hwnd, std::wstring& out_path)
{
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    static const wchar_t kFilter[] = L"Profile archive (*.zip)\0*.zip\0"
                                     L"All files\0*.*\0\0";
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1u;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&ofn))
        return false;
    out_path.assign(buf);
    return true;
}

bool copy_utf8_to_clipboard(HWND owner, const std::string& text)
{
    const std::wstring w = pmui::utf8_to_wide(text);
    if (!::OpenClipboard(owner))
        return false;
    ::EmptyClipboard();
    const size_t nchars = w.size() + 1;
    const size_t nbytes = nchars * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, nbytes);
    if (!hMem) {
        ::CloseClipboard();
        return false;
    }
    void* p = ::GlobalLock(hMem);
    if (!p) {
        ::GlobalFree(hMem);
        ::CloseClipboard();
        return false;
    }
    memcpy(p, w.c_str(), nbytes);
    ::GlobalUnlock(hMem);
    if (!::SetClipboardData(CF_UNICODETEXT, hMem)) {
        ::GlobalFree(hMem);
        ::CloseClipboard();
        return false;
    }
    ::CloseClipboard();
    return true;
}

bool read_utf8_from_clipboard(HWND owner, std::string& out_text, std::string& err)
{
    out_text.clear();
    if (!::OpenClipboard(owner)) {
        err = "OpenClipboard failed";
        return false;
    }
    HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
    if (!h) {
        ::CloseClipboard();
        err = "Clipboard has no Unicode text";
        return false;
    }
    const wchar_t* p = static_cast<const wchar_t*>(::GlobalLock(h));
    if (!p) {
        ::CloseClipboard();
        err = "GlobalLock clipboard failed";
        return false;
    }
    std::wstring w(p);
    ::GlobalUnlock(h);
    ::CloseClipboard();
    out_text = pmui::wide_to_utf8(w);
    if (out_text.empty()) {
        err = "Clipboard text is empty";
        return false;
    }
    return true;
}

#if FEATURE_CUSTOM_COMMANDS
std::string trim_ascii(std::string s)
{
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

std::string normalize_command_json_text(std::string json_text)
{
    if (json_text.size() >= 3 && static_cast<unsigned char>(json_text[0]) == 0xEF &&
        static_cast<unsigned char>(json_text[1]) == 0xBB && static_cast<unsigned char>(json_text[2]) == 0xBF) {
        json_text.erase(0, 3);
    }
    return trim_ascii(std::move(json_text));
}

nlohmann::json default_commands_doc()
{
    return nlohmann::json{{"version", 1}, {"ribbon", {{"groups", nlohmann::json::array()}}}};
}

std::filesystem::path module_exe_dir()
{
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1u));
    if (n == 0u)
        return {};
    return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
}

std::filesystem::path dist_root_dir()
{
    const auto ex = module_exe_dir();
    if (ex.empty())
        return {};
    if (ex.filename() == L"win-x64")
        return ex.parent_path();
    return ex;
}

std::filesystem::path tabler_vendor_dir()
{
    const auto root = dist_root_dir();
    if (root.empty())
        return {};
    return root / L"vendor" / L"tabler-icons" / L"icons" / L"filled";
}

std::filesystem::path shared_assets_pfs()
{
    const auto root = dist_root_dir();
    if (root.empty())
        return {};
    return root / L"shared" / L"assets.pfs";
}

std::string read_file_utf8_best_effort(const std::filesystem::path& path)
{
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    } catch (...) {
        return {};
    }
}

bool physfs_read_svg(const std::string& name, std::string& out)
{
    out.clear();
    if (!PHYSFS_isInit())
        return false;
    const std::string file = name + ".svg";
    PHYSFS_File* f = PHYSFS_openRead(file.c_str());
    if (!f)
        return false;
    const PHYSFS_sint64 nlen = PHYSFS_fileLength(f);
    if (nlen <= 0) {
        (void)PHYSFS_close(f);
        return false;
    }
    out.assign(static_cast<size_t>(nlen), '\0');
    const PHYSFS_sint64 nread = PHYSFS_readBytes(f, out.data(), static_cast<PHYSFS_uint64>(static_cast<size_t>(nlen)));
    (void)PHYSFS_close(f);
    if (nread != nlen) {
        out.clear();
        return false;
    }
    return true;
}

nlohmann::json list_tabler_filled_icons()
{
    std::set<std::string> names;
    const auto vendor = tabler_vendor_dir();
    if (!vendor.empty()) {
        try {
            if (std::filesystem::is_directory(vendor)) {
                for (const auto& entry : std::filesystem::directory_iterator(vendor)) {
                    if (!entry.is_regular_file() || entry.path().extension() != L".svg")
                        continue;
                    names.insert(entry.path().stem().string());
                }
            }
        } catch (...) {
        }
    }

    bool physfsMounted = false;
    const auto pfs = shared_assets_pfs();
    if (!pfs.empty()) {
        if (!PHYSFS_isInit())
            (void)PHYSFS_init(nullptr);
        if (PHYSFS_isInit() && std::filesystem::is_regular_file(pfs)) {
            const std::string pfsU8 = pmui::wide_to_utf8(pfs.native());
            physfsMounted = PHYSFS_mount(pfsU8.c_str(), "/", 1) != 0;
            if (physfsMounted) {
                char** files = PHYSFS_enumerateFiles("/");
                for (char** i = files; i && *i; ++i) {
                    std::string file(*i);
                    if (file.size() > 4 && file.substr(file.size() - 4) == ".svg")
                        names.insert(file.substr(0, file.size() - 4));
                }
                PHYSFS_freeList(files);
            }
        }
    }

    nlohmann::json icons = nlohmann::json::array();
    for (const auto& name : names) {
        std::string svg;
        if (!vendor.empty())
            svg = read_file_utf8_best_effort(vendor / (name + ".svg"));
        if (svg.empty())
            (void)physfs_read_svg(name, svg);
        icons.push_back({{"name", name}, {"svg", svg}});
    }
    return icons;
}

nlohmann::json registered_cli_commands()
{
    nlohmann::json commands = nlohmann::json::array();
    for (const auto& c : pm::cli::registered_cli_commands())
        commands.push_back({{"id", c.id ? c.id : ""}, {"label", c.label ? c.label : ""}, {"available", c.available}});
    return commands;
}

nlohmann::json registered_app_commands()
{
    nlohmann::json commands = nlohmann::json::array();
    for (const auto& c : pm::cli::registered_app_commands())
        commands.push_back({{"id", c.id ? c.id : ""}, {"label", c.label ? c.label : ""}, {"available", c.available}});
    return commands;
}

nlohmann::json registered_ribbon_commands()
{
    nlohmann::json commands = nlohmann::json::array();
    for (const auto& c : pm::cli::registered_ribbon_commands())
        commands.push_back({{"id", c.id ? c.id : ""}, {"label", c.label ? c.label : ""}, {"available", c.available}});
    return commands;
}

nlohmann::json command_variable_palette()
{
    auto variables = nlohmann::json::array();
    std::set<std::string> variable_names;
    auto push = [&](const char* name, const char* group, const char* description) {
        std::string n = name ? name : "";
        if (n.empty() || !variable_names.insert(n).second)
            return;
        variables.push_back({{"name", std::move(n)}, {"group", group ? group : ""}, {"description", description ? description : ""}});
    };
    auto is_safe_token_part = [](const std::string& value) {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_';
        });
    };
    auto known_folder_group = [](KF_CATEGORY category) -> const char* {
        switch (category) {
        case KF_CATEGORY_PERUSER: return "Known folders - User";
        case KF_CATEGORY_COMMON: return "Known folders - Public";
        case KF_CATEGORY_FIXED: return "Known folders - System";
        case KF_CATEGORY_VIRTUAL: return "Known folders - Virtual";
        default: return "Known folders";
        }
    };

    push("CURRENT_FILE", "Current file", "Current file absolute path from Explorer selection or the open preview.");
    push("CURRENT_FILE_NAME", "Current file", "Current file name including extension.");
    push("CURRENT_PATH", "Current file", "Current Explorer folder, or current file parent folder when a file is selected.");
    push("CURRENT_SELECTION", "Current file", "Whitespace-separated current Explorer selection list; entries are files or folders.");

    push("SRC_FILE", "Source", "Selected source file path.");
    push("SRC_DIR", "Source", "Selected source parent directory.");
    push("SRC_NAME", "Source", "Selected source filename without extension.");
    push("SRC_EXT", "Source", "Selected source extension without the leading dot.");
    push("SRC_FILE_EXT", "Source", "Selected source extension including the leading dot.");

    push("CWD", "Process", "Current command working directory.");
    push("PATH_SEP", "Process", "Native path separator for this platform.");
    push("PATH_LIST_SEP", "Process", "Native delimiter for lists of paths.");

    push("YYYY", "Date / time", "Current four-digit year.");
    push("MM", "Date / time", "Current two-digit month.");
    push("DD", "Date / time", "Current two-digit day of month.");
    push("HH", "Date / time", "Current two-digit hour.");
    push("SS", "Date / time", "Current two-digit seconds.");

    push("KNOWNFOLDER:Home", "Known folders - Portable", "User home/profile folder.");
    push("KNOWNFOLDER:Config", "Known folders - Portable", "User configuration folder.");
    push("KNOWNFOLDER:Data", "Known folders - Portable", "User data folder.");
    push("KNOWNFOLDER:Cache", "Known folders - Portable", "User cache folder.");
    push("KNOWNFOLDER:Temp", "Known folders - Portable", "Temporary files folder.");

    push("KNOWNFOLDER:Profile", "Known folders - User", "User profile folder.");
    push("KNOWNFOLDER:Desktop", "Known folders - User", "Current user's Desktop folder.");
    push("KNOWNFOLDER:Documents", "Known folders - User", "Current user's Documents folder.");
    push("KNOWNFOLDER:Downloads", "Known folders - User", "Current user's Downloads folder.");
    push("KNOWNFOLDER:Pictures", "Known folders - User", "Current user's Pictures folder.");
    push("KNOWNFOLDER:Music", "Known folders - User", "Current user's Music folder.");
    push("KNOWNFOLDER:Videos", "Known folders - User", "Current user's Videos folder.");
    push("KNOWNFOLDER:Templates", "Known folders - User", "Current user's Templates folder.");
    push("KNOWNFOLDER:Favorites", "Known folders - User", "Current user's Favorites folder.");
    push("KNOWNFOLDER:Links", "Known folders - User", "Current user's Links folder.");
    push("KNOWNFOLDER:Saved_Games", "Known folders - User", "Current user's Saved Games folder.");
    push("KNOWNFOLDER:Screenshots", "Known folders - User", "Current user's Screenshots folder.");

    push("KNOWNFOLDER:Local_App_Data", "Known folders - App data", "Current user's local application data folder.");
    push("KNOWNFOLDER:Roaming_App_Data", "Known folders - App data", "Current user's roaming application data folder.");
    push("KNOWNFOLDER:Program_Data", "Known folders - App data", "Shared program data folder.");

    push("KNOWNFOLDER:Public", "Known folders - Public", "Public user profile folder.");
    push("KNOWNFOLDER:Public_Desktop", "Known folders - Public", "Public Desktop folder.");
    push("KNOWNFOLDER:Public_Documents", "Known folders - Public", "Public Documents folder.");
    push("KNOWNFOLDER:Public_Downloads", "Known folders - Public", "Public Downloads folder.");
    push("KNOWNFOLDER:Public_Pictures", "Known folders - Public", "Public Pictures folder.");
    push("KNOWNFOLDER:Public_Music", "Known folders - Public", "Public Music folder.");
    push("KNOWNFOLDER:Public_Videos", "Known folders - Public", "Public Videos folder.");

    push("KNOWNFOLDER:Windows", "Known folders - System", "Windows installation folder.");
    push("KNOWNFOLDER:System", "Known folders - System", "Windows System folder.");
    push("KNOWNFOLDER:Program_Files", "Known folders - System", "Program Files folder.");
    push("KNOWNFOLDER:Program_Files_X86", "Known folders - System", "Program Files (x86) folder.");
    push("KNOWNFOLDER:Fonts", "Known folders - System", "Fonts folder.");
    push("KNOWNFOLDER:Start_Menu", "Known folders - System", "Current user's Start Menu folder.");
    push("KNOWNFOLDER:Programs", "Known folders - System", "Current user's Start Menu Programs folder.");
    push("KNOWNFOLDER:Startup", "Known folders - System", "Current user's Startup folder.");

    HRESULT init = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(init);
    if (init == RPC_E_CHANGED_MODE)
        init = S_OK;
    if (SUCCEEDED(init)) {
        IKnownFolderManager* manager = nullptr;
        if (SUCCEEDED(::CoCreateInstance(CLSID_KnownFolderManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&manager))) && manager) {
            KNOWNFOLDERID* ids = nullptr;
            UINT count = 0;
            if (SUCCEEDED(manager->GetFolderIds(&ids, &count)) && ids) {
                for (UINT i = 0; i < count; ++i) {
                    IKnownFolder* folder = nullptr;
                    if (FAILED(manager->GetFolder(ids[i], &folder)) || !folder)
                        continue;
                    KNOWNFOLDER_DEFINITION def{};
                    if (SUCCEEDED(folder->GetFolderDefinition(&def))) {
                        const std::string canonical = def.pszName ? pmui::wide_to_utf8(def.pszName) : std::string{};
                        PWSTR path = nullptr;
                        const bool hasPath = SUCCEEDED(folder->GetPath(KF_FLAG_DEFAULT, &path)) && path;
                        if (hasPath)
                            ::CoTaskMemFree(path);
                        if (hasPath && is_safe_token_part(canonical)) {
                            const std::string display = def.pszDescription ? pmui::wide_to_utf8(def.pszDescription) : canonical;
                            const std::string token = std::string("KNOWNFOLDER:") + canonical;
                            const std::string description = display.empty()
                                ? std::string("Windows known folder: ") + canonical
                                : std::string("Windows known folder: ") + display;
                            push(token.c_str(), known_folder_group(def.category), description.c_str());
                        }
                        FreeKnownFolderDefinitionFields(&def);
                    }
                    folder->Release();
                }
                ::CoTaskMemFree(ids);
            }
            manager->Release();
        }
    }
    if (uninit)
        ::CoUninitialize();

    std::set<std::string> env_names;
    if (LPWCH block = ::GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; p && *p; p += std::wcslen(p) + 1) {
            std::wstring entry(p);
            const auto eq = entry.find(L'=');
            if (eq == std::wstring::npos || eq == 0)
                continue;
            env_names.insert(pmui::wide_to_utf8(entry.substr(0, eq)));
        }
        ::FreeEnvironmentStringsW(block);
    }

    for (const auto& name : env_names) {
        variables.push_back({
            {"name", std::string("ENV:") + name},
            {"group", "Environment"},
            {"description", std::string("Environment variable: ") + name},
        });
    }
    return variables;
}

nlohmann::json cli_option_to_json(const CLI::Option* opt)
{
    nlohmann::json names = nlohmann::json::array();
    for (const auto& n : opt->get_snames())
        names.push_back("-" + n);
    for (const auto& n : opt->get_lnames())
        names.push_back("--" + n);

    return nlohmann::json{
        {"name", opt->get_name(opt->get_positional(), true)},
        {"names", std::move(names)},
        {"positional", opt->get_positional()},
        {"required", opt->get_required()},
        {"group", opt->get_group()},
        {"description", opt->get_description()},
        {"typeName", opt->get_type_name()},
        {"default", opt->get_default_str()},
        {"expectedMin", opt->get_expected_min()},
        {"expectedMax", opt->get_expected_max()},
        {"allowExtraArgs", opt->get_allow_extra_args()},
    };
}

CLI::App* find_cli_subcommand_path(CLI::App& app, const std::string& command_path)
{
    CLI::App* cur = &app;
    CLI::App* matched = nullptr;
    std::istringstream in(command_path);
    std::string token;
    while (in >> token) {
        CLI::App* next = cur->get_subcommand_no_throw(token);
        if (!next)
            return nullptr;
        cur = next;
        matched = cur;
    }
    return matched;
}

nlohmann::json cli_command_help_schema(const std::string& command)
{
    static CLI::App app{"PolyMech Image CLI"};
    static PmImageCliState state;
    static const bool registered = [] {
        pm_image_register_cli(app, state);
        return true;
    }();
    (void)registered;

    CLI::App* sub = find_cli_subcommand_path(app, command);
    if (!sub)
        return nlohmann::json::object();

    nlohmann::json options = nlohmann::json::array();
    for (const auto* opt : sub->get_options())
        options.push_back(cli_option_to_json(opt));

    nlohmann::json subcommands = nlohmann::json::array();
    for (const auto* child : sub->get_subcommands({})) {
        if (!child || child->get_name().empty())
            continue;
        subcommands.push_back({
            {"name", child->get_name()},
            {"description", child->get_description()},
        });
    }

    return nlohmann::json{
        {"name", sub->get_name()},
        {"path", command},
        {"description", sub->get_description()},
        {"allowExtras", sub->get_allow_extras()},
        {"options", std::move(options)},
        {"subcommands", std::move(subcommands)},
    };
}
#endif

void settings_rpc_reply(CWebViewManager& webViews, const std::string& rpc_id, nlohmann::json out)
{
    out["kind"] = "hostProviderRpc";
    out["id"]   = rpc_id;
    webViews.PostToFromAnyThread("settings", out.dump());
}

void settings_rpc_async(CWebViewManager& webViews,
                        const std::string& rpc_id,
                        std::function<nlohmann::json()> work)
{
    std::thread([webViews = &webViews, rpc_id, work = std::move(work)]() mutable {
        nlohmann::json out;
        try {
            out = work();
        } catch (const std::exception& e) {
            out["ok"]    = false;
            out["error"] = e.what();
        } catch (...) {
            out["ok"]    = false;
            out["error"] = "settings providerRpc failed";
        }
        settings_rpc_reply(*webViews, rpc_id, std::move(out));
    }).detach();
}

} // namespace

namespace pmui {

void CSettingsWebView::Show(CWebViewManager& webViews, HWND hostHwnd,
                            std::function<void()> onAppearanceChanged)
{
    const std::string settings_html = pmui::load_settings_web_html();
    if (settings_html.empty()) {
        ::MessageBoxW(hostHwnd,
            L"settings.html is missing or empty (expected dist\\shared\\settings.html).\n\nRun npm run build:settings:embed.",
            pm::brand::k_app_id_w,
            MB_ICONWARNING | MB_OK);
        return;
    }

    RECT wr{100, 100, 1060, 780};
    if (hostHwnd && ::IsWindow(hostHwnd))
        (void)::GetWindowRect(hostHwnd, &wr);
    const auto S = [&](int v) {
        UINT dpi = 96;
        if (hostHwnd && ::IsWindow(hostHwnd))
            dpi = ::GetDpiForWindow(hostHwnd);
        return ::MulDiv(v, static_cast<int>(dpi ? dpi : 96), 96);
    };
    const int pw = S(1120);
    const int ph = S(720);
    const int px = wr.left + (wr.right - wr.left - pw) / 2;
    const int py = wr.top + (wr.bottom - wr.top - ph) / 2;

    CWebViewOptions opts = CWebViewOptions::ForPurePopup(/*devTools=*/true);
    opts.html = settings_html;
    opts.frame = true;
    opts.clickthrough = false;
    opts.modal = true;
    opts.keepDwmShadow = true;
    opts.allowWebResizeHostWindow = false;
    opts.autoResizeHostWindowFromWebContent = false;
    opts.onMessage = [&webViews, hostHwnd, onAppearanceChanged = std::move(onAppearanceChanged)](const std::string& json) {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(json);
        } catch (...) {
            return;
        }
        const std::string kind = j.value("kind", std::string{});
        if (kind != "providerRpc")
            return;

        const std::string method = j.value("method", std::string{});
        const std::string rpcId = j.value("rpcId", std::string{});
        nlohmann::json out;
        out["kind"] = "hostProviderRpc";
        out["id"] = rpcId;

        if (method == "settingsProvidersGet") {
            settings_rpc_async(webViews, rpcId, []() -> nlohmann::json {
                nlohmann::json reply;
                std::string err;
                media::settings::ProviderMap providers;
                if (!media::settings::load_providers(providers, err)) {
                    reply["ok"] = false;
                    reply["error"] = err.empty() ? "load_providers failed" : err;
                    return reply;
                }
                int n = 0;
                const auto* defs = media::settings::known_providers(&n);
                nlohmann::json rows = nlohmann::json::array();
                for (int i = 0; i < n; ++i) {
                    const auto& def = defs[i];
                    media::settings::ProviderEntry row{};
                    row.base_url = def.base_url ? std::string(def.base_url) : std::string{};
                    if (const auto it = providers.find(def.name); it != providers.end())
                        row = it->second;
                    nlohmann::json models = nlohmann::json::array();
                    for (const auto* m : def.models)
                        models.push_back(m ? std::string(m) : std::string{});
                    rows.push_back(nlohmann::json{
                        {"id", def.name ? std::string(def.name) : std::string{}},
                        {"displayName", def.display_name ? std::string(def.display_name) : std::string{}},
                        {"apiKey", row.api_key},
                        {"baseUrl", row.base_url},
                        {"models", std::move(models)},
                    });
                }
                media::settings::AppearanceSettings app{};
                std::string app_err;
                media::settings::load_appearance(app, app_err);
                reply["ok"] = true;
                reply["data"] = nlohmann::json{
                    {"providers", std::move(rows)},
                    {"settingsPath", media::settings::get_settings_json_path().string()},
                    {"theme", pmui::theme_palette().dark ? "dark" : "light"},
                    {"displayLanguage", app.display_language},
                };
                return reply;
            });
            return;
        }

        if (method == "settingsProvidersSave") {
            std::string err;
            media::settings::ProviderMap providers;
            if (!media::settings::load_providers(providers, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "load_providers failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            if (!j.contains("providers") || !j["providers"].is_array()) {
                out["ok"] = false;
                out["error"] = "settingsProvidersSave: missing providers array";
                webViews.PostTo("settings", out.dump());
                return;
            }
            for (const auto& item : j["providers"]) {
                if (!item.is_object())
                    continue;
                const std::string id = item.value("id", std::string{});
                if (id.empty())
                    continue;
                auto& row = providers[id];
                if (item.contains("apiKey") && item["apiKey"].is_string())
                    row.api_key = item["apiKey"].get<std::string>();
                if (item.contains("baseUrl") && item["baseUrl"].is_string())
                    row.base_url = item["baseUrl"].get<std::string>();
            }
            if (!media::settings::save_providers(providers, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "save_providers failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsChatGet") {
            std::string err;
            media::settings::ChatProviderSettings chat{};
            media::settings::load_chat_provider(chat, err);
            media::settings::AppearanceSettings app{};
            std::string app_err;
            media::settings::load_appearance(app, app_err);
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"displayLanguage", app.display_language},
                {"chat",
                    {
                        {"router", chat.router},
                        {"model", chat.model},
                        {"max_iterations", chat.max_iterations},
                        {"image_provider", chat.image_provider},
                        {"image_model", chat.image_model},
                        {"image_recognition_provider", chat.image_recognition_provider},
                        {"image_recognition_model", chat.image_recognition_model},
                        {"video_provider", chat.video_provider},
                        {"video_model", chat.video_model},
                        {"stt_provider", chat.stt_provider},
                        {"stt_model", chat.stt_model},
                        {"tts_provider", chat.tts_provider},
                        {"tts_model", chat.tts_model},
                        {"tts_voice_id", chat.tts_voice_id},
                    }},
            };
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsChatSave") {
            std::string err;
            media::settings::ChatProviderSettings chat{};
            media::settings::load_chat_provider(chat, err);
            if (j.contains("router") && j["router"].is_string())
                chat.router = j["router"].get<std::string>();
            if (j.contains("model") && j["model"].is_string())
                chat.model = j["model"].get<std::string>();
            if (j.contains("max_iterations") && j["max_iterations"].is_number_integer())
                chat.max_iterations = (std::clamp)(j["max_iterations"].get<int>(), 1, 99);
            if (j.contains("image_provider") && j["image_provider"].is_string())
                chat.image_provider = j["image_provider"].get<std::string>();
            if (j.contains("image_model") && j["image_model"].is_string())
                chat.image_model = j["image_model"].get<std::string>();
            if (j.contains("image_recognition_provider") && j["image_recognition_provider"].is_string())
                chat.image_recognition_provider = j["image_recognition_provider"].get<std::string>();
            if (j.contains("image_recognition_model") && j["image_recognition_model"].is_string())
                chat.image_recognition_model = j["image_recognition_model"].get<std::string>();
            if (j.contains("video_provider") && j["video_provider"].is_string())
                chat.video_provider = j["video_provider"].get<std::string>();
            if (j.contains("video_model") && j["video_model"].is_string())
                chat.video_model = j["video_model"].get<std::string>();
            if (j.contains("stt_provider") && j["stt_provider"].is_string())
                chat.stt_provider = j["stt_provider"].get<std::string>();
            if (j.contains("stt_model") && j["stt_model"].is_string())
                chat.stt_model = j["stt_model"].get<std::string>();
            if (j.contains("tts_provider") && j["tts_provider"].is_string())
                chat.tts_provider = j["tts_provider"].get<std::string>();
            if (j.contains("tts_model") && j["tts_model"].is_string())
                chat.tts_model = j["tts_model"].get<std::string>();
            if (j.contains("tts_voice_id") && j["tts_voice_id"].is_string())
                chat.tts_voice_id = j["tts_voice_id"].get<std::string>();
            if (!media::settings::save_chat_provider(chat, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "save_chat_provider failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsGeneralGet") {
            std::string err;
            media::settings::AppearanceSettings app{};
            media::settings::load_appearance(app, err);
            media::settings::WindowLayout wl{};
            std::string wlerr;
            media::settings::load_window_layout(wl, wlerr);
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"general",
                    {
                        {"display_language", app.display_language},
                        {"theme", static_cast<int>(app.theme)},
                        {"font_size_extra_pt", app.font_size_extra_pt},
                        {"filetree_show_shell_frames", wl.filetree_show_shell_frames},
                    }},
            };
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsGeneralSave") {
            std::string err;
            media::settings::AppearanceSettings app{};
            media::settings::load_appearance(app, err);
            if (j.contains("display_language") && j["display_language"].is_string())
                app.display_language = j["display_language"].get<std::string>();
            if (j.contains("theme") && j["theme"].is_number_integer())
                app.theme = static_cast<media::settings::Theme>((std::clamp)(j["theme"].get<int>(), 0, 2));
            if (j.contains("font_size_extra_pt") && j["font_size_extra_pt"].is_number_integer())
                app.font_size_extra_pt = (std::clamp)(j["font_size_extra_pt"].get<int>(), 0, 4);
            if (!media::settings::save_appearance(app, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "save_appearance failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            if (j.contains("filetree_show_shell_frames") && j["filetree_show_shell_frames"].is_boolean()) {
                const bool show = j["filetree_show_shell_frames"].get<bool>();
                if (!media::settings::set_filetree_show_shell_frames(show, err)) {
                    out["ok"] = false;
                    out["error"] = err.empty() ? "set_filetree_show_shell_frames failed" : err;
                    webViews.PostTo("settings", out.dump());
                    return;
                }
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
            webViews.PostTo("settings", out.dump());
            if (onAppearanceChanged)
                onAppearanceChanged();
            return;
        }

        if (method == "settingsGeneralExport") {
            const bool encrypted = j.value("encrypted", true);
            std::wstring path;
            if (!pick_save_export_path(hostHwnd, encrypted, path)) {
                out["ok"] = true;
                out["data"] = nlohmann::json{{"cancelled", true}};
                webViews.PostTo("settings", out.dump());
                return;
            }
            std::string err;
            if (!media::settings::export_settings_file(std::filesystem::path(path), encrypted, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "export_settings_file failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json{{"cancelled", false}};
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsGeneralImport") {
            std::wstring path;
            if (!pick_open_import_path(hostHwnd, path)) {
                out["ok"] = true;
                out["data"] = nlohmann::json{{"cancelled", true}};
                webViews.PostTo("settings", out.dump());
                return;
            }
            std::string err;
            if (!media::settings::import_settings_file(std::filesystem::path(path), err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "import_settings_file failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json{{"cancelled", false}};
            webViews.PostTo("settings", out.dump());
            if (onAppearanceChanged)
                onAppearanceChanged();
            return;
        }

        if (method == "settingsGeneralExportArchive") {
            std::wstring path;
            if (!pick_save_archive_path(hostHwnd, path)) {
                out["ok"] = true;
                out["data"] = nlohmann::json{{"cancelled", true}};
                webViews.PostTo("settings", out.dump());
                return;
            }
            std::string err;
            if (!media::settings_archive::export_settings_archive_zip(std::filesystem::path(path), err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "export_settings_archive_zip failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json{{"cancelled", false}};
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsGeneralImportArchive") {
            std::wstring path;
            if (!pick_open_archive_path(hostHwnd, path)) {
                out["ok"] = true;
                out["data"] = nlohmann::json{{"cancelled", true}};
                webViews.PostTo("settings", out.dump());
                return;
            }
            std::string err;
            if (!media::settings_archive::import_settings_archive_zip(std::filesystem::path(path), err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "import_settings_archive_zip failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json{{"cancelled", false}};
            webViews.PostTo("settings", out.dump());
            if (onAppearanceChanged)
                onAppearanceChanged();
            return;
        }

        if (method == "settingsGeneralClipboardCopy") {
            std::string raw, err;
            if (!media::settings::load_settings_utf8(raw, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "load_settings_utf8 failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            if (!copy_utf8_to_clipboard(hostHwnd, raw)) {
                out["ok"] = false;
                out["error"] = "Could not copy settings to clipboard";
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsGeneralClipboardPaste") {
            std::string raw, err;
            if (!read_utf8_from_clipboard(hostHwnd, raw, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "Could not read clipboard text" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            if (!media::settings::save_settings_utf8(raw, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "save_settings_utf8 failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
            webViews.PostTo("settings", out.dump());
            if (onAppearanceChanged)
                onAppearanceChanged();
            return;
        }

        if (method == "settingsNativePathPick") {
            const std::string pick_kind = j.value("pickKind", std::string{"file"});
            std::wstring path;
            const bool picked = (pick_kind == "folder")
                ? CWebViewManager::PickNativeFolder(hostHwnd, path)
                : CWebViewManager::PickNativeFile(hostHwnd, path);
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"cancelled", !picked},
                {"path", picked ? pmui::wide_to_utf8(path) : std::string{}},
            };
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsProviderModelsGet") {
            const std::string provider_id = j.value("providerId", std::string{});
            const bool force_refresh = j.value("forceRefresh", false);
            const std::string collection_slug = j.value("collectionSlug", std::string{"official"});
            if (provider_id.empty()) {
                out["ok"] = false;
                out["error"] = "settingsProviderModelsGet: missing providerId";
                webViews.PostTo("settings", out.dump());
                return;
            }
            settings_rpc_async(webViews, rpcId, [provider_id, force_refresh, collection_slug]() -> nlohmann::json {
                nlohmann::json reply;
                std::string err;
                media::settings::ProviderMap providers;
                (void)media::settings::load_providers(providers, err);
                std::string api_key;
                std::string base_url;
                if (const auto it = providers.find(provider_id); it != providers.end()) {
                    api_key = it->second.api_key;
                    base_url = it->second.base_url;
                }
                if (provider_id == "replicate") {
                    std::vector<pmui::provider_dlg_replicate::ModelInfo> models;
                    if (!pmui::provider_dlg_replicate::fetch_collection_models(
                            api_key, base_url, collection_slug.empty() ? "official" : collection_slug, models, err, force_refresh)) {
                        reply["ok"] = false;
                        reply["error"] = err.empty() ? "replicate model fetch failed" : err;
                        return reply;
                    }
                    pmui::provider_dlg_replicate::sort_models_alpha(models);
                    nlohmann::json rows = nlohmann::json::array();
                    for (const auto& m : models)
                        rows.push_back(
                            nlohmann::json{{"id", m.slug}, {"label", m.slug}, {"description", m.description}, {"url", m.url}});
                    reply["ok"] = true;
                    reply["data"] = nlohmann::json{{"providerId", provider_id}, {"models", std::move(rows)}};
                    return reply;
                }
                std::vector<pmui::provider_models::ModelOption> models;
                if (!pmui::provider_models::list_models_for_provider(provider_id, api_key, base_url, models, err)) {
                    reply["ok"] = false;
                    reply["error"] = err.empty() ? "provider model fetch failed" : err;
                    return reply;
                }
                nlohmann::json rows = nlohmann::json::array();
                for (const auto& m : models)
                    rows.push_back(nlohmann::json{{"id", m.id}, {"label", m.label.empty() ? m.id : m.label}});
                reply["ok"] = true;
                reply["data"] = nlohmann::json{{"providerId", provider_id}, {"models", std::move(rows)}};
                return reply;
            });
            return;
        }

        if (method == "settingsReplicateCollectionsGet") {
            const bool force_refresh = j.value("forceRefresh", false);
            settings_rpc_async(webViews, rpcId, [force_refresh]() -> nlohmann::json {
                nlohmann::json reply;
                std::string err;
                media::settings::ProviderMap providers;
                (void)media::settings::load_providers(providers, err);
                std::string api_key;
                std::string base_url;
                if (const auto it = providers.find("replicate"); it != providers.end()) {
                    api_key = it->second.api_key;
                    base_url = it->second.base_url;
                }
                std::vector<pmui::provider_dlg_replicate::CollectionInfo> cols;
                if (!pmui::provider_dlg_replicate::fetch_collections(api_key, base_url, cols, err, force_refresh)) {
                    reply["ok"] = false;
                    reply["error"] = err.empty() ? "replicate collections fetch failed" : err;
                    return reply;
                }
                pmui::provider_dlg_replicate::sort_collections_alpha(cols);
                nlohmann::json rows = nlohmann::json::array();
                for (const auto& c : cols)
                    rows.push_back(nlohmann::json{{"slug", c.slug}, {"name", c.name}, {"description", c.description}});
                reply["ok"] = true;
                reply["data"] = nlohmann::json{{"collections", std::move(rows)}};
                return reply;
            });
            return;
        }

        if (method == "settingsReplicateResolveCollection") {
            const std::string model_slug = j.value("modelSlug", std::string{});
            if (model_slug.empty()) {
                out["ok"] = true;
                out["data"] = nlohmann::json{{"collection", nullptr}};
                webViews.PostTo("settings", out.dump());
                return;
            }
            std::string out_slug;
            if (!pmui::provider_dlg_replicate::resolve_collection_for_model_cached(model_slug, out_slug) || out_slug.empty()) {
                out["ok"] = true;
                out["data"] = nlohmann::json{{"collection", nullptr}};
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json{{"collection", out_slug}};
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsMcpGet") {
#if FEATURE_HOME_LLM_TOOLS && FEATURE_MCP_CLIENT
            const std::filesystem::path mcp_path = media::settings::get_config_dir() / "mcp.json";
            nlohmann::json root = nlohmann::json::object();
            if (std::ifstream in(mcp_path, std::ios::binary); in) {
                try {
                    in >> root;
                } catch (...) {
                    root = nlohmann::json::object();
                }
            }
            nlohmann::json servers = nlohmann::json::object();
            if (root.is_object() && root.contains("mcpServers") && root["mcpServers"].is_object())
                servers = root["mcpServers"];
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"mcpJsonPath", mcp_path.string()},
                {"mcpServers", std::move(servers)},
            };
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"disabled", true},
                {"note", "MCP client settings are disabled in this build."},
                {"mcpJsonPath", ""},
                {"mcpServers", nlohmann::json::object()},
            };
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsMcpSave") {
#if FEATURE_HOME_LLM_TOOLS && FEATURE_MCP_CLIENT
            if (!j.contains("mcpServers") || !j["mcpServers"].is_object()) {
                out["ok"] = false;
                out["error"] = "settingsMcpSave: missing mcpServers object";
                webViews.PostTo("settings", out.dump());
                return;
            }
            const std::filesystem::path mcp_path = media::settings::get_config_dir() / "mcp.json";
            nlohmann::json root = nlohmann::json::object();
            if (std::ifstream in(mcp_path, std::ios::binary); in) {
                try {
                    in >> root;
                } catch (...) {
                    root = nlohmann::json::object();
                }
            }
            if (!root.is_object())
                root = nlohmann::json::object();
            root["mcpServers"] = j["mcpServers"];
            std::ofstream out_file(mcp_path, std::ios::binary | std::ios::trunc);
            if (!out_file) {
                out["ok"] = false;
                out["error"] = "Could not open mcp.json for writing";
                webViews.PostTo("settings", out.dump());
                return;
            }
            out_file << root.dump(2) << "\n";
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
#else
            out["ok"] = false;
            out["error"] = "MCP client settings are disabled in this build.";
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsMcpPing") {
#if FEATURE_HOME_LLM_TOOLS && FEATURE_MCP_CLIENT
            const nlohmann::json probe = media::llm::mcp::probe_mcp_config({});
#else
            const nlohmann::json probe = nlohmann::json{
                {"skipped", true},
                {"disabled", true},
                {"note", "MCP client settings are disabled in this build."},
                {"servers", nlohmann::json::array()},
            };
#endif
            out["ok"] = true;
            out["data"] = nlohmann::json{{"probe", probe}};
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsToolsGet") {
#if FEATURE_HOME_LLM_TOOLS
            std::string err;
            media::runtime_settings::GlobalToolSettings gs;
            if (!media::runtime_settings::load_global_tool_settings(gs, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "settingsToolsGet failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"disabled_path_tools", gs.disabled_path_tools},
                {"mcp_tools_enabled", gs.mcp_tools_enabled},
                {"disabled_mcp_servers", gs.disabled_mcp_servers},
            };
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"disabled", true},
                {"note", "Home LLM tools are disabled in this build."},
                {"disabled_path_tools", nlohmann::json::array()},
                {"mcp_tools_enabled", false},
                {"disabled_mcp_servers", nlohmann::json::array()},
            };
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsToolsSave") {
#if FEATURE_HOME_LLM_TOOLS
            media::runtime_settings::GlobalToolSettings gs{};
            if (j.contains("disabled_path_tools") && j["disabled_path_tools"].is_array()) {
                for (const auto& el : j["disabled_path_tools"])
                    if (el.is_string())
                        gs.disabled_path_tools.push_back(el.get<std::string>());
            }
            gs.mcp_tools_enabled = j.value("mcp_tools_enabled", true);
            if (j.contains("disabled_mcp_servers") && j["disabled_mcp_servers"].is_array()) {
                for (const auto& el : j["disabled_mcp_servers"])
                    if (el.is_string())
                        gs.disabled_mcp_servers.push_back(el.get<std::string>());
            }
            std::string err;
            if (!media::runtime_settings::save_global_tool_settings(gs, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "settingsToolsSave failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
#else
            out["ok"] = false;
            out["error"] = "Home LLM tools are disabled in this build.";
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsSkillsGet") {
#if FEATURE_HOME_LLM_SKILLS
            std::string err;
            media::runtime_settings::AgentSkillsSettings cfg{};
            if (!media::runtime_settings::load_agent_skills_settings(cfg, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "settingsSkillsGet failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            media::llm::skills::SkillPolicy policy;
            policy.enabled = cfg.enabled;
            policy.roaming_enabled = cfg.roaming_enabled;
            policy.workspace_enabled = cfg.workspace_enabled;
            policy.pinned = cfg.pinned;
            policy.disabled = cfg.disabled;

            const auto snap = media::llm::skills::discover_skills(policy, std::filesystem::current_path().string());
            nlohmann::json skills = nlohmann::json::array();
            for (const auto& e : snap.entries) {
                skills.push_back({
                    {"name", e.name},
                    {"description", e.description},
                    {"source", e.source == media::llm::skills::SkillSource::Roaming ? "roaming" : "workspace"},
                    {"available", e.available},
                    {"active", e.active},
                    {"always", e.always},
                    {"pinned", e.pinned},
                    {"disabled", e.disabled},
                    {"missing_requirements", e.missing_requirements},
                    {"path", e.skill_md_path.string()},
                });
            }

            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"enabled", cfg.enabled},
                {"roaming_enabled", cfg.roaming_enabled},
                {"workspace_enabled", cfg.workspace_enabled},
                {"pinned", cfg.pinned},
                {"disabled", cfg.disabled},
                {"roots", nlohmann::json{
                    {"roaming", snap.roaming_root.string()},
                    {"workspace", snap.workspace_root.string()},
                }},
                {"skills", skills},
            };
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"feature_disabled", true},
                {"note", "Home LLM skills are disabled in this build."},
                {"enabled", false},
                {"roaming_enabled", false},
                {"workspace_enabled", false},
                {"pinned", nlohmann::json::array()},
                {"disabled", nlohmann::json::array()},
                {"roots", nlohmann::json{{"roaming", ""}, {"workspace", ""}}},
                {"skills", nlohmann::json::array()},
            };
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsSkillsSave") {
#if FEATURE_HOME_LLM_SKILLS
            media::runtime_settings::AgentSkillsSettings cfg{};
            cfg.enabled = j.value("enabled", true);
            cfg.roaming_enabled = j.value("roaming_enabled", true);
            cfg.workspace_enabled = j.value("workspace_enabled", true);
            if (j.contains("pinned") && j["pinned"].is_array()) {
                for (const auto& el : j["pinned"])
                    if (el.is_string())
                        cfg.pinned.push_back(el.get<std::string>());
            }
            if (j.contains("disabled") && j["disabled"].is_array()) {
                for (const auto& el : j["disabled"])
                    if (el.is_string())
                        cfg.disabled.push_back(el.get<std::string>());
            }
            std::string err;
            if (!media::runtime_settings::save_agent_skills_settings(cfg, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "settingsSkillsSave failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
#else
            out["ok"] = false;
            out["error"] = "Home LLM skills are disabled in this build.";
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsPixlwizGet") {
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
            out["ok"] = true;
            out["data"] = pmui::pixlwiz_auth::read_settings_status_payload();
            webViews.PostTo("settings", out.dump());
            return;
#else
            out["ok"] = false;
            out["error"] = "Pixlwiz auth feature disabled";
            webViews.PostTo("settings", out.dump());
            return;
#endif
        }

        if (method == "settingsPixlwizLogin") {
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
            pmui::StartPixlwizLoginAsync(hostHwnd);
            out["ok"] = true;
            out["data"] = nlohmann::json{{"started", true}};
            webViews.PostTo("settings", out.dump());
            return;
#else
            out["ok"] = false;
            out["error"] = "Pixlwiz auth feature disabled";
            webViews.PostTo("settings", out.dump());
            return;
#endif
        }

        if (method == "settingsPixlwizLogout") {
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
            std::string err;
            if (!pm_zitadel_oauth_clear(err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "logout failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
            webViews.PostTo("settings", out.dump());
            return;
#else
            out["ok"] = false;
            out["error"] = "Pixlwiz auth feature disabled";
            webViews.PostTo("settings", out.dump());
            return;
#endif
        }

        if (method == "settingsProviderOAuthGet") {
            const std::string provider_id = j.value("providerId", std::string{});
            bool has_token = false;
            std::string err;
            const bool ok = media::provider_oauth::has_access_token(provider_id, has_token, err);
            std::string info;
            std::string last_error;
            media::provider_oauth::read_last_login_message(provider_id, info, last_error);
            out["ok"] = ok;
            if (!ok)
                out["error"] = err.empty() ? "oauth status failed" : err;
            out["data"] = nlohmann::json{
                {"providerId", provider_id},
                {"hasToken", has_token},
                {"loginInProgress", media::provider_oauth::login_in_progress(provider_id)},
                {"info", info},
                {"lastError", last_error},
            };
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsProviderOAuthLogin") {
            const std::string provider_id = j.value("providerId", std::string{});
            std::string info;
            std::string err;
            if (!media::provider_oauth::start_login_async(provider_id, info, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "oauth login failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json{{"message", info}};
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsProviderOAuthLogout") {
            const std::string provider_id = j.value("providerId", std::string{});
            std::string err;
            if (!media::provider_oauth::clear_access_token(provider_id, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "oauth logout failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsRouterModelsGet") {
            const std::string router_id = j.value("routerId", std::string{});
            const bool force_refresh = j.value("forceRefresh", false);
            if (router_id.empty()) {
                out["ok"] = false;
                out["error"] = "settingsRouterModelsGet: missing routerId";
                webViews.PostTo("settings", out.dump());
                return;
            }
            settings_rpc_async(webViews, rpcId, [router_id, force_refresh]() -> nlohmann::json {
                nlohmann::json reply;
                std::string err;
                media::settings::ProviderMap providers;
                (void)media::settings::load_providers(providers, err);
                auto get_provider_creds = [&](const std::string& id, std::string& api_key, std::string& base_url) {
                    if (const auto it = providers.find(id); it != providers.end()) {
                        api_key = it->second.api_key;
                        base_url = it->second.base_url;
                    }
                };
                nlohmann::json rows = nlohmann::json::array();
                if (router_id == "openrouter") {
                    std::string api_key, base_url;
                    get_provider_creds("openrouter", api_key, base_url);
                    std::vector<media::openrouter_cli::OpenRouterCatalogModelRow> models;
                    if (!media::openrouter_cli::list_openrouter_catalog_models(api_key, base_url, models, err, force_refresh)) {
                        reply["ok"] = false;
                        reply["error"] = err.empty() ? "openrouter model fetch failed" : err;
                        return reply;
                    }
                    for (const auto& m : models)
                        rows.push_back(nlohmann::json{
                            {"id", m.id},
                            {"label", m.name.empty() ? m.id : m.name},
                            {"description", m.description},
                            {"url", m.open_url},
                        });
                } else if (router_id == "pixlwiz") {
                    std::string api_key, base_url;
                    get_provider_creds("pixlwiz", api_key, base_url);
                    std::vector<media::pixlwiz_cli::PixlWizModelRow> models;
                    if (!media::pixlwiz_cli::list_pixlwiz_catalog_models(api_key, base_url, models, err, force_refresh)) {
                        reply["ok"] = false;
                        reply["error"] = err.empty() ? "pixlwiz model fetch failed" : err;
                        return reply;
                    }
                    for (const auto& m : models)
                        rows.push_back(nlohmann::json{{"id", m.id}, {"label", m.name.empty() ? m.id : m.name}});
                } else if (router_id == "openai") {
                    std::string api_key, base_url;
                    get_provider_creds("openai", api_key, base_url);
                    std::vector<media::openai_cli::OpenAIModelRow> models;
                    if (!media::openai_cli::list_openai_models(api_key, base_url, models, err, force_refresh)) {
                        reply["ok"] = false;
                        reply["error"] = err.empty() ? "openai model fetch failed" : err;
                        return reply;
                    }
                    for (const auto& m : models)
                        rows.push_back(nlohmann::json{{"id", m.id}, {"label", m.id}});
                } else {
                    int n = 0;
                    const auto* defs = media::settings::known_providers(&n);
                    for (int i = 0; i < n; ++i) {
                        const auto& d = defs[i];
                        if (d.name && router_id == d.name && !d.models.empty()) {
                            for (const auto* m : d.models)
                                rows.push_back(nlohmann::json{{"id", m ? std::string(m) : std::string{}}, {"label", m ? std::string(m) : std::string{}}});
                            break;
                        }
                    }
                }
                reply["ok"] = true;
                reply["data"] = nlohmann::json{{"routerId", router_id}, {"models", std::move(rows)}};
                return reply;
            });
            return;
        }

        if (method == "settingsChatPresetsGet") {
            std::string err;
            nlohmann::json custom = nlohmann::json::object();
            (void)media::settings::load_custom_data(custom, err);
            nlohmann::json presets = nlohmann::json::array();
            if (custom.is_object() && custom.contains("settings_chat_presets") && custom["settings_chat_presets"].is_array())
                presets = custom["settings_chat_presets"];
            out["ok"] = true;
            out["data"] = nlohmann::json{{"presets", presets}};
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsChatPresetsSave") {
            if (!j.contains("presets") || !j["presets"].is_array()) {
                out["ok"] = false;
                out["error"] = "settingsChatPresetsSave: missing presets array";
                webViews.PostTo("settings", out.dump());
                return;
            }
            std::string err;
            nlohmann::json custom = nlohmann::json::object();
            (void)media::settings::load_custom_data(custom, err);
            if (!custom.is_object())
                custom = nlohmann::json::object();
            custom["settings_chat_presets"] = j["presets"];
            if (!media::settings::save_custom_data(custom, err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "save_custom_data failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = nlohmann::json::object();
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsCustomCommandsGet") {
#if FEATURE_CUSTOM_COMMANDS
            try {
                nlohmann::json data = pmui::custom_commands_host::commands_get_payload();
                data["variables"] = command_variable_palette();
                out["ok"] = true;
                out["data"] = std::move(data);
            } catch (const std::exception& e) {
                out["ok"] = false;
                out["error"] = e.what();
            }
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"feature_disabled", true},
                {"commandsPath", ""},
                {"document", nlohmann::json{{"version", 1}, {"ribbon", {{"groups", nlohmann::json::array()}}}}},
                {"variables", nlohmann::json::array()},
            };
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsCustomCommandsSave") {
#if FEATURE_CUSTOM_COMMANDS
            if (!j.contains("document") || !j["document"].is_object()) {
                out["ok"] = false;
                out["error"] = "settingsCustomCommandsSave: missing document object";
                webViews.PostTo("settings", out.dump());
                return;
            }
            std::string err;
            if (!pmui::custom_commands_host::save_commands_document(j["document"], err)) {
                out["ok"] = false;
                out["error"] = err.empty() ? "save_command_json_utf8 failed" : err;
                webViews.PostTo("settings", out.dump());
                return;
            }
            out["ok"] = true;
            out["data"] = pmui::custom_commands_host::commands_save_payload();
            if (onAppearanceChanged)
                onAppearanceChanged();
#else
            out["ok"] = false;
            out["error"] = "Custom commands are disabled in this build.";
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsTablerIconsGet") {
#if FEATURE_CUSTOM_COMMANDS
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"icons", list_tabler_filled_icons()},
            };
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{{"icons", nlohmann::json::array()}};
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsCliCommandsGet") {
#if FEATURE_CUSTOM_COMMANDS
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"commands", registered_cli_commands()},
            };
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{{"commands", nlohmann::json::array()}};
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsAppCommandsGet") {
#if FEATURE_CUSTOM_COMMANDS
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"commands", registered_app_commands()},
            };
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{{"commands", nlohmann::json::array()}};
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsRibbonCommandsGet") {
#if FEATURE_CUSTOM_COMMANDS
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"commands", registered_ribbon_commands()},
            };
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{{"commands", nlohmann::json::array()}};
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsCommandVariablesGet") {
#if FEATURE_CUSTOM_COMMANDS
            out["ok"] = true;
            out["data"] = nlohmann::json{
                {"variables", command_variable_palette()},
            };
#else
            out["ok"] = true;
            out["data"] = nlohmann::json{{"variables", nlohmann::json::array()}};
#endif
            webViews.PostTo("settings", out.dump());
            return;
        }

        if (method == "settingsCliCommandHelpGet") {
#if FEATURE_CUSTOM_COMMANDS
            const std::string command = j.value("command", std::string{});
            settings_rpc_async(webViews, rpcId, [command]() -> nlohmann::json {
                nlohmann::json reply;
                const auto schema = cli_command_help_schema(command);
                if (schema.empty()) {
                    reply["ok"] = false;
                    reply["error"] = "unknown CLI command";
                } else {
                    reply["ok"] = true;
                    reply["data"] = nlohmann::json{{"schema", schema}};
                }
                return reply;
            });
#else
            out["ok"] = false;
            out["error"] = "Custom commands are disabled in this build.";
            webViews.PostTo("settings", out.dump());
#endif
            return;
        }

        out["ok"] = false;
        out["error"] = "unknown settings providerRpc method";
        webViews.PostTo("settings", out.dump());
    };

    CWebViewManager::ShowRequest req{};
    req.id = "settings";
    req.title = L"Settings";
    req.opts = std::move(opts);
    req.style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE;
    req.exStyle = WS_EX_APPWINDOW;
    req.startupRect = RECT{px, py, px + pw, py + ph};
    webViews.ShowOrFocus(req);
}

} // namespace pmui

