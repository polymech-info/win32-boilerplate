#ifndef PM_UI_FINDPANEL_H
#define PM_UI_FINDPANEL_H
//
// FindPanel — dockable results list for the `find` operation.
//
// Mirrors the FileQueue pattern (View / Container / Docker triple) so it gets
// the standard caption + close button + drag-to-dock behaviour for free.
//
// Columns: Name | Score | Source | Reason | Path
// Per-row context menu (right-click): Reveal in Explorer panel / Open / Copy.
// Click       → posts UWM_FIND_ITEM_CLICKED to the frame (preview + show path).
// DEL key     → posts UWM_FIND_DELETE_SELECTION (handled by the frame).
//
#include "stdafx.h"
#include "helpers/dock_helpers.h"

#include <string>
#include <vector>

/// Row for `UWM_FIND_PROGRESS` (heap `std::vector<FindProgressRow>`; frame deletes).
struct FindProgressRow {
    std::wstring path;
    double       score = 0.0;
    std::wstring source;
    std::wstring reason;
};

enum FindCol : int {
    FIND_COL_NAME = 0,
    FIND_COL_SCORE,
    FIND_COL_SOURCE,
    FIND_COL_REASON,
    FIND_COL_PATH,
    FIND_COL_COUNT,
};

class CFindResultsView : public CListView
{
public:
    CFindResultsView() = default;
    virtual ~CFindResultsView() override = default;

    /// Append a single result row.  Score is shown with two decimals.
    int  AddResult(LPCWSTR path, double score, LPCWSTR source, LPCWSTR reason);
    void ClearAll();
    void RemoveSelectedItems();
    int  Count() const { return GetItemCount(); }
    /// Path for a given row index (empty if out of range).
    CString GetItemPath(int item) const;

    /// Re-apply the current theme palette to the listview (background + text).
    /// Called by CMainFrame::ApplyAppearance after the user changes the theme.
    void RefreshThemeColors();

protected:
    virtual void    OnAttach() override;
    virtual void    PreCreate(CREATESTRUCT& cs) override;
    virtual LRESULT OnNotifyReflect(WPARAM wparam, LPARAM lparam) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CFindResultsView(const CFindResultsView&) = delete;
    CFindResultsView& operator=(const CFindResultsView&) = delete;

    void SetupColumns();
    void ShowContextMenuFor(int item, POINT screenPt);
};

class CFindResultsContainer : public CDockContainerBase
{
public:
    CFindResultsContainer();
    virtual ~CFindResultsContainer() override = default;
    CFindResultsView& GetView() { return m_view; }

private:
    CFindResultsContainer(const CFindResultsContainer&) = delete;
    CFindResultsContainer& operator=(const CFindResultsContainer&) = delete;
    CFindResultsView m_view;
};

class CDockFindResults : public CDockPanelBase
{
public:
    CDockFindResults();
    virtual ~CDockFindResults() override = default;
    CFindResultsContainer& GetFindResultsContainer() { return m_container; }

private:
    CDockFindResults(const CDockFindResults&) = delete;
    CDockFindResults& operator=(const CDockFindResults&) = delete;
    CFindResultsContainer m_container;
};

#endif // PM_UI_FINDPANEL_H
