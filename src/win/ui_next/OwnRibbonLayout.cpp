// Own-ribbon toolbar strip — ToolbarBuilder (View.cpp) pattern: CPager + CToolBar + CImageList.
#include "stdafx.h"

#ifdef FEATURE_USE_OWN_RIBBON

#include "constants.hpp"
#include "OwnRibbonLayout.h"
#include "helpers/theme.hpp"
#include "helpers/ui_constants.hpp"
#ifdef FEATURE_SVG_BUTTONS
#include "helpers/svg_raster.hpp"
#endif

#include <cwchar>

#include <algorithm>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <commctrl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

#ifndef TBSTYLE_TRANSPARENT
#define TBSTYLE_TRANSPARENT 0x8000
#endif

#ifndef CDIS_DISABLED
#define CDIS_DISABLED 0x004U
#endif

#ifndef CDIS_CHECKED
#define CDIS_CHECKED 0x008U
#endif

#ifndef CDIS_HOT
#define CDIS_HOT 0x040U
#endif

#ifndef BTNS_SHOWTEXT
#define BTNS_SHOWTEXT 0x00000040
#endif

#ifndef BTNS_AUTOSIZE
#define BTNS_AUTOSIZE 0x00000010
#endif

#ifndef BTNS_DROPDOWN
#define BTNS_DROPDOWN 0x00000008
#endif

#ifndef BTNS_WHOLEDROPDOWN
#define BTNS_WHOLEDROPDOWN 0x00000080
#endif

#ifndef TBSTYLE_SEP
#define TBSTYLE_SEP 0x0001
#endif

namespace own_ribbon {

wchar_t s_lastLayoutError[512]{};

namespace {

using RibbonUi = pmui::ui::RibbonStripLayout;

void LogToDebugFile(const wchar_t* msg)
{
    static bool s_logInitialized = false;
    static FILE* s_logFile = nullptr;
    if (!s_logInitialized) {
        s_logInitialized = true;
        wchar_t cwd[MAX_PATH]{};
        if (::GetCurrentDirectoryW(MAX_PATH, cwd)) {
            std::wstring logPath = std::wstring(cwd) + L"\\debug.log";
            s_logFile = _wfopen(logPath.c_str(), L"a, ccs=UTF-8");
        }
    }
    if (s_logFile) {
        SYSTEMTIME st{};
        ::GetLocalTime(&st);
        fwprintf(s_logFile, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fwprintf(s_logFile, L"[%s] own ribbon: %s\n", pm::brand::k_app_id_w, msg);
        fflush(s_logFile);
    }
}

void SetLayoutErrorFmt(const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(s_lastLayoutError, fmt, ap);
    va_end(ap);
    // Log to file only (avoiding logpanel which can crash).
    LogToDebugFile(s_lastLayoutError);
}

void ClearLayoutError()
{
    s_lastLayoutError[0] = 0;
}

/// TB `NMCUSTOMDRAW::dwItemSpec` is the *command id* (4.70+), not a button *index* — we must
/// `CommandToIndex` before `GetButton` / per-item draw (using it as an index was painting the
/// wrong TBBUTTON and comctl was still default-drawing the real item — double “glow” labels).
static int ItemIndexFromTbCustomDraw(Win32xx::CToolBar& tb, DWORD_PTR dwItemSpec)
{
    const int n = tb.GetButtonCount();
    if (n <= 0) return -1;
    int idx = tb.CommandToIndex(static_cast<UINT>(dwItemSpec));
    if (idx >= 0) return idx;
    if (dwItemSpec < static_cast<DWORD_PTR>(n)) {
        TBBUTTON b{};
        if (tb.GetButton(static_cast<int>(dwItemSpec), b))
            return static_cast<int>(dwItemSpec);
    }
    return -1;
}

COLORREF BlendRgb(COLORREF a, COLORREF b, int weightA255);

bool IsVisuallyDark(COLORREF c)
{
    // Integer luminance approximation; below mid-gray should use the dark custom path.
    return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) < 128000;
}

static bool HasDropdownArrow(const TBBUTTON& b)
{
    return (b.fsStyle & (BTNS_DROPDOWN | BTNS_WHOLEDROPDOWN)) != 0;
}

static int DropdownArrowWidth(const Win32xx::CToolBar& tb)
{
    return tb.DpiScaleInt(RibbonUi::dropdown_arrow_w);
}

static void PaintDropdownArrow(HDC hdc, const CRect& rc, COLORREF fg, const Win32xx::CToolBar& tb)
{
    const Gdiplus::REAL cx = static_cast<Gdiplus::REAL>(rc.left + rc.Width() / 2);
    const Gdiplus::REAL cy = static_cast<Gdiplus::REAL>(rc.top + rc.Height() / 2);
    const Gdiplus::REAL halfW = static_cast<Gdiplus::REAL>(
        (std::max)(RibbonUi::dropdown_half_w_min, tb.DpiScaleInt(RibbonUi::dropdown_half_w)));
    const Gdiplus::REAL halfH = static_cast<Gdiplus::REAL>(
        (std::max)(RibbonUi::dropdown_half_h_min, tb.DpiScaleInt(RibbonUi::dropdown_half_h)));
    const Gdiplus::PointF pts[3] = {
        {cx - halfW, cy - halfH * 0.45f},
        {cx + halfW, cy - halfH * 0.45f},
        {cx, cy + halfH * 0.65f},
    };
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::SolidBrush br(Gdiplus::Color(235, GetRValue(fg), GetGValue(fg), GetBValue(fg)));
    g.FillPolygon(&br, pts, 3);
}

/// comctl’s default label path is skipped — one GDI `DrawTextW` pass. (`dwItemSpec` is command
/// id, not index — see `ItemIndexFromTbCustomDraw`.)
static void PaintOwnDarkLabeledItem(NMTBCUSTOMDRAW* p, Win32xx::CToolBar& tb, Win32xx::CImageList& iml,
                                    COLORREF bg, COLORREF fg, const TBBUTTON& b, bool greyIcon)
{
    HDC         hdc = p->nmcd.hdc;
    const CRect rc  = p->nmcd.rc;
    {
        HBRUSH br = ::CreateSolidBrush(bg);
        (void)::FillRect(hdc, &rc, br);
        (void)::DeleteObject(br);
    }
    const LRESULT stTb = tb.SendMessage(TB_GETSTATE, (WPARAM)b.idCommand, 0);
    const bool checked = (p->nmcd.uItemState & CDIS_CHECKED) != 0 ||
                         (stTb & TBSTATE_CHECKED) != 0;
    const bool pressed = (p->nmcd.uItemState & CDIS_SELECTED) != 0;
    const bool hot = (p->nmcd.uItemState & CDIS_HOT) != 0;
    const bool dropdown = HasDropdownArrow(b);
    if ((checked || pressed || hot) && !greyIcon) {
        const auto& pal = pmui::theme_palette();
        const COLORREF fill = pressed ? BlendRgb(pal.accent, bg, 98)
                            : checked ? BlendRgb(pal.accent, bg, 72)
                                      : BlendRgb(pal.caption_pen, bg, 92);
        CRect stateRc = rc;
        stateRc.DeflateRect(tb.DpiScaleInt(RibbonUi::dark_state_inset_x), tb.DpiScaleInt(RibbonUi::dark_state_inset_y));
        HBRUSH br = ::CreateSolidBrush(fill);
        (void)::FillRect(hdc, &stateRc, br);
        (void)::DeleteObject(br);
        const COLORREF border = pressed || checked ? BlendRgb(pal.accent, bg, 150)
                                                   : BlendRgb(pal.caption_pen, bg, 130);
        HPEN pen = ::CreatePen(PS_SOLID, 1, border);
        if (pen) {
            HGDIOBJ oldPen = ::SelectObject(hdc, pen);
            HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(HOLLOW_BRUSH));
            (void)::Rectangle(hdc, stateRc.left, stateRc.top, stateRc.right, stateRc.bottom);
            (void)::SelectObject(hdc, oldBrush);
            (void)::SelectObject(hdc, oldPen);
            (void)::DeleteObject(pen);
        }
    }
    CSize isz(0, 0);
    if (iml.GetHandle())
        isz = iml.GetIconSize();
    const int padT = tb.DpiScaleInt(RibbonUi::labeled_icon_top);
    const int gap  = tb.DpiScaleInt(RibbonUi::labeled_icon_gap);
    CRect contentRc = rc;
    contentRc.DeflateRect(tb.DpiScaleInt(RibbonUi::content_inset_x), 0);
    if (dropdown)
        contentRc.right -= DropdownArrowWidth(tb);
    if (isz.cx > 0 && isz.cy > 0 && b.iBitmap >= 0) {
        const int ix = contentRc.left + (contentRc.Width() - isz.cx) / 2;
        const int iy = rc.top + padT;
        IMAGELISTDRAWPARAMS idp{};
        idp.cbSize  = sizeof(idp);
        idp.himl    = iml;
        idp.i       = b.iBitmap;
        idp.hdcDst  = hdc;
        idp.x       = ix;
        idp.y       = iy;
        idp.cx      = isz.cx;
        idp.cy      = isz.cy;
        idp.rgbBk   = CLR_NONE;
        idp.fStyle  = (DWORD)ILD_TRANSPARENT;
        idp.fState  = greyIcon ? (UINT)ILS_ALPHA : 0U;
        idp.Frame   = greyIcon ? 135 : 0;
        (void)ImageList_DrawIndirect(&idp);
    }
    if (dropdown) {
        CRect arrowRc(rc.right - DropdownArrowWidth(tb), rc.top, rc.right, rc.bottom);
        PaintDropdownArrow(hdc, arrowRc, fg, tb);
    }
    const CString s = tb.GetButtonText(static_cast<UINT>(b.idCommand));
    if (s.IsEmpty())
        return;
    CRect tband = contentRc;
    tband.top = (isz.cy > 0) ? (rc.top + isz.cy + padT + gap) : (rc.top + padT);
    tband.DeflateRect(tb.DpiScaleInt(RibbonUi::label_inset_x), 0);
    if (tband.top >= tband.bottom)
        return;
    HFONT hf = (HFONT)tb.SendMessage(WM_GETFONT, 0, 0);
    if (!hf)
        return;
    int saved = ::SaveDC(hdc);
    (void)::SelectObject(hdc, hf);
    (void)::SetBkMode(hdc, TRANSPARENT);
    (void)::SetTextColor(hdc, fg);
    CRect        calc = tband;
    (void)::DrawTextW(
        hdc, (LPCWSTR)s, -1, &calc,
        UINT(DT_LEFT | DT_TOP | DT_CALCRECT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX));
    if (calc.Width() > 0 && calc.Height() > 0) {
        const int x = tband.left + (tband.Width() - calc.Width()) / 2;
        const int y = tband.top + (tband.Height() - calc.Height()) / 2;
        CRect     d(x, y, x + calc.Width(), y + calc.Height());
        d.right = (std::min)(d.right, tband.right);
        d.bottom = (std::min)(d.bottom + tb.DpiScaleInt(RibbonUi::label_bottom_slack), tband.bottom);
        (void)::DrawTextW(
            hdc, (LPCWSTR)s, -1, &d,
            UINT(DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX));
    }
    (void)::RestoreDC(hdc, saved);
}

static void PaintOwnDarkIconItem(NMTBCUSTOMDRAW* p, Win32xx::CToolBar& tb, Win32xx::CImageList& iml,
                                 COLORREF bg, const TBBUTTON& b, bool greyIcon)
{
    HDC         hdc = p->nmcd.hdc;
    const CRect rc  = p->nmcd.rc;
    const LRESULT stTb = tb.SendMessage(TB_GETSTATE, (WPARAM)b.idCommand, 0);
    {
        HBRUSH br = ::CreateSolidBrush(bg);
        (void)::FillRect(hdc, &rc, br);
        (void)::DeleteObject(br);
    }

    CSize isz(0, 0);
    if (iml.GetHandle())
        isz = iml.GetIconSize();
    if (isz.cx <= 0 || isz.cy <= 0 || b.iBitmap < 0)
        return;

    const int rcW = static_cast<int>(rc.Width());
    const int rcH = static_cast<int>(rc.Height());
    const int iconW = static_cast<int>(isz.cx);
    const bool dropdown = HasDropdownArrow(b);
    const int arrowW = dropdown ? DropdownArrowWidth(tb) : 0;
    const int contentW = (std::max)(1, rcW - arrowW);
    const int visual = (std::max)(iconW, (std::min)(contentW, tb.DpiScaleInt(RibbonUi::icon_visual_max)));
    CRect visualRc(
        rc.left + (contentW - visual) / 2,
        rc.top + (rcH - visual) / 2,
        rc.left + (contentW - visual) / 2 + visual,
        rc.top + (rcH - visual) / 2 + visual);

    const bool checked = (p->nmcd.uItemState & CDIS_SELECTED) != 0 ||
                         (p->nmcd.uItemState & CDIS_CHECKED) != 0 ||
                         (stTb & TBSTATE_CHECKED) != 0;
    const bool hot = (p->nmcd.uItemState & CDIS_HOT) != 0;
    if (checked || hot) {
        const auto& pal = pmui::theme_palette();
        const COLORREF fill = checked ? BlendRgb(pal.accent, bg, 72) : BlendRgb(pal.caption_pen, bg, 100);
        HBRUSH br = ::CreateSolidBrush(fill);
        (void)::FillRect(hdc, &visualRc, br);
        (void)::DeleteObject(br);
    }

    IMAGELISTDRAWPARAMS idp{};
    idp.cbSize = sizeof(idp);
    idp.himl   = iml;
    idp.i      = b.iBitmap;
    idp.hdcDst = hdc;
    idp.x      = visualRc.left + (visualRc.Width() - isz.cx) / 2;
    idp.y      = visualRc.top + (visualRc.Height() - isz.cy) / 2;
    idp.cx     = isz.cx;
    idp.cy     = isz.cy;
    idp.rgbBk  = CLR_NONE;
    idp.fStyle = (DWORD)ILD_TRANSPARENT;
    idp.fState = greyIcon ? (UINT)ILS_ALPHA : 0U;
    idp.Frame  = greyIcon ? 135 : 0;
    (void)ImageList_DrawIndirect(&idp);
    if (dropdown) {
        CRect arrowRc(rc.right - arrowW, rc.top, rc.right, rc.bottom);
        const COLORREF arrowFg = greyIcon ? pmui::theme_palette().caption_fg_inactive : pmui::theme_palette().window_fg;
        PaintDropdownArrow(hdc, arrowRc, arrowFg, tb);
    }
}

static void PaintOwnDarkSeparator(NMTBCUSTOMDRAW* p, COLORREF bg)
{
    HDC         hdc = p->nmcd.hdc;
    const CRect rc  = p->nmcd.rc;
    {
        HBRUSH br = ::CreateSolidBrush(bg);
        (void)::FillRect(hdc, &rc, br);
        (void)::DeleteObject(br);
    }

    const auto& pal = pmui::theme_palette();
    const COLORREF line = BlendRgb(pal.caption_pen, bg, 72);
    HPEN pen = ::CreatePen(PS_SOLID, 1, line);
    if (!pen)
        return;
    HGDIOBJ oldPen = ::SelectObject(hdc, pen);
    const int x = rc.left + rc.Width() / 2;
    const int padY = (std::max)(1, (rc.Height() - 22) / 2);
    (void)::MoveToEx(hdc, x, rc.top + padY, nullptr);
    (void)::LineTo(hdc, x, rc.bottom - padY);
    (void)::SelectObject(hdc, oldPen);
    (void)::DeleteObject(pen);
}

COLORREF BlendRgb(COLORREF a, COLORREF b, int weightA255)
{
    const int w = (weightA255 < 0) ? 0 : (weightA255 > 255 ? 255 : weightA255);
    const int wb = 255 - w;
    return RGB((GetRValue(a) * w + GetRValue(b) * wb) / 255, (GetGValue(a) * w + GetGValue(b) * wb) / 255,
               (GetBValue(a) * w + GetBValue(b) * wb) / 255);
}

/// Softer than raw strip/window `fg` — calmer on dark; matches the custom “inactive” look family.
static COLORREF SoftStripLabelFg(const pmui::ThemePalette& pal, COLORREF normal)
{
    if (!pal.dark) return normal;
    return BlendRgb(normal, pal.caption_fg_inactive, 70);
}

HBITMAP LoadRibbonBmpScaled(HINSTANCE inst, UINT resId, int targetPx)
{
    HBITMAP raw = (HBITMAP)::LoadImageW(inst, MAKEINTRESOURCEW(resId), IMAGE_BITMAP, 0, 0,
                                       LR_CREATEDIBSECTION | LR_DEFAULTCOLOR);
    if (!raw)
        return nullptr;

    Win32xx::CBitmap bm(raw);
    CSize sz = bm.GetSize();
    if (sz.cx <= 0 || sz.cy <= 0) {
        ::DeleteObject(raw);
        return nullptr;
    }

    if (sz.cx == targetPx && sz.cy == targetPx)
        return raw;

    Win32xx::CMemDC src(nullptr);
    src.SelectObject(bm);
    BITMAP bmd = bm.GetBitmapData();
    bmd.bmWidth  = targetPx;
    bmd.bmHeight = targetPx;
    Win32xx::CBitmapInfoPtr pbmi(bmd);
    Win32xx::CMemDC dst(nullptr);
    void* bits = nullptr;
    dst.CreateDIBSection(dst, pbmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    dst.StretchBlt(0, 0, targetPx, targetPx, src, 0, 0, sz.cx, sz.cy, SRCCOPY);
    Win32xx::CBitmap outBm = dst.DetachBitmap();
    HBITMAP              out = static_cast<HBITMAP>(outBm.Detach());
    ::DeleteObject(raw);
    return out;
}

HBITMAP LoadLayoutItemBitmap(const LayoutItem& it, HINSTANCE inst, int targetPx)
{
    if (targetPx <= 0)
        return nullptr;
#ifdef FEATURE_SVG_BUTTONS
    if (it.svgFilePathW && it.svgFilePathW[0] != 0) {
        const auto pal = pmui::theme_palette();
        const COLORREF tr = (it.svgTint != CLR_NONE) ? it.svgTint : pal.accent;
        return pmui_svg_rasterize_file_wide(
            it.svgFilePathW, targetPx, GetRValue(tr), GetGValue(tr), GetBValue(tr));
    }
#endif
    if (it.imageResId == 0)
        return nullptr;
    return LoadRibbonBmpScaled(inst, it.imageResId, targetPx);
}

/// When the strip uses a large slot but an item wants a small glyph, center the scaled bitmap
/// on a square tile so it reads smaller than the main commands.
HBITMAP PadBitmapCentered(HBITMAP scaledContent, int slotPx, COLORREF padFill)
{
    if (!scaledContent || slotPx <= 0)
        return nullptr;

    Win32xx::CBitmap bm(scaledContent);
    CSize sz = bm.GetSize();
    if (sz.cx <= 0 || sz.cy <= 0)
        return nullptr;
    if (sz.cx == slotPx && sz.cy == slotPx)
        return static_cast<HBITMAP>(bm.Detach());

    Win32xx::CMemDC src(nullptr);
    src.SelectObject(bm);
    BITMAP bmd = bm.GetBitmapData();
    bmd.bmWidth  = slotPx;
    bmd.bmHeight = slotPx;
    Win32xx::CBitmapInfoPtr pbmi(bmd);
    Win32xx::CMemDC dst(nullptr);
    void* bits = nullptr;
    dst.CreateDIBSection(dst, pbmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    dst.SolidFill(padFill, CRect(0, 0, slotPx, slotPx));
    const int x = (slotPx - sz.cx) / 2;
    const int y = (slotPx - sz.cy) / 2;
    dst.BitBlt(x, y, sz.cx, sz.cy, static_cast<HDC>(src), 0, 0, SRCCOPY);
    Win32xx::CBitmap outBm = dst.DetachBitmap();
    return static_cast<HBITMAP>(outBm.Detach());
}

} // namespace

const wchar_t* LastRibbonLayoutErrorW()
{
    return s_lastLayoutError;
}

int PreferredStripHeight(const Win32xx::CWnd& dpiRef)
{
    // Conservative fallback before ApplyLayout runs; actual strip uses [`MeasuredStripHeight`].
    const int icon = dpiRef.DpiScaleInt(RibbonUi::icon_large);
    const int text = dpiRef.DpiScaleInt(RibbonUi::fallback_text_h);
    const int pad  = dpiRef.DpiScaleInt(RibbonUi::fallback_pad);
    return pad + icon + text;
}

int RibbonChromeTopInset(const Win32xx::CWnd& dpiRef)
{
    return dpiRef.DpiScaleInt(5);
}

int RibbonChromeBottomInset(const Win32xx::CWnd& dpiRef)
{
    return dpiRef.DpiScaleInt(6);
}

int RibbonChromeBottomRule(const Win32xx::CWnd& dpiRef)
{
    return (std::max)(1, dpiRef.DpiScaleInt(1));
}

void COwnRibbonToolStrip::ClearLayout()
{
    for (HBITMAP h : m_ownedBmps) {
        if (h)
            ::DeleteObject(h);
    }
    m_hResourceModule = nullptr;
    m_ownedBmps.clear();
    m_runtimeDisabled.clear();
    m_layoutItems = nullptr;
    if (m_haveToolbar && m_toolbar.IsWindow()) {
        while (m_toolbar.GetButtonCount() > 0)
            m_toolbar.DeleteButton(0);
    }
    if (m_images.GetHandle())
        m_images.Destroy();
}

bool COwnRibbonToolStrip::ApplyLayout(HINSTANCE inst, const Win32xx::CWnd& dpiRef,
                                      const std::vector<LayoutItem>& items)
{
    ClearLayoutError();

    if (!m_haveToolbar || !m_toolbar.IsWindow()) {
        SetLayoutErrorFmt(L"Toolbar control is not ready (pager/toolbar not created).");
        return false;
    }

    ClearLayout();
    m_layoutItems   = &items;
    m_hResourceModule = inst;

    const int pxLarge = dpiRef.DpiScaleInt(RibbonUi::icon_large);
    const int pxSmall = dpiRef.DpiScaleInt(RibbonUi::icon_small);

    bool haveLargeIcon = false;
    for (const LayoutItem& it : items) {
        if (it.cmdId == 0)
            continue;
        if (it.icon != IconPresentation::Small16)
            haveLargeIcon = true;
    }
    const int iconDim = haveLargeIcon ? pxLarge : pxSmall;

    int nIcons = 0;
    for (const LayoutItem& it : items) {
        if (it.cmdId != 0)
            ++nIcons;
    }
    if (nIcons <= 0) {
        SetLayoutErrorFmt(L"No toolbar icons defined in layout.");
        ClearLayout();
        return false;
    }

    m_images.Create(iconDim, iconDim, ILC_COLOR32, nIcons, nIcons + 4);

    const COLORREF padFill = pmui::theme_palette().bar;

    for (const LayoutItem& it : items) {
        if (it.cmdId == 0)
            continue;
        const int nativePx = (it.icon == IconPresentation::Small16) ? pxSmall : pxLarge;
        HBITMAP h = nullptr;
#ifdef FEATURE_SVG_BUTTONS
        if (it.svgFilePathW && it.svgFilePathW[0] != 0) {
            h = LoadLayoutItemBitmap(it, inst, nativePx);
        } else
#endif
        {
            h = LoadRibbonBmpScaled(inst, it.imageResId, nativePx);
        }
        if (h && iconDim > nativePx)
            h = PadBitmapCentered(h, iconDim, padFill);
        if (!h) {
#ifdef FEATURE_SVG_BUTTONS
            if (it.svgFilePathW && it.svgFilePathW[0] != 0) {
                SetLayoutErrorFmt(L"SVG raster failed for command id %u (file).", static_cast<unsigned>(it.cmdId));
            } else
#endif
            {
                SetLayoutErrorFmt(L"Load/scaling failed for command id %u (resource id %u); target %d px (slot %d px).",
                                  static_cast<unsigned>(it.cmdId), static_cast<unsigned>(it.imageResId), nativePx, iconDim);
            }
            ClearLayout();
            return false;
        }
        if (m_images.Add(h) < 0) {
            SetLayoutErrorFmt(L"ImageList_Add failed for command id %u (resource id %u).", static_cast<unsigned>(it.cmdId),
                              static_cast<unsigned>(it.imageResId));
            ::DeleteObject(h);
            ClearLayout();
            return false;
        }
        m_ownedBmps.push_back(h);
    }

    m_images.SetBkColor(CLR_NONE);
    m_toolbar.SetImageList(m_images);
    m_toolbar.SetBitmapSize(iconDim, iconDim);

    // No TBSTYLE_LIST — labels render *below* bitmaps (standard toolbar).
    DWORD style = m_toolbar.GetStyle();
    style &= ~static_cast<DWORD>(TBSTYLE_LIST);
    style |= TBSTYLE_TOOLTIPS | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | CCS_NORESIZE | CCS_TOP
        | CCS_NOPARENTALIGN | CCS_NODIVIDER;
    m_toolbar.SetStyle(style);
    // `TBSTYLE_EX_MIXEDBUTTONS` + `BTNS_AUTOSIZE` — per-button width from that button’s text
    // (without this, comctl uses one width for the whole bar → huge gaps next to short labels).
    m_toolbar.SetExtendedStyle(TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS);
    m_toolbar.SetMaxTextRows(1);
    const int padX = dpiRef.DpiScaleInt(RibbonUi::toolbar_pad_x);
    const int padY = dpiRef.DpiScaleInt(RibbonUi::toolbar_pad_y);
    m_toolbar.SetPadding(padX, padY);
    m_toolbar.SetIndent(dpiRef.DpiScaleInt(RibbonUi::toolbar_indent));

    int imgN = 0;
    for (const LayoutItem& it : items) {
        if (it.cmdId == 0) {
            m_toolbar.AddButton(0);
            continue;
        }
        m_toolbar.AddButton(it.cmdId, it.enabled ? TRUE : FALSE, imgN);
        ++imgN;
    }

    for (const LayoutItem& it : items) {
        if (it.cmdId == 0)
            continue;
        wchar_t resBuf[512]{};
        const wchar_t* labelToSet = nullptr;
        if (it.labelStringId != 0) {
            if (::LoadStringW(inst, it.labelStringId, resBuf, _countof(resBuf)) > 0)
                labelToSet = resBuf;
        }
        if (!labelToSet)
            labelToSet = (it.label && it.label[0] != 0) ? it.label : nullptr;
        if (it.showLabel && labelToSet)
            m_toolbar.SetButtonText(it.cmdId, labelToSet);
        BYTE fs = it.isDropdown ? BTNS_WHOLEDROPDOWN : BTNS_BUTTON;
        if (it.isToggle)
            fs = BTNS_CHECK;
        if (it.showLabel && labelToSet)
            fs = static_cast<BYTE>(fs | BTNS_SHOWTEXT | BTNS_AUTOSIZE);
        m_toolbar.SetButtonStyle(it.cmdId, fs);
        if (!it.showLabel) {
            TBBUTTONINFO bi{};
            bi.cbSize = sizeof(bi);
            bi.dwMask = TBIF_SIZE;
            bi.cx     = static_cast<WORD>(dpiRef.DpiScaleInt(RibbonUi::icon_only_button_w));
            (void)m_toolbar.SendMessage(TB_SETBUTTONINFO, it.cmdId, reinterpret_cast<LPARAM>(&bi));
        }
    }

    m_toolbar.Autosize();
    for (const LayoutItem& it : items) {
        if (it.cmdId == 0 || it.showLabel)
            continue;
        TBBUTTONINFO bi{};
        bi.cbSize = sizeof(bi);
        bi.dwMask = TBIF_SIZE;
        bi.cx     = static_cast<WORD>(dpiRef.DpiScaleInt(RibbonUi::icon_only_button_w));
        (void)m_toolbar.SendMessage(TB_SETBUTTONINFO, it.cmdId, reinterpret_cast<LPARAM>(&bi));
    }
    RecalcLayout();
    ::InvalidateRect(*this, nullptr, TRUE);
    return true;
}

int COwnRibbonToolStrip::MeasuredStripHeight()
{
    if (!m_toolbar.IsWindow())
        return 0;
    // Height is intrinsic to the toolbar; do not use full RecalcLayout() here — before the
    // chrome host is sized, the pager can still have cy==0 and would distort measurements.
    m_toolbar.Autosize();
    const int cy = m_toolbar.GetMaxSize().cy;
    return (cy > 0) ? cy : PreferredStripHeight(*this);
}

void COwnRibbonToolStrip::RecalcLayout()
{
    if (!m_pager.IsWindow() || !m_toolbar.IsWindow())
        return;
    m_toolbar.Autosize();
    CRect rc = GetClientRect();
    const int ph = m_toolbar.GetMaxSize().cy;
    m_pager.SetWindowPos(nullptr, 0, 0, rc.Width(), (std::max)(ph, 1), SWP_NOZORDER | SWP_SHOWWINDOW);
    if (m_images.GetHandle())
        m_pager.SetButtonSize(m_toolbar.GetButtonSize().cx / 2);
    m_pager.RecalcSize();
}

const LayoutItem* COwnRibbonToolStrip::FindLayoutItem(UINT cmdId) const
{
    if (!m_layoutItems)
        return nullptr;
    for (const LayoutItem& it : *m_layoutItems) {
        if (it.cmdId == cmdId)
            return &it;
    }
    return nullptr;
}

bool COwnRibbonToolStrip::IsRuntimeEnabled(const LayoutItem& item) const
{
    if (!item.enabled)
        return false;
    return std::find(m_runtimeDisabled.begin(), m_runtimeDisabled.end(), item.cmdId) == m_runtimeDisabled.end();
}

bool COwnRibbonToolStrip::ShowDropdownForItem(const LayoutItem& item)
{
    if (!item.dropdownItems || !m_toolbar.IsWindow())
        return false;
    HMENU hMenu = ::CreatePopupMenu();
    if (!hMenu)
        return false;
    HINSTANCE h = m_hResourceModule ? m_hResourceModule : ::GetModuleHandleW(nullptr);
    std::vector<HBITMAP> menuBmps;
    for (const LayoutItem& child : *item.dropdownItems) {
        if (child.cmdId == 0) {
            ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        wchar_t       resBuf[512]{};
        const wchar_t* text = nullptr;
        if (child.labelStringId != 0 && ::LoadStringW(h, child.labelStringId, resBuf, _countof(resBuf)) > 0)
            text = resBuf;
        if (!text)
            text = (child.label && child.label[0] != 0) ? child.label : L"(command)";
        const bool enabled = IsRuntimeEnabled(child);
        ::AppendMenuW(hMenu, MF_STRING | (enabled ? 0 : MF_DISABLED | MF_GRAYED), child.cmdId, text);
        if (HBITMAP hbmp = LoadLayoutItemBitmap(child, h, DpiScaleInt(RibbonUi::menu_icon))) {
            MENUITEMINFOW mi{};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_BITMAP;
            mi.hbmpItem = hbmp;
            if (::SetMenuItemInfoW(hMenu, child.cmdId, FALSE, &mi))
                menuBmps.push_back(hbmp);
            else
                ::DeleteObject(hbmp);
        }
    }
    CRect rc{};
    (void)m_toolbar.SendMessage(TB_GETRECT, item.cmdId, reinterpret_cast<LPARAM>(&rc));
    POINT pt{rc.left, rc.bottom};
    ::MapWindowPoints(m_toolbar, HWND_DESKTOP, &pt, 1);
    const UINT choice = ::TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                         pt.x, pt.y, 0, *this, nullptr);
    ::DestroyMenu(hMenu);
    for (HBITMAP hbmp : menuBmps)
        ::DeleteObject(hbmp);
    if (choice != 0)
        ForwardCommandToRoot(MAKEWPARAM(choice, 0), 0);
    return true;
}

void COwnRibbonToolStrip::ForwardCommandToRoot(WPARAM wp, LPARAM lp)
{
    const UINT cmdId = LOWORD(wp);
    if (const LayoutItem* it = FindLayoutItem(cmdId)) {
        if (it->isDropdown && it->dropdownItems && ShowDropdownForItem(*it))
            return;
    }
    HWND root = ::GetAncestor(*this, GA_ROOT);
    if (root)
        ::SendMessageW(root, WM_COMMAND, wp, lp);
}

void COwnRibbonToolStrip::ApplyChrome(bool dark, COLORREF stripBg, COLORREF stripFg)
{
    if (!IsWindow())
        return;
    m_stripBg = stripBg;
    m_stripFg = stripFg;
    if (m_toolbar.IsWindow()) {
        // `DarkMode_Explorer` on Toolbars often pins label ink to near-black; stripping sub-app id
        // makes `NMTBCUSTOMDRAW::clrText` + our erase-bkgnd colours reliable in dark UI.
        if (dark)
            ::SetWindowTheme(m_toolbar, L"", L"");
        else
            ::SetWindowTheme(m_toolbar, L"Explorer", nullptr);
    }
    if (m_pager.IsWindow()) {
        m_pager.SetBkColor(stripBg);
        if (dark)
            ::SetWindowTheme(m_pager, L"", L"");
        else
            ::SetWindowTheme(m_pager, L"Explorer", nullptr);
        ::InvalidateRect(m_pager, nullptr, TRUE);
    }
    if (m_toolbar.IsWindow())
        ::InvalidateRect(m_toolbar, nullptr, TRUE);
    ::InvalidateRect(*this, nullptr, TRUE);
}

BOOL COwnRibbonToolStrip::OnEraseBkgnd(CDC& dc)
{
    CRect          rc = GetClientRect();
    const COLORREF bg =
        (m_stripBg != CLR_DEFAULT) ? m_stripBg : pmui::theme_palette().window_bg;
    dc.SolidFill(bg, rc);
    return TRUE;
}

void COwnRibbonToolStrip::SyncToggle(UINT cmdId, bool on)
{
    if (m_toolbar.IsWindow())
        m_toolbar.CheckButton(cmdId, on ? TRUE : FALSE);
}

void COwnRibbonToolStrip::SyncEnabled(UINT cmdId, BOOL enable)
{
    auto it = std::find(m_runtimeDisabled.begin(), m_runtimeDisabled.end(), cmdId);
    if (enable) {
        if (it != m_runtimeDisabled.end())
            m_runtimeDisabled.erase(it);
    } else if (it == m_runtimeDisabled.end()) {
        m_runtimeDisabled.push_back(cmdId);
    }
    if (m_toolbar.IsWindow() && m_toolbar.CommandToIndex(cmdId) >= 0)
        m_toolbar.EnableButton(cmdId, enable);
}

void COwnRibbonToolStrip::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
}

int COwnRibbonToolStrip::OnCreate(CREATESTRUCT&)
{
    if (!m_pager.Create(*this))
        return -1;
    DWORD ps = m_pager.GetStyle() | PGS_HORZ;
    m_pager.SetStyle(ps);
    // FALSE: do not forward wheel / hover-scroll to the pager — avoids the strip
    // jumping horizontally when clicking toolbar buttons near the overflow edges.
    m_pager.ForwardMouse(FALSE);
    m_pager.SetBorder(0);

    if (!m_toolbar.Create(m_pager))
        return -1;

    DWORD ts = m_toolbar.GetStyle();
    ts &= ~static_cast<DWORD>(TBSTYLE_LIST);
    ts |= CCS_NORESIZE | CCS_TOP | CCS_NOPARENTALIGN | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_TRANSPARENT
        | CCS_NODIVIDER;
    m_toolbar.SetStyle(ts);
    m_toolbar.SetExtendedStyle(TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DRAWDDARROWS);

    m_pager.SetChild(m_toolbar.GetHwnd());
    m_haveToolbar = true;
    return 0;
}

void COwnRibbonToolStrip::OnDestroy()
{
    ClearLayout();
    m_haveToolbar = false;
    CWnd::OnDestroy();
}

LRESULT COwnRibbonToolStrip::OnNotify(WPARAM wparam, LPARAM lparam)
{
    LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lparam);
    if (hdr && hdr->code == PGN_CALCSIZE) {
        auto* cs = reinterpret_cast<NMPGCALCSIZE*>(lparam);
        if (cs->dwFlag == PGF_CALCWIDTH && m_toolbar.IsWindow())
            cs->iWidth = m_toolbar.GetMaxSize().cx;
    }
    if (hdr && hdr->code == PGN_SCROLL) {
        auto* sc = reinterpret_cast<NMPGSCROLL*>(lparam);
        if (m_toolbar.IsWindow())
            sc->iScroll = m_toolbar.GetButtonSize().cx;
    }
    if (hdr && hdr->hwndFrom == m_toolbar && hdr->code == TBN_DROPDOWN && m_layoutItems) {
        auto* ntb = reinterpret_cast<NMTOOLBARW*>(lparam);
        if (const LayoutItem* it = FindLayoutItem(static_cast<UINT>(ntb->iItem))) {
            (void)ShowDropdownForItem(*it);
            return TBDDRET_NODEFAULT;
        }
    }
    if (hdr && hdr->hwndFrom == m_toolbar && hdr->code == TBN_GETINFOTIP && m_layoutItems) {
        auto*       git  = reinterpret_cast<NMTBGETINFOTIPW*>(lparam);
        HINSTANCE   h    = m_hResourceModule ? m_hResourceModule : ::GetModuleHandleW(nullptr);
        for (const LayoutItem& it : *m_layoutItems) {
            if (it.cmdId == 0 || static_cast<UINT>(git->iItem) != it.cmdId)
                continue;
            if (!git->pszText || git->cchTextMax <= 1)
                break;
            if (it.tooltipStringId != 0) {
                if (::LoadStringW(h, it.tooltipStringId, git->pszText, git->cchTextMax) > 0)
                    break;
            }
            if (it.tooltip && it.tooltip[0] != 0) {
                ::wcsncpy_s(git->pszText, static_cast<size_t>(git->cchTextMax), it.tooltip, _TRUNCATE);
                break;
            }
            if (it.labelStringId != 0) {
                (void)::LoadStringW(h, it.labelStringId, git->pszText, git->cchTextMax);
                break;
            }
            if (it.label && it.label[0] != 0) {
                ::wcsncpy_s(git->pszText, static_cast<size_t>(git->cchTextMax), it.label, _TRUNCATE);
            }
            break;
        }
    }
    if (hdr && hdr->hwndFrom == m_toolbar && hdr->code == NM_CUSTOMDRAW) {
        auto*      tbcd = reinterpret_cast<NMTBCUSTOMDRAW*>(lparam);
        const UINT st   = tbcd->nmcd.dwDrawStage;
        const auto& pal = pmui::theme_palette();
        const COLORREF normal =
            (m_stripFg != CLR_DEFAULT) ? m_stripFg : pal.window_fg;
        const COLORREF stripBg =
            (m_stripBg != CLR_DEFAULT) ? m_stripBg : pal.window_bg;
        const bool useDarkDraw = pal.dark || IsVisuallyDark(stripBg);
        switch (st) {
        case CDDS_PREPAINT: {
            if (useDarkDraw) {
                const CRect cr = m_toolbar.GetClientRect();
                Win32xx::CDC  dc(tbcd->nmcd.hdc);
                dc.SolidFill(stripBg, cr);
            }
            return CDRF_NOTIFYITEMDRAW;
        }
        case CDDS_ITEMPREPAINT: {
            const int idx = ItemIndexFromTbCustomDraw(m_toolbar, tbcd->nmcd.dwItemSpec);
            if (idx < 0) return CDRF_DODEFAULT;
            TBBUTTON btn = {};
            if (!m_toolbar.GetButton(idx, btn)) return CDRF_DODEFAULT;
            if (btn.fsStyle & TBSTYLE_SEP) {
                if (useDarkDraw) {
                    PaintOwnDarkSeparator(tbcd, stripBg);
                    return CDRF_SKIPDEFAULT;
                }
                const COLORREF sep = BlendRgb(pal.caption_pen, pal.bar, 70);
                tbcd->clrBtnFace            = sep;
                tbcd->clrHighlightHotTrack = sep;
                return CDRF_DODEFAULT;
            }
            const LRESULT  stTb = m_toolbar.SendMessage(TB_GETSTATE, (WPARAM)btn.idCommand, 0);
            const bool     off  = (stTb & TBSTATE_ENABLED) == 0;
            const bool     dis  = (tbcd->nmcd.uItemState & CDIS_DISABLED) != 0;
            const bool     inactive = dis || off;
            const COLORREF labelFg  = inactive ? pal.caption_fg_inactive : SoftStripLabelFg(pal, normal);
            tbcd->clrText  = labelFg;
            if ((btn.fsStyle & BTNS_SHOWTEXT) == 0) {
                PaintOwnDarkIconItem(tbcd, m_toolbar, m_images, stripBg, btn, inactive);
                return CDRF_SKIPDEFAULT;
            }
            if (useDarkDraw) {
                PaintOwnDarkLabeledItem(tbcd, m_toolbar, m_images, stripBg, labelFg, btn, inactive);
                return CDRF_SKIPDEFAULT;
            }
            return CDRF_DODEFAULT;
        }
        default:
            break;
        }
    }
    (void)wparam;
    return CWnd::OnNotify(wparam, lparam);
}

LRESULT COwnRibbonToolStrip::OnSize(WPARAM, LPARAM)
{
    RecalcLayout();
    return 0;
}

void COwnRibbonToolStrip::OnDpiChanged(UINT, WPARAM, LPARAM)
{
    const std::vector<LayoutItem>* items = m_layoutItems;
    HINSTANCE inst = m_hResourceModule;
    if (items && inst && m_toolbar.IsWindow()) {
        (void)ApplyLayout(inst, *this, *items);
        return;
    }
    RecalcLayout();
}

LRESULT COwnRibbonToolStrip::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_SIZE: return OnSize(wparam, lparam);
    case WM_COMMAND: ForwardCommandToRoot(wparam, lparam); return 0;
    case WM_NOTIFY: return OnNotify(wparam, lparam);
    case WM_DPICHANGED_AFTERPARENT: OnDpiChanged(msg, wparam, lparam); return 0;
    }
    return CWnd::WndProcDefault(msg, wparam, lparam);
}

} // namespace own_ribbon

#endif // FEATURE_USE_OWN_RIBBON
