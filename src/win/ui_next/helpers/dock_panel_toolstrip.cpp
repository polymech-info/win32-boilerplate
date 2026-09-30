#include "stdafx.h"
#include "helpers/dock_panel_toolstrip.h"
#include "helpers/theme.hpp"
#include "constants.hpp"

#include <algorithm>

#ifndef TBSTYLE_TRANSPARENT
#define TBSTYLE_TRANSPARENT 0x8000
#endif
#ifndef BTNS_SHOWTEXT
#define BTNS_SHOWTEXT 0x00000040
#endif
#ifndef BTNS_AUTOSIZE
#define BTNS_AUTOSIZE 0x00000010
#endif

namespace pmui {

int CDockPanelToolStrip::HairlinePx() const
{
    return (std::max)(1, DpiScaleInt(1));
}

void CDockPanelToolStrip::ReleaseImageList()
{
    if (m_il.GetHandle())
        m_il.Destroy();
}

HBITMAP CDockPanelToolStrip::LoadGlyphScaled(HINSTANCE inst, UINT resId, int targetPx) const
{
    HBITMAP raw = (HBITMAP)::LoadImageW(inst, MAKEINTRESOURCEW(resId), IMAGE_BITMAP, 0, 0,
                                        LR_CREATEDIBSECTION | LR_DEFAULTCOLOR);
    if (!raw)
        return nullptr;

    CBitmap bm(raw);
    CSize   sz = bm.GetSize();
    if (sz.cx <= 0 || sz.cy <= 0) {
        ::DeleteObject(raw);
        return nullptr;
    }
    if (sz.cx == targetPx && sz.cy == targetPx)
        return raw;

    CMemDC src(nullptr);
    src.SelectObject(bm);
    BITMAP bmd = bm.GetBitmapData();
    bmd.bmWidth  = targetPx;
    bmd.bmHeight = targetPx;
    CBitmapInfoPtr pbmi(bmd);
    CMemDC           dst(nullptr);
    void*            bits = nullptr;
    dst.CreateDIBSection(dst, pbmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    dst.StretchBlt(0, 0, targetPx, targetPx, src, 0, 0, sz.cx, sz.cy, SRCCOPY);
    CBitmap outBm = dst.DetachBitmap();
    HBITMAP out   = static_cast<HBITMAP>(outBm.Detach());
    ::DeleteObject(raw);
    return out;
}

void CDockPanelToolStrip::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
}

int CDockPanelToolStrip::OnCreate(CREATESTRUCT&)
{
    if (!m_tb.Create(*this))
        return -1;
    DWORD ts = m_tb.GetStyle();
    ts &= ~static_cast<DWORD>(TBSTYLE_LIST);
    ts |= CCS_NORESIZE | CCS_TOP | CCS_NOPARENTALIGN | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS
        | TBSTYLE_TRANSPARENT | CCS_NODIVIDER;
    m_tb.SetStyle(ts);
    m_tb.SetExtendedStyle(TBSTYLE_EX_HIDECLIPPEDBUTTONS | TBSTYLE_EX_MIXEDBUTTONS);
    m_tb.SetMaxTextRows(1);
    const int padX = DpiScaleInt(2);
    m_tb.SetPadding(padX, 0);
    m_tb.SetIndent(DpiScaleInt(2));
    return 0;
}

void CDockPanelToolStrip::OnDestroy()
{
    ReleaseImageList();
    CWnd::OnDestroy();
}

bool CDockPanelToolStrip::ApplyButtons(HINSTANCE resourceInst, const CWnd& dpiRef,
                                       const std::vector<DockPanelToolBtn>& items)
{
    if (!m_tb.IsWindow())
        return false;
    ReleaseImageList();
    m_items        = items;
    m_resourceInst = resourceInst;

    while (m_tb.GetButtonCount() > 0)
        m_tb.DeleteButton(0);

    const int iconPx = dpiRef.DpiScaleInt(16);
    int       imgN   = 0;
    for (const auto& it : items) {
        if (it.cmdId == 0) {
            m_tb.AddButton(0);
            continue;
        }
        HBITMAP hb = LoadGlyphScaled(resourceInst, it.imageResId, iconPx);
        if (!hb)
            return false;
        CBitmap bmap(hb);
        if (m_il.GetHandle() == nullptr) {
            m_il.Create(iconPx, iconPx, ILC_COLOR32 | ILC_MASK, 4, 4);
            if (m_il.GetHandle() == nullptr)
                return false;
            m_il.SetBkColor(CLR_NONE);
        }
        m_il.Add(bmap, RGB(192, 192, 192));
        bmap.Detach();
        ::DeleteObject(hb);
        m_tb.AddButton(it.cmdId, TRUE, imgN++);
        m_tb.SetButtonStyle(it.cmdId, BTNS_BUTTON);
    }

    m_tb.SetImageList(m_il);
    m_tb.SetBitmapSize(iconPx, iconPx);

    wchar_t labelBuf[256]{};
    for (const auto& it : items) {
        if (it.cmdId == 0)
            continue;
        if (!it.showLabel)
            continue;
        const wchar_t* label = nullptr;
        if (it.labelStringId != 0) {
            if (::LoadStringW(resourceInst, it.labelStringId, labelBuf, static_cast<int>(std::size(labelBuf))) > 0)
                label = labelBuf;
        }
        if (!label)
            label = it.labelFallback;
        if (label && label[0]) {
            m_tb.SetButtonText(it.cmdId, label);
            m_tb.SetButtonStyle(it.cmdId,
                static_cast<BYTE>(BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE));
        }
    }

    m_tb.Autosize();
    m_applied = true;

    CRect rc = GetClientRect();
    if (rc.Height() < IdealHeight())
        SetWindowPos(nullptr, 0, 0, rc.Width(), IdealHeight(), SWP_NOMOVE | SWP_NOZORDER);
    return true;
}

void CDockPanelToolStrip::ApplyTheme(bool dark, COLORREF stripBg)
{
    m_dark    = dark;
    m_stripBg = stripBg;
    if (!IsWindow())
        return;
    if (m_tb.IsWindow()) {
        if (dark)
            ::SetWindowTheme(m_tb, L"", L"");
        else
            ::SetWindowTheme(m_tb, L"Explorer", nullptr);
        ::InvalidateRect(m_tb, nullptr, TRUE);
    }
    ::InvalidateRect(*this, nullptr, TRUE);
}

void CDockPanelToolStrip::RebuildForCurrentDpi()
{
    if (!m_applied || !m_resourceInst || m_items.empty())
        return;
    std::vector<DockPanelToolBtn> items = m_items;
    if (ApplyButtons(m_resourceInst, *this, items))
        ApplyTheme(m_dark, m_stripBg);
}

int CDockPanelToolStrip::IdealHeight() const
{
    const int line = HairlinePx();
    if (!m_tb.IsWindow() || !m_applied)
        return DpiScaleInt(22) + line;
    m_tb.Autosize();
    const int cy = m_tb.GetMaxSize().cy;
    return (cy > 0) ? cy + line : DpiScaleInt(22) + line;
}

LRESULT CDockPanelToolStrip::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    try {
    switch (msg) {
    case WM_SIZE:
        if (m_tb.IsWindow()) {
            CRect rc = GetClientRect();
            const int line = HairlinePx();
            m_tb.SetWindowPos(nullptr, 0, 0, rc.Width(), (std::max)(1, rc.Height() - line),
                               SWP_NOZORDER);
        }
        break;
    case WM_DPICHANGED:
    case WM_DPICHANGED_AFTERPARENT:
        RebuildForCurrentDpi();
        Invalidate(FALSE);
        return 0;
    case WM_ERASEBKGND: {
        HDC      hdc = reinterpret_cast<HDC>(wp);
        CRect    rc  = GetClientRect();
        COLORREF bg  = (m_stripBg != CLR_DEFAULT) ? m_stripBg : theme_palette().window_bg;
        if (hdc) {
            HBRUSH br = ::CreateSolidBrush(bg);
            if (br) {
                ::FillRect(hdc, &rc, br);
                ::DeleteObject(br);
            }
            const int line = HairlinePx();
            if (line > 0 && rc.Height() >= line) {
                RECT bottom{rc.left, rc.bottom - line, rc.right, rc.bottom};
                const COLORREF rule = theme_palette().caption_pen;
                HBRUSH         brL  = ::CreateSolidBrush(rule);
                if (brL) {
                    ::FillRect(hdc, &bottom, brL);
                    ::DeleteObject(brL);
                }
            }
        }
        return 1;
    }
    case WM_COMMAND:
        if (m_tb.IsWindow() && reinterpret_cast<HWND>(lp) == m_tb.GetHwnd()) {
            HWND target = ::GetAncestor(*this, GA_ROOTOWNER);
            if (!target)
                target = ::GetAncestor(*this, GA_ROOT);
            if (target)
                ::SendMessageW(target, WM_COMMAND, wp, lp);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lp);
        if (hdr && hdr->hwndFrom == m_tb && hdr->code == TBN_GETINFOTIP) {
            auto* git = reinterpret_cast<NMTBGETINFOTIPW*>(lp);
            for (const auto& it : m_items) {
                if (it.cmdId == 0 || static_cast<UINT>(git->iItem) != it.cmdId)
                    continue;
                if (!git->pszText || git->cchTextMax <= 1)
                    break;
                if (it.tooltipStringId != 0) {
                    HINSTANCE h = m_resourceInst ? m_resourceInst : ::GetModuleHandleW(nullptr);
                    if (::LoadStringW(h, it.tooltipStringId, git->pszText, git->cchTextMax) > 0)
                        return 0;
                }
                if (it.tooltipFallback && it.tooltipFallback[0]) {
                    wcsncpy_s(git->pszText, (size_t)git->cchTextMax, it.tooltipFallback, _TRUNCATE);
                    return 0;
                }
                break;
            }
        }
        if (hdr && hdr->hwndFrom == m_tb && hdr->code == NM_CUSTOMDRAW) {
            auto* cd = reinterpret_cast<NMTBCUSTOMDRAW*>(lp);
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                const auto& pal = theme_palette();
                COLORREF    bg = (m_stripBg != CLR_DEFAULT) ? m_stripBg : pal.window_bg;
                // Dark: `window_fg` for readable labels on the strip (was blended toward
                // `caption_fg_inactive`, which read too dim). Light: unchanged `control_fg`.
                COLORREF fg = m_dark ? pal.window_fg : pal.control_fg;
                cd->clrText    = fg;
                cd->clrBtnFace = bg;
                return CDRF_DODEFAULT;
            }
        }
        break;
    }
    default:
        break;
    }
    return WndProcDefault(msg, wp, lp);
    }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, pm::brand::k_app_id_w, MB_ICONERROR);
    }
    return 0;
}

} // namespace pmui
