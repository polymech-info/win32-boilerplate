#ifndef PM_UI_MAINWORKBENCH_H
#define PM_UI_MAINWORKBENCH_H

#include <windows.h>
#include <nlohmann/json.hpp>
#include <string>
#include "workbench/WorkbenchInterfaces.h"

/// Main-window workbench: default `CMainFrame` policy (layout, dock JSON, reset).
/// `CMainFrame` delegates; implementations call back into @ref CMainFrame (friend).
///
/// §7d: `IWorkbench` inherits from all four subsystem interfaces so each
/// workbench IS the policy by default.  Callers can address the sub-interface
/// directly via the accessor (`previewPolicy()`, `startupHandler()`, etc.)
/// for clarity, or call the method on `IWorkbench` directly — both resolve
/// to the same virtual.
class CMainFrame;

namespace pmui {

// Re-export the StatusBarLayout type for backward compat (was nested in IWorkbench before §7d).
using StatusBarLayout = IStatusBarModel::Layout;

class IWorkbench : public IPreviewPolicy,
                   public IStartupHandler,
                   public IDeferredInitSequence,
                   public IStatusBarModel {
public:
    ~IWorkbench() override = default;

    /// Key under @c settings.json["workbench"] for this policy (stable ASCII, e.g. @c "main" or @c "chat_simple").
    virtual const char* workbenchSettingsId() const noexcept = 0;

    // ── Layout / dock topology (core — stays on IWorkbench) ─────────────────
    virtual void LoadLayout(CMainFrame& frame) = 0;
    virtual void SaveLayout(CMainFrame& frame) = 0;
    virtual bool ImportLayoutDocument(
        CMainFrame& frame, const nlohmann::json& doc, std::string& err, bool persist = true) = 0;
    virtual BOOL LoadDockFromSettings(CMainFrame& frame) = 0;
    virtual BOOL SaveDockToSettings(CMainFrame& frame)   = 0;
    virtual BOOL LoadDockContainersFromSettings(CMainFrame& frame) = 0;
    /// Workbench-specific acceptance check after a dock tree was restored and
    /// cached dock pointers were rebound. Main accepts any subset; chat/viewer
    /// reject stale topologies that belong to older/default workbench shapes.
    virtual bool AcceptRestoredDockLayout(CMainFrame& frame, std::string& reason) const = 0;
    virtual void ResetLayout(CMainFrame& frame) = 0;
    virtual void SetupDockContainers(CMainFrame& frame) = 0;
    /// Clears CMainFrame's cached dock member pointers after the docker tree is destroyed.
    virtual void ClearDockPointers(CMainFrame& frame) = 0;
    /// Rebinds CMainFrame's cached dock member pointers after Win32++ recreated the dock tree.
    virtual void BindDockPointers(CMainFrame& frame) = 0;
    /// Applies workbench-specific container settings and common docker chrome after dock creation/restore.
    virtual void FinishDockRestore(CMainFrame& frame) = 0;
    virtual void BuildInitialDockLayout(CMainFrame& frame) = 0;
    virtual void AttachClientView(CMainFrame& frame) = 0;
    /// Responsive workbench policy after the base frame handled WM_SIZE.
    /// Called only for maximize/unmaximize transitions, not ordinary drag-resize.
    virtual void OnFrameMaximizeTransition(CMainFrame& /*frame*/, SIZE /*previousClient*/, SIZE /*currentClient*/) {}

    // ── §7d: Composed subsystem accessors ───────────────────────────────────
    // Default: return *this (the workbench IS the policy).
    // Override to return a separate object when the policy is shared across
    // workbenches or when you want the workbench to compose fine-grained
    // strategy objects instead of overriding virtuals.
    virtual IPreviewPolicy&       previewPolicy()       { return *this; }
    virtual IStartupHandler&      startupHandler()      { return *this; }
    virtual IDeferredInitSequence& deferredInit()        { return *this; }
    virtual IStatusBarModel&      statusBarModel()      { return *this; }

    // Const variants for read-only access.
    virtual const IPreviewPolicy&  previewPolicy()  const { return *this; }
    virtual const IStatusBarModel& statusBarModel() const { return *this; }
};

class CDefaultMainWorkbench : public IWorkbench {
public:
    const char* workbenchSettingsId() const noexcept override;
    void LoadLayout(CMainFrame& frame) override;
    void SaveLayout(CMainFrame& frame) override;
    bool ImportLayoutDocument(
        CMainFrame& frame, const nlohmann::json& doc, std::string& err, bool persist = true) override;
    BOOL LoadDockFromSettings(CMainFrame& frame) override;
    BOOL SaveDockToSettings(CMainFrame& frame) override;
    BOOL LoadDockContainersFromSettings(CMainFrame& frame) override;
    bool AcceptRestoredDockLayout(CMainFrame& frame, std::string& reason) const override;
    void ResetLayout(CMainFrame& frame) override;
    void SetupDockContainers(CMainFrame& frame) override;
    void ClearDockPointers(CMainFrame& frame) override;
    void BindDockPointers(CMainFrame& frame) override;
    void FinishDockRestore(CMainFrame& frame) override;
    void BuildInitialDockLayout(CMainFrame& frame) override;
    void AttachClientView(CMainFrame& frame) override;
    void OnFrameMaximizeTransition(CMainFrame& frame, SIZE previousClient, SIZE currentClient) override;
    // §7a: centre preview via PreviewCoordinator (xblox/text/pdf/…), not queue LoadPicture only.
    void ApplyStartupFilePaths(CMainFrame& frame, const std::vector<std::wstring>& paths) override;
};

/// `ui.workbench: "chat"` — ChatApp-like chrome + chat as frame SetView.
class CChatSimpleWorkbench final : public CDefaultMainWorkbench {
public:
    const char* workbenchSettingsId() const noexcept override;
    void        LoadLayout(CMainFrame& frame) override;
    bool        AcceptRestoredDockLayout(CMainFrame& frame, std::string& reason) const override;
    void        ResetLayout(CMainFrame& frame) override;
    void        SetupDockContainers(CMainFrame& frame) override;
    void        BuildInitialDockLayout(CMainFrame& frame) override;
    void        AttachClientView(CMainFrame& frame) override;
    // §7a: queue + file-tree only — no centre preview (chat is the frame SetView).
    void        ApplyStartupFilePaths(CMainFrame& frame, const std::vector<std::wstring>& paths) override;
    // §7 overrides (IPreviewPolicy)
    std::vector<std::wstring> FilterExplorerSelection(const std::vector<std::wstring>& raw) const override;
    bool ShouldUpdateCentrePreview(bool is_empty, bool startup_latch,
                                   const std::vector<std::wstring>& explorer_paths) const override;
    // §7c override (IStatusBarModel): 3-part status bar (no selection, no queue)
    Layout GetStatusBarLayout() const override;
};

/// `ui.workbench: "viewer"` or @c --ui-preset=viewer — minimal photo-viewer mode.
class CViewerSimpleWorkbench final : public CDefaultMainWorkbench {
public:
    const char* workbenchSettingsId() const noexcept override;
    void        LoadLayout(CMainFrame& frame) override;
    bool        AcceptRestoredDockLayout(CMainFrame& frame, std::string& reason) const override;
    void        ResetLayout(CMainFrame& frame) override;
    void        SetupDockContainers(CMainFrame& frame) override;
    void        BuildInitialDockLayout(CMainFrame& frame) override;
    void        AttachClientView(CMainFrame& frame) override;
    // §7 override (IPreviewPolicy): suppress Explorer-driven preview clears while startup latch
    // is active, and suppress folder-only picks at all times (folder navigation is handled
    // via OnExplorerFolderPath so a dir pick never wipes the current file preview).
    bool ShouldUpdateCentrePreview(bool is_empty, bool startup_latch,
                                   const std::vector<std::wstring>& explorer_paths) const override;
    void OnPreviewChanged(CMainFrame& frame, PreviewSource source, const std::vector<std::wstring>& paths,
                          PreviewStatus status) override;
    // §7 override (IPreviewPolicy): auto-preview first previewable file when the tree
    // navigates to a new folder (drives viewer from FileTreePanel folder changes).
    void OnExplorerFolderPath(CMainFrame& frame, const std::wstring& folder) override;
    // §7a override (IStartupHandler): load first previewable file + set startup latch
    void ApplyStartupFilePaths(CMainFrame& frame, const std::vector<std::wstring>& paths) override;
    // §7b override (IDeferredInitSequence): skip Shell rebuild when startup paths present
    void DeferredPostLayoutInit(CMainFrame& frame, const std::vector<std::wstring>& pendingPaths) override;
    // §7c override (IStatusBarModel): 4-part status bar (no queue ETA)
    Layout GetStatusBarLayout() const override;
};

} // namespace pmui

#endif
