#include "win/session_replay/session_input_hooks.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <vector>

#include <nlohmann/json.hpp>

namespace media::win::session_replay::input {

using json = nlohmann::json;

std::mutex              g_mutex;
std::vector<json>        g_events;
HHOOK                   g_hMouse = nullptr;
HHOOK                   g_hKbd   = nullptr;
HWND                    g_main   = nullptr;
LARGE_INTEGER           g_t0{};

// Move sampling: pixels before another `mouse.move` is logged; tighter during drag
constexpr int kMoveThresholdIdle = 8;
constexpr int kMoveThresholdDrag = 2;

int  g_last_mx = 0, g_last_my = 0;
bool g_have_last_move = false;

int64_t perf_ms()
{
    LARGE_INTEGER f, c;
    (void)QueryPerformanceFrequency(&f);
    (void)QueryPerformanceCounter(&c);
    if (f.QuadPart == 0)
        return 0;
    return (c.QuadPart - g_t0.QuadPart) * 1000 / f.QuadPart;
}

std::string utf8_class(HWND h)
{
    if (!h || !IsWindow(h))
        return {};
    std::array<wchar_t, 80> w{};
    const int n = GetClassNameW(h, w.data(), static_cast<int>(w.size()));
    if (n <= 0)
        return {};
    w[static_cast<size_t>(n > 0 ? n : 0)] = L'\0';
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};
    std::string s(static_cast<size_t>(bytes), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, w.data(), n, s.data(), bytes, nullptr, nullptr);
    return s;
}

static bool in_pm_image(HWND h)
{
    if (!g_main)
        return false;
    if (!h)
        return false;
    if (h == g_main)
        return true;
    return IsChild(g_main, h) != FALSE;
}

static const char* mouse_name(WPARAM wp)
{
    switch (wp) {
    case WM_LBUTTONDOWN:  return "l_down";
    case WM_LBUTTONUP:    return "l_up";
    case WM_LBUTTONDBLCLK:return "l_dbl";
    case WM_RBUTTONDOWN:  return "r_down";
    case WM_RBUTTONUP:    return "r_up";
    case WM_MBUTTONDOWN:  return "m_down";
    case WM_MBUTTONUP:    return "m_up";
    case WM_MOUSEMOVE:    return "move";
    case WM_MOUSEWHEEL:   return "wheel";
    case WM_MOUSEHWHEEL:  return "hwheel";
    case WM_XBUTTONDOWN:  return "x_down";
    case WM_XBUTTONUP:    return "x_up";
    default:              return "other";
    }
}

static const char* key_name(WPARAM wp)
{
    switch (wp) {
    case WM_KEYDOWN:     return "key_down";
    case WM_KEYUP:       return "key_up";
    case WM_SYSKEYDOWN:  return "sys_down";
    case WM_SYSKEYUP:    return "sys_up";
    default:             return "key_other";
    }
}

LRESULT CALLBACK MouseLLProc(int nCode, WPARAM wparam, LPARAM lparam)
{
    if (nCode < HC_ACTION) {
        return CallNextHookEx(g_hMouse, nCode, wparam, lparam);
    }
    const auto* m   = reinterpret_cast<MSLLHOOKSTRUCT*>(lparam);
    const int   x   = m->pt.x;
    const int   y   = m->pt.y;
    const HWND  hit = ::WindowFromPoint(m->pt);
    // Drag / idle threshold for high-frequency moves
    const bool  drag = ((GetKeyState(VK_LBUTTON) & 0x8000) != 0) || ((GetKeyState(VK_RBUTTON) & 0x8000) != 0) ||
                      ((GetKeyState(VK_MBUTTON) & 0x8000) != 0) || ((GetKeyState(VK_XBUTTON1) & 0x8000) != 0) ||
                      ((GetKeyState(VK_XBUTTON2) & 0x8000) != 0);

    if (wparam == WM_MOUSEMOVE) {
        const int th = drag ? kMoveThresholdDrag : kMoveThresholdIdle;
        if (g_have_last_move) {
            if (std::abs(x - g_last_mx) < th && std::abs(y - g_last_my) < th)
                return CallNextHookEx(g_hMouse, nCode, wparam, lparam);
        }
        g_last_mx        = x;
        g_last_my        = y;
        g_have_last_move = true;
    } else {
        // Clicks, wheels: always log; nudge “last” so the next move isn’t
        // spuriously suppressed.
        g_last_mx        = x;
        g_last_my        = y;
        g_have_last_move = true;
    }

    json e;
    e["t_ms"]  = perf_ms();
    e["type"]  = "mouse";
    e["m"]     = mouse_name(wparam);
    e["x"]     = x;
    e["y"]     = y;
    e["hit"]   = reinterpret_cast<std::int64_t>(hit);
    e["in"]    = in_pm_image(hit);
    e["cls"]   = utf8_class(hit);
    e["fg"]    = reinterpret_cast<std::int64_t>(::GetForegroundWindow());
    e["in_fg"] = in_pm_image(::GetForegroundWindow());
    e["drv"]   = drag && (wparam == WM_MOUSEMOVE);

    if (wparam == WM_MOUSEWHEEL || wparam == WM_MOUSEHWHEEL) {
        e["wh"]   = (SHORT)HIWORD(m->mouseData);
    }
    if (wparam == WM_XBUTTONDOWN || wparam == WM_XBUTTONUP) {
        const int xb = (int)HIWORD(m->mouseData);
        e["xb"]      = (xb == XBUTTON1) ? 1 : 2;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_events.push_back(std::move(e));
    }
    return CallNextHookEx(g_hMouse, nCode, wparam, lparam);
}

LRESULT CALLBACK KbdLLProc(int nCode, WPARAM wparam, LPARAM lparam)
{
    if (nCode < HC_ACTION) {
        return CallNextHookEx(g_hKbd, nCode, wparam, lparam);
    }
    const KBDLLHOOKSTRUCT* k   = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
    const HWND            fg  = ::GetForegroundWindow();
    // Skip purely injected (often automation); still record user-level injection if needed later.
    if (k->flags & LLKHF_INJECTED) {
        return CallNextHookEx(g_hKbd, nCode, wparam, lparam);
    }

    json e;
    e["t_ms"]  = perf_ms();
    e["type"]  = "key";
    e["k"]     = key_name(wparam);
    e["vk"]    = (int)k->vkCode;
    e["sc"]    = (int)k->scanCode;
    e["e"]     = (int)k->flags; // LLKHF_*; bit 0=extended, etc.
    e["fg"]    = reinterpret_cast<std::int64_t>(fg);
    e["in"]    = in_pm_image(fg);
    e["cls"]   = utf8_class(fg);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_events.push_back(std::move(e));
    }
    return CallNextHookEx(g_hKbd, nCode, wparam, lparam);
}

bool start_input_recording(HWND main_frame_hwnd, std::string& err)
{
    if (g_hMouse != nullptr || g_hKbd != nullptr) {
        err = "input hooks already active";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_events.clear();
        g_have_last_move = false;
    }
    (void)QueryPerformanceCounter(&g_t0);
    g_main = main_frame_hwnd;

    // Do not hold g_mutex across SetWindowsHookEx: a hook may run on this thread
    // and also take g_mutex (vector push) — deadlock if we still hold the lock.
    g_hMouse = SetWindowsHookExW(WH_MOUSE_LL, MouseLLProc, GetModuleHandleW(nullptr), 0);
    if (!g_hMouse) {
        err = "SetWindowsHookEx(MOUSE_LL) failed: " + std::to_string((unsigned)GetLastError());
        g_main = nullptr;
        return false;
    }
    g_hKbd = SetWindowsHookExW(WH_KEYBOARD_LL, KbdLLProc, GetModuleHandleW(nullptr), 0);
    if (!g_hKbd) {
        err = "SetWindowsHookEx(KEYBOARD_LL) failed: " + std::to_string((unsigned)GetLastError());
        (void)UnhookWindowsHookEx(g_hMouse);
        g_hMouse = nullptr;
        g_main   = nullptr;
        return false;
    }
    return true;
}

void stop_input_recording(json& out_events)
{
    if (g_hKbd) {
        (void)UnhookWindowsHookEx(g_hKbd);
        g_hKbd = nullptr;
    }
    if (g_hMouse) {
        (void)UnhookWindowsHookEx(g_hMouse);
        g_hMouse = nullptr;
    }
    g_main = nullptr;

    std::lock_guard<std::mutex> lock(g_mutex);
    out_events = json::array();
    for (json& e : g_events) {
        out_events.push_back(std::move(e));
    }
    g_events.clear();
    g_have_last_move = false;
}

bool is_input_recording()
{
    return g_hMouse != nullptr || g_hKbd != nullptr;
}

} // namespace media::win::session_replay::input
