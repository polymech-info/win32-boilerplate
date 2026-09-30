#include "win/screenshot.hpp"

// <gdiplus.h> uses IStream / HDC unqualified in its inline declarations,
// but our screenshot.hpp defines WIN32_LEAN_AND_MEAN which strips ole2.h
// from <windows.h>. Pull the COM stream / HDC declarations in explicitly
// BEFORE <gdiplus.h> or compilation breaks with cascades of "C2065:
// 'IStream' undeclared", "C2275: 'HDC' …", and "C2061: identifier 'byte'".
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>          // DwmGetWindowAttribute / DWMWA_EXTENDED_FRAME_BOUNDS
#include <vector>
#include <cstring>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Dwmapi.lib")

namespace media::win {
namespace {

// Find the GDI+ encoder CLSID for a given MIME type (e.g. L"image/png").
// Returns the encoder index on success, -1 on failure.
int find_encoder_clsid(const wchar_t* mime, CLSID& out)
{
    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0)
        return -1;

    std::vector<unsigned char> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok)
        return -1;

    for (UINT j = 0; j < num; ++j) {
        if (info[j].MimeType && std::wcscmp(info[j].MimeType, mime) == 0) {
            out = info[j].Clsid;
            return static_cast<int>(j);
        }
    }
    return -1;
}

// RAII pair for GDI+ startup / shutdown — kept local so we don't introduce
// a process-wide dependency on the caller having already started GDI+.
struct GdiplusScope
{
    ULONG_PTR token = 0;
    bool      ok    = false;

    GdiplusScope()
    {
        Gdiplus::GdiplusStartupInput input;
        ok = Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    }
    ~GdiplusScope()
    {
        if (ok) Gdiplus::GdiplusShutdown(token);
    }
    GdiplusScope(const GdiplusScope&) = delete;
    GdiplusScope& operator=(const GdiplusScope&) = delete;
};

// Get the *visible* bounds of @p hwnd — what the user actually sees on
// screen, with the invisible DWM drop-shadow / resize-grab margin removed.
//
// On Windows Vista+, GetWindowRect returns the OS window rectangle which
// (since DWM composition was added) includes a few pixels of invisible
// frame around the visible chrome. Capturing that rect with BitBlt drags
// in the desktop background under those margins → the captured PNG looks
// like the app has a grey/black border around it.
//
// DWMWA_EXTENDED_FRAME_BOUNDS asks DWM for the bounds of the visible
// chrome only. Falls back to GetWindowRect if DWM is disabled (e.g.
// classic theme on legacy systems) or the call otherwise fails — there's
// nothing better to do, and the worst case is the same border we had
// before this fix.
bool get_visible_frame_bounds(HWND hwnd, RECT& out)
{
    if (::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                &out, sizeof(out)) == S_OK)
        return true;
    return ::GetWindowRect(hwnd, &out) != FALSE;
}

// RAII override of the calling thread's DPI awareness context.
//
// Why this exists:
//   The app manifest declares PerMonitorV2, but screenshots can also be taken
//   from helper paths or older builds where the caller's DPI context is not
//   guaranteed. A DPI-unaware caller sees GetWindowRect in logical pixels
//   while GetDC(NULL) is in physical pixels; BitBlt then captures only the
//   upper-left fraction of the window on scaled displays.
//
//   SetThreadDpiAwarenessContext (Win10 1607+) flips the calling thread
//   into per-monitor v2 just for the duration of the capture, so:
//     * GetWindowRect returns physical pixels,
//     * the screen DC is in physical pixels,
//     * the bitmap dimensions match what we BitBlt.
//
//   Resolved dynamically via GetProcAddress so the binary still loads on
//   pre-1607 Windows (where there's no per-monitor DPI to begin with and
//   the original BitBlt path is correct).
struct ThreadDpiPerMonitorScope
{
    using SetCtx_t = DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT);

    SetCtx_t              fn   = nullptr;
    DPI_AWARENESS_CONTEXT prev = nullptr;
    bool                  active = false;

    ThreadDpiPerMonitorScope()
    {
        if (HMODULE h = ::GetModuleHandleW(L"user32.dll")) {
            fn = reinterpret_cast<SetCtx_t>(
                ::GetProcAddress(h, "SetThreadDpiAwarenessContext"));
        }
        if (fn) {
            prev = fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            active = (prev != nullptr);
        }
    }
    ~ThreadDpiPerMonitorScope()
    {
        if (active && fn) fn(prev);
    }
    ThreadDpiPerMonitorScope(const ThreadDpiPerMonitorScope&) = delete;
    ThreadDpiPerMonitorScope& operator=(const ThreadDpiPerMonitorScope&) = delete;
};

} // namespace

bool capture_window_to_png(HWND hwnd,
                           const std::wstring& out_path,
                           std::string& err_utf8)
{
    err_utf8.clear();

    if (!hwnd || !::IsWindow(hwnd)) {
        err_utf8 = "screenshot: invalid window handle";
        return false;
    }

    // Flip THIS THREAD into per-monitor v2 DPI awareness for the duration
    // of the capture. Without this, on >100% scaled displays the
    // GetWindowRect / screen-DC mismatch produces an upper-left crop.
    // See the ThreadDpiPerMonitorScope header comment above for the
    // full why. Restored on scope exit (RAII).
    ThreadDpiPerMonitorScope dpi_scope;

    RECT rc{};
    // Visible bounds (no DWM extended-frame margin around the chrome) so
    // the captured PNG doesn't have a grey halo around the actual window.
    if (!get_visible_frame_bounds(hwnd, rc)) {
        err_utf8 = "screenshot: GetWindowRect / DwmGetWindowAttribute failed";
        return false;
    }
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        err_utf8 = "screenshot: zero-size window rectangle";
        return false;
    }

    HDC hdcScreen = ::GetDC(nullptr);
    if (!hdcScreen) {
        err_utf8 = "screenshot: GetDC(NULL) failed";
        return false;
    }

    HDC     hdcMem = ::CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp   = ::CreateCompatibleBitmap(hdcScreen, w, h);
    bool    save_ok = false;

    if (hdcMem && hBmp) {
        HGDIOBJ old = ::SelectObject(hdcMem, hBmp);

        // CAPTUREBLT pulls in layered / topmost windows that overlap us — what
        // a "screen photo" of our window should normally include.
        const BOOL blit_ok = ::BitBlt(hdcMem, 0, 0, w, h,
                                       hdcScreen, rc.left, rc.top,
                                       SRCCOPY | CAPTUREBLT);
        ::SelectObject(hdcMem, old);

        if (blit_ok) {
            GdiplusScope gp;
            if (gp.ok) {
                CLSID png_clsid{};
                if (find_encoder_clsid(L"image/png", png_clsid) >= 0) {
                    Gdiplus::Bitmap bmp(hBmp, nullptr);
                    if (bmp.GetLastStatus() == Gdiplus::Ok) {
                        const Gdiplus::Status st =
                            bmp.Save(out_path.c_str(), &png_clsid, nullptr);
                        save_ok = (st == Gdiplus::Ok);
                        if (!save_ok)
                            err_utf8 = "screenshot: GDI+ Bitmap::Save failed";
                    } else {
                        err_utf8 = "screenshot: GDI+ Bitmap construction failed";
                    }
                } else {
                    err_utf8 = "screenshot: PNG encoder not registered";
                }
            } else {
                err_utf8 = "screenshot: GdiplusStartup failed";
            }
        } else {
            err_utf8 = "screenshot: BitBlt failed";
        }
    } else {
        err_utf8 = "screenshot: CreateCompatibleDC/Bitmap failed";
    }

    if (hBmp)   ::DeleteObject(hBmp);
    if (hdcMem) ::DeleteDC(hdcMem);
    ::ReleaseDC(nullptr, hdcScreen);

    return save_ok;
}

} // namespace media::win
