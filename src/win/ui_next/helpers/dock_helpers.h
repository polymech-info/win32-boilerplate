#pragma once
// Shared base classes for the standard CDocker panel + container shells.
//
// Every dock panel in ui_next follows this pattern:
//   CXxxContainer : CDockContainer { view member; tab/caption setup; WndProc try/catch }
//   CDockXxx      : CDocker        { container member; bar style; WndProc try/catch }
//
// Inheriting from CDockContainerBase / CDockPanelBase eliminates the repeated
// WndProc boilerplate across all panel wrappers.  Views (CXxxView) keep their
// own WndProc since they have real per-panel message handling.
//
// Usage in a panel header:
//   #include "helpers/dock_helpers.h"
//   class CMyContainer : public CDockContainerBase { ... };  // no WndProc override
//   class CDockMy      : public CDockPanelBase      { ... };  // no WndProc override
//
// CDockPanelBase sets SetBarWidth(3) + SetBarColor in its constructor; derived
// classes just call SetView(m_container) after the base.

#include "stdafx.h"
#include "Resource.h"
#include "theme.hpp"

// ── CDockContainerBase ────────────────────────────────────────────────────────
class CDockContainerBase : public CDockContainer
{
public:
    CDockContainerBase()
    {
        // Set initial blank page color to match theme before window is created
        const auto& pal = pmui::theme_palette();
        SetBlankPageColor(pal.window_bg);
    }

    /// Called when window is created - ensure blank page color is set
    virtual int OnCreate(CREATESTRUCT& cs) override
    {
        int ret = CDockContainer::OnCreate(cs);
        // Re-apply blank page color now that window exists (palette is guaranteed initialized)
        const auto& pal = pmui::theme_palette();
        SetBlankPageColor(pal.window_bg);
        SetPadding(DpiScaleInt(1), 0);
        return ret;
    }

    /// Apply the current theme palette to the tab strip background and the
    /// blank-page colour. Called by CMainFrame::ApplyAppearance after a theme
    /// switch so tabs (e.g. "Queue / Find") repaint dark instead of using
    /// Win32++'s hard-coded RGB(248,248,248).
    virtual void RefreshTabTheme()
    {
        if (!IsWindow()) return;
        const auto& pal = pmui::theme_palette();
        SetBlankPageColor(pal.window_bg);
        SetPadding(DpiScaleInt(1), 0);
        ::InvalidateRect(GetHwnd(), nullptr, TRUE);
    }

protected:
    // ── Themed tab background (fills space behind the tab buttons) ──────────
    // Win32++ paints the area below the tabs with COLOR_BTNFACE which leaks
    // through as a bright bar in dark mode. We paint our own border ourselves
    // (and also draw the dark line under the active tab so it blends in).
    virtual void DrawTabBorders(CDC& dc, RECT& rc) override
    {
        const auto& pal = pmui::theme_palette();
        const bool isBottomTab = (GetStyle() & TCS_BOTTOM) != 0;

        CRect rcItem;
        const int gap = 1;
        if (GetItemCount() == 0) return;
        GetItemRect(0, rcItem);
        if (rcItem.IsRectEmpty()) return;

        const int left   = rcItem.left;
        const int right  = rc.right;
        int       top    = rc.bottom;
        int       bottom = top + gap;

        if (!isBottomTab) {
            const int rcTop = rc.top;
            bottom = std::max<int>(rcTop, GetTabHeight() + gap);
            top    = bottom - gap;
        }

        // Strip behind the row of tab buttons.
        dc.CreateSolidBrush(pal.window_bg);
        dc.CreatePen(PS_SOLID, 1, pal.window_bg);
        dc.Rectangle(left, top, right, bottom);

        // 1px line separating the strip from the panel content.
        dc.CreatePen(PS_SOLID, 1, pal.caption_pen);
        if (isBottomTab) {
            dc.MoveTo(left - 1, bottom);
            dc.LineTo(right - 1, bottom);
        } else {
            dc.MoveTo(left - 1, top - 1);
            dc.LineTo(right - 1, top - 1);
        }

        // Restore the line under the *selected* tab so it blends in.
        dc.CreatePen(PS_SOLID, 1, pal.window_bg);
        GetItemRect(GetCurSel(), rcItem);
        ::OffsetRect(&rcItem, 0, 1);
        if (isBottomTab) {
            dc.MoveTo(rcItem.left, bottom);
            dc.LineTo(rcItem.right, bottom);
        } else {
            dc.MoveTo(rcItem.left, top - 1);
            dc.LineTo(rcItem.right, top - 1);
        }
    }

    // ── Themed tab buttons ──────────────────────────────────────────────────
    // Replaces RGB(248,248,248) (selected) / RGB(200,200,200) (idle) /
    // RGB(160,160,160) (border pen) with values from the active palette.
    virtual void DrawTabs(CDC& dc) override
    {
        const auto& pal = pmui::theme_palette();
        // Active tab uses control_bg; idle uses caption_bg (slightly darker).
        // In light mode this still looks like the original (just slightly
        // softer) because the palette light values are close to the originals.
        for (int i = 0; i < GetItemCount(); ++i) {
            CRect rcItem;
            GetItemRect(i, rcItem);
            if (rcItem.IsRectEmpty()) continue;

            const bool sel = (i == GetCurSel());
            const COLORREF tabBg = sel ? pal.control_bg : pal.caption_bg;
            dc.CreateSolidBrush(tabBg);
            dc.SetBkColor(tabBg);
            dc.SetTextColor(pal.window_fg);

            dc.CreatePen(PS_SOLID, 1, pal.caption_pen);
            dc.RoundRect(rcItem.left, rcItem.top, rcItem.right + 1, rcItem.bottom, 3, 3);

            CSize szImage = GetImages().GetIconSize();
            const int padding = DpiScaleInt(2);
            if (rcItem.Width() < szImage.cx + 2 * padding) continue;

            // Per-tab text + icon — pull from the container at this index
            // (GetDockTabText / GetDockTabImageID are private in CDockContainer).
            CDockContainer* pTab = GetContainerFromIndex(static_cast<size_t>(i));
            CString str   = pTab ? pTab->GetTabText() : CString();
            int     image = (pTab && pTab->GetTabIcon()) ? GetImages().Add(pTab->GetTabIcon()) : -1;
            int yOffset = (rcItem.Height() - szImage.cy) / 2;
            int drawleft = rcItem.left + padding;
            int drawtop  = rcItem.top  + yOffset;
            GetImages().Draw(dc, image, CPoint(drawleft, drawtop), ILD_NORMAL);

            CRect rcText = rcItem;
            if (image >= 0) rcText.left += szImage.cx + padding;
            rcText.left += padding;

            dc.SelectObject(GetTabFont());
            dc.SetBkMode(TRANSPARENT);
            dc.DrawText(str, -1, rcText,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
    }

    virtual LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp) override
    {
        try {
            return WndProcDefault(msg, wp, lp);
        }
        catch (const CException& e) {
            CString s;
            s << e.GetText() << L'\n' << e.GetErrorString();
            ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
        }
        return 0;
    }
};

// ── CDockPanelBase ────────────────────────────────────────────────────────────
//
// All dock panels in ui_next derive from this. The two non-trivial overrides:
//
//  * `OnClose()` — Win32++'s default `OnClose` calls `Destroy()` which kills
//    the HWND tree (the docker, its CDockClient, the container, the view, and
//    every control inside). The C++ `CDock<X>*` pointer survives but every
//    HWND is gone, so any subsequent `IsPanelVisible(pDock)`/`Dock(pDock,…)`/
//    `ShowWindow(controlHwnd,…)` call asserts `IsWindow()`.
//
//    The user's X-button click is meant to be "hide this panel for now",
//    not "delete the whole panel object". Override to plain `Hide()` so the
//    panel can be re-shown via the View tab toggle or auto-shown by an
//    Action button (CMainFrame::EnsurePanelVisible).
//
class CDockPanelBase : public CDocker
{
public:
    CDockPanelBase()
    {
        SetBarWidth(2);
        // Initial colour; CMainFrame::ApplyAppearance overwrites from palette.
        SetBarColor(pmui::theme_palette().bar);
    }

protected:
    virtual int OnCreate(CREATESTRUCT& cs) override
    {
        const int r = CDocker::OnCreate(cs);
        ApplyCompactCaptionHeight();
        return r;
    }

    void ApplyCompactCaptionHeight()
    {
        SetCaptionHeight(std::max(DpiScaleInt(18), GetTextHeight() + DpiScaleInt(3)));
    }

    void ApplyFloatingChromeForTheme()
    {
        if (!IsWindow())
            return;
        const auto& pal = pmui::theme_palette();
        SetCaptionColors(pal.caption_fg, pal.caption_bg,
                         pal.caption_fg_inactive, pal.caption_bg_inactive,
                         pal.caption_pen);
        SetBarColor(pal.bar);
        if (IsUndocked()) {
            pmui::apply_dark_titlebar(GetHwnd(), pal.dark);
            pmui::apply_dwm_toplevel_frame_tint(GetHwnd(), pal.caption_bg_inactive);
            ::RedrawWindow(GetHwnd(), nullptr, nullptr,
                RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        }
    }

    virtual void OnClose() override
    {
        // Just hide — never call Destroy(). See the class comment.
        const HWND hPanel = GetHwnd();
        const HWND hRoot  = hPanel ? ::GetAncestor(hPanel, GA_ROOT) : nullptr;
        if (IsWindow())
            Hide();
        if (hRoot && ::IsWindow(hRoot))
            (void)::PostMessageW(hRoot, UWM_PM_SAVE_WORKBENCH_LAYOUT, 0, 0);
    }

    virtual LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp) override
    {
        try {
            switch (msg) {
            case WM_NCACTIVATE:
            case WM_ACTIVATE:
            case WM_SHOWWINDOW:
            case WM_WINDOWPOSCHANGED:
            case WM_EXITSIZEMOVE:
            case WM_THEMECHANGED:
                ApplyFloatingChromeForTheme();
                break;
            default:
                break;
            }
            return WndProcDefault(msg, wp, lp);
        }
        catch (const CException& e) {
            CString s;
            s << e.GetText() << L'\n' << e.GetErrorString();
            ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
        }
        return 0;
    }
};
