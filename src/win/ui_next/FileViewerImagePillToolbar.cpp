#include "stdafx.h"
#include "FileViewerImagePillToolbar.h"
#include "helpers/theme.hpp"
#if defined(FEATURE_SVG_BUTTONS)
#include "helpers/svg_raster.hpp"
#include "svg_paths.generated.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <gdiplus.h>
#include <memory>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

namespace {

constexpr bool kImageCropFeatureEnabled = false;

struct PillPaintColors {
    BYTE bgA;
    BYTE bgR;
    BYTE bgG;
    BYTE bgB;
    BYTE borderA;
    BYTE borderR;
    BYTE borderG;
    BYTE borderB;
    BYTE hoverA;
    BYTE hoverR;
    BYTE hoverG;
    BYTE hoverB;
    COLORREF fallbackIcon;
    Gdiplus::Color label;
};

inline int Ui115(int basePx96, int dpi)
{
    return ::MulDiv(::MulDiv(basePx96, 115, 100), dpi, 96);
}

inline int UiPill(int basePx96, int dpi)
{
    return ::MulDiv(Ui115(basePx96, dpi), 7, 10);
}

void FillRoundRectAlpha(HDC hdc, int x, int y, int w, int h, int radius, BYTE alpha, BYTE rr, BYTE gg, BYTE bb)
{
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float rx = (float)x, ry = (float)y, rw = (float)w, rh = (float)h;
    const float d  = (float)(radius * 2);
    Gdiplus::GraphicsPath path;
    path.AddArc(rx, ry, d, d, 180, 90);
    path.AddArc(rx + rw - d, ry, d, d, 270, 90);
    path.AddArc(rx + rw - d, ry + rh - d, d, d, 0, 90);
    path.AddArc(rx, ry + rh - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::SolidBrush br(Gdiplus::Color(alpha, rr, gg, bb));
    g.FillPath(&br, &path);
}

void StrokeRoundRectAlpha(HDC hdc, int x, int y, int w, int h, int radius,
                          BYTE alpha, BYTE rr, BYTE gg, BYTE bb)
{
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float rx = (float)x + 0.5f, ry = (float)y + 0.5f;
    const float rw = (float)w - 1.f, rh = (float)h - 1.f;
    const float d  = (float)(radius * 2);
    Gdiplus::GraphicsPath path;
    path.AddArc(rx, ry, d, d, 180, 90);
    path.AddArc(rx + rw - d, ry, d, d, 270, 90);
    path.AddArc(rx + rw - d, ry + rh - d, d, d, 0, 90);
    path.AddArc(rx, ry + rh - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::Pen pen(Gdiplus::Color(alpha, rr, gg, bb), 1.f);
    g.DrawPath(&pen, &path);
}

void DrawBitmapIcon(HDC hdc, HBITMAP hb, int x, int y, int size, BYTE globalAlpha = 255)
{
    if (!hb || globalAlpha == 0)
        return;
    DIBSECTION ds{};
    if (::GetObject(hb, sizeof(ds), &ds) == sizeof(DIBSECTION) && ds.dsBm.bmBitsPixel == 32
        && ds.dsBm.bmBits != nullptr) {
        const int bw = static_cast<int>(std::labs(ds.dsBm.bmWidth));
        const int bh = static_cast<int>(std::labs(ds.dsBm.bmHeight));
        if (bw > 0 && bh > 0) {
            HDC mdc = ::CreateCompatibleDC(hdc);
            if (mdc) {
                HGDIOBJ old = ::SelectObject(mdc, hb);
                BLENDFUNCTION bf{};
                bf.BlendOp             = AC_SRC_OVER;
                bf.BlendFlags          = 0;
                bf.SourceConstantAlpha = globalAlpha;
                bf.AlphaFormat         = AC_SRC_ALPHA;
                (void)::AlphaBlend(hdc, x, y, size, size, mdc, 0, 0, bw, bh, bf);
                (void)::SelectObject(mdc, old);
                (void)::DeleteDC(mdc);
                return;
            }
        }
    }
    std::unique_ptr<Gdiplus::Bitmap> bmp(Gdiplus::Bitmap::FromHBITMAP(hb, nullptr));
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok)
        return;
    Gdiplus::Graphics g(hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.DrawImage(bmp.get(), x, y, size, size);
}

PillPaintColors CurrentPillColors()
{
    const auto& pal = pmui::theme_palette();
    if (pal.dark) {
        return {178, 30, 32, 38,
                64, 96, 105, 124,
                58, 112, 124, 148,
                RGB(196, 202, 214),
                Gdiplus::Color(255, 198, 204, 216)};
    }
    return {204, 248, 250, 255,
            96, 130, 140, 165,
            72, 70, 86, 115,
            RGB(55, 62, 78),
            Gdiplus::Color(255, 52, 58, 72)};
}

} // namespace

void CFileViewerImagePillToolbar::ReleaseResources()
{
#if defined(FEATURE_SVG_BUTTONS)
    auto wipe = [](HBITMAP& h) {
        if (h) {
            ::DeleteObject(h);
            h = nullptr;
        }
    };
    wipe(m_bmpPrevImg);
    wipe(m_bmpNextImg);
    wipe(m_bmpFit);
    wipe(m_bmpFull);
    wipe(m_bmpCrop);
    wipe(m_bmpZoomIn);
    wipe(m_bmpZoomOut);
    m_bmpDpi = 0;
#endif
}

void CFileViewerImagePillToolbar::EnsureIconBitmaps(HWND dpiHwnd)
{
#if !defined(FEATURE_SVG_BUTTONS)
    (void)dpiHwnd;
#else
    if (!dpiHwnd)
        return;
    static constexpr UINT kTileVer = 7u;
    if (m_tileVer != kTileVer) {
        ReleaseResources();
        m_tileVer = kTileVer;
    }
    const UINT dpi    = static_cast<UINT>(::GetDpiForWindow(dpiHwnd));
    const int  iconPx = UiPill(24, static_cast<int>(dpi));
    if (m_bmpDpi == dpi && m_bmpPrevImg && m_bmpNextImg && m_bmpFit && m_bmpFull
        && (!kImageCropFeatureEnabled || m_bmpCrop) && m_bmpZoomIn && m_bmpZoomOut)
        return;
    ReleaseResources();
    m_bmpDpi = dpi;

    const auto& pal = pmui::theme_palette();
    const COLORREF iconRgb = pal.dark ? RGB(196, 202, 214) : RGB(58, 65, 82);
    const auto r = static_cast<std::uint8_t>(GetRValue(iconRgb));
    const auto g = static_cast<std::uint8_t>(GetGValue(iconRgb));
    const auto b = static_cast<std::uint8_t>(GetBValue(iconRgb));

    auto tablerFile = [](const wchar_t* leaf) -> std::wstring {
        std::wstring p(PM_TABLER_FILLED_DIR_W);
        if (!p.empty() && p.back() != L'/' && p.back() != L'\\')
            p += L'\\';
        p += leaf;
        return p;
    };

    m_bmpPrevImg =
        pmui_svg_rasterize_file_wide(tablerFile(L"chevron-left.svg").c_str(), iconPx, r, g, b);
    m_bmpNextImg =
        pmui_svg_rasterize_file_wide(tablerFile(L"chevron-right.svg").c_str(), iconPx, r, g, b);
    m_bmpFit =
        pmui_svg_rasterize_file_wide(tablerFile(L"arrow-autofit-content.svg").c_str(), iconPx, r, g, b);
    m_bmpFull = pmui_svg_rasterize_file_wide(tablerFile(L"photo.svg").c_str(), iconPx, r, g, b);
    if (kImageCropFeatureEnabled) {
        m_bmpCrop =
            pmui_svg_rasterize_file_wide(tablerFile(L"crop-1-1.svg").c_str(), iconPx, r, g, b);
    }
    m_bmpZoomIn =
        pmui_svg_rasterize_file_wide(tablerFile(L"zoom-in.svg").c_str(), iconPx, r, g, b);
    m_bmpZoomOut =
        pmui_svg_rasterize_file_wide(tablerFile(L"zoom-out.svg").c_str(), iconPx, r, g, b);
#endif
}

void CFileViewerImagePillToolbar::Layout(int clientW, int clientH, int dpi)
{
    const int iconPx   = UiPill(24, dpi);
    const int btnGap   = UiPill(5, dpi);
    const int groupGap = UiPill(9, dpi);
    const int pillPadX = UiPill(12, dpi);
    const int pillPadY = UiPill(7, dpi);
    const int btnPitch = iconPx + btnGap;
    const int zoomLabW = UiPill(46, dpi);
    const int navW     = m_showImageNav ? (iconPx * 2 + groupGap) : 0;
    const int toolCount = kImageCropFeatureEnabled ? 5 : 4;
    const int toolsW   = iconPx * toolCount + btnGap * (toolCount - 1);
    const int pillW    = pillPadX * 2 + navW + toolsW + groupGap + zoomLabW;
    const int pillH    = iconPx + pillPadY * 2;
    const int pillX    = (std::max)(4, (clientW - pillW) / 2);
    const int pillY    = UiPill(14, dpi) + UiPill(6, dpi);

    m_pillBottomY = pillY + pillH;

    m_rcTopPill.SetRectEmpty();
    m_btnPrevImg.SetRectEmpty();
    m_btnNextImg.SetRectEmpty();
    m_btnZoomOut.SetRectEmpty();
    m_btnFit.SetRectEmpty();
    m_btnFull.SetRectEmpty();
    m_btnCrop.SetRectEmpty();
    m_btnZoomIn.SetRectEmpty();

    if (!m_visible)
        return;

    m_rcTopPill.SetRect(pillX, pillY, pillX + pillW, pillY + pillH);

    int bx = pillX + pillPadX;
    const int by = pillY + pillPadY;
    if (m_showImageNav) {
        m_btnPrevImg.SetRect(bx, by, bx + iconPx, by + iconPx);
        bx += iconPx + groupGap;
        m_btnNextImg.SetRect(bx, by, bx + iconPx, by + iconPx);
        bx += iconPx + groupGap;
    }
    m_btnZoomOut.SetRect(bx, by, bx + iconPx, by + iconPx);
    bx += btnPitch;
    m_btnFit.SetRect(bx, by, bx + iconPx, by + iconPx);
    bx += btnPitch;
    m_btnFull.SetRect(bx, by, bx + iconPx, by + iconPx);
    bx += btnPitch;
    if (kImageCropFeatureEnabled) {
        m_btnCrop.SetRect(bx, by, bx + iconPx, by + iconPx);
        bx += btnPitch;
    }
    m_btnZoomIn.SetRect(bx, by, bx + iconPx, by + iconPx);
}

void CFileViewerImagePillToolbar::Paint(HDC hdc, HWND dpiHwnd, double fitZoom, double userZoom,
    int overlayHoveredBtn) const
{
    if (!m_visible || m_rcTopPill.IsRectEmpty())
        return;

    const int dpiPaint = dpiHwnd ? (int)::GetDpiForWindow(dpiHwnd) : 96;
    const int iconPx = UiPill(24, dpiPaint);

    const CRect pillRc = m_rcTopPill;
    const int   pillX = pillRc.left, pillY = pillRc.top, pillW = pillRc.Width(), pillH = pillRc.Height();
    const int   cornerR = pillH / 2;
    const PillPaintColors colors = CurrentPillColors();

    FillRoundRectAlpha(hdc, pillX, pillY, pillW, pillH, cornerR,
        colors.bgA, colors.bgR, colors.bgG, colors.bgB);
    StrokeRoundRectAlpha(hdc, pillX, pillY, pillW, pillH, cornerR,
        colors.borderA, colors.borderR, colors.borderG, colors.borderB);

    auto btnHov = [&](const CRect& slot, int id) -> bool { return overlayHoveredBtn == id; };

    auto paintIconSlot = [&](CRect slot, int id, HBITMAP hb, LPCWSTR fallback) {
        if (btnHov(slot, id)) {
            RECT rr = slot;
            ::InflateRect(&rr, -UiPill(1, dpiPaint), -UiPill(1, dpiPaint));
            FillRoundRectAlpha(hdc, rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top,
                UiPill(7, dpiPaint),
                colors.hoverA, colors.hoverR, colors.hoverG, colors.hoverB);
        }
#if defined(FEATURE_SVG_BUTTONS)
        if (hb) {
            const bool nav = (id == kPrevImg || id == kNextImg);
            const int drawPx = nav ? UiPill(29, dpiPaint) : iconPx;
            const int ix = slot.left + (slot.Width() - drawPx) / 2;
            const int iy = slot.top + (slot.Height() - drawPx) / 2;
            DrawBitmapIcon(hdc, hb, ix, iy, drawPx, 255);
        } else
#endif
        {
            RECT rw = {slot.left, slot.top, slot.right, slot.bottom};
            ::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, colors.fallbackIcon);
            HFONT hPrev = (HFONT)::SelectObject(hdc, (HFONT)::GetStockObject(DEFAULT_GUI_FONT));
            ::DrawTextW(hdc, fallback, -1, &rw, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            ::SelectObject(hdc, hPrev);
        }
    };

#if defined(FEATURE_SVG_BUTTONS)
    const HBITMAP hbPrev = m_bmpPrevImg, hbNext = m_bmpNextImg, hbZo = m_bmpZoomOut, hbFit = m_bmpFit,
        hbFull = m_bmpFull, hbCrop = kImageCropFeatureEnabled ? m_bmpCrop : nullptr, hbZi = m_bmpZoomIn;
#else
    const HBITMAP hbPrev = nullptr, hbNext = nullptr, hbZo = nullptr, hbFit = nullptr, hbFull = nullptr,
        hbCrop = nullptr, hbZi = nullptr;
#endif

    if (m_showImageNav)
        paintIconSlot(m_btnPrevImg, kPrevImg, hbPrev, L"\u2039");
    paintIconSlot(m_btnZoomOut, kZoomOut, hbZo, L"\u2212");
    paintIconSlot(m_btnFit, kFit, hbFit, L"Fit");
    paintIconSlot(m_btnFull, kFull, hbFull, L"Full");
    if (kImageCropFeatureEnabled)
        paintIconSlot(m_btnCrop, kCrop, hbCrop, L"\u229E");
    paintIconSlot(m_btnZoomIn, kZoomIn, hbZi, L"+");
    if (m_showImageNav)
        paintIconSlot(m_btnNextImg, kNextImg, hbNext, L"\u203A");

    wchar_t zbuf[24]{};
    if (userZoom > 0.0) {
        double rel = (fitZoom > 1e-12) ? (userZoom / fitZoom) * 100.0 : (userZoom * 100.0);
        if (rel > 99999.0)
            rel = 99999.0;
        swprintf_s(zbuf, L"%.0f%%", rel);
    } else
        wcscpy_s(zbuf, L"Fit");

    const int pillPadX = UiPill(12, dpiPaint);
    const int zoomLabW = UiPill(46, dpiPaint);
    const int zl       = pillX + pillW - zoomLabW - pillPadX;
    const int zt       = pillY;
    const int zr       = pillX + pillW - UiPill(10, dpiPaint);
    const int zb       = pillY + pillH;

    Gdiplus::Graphics gZ(hdc);
    gZ.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gZ.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    Gdiplus::FontFamily ff(L"Segoe UI");
    const Gdiplus::REAL px = (Gdiplus::REAL)UiPill(11, dpiPaint);
    Gdiplus::Font       font(&ff, px, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush br(colors.label);
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentFar);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetTrimming(Gdiplus::StringTrimmingNone);
    const Gdiplus::RectF rf((Gdiplus::REAL)zl, (Gdiplus::REAL)zt, (Gdiplus::REAL)(zr - zl),
        (Gdiplus::REAL)(zb - zt));
    gZ.DrawString(zbuf, -1, &font, rf, &sf, &br);
}

CFileViewerImagePillToolbar::Hit CFileViewerImagePillToolbar::HitTest(POINT pt) const
{
    if (!m_visible)
        return kNone;
    if (m_showImageNav) {
        if (m_btnPrevImg.PtInRect(pt))
            return kPrevImg;
        if (m_btnNextImg.PtInRect(pt))
            return kNextImg;
    }
    if (m_btnZoomOut.PtInRect(pt))
        return kZoomOut;
    if (m_btnFit.PtInRect(pt))
        return kFit;
    if (m_btnFull.PtInRect(pt))
        return kFull;
    if (kImageCropFeatureEnabled && m_btnCrop.PtInRect(pt))
        return kCrop;
    if (m_btnZoomIn.PtInRect(pt))
        return kZoomIn;
    return kNone;
}
