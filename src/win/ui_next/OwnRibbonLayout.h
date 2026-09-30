#ifndef PM_UI_OWN_RIBBON_LAYOUT_H
#define PM_UI_OWN_RIBBON_LAYOUT_H

#ifdef FEATURE_USE_OWN_RIBBON

#include <wxx_wincore.h>

#include <vector>

// Win32++ ToolbarBuilder-style strip: CPager + CToolBar, image list, groups (separators),
// labels below icons (no TBSTYLE_LIST), large vs small icons, BTNS_CHECK toggles — proper hot/pressed
// chrome from comctl v6 instead of BS_BITMAP buttons.

namespace own_ribbon {

enum class IconPresentation : std::uint8_t { Large32, Small16 };

struct LayoutItem {
    /// 0 = vertical separator between groups (same row).
    UINT    cmdId = 0;
    UINT    imageResId = 0;
    IconPresentation icon = IconPresentation::Large32;
    bool    showLabel = true;
    bool    enabled = true;
    bool    isToggle = false;
    /// `RibbonUI.h` `*_LabelTitle_RESID` (see `locales/RibbonStrings_i18n.rc`). Loaded at
    /// layout; when 0, `label` is used (English fallback is recommended).
    UINT   labelStringId = 0;
    /// `*_TooltipDescription_RESID` / title id when present; 0 = use `tooltip`, else label.
    UINT   tooltipStringId = 0;
    const wchar_t* label = nullptr;
    const wchar_t* tooltip = nullptr;
    /// Split/dropdown toolbar button. When set, `dropdownItems` supplies the popup menu items.
    bool isDropdown = false;
    const std::vector<LayoutItem>* dropdownItems = nullptr;
#ifdef FEATURE_SVG_BUTTONS
    /// When non-null, rasterize this Tabler `filled/*.svg` (ThorVG) with [`svgTint`] or theme fg.
    const wchar_t* svgFilePathW = nullptr;
    /// `CLR_NONE` → `theme_palette().window_fg`; else RGB fill in place of `currentColor` in the SVG.
    COLORREF         svgTint = CLR_NONE;
#endif
};

/// Fallback height when the toolbar has not been laid out yet (DPI-scaled).
int PreferredStripHeight(const Win32xx::CWnd& dpiRef);

/// Breathing room between the frame menu row and the first toolbar buttons (DPI-scaled).
int RibbonChromeTopInset(const Win32xx::CWnd& dpiRef);
/// Same padding below the toolbar row as above (then [`RibbonChromeBottomRule`] under that).
int RibbonChromeBottomInset(const Win32xx::CWnd& dpiRef);
/// 1px hairline (min 1 device px) under the entire ribbon host — separates strip from dock view.
int RibbonChromeBottomRule(const Win32xx::CWnd& dpiRef);

/// Human-readable detail from the last failed `COwnRibbonToolStrip::ApplyLayout` (empty if none).
const wchar_t* LastRibbonLayoutErrorW();

/// Single-row toolbar inside a horizontal pager (scroll if needed). Commands bubble as WM_COMMAND
/// to the application root the same way as the old static buttons.
///
/// Toolbar button labels use the app UI font from [`pmui::ui_font`]; `CMainFrame::ApplyAppearance`
/// broadcasts it with [`pmui::apply_font_to_tree`] on the ribbon host (no separate font model here).
///
/// For **dock panels** (log/settings chrome) use the slimmer icon-only helper
/// [`pmui::CDockPanelToolStrip`](helpers/dock_panel_toolstrip.h) + ribbon small BMP ids.
class COwnRibbonToolStrip : public Win32xx::CWnd {
public:
    COwnRibbonToolStrip() = default;

    void ClearLayout();
    /// Build buttons from items (must include imageResId for every non-separator). `inst` = module
    /// with ribbon BMPs; `dpiRef` supplies DpiScaleInt for icon sizes.
    bool ApplyLayout(HINSTANCE inst, const Win32xx::CWnd& dpiRef, const std::vector<LayoutItem>& items);

    Win32xx::CToolBar& ToolBar() { return m_toolbar; }

    void ApplyChrome(bool dark, COLORREF stripBg, COLORREF stripFg);
    void SyncToggle(UINT cmdId, bool on);
    void SyncEnabled(UINT cmdId, BOOL enable);

    /// Vertical size of the pager/toolbar row after layout — use for the chrome host height
    /// instead of [`PreferredStripHeight`], which over-counts and leaves empty top/bottom bands.
    int MeasuredStripHeight();

protected:
    void  PreCreate(CREATESTRUCT& cs) override;
    int   OnCreate(CREATESTRUCT&) override;
    void  OnDestroy() override;
    BOOL OnEraseBkgnd(Win32xx::CDC& dc) override;
    void OnDpiChanged(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT OnNotify(WPARAM wp, LPARAM lp);
    LRESULT OnSize(WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp) override;

private:
    struct CPagerForward final : public Win32xx::CPager {
        LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp) override
        {
            if (msg == WM_COMMAND || msg == WM_NOTIFY)
                return GetParent().SendMessage(msg, wp, lp);
            return CPager::WndProc(msg, wp, lp);
        }
    };

    void RecalcLayout();
    const LayoutItem* FindLayoutItem(UINT cmdId) const;
    bool IsRuntimeEnabled(const LayoutItem& item) const;
    bool ShowDropdownForItem(const LayoutItem& item);
    void ForwardCommandToRoot(WPARAM wp, LPARAM lp);

    /// Background under the pager + toolbar (matches `ThemePalette::window_bg` after ApplyChrome).
    COLORREF m_stripBg  = CLR_DEFAULT;
    COLORREF m_stripFg  = CLR_DEFAULT;

    CPagerForward m_pager;
    Win32xx::CToolBar    m_toolbar;
    Win32xx::CImageList  m_images;
    std::vector<HBITMAP>         m_ownedBmps;
    std::vector<UINT>            m_runtimeDisabled;
    bool                         m_haveToolbar = false;
    const std::vector<LayoutItem>* m_layoutItems = nullptr;
    /// Set in [`ApplyLayout`] for [`LoadStringW`] in `TBN_GETINFOTIP`.
    HINSTANCE m_hResourceModule = nullptr;
};

} // namespace own_ribbon

#endif // FEATURE_USE_OWN_RIBBON

#endif
