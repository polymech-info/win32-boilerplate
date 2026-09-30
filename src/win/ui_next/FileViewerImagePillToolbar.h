#ifndef PM_UI_FILEVIEWER_IMAGE_PILL_TOOLBAR_H
#define PM_UI_FILEVIEWER_IMAGE_PILL_TOOLBAR_H

//
// Centre image preview: floating zoom / fit / full pill drawn into the viewer's
// memory DC (no separate HWND). Owns layout, hit-testing, optional Tabler SVG tiles,
// and visibility — similar in spirit to `OwnRibbonLayout.h` factoring UI chrome out
// of the host window.
//
#include "stdafx.h"
#include "features.h"

/// GDI-painted pill strip for `CFileViewer` image mode (prev/next, zoom−, fit, full, crop, zoom+, % label).
class CFileViewerImagePillToolbar {
public:
    /// Matches `CFileViewer::OverlayBtn` values for tool hits (not filename). Prev/next map to 7/8 in the host.
    enum Hit : int { kNone = -1, kZoomOut = 0, kFit = 1, kFull = 2, kCrop = 3, kZoomIn = 4, kPrevImg = 5, kNextImg = 6 };

    void ReleaseResources();
    /// Rebuild icon DIBs when DPI or theme changes; no-op when `FEATURE_SVG_BUTTONS` is off.
    void EnsureIconBitmaps(HWND dpiHwnd);

    /// Y coordinate of the pill bottom edge — for RAW status stacking (valid after `Layout`).
    int PillBottomY() const noexcept { return m_pillBottomY; }

    void SetVisible(bool on) noexcept { m_visible = on; }
    bool Visible() const noexcept { return m_visible; }

    void SetShowImageNav(bool on) noexcept { m_showImageNav = on; }
    bool ShowImageNav() const noexcept { return m_showImageNav; }

    void Layout(int clientW, int clientH, int dpi);

    /// @param dpiHwnd Window used for `GetDpiForWindow` (not the mem DC — `hdc` may be off-screen).
    /// @param overlayHoveredBtn `CFileViewer::OverlayBtn` (−1..5); only 0..4 affect pill hover.
    void Paint(HDC hdc, HWND dpiHwnd, double fitZoom, double userZoom, int overlayHoveredBtn) const;

    [[nodiscard]] Hit HitTest(POINT pt) const;

    CRect BtnZoomOut() const { return m_btnZoomOut; }
    CRect BtnFit() const { return m_btnFit; }
    CRect BtnFull() const { return m_btnFull; }
    CRect BtnCrop() const { return m_btnCrop; }
    CRect BtnZoomIn() const { return m_btnZoomIn; }
    CRect BtnPrevImg() const { return m_btnPrevImg; }
    CRect BtnNextImg() const { return m_btnNextImg; }
    CRect PillOuter() const { return m_rcTopPill; }

private:
    int   m_pillBottomY = 0;
    bool  m_visible     = false;
    bool  m_showImageNav = false;
    CRect m_rcTopPill{};
    CRect m_btnPrevImg{};
    CRect m_btnNextImg{};
    CRect m_btnZoomOut{};
    CRect m_btnFit{};
    CRect m_btnFull{};
    CRect m_btnCrop{};
    CRect m_btnZoomIn{};
#if defined(FEATURE_SVG_BUTTONS)
    HBITMAP m_bmpPrevImg = nullptr;
    HBITMAP m_bmpNextImg = nullptr;
    HBITMAP m_bmpFit     = nullptr;
    HBITMAP m_bmpFull    = nullptr;
    HBITMAP m_bmpCrop    = nullptr;
    HBITMAP m_bmpZoomIn = nullptr;
    HBITMAP m_bmpZoomOut = nullptr;
    UINT    m_bmpDpi     = 0;
    UINT    m_tileVer    = 0;
#endif
};

#endif // PM_UI_FILEVIEWER_IMAGE_PILL_TOOLBAR_H
