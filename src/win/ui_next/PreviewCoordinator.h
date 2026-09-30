#ifndef PM_UI_PREVIEW_COORDINATOR_H
#define PM_UI_PREVIEW_COORDINATOR_H
/// §8 — Single owner for the centre file-viewer preview.
///
/// All code that used to call `ApplyCentralFileViewerPreviewFromPaths` or the
/// primary viewer directly now goes through
/// `CPreviewCoordinator::Request(source, paths)`.
///
/// The coordinator:
///  1. Asks the workbench `ShouldUpdateCentrePreview` (policy gate).
///  2. Calls `CFileViewer::OpenFile(paths, source)` → PreviewStatus.
///  3. Notifies the workbench via `OnPreviewChanged(source, paths, status)`.
///  4. Tracks the active source + paths so priority / latch logic is localised.

#include <vector>
#include <string>
#include <cstdint>

class CMainFrame;
class CFileViewer;

namespace pmui {

class IWorkbench;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

/// Who requested the preview change.
enum class PreviewSource : uint8_t {
    None,             ///< Initial / cleared state.
    Explorer,         ///< Explorer selection (timer poll).
    CliStartup,       ///< `--src` / IExecuteCommand at launch.
    QueueRow,         ///< User clicked a queue row.
    ChatGenerated,    ///< AI transform produced a file.
    RecentFile,       ///< File → Recent Files menu.
    AppBrowse,        ///< `app browse|…` bridge or OnAppBrowseToPaths.
    SessionReplay,    ///< Session replay applying a preview snapshot.
    Extension,        ///< External extension / bridge `app preview|…`.
};

/// Result of `CFileViewer::OpenFile` or coordinator gate.
enum class PreviewStatus : uint8_t {
    Ok,                 ///< A file was successfully loaded into the viewer.
    Failed,             ///< A previewable file was found but the loader failed.
    NothingPreviewable, ///< No paths matched any known preview type → cleared.
    Suppressed,         ///< Workbench policy rejected the update (e.g. latch).
};

/// Lightweight value describing the current preview state.
struct PreviewState {
    PreviewSource                source = PreviewSource::None;
    std::vector<std::wstring>    paths;   ///< The paths that were passed to Request().
    std::wstring                 active;  ///< The single file that was actually loaded (empty if cleared).
};

// ─────────────────────────────────────────────────────────────────────────────
// CPreviewCoordinator
// ─────────────────────────────────────────────────────────────────────────────

class CPreviewCoordinator {
public:
    CPreviewCoordinator() = default;

    /// Central entry point. All preview changes go through here.
    ///
    /// @param source   Who is requesting the preview.
    /// @param paths    File paths (may be empty → clear preview).
    /// @param viewer   The CFileViewer to load into.
    /// @param wb       Active workbench for policy + notification.
    /// @param frame    Owning frame (passed to workbench callbacks).
    /// @return         PreviewStatus indicating what happened.
    PreviewStatus Request(PreviewSource              source,
                          const std::vector<std::wstring>& paths,
                          CFileViewer&               viewer,
                          IWorkbench&                wb,
                          CMainFrame&                frame);

    /// Read-only access to the current state.
    const PreviewState& State() const noexcept { return m_state; }

    /// The source of the currently active preview.
    PreviewSource ActiveSource() const noexcept { return m_state.source; }

    /// Force-clear: resets state to None without loading anything.
    /// Used during shutdown / workbench switch.
    void Reset() noexcept { m_state = {}; }

    /// After @c CFileViewer loads another file via in-viewer prev/next (same coordinator
    /// source is kept; paths/active are updated so chat / status see the new file).
    void AdoptNavigatedImage(const std::wstring& path);

private:
    PreviewState m_state;
};

} // namespace pmui

#endif // PM_UI_PREVIEW_COORDINATOR_H
