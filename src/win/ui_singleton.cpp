#include "win/ui_singleton.hpp"

#include <string>
#include <vector>

#pragma comment(lib, "User32.lib")

namespace media::win {
namespace {

HWND g_merge_input_edit{};
HWND g_bridge_hwnd{};
HANDLE g_mutex{};

// App-command channel (dwData == 2). When non-null, the bridge forwards
// UTF-8 command payloads to this HWND via PostMessage(g_command_msg, ...).
HWND g_command_target_hwnd{};
UINT g_command_msg{};

static const wchar_t kBridgeClass[] = L"MediaImgUiBridge";
static const wchar_t kBridgeTitle[] = L"media-img";

std::wstring utf8_to_wide(const std::string &s) {
    if (s.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

LRESULT CALLBACK bridge_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)wp;
    if (msg == WM_COPYDATA) {
        auto *cds = reinterpret_cast<COPYDATASTRUCT *>(lp);
        if (!cds || !cds->lpData || cds->cbData == 0)
            return FALSE;

        const char *bytes = static_cast<const char *>(cds->lpData);
        size_t len = cds->cbData;
        while (len > 0 && bytes[len - 1] == '\0')
            --len;
        if (len == 0)
            return TRUE;
        std::string payload(bytes, bytes + len);

        // ── dwData == 1 : legacy resize-UI path-merge channel ────────────
        if (cds->dwData == 1) {
            if (g_merge_input_edit && IsWindow(g_merge_input_edit)) {
                const int n = GetWindowTextLengthW(g_merge_input_edit);
                std::wstring cur;
                if (n > 0) {
                    cur.assign(static_cast<size_t>(n) + 1, L'\0');
                    GetWindowTextW(g_merge_input_edit, cur.data(), n + 1);
                    cur.resize(static_cast<size_t>(n));
                }
                std::wstring add = utf8_to_wide(payload);
                if (!cur.empty() && !add.empty())
                    cur += L';';
                cur += add;
                SetWindowTextW(g_merge_input_edit, cur.c_str());
            }
            return TRUE;
        }

        // ── dwData == 2 : app-command channel (e.g. takescreenshot) ──────
        if (cds->dwData == 2) {
            if (g_command_target_hwnd && g_command_msg &&
                IsWindow(g_command_target_hwnd))
            {
                // Heap-allocate so we can survive the WM_COPYDATA stack
                // unwind; the receiver owns the pointer and must delete it.
                auto *heap = new std::string(std::move(payload));
                if (!PostMessageW(g_command_target_hwnd, g_command_msg,
                                  reinterpret_cast<WPARAM>(heap), 0))
                {
                    delete heap;
                }
            }
            return TRUE;
        }

        return FALSE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool register_bridge_class_once() {
    static bool tried = false;
    if (tried)
        return true;
    tried = true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = bridge_wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kBridgeClass;
    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }
    return true;
}

} // namespace

bool try_acquire_ui_singleton_mutex() {
    g_mutex = CreateMutexW(nullptr, TRUE, L"Local\\MediaImgResizeUi_v1");
    if (!g_mutex)
        return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mutex);
        g_mutex = nullptr;
        return false;
    }
    return true;
}

void release_ui_singleton_mutex() {
    if (!g_mutex)
        return;
    ReleaseMutex(g_mutex);
    CloseHandle(g_mutex);
    g_mutex = nullptr;
}

bool create_ui_singleton_bridge() {
    if (g_bridge_hwnd)
        return true;
    if (!register_bridge_class_once())
        return false;
#ifndef HWND_MESSAGE
#define HWND_MESSAGE ((HWND)(-3))
#endif
    g_bridge_hwnd =
        CreateWindowExW(0, kBridgeClass, kBridgeTitle, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                        nullptr);
    return g_bridge_hwnd != nullptr;
}

void destroy_ui_singleton_bridge() {
    if (g_bridge_hwnd) {
        DestroyWindow(g_bridge_hwnd);
        g_bridge_hwnd = nullptr;
    }
}

void set_ui_merge_input_edit(HWND h) {
    g_merge_input_edit = h;
}

void clear_ui_merge_input_edit() {
    g_merge_input_edit = nullptr;
}

bool forward_resize_ui_paths_to_primary(const std::string &utf8_paths) {
    HWND bridge = nullptr;
    for (int i = 0; i < 80 && !bridge; ++i) {
        bridge = FindWindowW(kBridgeClass, kBridgeTitle);
        if (!bridge)
            Sleep(25);
    }
    if (!bridge)
        return false;

    std::vector<char> buf;
    buf.reserve(utf8_paths.size() + 1);
    buf.assign(utf8_paths.begin(), utf8_paths.end());
    buf.push_back('\0');

    COPYDATASTRUCT cds{};
    cds.dwData = 1;
    cds.cbData = static_cast<DWORD>(buf.size());
    cds.lpData = buf.data();

    DWORD_PTR result = 0;
    const LRESULT r =
        SendMessageTimeoutW(bridge, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 15000, &result);
    return r != 0;
}

void set_ui_command_target(HWND target, UINT message_id) {
    g_command_target_hwnd = target;
    g_command_msg         = message_id;
}

void clear_ui_command_target() {
    g_command_target_hwnd = nullptr;
    g_command_msg         = 0;
}

bool forward_app_command_to_primary(const std::string &utf8_command) {
    HWND bridge = nullptr;
    for (int i = 0; i < 80 && !bridge; ++i) {
        bridge = FindWindowW(kBridgeClass, kBridgeTitle);
        if (!bridge)
            Sleep(25);
    }
    if (!bridge)
        return false;

    std::vector<char> buf;
    buf.reserve(utf8_command.size() + 1);
    buf.assign(utf8_command.begin(), utf8_command.end());
    buf.push_back('\0');

    COPYDATASTRUCT cds{};
    cds.dwData = 2;
    cds.cbData = static_cast<DWORD>(buf.size());
    cds.lpData = buf.data();

    DWORD_PTR result = 0;
    const LRESULT r =
        SendMessageTimeoutW(bridge, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 5000, &result);
    return r != 0;
}

} // namespace media::win
