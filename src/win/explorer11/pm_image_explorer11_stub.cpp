/**
 * Small Win11 packaged-COM forwarding stub, matching the TortoiseGit shape:
 * the AppxManifest points at this DLL, and this DLL forwards COM activation to
 * the real implementation DLL next to it.
 */
#if defined(_WIN32)

    #include "constants.hpp"

    #include <cstdio>
    #include <string>

    #include <shlobj.h>
    #include <shlwapi.h>
    #include <windows.h>

    #pragma comment(lib, "Shlwapi.lib")

using FnDllGetClassObject = HRESULT (STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
using FnDllCanUnloadNow = HRESULT (STDAPICALLTYPE*)();

static HINSTANCE g_hThis = nullptr;
static HMODULE g_realDll = nullptr;
static FnDllGetClassObject g_realGetClassObject = nullptr;
static FnDllCanUnloadNow g_realCanUnloadNow = nullptr;

static void log_stub(const wchar_t* msg, DWORD code = 0) {
    wchar_t appdata[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (!len || len >= MAX_PATH)
        return;

    std::wstring dir = appdata;
    dir += L"\\";
    dir += pm::brand::k_config_subpath_w;
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);

    std::wstring path = dir + L"\\pm-image-explorer11-stub.log";
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"a, ccs=UTF-8") != 0 || !file)
        return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    fwprintf(file,
             L"%04u-%02u-%02u %02u:%02u:%02u  %ls code=%lu\n",
             st.wYear,
             st.wMonth,
             st.wDay,
             st.wHour,
             st.wMinute,
             st.wSecond,
             msg ? msg : L"",
             code);
    fclose(file);
}

static bool load_real_library() {
    if (g_realDll && g_realGetClassObject)
        return true;
    if (!g_hThis) {
        log_stub(L"load_real_library: missing module handle");
        return false;
    }

    wchar_t module[MAX_PATH]{};
    if (!GetModuleFileNameW(g_hThis, module, MAX_PATH)) {
        log_stub(L"load_real_library: GetModuleFileNameW failed", GetLastError());
        return false;
    }
    if (!PathRemoveFileSpecW(module)) {
        log_stub(L"load_real_library: PathRemoveFileSpecW failed", GetLastError());
        return false;
    }

    std::wstring realPath = module;
    realPath += L"\\";
    realPath += pm::brand::k_explorer11_dll_basename_w;

    g_realDll = LoadLibraryExW(realPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_realDll) {
        log_stub(L"load_real_library: LoadLibraryExW failed", GetLastError());
        return false;
    }

    g_realGetClassObject = reinterpret_cast<FnDllGetClassObject>(GetProcAddress(g_realDll, "DllGetClassObject"));
    g_realCanUnloadNow = reinterpret_cast<FnDllCanUnloadNow>(GetProcAddress(g_realDll, "DllCanUnloadNow"));
    if (!g_realGetClassObject || !g_realCanUnloadNow) {
        log_stub(L"load_real_library: GetProcAddress failed", GetLastError());
        FreeLibrary(g_realDll);
        g_realDll = nullptr;
        g_realGetClassObject = nullptr;
        g_realCanUnloadNow = nullptr;
        return false;
    }

    log_stub(L"load_real_library: loaded real explorer11 DLL");
    return true;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (!load_real_library()) {
        if (ppv) *ppv = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    log_stub(L"DllGetClassObject: forwarding");
    return g_realGetClassObject(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow() {
    if (g_realCanUnloadNow) {
        const HRESULT hr = g_realCanUnloadNow();
        if (hr != S_OK)
            return hr;
    }
    if (g_realDll) {
        log_stub(L"DllCanUnloadNow: unloading real explorer11 DLL");
        FreeLibrary(g_realDll);
        g_realDll = nullptr;
        g_realGetClassObject = nullptr;
        g_realCanUnloadNow = nullptr;
    }
    return S_OK;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hThis = h;
        DisableThreadLibraryCalls(h);
        log_stub(L"DllMain: stub loaded");
    }
    return TRUE;
}

#endif
