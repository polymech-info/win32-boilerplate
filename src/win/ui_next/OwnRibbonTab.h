#ifndef PM_UI_OWN_RIBBON_TAB_H
#define PM_UI_OWN_RIBBON_TAB_H

#ifdef FEATURE_USE_OWN_RIBBON

#include "OwnRibbonLayout.h"

#include <string>
#include <vector>

class CMainFrame;

namespace own_ribbon {

inline constexpr UINT kCustomRibbonCommandFirst = 52000;
inline constexpr UINT kCustomRibbonCommandLast  = 52999;

struct CustomRibbonCommand {
    UINT        cmdId = 0;
    UINT        targetRibbonCommandId = 0;
    bool        enabled = true;
    std::string id;
    std::string appCommand;
    std::string cliCommand;
    std::string userDataJson;
    bool        externalShellMode = false;
    std::wstring externalShellLine;
    std::wstring externalCommand;
    std::vector<std::wstring> externalArgs;
    std::wstring externalCwd;
    bool        runNewShellWindow = false;
    bool        closeOnExit = false;
    std::wstring openUrl;
    std::wstring openPath;
};

const CustomRibbonCommand* FindCustomRibbonCommand(UINT cmdId);
bool IsCustomRibbonCommandId(UINT cmdId);
void AppendCustomRibbonLayout(std::vector<LayoutItem>& out);

} // namespace own_ribbon

/// Child of the main frame hosting a [`COwnRibbonToolStrip`](OwnRibbonLayout.h).
class COwnRibbonChromePage : public Win32xx::CWnd {
public:
    own_ribbon::COwnRibbonToolStrip m_strip;

    void ApplyPageChrome(bool dark, COLORREF stripBg, COLORREF stripFg);

protected:
    void    PreCreate(CREATESTRUCT& cs) override;
    BOOL    OnEraseBkgnd(Win32xx::CDC& dc) override;
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp) override;

    COLORREF m_pageBg   = RGB(32, 32, 32);
    COLORREF m_ruleColor = RGB(60, 60, 60);
};

/// Single horizontal strip (no CTab row): Home actions + layout / debug footer in one toolbar (panel
/// toggles are on the main View menu).
class COwnRibbonTab : public COwnRibbonChromePage {
public:
    COwnRibbonTab() = default;
    ~COwnRibbonTab() override = default;

    bool Create(CMainFrame& parent);
    /// Populate toolbar buttons / images — call **after** `Create` succeeds (not from WM_CREATE,
    /// so ApplyLayout failures don’t abort window creation with a useless GetLastError() message).
    bool BuildRibbonLayout();

    /// Toolbar row height only (no tab strip).
    int PreferredHeight() const;

    void ApplyChrome(bool dark, COLORREF pageBg);
    void SyncBatchControls(CMainFrame& frame);

protected:
    int OnCreate(CREATESTRUCT& cs) override;

private:
    COwnRibbonTab(const COwnRibbonTab&) = delete;
    COwnRibbonTab& operator=(const COwnRibbonTab&) = delete;

    std::vector<own_ribbon::LayoutItem> m_layoutItems;
};

#endif // FEATURE_USE_OWN_RIBBON

#endif
