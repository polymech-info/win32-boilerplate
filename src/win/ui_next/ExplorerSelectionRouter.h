#ifndef PM_UI_EXPLORER_SELECTION_ROUTER_H
#define PM_UI_EXPLORER_SELECTION_ROUTER_H
/// §13 — Owns Explorer selection state that was previously scattered across CMainFrame members.
///
/// `CExplorerSelectionRouter` holds `m_selectionPaths` (workbench-filtered, used by
/// batch commands / chat context / Pixlwiz Share) and `m_statusPaths` (raw picks, used
/// by the status bar).  All mutations go through typed methods with structured logging.
///
/// `CMainFrame` keeps a `CExplorerSelectionRouter m_selRouter` member and replaces
/// direct access to the old `m_explorerSelectionPaths` / `m_explorerStatusPaths` with
/// `m_selRouter.SelectionPaths()` / `m_selRouter.StatusPaths()` and the mutation API.

#include <string>
#include <vector>
#include <cstdint>

namespace pmui {

class CExplorerSelectionRouter {
public:
    CExplorerSelectionRouter() = default;

    // ── Read access ──────────────────────────────────────────────────────────

    /// Workbench-filtered selection (images, text, PDF, 3D, spreadsheet, or
    /// chat-eligible depending on workbench). Used by batch commands, chat
    /// context push, Pixlwiz Share.
    const std::vector<std::wstring>& SelectionPaths() const noexcept { return m_selectionPaths; }

    /// Raw Explorer pick (dirs + all file types). Used by the status bar
    /// (item count + total size) and by the preview coordinator.
    const std::vector<std::wstring>& StatusPaths() const noexcept { return m_statusPaths; }

    bool HasSelection() const noexcept { return !m_selectionPaths.empty(); }
    bool HasStatusPaths() const noexcept { return !m_statusPaths.empty(); }

    // ── Mutable access (for legacy callers that need push_back / erase) ─────

    std::vector<std::wstring>& MutableSelectionPaths() noexcept { return m_selectionPaths; }
    std::vector<std::wstring>& MutableStatusPaths()    noexcept { return m_statusPaths; }

    // ── Typed mutations (with logging) ───────────────────────────────────────

    /// Called from `OnExplorerSelection` with a non-empty pick.
    void SetFromExplorerPick(const std::vector<std::wstring>& raw_picked,
                             const std::vector<std::wstring>& filtered);

    /// Called from `OnExplorerSelection` when pick is empty.
    void ClearFromEmptyExplorerPick();

    /// Called from `FlushPendingStartupIfAny` after workbench filter.
    void SetFromStartup(const std::vector<std::wstring>& filtered);

    /// Called from `UWM_CHAT_GENERATED` — adopt AI outputs as implicit selection.
    void SetFromChatGenerated(const std::vector<std::wstring>& paths);

    /// Called from `OnAppOpenChat` / `OnAppBrowseToPaths` — bridge / IPC seed.
    void SetFromAppPaths(const std::vector<std::wstring>& filtered);

    /// Called from `UWM_QUEUE_ITEM_CLICKED` — queue takes focus.
    void ClearForQueueClick();

    /// Called from `OnReleasePreviewForPaths(lparam!=0)` — prune deleted paths.
    void PruneDeletedPaths(const std::vector<std::wstring>& deleted);

    /// Full reset (e.g. workbench switch).
    void Reset();

private:
    std::vector<std::wstring> m_selectionPaths;
    std::vector<std::wstring> m_statusPaths;
};

} // namespace pmui

#endif // PM_UI_EXPLORER_SELECTION_ROUTER_H
