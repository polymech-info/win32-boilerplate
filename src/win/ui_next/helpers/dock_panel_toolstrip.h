#pragma once
// Slim toolbar row for dock panel content (log, settings chrome, …).
// Uses the same 16×16 ribbon small bitmaps as OwnRibbonLayout / Tabler strips.
// Optional label right of icon: set `showLabel` + `labelStringId` and/or `labelFallback`.
// Commands are forwarded to the top-level frame with WM_COMMAND (BN_CLICKED from toolbar).
//
// Theming: **not** covered by `apply_window_theme_recursive` (it is a dedicated `CWnd`).
// Hosts call `ApplyTheme(dark, stripBg)` when the app palette changes; the impl strips
// window edges in `PreCreate`, uses `SetWindowTheme` on the inner `CToolBar` + `NMTBCUSTOMDRAW`
// for label colours: `window_fg` when dark, `control_fg` when light (`theme_palette()` in the .cpp).
// Contrast: standard COMBO/EDIT in
// `theme.cpp` global walk; `IExplorerBrowser` in `FileTreePanel.cpp` (EBO, no walk).

#include "stdafx.h"
#include <vector>

namespace pmui {

struct DockPanelToolBtn {
    UINT           cmdId = 0; ///< 0 = separator (TBSTYLE_SEP)
    UINT           imageResId = 0;
    UINT           tooltipStringId = 0; ///< LoadStringW on `resourceInst` in ApplyButtons
    const wchar_t* tooltipFallback = nullptr;
    bool           showLabel = false;
    UINT           labelStringId = 0; ///< 0 = use `labelFallback` only
    const wchar_t* labelFallback = nullptr;
};

class CDockPanelToolStrip : public CWnd {
public:
    CDockPanelToolStrip() = default;

    /// Load glyphs, add buttons, autosize. Call after `Create(host)`.
    bool ApplyButtons(HINSTANCE resourceInst, const CWnd& dpiRef,
                      const std::vector<DockPanelToolBtn>& items);

    void ApplyTheme(bool dark, COLORREF stripBg);
    int  IdealHeight() const;

    CToolBar& ToolBar() { return m_tb; }

protected:
    int  OnCreate(CREATESTRUCT& cs) override;
    void PreCreate(CREATESTRUCT& cs) override;
    void OnDestroy() override;
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp) override;

private:
    void    ReleaseImageList();
    void    RebuildForCurrentDpi();
    HBITMAP LoadGlyphScaled(HINSTANCE inst, UINT resId, int targetPx) const;
    int     HairlinePx() const;

    CToolBar              m_tb;
    CImageList            m_il;
    std::vector<DockPanelToolBtn> m_items;
    HINSTANCE             m_resourceInst = nullptr;
    COLORREF              m_stripBg = CLR_DEFAULT;
    bool                  m_dark    = false;
    bool                  m_applied = false;
};

} // namespace pmui
