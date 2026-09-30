/**
 * In-proc shell extension: IExecuteCommand + IObjectWithSelection.
 * https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexecutecommand
 * CLSID → payload: HKCU\…\IExecuteMap\<GUID>\ (see pm::brand; written by register-explorer).
 *
 * **Explorer "Chat…" (Kind::Chat)** / **Open in Viewer (Kind::Viewer, ProgId @c PolyMech.pm-image.viewer):**
 *   spawns `pm-image.exe --ui-preset=viewer --src "<;joined paths>"` (DelegateExecute from register-explorer).
 *   Viewer path collection accepts **folders** (one path each) and **viewer-previewable files** (images, text,
 *   PDF, 3D, spreadsheets, HTML, and video — same rules as the in-app viewer), not only images.
 * with no PowerShell middleman. Shell verbs use DelegateExecute to this DLL;
 * install with `pm-image register-explorer`, not ad-hoc registry edits.
 */
#if defined(_WIN32)

    #include "pm_iexecute_map.h"
    #include "file_extensions.hpp"

    #include <shlwapi.h>
    #include <wrl/client.h>
    #include <wrl/implements.h>

    #include <algorithm>
    #include <cctype>
    #include <cstdio>
    #include <cwctype>
    #include <filesystem>
    #include <fstream>
    #include <mutex>
    #include <string>
    #include <vector>

    #include <shobjidl.h>
    #include <windows.h>

    #pragma comment(lib, "Shlwapi.lib")
    #pragma comment(lib, "Shell32.lib")
    #pragma comment(lib, "Ole32.lib")
    #pragma comment(lib, "Propsys.lib")

namespace fs = std::filesystem;

namespace {
    /** Extract parent directory from first path for --cwd. Handles both files and folders. */
    std::wstring cwd_from_first_path(const std::vector<std::wstring>& files) {
        if (files.empty()) return {};
        std::error_code ec;
        fs::path p = fs::path(files[0]);
        if (fs::is_directory(p, ec)) {
            return p.wstring();
        }
        // File: return its parent directory
        return p.parent_path().wstring();
    }
}

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

static HMODULE g_hThis = nullptr;
static long    g_cLock  = 0;
static std::mutex g_iexecute_file_log_mutex;
/** Same directory as register-explorer.log: %APPDATA%\\<k_config_subpath>\\ */
static constexpr wchar_t k_iexecute_log_name[] = L"pm-image-iexecute.log";

static std::string w_to_u8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

static std::filesystem::path iexecute_config_dir() {
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

/** No dependency on the main EXE: append UTF-8 lines (when COM runs, Explorer is the host). */
static void iexecute_log_u8(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_iexecute_file_log_mutex);
    try {
        std::error_code ec;
        const std::filesystem::path dir = iexecute_config_dir();
        if (dir.empty()) return;
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path logf = dir / k_iexecute_log_name;
        std::ofstream out(logf, std::ios::app | std::ios::binary);
        if (!out) return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char ts[40]{};
        sprintf_s(ts, "%04u-%02u-%02u %02u:%02u:%02u  ", (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                  (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        out << ts << line << '\n';
        out.flush();
    } catch (...) {
    }
}

static std::wstring get_pm_image_exe() {
    if (!g_hThis) return {};
    wchar_t mod[MAX_PATH]{};
    if (!GetModuleFileNameW(g_hThis, mod, MAX_PATH)) return {};
    if (!PathRemoveFileSpecW(mod)) return {};
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

static void append_quoted(std::wstring* o, const std::wstring& s) {
    *o += L'\"';
    for (wchar_t c : s) {
        if (c == L'\"') *o += L"\"\"";
        else *o += c;
    }
    *o += L'\"';
}

static bool is_image_ext(const std::wstring& ext) {
    static const wchar_t* k[] = {L".jpg",  L".jpeg", L".png",  L".gif", L".bmp", L".webp", L".tiff", L".tif",
                                 L".jpe",  L".jfif",  L".avif", L".arw"};
    std::wstring e = ext;
    for (auto& c : e) c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    for (const auto* t : k) {
        if (e == t) return true;
    }
    return false;
}

static void add_expanded(const std::wstring& path, std::vector<std::wstring>* out) {
    std::error_code ec;
    if (path.empty()) return;
    fs::path p = path;
    if (!fs::exists(p, ec) || ec) return;
    if (fs::is_directory(p, ec) && !ec) {
        try {
            for (const auto& ent : fs::recursive_directory_iterator(
                     p, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (!ent.is_regular_file()) continue;
                if (!is_image_ext(ent.path().extension().wstring())) continue;
                out->push_back(ent.path().lexically_normal().wstring());
            }
        } catch (...) {
        }
    } else {
        if (is_image_ext(p.extension().wstring())) out->push_back(p.lexically_normal().wstring());
    }
}

static bool is_workbench_previewable_file(const std::wstring& path) {
    std::wstring ext = fs::path(path).extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    return pmui::is_image_ext(ext) || pmui::is_browser_image_ext(ext) || pmui::is_text_preview_eligible_for_path(path)
        || pmui::is_viewer_3d_ext(ext) || pmui::is_viewer_pdf_ext(ext) || pmui::is_viewer_spreadsheet_ext(ext)
        || pmui::is_html_ext(ext) || pmui::is_video_preview_eligible_for_path(path);
}

/** "Open in Workbench" / Chat: pass directories as a single path (app expands in-queue). Do not
 *  enumerate the tree in the shell — huge lists exceed CreateProcess cmdline limits and
 *  the UI seed must stay one logical folder. */
static void add_open_ui_path(const std::wstring& path, std::vector<std::wstring>* out) {
    std::error_code ec;
    if (path.empty()) return;
    const fs::path p = path;
    if (!fs::exists(p, ec) || ec) return;
    if (fs::is_directory(p, ec) && !ec) {
        out->push_back(p.lexically_normal().wstring());
        return;
    }
    if (fs::is_regular_file(p, ec) && !ec) {
        if (is_workbench_previewable_file(path)) out->push_back(p.lexically_normal().wstring());
    }
}

/** PM Viewer (`Kind::Viewer`): directories as one path each; regular files the centre viewer can open. */
static void add_viewer_open_path(const std::wstring& path, std::vector<std::wstring>* out) {
    std::error_code ec;
    if (path.empty() || !out) return;
    const fs::path p = path;
    if (!fs::exists(p, ec) || ec) return;
    if (fs::is_directory(p, ec) && !ec) {
        out->push_back(p.lexically_normal().wstring());
        return;
    }
    if (!fs::is_regular_file(p, ec) || ec) return;
    if (is_workbench_previewable_file(path))
        out->push_back(p.lexically_normal().wstring());
}

static void paths_from_ishell(ComPtr<IShellItemArray> spia, std::vector<std::wstring>* out) {
    DWORD n = 0;
    if (!spia || FAILED(spia->GetCount(&n)) || n == 0) return;
    for (DWORD i = 0; i < n; i++) {
        ComPtr<IShellItem> it;
        if (FAILED(spia->GetItemAt(i, &it))) continue;
        PWSTR w = nullptr;
        if (FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &w)) || !w) continue;
        std::wstring s(w);
        CoTaskMemFree(w);
        if (!s.empty()) add_expanded(s, out);
    }
}

static void paths_from_ishell_browse(ComPtr<IShellItemArray> spia, std::vector<std::wstring>* out) {
    DWORD n = 0;
    if (!spia || FAILED(spia->GetCount(&n)) || n == 0) return;
    for (DWORD i = 0; i < n; i++) {
        ComPtr<IShellItem> it;
        if (FAILED(spia->GetItemAt(i, &it))) continue;
        PWSTR w = nullptr;
        if (FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &w)) || !w) continue;
        std::wstring s(w);
        CoTaskMemFree(w);
        if (!s.empty()) {
            if (n <= 8u)
                iexecute_log_u8("Execute: openUi/Chat item[" + std::to_string((unsigned long)i) + "] pathU8=" + w_to_u8(s));
            add_open_ui_path(s, out);
        }
    }
    if (n > 8u)
        iexecute_log_u8("Execute: openUi/Chat shell item count=" + std::to_string(n) + " (per-item paths logged only when count<=8)");
}

static void paths_from_ishell_raw(ComPtr<IShellItemArray> spia, std::vector<std::wstring>* out) {
    DWORD n = 0;
    if (!spia || FAILED(spia->GetCount(&n)) || n == 0) return;
    for (DWORD i = 0; i < n; i++) {
        ComPtr<IShellItem> it;
        if (FAILED(spia->GetItemAt(i, &it))) continue;
        PWSTR w = nullptr;
        if (FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &w)) || !w) continue;
        std::wstring s(w);
        CoTaskMemFree(w);
        if (!s.empty()) {
            if (n <= 16u)
                iexecute_log_u8("Execute: custom raw item[" + std::to_string((unsigned long)i) + "] pathU8=" + w_to_u8(s));
            out->push_back(std::move(s));
        }
    }
    if (n > 16u)
        iexecute_log_u8("Execute: custom raw shell item count=" + std::to_string(n) + " (per-item paths logged only when count<=16)");
}

static void paths_from_ishell_viewer(ComPtr<IShellItemArray> spia, std::vector<std::wstring>* out) {
    DWORD n = 0;
    if (!spia || FAILED(spia->GetCount(&n)) || n == 0) return;
    for (DWORD i = 0; i < n; i++) {
        ComPtr<IShellItem> it;
        if (FAILED(spia->GetItemAt(i, &it))) continue;
        PWSTR w = nullptr;
        if (FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &w)) || !w) continue;
        std::wstring s(w);
        CoTaskMemFree(w);
        if (!s.empty()) {
            if (n <= 8u)
                iexecute_log_u8("Execute: Viewer item[" + std::to_string((unsigned long)i) + "] pathU8=" + w_to_u8(s));
            add_viewer_open_path(s, out);
        }
    }
    if (n > 8u)
        iexecute_log_u8("Execute: Viewer shell item count=" + std::to_string(n) + " (per-item paths logged only when count<=8)");
}

static bool spawn_cmdline(const std::wstring& cmd) {
    // `pm-image.exe` is a /SUBSYSTEM:WINDOWS app. We still use CREATE_NO_WINDOW: for a GUI
    // process it does not create a conhost; it is harmless. (When this was used for a true
    // console app, the flag suppressed a stray console window.)
    static constexpr DWORD k_base_flags = CREATE_NO_WINDOW;

    // **Do not** set STARTF_USESHOWWINDOW + SW_HIDE. The process inherits the startup show
    // state for the first ShowWindow(nCmdShow): SW_HIDE (0) makes SW_SHOWDEFAULT hide the
    // main window — the user gets "no UI" / headless until e.g. the file manager (Salamander)
    // exits and focus/activation changes. Default STARTUPINFO (all zeros) leaves the normal
    // show path for Win32++ / the CRT.
    STARTUPINFOW si{};
    si.cb = sizeof si;

    // Shell hosts (Explorer, some file managers) can place the in-proc IExecute object in a
    // job object. Without breakaway, the child can inherit job limits or odd lifetime; try
    // breakaway first, then fall back to plain CreateProcess.
    const DWORD   flags_with_break = k_base_flags | CREATE_BREAKAWAY_FROM_JOB;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    BOOL ok = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags_with_break, nullptr, nullptr, &si, &pi);
    if (!ok) {
        const DWORD err0 = GetLastError();
        ok = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, k_base_flags, nullptr, nullptr, &si, &pi);
        if (ok) {
            iexecute_log_u8("CreateProcessW: breakaway from job not allowed (err=" + std::to_string(err0)
                            + ") — retried without CREATE_BREAKAWAY_FROM_JOB, ok");
        } else {
            const DWORD err = GetLastError();
            iexecute_log_u8("CreateProcessW failed (with+without breakaway) err2=" + std::to_string(err) + " err1="
                            + std::to_string(err0) + " cmdU8=" + w_to_u8(cmd.size() > 4000 ? cmd.substr(0, 4000) + L"…" : cmd));
            return false;
        }
    }
    iexecute_log_u8("CreateProcessW ok (process started)");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

class ClassFactory : public IClassFactory {
    ULONG                   m_cref   = 1;
    pm::iexecute::Payload  m_pl{};
public:
    explicit ClassFactory(const pm::iexecute::Payload& p) : m_pl(p) {}
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement((LONG*)&m_cref); }
    IFACEMETHODIMP_(ULONG) Release() {
        ULONG c = (ULONG)InterlockedDecrement((LONG*)&m_cref);
        if (c == 0) delete this;
        return c;
    }
    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv);
    IFACEMETHODIMP LockServer(BOOL fLock) {
        if (fLock) InterlockedIncrement(&g_cLock);
        else InterlockedDecrement(&g_cLock);
        return S_OK;
    }
};

class PmExecuteHandler
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExecuteCommand, IObjectWithSelection> {
    ComPtr<IShellItemArray>                 m_spia;
    pm::iexecute::Payload  m_p{};
    std::wstring m_directory;

public:
    HRESULT RuntimeClassInitialize(const pm::iexecute::Payload& p) {
        m_p = p;
        return S_OK;
    }
    IFACEMETHODIMP SetKeyState(DWORD) override { return S_OK; }
    IFACEMETHODIMP SetParameters(LPCWSTR) override { return S_OK; }
    IFACEMETHODIMP SetPosition(POINT) override { return S_OK; }
    IFACEMETHODIMP SetShowWindow(int) override { return S_OK; }
    IFACEMETHODIMP SetNoShowUI(BOOL) override { return S_OK; }
    IFACEMETHODIMP SetDirectory(LPCWSTR directory) override {
        m_directory = directory ? directory : L"";
        return S_OK;
    }
    IFACEMETHODIMP SetSelection(__RPC__in_opt IShellItemArray* psia) override {
        m_spia = psia;
        return S_OK;
    }
    IFACEMETHODIMP GetSelection(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (!m_spia) return E_FAIL;
        return m_spia->QueryInterface(riid, ppv);
    }
    IFACEMETHODIMP Execute() override;
};

IFACEMETHODIMP PmExecuteHandler::Execute() {
    iexecute_log_u8("Execute: entry kind=" + std::to_string(static_cast<int>(m_p.kind)) + " selection="
                    + (m_spia ? "set" : "null"));
    if (!m_spia && m_directory.empty()) {
        iexecute_log_u8("Execute: E_FAIL (IObjectWithSelection / IShellItemArray not set by shell, SetDirectory empty)");
        return E_FAIL;
    }
    const std::wstring exep = get_pm_image_exe();
    iexecute_log_u8("Execute: exepU8=" + w_to_u8(exep) + " exists=" + (PathFileExistsW(exep.c_str()) ? "yes" : "no"));
    if (exep.empty() || !PathFileExistsW(exep.c_str())) {
        iexecute_log_u8("Execute: E_FAIL (pm-image.exe not found next to pm-image-execute.dll — check dist install)");
        return E_FAIL;
    }

    std::vector<std::wstring> files;
    const bool browse_verb = (m_p.kind == pm::iexecute::Kind::OpenUi || m_p.kind == pm::iexecute::Kind::Chat
                              || m_p.kind == pm::iexecute::Kind::Viewer || m_p.kind == pm::iexecute::Kind::Custom);
    if (!m_spia && !m_directory.empty()) {
        iexecute_log_u8("Execute: using SetDirectory fallback pathU8=" + w_to_u8(m_directory));
        files.push_back(m_directory);
    } else if (m_p.kind == pm::iexecute::Kind::Viewer) {
        iexecute_log_u8("Execute: collecting paths for PM Viewer (dirs + viewer-previewable files)");
        paths_from_ishell_viewer(m_spia, &files);
    } else if (m_p.kind == pm::iexecute::Kind::Custom) {
        iexecute_log_u8("Execute: collecting raw paths for Custom (files/folders as selected, no recursive shell enum)");
        paths_from_ishell_raw(m_spia, &files);
    } else if (m_p.kind == pm::iexecute::Kind::OpenUi || m_p.kind == pm::iexecute::Kind::Chat) {
        iexecute_log_u8("Execute: collecting paths for OpenUi/Chat (folder = one path, no recursive shell enum)");
        paths_from_ishell_browse(m_spia, &files);
    } else
        paths_from_ishell(m_spia, &files);
    iexecute_log_u8("Execute: paths count=" + std::to_string(files.size())
                    + (browse_verb ? " (browse_verb)" : " (batch expanded)"));
    if (files.empty()) {
        iexecute_log_u8("Execute: S_OK with 0 files (no usable paths; selection not a file/folder or unsupported type — early exit)");
        return S_OK;
    }
    iexecute_log_u8("Execute: first pathU8=" + w_to_u8(files[0]));
    if (files.size() > 1u)
        iexecute_log_u8("Execute: last pathU8=" + w_to_u8(files.back()));

    switch (m_p.kind) {
    case pm::iexecute::Kind::Resize: {
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd += L" resize --max-width ";
        cmd += std::to_wstring(m_p.width);
        cmd += L" --fit inside --no-cache --job-ui --dst ";
        if (m_p.inplace) {
            cmd += L"\"${SRC_DIR}/${SRC_NAME}${SRC_FILE_EXT}\"";
        } else {
            cmd += L"\"${SRC_DIR}/${SRC_NAME}_";
            cmd += std::to_wstring(m_p.width);
            cmd += L"${SRC_FILE_EXT}\"";
        }
        for (const auto& f : files) {
            cmd += L" --src ";
            append_quoted(&cmd, f);
        }
        const bool ok = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: resize done hr=") + (ok ? "S_OK" : "E_FAIL"));
        return ok ? S_OK : E_FAIL;
    }
    case pm::iexecute::Kind::ConvertJpg: {
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd += L" resize";
        for (const auto& f : files) {
            cmd += L" --src ";
            append_quoted(&cmd, f);
        }
        cmd += L" --dst \"${SRC_DIR}/${SRC_NAME}_converted.jpg\" --format jpg -q 88 --fit inside --no-cache --job-ui";
        const bool ok2 = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: convertJpg done hr=") + (ok2 ? "S_OK" : "E_FAIL"));
        return ok2 ? S_OK : E_FAIL;
    }
    case pm::iexecute::Kind::OpenUi: {
        std::wstring join;
        for (size_t i = 0; i < files.size(); i++) {
            if (i) join += L';';
            join += files[i];
        }
        const std::wstring cwd = cwd_from_first_path(files);
        iexecute_log_u8("Execute: openUi --ui-preset=main --src joinLenWchars=" + std::to_string(join.size()) + " (8191=typical CreateProcess cap)");
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd += L" --cwd ";
        append_quoted(&cmd, cwd);
        cmd += L" --ui-preset=main --src ";
        append_quoted(&cmd, join);
        if (cmd.size() > 8000u)
            iexecute_log_u8("Execute: openUi warning cmdWchars=" + std::to_string(cmd.size()) + " (may fail CreateProcess)");
        const bool ok3 = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: openUi done hr=") + (ok3 ? "S_OK" : "E_FAIL"));
        return ok3 ? S_OK : E_FAIL;
    }
    case pm::iexecute::Kind::Chat: {
        // Main window on chat workbench; no subcommand + --ui-preset + top-level --src.
        // register-explorer DelegateExecute path.
        std::wstring join;
        for (size_t i = 0; i < files.size(); i++) {
            if (i) join += L';';
            join += files[i];
        }
        const std::wstring cwd = cwd_from_first_path(files);
        iexecute_log_u8("Execute: chat --ui-preset=chat --src joinLenWchars=" + std::to_string(join.size()));
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd += L" --cwd ";
        append_quoted(&cmd, cwd);
        cmd += L" --ui-preset=chat --src ";
        append_quoted(&cmd, join);
        if (cmd.size() > 8000u)
            iexecute_log_u8("Execute: chat warning cmdWchars=" + std::to_string(cmd.size()) + " (may fail CreateProcess)");
        const bool ok4 = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: chat done hr=") + (ok4 ? "S_OK" : "E_FAIL"));
        return ok4 ? S_OK : E_FAIL;
    }
    case pm::iexecute::Kind::Viewer: {
        std::wstring join;
        for (size_t i = 0; i < files.size(); i++) {
            if (i) join += L';';
            join += files[i];
        }
        const std::wstring cwd = cwd_from_first_path(files);
        iexecute_log_u8("Execute: viewer --ui-preset=viewer --src joinLenWchars=" + std::to_string(join.size()));
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd += L" --cwd ";
        append_quoted(&cmd, cwd);
        cmd += L" --ui-preset=viewer --src ";
        append_quoted(&cmd, join);
        if (cmd.size() > 8000u)
            iexecute_log_u8("Execute: viewer warning cmdWchars=" + std::to_string(cmd.size()) + " (may fail CreateProcess)");
        const bool okv = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: viewer done hr=") + (okv ? "S_OK" : "E_FAIL"));
        return okv ? S_OK : E_FAIL;
    }
    case pm::iexecute::Kind::Meta: {
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd += L" meta";
        for (const auto& f : files) {
            cmd += L" ";
            append_quoted(&cmd, f);
        }
        cmd += L" --resize-width 512 --job-ui";
        const bool ok5 = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: meta done hr=") + (ok5 ? "S_OK" : "E_FAIL"));
        return ok5 ? S_OK : E_FAIL;
    }
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    case pm::iexecute::Kind::Share: {
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd += L" service posts create --job-ui";
        for (const auto& f : files) {
            cmd += L" ";
            append_quoted(&cmd, f);
        }
        const bool ok7 = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: share (service posts create --job-ui) done hr=") + (ok7 ? "S_OK" : "E_FAIL"));
        return ok7 ? S_OK : E_FAIL;
    }
#endif
    case pm::iexecute::Kind::Transform: {
        if (m_p.preset_id.empty()) {
            iexecute_log_u8("Execute: transform E_FAIL (empty preset_id)");
            return E_FAIL;
        }
        iexecute_log_u8("Execute: transform preset_idU8=" + w_to_u8(m_p.preset_id));
        iexecute_log_u8("Execute: transform input_file_count=" + std::to_string(files.size()));
        iexecute_log_u8(
            "Execute: transform note — this DLL does not select AI provider/model; pm-image.exe reads encrypted "
            "%APPDATA%\\…\\" + w_to_u8(std::wstring(pm::brand::k_config_subpath_w))
            + "\\settings.json (chat.image_provider, chat.image_model, providers.*) and writes [pm-image] lines to "
              "this same log for correlation.");
        const std::wstring cwd = cwd_from_first_path(files);
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd += L" --cwd ";
        append_quoted(&cmd, cwd);
        cmd += L" transform";
        for (const auto& f : files) {
            cmd += L" --src ";
            append_quoted(&cmd, f);
        }
        cmd += L" --preset-id ";
        append_quoted(&cmd, m_p.preset_id);
        cmd += L" --job-ui";
        {
            std::wstring prev = cmd.size() > 8000u ? cmd.substr(0, 8000u) + L"…" : cmd;
            iexecute_log_u8("Execute: transform CreateProcess cmdlineU8=" + w_to_u8(prev));
        }
        const bool ok6 = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: transform CreateProcess ") + (ok6 ? "ok" : "FAILED"));
        return ok6 ? S_OK : E_FAIL;
    }
    case pm::iexecute::Kind::Custom: {
        if (m_p.preset_id.empty()) {
            iexecute_log_u8("Execute: custom E_FAIL (empty command id)");
            return E_FAIL;
        }
        iexecute_log_u8("Execute: custom idU8=" + w_to_u8(m_p.preset_id)
                        + " input_count=" + std::to_string(files.size())
                        + " (dispatcher infers --cwd from first --src when needed)");
        std::wstring cmd;
        append_quoted(&cmd, exep);
        cmd.push_back(L' ');
        append_quoted(&cmd, m_p.preset_id);
        for (const auto& f : files) {
            cmd += L" --src ";
            append_quoted(&cmd, f);
        }
        {
            std::wstring prev = cmd.size() > 8000u ? cmd.substr(0, 8000u) + L"…" : cmd;
            iexecute_log_u8("Execute: custom CreateProcess cmdlineU8=" + w_to_u8(prev));
        }
        const bool ok_custom = spawn_cmdline(cmd);
        iexecute_log_u8(std::string("Execute: custom CreateProcess ") + (ok_custom ? "ok" : "FAILED"));
        return ok_custom ? S_OK : E_FAIL;
    }
    default: {
        iexecute_log_u8("Execute: E_NOTIMPL (unknown kind)");
        return E_NOTIMPL;
    }
    }
}

static std::string clsid_u8(REFCLSID id) {
    wchar_t w[64]{};
    return StringFromGUID2(id, w, 64) ? w_to_u8(w) : "StringFromGUID2 failed";
}

static std::string hresult_u8(HRESULT hr) {
    char b[20]{};
    sprintf_s(b, "0x%08lX", (unsigned long)hr);
    return b;
}

IFACEMETHODIMP ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (pUnkOuter) return CLASS_E_NOAGGREGATION;
    ComPtr<PmExecuteHandler> h;
    const HRESULT hr = MakeAndInitialize<PmExecuteHandler>(&h, m_pl);
    if (FAILED(hr)) {
        iexecute_log_u8("ClassFactory::CreateInstance MakeAndInitialize failed " + hresult_u8(hr));
        return hr;
    }
    const HRESULT hr2 = h->QueryInterface(riid, ppv);
    if (FAILED(hr2)) iexecute_log_u8("ClassFactory::CreateInstance PmExecuteHandler->QI failed " + hresult_u8(hr2));
    return hr2;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    {
        static bool s_once = false;
        if (!s_once) {
            s_once     = true;
            wchar_t host[MAX_PATH]{};
            GetModuleFileNameW(nullptr, host, MAX_PATH);
            iexecute_log_u8("IExecute: first DllGetClassObject in this process (hostU8=" + w_to_u8(host) + ")");
        }
    }
    iexecute_log_u8("DllGetClassObject rclsidU8=" + clsid_u8(rclsid));
    pm::iexecute::Payload pay{};
    if (!pm::iexecute::load_payload(rclsid, &pay)) {
        iexecute_log_u8(
            "DllGetClassObject load_payload failed for HKCU IExecuteMap\\" + clsid_u8(rclsid) + " — re-run: pm-image register-explorer");
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    iexecute_log_u8("DllGetClassObject map ok kind=" + std::to_string(static_cast<int>(pay.kind))
                    + (pay.preset_id.empty() ? std::string{} : (" payloadU8=" + w_to_u8(pay.preset_id))));
    IClassFactory* f = new ClassFactory(pay);
    const HRESULT  hr = f->QueryInterface(riid, ppv);
    f->Release();
    if (FAILED(hr))
        iexecute_log_u8("DllGetClassObject ClassFactory->QueryInterface " + hresult_u8(hr));
    return hr;
}

STDAPI DllCanUnloadNow() { return g_cLock == 0 ? S_OK : S_FALSE; }

/** All Inproc+map are written by pm-image register-explorer. */
STDAPI DllRegisterServer() { return S_OK; }
STDAPI DllUnregisterServer() { return S_OK; }

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD r, void*) {
    if (r == DLL_PROCESS_ATTACH) {
        g_hThis = h;
        DisableThreadLibraryCalls(h);
        static bool s_once = false;
        if (!s_once) {
            s_once = true;
            iexecute_log_u8("DllMain: pm-image-execute loaded (verify IExecute path — Win11 sparse menu uses explorer11 DLL, no iexecute log)");
        }
    }
    return TRUE;
}

#endif
