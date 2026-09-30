#ifndef PM_UI_FILETREEPANEL_H
#define PM_UI_FILETREEPANEL_H

#include "stdafx.h"
#include "helpers/dock_helpers.h"
#include "file_extensions.hpp"
#include <shobjidl.h>
#include <filesystem>
#include <string>

class CExplorerBrowserView;

// ── ExplorerColumnSpec ───────────────────────────────────────────────────────
/// Lightweight in-process column descriptor; GUID kept as a native type for
/// direct use with IColumnManager / PROPERTYKEY.  Conversion to/from the
/// JSON-friendly FiletreeColumnSpec (LayoutStore) happens in Mainfrm_layout.
struct ExplorerColumnSpec {
    GUID  fmtid{};
    DWORD pid   = 0;
    UINT  width = 120;  ///< Column width in pixels
};

// ── CShellGlobFilter ─────────────────────────────────────────────────────────
// IFolderFilter implementation for glob-mask filtering of the Details view.
// Owned by value inside CExplorerBrowserView — never heap-deleted via Release().
// Installed via IFolderFilterSite::SetFilter; survives folder navigations.
// Folders are always shown so the user can browse into them freely.
// ShouldShow is the hot path: it returns S_OK (show) or S_FALSE (hide).
class CShellGlobFilter : public IFolderFilter
{
public:
    void SetMask(const std::wstring& mask) { m_mask = mask; }
    const std::wstring& GetMask() const noexcept { return m_mask; }

    // IUnknown — ref-counted but never self-deleting (lifetime = CExplorerBrowserView)
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IFolderFilter) {
            *ppv = static_cast<IFolderFilter*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override { if (m_ref) --m_ref; return m_ref; }

    // IFolderFilter — implemented in FileTreePanel.cpp
    STDMETHODIMP ShouldShow(IShellFolder* psf,
                             PCIDLIST_ABSOLUTE pidlFolder,
                             PCUITEMID_CHILD   pidlItem) override;
    STDMETHODIMP GetEnumFlags(IShellFolder*, PCIDLIST_ABSOLUTE,
                               HWND*, DWORD* pFlags) override
    {
        if (pFlags) *pFlags = SHCONTF_FOLDERS | SHCONTF_NONFOLDERS;
        return S_OK;
    }

private:
    std::wstring m_mask = L"*.*";
    ULONG        m_ref  = 0;   // managed externally; never triggers delete
};

/// §12: Heap-allocated payload for UWM_EXPLORER_SELECTION (WPARAM).
/// Freed by the receiver (CMainFrame::OnExplorerSelection).
struct ExplorerSelectionMsg {
    std::vector<std::wstring> paths;
    uint32_t navGeneration = 0;   ///< Navigation epoch from CExplorerBrowserView
    bool     ctrlDown      = false;
};

// ── IExplorerBrowserEvents sink ──────────────────────────────────────────────
// Tracks the current folder path (for settings.json persistence) and clears
// the last-selected-file cache on each navigation so stale paths don't linger.
// Also notifies the host CExplorerBrowserView whenever a new IShellView is
// created or a navigation completes, so the view can (re)subclass the inner
// SysListView32 to add custom keyboard shortcuts (Backspace = up,
// Delete = move to Recycle Bin).
class CShellEventSink : public IExplorerBrowserEvents
{
public:
    CShellEventSink(std::wstring* pCurrentFolder,
                    std::wstring* pLastSelected,
                    CExplorerBrowserView* pView)
        : m_pCurrentFolder(pCurrentFolder)
        , m_pLastSelected(pLastSelected)
        , m_pView(pView) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IExplorerBrowserEvents) {
            *ppv = static_cast<IExplorerBrowserEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    // IExplorerBrowserEvents — implementations live in the .cpp so they can
    // call back into CExplorerBrowserView (forward-declared above).
    STDMETHODIMP OnViewCreated(IShellView* psv) override;
    STDMETHODIMP OnNavigationPending(PCIDLIST_ABSOLUTE) override { return S_OK; }
    STDMETHODIMP OnNavigationFailed(PCIDLIST_ABSOLUTE) override  { return S_OK; }
    STDMETHODIMP OnNavigationComplete(PCIDLIST_ABSOLUTE pidl) override;

private:
    std::wstring*         m_pCurrentFolder = nullptr;
    std::wstring*         m_pLastSelected  = nullptr;
    CExplorerBrowserView* m_pView          = nullptr;
    ULONG                 m_ref            = 1;
};

// ── CExplorerBrowserView ─────────────────────────────────────────────────────
// Hosts IExplorerBrowser: Details view (FVM_DETAILS). Optional `EBO_SHOWFRAMES`
// (address bar, nav tree, Shell command rows) via SetShowShellFrames / settings.json
// `window.filetree_show_shell_frames`.
//
// Chrome / borders: `InitBrowser` always sets `EBO_NOWRAPPERWINDOW | EBO_NOBORDER` so
// the host draws the dock without Shell’s extra wrapper border (see .cpp). This is
// **not** the same path as app-owned combos/edits — those are themed in
// `helpers/theme.cpp` (`apply_window_theme_recursive`, dark `COMBOBOX` → `DarkMode_CFD`, etc.).
// `CMainFrame::ApplyAppearance` intentionally does **not** run that walk on the File
// tree docker (Shell COM owns the inner HWNDs; theming the subtree breaks IExplorerBrowser).
// **Font size:** the frame still calls `pmui::apply_font_to_tree` on the file-tree *docker*
// (same as other panels). That can update chrome that honors `WM_SETFONT`; the hosted
// Explorer `IShellView` list, header, and address UI may keep Shell/system sizing — there
// is no supported API to retarget the entire control to `pmui::ui_font` like a plain ListView.
// IExplorerBrowser creation is deferred via PostMessage to avoid the
// MapWindowPoints(0,0) assertion in Win32++'s CDockClient::DrawCloseButton.
//
// Selection tracking: a 250 ms WM_TIMER polls IFolderView2::GetSelection.
// IExplorerBrowserEvents has no file-selection callback, so polling is the
// standard approach. On each tick, if the selection changed, UWM_EXPLORER_SELECTION
// is posted with filesystem dirs, image paths, and text paths (the frame routes
// dirs + images to batch commands).
//
// Current folder path is mirrored on the main window status bar (see
// UWM_EXPLORER_FOLDER_PATH in CMainFrame).
class CExplorerBrowserView : public CWnd
{
public:
    /// Explicit lifecycle states — see `docs/selection-structural.md` §10.
    /// `CheckSelection` (selection timer) only runs in `Ready`; the timer is
    /// killed on `Rebuilding` and restarted when `Ready` is reached again.
    /// This prevents stale empty-selection posts during Shell destroy/recreate.
    enum class BrowserState { PreInit, Initialising, Ready, Rebuilding, Destroyed };

    CExplorerBrowserView() = default;
    virtual ~CExplorerBrowserView() override;

    static constexpr UINT WM_EB_INIT      = WM_APP + 300;
    /// Defer one tick so navigation runs after the dock is shown / sized (IExplorerBrowser needs a valid SetRect).
    static constexpr UINT WM_EB_DEFERRED_NAV = WM_APP + 301;
    /// Re-run InitBrowser (destroy/create IExplorerBrowser) after the docker is re-attached
    /// so the Shell rebinds to the client HWND; `SetRect` alone is not always enough.
    static constexpr UINT WM_EB_REDOCK = WM_APP + 302;
    static constexpr UINT TIMER_SELECTION    = 1;
    /// Short-interval timer that retries SelectFile after OnNavigationComplete if the
    /// Shell view was still enumerating items when the first attempt ran.
    static constexpr UINT TIMER_SELECT_RETRY = 3;
    static constexpr int  k_select_retry_max = 8;   // 8 × 80 ms = 640 ms covers shell background refresh
    static constexpr int  k_select_retry_ms  = 80;

    /** Call before WM_EB_INIT is dispatched to restore the last-browsed folder. */
    void SetInitialFolder(std::wstring folder) { m_initialFolder = std::move(folder); }
    /** Call at startup to record which files were requested for initial selection (tracking only). */
    void SetInitialFiles(std::vector<std::wstring> files) { m_initialFiles = std::move(files); }

    /** Shell chrome: address bar, navigation pane, etc. Rebuilds the browser if already up. */
    void SetShowShellFrames(bool show);
    bool GetShowShellFrames() const { return m_showShellFrames; }

    /** Glob filter applied to the Details view (e.g. L"*.jpg;*.png").
     *  L"*.*" or empty = no filtering (pass-through).
     *  Folders are always shown regardless of the mask.
     *  Takes effect immediately when the browser is up (triggers a re-navigation). */
    void SetFilterMask(const std::wstring& mask);
    const std::wstring& GetFilterMask() const noexcept { return m_filterMask; }

    /** Ordered column descriptors to restore after each navigation.
     *  Empty = leave Shell defaults untouched. */
    void SetColumnSpecs(std::vector<ExplorerColumnSpec> specs) { m_columnSpecs = std::move(specs); }
    const std::vector<ExplorerColumnSpec>& GetColumnSpecs() const noexcept { return m_columnSpecs; }

    /** Read current visible columns + widths from IColumnManager into m_columnSpecs.
     *  No-op when the browser is not ready.  Call before serialising the layout. */
    void SnapshotColumnSettings();

    /** Apply m_columnSpecs to the live IColumnManager.
     *  Called by CShellEventSink::OnNavigationComplete after each folder change. */
    void ApplyColumnSettings();

    /** View mode (FOLDERVIEWMODE) and icon/thumbnail pixel size.
     *  mode = FVM_DETAILS(4) by default; size = 0 means Shell-default for that mode.
     *  Setting either value to 0 / FVM_AUTO leaves that aspect at Shell default. */
    void  SetViewMode(int mode, int iconSize = 0) { m_viewMode = mode; m_iconSize = iconSize; }
    int   GetViewMode()   const noexcept { return m_viewMode; }
    int   GetIconSize()   const noexcept { return m_iconSize; }

    /** Snapshot the live IFolderView2 view mode into m_viewMode / m_iconSize. */
    void  SnapshotViewMode();

    /** Apply m_viewMode / m_iconSize via IFolderView2::SetViewModeAndIconSize.
     *  Called by CShellEventSink::OnNavigationComplete. */
    void  ApplyViewMode();

    /** Last successfully navigated folder path (empty for virtual folders). */
    const std::wstring& GetCurrentFolder() const { return m_currentFolder; }

    /** Programmatically navigate the in-app browser to the given folder.
     *  Returns S_OK if BrowseToIDList was initiated, E_FAIL if the PIDL could
     *  not be resolved, or E_POINTER if the browser isn't initialised yet
     *  (path queued as initial folder instead).  S_OK does NOT mean navigation
     *  completed — completion fires OnNavigationComplete asynchronously. */
    HRESULT NavigateToFolder(const std::wstring& folder);
    /** Post one deferred navigation (preferred when the Files dock was just shown). */
    void RequestNavigateToFolder(const std::wstring& folder);

    // ── keyboard-shortcut hooks (called from the SysListView32 subclass) ──
    /** History back / forward (folder stack). Bound to mouse X1 / X2. */
    void NavigateBack();
    void NavigateForward();
    /** Navigate one level up (parent folder). Bound to Backspace. */
    void NavigateUp();
    /** Move the current file selection to the Recycle Bin. Bound to Delete. */
    void DeleteSelectionToRecycleBin();
    /** Select a file by name in the current folder view.
     *  `initialReveal` = true  → first programmatic reveal: deselect others,
     *                             grab keyboard focus (SVSI_DESELECTOTHERS|SVSI_FOCUSED).
     *  `initialReveal` = false → re-assertion during retry: keep any user selection
     *                             intact, no focus grab (SVSI_SELECT only).
     *  Returns true if IShellView::SelectItem succeeded, false if the view
     *  was not yet ready (items still enumerating) — caller should retry. */
    bool SelectFile(const std::wstring& filename, bool initialReveal = true);
    /** Start the retry timer so SelectFile is re-attempted until it succeeds
     *  or k_select_retry_max ticks expire. Safe to call from the event sink. */
    void ScheduleSelectRetry(const std::wstring& filename);

    // ── Debug accessors ───────────────────────────────────────────────────────
    const std::wstring&              GetInitialFolder() const noexcept { return m_initialFolder; }
    const std::vector<std::wstring>& GetInitialFiles()  const noexcept { return m_initialFiles; }
    const std::wstring& GetSelectRetryFile()  const noexcept { return m_selectRetryFile; }
    int                 GetSelectRetryCount() const noexcept { return m_selectRetryCount; }
    /** Request selection of a file after next navigation completes. */
    void SetPendingSelectFile(const std::wstring& filename) { m_pendingSelectFile = filename; }
    /** Get and clear the pending file to select (called by event sink on nav complete). */
    std::wstring TakePendingSelectFile() { std::wstring f; f.swap(m_pendingSelectFile); return f; }

    /** Locate the inner Shell SysListView32 and (re)attach our keyboard hook.
     *  Called by CShellEventSink on OnViewCreated / OnNavigationComplete since
     *  the listview can be recreated when the folder changes. Idempotent. */
    void RebindListKeySubclass();

    /// Re-push the host client rect to IExplorerBrowser after layout is final (e.g. after
    /// `LockWindowUpdate` in deferred init or re-dock). Does not destroy the COM object.
    void SyncShellHostLayout();

    /// Re-apply the app theme to the Explorer host. Recreates IExplorerBrowser only when
    /// Light/Dark actually changed so Shell-owned child HWNDs can pick up app mode.
    void RefreshThemeChrome();

    /// `RecalcLayout` should run first; this posts a one-tick full Shell recreate (same
    /// pattern as `SetShowShellFrames` without toggling that option) after re-dock.
    void RequestRebuildAfterReDock();

    /// §12: Monotonic navigation epoch — incremented on every InitBrowser /
    /// NavigateToFolder / RebuildShellAfterReDock.  Included in
    /// ExplorerSelectionMsg so the frame can discard stale empty-selection posts.
    uint32_t GetNavGeneration() const noexcept { return m_navGeneration; }

protected:
    virtual void PreCreate(CREATESTRUCT& cs) override;
    virtual int  OnCreate(CREATESTRUCT& cs) override;
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;
    /// Pre-pump hook: catches Backspace / Delete *before* the Shell's hosted
    /// view sees them. The SysListView32 subclass we install in
    /// RebindListKeySubclass() is unreliable because the IExplorerBrowser
    /// recreates the inner listview on navigation and Win32++'s dock client
    /// can keep focus from settling on it — PreTranslateMessage runs from
    /// CMessagePump::PreTranslateMessage walking up msg.hwnd's parent chain,
    /// so any key whose focused window is a descendant of this view triggers
    /// us regardless of where focus actually lives.
    virtual BOOL PreTranslateMessage(MSG& msg) override;

private:
    CExplorerBrowserView(const CExplorerBrowserView&) = delete;
    CExplorerBrowserView& operator=(const CExplorerBrowserView&) = delete;

    void InitBrowser();
    void DestroyBrowser();
    void RebuildShellAfterReDock();
    void CheckSelection();          // called on TIMER_SELECTION tick
    void InstallFilter();           // (re)attach m_filter via IFolderFilterSite

    IExplorerBrowser* m_peb          = nullptr;
    CShellEventSink*  m_pSink        = nullptr;
    DWORD             m_adviseCookie = 0;
    BrowserState      m_browserState = BrowserState::PreInit;
    bool              m_showShellFrames = false;
    CShellGlobFilter  m_filter;         // owned by value; installed via IFolderFilterSite
    std::wstring      m_filterMask = L"*.*";
    std::vector<ExplorerColumnSpec> m_columnSpecs; // empty → don't touch Shell column defaults
    int               m_viewMode = 4;   // FVM_DETAILS; 0/FVM_AUTO = Shell default
    int               m_iconSize = 0;   // 0 = Shell default for the active view mode

    std::wstring              m_initialFolder;  // set from settings `workbench.main.window` in CMainFrame::LoadLayout
    std::vector<std::wstring> m_initialFiles;   // set at startup to track requested initial file selections (debug only)
    std::wstring m_currentFolder;    // updated by CShellEventSink
    std::wstring m_lastSelectedPath; // tracks last notified path (dedup)
    std::wstring m_pendingSelectFile; // file to select after next navigation (shift+click reveal)
    std::wstring m_selectRetryFile;  // filename being retried by TIMER_SELECT_RETRY
    int          m_selectRetryCount = 0; // remaining retry attempts

    HWND m_hSubclassedList = nullptr; // current SysListView32 we have hooked
    uint32_t m_navGeneration = 0;     // §12: monotonic navigation epoch
    bool m_hasThemeSnapshot = false;
    bool m_lastThemeDark = false;
public:
    /// §12: current navigation epoch — read by CMainFrame to snapshot the
    /// generation at startup latch time.
    uint32_t NavGeneration() const noexcept { return m_navGeneration; }
};

// ── CFileTreeContainer ───────────────────────────────────────────────────────
class CFileTreeContainer : public CDockContainerBase
{
public:
    CFileTreeContainer();
    virtual ~CFileTreeContainer() override = default;
    CExplorerBrowserView& GetBrowserView() { return m_view; }

private:
    CFileTreeContainer(const CFileTreeContainer&) = delete;
    CFileTreeContainer& operator=(const CFileTreeContainer&) = delete;
    CExplorerBrowserView m_view;
};

// ── CDockFileTree ────────────────────────────────────────────────────────────
class CDockFileTree : public CDockPanelBase
{
public:
    CDockFileTree();
    virtual ~CDockFileTree() override = default;
    CFileTreeContainer& GetFileTreeContainer() { return m_container; }

private:
    CDockFileTree(const CDockFileTree&) = delete;
    CDockFileTree& operator=(const CDockFileTree&) = delete;
    CFileTreeContainer m_container;
};

#endif // PM_UI_FILETREEPANEL_H
