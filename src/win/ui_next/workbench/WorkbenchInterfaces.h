#ifndef PM_UI_WORKBENCH_INTERFACES_H
#define PM_UI_WORKBENCH_INTERFACES_H
/// §7d — Composed subsystem interfaces for IWorkbench.
///
/// Each interface isolates one policy dimension that differs across workbenches.
/// `IWorkbench` inherits from all four; the default accessors return `*this` so
/// the workbench IS the policy. A future workbench that wants separate policy
/// objects can override the accessor and return a different instance.
///
/// Callers migrate incrementally:
///   frame.m_workbench->previewPolicy().ShouldUpdateCentrePreview(..., paths)
///   frame.m_workbench->statusBarModel().GetStatusBarLayout()
///   frame.m_workbench->startupHandler().ApplyStartupFilePaths(...)
///   frame.m_workbench->deferredInit().DeferredPostLayoutInit(...)

#include <vector>
#include <string>
#include <cstdint>

class CMainFrame;
class CFileViewer;

namespace pmui { enum class PreviewSource : uint8_t; enum class PreviewStatus : uint8_t; }

namespace pmui {

// ─────────────────────────────────────────────────────────────────────────────
// §7: Preview / selection routing policy
// ─────────────────────────────────────────────────────────────────────────────

class IPreviewPolicy {
public:
    virtual ~IPreviewPolicy() = default;

    /// Given a raw Explorer selection, return which paths should be adopted as
    /// the "active selection" (images for main, chat-eligible for chat, etc.).
    /// Default: dirs + images + text/md + pdf/xls/3d.
    virtual std::vector<std::wstring> FilterExplorerSelection(
        const std::vector<std::wstring>& raw) const;

    /// Decide whether to update the centre preview when the Explorer selection
    /// changes.  Return false to suppress (e.g. chat workbench never previews;
    /// viewer suppresses empty-selection while a startup latch is active).
    /// @param explorer_paths Raw Explorer pick (only meaningful when gating
    ///        `PreviewSource::Explorer` — same vector passed to `Request`).
    virtual bool ShouldUpdateCentrePreview(
        bool is_empty_selection,
        bool startup_latch_active,
        const std::vector<std::wstring>& explorer_paths) const;

    /// §8: Called by `CPreviewCoordinator::Request` after `CFileViewer::OpenFile`
    /// completes (or after clearing). Workbenches can react to the preview
    /// change — e.g. update a latch, push chat context, log, etc.
    /// Default implementation: no-op.
    virtual void OnPreviewChanged(CMainFrame& frame,
                                  PreviewSource source,
                                  const std::vector<std::wstring>& paths,
                                  PreviewStatus status);

    /// Called when the Explorer tree successfully navigates to a new folder
    /// (`UWM_EXPLORER_FOLDER_PATH`), after status-bar / MRU updates.
    /// Default implementation: no-op.
    /// Viewer workbench overrides to auto-preview the first previewable file
    /// in @p folder so the centre view follows folder navigation.
    virtual void OnExplorerFolderPath(CMainFrame& frame, const std::wstring& folder);
};

// ─────────────────────────────────────────────────────────────────────────────
// §7a: Startup file-path handling strategy
// ─────────────────────────────────────────────────────────────────────────────

class IStartupHandler {
public:
    virtual ~IStartupHandler() = default;

    /// Handle non-directory startup file paths from `--src` / IExecute.
    /// Called from `FlushPendingStartupIfAny` after the folder-only check.
    /// Default: `AddFilesToQueue(paths)`.  Viewer overrides to load the first
    /// previewable file + set the startup latch.
    virtual void ApplyStartupFilePaths(CMainFrame& frame,
                                       const std::vector<std::wstring>& paths);
};

// ─────────────────────────────────────────────────────────────────────────────
// §7b: Deferred post-layout init sequence
// ─────────────────────────────────────────────────────────────────────────────

class IDeferredInitSequence {
public:
    virtual ~IDeferredInitSequence() = default;

    /// Workbench-specific work after the generic deferred init (splash,
    /// session replay, startup flush, screenshot probe).
    virtual void DeferredPostLayoutInit(CMainFrame& frame,
                                        const std::vector<std::wstring>& pendingPaths);
};

// ─────────────────────────────────────────────────────────────────────────────
// §7c: Status-bar part model
// ─────────────────────────────────────────────────────────────────────────────

class IStatusBarModel {
public:
    virtual ~IStatusBarModel() = default;

    /// Part indices (−1 = absent / not used by this workbench).
    struct Layout {
        int count     = 5;
        int hint      = 0;
        int explorer  = 1;
        int selection = 2;   ///< −1 for chat (no selection count)
        int queue     = 3;   ///< −1 for viewer/chat (no queue ETA)
        int system    = 4;
    };

    /// Returns the status bar layout for this workbench.
    virtual Layout GetStatusBarLayout() const;

    /// Compute SB_SETPARTS widths and send to the status bar control.
    virtual void RebuildStatusBarParts(CMainFrame& frame);
};

} // namespace pmui

#endif // PM_UI_WORKBENCH_INTERFACES_H
