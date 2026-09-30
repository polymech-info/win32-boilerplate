// pm-win32-mini: borderless + DWM + GDI+ (experiment host).
// Snapshots (apps/win32-mini/backup/ — frozen copies):
//   main.round-shell-translucent-content-feature.cpp — round shell + translucent body/title ("whole-window glass"; intentional).
//   main.translucent-content-opaque-corners.cpp — earlier stage with square opaque HWND-corner wedges.
// Presentation: UpdateLayeredWindow (32bpp PARGB DIB) — avoids SetLayeredWindowAttributes + WM_PAINT
// trails on layered HWNDs (see file-header links).
// Borrowed from `splash_window.cpp`: WS_EX_LAYERED popup, hbrBackground=nullptr, pre-present before ShowWindow.
// WS_THICKFRAME + WM_NCCALCSIZE (client == full window) for real edge sizing; WM_NCHITTEST still uses kResizeBorderPx band.

// Further reading (DWM / borderless / layered):
// - Custom frame overview: https://learn.microsoft.com/en-us/windows/win32/dwm/customframe
// - DWMWINDOWATTRIBUTE: https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
// - DwmExtendFrameIntoClientArea borders Q&A: https://learn.microsoft.com/en-us/answers/questions/1110801/how-to-remove-the-borders-created-by-dwmextendfram
// - Borderless + shadow: https://stackoverflow.com/questions/43818022/borderless-window-with-drop-shadow
// - Layered redraw / UpdateLayeredWindow: https://stackoverflow.com/questions/19217211/correct-method-for-redrawing-a-layered-window
// - UpdateLayeredWindow: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <dwmapi.h>

#undef min
#undef max
#include <algorithm>
#include <memory>
#include <gdiplus.h>

#pragma comment(lib, "Gdiplus.lib")

#include "filmstrip/FilmStrip.h"
#include "AppDefaults.hpp"

namespace {

const wchar_t kClassName[] = L"PmWin32MiniWnd";

constexpr int kTitleBarPx = 40;
constexpr float kCornerRadius = 12.f;
constexpr int kResizeBorderPx = 6;
// Disabled: offset shadow layers read as sharp rectangular smears vs the round ULW shell (re-enable + tune if revisiting).
// constexpr float kFrameShadowDx = 2.5f;
// constexpr float kFrameShadowDy = 4.f;
// Caption control hit targets (WM_NCLBUTTONDOWN → WM_SYSCOMMAND); width ~ Win11 cells.
constexpr int kCaptionBtnW = 46;

ULONG_PTR g_gdiplusToken = 0;
// -1 = none, 0 = minimize, 1 = maximize/restore, 2 = close — drives hover chrome in the layered title bar.
int g_captionHot = -1;

// Off-screen DIB for UpdateLayeredWindow (single HWND — static is fine).
HDC g_layerDc = nullptr;
HBITMAP g_layerBmp = nullptr;
HBITMAP g_layerOldBmp = nullptr;
void* g_layerBits = nullptr;
int g_layerW = 0;
int g_layerH = 0;

std::unique_ptr<Gdiplus::Font> g_fontTitle;
std::unique_ptr<Gdiplus::Font> g_fontBody;

void EnsurePaintFonts()
{
    if (!g_fontBody) {
        g_fontBody = std::make_unique<Gdiplus::Font>(
            L"Segoe UI", 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        if (g_fontBody->GetLastStatus() != Gdiplus::Ok) {
            g_fontBody = std::make_unique<Gdiplus::Font>(
                L"Tahoma", 11.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        }
    }
    if (!g_fontTitle) {
        g_fontTitle = std::make_unique<Gdiplus::Font>(
            L"Segoe UI", 13.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        if (g_fontTitle->GetLastStatus() != Gdiplus::Ok) {
            g_fontTitle = std::make_unique<Gdiplus::Font>(
                L"Tahoma", 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        }
    }
}

bool CompositionEnabled()
{
    BOOL on = FALSE;
    return SUCCEEDED(DwmIsCompositionEnabled(&on)) && on;
}

void FreeLayerBuffer()
{
    if (g_layerDc) {
        if (g_layerOldBmp) {
            SelectObject(g_layerDc, g_layerOldBmp);
            g_layerOldBmp = nullptr;
        }
        DeleteDC(g_layerDc);
        g_layerDc = nullptr;
    }
    if (g_layerBmp) {
        DeleteObject(g_layerBmp);
        g_layerBmp = nullptr;
    }
    g_layerBits = nullptr;
    g_layerW = 0;
    g_layerH = 0;
}

bool EnsureLayerBuffer(int w, int h)
{
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (g_layerDc && g_layerBmp && w == g_layerW && h == g_layerH) {
        return true;
    }

    FreeLayerBuffer();

    HDC screen = GetDC(nullptr);
    if (!screen) {
        return false;
    }
    g_layerDc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!g_layerDc) {
        return false;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    g_layerBmp = CreateDIBSection(g_layerDc, &bi, DIB_RGB_COLORS, &g_layerBits, nullptr, 0);
    if (!g_layerBmp || !g_layerBits) {
        FreeLayerBuffer();
        return false;
    }
    g_layerOldBmp = static_cast<HBITMAP>(SelectObject(g_layerDc, g_layerBmp));
    g_layerW = w;
    g_layerH = h;
    return true;
}

void PresentLayered(HWND hwnd);

void ApplyDwmChrome(HWND hwnd)
{
    const MARGINS margins{};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    int ncPolicy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));

    const COLORREF borderNone = static_cast<COLORREF>(0xFFFFFFFE);
    (void)DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &borderNone, sizeof(borderNone));

    const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

void NcCalcClient(HWND hwnd, NCCALCSIZE_PARAMS* p)
{
    RECT* r = &p->rgrc[0];

    if (IsZoomed(hwnd)) {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        if (mon && GetMonitorInfoW(mon, &mi)) {
            *r = mi.rcWork;
            return;
        }
    }
}

void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rc, float rad)
{
    const float d = rad * 2.f;
    path.AddArc(rc.X, rc.Y, d, d, 180.f, 90.f);
    path.AddArc(rc.GetRight() - d, rc.Y, d, d, 270.f, 90.f);
    path.AddArc(rc.GetRight() - d, rc.GetBottom() - d, d, d, 0.f, 90.f);
    path.AddArc(rc.X, rc.GetBottom() - d, d, d, 90.f, 90.f);
    path.CloseFigure();
}

struct CaptionButtonRects {
    RECT rMin{};
    RECT rMax{};
    RECT rClose{};
};

void GetCaptionButtonRects(int clientW, CaptionButtonRects& out)
{
    const int bw = kCaptionBtnW;
    const int h = kTitleBarPx;
    out.rClose = {clientW - bw, 0, clientW, h};
    out.rMax = {clientW - 2 * bw, 0, clientW - bw, h};
    out.rMin = {clientW - 3 * bw, 0, clientW - 2 * bw, h};
}

void SyncCaptionHoverFromClientPt(HWND hwnd, POINT clientPt)
{
    RECT crc{};
    GetClientRect(hwnd, &crc);
    const int cw = crc.right - crc.left;
    CaptionButtonRects cb{};
    GetCaptionButtonRects(cw, cb);

    int hot = -1;
    if (clientPt.y >= 0 && clientPt.y < kTitleBarPx) {
        if (PtInRect(&cb.rClose, clientPt)) {
            hot = 2;
        } else if (PtInRect(&cb.rMax, clientPt)) {
            hot = 1;
        } else if (PtInRect(&cb.rMin, clientPt)) {
            hot = 0;
        }
    }
    if (hot != g_captionHot) {
        g_captionHot = hot;
        PresentLayered(hwnd);
    }
}

void DrawCaptionButtonGlyphs(Gdiplus::Graphics& g, HWND hwnd, const CaptionButtonRects& cb)
{
    const bool zoomed = IsZoomed(hwnd) != FALSE;
    const Gdiplus::Color stroke(210, 200, 206, 218);
    const Gdiplus::Color strokeClose(230, 232, 112, 128);
    Gdiplus::Pen pen(stroke, 1.05f);
    Gdiplus::Pen penClose(strokeClose, 1.05f);

    auto center = [](const RECT& r) -> Gdiplus::PointF {
        return Gdiplus::PointF((static_cast<float>(r.left) + static_cast<float>(r.right)) * 0.5f,
            (static_cast<float>(r.top) + static_cast<float>(r.bottom)) * 0.5f);
    };

    // Minimize: horizontal bar
    {
        const Gdiplus::PointF c = center(cb.rMin);
        g.DrawLine(&pen, c.X - 5.f, c.Y + 4.f, c.X + 5.f, c.Y + 4.f);
    }

    // Maximize / restore
    {
        const Gdiplus::PointF c = center(cb.rMax);
        if (!zoomed) {
            const float s = 5.f;
            g.DrawRectangle(&pen, c.X - s, c.Y - s + 1.f, s * 2.f, s * 2.f);
        } else {
            const float s = 4.f;
            const float o = 2.f;
            g.DrawRectangle(&pen, c.X - s - o * 0.5f, c.Y - s - o * 0.5f + 1.f, s * 2.f, s * 2.f);
            g.DrawRectangle(&pen, c.X - s + o * 0.5f, c.Y - s + o * 0.5f + 1.f, s * 2.f, s * 2.f);
        }
    }

    // Close: X
    {
        const Gdiplus::PointF c = center(cb.rClose);
        const float d = 5.f;
        g.DrawLine(&penClose, c.X - d, c.Y - d + 1.f, c.X + d, c.Y + d + 1.f);
        g.DrawLine(&penClose, c.X + d, c.Y - d + 1.f, c.X - d, c.Y + d + 1.f);
    }
}

void DrawCaptionHoverChrome(Gdiplus::Graphics& g, const CaptionButtonRects& cb, int hot)
{
    if (hot < 0 || hot > 2) {
        return;
    }
    const RECT* cell = (hot == 0) ? &cb.rMin : (hot == 1) ? &cb.rMax : &cb.rClose;
    const Gdiplus::Color fill =
        (hot == 2) ? Gdiplus::Color(72, 200, 72, 88) : Gdiplus::Color(48, 255, 255, 255);
    Gdiplus::SolidBrush b(fill);
    g.FillRectangle(&b, static_cast<Gdiplus::REAL>(cell->left),
        static_cast<Gdiplus::REAL>(cell->top),
        static_cast<Gdiplus::REAL>(cell->right - cell->left),
        static_cast<Gdiplus::REAL>(cell->bottom - cell->top));
}

LRESULT HitNcTest(HWND hwnd, POINT screenPt)
{
    const int bx = kResizeBorderPx;
    const int by = kResizeBorderPx;

    RECT wr{};
    GetWindowRect(hwnd, &wr);

    // No edge sizing when maximized (same as default framed behavior).
    if (!IsZoomed(hwnd)) {
        const bool left = screenPt.x < (wr.left + bx);
        const bool right = screenPt.x >= (wr.right - bx);
        const bool top = screenPt.y < (wr.top + by);
        const bool bottom = screenPt.y >= (wr.bottom - by);

        if (left && top) {
            return HTTOPLEFT;
        }
        if (right && top) {
            return HTTOPRIGHT;
        }
        if (left && bottom) {
            return HTBOTTOMLEFT;
        }
        if (right && bottom) {
            return HTBOTTOMRIGHT;
        }
        if (left) {
            return HTLEFT;
        }
        if (right) {
            return HTRIGHT;
        }
        if (bottom) {
            return HTBOTTOM;
        }
        if (top) {
            return HTTOP;
        }
    }

    POINT clientPt = screenPt;
    ScreenToClient(hwnd, &clientPt);

    RECT crc{};
    GetClientRect(hwnd, &crc);
    const int cw = crc.right - crc.left;
    if (clientPt.y >= 0 && clientPt.y < kTitleBarPx && cw > 0) {
        CaptionButtonRects cb{};
        GetCaptionButtonRects(cw, cb);
        if (PtInRect(&cb.rClose, clientPt)) {
            return HTCLOSE;
        }
        if (PtInRect(&cb.rMax, clientPt)) {
            return HTMAXBUTTON;
        }
        if (PtInRect(&cb.rMin, clientPt)) {
            return HTMINBUTTON;
        }
        return HTCAPTION;
    }
    return HTCLIENT;
}

void PaintScene(HWND hwnd, Gdiplus::Graphics& g, float w, float h)
{
    if (w < 2.f || h < 2.f) {
        return;
    }

    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    // GDI DrawText on a 32bpp DIB leaves alpha at 0 → UpdateLayeredWindow "holes". Use GDI+ text only.
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Outside the rounded shell: fully transparent (α=0) so the HWND no longer shows square opaque corners.
    // Tradeoff: very small triangular regions at the literal window corners can be click-through.
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    // Full bitmap bounds so the arc reaches the HWND edges — minimizes transparent wedges under the
    // kResizeBorderPx hit-test band (layered hit-testing uses alpha).
    const Gdiplus::RectF body{0.f, 0.f, w, h};
    const float maxR = (body.Width < body.Height ? body.Width : body.Height) * 0.5f;
    float r = (kCornerRadius * 2.f > body.Width || kCornerRadius * 2.f > body.Height)
        ? maxR * 0.35f
        : kCornerRadius;
    const float rCap = (std::max)(1.f, (std::min)(w, h) * 0.5f - 0.5f);
    if (r > rCap) {
        r = rCap;
    }

    Gdiplus::GraphicsPath shell;
    AddRoundRect(shell, body, r);

#if 0 // Frame shadow (ULW): looked bad — sharp / boxy vs round shell; DWM shadow still preferable if we revisit NC policy.
    {
        const float rShadow = (std::min)(r + 1.75f, rCap);
        Gdiplus::GraphicsPath sh1;
        const Gdiplus::RectF b1(
            body.X + kFrameShadowDx * 0.45f,
            body.Y + kFrameShadowDy * 0.35f,
            body.Width + 1.25f,
            body.Height + 2.f);
        AddRoundRect(sh1, b1, rShadow);
        Gdiplus::SolidBrush brushSh1(Gdiplus::Color(52, 0, 0, 0));
        g.FillPath(&brushSh1, &sh1);

        Gdiplus::GraphicsPath sh2;
        const Gdiplus::RectF b2(body.X + kFrameShadowDx, body.Y + kFrameShadowDy, body.Width + 0.5f, body.Height + 1.f);
        AddRoundRect(sh2, b2, rShadow);
        Gdiplus::SolidBrush brushSh2(Gdiplus::Color(38, 0, 0, 0));
        g.FillPath(&brushSh2, &sh2);
    }
#endif

    // Dark glass: low-luminance fills with α<255 keep the desktop visible through the shell (ULW PARGB).
    const Gdiplus::Color kBodyFill(168, 12, 14, 20);
    Gdiplus::SolidBrush brushBody(kBodyFill);
    g.FillPath(&brushBody, &shell);

    g.SetClip(&shell, Gdiplus::CombineModeIntersect);
    const Gdiplus::Color kTitleBg(188, 16, 18, 26);
    Gdiplus::SolidBrush brushTitle(kTitleBg);
    const float titleH = static_cast<float>(kTitleBarPx);
    g.FillRectangle(&brushTitle, body.X, body.Y, body.Width, titleH);
    g.ResetClip();

    const Gdiplus::Color line(110, 72, 210, 230);
    Gdiplus::Pen penSep(line, 1.f);
    g.DrawLine(&penSep, body.X + r * 0.35f, body.Y + titleH, body.GetRight() - r * 0.35f, body.Y + titleH);

    const int cw = (std::max)(1, static_cast<int>(w));
    CaptionButtonRects cb{};
    GetCaptionButtonRects(cw, cb);
    DrawCaptionHoverChrome(g, cb, g_captionHot);
    DrawCaptionButtonGlyphs(g, hwnd, cb);

    EnsurePaintFonts();
    const float titleReserve = static_cast<float>(kCaptionBtnW * 3 + 16);
    const Gdiplus::RectF titleRect(8.f, 0.f, static_cast<float>(cw) - titleReserve - 8.f, titleH);
    Gdiplus::StringFormat fmtTitle;
    fmtTitle.SetAlignment(Gdiplus::StringAlignmentNear);
    fmtTitle.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmtTitle.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmtTitle.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit | Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush brushTitleText(Gdiplus::Color(235, 228, 232, 240));
    if (g_fontTitle) {
        g.DrawString(L"PM Win32 Mini", -1, g_fontTitle.get(), titleRect, &fmtTitle, &brushTitleText);
    }

    const wchar_t kHint[] =
        L"ULW + PARGB: α=0 outside round shell; body/title α<255 → whole-window glass (feature). "
        L"Caption: WM_NCLBUTTONDOWN → SC_*. Text: GDI+ (GDI α holes). "
        L"Backups: backup\\main.round-shell-translucent-content-feature.cpp, "
        L"backup\\main.translucent-content-opaque-corners.cpp — npm run build:win32-mini";
    const wchar_t kHintFilm[] =
        L"Filmstrip — ← →  PgUp/PgDn  Home/End  ·  wheel on dock  ·  click thumb.  "
        L".\\dist\\pm-win32-mini.exe --src tests\\assets\\agent";
    const float pad = 16.f;
    const float dock = static_cast<float>(filmstrip::DockHeightPx());
    const float hintBottomPad = (filmstrip::FEATURE_FILMSTRIP ? (dock + 28.f) : 56.f);
    const Gdiplus::RectF hintRect(pad, titleH + pad, w - 2.f * pad, h - titleH - pad - hintBottomPad);
    Gdiplus::StringFormat fmtBody;
    fmtBody.SetAlignment(Gdiplus::StringAlignmentNear);
    Gdiplus::SolidBrush brushHint(Gdiplus::Color(150, 132, 148, 168));
    if (g_fontBody) {
        if (filmstrip::FEATURE_FILMSTRIP) {
            g.DrawString(kHintFilm, -1, g_fontBody.get(), hintRect, &fmtBody, &brushHint);
        } else {
            g.DrawString(kHint, -1, g_fontBody.get(), hintRect, &fmtBody, &brushHint);
        }
    }
}

void PresentLayered(HWND hwnd)
{
    RECT wr{};
    if (!GetWindowRect(hwnd, &wr)) {
        return;
    }
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0) {
        return;
    }
    if (!EnsureLayerBuffer(w, h) || !g_layerDc) {
        return;
    }

    {
        Gdiplus::Graphics g(g_layerDc);
        PaintScene(hwnd, g, static_cast<float>(w), static_cast<float>(h));
    }
    if (filmstrip::FEATURE_FILMSTRIP) {
        filmstrip::PaintOverGdi(g_layerDc, w, h, kTitleBarPx);
    }

    POINT dst{wr.left, wr.top};
    SIZE sz{w, h};
    POINT src{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    HDC screen = GetDC(nullptr);
    if (!screen) {
        return;
    }
    (void)UpdateLayeredWindow(hwnd, screen, &dst, &sz, g_layerDc, &src, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        ApplyDwmChrome(hwnd);
        PresentLayered(hwnd);
        return 0;
    }

    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            if (IsZoomed(hwnd)) {
                NcCalcClient(hwnd, p);
                return 0;
            }
            // WS_THICKFRAME enables edge sizing. Save proposed window rect, let DefWindowProc apply frame
            // insets, then set client == full window so the frame is not visible (borderless look).
            const RECT fullWindow = p->rgrc[0];
            const LRESULT lr = DefWindowProcW(hwnd, msg, wParam, lParam);
            p->rgrc[0] = fullWindow;
            return lr;
        }
        break;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 420;
        mmi->ptMinTrackSize.y = 320;
        return 0;
    }

    case WM_NCHITTEST:
        return HitNcTest(hwnd, POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});

    case WM_NCLBUTTONDOWN:
        if (wParam == HTMINBUTTON) {
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            return 0;
        }
        if (wParam == HTMAXBUTTON) {
            PostMessageW(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
            return 0;
        }
        if (wParam == HTCLOSE) {
            PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            return 0;
        }
        break;

    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION || wParam == HTMAXBUTTON) {
            PostMessageW(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
            return 0;
        }
        break;

    case WM_NCMOUSEMOVE: {
        POINT scr{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        POINT clientPt = scr;
        ScreenToClient(hwnd, &clientPt);
        SyncCaptionHoverFromClientPt(hwnd, clientPt);
        if (clientPt.y >= 0 && clientPt.y < kTitleBarPx) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            (void)TrackMouseEvent(&tme);
        }
        break;
    }

    case WM_MOUSEMOVE: {
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        SyncCaptionHoverFromClientPt(hwnd, pt);
        if (filmstrip::FEATURE_FILMSTRIP) {
            RECT crMm{};
            GetClientRect(hwnd, &crMm);
            filmstrip::OnMouseMove(hwnd, pt.x, pt.y, static_cast<int>(crMm.right), static_cast<int>(crMm.bottom));
        }
        break;
    }

    case WM_MOUSEWHEEL:
        if (filmstrip::FEATURE_FILMSTRIP) {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            RECT cr{};
            GetClientRect(hwnd, &cr);
            if (filmstrip::OnMouseWheel(hwnd, GET_WHEEL_DELTA_WPARAM(wParam), pt.x, pt.y, kTitleBarPx,
                    static_cast<int>(cr.right), static_cast<int>(cr.bottom))) {
                return 0;
            }
        }
        break;

    case WM_LBUTTONDOWN:
        if (filmstrip::FEATURE_FILMSTRIP) {
            RECT crLb{};
            GetClientRect(hwnd, &crLb);
            if (filmstrip::OnLButtonDown(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kTitleBarPx,
                    static_cast<int>(crLb.right), static_cast<int>(crLb.bottom))) {
                return 0;
            }
        }
        break;

    case WM_KEYDOWN:
        if (filmstrip::FEATURE_FILMSTRIP && filmstrip::OnKeyDown(hwnd, static_cast<UINT>(wParam))) {
            return 0;
        }
        break;

    case WM_TIMER:
        if (filmstrip::FEATURE_FILMSTRIP && filmstrip::OnTimer(hwnd, wParam)) {
            return 0;
        }
        break;

    case WM_MOUSELEAVE:
        if (filmstrip::FEATURE_FILMSTRIP) {
            filmstrip::OnMouseLeaveClient(hwnd);
        }
        if (g_captionHot != -1) {
            g_captionHot = -1;
            PresentLayered(hwnd);
        }
        return 0;

    case WM_NCACTIVATE:
        if (!CompositionEnabled()) {
            return TRUE;
        }
        break;

    case WM_ERASEBKGND:
        // Pixels owned by UpdateLayeredWindow; avoid GDI erase fighting the DIB.
        return 1;

    case WM_WINDOWPOSCHANGED: {
        const auto* wp = reinterpret_cast<const WINDOWPOS*>(lParam);
        if (!(wp->flags & SWP_NOMOVE) || !(wp->flags & SWP_NOSIZE)) {
            PresentLayered(hwnd);
            AppDefaults::Save(hwnd);
        }
        break;
    }

    case WM_SIZE:
        PresentLayered(hwnd);
        break;

    case WM_DPICHANGED: {
        const RECT* const suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
            suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        (void)BeginPaint(hwnd, &ps);
        PresentLayered(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        AppDefaults::Save(hwnd);
        if (filmstrip::FEATURE_FILMSTRIP) {
            filmstrip::Shutdown();
        }
        g_fontTitle.reset();
        g_fontBody.reset();
        FreeLayerBuffer();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_cmd)
{
    const HRESULT coinit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool coinit_owned = (coinit == S_OK);
    if (FAILED(coinit) && coinit != S_FALSE && coinit != RPC_E_CHANGED_MODE) {
        return 1;
    }

    Gdiplus::GdiplusStartupInput gdiin;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiin, nullptr) != Gdiplus::Ok) {
        if (coinit_owned) {
            CoUninitialize();
        }
        return 1;
    }

    if (filmstrip::FEATURE_FILMSTRIP) {
        (void)filmstrip::InitFromArgv(__argc, __wargv);
    }

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    if (!RegisterClassW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            if (filmstrip::FEATURE_FILMSTRIP) {
                filmstrip::Shutdown();
            }
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            if (coinit_owned) {
                CoUninitialize();
            }
            return 1;
        }
    }

    const DWORD style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN
        | WS_CLIPSIBLINGS;
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_LAYERED,
        kClassName,
        L"PM Win32 Mini",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        780,
        520,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!hwnd) {
        if (filmstrip::FEATURE_FILMSTRIP) {
            filmstrip::Shutdown();
        }
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        if (coinit_owned) {
            CoUninitialize();
        }
        return 1;
    }

    if (filmstrip::FEATURE_FILMSTRIP) {
        filmstrip::SetHost(hwnd);
    }

    AppDefaults::LoadAndApply(hwnd);

    // Do not call SetLayeredWindowAttributes(LWA_ALPHA) when driving pixels via UpdateLayeredWindow.
    PresentLayered(hwnd);

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    ShowWindow(hwnd, show_cmd);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    if (coinit_owned) {
        CoUninitialize();
    }
    return static_cast<int>(msg.wParam);
}
