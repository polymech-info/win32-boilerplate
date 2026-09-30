/**
 * Windows 11–oriented File Explorer context menu: IExplorerCommand.
 * Used with a sparse/MSIX AppxManifest (branding/AppxManifest.xml.in): package identity + com:InprocServer + desktop4:FileExplorerContextMenus.
 * This DLL does not replace IExecute (register-explorer); it is an additional surface for Explorer’s modern menu.
 * CLSIDs: keep in sync with PM_EXPLORER11_CLSID / PM_EXPLORER11_VIEWER_CLSID in CMake and AppxManifest.xml.in.
 * Viewer verb (`CLSID_PmImageExplorer11Viewer`): spawns the same exe as `pm-image --ui-preset=viewer --src …`
 * with semicolon-joined paths (folders and files from the Explorer selection).
 */
#if defined(_WIN32)

    #include "constants.hpp"
    #include "win/shell/pm_iexecute_map.h"

    #include <shlwapi.h>
    #include <wrl/client.h>
    #include <wrl/implements.h>

    #include <algorithm>
    #include <cstdio>
    #include <filesystem>
    #include <fstream>
    #include <iterator>
    #include <mutex>
    #include <set>
    #include <string>
    #include <vector>

    #include <shobjidl.h>
    #include <shlobj_core.h>
    #include <shellapi.h>
    #include <windows.h>

    #pragma comment(lib, "Shlwapi.lib")
    #pragma comment(lib, "Shell32.lib")
    #pragma comment(lib, "Ole32.lib")

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::InhibitRoOriginateError;
using Microsoft::WRL::MakeAndInitialize;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::WinRtClassicComMix;

enum class CommandKind {
    Root,
    OpenPixlwiz,
    Chat,
    Viewer,
    Workbench,
    TransformPreset,
    Custom,
};

static const char* kind_name(CommandKind kind);
struct Win11MenuEntry;
struct Win11MenuGroup;

// {A1B2C3D4-E5F6-4A5B-8C9D-0E1F2A3B4C5D} — sync with PM_EXPLORER11_CLSID in CMake / branding/AppxManifest.xml.in
static const GUID CLSID_PmImageExplorer11 = {
    0xA1B2C3D4, 0xE5F6, 0x4A5B, {0x8C, 0x9D, 0x0E, 0x1F, 0x2A, 0x3B, 0x4C, 0x5D}};
// {B2B3C4D5-E6F7-4A6B-9D8E-1F2A3B4C5D6E} — sync with PM_EXPLORER11_VIEWER_CLSID
static const GUID CLSID_PmImageExplorer11Viewer = {
    0xB2B3C4D5, 0xE6F7, 0x4A6B, {0x9D, 0x8E, 0x1F, 0x2A, 0x3B, 0x4C, 0x5D, 0x6E}};

static HMODULE g_hThis  = nullptr;
static long    g_cLock  = 0;
static std::mutex g_explorer11_log_mutex;

static constexpr const wchar_t k_open_in_pixlwiz_title[] = L"PixlWiz";

static std::string w_to_u8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static std::filesystem::path explorer11_config_dir() {
    wchar_t ad[32768]{};
    if (GetEnvironmentVariableW(L"APPDATA", ad, 32768) == 0) return {};
    std::filesystem::path p(ad);
    const std::wstring rel(pm::brand::k_config_subpath_w);
    size_t a = 0;
    for (;;) {
        const size_t b = rel.find(L'\\', a);
        if (b == std::wstring::npos) {
            if (a < rel.size()) p /= rel.substr(a);
            break;
        }
        if (b > a) p /= rel.substr(a, b - a);
        a = b + 1;
    }
    return p;
}

static void explorer11_log_u8(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_explorer11_log_mutex);
    try {
        std::error_code ec;
        const std::filesystem::path dir = explorer11_config_dir();
        if (dir.empty()) return;
        std::filesystem::create_directories(dir, ec);
        std::ofstream out(dir / L"pm-image-explorer11.log", std::ios::app | std::ios::binary);
        if (!out) return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char ts[40]{};
        sprintf_s(ts, "%04u-%02u-%02u %02u:%02u:%02u  ", (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                  (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        out << ts << line << '\n';
    } catch (...) {
    }
}

static std::string hr_u8(HRESULT hr) {
    char b[20]{};
    sprintf_s(b, "0x%08lX", (unsigned long)hr);
    return b;
}

static std::string clsid_u8(REFCLSID id) {
    wchar_t w[64]{};
    return StringFromGUID2(id, w, 64) ? w_to_u8(w) : "StringFromGUID2 failed";
}

static void append_quoted(std::wstring* o, const std::wstring& s) {
    *o += L'\"';
    for (wchar_t c : s) {
        if (c == L'\"') *o += L"\"\"";
        else *o += c;
    }
    *o += L'\"';
}

struct Win11MenuEntry {
    std::wstring title;
    CommandKind kind = CommandKind::OpenPixlwiz;
    std::wstring payload;
};

struct Win11MenuGroup {
    std::wstring title;
    std::vector<Win11MenuEntry> items;
    std::vector<Win11MenuGroup> submenus;
};

static std::wstring get_pm_image_exe() {
    if (!g_hThis) {
        explorer11_log_u8("get_pm_image_exe: g_hThis is null");
        return {};
    }
    wchar_t mod[MAX_PATH]{};
    if (!GetModuleFileNameW(g_hThis, mod, MAX_PATH)) {
        explorer11_log_u8("get_pm_image_exe: GetModuleFileNameW failed err=" + std::to_string(GetLastError()));
        return {};
    }
    if (!PathRemoveFileSpecW(mod)) {
        explorer11_log_u8("get_pm_image_exe: PathRemoveFileSpecW failed moduleU8=" + w_to_u8(mod));
        return {};
    }
    std::wstring p = mod;
    p += L"\\";
    p += pm::brand::k_exe_basename_w;
    if (PathFileExistsW(p.c_str())) return p;
    p = mod;
    p += L"\\..\\";
    p += pm::brand::k_exe_basename_w;
    wchar_t full[MAX_PATH]{};
    if (GetFullPathNameW(p.c_str(), MAX_PATH, full, nullptr) && PathFileExistsW(full)) return full;
    return std::wstring(mod) + L"\\" + std::wstring(pm::brand::k_exe_basename_w);
}

static std::wstring get_explorer11_icon_location() {
    std::wstring exep = get_pm_image_exe();
    if (!exep.empty() && PathFileExistsW(exep.c_str()))
        return exep + L",0";
    wchar_t mod[MAX_PATH]{};
    if (g_hThis && GetModuleFileNameW(g_hThis, mod, MAX_PATH) && PathFileExistsW(mod))
        return std::wstring(mod) + L",0";
    return {};
}

static GUID canonical_guid_for_kind(CommandKind kind) {
    switch (kind) {
    case CommandKind::Viewer: return CLSID_PmImageExplorer11Viewer;
    case CommandKind::Chat:
    case CommandKind::Workbench:
    case CommandKind::TransformPreset:
    case CommandKind::Custom:
    case CommandKind::Root:
    case CommandKind::OpenPixlwiz:
    default: return CLSID_PmImageExplorer11;
    }
}

static bool spawn_cmdline(const std::wstring& cmd) {
    // Match `pm_image_iexecute.cpp`: GUI subsystem + CREATE_NO_WINDOW is fine; do **not** set
    // STARTF_USESHOWWINDOW + SW_HIDE — the child inherits that for its first ShowWindow and the
    // main Win32++ frame stays invisible (Win11 Explorer host is especially sensitive here).
    static constexpr DWORD k_base_flags = CREATE_NO_WINDOW;
    STARTUPINFOW           si{};
    si.cb = sizeof si;

    const DWORD          flags_with_break = k_base_flags | CREATE_BREAKAWAY_FROM_JOB;
    PROCESS_INFORMATION  pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    explorer11_log_u8("CreateProcessW: cmdU8=" + w_to_u8(cmd.size() > 4000u ? cmd.substr(0, 4000u) + L"..." : cmd));
    BOOL ok = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags_with_break, nullptr, nullptr, &si, &pi);
    if (!ok) {
        const DWORD err0 = GetLastError();
        ok = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, k_base_flags, nullptr, nullptr, &si, &pi);
        if (ok)
            explorer11_log_u8("CreateProcessW: breakaway failed err=" + std::to_string(err0) + ", retry without breakaway ok");
        else
            explorer11_log_u8("CreateProcessW: failed err1=" + std::to_string(err0) + " err2=" + std::to_string(GetLastError()));
    }
    if (!ok)
        return false;
    explorer11_log_u8("CreateProcessW: ok");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static void paths_from_items(IShellItemArray* psia, std::vector<std::wstring>* out) {
    if (!psia || !out) {
        explorer11_log_u8("paths_from_items: null shell item array or out");
        return;
    }
    DWORD n = 0;
    const HRESULT hr_count = psia->GetCount(&n);
    if (FAILED(hr_count) || n == 0) {
        explorer11_log_u8("paths_from_items: GetCount hr=" + hr_u8(hr_count) + " count=" + std::to_string(n));
        return;
    }
    explorer11_log_u8("paths_from_items: count=" + std::to_string(n));
    for (DWORD i = 0; i < n; i++) {
        ComPtr<IShellItem> it;
        const HRESULT hr_item = psia->GetItemAt(i, &it);
        if (FAILED(hr_item)) {
            explorer11_log_u8("paths_from_items: GetItemAt[" + std::to_string(i) + "] hr=" + hr_u8(hr_item));
            continue;
        }
        PWSTR w = nullptr;
        const HRESULT hr_name = it->GetDisplayName(SIGDN_FILESYSPATH, &w);
        if (FAILED(hr_name) || !w) {
            explorer11_log_u8("paths_from_items: GetDisplayName[" + std::to_string(i) + "] hr=" + hr_u8(hr_name));
            continue;
        }
        std::wstring s(w);
        CoTaskMemFree(w);
        if (!s.empty()) {
            explorer11_log_u8("paths_from_items: item[" + std::to_string(i) + "] pathU8=" + w_to_u8(s));
            out->push_back(std::move(s));
        }
    }
}

static std::wstring cwd_from_first_path(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return {};
    const std::wstring& first = paths[0];
    const DWORD attrs = GetFileAttributesW(first.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return first;
    std::vector<wchar_t> buf(first.begin(), first.end());
    buf.push_back(L'\0');
    if (PathRemoveFileSpecW(buf.data())) return buf.data();
    return {};
}

static std::wstring explorer_view_path(const ComPtr<IUnknown>& site) {
    if (!site) {
        explorer11_log_u8("explorer_view_path: no site");
        return {};
    }

    ComPtr<IServiceProvider> service_provider;
    HRESULT hr = site.As(&service_provider);
    if (FAILED(hr)) {
        explorer11_log_u8("explorer_view_path: QI IServiceProvider failed hr=" + hr_u8(hr));
        return {};
    }

    ComPtr<IShellBrowser> shell_browser;
    hr = service_provider->QueryService(SID_SShellBrowser, IID_PPV_ARGS(&shell_browser));
    if (FAILED(hr)) {
        explorer11_log_u8("explorer_view_path: QueryService SID_SShellBrowser failed hr=" + hr_u8(hr));
        return {};
    }

    ComPtr<IShellView> shell_view;
    hr = shell_browser->QueryActiveShellView(&shell_view);
    if (FAILED(hr)) {
        explorer11_log_u8("explorer_view_path: QueryActiveShellView failed hr=" + hr_u8(hr));
        return {};
    }

    ComPtr<IFolderView> folder_view;
    hr = shell_view.As(&folder_view);
    if (FAILED(hr)) {
        explorer11_log_u8("explorer_view_path: QI IFolderView failed hr=" + hr_u8(hr));
        return {};
    }

    ComPtr<IPersistFolder2> persist_folder;
    hr = folder_view->GetFolder(IID_PPV_ARGS(&persist_folder));
    if (FAILED(hr)) {
        explorer11_log_u8("explorer_view_path: GetFolder IPersistFolder2 failed hr=" + hr_u8(hr));
        return {};
    }

    PIDLIST_ABSOLUTE pidl = nullptr;
    hr = persist_folder->GetCurFolder(&pidl);
    if (FAILED(hr) || !pidl) {
        explorer11_log_u8("explorer_view_path: GetCurFolder failed hr=" + hr_u8(hr));
        return {};
    }

    wchar_t path[MAX_PATH]{};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    explorer11_log_u8(std::string("explorer_view_path: SHGetPathFromIDListW ")
                      + (ok ? (std::string("ok pathU8=") + w_to_u8(path)) : std::string("failed")));
    return ok ? std::wstring(path) : std::wstring{};
}

static std::wstring join_paths(const std::vector<std::wstring>& paths) {
    std::wstring join;
    for (size_t i = 0; i < paths.size(); i++) {
        if (i) join += L';';
        join += paths[i];
    }
    return join;
}

static HRESULT invoke_kind_with_paths(CommandKind kind, const std::vector<std::wstring>& paths, const std::wstring& payload = {}) {
    if (kind == CommandKind::Root) {
        explorer11_log_u8("invoke_kind_with_paths: root maps to open-pixlwiz direct action");
        kind = CommandKind::OpenPixlwiz;
    }
    const std::wstring exep = get_pm_image_exe();
    explorer11_log_u8("invoke_kind_with_paths: kind=" + std::string(kind_name(kind)) + " exepU8=" + w_to_u8(exep)
                      + " exists=" + (PathFileExistsW(exep.c_str()) ? "yes" : "no")
                      + " paths=" + std::to_string(paths.size()));
    if (exep.empty() || !PathFileExistsW(exep.c_str())) return E_FAIL;
    if (paths.empty()) return S_OK;

    if (kind == CommandKind::TransformPreset) {
        if (payload.empty()) {
            explorer11_log_u8("invoke_kind_with_paths: transform empty preset id");
            return E_FAIL;
        }
        std::wstring cmd;
        append_quoted(&cmd, exep);
        const std::wstring cwd = cwd_from_first_path(paths);
        if (!cwd.empty()) {
            cmd += L" --cwd ";
            append_quoted(&cmd, cwd);
        }
        cmd += L" transform";
        for (const auto& f : paths) {
            cmd += L" --src ";
            append_quoted(&cmd, f);
        }
        cmd += L" --preset-id ";
        append_quoted(&cmd, payload);
        cmd += L" --job-ui";
        return spawn_cmdline(cmd) ? S_OK : E_FAIL;
    }

    if (kind == CommandKind::Custom) {
        if (payload.empty()) {
            explorer11_log_u8("invoke_kind_with_paths: custom empty command id");
            return E_FAIL;
        }
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd.push_back(L' ');
        append_quoted(&cmd, payload);
        for (const auto& f : paths) {
            cmd += L" --src ";
            append_quoted(&cmd, f);
        }
        return spawn_cmdline(cmd) ? S_OK : E_FAIL;
    }

    const std::wstring join = join_paths(paths);
    std::wstring cmd;
    append_quoted(&cmd, exep);
    const std::wstring cwd = cwd_from_first_path(paths);
    if (!cwd.empty()) {
        cmd += L" --cwd ";
        append_quoted(&cmd, cwd);
    }
    if (kind == CommandKind::Chat)
        cmd += L" --ui-preset=chat --src ";
    else if (kind == CommandKind::Viewer)
        cmd += L" --ui-preset=viewer --src ";
    else
        cmd += L" --ui-preset=main --src ";
    append_quoted(&cmd, join);
    return spawn_cmdline(cmd) ? S_OK : E_FAIL;
}

static bool reg_read_sz(HKEY root, const std::wstring& subkey, const wchar_t* value_name, std::wstring* out) {
    if (!out) return false;
    out->clear();
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD cb = 0;
    LSTATUS s = RegGetValueW(h, nullptr, value_name, RRF_RT_REG_SZ, &type, nullptr, &cb);
    if (s != ERROR_SUCCESS || cb < sizeof(wchar_t)) {
        RegCloseKey(h);
        return false;
    }
    std::wstring value(cb / sizeof(wchar_t), L'\0');
    s = RegGetValueW(h, nullptr, value_name, RRF_RT_REG_SZ, &type, value.data(), &cb);
    RegCloseKey(h);
    if (s != ERROR_SUCCESS)
        return false;
    if (!value.empty() && value.back() == L'\0')
        value.pop_back();
    *out = std::move(value);
    return true;
}

static bool reg_read_dword(HKEY root, const std::wstring& subkey, const wchar_t* value_name, DWORD* out) {
    if (!out) return false;
    *out = 0;
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD cb = sizeof(DWORD);
    DWORD value = 0;
    const LSTATUS s = RegGetValueW(h, nullptr, value_name, RRF_RT_REG_DWORD, &type, &value, &cb);
    RegCloseKey(h);
    if (s != ERROR_SUCCESS)
        return false;
    *out = value;
    return true;
}

static bool reg_key_exists(HKEY root, const std::wstring& subkey) {
    HKEY h = nullptr;
    const LSTATUS s = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &h);
    if (s == ERROR_SUCCESS) RegCloseKey(h);
    return s == ERROR_SUCCESS;
}

static std::vector<std::wstring> reg_enum_subkeys(HKEY root, const std::wstring& subkey) {
    std::vector<std::wstring> out;
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS)
        return out;
    DWORD index = 0;
    for (;;) {
        wchar_t name[256]{};
        DWORD cch = static_cast<DWORD>(std::size(name));
        const LSTATUS s = RegEnumKeyExW(h, index, name, &cch, nullptr, nullptr, nullptr, nullptr);
        if (s == ERROR_NO_MORE_ITEMS)
            break;
        if (s == ERROR_SUCCESS)
            out.emplace_back(name, cch);
        ++index;
    }
    RegCloseKey(h);
    return out;
}

static bool load_classic_delegate_entry(const std::wstring& verb_key, Win11MenuEntry* out) {
    if (!out) return false;
    std::wstring title;
    if (!reg_read_sz(HKEY_CURRENT_USER, verb_key, L"MUIVerb", &title) || title.empty())
        return false;
    std::wstring clsid;
    if (!reg_read_sz(HKEY_CURRENT_USER, verb_key + L"\\command", L"DelegateExecute", &clsid) || clsid.empty())
        return false;

    const std::wstring map_key = std::wstring(pm::iexecute::k_map_key_root()) + clsid;
    DWORD kind_dword = 0;
    if (!reg_read_dword(HKEY_CURRENT_USER, map_key, L"Kind", &kind_dword))
        return false;
    std::wstring payload;
    (void)reg_read_sz(HKEY_CURRENT_USER, map_key, L"Preset", &payload);

    if (kind_dword == static_cast<DWORD>(pm::iexecute::Kind::Transform)) {
        if (payload.empty()) return false;
        out->kind = CommandKind::TransformPreset;
    } else if (kind_dword == static_cast<DWORD>(pm::iexecute::Kind::Custom)) {
        if (payload.empty()) return false;
        out->kind = CommandKind::Custom;
    } else {
        return false;
    }
    out->title = std::move(title);
    out->payload = std::move(payload);
    return true;
}

static Win11MenuGroup load_classic_menu_group(const std::wstring& menu_key, std::set<std::wstring>& seen_payloads) {
    Win11MenuGroup group;
    (void)reg_read_sz(HKEY_CURRENT_USER, menu_key, L"MUIVerb", &group.title);
    if (group.title.empty())
        group.title = L"Commands";

    const std::wstring shell_key = menu_key + L"\\shell";
    for (const std::wstring& child : reg_enum_subkeys(HKEY_CURRENT_USER, shell_key)) {
        const std::wstring child_key = shell_key + L"\\" + child;
        if (reg_key_exists(HKEY_CURRENT_USER, child_key + L"\\shell")) {
            Win11MenuGroup sub = load_classic_menu_group(child_key, seen_payloads);
            if (!sub.items.empty() || !sub.submenus.empty())
                group.submenus.push_back(std::move(sub));
            continue;
        }
        Win11MenuEntry item;
        if (!load_classic_delegate_entry(child_key, &item))
            continue;
        const std::wstring dedupe = std::to_wstring(static_cast<int>(item.kind)) + L":" + item.payload;
        if (!seen_payloads.insert(dedupe).second)
            continue;
        group.items.push_back(std::move(item));
    }
    return group;
}

static std::vector<Win11MenuGroup> load_win11_classic_mirror_menus() {
    static const wchar_t* roots[] = {
        L"Software\\Classes\\Directory\\shell\\PM Media\\shell",
        L"Software\\Classes\\SystemFileAssociations\\.jpg\\shell\\PM Media\\shell",
        L"Software\\Classes\\SystemFileAssociations\\.png\\shell\\PM Media\\shell",
    };
    std::vector<Win11MenuGroup> out;
    std::set<std::wstring> seen_payloads;
    for (const wchar_t* root : roots) {
        if (!reg_key_exists(HKEY_CURRENT_USER, root))
            continue;
        explorer11_log_u8("Win11 classic mirror: reading HKCU\\" + w_to_u8(root));
        for (const std::wstring& child : reg_enum_subkeys(HKEY_CURRENT_USER, root)) {
            const std::wstring child_key = std::wstring(root) + L"\\" + child;
            if (!reg_key_exists(HKEY_CURRENT_USER, child_key + L"\\shell"))
                continue;
            Win11MenuGroup group = load_classic_menu_group(child_key, seen_payloads);
            if (!group.items.empty() || !group.submenus.empty()) {
                explorer11_log_u8("Win11 classic mirror: + submenu title=" + w_to_u8(group.title)
                                  + " items=" + std::to_string(group.items.size())
                                  + " submenus=" + std::to_string(group.submenus.size()));
                out.push_back(std::move(group));
            }
        }
        break;
    }
    explorer11_log_u8("Win11 classic mirror: groups=" + std::to_string(out.size())
                      + " payloads=" + std::to_string(seen_payloads.size()));
    return out;
}

// IExplorerCommand (SDK 10.0.26100): GetTitle, GetIcon, GetToolTip, GetCanonicalName, GetState, Invoke, GetFlags, EnumSubCommands
struct PmExplorer11Cmd
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExplorerCommand, IObjectWithSite, IShellExtInit, IContextMenu> {
    CommandKind m_kind = CommandKind::OpenPixlwiz;
    ComPtr<IUnknown> m_site;
    std::vector<std::wstring> m_legacy_paths;
    std::vector<ComPtr<IExplorerCommand>> m_explorer_commands;
    HRESULT RuntimeClassInitialize(CommandKind kind) {
        m_kind = kind;
        return S_OK;
    }
    HRESULT RuntimeClassInitialize(CommandKind kind, IUnknown* site) {
        m_kind = kind;
        m_site = site;
        return S_OK;
    }
    IFACEMETHODIMP GetTitle(
        _In_opt_ IShellItemArray* psiItemArray, _Outptr_result_nullonfailure_ PWSTR* ppszName) override;
    IFACEMETHODIMP GetIcon(
        _In_opt_ IShellItemArray* psiItemArray, _Outptr_result_nullonfailure_ PWSTR* ppszIcon) override;
    IFACEMETHODIMP GetToolTip(
        _In_opt_ IShellItemArray* psiItemArray, _Outptr_result_nullonfailure_ PWSTR* ppszInfotip) override;
    IFACEMETHODIMP GetCanonicalName(_Out_ GUID* pguidCommandName) override;
    IFACEMETHODIMP GetState(
        _In_opt_ IShellItemArray* psiItemArray, BOOL fOkToBeSlow, _Out_ EXPCMDSTATE* pCmdState) override;
    IFACEMETHODIMP Invoke(_In_opt_ IShellItemArray* psiItemArray, _In_opt_ IBindCtx* pbc) override;
    IFACEMETHODIMP GetFlags(_Out_ EXPCMDFLAGS* pFlags) override;
    IFACEMETHODIMP EnumSubCommands(_COM_Outptr_ IEnumExplorerCommand** ppEnum) override;
    IFACEMETHODIMP SetSite(_In_opt_ IUnknown* pUnkSite) override;
    IFACEMETHODIMP GetSite(_In_ REFIID riid, _COM_Outptr_ void** ppvSite) override;
    IFACEMETHODIMP Initialize(_In_opt_ PCIDLIST_ABSOLUTE pidlFolder, _In_opt_ IDataObject* pdtobj, _In_opt_ HKEY hkeyProgID) override;
    IFACEMETHODIMP QueryContextMenu(_In_ HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) override;
    IFACEMETHODIMP InvokeCommand(_In_ CMINVOKECOMMANDINFO* pici) override;
    IFACEMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* pReserved, LPSTR pszName, UINT cchMax) override;
    HRESULT PrepareExplorerCommands(const std::vector<std::wstring>& paths);
};

class PmExplorerCommandEnum
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IEnumExplorerCommand> {
    std::vector<ComPtr<IExplorerCommand>> m_commands;
    size_t m_index = 0;

public:
    HRESULT RuntimeClassInitialize(std::vector<ComPtr<IExplorerCommand>> commands) {
        m_commands = std::move(commands);
        return S_OK;
    }

    IFACEMETHODIMP Next(ULONG celt, IExplorerCommand** rgelt, ULONG* pceltFetched) override {
        if (!rgelt) return E_POINTER;
        if (pceltFetched) *pceltFetched = 0;
        ULONG fetched = 0;
        while (fetched < celt && m_index < m_commands.size()) {
            rgelt[fetched] = m_commands[m_index].Get();
            rgelt[fetched]->AddRef();
            ++fetched;
            ++m_index;
        }
        if (pceltFetched) *pceltFetched = fetched;
        return fetched == celt ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Skip(ULONG celt) override {
        m_index = (std::min)(m_index + static_cast<size_t>(celt), m_commands.size());
        return m_index < m_commands.size() ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Reset() override {
        m_index = 0;
        return S_OK;
    }

    IFACEMETHODIMP Clone(IEnumExplorerCommand** ppenum) override {
        if (!ppenum) return E_POINTER;
        *ppenum = nullptr;
        ComPtr<PmExplorerCommandEnum> clone;
        const HRESULT hr = MakeAndInitialize<PmExplorerCommandEnum>(&clone, m_commands);
        if (FAILED(hr)) return hr;
        clone->m_index = m_index;
        return clone.CopyTo(ppenum);
    }
};

static HRESULT make_explorer_command_for_group(const Win11MenuGroup& group, IUnknown* site, IExplorerCommand** out);

struct PmExplorer11LeafCmd
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix | InhibitRoOriginateError>, IExplorerCommand, IObjectWithSite> {
    CommandKind m_kind = CommandKind::OpenPixlwiz;
    ComPtr<IUnknown> m_site;
    std::wstring m_title;
    std::wstring m_payload;

    HRESULT RuntimeClassInitialize(CommandKind kind, IUnknown* site) {
        m_kind = kind;
        m_site = site;
        return S_OK;
    }

    HRESULT RuntimeClassInitialize(CommandKind kind, IUnknown* site, std::wstring title, std::wstring payload) {
        m_kind = kind;
        m_site = site;
        m_title = std::move(title);
        m_payload = std::move(payload);
        return S_OK;
    }

    IFACEMETHODIMP GetTitle(
        _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszName) override {
        if (!ppszName) return E_POINTER;
        *ppszName = nullptr;
        explorer11_log_u8(std::string("Leaf GetTitle: ") + kind_name(m_kind));
        std::wstring title = m_title;
        if (title.empty()) {
            title = k_open_in_pixlwiz_title;
            if (m_kind == CommandKind::Chat)
                title = L"Chat...";
            else if (m_kind == CommandKind::Viewer)
                title = L"Viewer...";
            else if (m_kind == CommandKind::Workbench)
                title = L"Workbench";
        }
        return SHStrDupW(title.c_str(), ppszName);
    }

    IFACEMETHODIMP GetIcon(
        _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszIcon) override {
        if (!ppszIcon) return E_POINTER;
        *ppszIcon = nullptr;
        const std::wstring ico = get_explorer11_icon_location();
        explorer11_log_u8("Leaf GetIcon: kind=" + std::string(kind_name(m_kind)) + " iconU8=" + w_to_u8(ico));
        if (ico.empty()) return E_NOTIMPL;
        return SHStrDupW(ico.c_str(), ppszIcon);
    }

    IFACEMETHODIMP GetToolTip(
        _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszInfotip) override {
        if (!ppszInfotip) return E_POINTER;
        *ppszInfotip = nullptr;
        explorer11_log_u8(std::string("Leaf GetToolTip: ") + kind_name(m_kind) + " E_NOTIMPL");
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(_Out_ GUID* pguidCommandName) override {
        if (!pguidCommandName) return E_POINTER;
        *pguidCommandName = GUID_NULL;
        explorer11_log_u8(std::string("Leaf GetCanonicalName: ") + kind_name(m_kind)
                          + " guid=" + clsid_u8(*pguidCommandName));
        return S_OK;
    }

    IFACEMETHODIMP GetState(
        _In_opt_ IShellItemArray* psiItemArray, BOOL fOkToBeSlow, _Out_ EXPCMDSTATE* pCmdState) override {
        (void)psiItemArray;
        (void)fOkToBeSlow;
        if (!pCmdState) return E_POINTER;
        *pCmdState = ECS_ENABLED;
        explorer11_log_u8(std::string("Leaf GetState: ") + kind_name(m_kind) + " enabled");
        return S_OK;
    }

    IFACEMETHODIMP Invoke(_In_opt_ IShellItemArray* psiItemArray, _In_opt_ IBindCtx* pbc) override {
        (void)pbc;
        explorer11_log_u8(std::string("Leaf Invoke: entry kind=") + kind_name(m_kind));
        std::vector<std::wstring> paths;
        paths_from_items(psiItemArray, &paths);
        if (paths.empty()) {
            std::wstring folder = explorer_view_path(m_site);
            if (!folder.empty()) paths.push_back(std::move(folder));
        }
        explorer11_log_u8("Leaf Invoke: paths count=" + std::to_string(paths.size()));
        return invoke_kind_with_paths(m_kind, paths, m_payload);
    }

    IFACEMETHODIMP GetFlags(_Out_ EXPCMDFLAGS* pFlags) override {
        if (!pFlags) return E_POINTER;
        *pFlags = ECF_DEFAULT;
        explorer11_log_u8(std::string("Leaf GetFlags: ") + kind_name(m_kind) + " ECF_DEFAULT");
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(_COM_Outptr_ IEnumExplorerCommand** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = nullptr;
        explorer11_log_u8(std::string("Leaf EnumSubCommands: ") + kind_name(m_kind) + " E_NOTIMPL");
        return E_NOTIMPL;
    }

    IFACEMETHODIMP SetSite(_In_opt_ IUnknown* pUnkSite) override {
        m_site = pUnkSite;
        explorer11_log_u8(std::string("Leaf SetSite: ") + (pUnkSite ? "site set" : "site cleared"));
        return S_OK;
    }

    IFACEMETHODIMP GetSite(_In_ REFIID riid, _COM_Outptr_ void** ppvSite) override {
        if (!ppvSite) return E_POINTER;
        *ppvSite = nullptr;
        if (!m_site) {
            explorer11_log_u8("Leaf GetSite: no site");
            return E_FAIL;
        }
        const HRESULT hr = m_site.CopyTo(riid, ppvSite);
        explorer11_log_u8("Leaf GetSite: riid=" + clsid_u8(riid) + " hr=" + hr_u8(hr));
        return hr;
    }
};

struct PmExplorer11MenuCmd
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExplorerCommand, IObjectWithSite> {
    Win11MenuGroup m_group;
    ComPtr<IUnknown> m_site;

    HRESULT RuntimeClassInitialize(Win11MenuGroup group, IUnknown* site) {
        m_group = std::move(group);
        m_site = site;
        return S_OK;
    }

    IFACEMETHODIMP GetTitle(
        _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszName) override {
        if (!ppszName) return E_POINTER;
        *ppszName = nullptr;
        explorer11_log_u8("Menu GetTitle: " + w_to_u8(m_group.title));
        return SHStrDupW(m_group.title.c_str(), ppszName);
    }

    IFACEMETHODIMP GetIcon(
        _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszIcon) override {
        if (!ppszIcon) return E_POINTER;
        *ppszIcon = nullptr;
        const std::wstring ico = get_explorer11_icon_location();
        if (ico.empty()) return E_NOTIMPL;
        return SHStrDupW(ico.c_str(), ppszIcon);
    }

    IFACEMETHODIMP GetToolTip(
        _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszInfotip) override {
        if (!ppszInfotip) return E_POINTER;
        *ppszInfotip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(_Out_ GUID* pguidCommandName) override {
        if (!pguidCommandName) return E_POINTER;
        *pguidCommandName = GUID_NULL;
        return S_OK;
    }

    IFACEMETHODIMP GetState(
        _In_opt_ IShellItemArray* /*psiItemArray*/, BOOL /*fOkToBeSlow*/, _Out_ EXPCMDSTATE* pCmdState) override {
        if (!pCmdState) return E_POINTER;
        *pCmdState = (m_group.items.empty() && m_group.submenus.empty()) ? ECS_HIDDEN : ECS_ENABLED;
        explorer11_log_u8("Menu GetState: title=" + w_to_u8(m_group.title)
                          + " items=" + std::to_string(m_group.items.size())
                          + " submenus=" + std::to_string(m_group.submenus.size())
                          + " state=" + (*pCmdState == ECS_ENABLED ? "enabled" : "hidden"));
        return S_OK;
    }

    IFACEMETHODIMP Invoke(_In_opt_ IShellItemArray* /*psiItemArray*/, _In_opt_ IBindCtx* /*pbc*/) override {
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetFlags(_Out_ EXPCMDFLAGS* pFlags) override {
        if (!pFlags) return E_POINTER;
        *pFlags = ECF_HASSUBCOMMANDS;
        explorer11_log_u8("Menu GetFlags: title=" + w_to_u8(m_group.title) + " ECF_HASSUBCOMMANDS");
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(_COM_Outptr_ IEnumExplorerCommand** ppEnum) override {
        if (!ppEnum) return E_POINTER;
        *ppEnum = nullptr;
        std::vector<ComPtr<IExplorerCommand>> commands;
        explorer11_log_u8("Menu EnumSubCommands: title=" + w_to_u8(m_group.title)
                          + " items=" + std::to_string(m_group.items.size())
                          + " submenus=" + std::to_string(m_group.submenus.size()));
        for (const auto& item : m_group.items) {
            ComPtr<IExplorerCommand> cmd;
            const HRESULT hr = MakeAndInitialize<PmExplorer11LeafCmd>(&cmd, item.kind, m_site.Get(), item.title, item.payload);
            if (SUCCEEDED(hr)) {
                explorer11_log_u8("Menu EnumSubCommands: + leaf title=" + w_to_u8(item.title)
                                  + " kind=" + kind_name(item.kind)
                                  + " payload=" + w_to_u8(item.payload));
                commands.push_back(cmd);
            }
            else explorer11_log_u8("Menu EnumSubCommands: leaf failed hr=" + hr_u8(hr));
        }
        for (const auto& sub : m_group.submenus) {
            ComPtr<IExplorerCommand> cmd;
            const HRESULT hr = make_explorer_command_for_group(sub, m_site.Get(), cmd.GetAddressOf());
            if (SUCCEEDED(hr)) {
                explorer11_log_u8("Menu EnumSubCommands: + submenu title=" + w_to_u8(sub.title));
                commands.push_back(cmd);
            }
            else explorer11_log_u8("Menu EnumSubCommands: submenu failed hr=" + hr_u8(hr));
        }
        ComPtr<PmExplorerCommandEnum> enumerator;
        const HRESULT hr = MakeAndInitialize<PmExplorerCommandEnum>(&enumerator, std::move(commands));
        if (FAILED(hr)) return hr;
        return enumerator.CopyTo(ppEnum);
    }

    IFACEMETHODIMP SetSite(_In_opt_ IUnknown* pUnkSite) override {
        m_site = pUnkSite;
        return S_OK;
    }

    IFACEMETHODIMP GetSite(_In_ REFIID riid, _COM_Outptr_ void** ppvSite) override {
        if (!ppvSite) return E_POINTER;
        *ppvSite = nullptr;
        if (!m_site) return E_FAIL;
        return m_site.CopyTo(riid, ppvSite);
    }
};

static HRESULT make_explorer_command_for_group(const Win11MenuGroup& group, IUnknown* site, IExplorerCommand** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    ComPtr<IExplorerCommand> cmd;
    const HRESULT hr = MakeAndInitialize<PmExplorer11MenuCmd>(&cmd, group, site);
    if (FAILED(hr)) return hr;
    return cmd.CopyTo(out);
}

static const char* kind_name(CommandKind kind) {
    switch (kind) {
    case CommandKind::Root: return "root";
    case CommandKind::OpenPixlwiz: return "open-pixlwiz";
    case CommandKind::Chat: return "chat";
    case CommandKind::Viewer: return "viewer";
    case CommandKind::Workbench: return "workbench";
    case CommandKind::TransformPreset: return "transform-preset";
    case CommandKind::Custom: return "custom";
    default: return "unknown";
    }
}

IFACEMETHODIMP PmExplorer11Cmd::GetTitle(
    _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszName) {
    if (!ppszName) return E_POINTER;
    *ppszName = nullptr;
    explorer11_log_u8(std::string("GetTitle: ") + kind_name(m_kind));
    const wchar_t* title = k_open_in_pixlwiz_title;
    if (m_kind == CommandKind::Chat)
        title = L"Chat\u2026";
    else if (m_kind == CommandKind::Viewer)
        title = L"Viewer\u2026";
    else if (m_kind == CommandKind::Workbench)
        title = L"Workbench";
    return SHStrDupW(title, ppszName);
}

IFACEMETHODIMP PmExplorer11Cmd::GetIcon(
    _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszIcon) {
    if (!ppszIcon) return E_POINTER;
    *ppszIcon = nullptr;
    const std::wstring exep = get_pm_image_exe();
    explorer11_log_u8("GetIcon: kind=" + std::string(kind_name(m_kind)) + " exepU8=" + w_to_u8(exep)
                      + " exists=" + (PathFileExistsW(exep.c_str()) ? "yes" : "no"));
    const std::wstring ico = get_explorer11_icon_location();
    explorer11_log_u8("GetIcon: kind=" + std::string(kind_name(m_kind)) + " iconU8=" + w_to_u8(ico));
    if (ico.empty()) return E_NOTIMPL;
    return SHStrDupW(ico.c_str(), ppszIcon);
}

IFACEMETHODIMP PmExplorer11Cmd::GetToolTip(
    _In_opt_ IShellItemArray* /*psiItemArray*/, _Outptr_result_nullonfailure_ PWSTR* ppszInfotip) {
    if (!ppszInfotip) return E_POINTER;
    *ppszInfotip = nullptr;
    explorer11_log_u8(std::string("GetToolTip: ") + kind_name(m_kind) + " E_NOTIMPL");
    return E_NOTIMPL;
}

IFACEMETHODIMP PmExplorer11Cmd::GetCanonicalName(_Out_ GUID* pguidCommandName) {
    if (!pguidCommandName) return E_POINTER;
    *pguidCommandName = (m_kind == CommandKind::Root) ? GUID_NULL : canonical_guid_for_kind(m_kind);
    explorer11_log_u8(std::string("GetCanonicalName: ") + kind_name(m_kind)
                      + " guid=" + clsid_u8(*pguidCommandName));
    return S_OK;
}

HRESULT PmExplorer11Cmd::PrepareExplorerCommands(const std::vector<std::wstring>& paths) {
    m_explorer_commands.clear();
    explorer11_log_u8("PrepareExplorerCommands: kind=" + std::string(kind_name(m_kind))
                      + " paths=" + std::to_string(paths.size()));
    if (m_kind != CommandKind::Root)
        return S_OK;

    ComPtr<IExplorerCommand> chat;
    HRESULT hr = MakeAndInitialize<PmExplorer11LeafCmd>(&chat, CommandKind::Chat, m_site.Get());
    if (FAILED(hr)) {
        explorer11_log_u8("PrepareExplorerCommands: create Chat failed hr=" + hr_u8(hr));
        return hr;
    }
    m_explorer_commands.push_back(chat);

    ComPtr<IExplorerCommand> viewer;
    hr = MakeAndInitialize<PmExplorer11LeafCmd>(&viewer, CommandKind::Viewer, m_site.Get());
    if (FAILED(hr)) {
        explorer11_log_u8("PrepareExplorerCommands: create Viewer failed hr=" + hr_u8(hr));
        return hr;
    }
    m_explorer_commands.push_back(viewer);

    ComPtr<IExplorerCommand> workbench;
    hr = MakeAndInitialize<PmExplorer11LeafCmd>(&workbench, CommandKind::Workbench, m_site.Get());
    if (FAILED(hr)) {
        explorer11_log_u8("PrepareExplorerCommands: create Workbench failed hr=" + hr_u8(hr));
        return hr;
    }
    m_explorer_commands.push_back(workbench);

    // Win11 packaged IExplorerCommand is reliable for this first submenu level (Chat / Viewer /
    // Workbench), but not for mirroring the classic Files/Presets cascades. In testing, Explorer
    // accepted the extra Files/Presets submenu commands and even queried their leaf entries, yet
    // rendered those submenus empty in the modern context menu. Keep those deeper command trees in
    // the classic "Show more options" menu for now instead of shipping a broken empty Win11 flyout.
    //
    // The loader remains below as a diagnostic/prototype path if we revisit this with a different
    // manifest shape or separate packaged verb IDs per branch.
#if 0
    const std::vector<Win11MenuGroup> classic_menus = load_win11_classic_mirror_menus();
    for (const auto& group : classic_menus) {
        ComPtr<IExplorerCommand> submenu_cmd;
        hr = make_explorer_command_for_group(group, m_site.Get(), submenu_cmd.GetAddressOf());
        if (FAILED(hr)) {
            explorer11_log_u8("PrepareExplorerCommands: create classic mirror submenu failed hr=" + hr_u8(hr)
                              + " title=" + w_to_u8(group.title));
            return hr;
        }
        explorer11_log_u8("PrepareExplorerCommands: + classic submenu title=" + w_to_u8(group.title)
                          + " items=" + std::to_string(group.items.size())
                          + " submenus=" + std::to_string(group.submenus.size()));
        m_explorer_commands.push_back(submenu_cmd);
    }
#endif

    explorer11_log_u8("PrepareExplorerCommands: root prepared commands=" + std::to_string(m_explorer_commands.size()));
    return S_OK;
}

IFACEMETHODIMP PmExplorer11Cmd::GetState(
    _In_opt_ IShellItemArray* psiItemArray, BOOL fOkToBeSlow, _Out_ EXPCMDSTATE* pCmdState) {
    if (!pCmdState) return E_POINTER;
    *pCmdState = ECS_ENABLED;
    explorer11_log_u8("GetState: entry kind=" + std::string(kind_name(m_kind))
                      + " fOkToBeSlow=" + (fOkToBeSlow ? std::string("true") : std::string("false")));
    if (m_kind == CommandKind::Root && !fOkToBeSlow) {
        explorer11_log_u8("GetState: root E_PENDING until Explorer allows slow state");
        return E_PENDING;
    }

    std::vector<std::wstring> paths;
    paths_from_items(psiItemArray, &paths);
    DWORD n = 0;
    HRESULT hr_count = psiItemArray ? psiItemArray->GetCount(&n) : E_POINTER;
    if (paths.empty()) {
        std::wstring folder = explorer_view_path(m_site);
        *pCmdState = folder.empty() ? ECS_HIDDEN : ECS_ENABLED;
        if (!folder.empty())
            paths.push_back(std::move(folder));
        explorer11_log_u8("GetState: no selection/count failed hr=" + hr_u8(hr_count) + " count=" + std::to_string(n)
                          + " fallbackPaths=" + std::to_string(paths.size())
                          + " state=" + (*pCmdState == ECS_ENABLED ? "enabled" : "hidden"));
    } else {
        *pCmdState = ECS_ENABLED;
        explorer11_log_u8("GetState: selection count=" + std::to_string(n)
                          + " paths=" + std::to_string(paths.size()) + " state=enabled");
    }

    if (*pCmdState != ECS_HIDDEN && m_kind == CommandKind::Root) {
        const HRESULT hr_prepare = QueryContextMenu(nullptr, 0, 0, 0, CMF_EXTENDEDVERBS | CMF_NORMAL);
        if (FAILED(hr_prepare) || m_explorer_commands.empty()) {
            *pCmdState = ECS_HIDDEN;
            explorer11_log_u8("GetState: root hidden; QueryContextMenu(nullptr) hr=" + hr_u8(hr_prepare)
                              + " commands=" + std::to_string(m_explorer_commands.size()));
        } else {
            explorer11_log_u8("GetState: root ready commands=" + std::to_string(m_explorer_commands.size()));
        }
    }

    return S_OK;
}

IFACEMETHODIMP PmExplorer11Cmd::Invoke(
    _In_opt_ IShellItemArray* psiItemArray, _In_opt_ IBindCtx* pbc) {
    (void)pbc;
    explorer11_log_u8(std::string("Invoke: entry kind=") + kind_name(m_kind));
    std::vector<std::wstring> paths;
    paths_from_items(psiItemArray, &paths);
    if (paths.empty()) {
        std::wstring folder = explorer_view_path(m_site);
        if (!folder.empty()) paths.push_back(std::move(folder));
    }
    explorer11_log_u8("Invoke: paths count=" + std::to_string(paths.size()));
    return invoke_kind_with_paths(m_kind, paths);
}

IFACEMETHODIMP PmExplorer11Cmd::GetFlags(_Out_ EXPCMDFLAGS* pFlags) {
    if (!pFlags) return E_POINTER;
    *pFlags = (m_kind == CommandKind::Root) ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
    explorer11_log_u8(std::string("GetFlags: kind=") + kind_name(m_kind)
                      + (m_kind == CommandKind::Root ? " ECF_HASSUBCOMMANDS" : " ECF_DEFAULT"));
    return S_OK;
}

IFACEMETHODIMP PmExplorer11Cmd::EnumSubCommands(_COM_Outptr_ IEnumExplorerCommand** ppEnum) {
    if (!ppEnum) return E_POINTER;
    *ppEnum = nullptr;
    if (m_kind != CommandKind::Root) {
        explorer11_log_u8(std::string("EnumSubCommands: ") + kind_name(m_kind) + " E_NOTIMPL");
        return E_NOTIMPL;
    }

    m_explorer_commands.clear();
    const HRESULT hr_query = QueryContextMenu(nullptr, 0, 0, 0, CMF_EXTENDEDVERBS | CMF_NORMAL);
    if (FAILED(hr_query)) {
        explorer11_log_u8("EnumSubCommands: QueryContextMenu(nullptr) failed hr=" + hr_u8(hr_query));
        return hr_query;
    }

    std::vector<ComPtr<IExplorerCommand>> commands = m_explorer_commands;
    ComPtr<PmExplorerCommandEnum> enumerator;
    HRESULT hr = MakeAndInitialize<PmExplorerCommandEnum>(&enumerator, std::move(commands));
    if (FAILED(hr)) {
        explorer11_log_u8("EnumSubCommands: create enum failed hr=" + hr_u8(hr));
        return hr;
    }
    explorer11_log_u8("EnumSubCommands: root returning commands=" + std::to_string(m_explorer_commands.size()));
    return enumerator.CopyTo(ppEnum);

#if 0
    ComPtr<IExplorerCommand> open;
    HRESULT hr = MakeAndInitialize<PmExplorer11Cmd>(&open, CommandKind::OpenPixlwiz, m_site.Get());
    if (FAILED(hr)) {
        explorer11_log_u8("EnumSubCommands: create OpenPixlwiz failed hr=" + hr_u8(hr));
        return hr;
    }
    commands.push_back(open);

    ComPtr<IExplorerCommand> viewer;
    hr = MakeAndInitialize<PmExplorer11Cmd>(&viewer, CommandKind::Viewer, m_site.Get());
    if (FAILED(hr)) {
        explorer11_log_u8("EnumSubCommands: create Viewer failed hr=" + hr_u8(hr));
        return hr;
    }
    commands.push_back(viewer);

    ComPtr<PmExplorerCommandEnum> enumerator;
    hr = MakeAndInitialize<PmExplorerCommandEnum>(&enumerator, std::move(commands));
    if (FAILED(hr)) {
        explorer11_log_u8("EnumSubCommands: create enum failed hr=" + hr_u8(hr));
        return hr;
    }
    explorer11_log_u8("EnumSubCommands: root returning 2 commands");
    return enumerator.CopyTo(ppEnum);
#endif
}

IFACEMETHODIMP PmExplorer11Cmd::SetSite(_In_opt_ IUnknown* pUnkSite) {
    m_site = pUnkSite;
    explorer11_log_u8(std::string("SetSite: ") + (pUnkSite ? "site set" : "site cleared"));
    return S_OK;
}

IFACEMETHODIMP PmExplorer11Cmd::GetSite(_In_ REFIID riid, _COM_Outptr_ void** ppvSite) {
    if (!ppvSite) return E_POINTER;
    *ppvSite = nullptr;
    if (!m_site) {
        explorer11_log_u8("GetSite: no site");
        return E_FAIL;
    }
    const HRESULT hr = m_site.CopyTo(riid, ppvSite);
    explorer11_log_u8("GetSite: riid copy hr=" + hr_u8(hr));
    return hr;
}

IFACEMETHODIMP PmExplorer11Cmd::Initialize(
    _In_opt_ PCIDLIST_ABSOLUTE /*pidlFolder*/, _In_opt_ IDataObject* pdtobj, _In_opt_ HKEY /*hkeyProgID*/) {
    m_legacy_paths.clear();
    explorer11_log_u8(std::string("IShellExtInit::Initialize: dataObject=") + (pdtobj ? "yes" : "no"));
    if (!pdtobj) return S_OK;

    FORMATETC fmt{};
    fmt.cfFormat = CF_HDROP;
    fmt.ptd = nullptr;
    fmt.dwAspect = DVASPECT_CONTENT;
    fmt.lindex = -1;
    fmt.tymed = TYMED_HGLOBAL;

    STGMEDIUM stg{};
    const HRESULT hr = pdtobj->GetData(&fmt, &stg);
    if (FAILED(hr)) {
        explorer11_log_u8("IShellExtInit::Initialize: CF_HDROP GetData failed hr=" + hr_u8(hr));
        return S_OK;
    }

    HDROP drop = static_cast<HDROP>(GlobalLock(stg.hGlobal));
    if (drop) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        explorer11_log_u8("IShellExtInit::Initialize: CF_HDROP count=" + std::to_string(count));
        for (UINT i = 0; i < count; ++i) {
            const UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(len, L'\0');
            if (DragQueryFileW(drop, i, path.data(), len + 1)) {
                explorer11_log_u8("IShellExtInit::Initialize: item[" + std::to_string(i) + "] pathU8=" + w_to_u8(path));
                m_legacy_paths.push_back(std::move(path));
            }
        }
        GlobalUnlock(stg.hGlobal);
    } else {
        explorer11_log_u8("IShellExtInit::Initialize: GlobalLock failed");
    }
    ReleaseStgMedium(&stg);
    return S_OK;
}

IFACEMETHODIMP PmExplorer11Cmd::QueryContextMenu(
    _In_ HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags) {
    explorer11_log_u8("IContextMenu::QueryContextMenu: hmenu=" + std::string(hmenu ? "yes" : "no")
                      + " idFirst=" + std::to_string(idCmdFirst)
                      + " idLast=" + std::to_string(idCmdLast)
                      + " flags=" + std::to_string(uFlags)
                      + " legacyPaths=" + std::to_string(m_legacy_paths.size()));
    if (!hmenu) {
        const HRESULT hr_prepare = PrepareExplorerCommands(m_legacy_paths);
        explorer11_log_u8("IContextMenu::QueryContextMenu: null hmenu prepared commands="
                          + std::to_string(m_explorer_commands.size()) + " hr=" + hr_u8(hr_prepare));
        return hr_prepare;
    }

    (void)indexMenu;
    (void)idCmdFirst;
    (void)idCmdLast;
    explorer11_log_u8("IContextMenu::QueryContextMenu: visible classic menu disabled for packaged command");
    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
}

IFACEMETHODIMP PmExplorer11Cmd::InvokeCommand(_In_ CMINVOKECOMMANDINFO* pici) {
    if (!pici) return E_POINTER;
    if (HIWORD(pici->lpVerb)) return E_FAIL;
    const UINT id = LOWORD(pici->lpVerb);
    const CommandKind kind = id == 1 ? CommandKind::Viewer : CommandKind::OpenPixlwiz;
    explorer11_log_u8("IContextMenu::InvokeCommand: id=" + std::to_string(id)
                      + " kind=" + kind_name(kind)
                      + " legacyPaths=" + std::to_string(m_legacy_paths.size()));
    return invoke_kind_with_paths(kind, m_legacy_paths);
}

IFACEMETHODIMP PmExplorer11Cmd::GetCommandString(
    UINT_PTR idCmd, UINT uType, UINT* /*pReserved*/, LPSTR pszName, UINT cchMax) {
    explorer11_log_u8("IContextMenu::GetCommandString: id=" + std::to_string(idCmd)
                      + " type=" + std::to_string(uType));
    if (!pszName || cchMax == 0) return E_INVALIDARG;
    const wchar_t* text_w = idCmd == 1 ? L"Open in Viewer" : L"Open in Pixlwiz";
    if (uType & GCS_UNICODE) {
        wcsncpy_s(reinterpret_cast<PWSTR>(pszName), cchMax, text_w, _TRUNCATE);
    } else {
        const std::string text = w_to_u8(text_w);
        strncpy_s(pszName, cchMax, text.c_str(), _TRUNCATE);
    }
    return S_OK;
}

class Explorer11ClassFactory : public IClassFactory {
    ULONG m_cref = 1;
    CommandKind m_kind = CommandKind::Root;

public:
    explicit Explorer11ClassFactory(CommandKind kind) : m_kind(kind) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            explorer11_log_u8("ClassFactory::QueryInterface: riid=" + clsid_u8(riid) + " hr=0x00000000");
            return S_OK;
        }
        explorer11_log_u8("ClassFactory::QueryInterface: riid=" + clsid_u8(riid) + " hr=E_NOINTERFACE");
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement((LONG*)&m_cref); }
    IFACEMETHODIMP_(ULONG) Release() {
        ULONG c = (ULONG)InterlockedDecrement((LONG*)&m_cref);
        if (c == 0) delete this;
        return c;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        explorer11_log_u8(std::string("ClassFactory::CreateInstance: kind=") + kind_name(m_kind)
                          + " riid=" + clsid_u8(riid));
        ComPtr<IExplorerCommand> h;
        const HRESULT             hr = MakeAndInitialize<PmExplorer11Cmd>(&h, m_kind);
        if (FAILED(hr)) {
            explorer11_log_u8("ClassFactory::CreateInstance: MakeAndInitialize failed hr=" + hr_u8(hr));
            return hr;
        }
        const HRESULT hr_qi = h->QueryInterface(riid, ppv);
        explorer11_log_u8("ClassFactory::CreateInstance: QI riid=" + clsid_u8(riid) + " hr=" + hr_u8(hr_qi));
        return hr_qi;
    }
    IFACEMETHODIMP LockServer(BOOL fLock) {
        if (fLock) InterlockedIncrement(&g_cLock);
        else InterlockedDecrement(&g_cLock);
        return S_OK;
    }
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    const bool open_viewer = IsEqualCLSID(rclsid, CLSID_PmImageExplorer11Viewer);
    const bool open_root = IsEqualCLSID(rclsid, CLSID_PmImageExplorer11);
    const CommandKind kind = open_viewer ? CommandKind::Viewer : CommandKind::Root;
    explorer11_log_u8("DllGetClassObject: rclsid=" + clsid_u8(rclsid) + " kind="
                      + (open_viewer ? "viewer" : (open_root ? "root" : "unknown"))
                      + " riid=" + clsid_u8(riid));
    if (!open_viewer && !IsEqualCLSID(rclsid, CLSID_PmImageExplorer11)) return CLASS_E_CLASSNOTAVAILABLE;
    IClassFactory* f = new Explorer11ClassFactory(kind);
    const HRESULT  hr  = f->QueryInterface(riid, ppv);
    f->Release();
    explorer11_log_u8("DllGetClassObject: factory QI riid=" + clsid_u8(riid) + " hr=" + hr_u8(hr));
    return hr;
}

STDAPI DllCanUnloadNow() {
    const HRESULT hr = g_cLock == 0 ? S_OK : S_FALSE;
    explorer11_log_u8("DllCanUnloadNow: locks=" + std::to_string(g_cLock) + " hr=" + hr_u8(hr));
    return hr;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, void*) {
    if (r == DLL_PROCESS_ATTACH) {
        g_hThis = h;
        DisableThreadLibraryCalls(h);
        wchar_t host[MAX_PATH]{};
        GetModuleFileNameW(nullptr, host, MAX_PATH);
        explorer11_log_u8("DllMain: pm-image-explorer11 loaded hostU8=" + w_to_u8(host));
    }
    return TRUE;
}

#endif
