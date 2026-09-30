// FEATURE_SPLASH: layered topmost GDI+ window; optional fade before teardown.
#include "stdafx.h"
#include "helpers/splash_window.hpp"

#include <atomic>

static std::atomic<bool> g_splash_disabled_cli{true};

namespace pmui {

void set_splash_disabled_for_session(bool d) noexcept
{
    g_splash_disabled_cli.store(d, std::memory_order_relaxed);
}

bool splash_disabled_for_session() noexcept
{
    return g_splash_disabled_cli.load(std::memory_order_relaxed);
}

} // namespace pmui

#if defined(FEATURE_SPLASH)
#include "Resource.h"
#include "../ui_log_file.hpp"
#include "win/settings_store.hpp"

#include <gdiplus.h>
#include <string>
#include <objidl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")

namespace pmui {
namespace {

constexpr wchar_t   kClassName[]   = L"PM_StartupSplash";
constexpr UINT_PTR kTimerFade = 9;
// After “fit in 90% of work area”, apply this so the window is not full-screen (880w PNG was ~2× too big).
constexpr double   kSplashScale = 0.5;

ULONG_PTR  g_gdip   = 0;
bool       g_gdipOk = false;

Gdiplus::Image* load_rcdata_png(WORD resId) {
    HMODULE     h  = ::GetModuleHandleW(nullptr);
    HRSRC       hr = ::FindResourceW(h, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hr) {
        (void)::OutputDebugStringA("[splash] FindResourceW failed (PNG not embedded?)\n");
        pmui::ui_log_file_event("splash: FindResourceW failed (splash PNGs not in executable?)");
        return nullptr;
    }
    HGLOBAL hMem = ::LoadResource(h, hr);
    if (!hMem) return nullptr;
    const void* p  = ::LockResource(hMem);
    const DWORD sz = ::SizeofResource(h, hr);
    if (!p || sz == 0) return nullptr;
    HGLOBAL     glob = ::GlobalAlloc(GMEM_MOVEABLE, sz);
    if (!glob) return nullptr;
    void* dst = ::GlobalLock(glob);
    if (!dst) { ::GlobalFree(glob); return nullptr; }
    std::memcpy(dst, p, sz);
    ::GlobalUnlock(glob);
    IStream* st = nullptr;
    if (FAILED(::CreateStreamOnHGlobal(glob, TRUE, &st)) || !st) { ::GlobalFree(glob); return nullptr; }
    // GdiplusStartup must run before any GDI+ type (Image, Graphics, …); do not call FromStream first.
    if (!g_gdipOk) {
        Gdiplus::GdiplusStartupInput si;
        g_gdipOk = (Gdiplus::GdiplusStartup(&g_gdip, &si, nullptr) == Gdiplus::Ok);
        if (!g_gdipOk) {
            st->Release();
            (void)::OutputDebugStringA("[splash] GdiplusStartup failed before Image::FromStream\n");
            pmui::ui_log_file_event("splash: GdiplusStartup failed (before decode)");
            return nullptr;
        }
    }
    Gdiplus::Image* im = Gdiplus::Image::FromStream(st);
    st->Release();
    if (!im || im->GetLastStatus() != Gdiplus::Ok) {
        delete im;
        (void)::OutputDebugStringA("[splash] GDI+ could not decode PNG from resource\n");
        pmui::ui_log_file_event("splash: GDI+ decode failed (bad or non-PNG RCDATA?)");
        return nullptr;
    }
    return im;
}

HWND        g_hwnd   = nullptr;
Gdiplus::Image* g_splash     = nullptr;
Gdiplus::Image* g_logo      = nullptr;
int         g_fadeAlpha     = 255; // set when fade starts; 0 = not fading
int         g_fadePerTick   = 0;
int         g_fadeTicksLeft = 0;

static void paint_splash_client(HDC hdc, int W, int H) {
    if (!g_splash || !hdc)
        return;
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(g_splash, 0, 0, W, H);
    if (g_logo) {
        if (W > 16 && H > 16) {
            int    sm  = (std::min)(W, H);
            int    mxl = sm / 5;
            if (mxl < 32) mxl = 32;
            int    lw  = (int)g_logo->GetWidth();
            int    lh  = (int)g_logo->GetHeight();
            if (lw > 0 && lh > 0) {
                int          base = (std::max)(lw, lh);
                const double s   = (double)mxl / (double)base;
                int          dw  = (int)std::lround(lw * s);
                int          dh  = (int)std::lround(lh * s);
                int          x   = (std::max)(0, W - dw - 16);
                int          y   = (std::max)(0, H - dh - 16);
                g.DrawImage(g_logo, x, y, dw, dh);
            }
        }
    }
}

void teardown_gfx_after_destroy() {
    g_fadePerTick   = 0;
    g_fadeTicksLeft = 0;
    delete g_splash;
    g_splash = nullptr;
    delete g_logo;
    g_logo  = nullptr;
    if (g_gdip) {
        Gdiplus::GdiplusShutdown(g_gdip);
        g_gdip   = 0;
        g_gdipOk = false;
    }
    g_fadeAlpha = 255;
}

LRESULT CALLBACK SplashWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND)
        return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC         hdc = ::BeginPaint(h, &ps);
        RECT        rc{};
        ::GetClientRect(h, &rc);
        const int   W   = rc.right - rc.left;
        const int   H   = rc.bottom - rc.top;
        if (g_splash && hdc) paint_splash_client(hdc, W, H);
        ::EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_TIMER && w == kTimerFade) {
        g_fadeAlpha = (std::max)(0, g_fadeAlpha - g_fadePerTick);
        (void)::SetLayeredWindowAttributes(h, 0, static_cast<BYTE>(g_fadeAlpha & 0xFF), LWA_ALPHA);
        --g_fadeTicksLeft;
        if (g_fadeAlpha <= 0 || g_fadeTicksLeft <= 0) {
            (void)::KillTimer(h, kTimerFade);
            g_fadePerTick   = 0;
            (void)::DestroyWindow(h);
        }
        return 0;
    }
    if (m == WM_DESTROY) {
        g_hwnd = nullptr;
        teardown_gfx_after_destroy();
        return ::DefWindowProcW(h, m, w, l);
    }
    return ::DefWindowProcW(h, m, w, l);
}

void register_once() {
    static bool done = false;
    if (done) return;
    done             = true;
    WNDCLASSW wc{};
    wc.lpfnWndProc   = SplashWndProc;
    wc.hInstance     = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor       = ::LoadCursor(nullptr, IDC_APPSTARTING);
    wc.hbrBackground = nullptr; // we paint; layered window
    (void)::RegisterClassW(&wc);
}

/// Same monitor heuristic as @c CMainFrame::ApplyWindowLayoutData: centre of saved @c normal_rect
/// for the active @c ui.workbench slot (main vs chat, …).
static HMONITOR monitor_for_saved_window_placement()
{
    std::string wbench;
    std::string err;
    // Peek only: splash runs before CMainFrame; load_settings_ui_workbench_id consumes the CLI one-shot.
    if (!media::settings::peek_settings_ui_workbench_id(wbench, err) || wbench.empty())
        wbench = "main";
    media::settings::WindowLayout wl{};
    if (!media::settings::load_window_layout(wl, err, wbench.c_str()))
        return ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!wl.has_placement)
        return ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    // Avoid `(left+right)/2` overflow on pathological persisted rects (INT_MIN/INT_MAX).
    const std::int64_t mid_x
        = (static_cast<std::int64_t>(wl.normal_rect.left) + static_cast<std::int64_t>(wl.normal_rect.right)) / 2;
    const std::int64_t mid_y
        = (static_cast<std::int64_t>(wl.normal_rect.top) + static_cast<std::int64_t>(wl.normal_rect.bottom)) / 2;
    LONG               cx = 0;
    LONG               cy = 0;
    if (mid_x > static_cast<std::int64_t>(INT32_MAX))
        cx = INT32_MAX;
    else if (mid_x < static_cast<std::int64_t>(INT32_MIN))
        cx = INT32_MIN;
    else
        cx = static_cast<LONG>(mid_x);
    if (mid_y > static_cast<std::int64_t>(INT32_MAX))
        cy = INT32_MAX;
    else if (mid_y < static_cast<std::int64_t>(INT32_MIN))
        cy = INT32_MIN;
    else
        cy = static_cast<LONG>(mid_y);
    const POINT centre{cx, cy};
    // Unplugged / RDP monitor: centre may not hit a display — nearest avoids falling back to (0,0) primary guess.
    HMONITOR hMon = ::MonitorFromPoint(centre, MONITOR_DEFAULTTONEAREST);
    if (hMon)
        return hMon;
    return ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

/** Fit top-left @p x,@p y + @p winW x @p winH inside @p work (multi-monitor: keep splash on one monitor). */
static void clamp_window_to_work_rect(LONG* x, LONG* y, int winW, int winH, const RECT& work)
{
    if (!x || !y || winW <= 0 || winH <= 0)
        return;
    const LONG wa_l = work.left;
    const LONG wa_t = work.top;
    const LONG wa_r = work.right;
    const LONG wa_b = work.bottom;
    if (wa_r <= wa_l || wa_b <= wa_t)
        return;
    LONG rx = *x;
    LONG ry = *y;
    if (rx + winW > wa_r)
        rx = wa_r - winW;
    if (ry + winH > wa_b)
        ry = wa_b - winH;
    if (rx < wa_l)
        rx = wa_l;
    if (ry < wa_t)
        ry = wa_t;
    *x = rx;
    *y = ry;
}

} // namespace

void splash_reassert_topmost() {
    if (g_hwnd && ::IsWindow(g_hwnd))
        (void)::SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void splash_show() {
    if (splash_disabled_for_session())
        return;
    if (g_hwnd && ::IsWindow(g_hwnd)) return;
    g_fadePerTick   = 0;
    g_fadeTicksLeft = 0;

    g_splash = load_rcdata_png((WORD)IDB_PM_SPLASH_1_PNG);
    g_logo   = load_rcdata_png((WORD)IDB_PM_LOGO_PNG);
    if (!g_splash) {
        (void)::OutputDebugStringA(
            "[splash] load_rcdata_png(splash) failed; check Resource.rc IDB_PM_SPLASH_1_PNG "
            "-> dist/branding/splash-1.png\n");
        pmui::ui_log_file_event("splash: skipped (no splash image from resource 5002)");
        teardown_gfx_after_destroy();
        return;
    }
    register_once();
    int iw = (int)g_splash->GetWidth();
    int ih = (int)g_splash->GetHeight();
    if (iw <= 0 || ih <= 0) {
        teardown_gfx_after_destroy();
        return;
    }
    HMONITOR mon = monitor_for_saved_window_placement();
    MONITORINFO mi{};
    mi.cbSize = sizeof(MONITORINFO);
    bool       haveMi = mon && ::GetMonitorInfoW(mon, &mi);
    if (haveMi) {
        const int wArea = mi.rcWork.right - mi.rcWork.left;
        const int hArea = mi.rcWork.bottom - mi.rcWork.top;
        if (wArea < 32 || hArea < 32) {
            mon    = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
            haveMi = mon && ::GetMonitorInfoW(mon, &mi);
        }
    }
    if (haveMi) {
        const int wArea = mi.rcWork.right - mi.rcWork.left;
        const int hArea = mi.rcWork.bottom - mi.rcWork.top;
        const int maxW  = (std::max)(0, (wArea * 9) / 10);
        const int maxH  = (std::max)(0, (hArea * 9) / 10);
        const double ax  = maxW > 0 ? maxW / (double)iw : 0.1;
        const double ay  = maxH > 0 ? maxH / (double)ih : 0.1;
        double     fit  = (std::min)(ax, ay) * kSplashScale;
        if (fit < 0.1) fit = 0.1;
        if (fit > 3.0) fit = 3.0;
        int        winW = (int)std::lround(iw * fit);
        int        winH = (int)std::lround(ih * fit);
        winW            = (std::max)(32, (std::min)(winW, (std::max)(32, wArea)));
        winH            = (std::max)(32, (std::min)(winH, (std::max)(32, hArea)));
        const std::int64_t cx64
            = (static_cast<std::int64_t>(mi.rcWork.left) + static_cast<std::int64_t>(mi.rcWork.right)) / 2;
        const std::int64_t cy64
            = (static_cast<std::int64_t>(mi.rcWork.top) + static_cast<std::int64_t>(mi.rcWork.bottom)) / 2;
        LONG               cx = (cx64 > INT32_MAX) ? INT32_MAX : (cx64 < INT32_MIN ? INT32_MIN : static_cast<LONG>(cx64));
        LONG               cy = (cy64 > INT32_MAX) ? INT32_MAX : (cy64 < INT32_MIN ? INT32_MIN : static_cast<LONG>(cy64));
        LONG               x  = cx - winW / 2;
        LONG               y  = cy - winH / 2;
        clamp_window_to_work_rect(&x, &y, winW, winH, mi.rcWork);
        g_hwnd = ::CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            kClassName, L"", WS_POPUP,
            static_cast<int>(x), static_cast<int>(y), winW, winH,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    } else {
        const int winW = (int)std::lround(iw * kSplashScale);
        const int winH = (int)std::lround(ih * kSplashScale);
        g_hwnd         = ::CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            kClassName, L"", WS_POPUP, 0, 0, winW, winH, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    }
    if (!g_hwnd) {
        (void)::OutputDebugStringA("[splash] CreateWindowEx failed\n");
        pmui::ui_log_file_event("splash: CreateWindowEx failed");
        teardown_gfx_after_destroy();
        return;
    }
    (void)::SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    RECT         client{};
    (void)::GetClientRect(g_hwnd, &client);
    const int    cw  = client.right - client.left;
    const int    ch  = client.bottom - client.top;
    {
        HDC cdc = ::GetDC(g_hwnd);
        if (cdc) {
            paint_splash_client(cdc, cw, ch);
            (void)::ReleaseDC(g_hwnd, cdc);
        }
    }
    (void)::ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    (void)::SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    (void)::UpdateWindow(g_hwnd);
    for (int n = 0; n < 64; n++) {
        MSG msg{};
        if (!::PeekMessageW(&msg, g_hwnd, 0, 0, PM_REMOVE) && !::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            break;
        (void)::TranslateMessage(&msg);
        (void)::DispatchMessageW(&msg);
    }
    pmui::ui_log_file_eventf("splash: shown (layered, %dx%d) — primary frame painted synchronously", cw, ch);
}

void splash_hide() {
    if (g_hwnd && ::IsWindow(g_hwnd)) {
        if (g_fadePerTick) {
            (void)KillTimer(g_hwnd, kTimerFade);
            g_fadePerTick   = 0;
            g_fadeTicksLeft = 0;
        }
        (void)::DestroyWindow(g_hwnd);
    } else if (g_splash || g_gdip) {
        teardown_gfx_after_destroy();
    }
}

void splash_fade_out_and_hide(unsigned durationMs) {
    if (!g_hwnd || !::IsWindow(g_hwnd)) {
        teardown_gfx_after_destroy();
        return;
    }
    if (g_fadePerTick) {
        (void)KillTimer(g_hwnd, kTimerFade);
        g_fadePerTick = 0;
    }
    g_fadeAlpha = 255;
    const unsigned      tickMs = 30u;
    const int           ticks  = (std::max)(2, (int)(durationMs / tickMs));
    g_fadeTicksLeft  = ticks;
    g_fadePerTick  = 255 / ticks;
    (void)SetWindowLongPtrW(
        g_hwnd, GWL_EXSTYLE, GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    (void)::SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    (void)::SetTimer(g_hwnd, kTimerFade, tickMs, nullptr);
}

bool splash_is_visible() { return g_hwnd != nullptr && ::IsWindow(g_hwnd); }

} // namespace pmui
#endif

// Chat workbench + WebView2: 10s watchdog — dismiss splash on JS `ready`, or on timeout fade and
// continue (works with or without FEATURE_SPLASH; fade is no-op when splash is not compiled in).
#include "../ui_log_file.hpp"
namespace pmui {
namespace {
HWND  g_chatLoadWatchFrame   = nullptr;
bool  g_chatLoadWatchPending = false;
/// Set when the chat `kind:ready` path has already run `splash_fade_out` (avoid double fade in OnDeferred).
bool g_chatReadyAlreadyFadedSplash = false;
} // namespace

void splash_register_chat_workbench_composer_wait(HWND mainFrame)
{
    if (!mainFrame || !::IsWindow(mainFrame))
        return;
    if (g_chatLoadWatchPending && g_chatLoadWatchFrame == mainFrame)
        return; // idempotent: one 10s watch per session
    if (g_chatLoadWatchPending && g_chatLoadWatchFrame && ::IsWindow(g_chatLoadWatchFrame))
        (void)::KillTimer(g_chatLoadWatchFrame, kTimerChatWebComposerLoadTimeout);
    g_chatLoadWatchPending  = true;
    g_chatLoadWatchFrame     = mainFrame;
    (void)::SetTimer(mainFrame, kTimerChatWebComposerLoadTimeout, 10000, nullptr);
    pmui::ui_log_file_event("splash: chat workbench — wait for web composer (kind=ready), 10s timeout on main frame");
}

void splash_on_chat_composer_ready()
{
    const bool hadLoadWatch = g_chatLoadWatchPending;
    if (g_chatLoadWatchFrame && ::IsWindow(g_chatLoadWatchFrame)) {
        (void)::KillTimer(g_chatLoadWatchFrame, kTimerChatWebComposerLoadTimeout);
    }
    g_chatLoadWatchPending  = false;
    g_chatLoadWatchFrame     = nullptr;
#if defined(FEATURE_SPLASH)
    // Embeddable chat: main workbench dock still dismisses the splash in `OnDeferred` (150ms).
    // Chat workbench + register: we fade here and set `g_chatReadyAlreadyFadedSplash` for that pass.
    // `--ui-chat` only: same as before (always fade on `ready` here, no register).
    if (hadLoadWatch) {
        g_chatReadyAlreadyFadedSplash = true; // so OnDeferred does not run a second 240ms fade
        pmui::splash_fade_out_and_hide(420);
    } else if (pmui::is_ui_chat_standalone_session()) {
        pmui::splash_fade_out_and_hide(420);
    }
#endif
    if (hadLoadWatch)
        pmui::ui_log_file_event("splash: chat workbench — web composer ready, splash watch cleared");
}

void splash_on_chat_workbench_composer_load_timeout()
{
    HWND h = g_chatLoadWatchFrame;
    if (h && ::IsWindow(h))
        (void)::KillTimer(h, kTimerChatWebComposerLoadTimeout);
    (void)::OutputDebugStringA(
        "[pm-image] chat web: 10s composer load timeout — dismissing splash; process continues\n");
    pmui::ui_log_file_event(
        "chat web: composer `kind:ready` not within 10s (Win11/WebView2 can be slow) — dismissing splash; app continues");
    g_chatLoadWatchPending = false;
    g_chatLoadWatchFrame   = nullptr;
    // Never ExitProcess here: users lose work; embedded WebView2 may still post ready after cold start.
    pmui::splash_fade_out_and_hide(240);
}

bool splash_main_frame_defers_splash_dismissal() noexcept
{
    return g_chatLoadWatchPending;
}

bool splash_consume_deferred_splash_dismissal_by_chat_ready() noexcept
{
    if (!g_chatReadyAlreadyFadedSplash)
        return false;
    g_chatReadyAlreadyFadedSplash = false;
    return true;
}

} // namespace pmui

