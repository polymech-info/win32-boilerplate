// §13 — CExplorerSelectionRouter: typed mutations with structured logging.
#include "stdafx.h"
#include "ExplorerSelectionRouter.h"
#include "ui_log_file.hpp"

#include <algorithm>

namespace pmui {

// ── Typed mutations ──────────────────────────────────────────────────────────

void CExplorerSelectionRouter::SetFromExplorerPick(
    const std::vector<std::wstring>& raw_picked,
    const std::vector<std::wstring>& filtered)
{
    m_statusPaths    = raw_picked;
    m_selectionPaths = filtered;
    ui_log_file_eventf("SelRouter: SetFromExplorerPick raw=%zu filtered=%zu",
                        raw_picked.size(), filtered.size());
}

void CExplorerSelectionRouter::ClearFromEmptyExplorerPick()
{
    const bool had = !m_selectionPaths.empty();
    m_statusPaths.clear();
    m_selectionPaths.clear();
    if (had)
        ui_log_file_event("SelRouter: ClearFromEmptyExplorerPick (had selection)");
}

void CExplorerSelectionRouter::SetFromStartup(const std::vector<std::wstring>& filtered)
{
    m_selectionPaths = filtered;
    ui_log_file_eventf("SelRouter: SetFromStartup filtered=%zu", filtered.size());
}

void CExplorerSelectionRouter::SetFromChatGenerated(const std::vector<std::wstring>& paths)
{
    m_selectionPaths = paths;
    ui_log_file_eventf("SelRouter: SetFromChatGenerated paths=%zu", paths.size());
}

void CExplorerSelectionRouter::SetFromAppPaths(const std::vector<std::wstring>& filtered)
{
    m_selectionPaths = filtered;
    ui_log_file_eventf("SelRouter: SetFromAppPaths filtered=%zu", filtered.size());
}

void CExplorerSelectionRouter::ClearForQueueClick()
{
    m_selectionPaths.clear();
    m_statusPaths.clear();
    ui_log_file_event("SelRouter: ClearForQueueClick");
}

void CExplorerSelectionRouter::PruneDeletedPaths(const std::vector<std::wstring>& deleted)
{
    if (deleted.empty()) return;
    auto matches = [&](const std::wstring& p) {
        return std::any_of(deleted.begin(), deleted.end(),
                           [&](const std::wstring& d) { return _wcsicmp(p.c_str(), d.c_str()) == 0; });
    };
    const size_t before = m_selectionPaths.size();
    m_selectionPaths.erase(
        std::remove_if(m_selectionPaths.begin(), m_selectionPaths.end(), matches),
        m_selectionPaths.end());
    const size_t pruned = before - m_selectionPaths.size();
    if (pruned > 0)
        ui_log_file_eventf("SelRouter: PruneDeletedPaths removed=%zu remaining=%zu",
                            pruned, m_selectionPaths.size());
}

void CExplorerSelectionRouter::Reset()
{
    m_selectionPaths.clear();
    m_statusPaths.clear();
    ui_log_file_event("SelRouter: Reset");
}

} // namespace pmui
