#include "stdafx.h"
#include "workbench/MainWorkbench.h"
#include "Resource.h"
#include "Mainfrm.h"
#include "win/settings_store.hpp"
#include "file_extensions.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>

namespace pmui {

const char* CViewerSimpleWorkbench::workbenchSettingsId() const noexcept
{
    return "viewer";
}

void CViewerSimpleWorkbench::BuildInitialDockLayout(CMainFrame& frame)
{
    frame.CloseAllDockers();
    ClearDockPointers(frame);

    // Viewer starts centre-only. Optional Explorer is created lazily via View -> Explorer,
    // or restored if the saved viewer layout explicitly has it visible.
}

void CViewerSimpleWorkbench::AttachClientView(CMainFrame& frame)
{
    frame.SetView(frame.m_viewerManager.ActiveView());
}

void CViewerSimpleWorkbench::SetupDockContainers(CMainFrame& frame)
{
    if (frame.m_pDockFileTree)
        frame.m_pDockFileTree->GetFileTreeContainer().SetHideSingleTab(TRUE);
}

void CViewerSimpleWorkbench::LoadLayout(CMainFrame& frame)
{
    media::settings::WindowLayout layout;
    std::string                err;
    if (!media::settings::load_window_layout(layout, err, workbenchSettingsId()))
        return;
    frame.ApplyWindowLayoutData(layout);
    if (!layout.has_panel_visibility)
        frame.FinishViewerWorkbenchDefaultDockVisibility();
}

bool CViewerSimpleWorkbench::AcceptRestoredDockLayout(CMainFrame& frame, std::string& reason) const
{
    (void)frame;
    reason.clear();
    // Viewer preset is intentionally flexible: users may keep only centre view,
    // restore Explorer and Chat, or persist custom dock combinations.
    // Rejecting restored topologies caused valid saved layouts to be discarded
    // ("dock miss" fallback path) and panels to disappear on next launch.
    return true;
}

void CViewerSimpleWorkbench::ResetLayout(CMainFrame& frame)
{
    BuildInitialDockLayout(frame);
    frame.RecalcLayout();

    (void)frame.SaveDockLayout();
    std::string err;
    media::settings::WindowLayout def;
    if (!frame.FillWindowLayoutFromState(def))
        return;
    def.has_panel_visibility = true;
    def.pv_queue             = false;
    def.pv_log               = false;
    def.pv_settings          = false;
    def.pv_fileinfo          = false;
    def.pv_findresults       = false;
    def.pv_dupresults        = false;
    def.pv_chat              = false;
    def.pv_filetree                = false;
    def.filetree_show_shell_frames = false;
    def.pv_nodes              = false;
    def.defer_dock_containers = true;
    media::settings::save_window_layout(def, err, workbenchSettingsId());
}

// ── §7: Viewer preview policy ───────────────────────────────────────────────

namespace {
bool explorer_pick_has_previewable_regular_file(const std::vector<std::wstring>& raw)
{
    namespace fs = std::filesystem;
    for (const auto& p : raw) {
        std::error_code ec;
        if (!fs::is_regular_file(fs::path(p), ec) || ec)
            continue;
        std::wstring ext = fs::path(p).extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
        if (pmui::is_image_ext(ext) || pmui::is_browser_image_ext(ext)
            || pmui::is_text_preview_eligible_for_path(p)
            || pmui::is_viewer_3d_ext(ext) || pmui::is_viewer_pdf_ext(ext)
            || pmui::is_viewer_spreadsheet_ext(ext) || pmui::is_video_preview_eligible_for_path(p))
            return true;
    }
    return false;
}
} // namespace

bool CViewerSimpleWorkbench::ShouldUpdateCentrePreview(bool is_empty, bool startup_latch,
                                                       const std::vector<std::wstring>& explorer_paths) const
{
    // Suppress Explorer-driven preview updates while the CLI startup latch is active
    // if they would clear or replace `--src` content: empty picks, or a folder-only /
    // non-previewable pick (saved `filetree_folder` navigates before the Shell selects
    // the `--src` file — see selection.md).
    if (startup_latch) {
        if (is_empty)
            return false;
        if (!explorer_pick_has_previewable_regular_file(explorer_paths))
            return false;
    }
    // In viewer mode the centre always keeps its last loaded file — empty selections
    // and folder-only picks never clear it.  Folder navigation is handled by
    // OnExplorerFolderPath; the selection polling timer fires empty ticks between
    // navigations that must not wipe what was just loaded.  A real file selection
    // (non-empty, has a previewable regular file) is the only thing that replaces
    // the current preview via the Explorer path.
    if (is_empty)
        return false;
    if (!explorer_pick_has_previewable_regular_file(explorer_paths))
        return false;
    return true;
}

void CViewerSimpleWorkbench::OnPreviewChanged(CMainFrame& frame, PreviewSource source,
                                              const std::vector<std::wstring>& /*paths*/,
                                              PreviewStatus status)
{
    if (source != PreviewSource::Explorer)
        return;
    if (status != PreviewStatus::Ok && status != PreviewStatus::Failed)
        return;
    frame.m_startupPreviewLatch = false;
    frame.m_startupNavGeneration = 0;
}

void CViewerSimpleWorkbench::OnExplorerFolderPath(CMainFrame& frame, const std::wstring& folder)
{
    // Suppress during startup latch — the --src preview already seeded the viewer.
    if (frame.m_startupPreviewLatch)
        return;
    // Auto-preview the first previewable file in the newly navigated-to folder
    // so the centre view tracks FileTreePanel folder changes without requiring an
    // explicit file selection.
    frame.PreviewFirstFileInFolder(folder);
}

// ── §7c: Viewer status-bar layout ───────────────────────────────────────────

IStatusBarModel::Layout CViewerSimpleWorkbench::GetStatusBarLayout() const
{
    // Viewer: 4-part — no queue ETA. hint(0), explorer(1), selection(2), system(3).
    return Layout{ 4, /*hint*/0, /*explorer*/1, /*selection*/2, /*queue*/-1, /*system*/3 };
}

// ── §7b: Viewer deferred post-layout sequencing ─────────────────────────────

void CViewerSimpleWorkbench::DeferredPostLayoutInit(CMainFrame& frame,
                                                    const std::vector<std::wstring>& pendingPaths)
{
    if (frame.m_pDockFileTree && frame.m_pDockFileTree->IsWindowVisible()) {
        auto& eb = frame.m_pDockFileTree->GetFileTreeContainer().GetBrowserView();
        // §3 (tactical): skip Shell rebuild when startup paths are present —
        // prevents the rebuild from racing the CLI-seeded preview (the startup
        // latch + navGeneration already protect against stale empty-selection,
        // but avoiding a needless full destroy/create is cleaner).
        if (frame.m_dockLayoutFromSettingsJson && pendingPaths.empty())
            eb.RequestRebuildAfterReDock();
        else
            eb.SyncShellHostLayout();
        frame.m_dockLayoutFromSettingsJson = false;
    }
    (void)pendingPaths;
    frame.ApplyPendingStartupFileTreeIfAny();
}

// ── §7a: Viewer startup path handling ───────────────────────────────────────

void CViewerSimpleWorkbench::ApplyStartupFilePaths(CMainFrame& frame,
                                                   const std::vector<std::wstring>& paths)
{
    // Viewer has no queue — load the first previewable file directly.
    frame.LoadFirstStartupPreviewFromPaths(paths);

    // §1 startup preview latch — protect the CLI-seeded preview from
    // stale empty-selection posts that arrive during Shell rebuild /
    // first navigation.  Cleared in OnExplorerSelection on the first
    // non-empty selection (user interaction).
    frame.m_startupPreviewLatch = true;

    // §12: snapshot the Explorer's current navGeneration so stale empty-
    // selection posts (already queued before the timer was killed in §10)
    // are discarded in OnExplorerSelection.
    if (frame.m_pDockFileTree)
        frame.m_startupNavGeneration = frame.m_pDockFileTree->GetFileTreeContainer()
                                           .GetBrowserView().NavGeneration();
}

} // namespace pmui
