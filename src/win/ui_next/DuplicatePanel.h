#ifndef PM_UI_DUPLICATEPANEL_H
#define PM_UI_DUPLICATEPANEL_H
//
// Dockable tree: one parent node per duplicate group, children = member paths.
// See packages/Win32xx/samples/Explorer (CTreeView) for the same control pattern.
//
#include "stdafx.h"
#include "helpers/dock_helpers.h"
#include "Resource.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

struct DupListRow {
    std::wstring                method;
    std::wstring                key;
    int                         count = 0;
    std::wstring                samplePath;
    std::vector<std::wstring>   paths;
    /// Optional per-path short label (e.g. "8/10" for LLM, "Hamming 3" for dHash) — from report JSON.
    std::vector<std::wstring>   pathSubtext;
};

/// Fills @p pathSubtext on each row from a duplicate session/report (LLM pairs, fingerprint graph).
void enrich_dup_rows_from_report(std::vector<DupListRow>& rows, const nlohmann::json& report);
/// Free-form text for the File info panel: pairwise LLM / Hamming lines for the selected file.
std::wstring build_duplicate_report_detail_for_selection(const nlohmann::json& report,
    const std::wstring& selectedPath, const std::vector<std::wstring>& peerPaths);

/// Payload posted with UWM_DUPLICATES_DONE (heap; frame deletes after handling).
struct DuplicatesUiResult {
    std::vector<DupListRow>  rows;
    nlohmann::json           report = nlohmann::json::object();
    bool                     ok      = false;
    std::wstring             error; // shown when !ok
};

/// Item LPARAM: parent group = 1 + groupIndex; member = 0x80000000 | (group<<16) | pathIndex
class CDuplicateResultsView : public CTreeView
{
public:
    CDuplicateResultsView() = default;
    virtual ~CDuplicateResultsView() override = default;

    void ClearAll();
    void SetRows(const std::vector<DupListRow>& rows);
    int  Count() const { return (int)m_rows.size(); }

    /// Selected item or first path in group — for preview / status
    std::wstring GetPathForItemData(UINT_PTR d) const;
    const std::vector<std::wstring>* GetPathsForItemData(UINT_PTR d) const;
    /// Group row for the tree item (parent or any child in that group); null if invalid.
    const DupListRow* GetRowForItemData(UINT_PTR d) const;

    void RemoveSelectedItems();
    void RefreshThemeColors();

protected:
    virtual void    OnAttach() override;
    virtual void    PreCreate(CREATESTRUCT& cs) override;
    virtual LRESULT OnNotifyReflect(WPARAM wparam, LPARAM lparam) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    CDuplicateResultsView(const CDuplicateResultsView&) = delete;
    CDuplicateResultsView& operator=(const CDuplicateResultsView&) = delete;

    void    ShowContextMenuFor(HTREEITEM hit, POINT screenPt);
    int     GroupIndexForItemData(UINT_PTR d) const;
    void    OnSelChanged(HTREEITEM h);

    std::vector<DupListRow> m_rows;
};

class CDuplicateResultsContainer : public CDockContainerBase
{
public:
    CDuplicateResultsContainer();
    virtual ~CDuplicateResultsContainer() override = default;
    CDuplicateResultsView& GetView() { return m_view; }

private:
    CDuplicateResultsContainer(const CDuplicateResultsContainer&) = delete;
    CDuplicateResultsContainer& operator=(const CDuplicateResultsContainer&) = delete;
    CDuplicateResultsView m_view;
};

class CDockDuplicateResults : public CDockPanelBase
{
public:
    CDockDuplicateResults();
    virtual ~CDockDuplicateResults() override = default;
    CDuplicateResultsContainer& GetDuplicateResultsContainer() { return m_container; }

private:
    CDockDuplicateResults(const CDockDuplicateResults&) = delete;
    CDockDuplicateResults& operator=(const CDockDuplicateResults&) = delete;
    CDuplicateResultsContainer m_container;
};

#endif
