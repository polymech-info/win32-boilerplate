#include "win/pixlwiz_login_spawn.hpp"

#include <windows.h>

#include "constants.hpp"
#include "win/ui_next/Resource.h"
#include "win/ui_next/helpers/text_conv.hpp"

#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#ifndef CREATE_NO_WINDOW
#define PM_IMAGE_CREATE_NO_WINDOW 0x08000000u
#else
#define PM_IMAGE_CREATE_NO_WINDOW CREATE_NO_WINDOW
#endif

namespace fs = std::filesystem;

namespace pmui {
namespace {

/** True when @p name is set to a non-empty value (after trimming ASCII-ish whitespace). */
bool win_env_nonempty_w(const wchar_t* name)
{
    wchar_t     buf[4096];
    const DWORD cap = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    DWORD       n   = ::GetEnvironmentVariableW(name, buf, cap);
    if (n == 0)
        return false;
    if (n >= cap)
        return true;
    size_t lo = 0;
    while (lo < n && (buf[lo] == L' ' || buf[lo] == L'\t'))
        ++lo;
    size_t hi = n;
    while (hi > lo && (buf[hi - 1] == L' ' || buf[hi - 1] == L'\t' || buf[hi - 1] == L'\r' || buf[hi - 1] == L'\n'))
        --hi;
    return lo < hi;
}

bool set_handle_inherit(HANDLE h, bool inherit)
{
    return ::SetHandleInformation(h, HANDLE_FLAG_INHERIT, inherit ? HANDLE_FLAG_INHERIT : 0) != FALSE;
}

/** Run on worker thread; posts @c UWM_PIXLWIZ_LOGIN_DONE with heap @ref PixlwizLoginDonePayload (or nullptr on OOM). */
void pixlwiz_login_worker(HWND frame_hwnd, std::wstring exe_path, std::wstring work_dir)
{
    auto* payload = new PixlwizLoginDonePayload{};

    wchar_t temp_dir[MAX_PATH]{};
    if (::GetTempPathW(MAX_PATH, temp_dir) == 0) {
        payload->spawn_error = L"GetTempPathW failed.";
        ::PostMessageW(frame_hwnd, UWM_PIXLWIZ_LOGIN_DONE, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    wchar_t log_path[MAX_PATH]{};
    if (::GetTempFileNameW(temp_dir, L"pml", 0, log_path) == 0) {
        payload->win32_error   = ::GetLastError();
        payload->spawn_error   = L"GetTempFileNameW failed.";
        ::PostMessageW(frame_hwnd, UWM_PIXLWIZ_LOGIN_DONE, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle       = TRUE;

    HANDLE h_log_write = ::CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, &sa,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h_log_write == INVALID_HANDLE_VALUE) {
        payload->win32_error   = ::GetLastError();
        payload->spawn_error   = L"CreateFileW (login log) failed.";
        ::DeleteFileW(log_path);
        ::PostMessageW(frame_hwnd, UWM_PIXLWIZ_LOGIN_DONE, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }
    (void)set_handle_inherit(h_log_write, true);

    HANDLE h_nul_in = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h_nul_in == INVALID_HANDLE_VALUE) {
        payload->win32_error   = ::GetLastError();
        payload->spawn_error   = L"CreateFileW (NUL stdin) failed.";
        ::CloseHandle(h_log_write);
        ::DeleteFileW(log_path);
        ::PostMessageW(frame_hwnd, UWM_PIXLWIZ_LOGIN_DONE, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }
    (void)set_handle_inherit(h_nul_in, true);

    std::wstring cmdline = L"\"";
    cmdline += exe_path;
    cmdline += L"\" --no-mcp login";
    // Same defaults as `pm_image_cmd_login` + `constants.hpp`: pass explicitly so menu login works even when
    // cwd has no `.env` and (defensively) if an older login object file omitted the in-process fallback.
    const bool have_issuer_env =
        win_env_nonempty_w(L"ZITADEL_ISSUER") || win_env_nonempty_w(L"VITE_ZITADEL_AUTHORITY");
    const bool have_client_env =
        win_env_nonempty_w(L"ZITADEL_OIDC_CLIENT_ID") || win_env_nonempty_w(L"VITE_ZITADEL_CLIENT_ID");
    if (!have_issuer_env && pm::k_pixlwiz_zitadel_authority[0] != '\0') {
        cmdline += L" --issuer=";
        cmdline += utf8_to_wide(std::string(pm::k_pixlwiz_zitadel_authority));
    }
    if (!have_client_env && pm::k_pixlwiz_zitadel_client_id[0] != '\0') {
        cmdline += L" --client-id=";
        cmdline += utf8_to_wide(std::string(pm::k_pixlwiz_zitadel_client_id));
    }
    std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb            = sizeof(si);
    si.dwFlags       = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow   = SW_HIDE;
    si.hStdInput     = h_nul_in;
    si.hStdOutput    = h_log_write;
    si.hStdError     = h_log_write;

    PROCESS_INFORMATION pi{};
    const BOOL ok =
        ::CreateProcessW(exe_path.c_str(), cmd_buf.data(), nullptr, nullptr, TRUE, PM_IMAGE_CREATE_NO_WINDOW, nullptr,
                         work_dir.empty() ? nullptr : work_dir.c_str(), &si, &pi);
    ::CloseHandle(h_nul_in);
    ::CloseHandle(h_log_write);

    if (!ok) {
        payload->win32_error = ::GetLastError();
        payload->spawn_error = L"CreateProcessW failed.";
        ::DeleteFileW(log_path);
        ::PostMessageW(frame_hwnd, UWM_PIXLWIZ_LOGIN_DONE, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    ::CloseHandle(pi.hThread);
    (void)::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    (void)::GetExitCodeProcess(pi.hProcess, &exit_code);
    ::CloseHandle(pi.hProcess);

    std::string raw_utf8;
    try {
        std::error_code ec;
        std::ifstream   ifs(fs::path(log_path), std::ios::binary);
        if (ifs)
            raw_utf8.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    } catch (...) {
    }
    ::DeleteFileW(log_path);

    payload->create_process_ok = true;
    payload->exit_code         = exit_code;
    payload->stdio_log_utf16   = pmui::utf8_to_wide(raw_utf8);
    ::PostMessageW(frame_hwnd, UWM_PIXLWIZ_LOGIN_DONE, 0, reinterpret_cast<LPARAM>(payload));
}

} // namespace

void StartPixlwizLoginAsync(HWND main_frame_hwnd)
{
    if (!main_frame_hwnd)
        return;

    wchar_t path[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        auto* payload            = new PixlwizLoginDonePayload{};
        payload->spawn_error       = L"GetModuleFileNameW failed.";
        ::PostMessageW(main_frame_hwnd, UWM_PIXLWIZ_LOGIN_DONE, 0, reinterpret_cast<LPARAM>(payload));
        return;
    }

    std::wstring exe(path);
    std::wstring work_dir = exe;
    const size_t slash    = work_dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        work_dir.resize(slash);
    else
        work_dir.clear();

    std::thread([frame_hwnd = main_frame_hwnd, exe_path = std::move(exe), cwd = std::move(work_dir)]() mutable {
        pixlwiz_login_worker(frame_hwnd, std::move(exe_path), std::move(cwd));
    }).detach();
}

} // namespace pmui
