#ifndef PM_UI_FILEVIEWERPANEL_H
#define PM_UI_FILEVIEWERPANEL_H
//
// Dockable preview panel: wraps a CFileViewer instance inside the standard
// CDockPanelBase / CDockContainerBase shell so it can be placed anywhere in the
// dock tree independently of CMainFrame::m_fileViewer (which remains the primary
// client-view surface and is left untouched).
//
// Primary use-case: docked below CDockFileTree in the viewer and chat workbenches
// so a persistent preview surface is available without occupying the centre area.
//
// Coordinator delegation
// ──────────────────────
// CPreviewCoordinator::Request takes a CFileViewer& argument, so the frame (or
// workbench) can choose to target this panel's viewer instead of m_fileViewer —
// e.g. when the active workbench has no primary viewer client-view (chat mode)
// and this panel is the only visible preview surface.  GetFileViewer() exposes
// the inner instance for that purpose.
//
#include "stdafx.h"
#include "helpers/dock_helpers.h"
#include "FileViewer.h"

// ── CViewerPanelContainer ────────────────────────────────────────────────────
// Dock container that hosts a CFileViewer as its sole view.
class CViewerPanelContainer : public CDockContainerBase
{
public:
    CViewerPanelContainer();
    virtual ~CViewerPanelContainer() override;

    CFileViewer& GetFileViewer() { return m_viewer; }
    bool IsPinned() const noexcept { return m_pinned; }
    void SetPinned(bool pinned);
    void SetTitle(const CString& title);

    /// Convenience passthrough — call instead of GetFileViewer().OpenFile() when
    /// the container is the only handle in scope.
    pmui::PreviewStatus OpenFile(const std::vector<std::wstring>& paths,
                                 pmui::PreviewSource              source)
    {
        return m_viewer.OpenFile(paths, source);
    }

private:
    CViewerPanelContainer(const CViewerPanelContainer&)            = delete;
    CViewerPanelContainer& operator=(const CViewerPanelContainer&) = delete;

    int OnCreate(CREATESTRUCT& cs) override;
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;
    void LayoutPinButton();
    void RefreshTitle();
    void UpdatePinButton();
    HICON TabIconForState(bool pinned);

    CFileViewer m_viewer;
    HWND m_hPinButton = nullptr;
    HICON m_hPinnedIcon = nullptr;
    HICON m_hUnpinnedIcon = nullptr;
    CString m_baseTabText;
    CString m_baseCaption;
    bool m_pinned = false;
};

// Root container used as the centre view for viewer tabs. It mirrors the
// Win32++ DockContainer sample: the first/only tab cannot be closed or undocked,
// but additional tabs behave as normal dockable containers.
class CMainViewerContainer : public CDockContainerBase
{
public:
    CMainViewerContainer() = default;
    virtual ~CMainViewerContainer() override = default;
    CViewerPanelContainer* ActiveViewerContainer() const;
    void ToggleActivePinned();
    bool IsActivePinned() const;

protected:
    int OnCreate(CREATESTRUCT& cs) override;
    void AddContainer(CDockContainer* pContainer, BOOL insert, BOOL selectPage) override;
    void RemoveContainer(CDockContainer* pContainer, BOOL updateParent) override;
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    bool TogglePinnedFromTabIcon(LPARAM lparam);
    void LayoutPinButton();
    void UpdatePinButton();

    HWND m_hPinButton = nullptr;
};

// ── CDockViewerPanel ─────────────────────────────────────────────────────────
// Docker wrapping CViewerPanelContainer.
class CDockViewerPanel : public CDockPanelBase
{
public:
    CDockViewerPanel();
    virtual ~CDockViewerPanel() override = default;

    CViewerPanelContainer& GetViewerPanelContainer() { return m_container; }
    CFileViewer&           GetFileViewer()            { return m_container.GetFileViewer(); }
    bool IsPinned() const noexcept { return m_container.IsPinned(); }
    void SetPinned(bool pinned) { m_container.SetPinned(pinned); }
    void SetTitle(const CString& title) { m_container.SetTitle(title); }

private:
    CDockViewerPanel(const CDockViewerPanel&)            = delete;
    CDockViewerPanel& operator=(const CDockViewerPanel&) = delete;

    CViewerPanelContainer m_container;
};

#endif // PM_UI_FILEVIEWERPANEL_H
