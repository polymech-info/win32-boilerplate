#ifndef PM_UI_MAINFRM_H
#define PM_UI_MAINFRM_H

#include "features.h"         // feature-flag documentation
#include "ChatWebPanel.h"
#include "FileViewer.h"
#include "ViewerManager.h"
#include "FileQueue.h"
#include "SettingsPanel.h"
#include "LogPanel.h"
#include "FindPanel.h"
#include "DuplicatePanel.h"
#include "FileTreePanel.h"
#ifdef FEATURE_NODES
#include "NodesPanel.h"
#endif
#include "FileViewerPanel.h"
#ifdef FEATURE_BROWSER
#include "win/web/CWebViewManager.h"
#endif
#ifdef FEATURE_CONSOLE
#include "win/web/CWebConsole.h"
#endif
#include "Resource.h"
#include "workbench/MainWorkbench.h"
#include "PreviewCoordinator.h"
#include "ExplorerSelectionRouter.h"
#include "pm_win32_dock_json.hpp"
#ifdef FEATURE_USE_OWN_RIBBON
#include "OwnRibbonTab.h"
#endif
#include "win/settings_store.hpp"
#include "core/batch_queue.hpp"
#include "core/transform.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <queue>
#include <vector>

struct PromptPreset {
    std::string name;
    std::string prompt;
};

#ifdef FEATURE_USE_OWN_RIBBON
class CMainFrame : public Win32xx::CDockFrame
#else
class CMainFrame : public Win32xx::CRibbonDockFrame
#endif
{
    friend class pmui::IWorkbench;
    friend class pmui::IPreviewPolicy;
    friend class pmui::IStartupHandler;
    friend class pmui::IDeferredInitSequence;
    friend class pmui::IStatusBarModel;
    friend class pmui::CDefaultMainWorkbench;
    friend class pmui::CChatSimpleWorkbench;
    friend class pmui::CViewerSimpleWorkbench;

public:
    // ── Stable dock IDs (persisted in win32_dock JSON — never change) ─────────
    static constexpr int DOCK_ID_QUEUE      = 1;
    static constexpr int DOCK_ID_LOG        = 2;
    static constexpr int DOCK_ID_SETTINGS   = 3;
    static constexpr int DOCK_ID_FILEINFO   = 5;
#ifdef FEATURE_NODES
    static constexpr int DOCK_ID_NODES      = 6;
#endif
    static constexpr int DOCK_ID_FILETREE   = 7;
    static constexpr int DOCK_ID_FINDRESULTS = 8;
    static constexpr int DOCK_ID_CHAT        = 9;
    static constexpr int DOCK_ID_DUPLICATERESULTS = 10;
    static constexpr int DOCK_ID_VIEWER_PANEL     = 12;
    static constexpr int DOCK_ID_VIEWER_TAB_FIRST = 100;
    static constexpr int DOCK_ID_VIEWER_TAB_LAST  = 149;
#ifdef FEATURE_CONSOLE
    static constexpr int DOCK_ID_CONSOLE          = 13;
#endif

    CMainFrame();
    virtual ~CMainFrame() override = default;
    virtual HWND Create(HWND parent = nullptr) override;

    /// When @p openSingleFilePreview is false, skip auto-opening the single-file
    /// preview (used when enqueue feeds an immediate batch op — avoids UI-thread
    /// vips/RAW preview racing the worker, same as bulk folder adds).
    void AddFilesToQueue(const std::vector<std::wstring>& paths, bool openSingleFilePreview = true);
    /// Seeds queue + chat context after docks exist (CLI / Explorer `--ui-preset ... --src`).
    void SetPendingStartupEnqueue(const std::vector<std::wstring>& paths, bool open_chat);
#if defined(_WIN32)
    /// `launch_ui_next_screenshot_probe`: wait, capture main HWND to PNG, exit (CLI `test screenshot` / `app takescreenshot`).
    /// Optional @p win_w / @p win_h: outer window size in pixels (both > 0); applied after layout restore, before capture.
    void SetScreenshotProbe(std::wstring out_path, int wait_ms, int win_w = 0, int win_h = 0);
    /// CLI `replay --path=…` — after `LoadLayout`, apply `snapshot.window_layout` from this file.
    void SetSessionReplayPath(std::wstring session_json_path);
    /// CLI `--layout <path>` — apply exported layout before first show; do not persist settings.
    void SetLayoutOverridePath(std::wstring layout_json_path);
    /// CLI launch without --src: navigate file tree to cwd instead of restoring saved folder.
    void SetCliCwdStartupFolder(std::wstring cwd_path);
    /// Session record (Ctrl+R / `pm-image app recordstart`); stop with Ctrl+H or `recordstop`.
    void StartSessionRecording();
    void StopSessionRecording();
    /// Main-window MP4 under Videos (Ctrl+Alt+R / `app videorecordstart`, also resumes when paused); stop with Ctrl+Alt+H or `videorecordstop`; pause/resume with Ctrl+Alt+P or `videorecordpause` (WGC+MF when built).
    void StartSessionVideoRecording();
    void StopSessionVideoRecording();
    void ToggleSessionVideoPause();
#endif
    void LogMessage(const CString& msg);

    /// Push the current Explorer selection + current folder into the chat
    /// dock (no-op if the dock isn't created yet). Called from
    /// OnExplorerSelection so the chat agent always sees the latest context,
    /// and from CChatWebView::OnSendClicked (synchronously) right before each
    /// turn so the model never operates on a stale snapshot.
    /// When @p explorer_ctrl_additive is true (Explorer posted selection with Ctrl held),
    /// the web chat may append newly added image paths to the explicit extras list.
    void RefreshChatContext(bool explorer_ctrl_additive = false);

    /// Primary client preview (`CFileViewer` — `FileViewer.cpp` plus `FileViewer_*.cpp`). Additional
    /// instances may be hosted in docks; this remains the main surface for queue
    /// / Explorer / transform output.
    pmui::CViewerManager&       ViewerManager() noexcept { return m_viewerManager; }
    const pmui::CViewerManager& ViewerManager() const noexcept { return m_viewerManager; }
    CFileViewer&       FileViewer() { return m_viewerManager.ActiveView(); }
    const CFileViewer& FileViewer() const { return m_viewerManager.ActiveView(); }
    /// §8 preview state — updated when the centre viewer navigates prev/next without a new @c Request.
    pmui::CPreviewCoordinator&       PreviewCoordinator() noexcept { return m_previewCoord; }
    const pmui::CPreviewCoordinator& PreviewCoordinator() const noexcept { return m_previewCoord; }
    /// @c true when @c settings.json has @c ui.workbench @c "chat" (centre client is workbench chat, not image preview).
    bool IsChatWorkbench() const;
    /// @c true for the minimal @c viewer workbench (@c CFileViewer client, no tool docks by default).
    bool IsViewerWorkbench() const;
    /// §1 startup preview latch (viewer `--src`); used by `CPreviewCoordinator` to gate Explorer updates.
    bool StartupPreviewLatchActive() const noexcept { return m_startupPreviewLatch; }

    /// App-wide hotkeys (session JSON, video, screenshot, fullscreen). Invoked
    /// from `CPmImageApp::PreTranslateMessage` *before* the default pump so keys
    /// are handled even when focus is on a hosted HWND that is not a child of
    /// the main frame in the Win32 parent chain (e.g. some WebView2 / Chromium
    /// surfaces). Return TRUE if the message was handled.
    BOOL TryProcessGlobalHotkeys(MSG& msg);

#ifdef FEATURE_USE_OWN_RIBBON
    /// COwnRibbonTab reads batch / toggle state without friending the whole frame.
    bool RibbonIsToggleOn(UINT32 cmdID) const { return IsToggleSelected(cmdID); }
    bool RibbonProcessing() const { return m_processing; }
    bool RibbonBatchPaused() const { return m_batchCtrl && m_batchCtrl->paused.load(); }
    bool RibbonHasQueueItems() const;
#endif

protected:
#ifndef FEATURE_USE_OWN_RIBBON
    // ── Ribbon IUIApplication ─────────────────────────────────────────────────
    virtual STDMETHODIMP Execute(UINT32, UI_EXECUTIONVERB, const PROPERTYKEY*,
                                  const PROPVARIANT*, IUISimplePropertySet*) override;
    virtual STDMETHODIMP UpdateProperty(UINT32, REFPROPERTYKEY,
                                         const PROPVARIANT*, PROPVARIANT*) override;
    virtual STDMETHODIMP OnViewChanged(UINT32, UI_VIEWTYPE, IUnknown*,
                                        UI_VIEWVERB, INT32) override;
#endif

    // ── Win32++ overrides ─────────────────────────────────────────────────────
    // TRIAL (may revert): frame Create() does not call ShowWindow until OnInitialUpdate ends.
    virtual void    PreCreate(CREATESTRUCT& cs) override;
    /// TRIAL: suppress repaints while Win32++ recreates dockers from settings.json `win32_dock`.
    virtual BOOL    LoadDockLayout() override;
    virtual BOOL    SaveDockLayout() override;
    /// Tab order from `win32_dock.containers` in settings.json.
    virtual BOOL    LoadDockContainers() override;
    virtual BOOL    OnCommand(WPARAM wparam, LPARAM lparam) override;
    virtual BOOL    OnHelp() override;
    virtual void    OnInitialUpdate() override;    // Mainfrm_layout.cpp
    virtual void    SetupToolBar() override;
    /// Win32++: dock activation (focus / title-bar clicks). Heavy work such as
    /// ApplyAppearance() must not run here — UWM_DOCKACTIVATE fires often.
    virtual LRESULT OnDockActivated(UINT msg, WPARAM wparam, LPARAM lparam) override;
    /// Win32++: prune dynamic viewer-tab pointers after close/destroy.
    virtual LRESULT OnDockDestroyed(UINT msg, WPARAM wparam, LPARAM lparam) override;
    /// Win32++: drag-to-dock completed (float → dock). After this, run the same frame
    /// relayout as `ResetLayout` so the client view + `IExplorerBrowser` host stay in sync.
    virtual LRESULT OnDockEnd(Win32xx::DragPos* pDragPos) override;
    /// Tab-order / active page restore (when `defer_dock_containers` is true) — wxx + posted message.
    LRESULT OnPmLoadDockContainers();
    virtual LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;
    /// Forwards to the dock frame; global hotkeys run from `TryProcessGlobalHotkeys`
    /// via `CPmImageApp::PreTranslateMessage` first.
    virtual BOOL    PreTranslateMessage(MSG& msg) override;

#ifdef FEATURE_USE_OWN_RIBBON
    virtual CRect GetViewRect() const override;
    virtual int   OnCreate(CREATESTRUCT& cs) override;
    /// Win32++ dark popup path uses pure black + blue gutter; use [`ThemePalette`] instead.
    void DrawMenuItemBkgnd(LPDRAWITEMSTRUCT pDrawItem) override;
    /// Fills default (non-hover) menu items with a dark `window_bg` — base custom-draw only
    /// repaints hot/selected, leaving a white band behind the menu labels otherwise.
    LRESULT CustomDrawMenuBar(NMHDR* pNMHDR) override;
#endif

private:
    friend bool pm::win32_dock::apply_dock_tree_from_json(CMainFrame&, const nlohmann::json&, std::string&);
    friend nlohmann::json pm::win32_dock::serialize_dock_to_json(CMainFrame&);
    friend bool pm::win32_dock::apply_containers_from_json(CMainFrame&, const nlohmann::json&, std::string&);
    CMainFrame(const CMainFrame&) = delete;
    CMainFrame& operator=(const CMainFrame&) = delete;

    // ── Commands: queue (Mainfrm_queue.cpp), rest (Mainfrm_commands.cpp) ─────
    void OnAddFiles();
    void OnAddFolder();
    void PushRecentFilesFromUserAdd(const std::vector<std::wstring>& files);
    void PushRecentFolderExplicit(const std::wstring& folder);
    /// Explorer `IExplorerBrowser` folder changes — skips MRU when only moving up/down the same tree.
    void PushRecentFolderFromShellPath(const std::wstring& folder);
    void RestoreExplorerRecentMruBaselineFromLayout(const std::string& filetree_folder_utf8);
    void SyncRecentFilesMenuPopup(HMENU hPopup);
    void SyncRecentFoldersMenuPopup(HMENU hPopup);
    /// ReBar `CMenuBar` owns the menu — `::GetMenu(main_hwnd)` is often NULL.
    HMENU AppMenuHandle() const;
    void OnRecentFileMenu(UINT id);
    void OnRecentFolderMenu(UINT id);
    void OnClearQueue();
    void OnResize();
    void OnSaveAs();
    void OnFileSaveImagePreview();
    void OnFileSaveImagePreviewAs();
    void OnFileSaveLayout();
    void OnFileLoadLayout();
    void OnRun();
    void OnCompress();
    void OnMeta();
    void OnFind();
    void OnDuplicates();
    void OnChat();
    void OnExit();
    void OnUpdateExplorer();
    void OnUnregisterExplorer();
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
    void OnPixlwizLogin();
    void OnPixlwizLogout();
    LRESULT OnPixlwizLoginDone(WPARAM wparam, LPARAM lparam);
#endif
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    void OnPixlwizShare();
#endif
    std::vector<int> GetSelectedQueueItems();
    /// If the queue is empty, enqueue images under the Explorer file tree’s current
    /// folder (same as Add folder: recursive). Returns true if the queue is non-empty after.
    bool TryEnqueueCurrentExplorerFolderIfEmpty();

    // ── Batch queue control (Mainfrm_commands.cpp — pause/save session, etc.) ─
    void OnPauseBatch();
    void OnResumeBatch();
    void OnCancelBatch();
    void OnSaveSession();
    void OnLoadSession();

    // ── Settings mode / workbench (Mainfrm.cpp) ───────────────────────────────
    void SwitchSettingsMode(CSettingsView::Mode mode);
#ifdef FEATURE_NODES
    enum class Workbench { Standard, Nodes };
    void SwitchWorkbench(Workbench wb);
#endif

    // ── Ribbon helpers (Mainfrm.cpp) ──────────────────────────────────────────
    void InvalidateToggle(UINT32 cmdID);
    bool IsToggleSelected(UINT32 cmdID) const;
    /// Dock panel show/hide for View menu + ribbon (IDs `IDC_CMD_VIEW_*`).
    void RunViewPanelCommand(UINT32 cmdID);
    void ToggleCentreViewerTabs(bool persist);
    void ApplyCentreViewerTabbed(bool tabbed, bool persist);
    void ToggleActiveViewerTabPinned(bool persist);
    bool IsViewerTabDocker(const CDocker* docker) const;
    bool IsViewerTabChromeDocker(const CDocker* docker) const;
    bool IsViewerTabReferencedByGroup(const CDockViewerPanel* panel) const;
    void RefreshViewerDockTabs();
    void PruneOrphanedViewerDockTabs();
    int AllocateViewerDockTabId();
    CDockViewerPanel* EnsureDockableViewerTab(bool forceNew);
    CDockViewerPanel* ActiveDockableViewerTab();
    CFileViewer& LiveDockableViewerForExplorer(bool* created = nullptr);
    /// Reflect panel visibility on the View menu checkmarks.
    void SyncViewMenuChecks();
#ifdef FEATURE_USE_OWN_RIBBON
    void RunRibbonCommandId(UINT32 cmdID);
    /// Client Y for the own-ribbon strip: top of client, or just below the ReBar menu band.
    int OwnRibbonClientTopY() const;
#endif
    /// Batch toolbar / ribbon pause-resume state.
    void InvalidateBatchUi();

    // ── Appearance ────────────────────────────────────────────────────────────
    /// Apply theme + font to every dock panel + the frame. When @p reread_settings
    /// is false, uses the already-loaded palette/font (launch + OnInitialUpdate first
    /// call) to avoid a duplicate settings.json read and DWM churn.
    void ApplyAppearance(bool reread_settings = true);
    /// Toggle the persisted global theme override between Light and Dark, then refresh all hosts.
    void ToggleGlobalThemeOverride();
    /// Open the modal "App Settings" dialog and re-apply on Save.
    void OnAppSettings();
#ifdef FEATURE_BROWSER
    /// Open (or bring to front) the floating Web Browser popup window.
    void ShowBrowserPopup();
    /// Open a managed browser popup at a URL, used by hosted web panels.
    void ShowBrowserPopupUrl(const std::string& url);
    /// Open (or bring to front) the web settings popup.
    void ShowSettingsPopup();
    /// Open (or bring to front) the transparent Viewer Browser popup window.
    void ShowViewerBrowserPopup();
    /// Open/focus a named playground popup managed by CWebViewManager.
    void ShowViewerBrowserPopupInstance(const std::string& id, bool modal);
    /// Shared WebView host commands (file preview, Explorer, settings, panel toggles).
    bool HandleWebHostCommand(const std::string& fromId, const std::string& json_utf8);
#endif
#if defined(FEATURE_BROWSER) && defined(FEATURE_CONSOLE)
    void WireConsoleBus();
    /// Single entry point for host-initiated direct console execution (ribbon, web bus, etc.).
    /// Shows the docked console, posts `console_run` over cweb_bus, shell app waits for shell_ready.
    bool RunInWebConsole(const std::string& line_utf8, bool new_shell = true, bool close_on_exit = false);
#endif
#if defined(FEATURE_BROWSER) && defined(FEATURE_CHAT_WEB)
    void WireChatBus();
#endif
#if defined(FEATURE_BROWSER) && (defined(FEATURE_VIEWER_WEB) || defined(FEATURE_HOME_PAGE))
    void WireFileViewerBus();
#endif
    /// First call after the ReBar + status exist: apply `m_workbenchChrome` (main menu, status).
    void ApplyWorkbenchFrameChromeOnce();

    // ── Dock layout (Mainfrm_layout.cpp) ─────────────────────────────────────
    virtual DockPtr NewDockerFromID(int dockID) override;
    void SaveLayout();
    void LoadLayout();
    /// One-shot exported layout override (`--layout`): applies dock + window data without persistence.
    void ApplyLayoutOverrideFromPath();
    /// Fills @p layout from the current main-frame placement + panel visibility (used by `SaveLayout` and session record).
    /// @return false if `GetWindowPlacement` failed (same as previous `SaveLayout` early exit).
    bool FillWindowLayoutFromState(media::settings::WindowLayout& layout);
    /// Applies placement + panel visibility + Explorer folder (same rules as `LoadLayout` for a given struct).
    void ApplyWindowLayoutData(const media::settings::WindowLayout& layout);
    void ApplySavedPanelVisibility(const media::settings::WindowLayout& layout);
    void ResetLayout();
    void DebugDockState();
    /// Dock topology snapshot for session JSON (`snapshot.docks`); same fields as `debug.json` / `docks`.
    nlohmann::json SnapshotDockLayoutForSession();
    void TogglePanelView(CDocker* pDock, UINT dockStyle, CDocker* pParent,
                          int defaultSize, UINT32 cmdID);
    /// Idempotent "show this panel". If the panel is **docked and** already visible, no-op.
    /// A visible but **undocked** (floating) window is re-docked at @p dockStyle / @p defaultSize
    /// so restored layouts cannot strand panels outside the main frame. Used by Action
    /// handlers and chat file-tree normalization.
    void EnsurePanelVisible(CDocker* pDock, UINT dockStyle, CDocker* pParent,
                             int defaultSize, UINT32 cmdID);
    /// `CChatSimpleWorkbench` when window layout has no panel flags: Explorer visible, shell frames on, focus chat.
    void FinishChatWorkbenchDefaultDockVisibility();
    /// `CViewerSimpleWorkbench` when window layout has no panel flags: Explorer hidden, light shell defaults.
    void FinishViewerWorkbenchDefaultDockVisibility();
    /// `RecalcLayout` + `SyncShellHostLayout` on the Explorer; full re-init is separate.
    void NudgeLayoutAfterDockChange();
    bool IsPanelVisible(const CDocker* pDock) const;

    // ── Batch UWM handlers (Mainfrm.cpp) ─────────────────────────────────────
    LRESULT OnBatchPaused(WPARAM wparam, LPARAM lparam);
    LRESULT OnBatchResumed(WPARAM wparam, LPARAM lparam);
    LRESULT OnBatchCancelled(WPARAM wparam, LPARAM lparam);
    LRESULT OnBatchStateUpdate(WPARAM wparam, LPARAM lparam);

    // ── UWM handlers (Mainfrm.cpp) ────────────────────────────────────────────
    void    ToggleFullscreen();
    /// `RegisterHotKey` while the frame is active (WebView2 often eats keys before the queue).
    void    RegisterForegroundHotkeys();
    void    UnregisterForegroundHotkeys();
    LRESULT OnGetMinMaxInfo(UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT OnQueueProgress(WPARAM wparam, LPARAM lparam);
    LRESULT OnQueueOpStatus(WPARAM wparam, LPARAM lparam);
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    LRESULT OnPixlwizShareProgress(WPARAM wparam, LPARAM lparam);
    LRESULT OnPixlwizShareDone(WPARAM wparam, LPARAM lparam);
#endif
    LRESULT OnQueueDone(WPARAM wparam, LPARAM lparam);
    LRESULT OnTransformProgress(WPARAM wparam, LPARAM lparam);
    LRESULT OnTransformDone(WPARAM wparam, LPARAM lparam);
    LRESULT OnLogMessage(WPARAM wparam);
    LRESULT OnGeneratedFile(WPARAM wparam);
    /// wparam: heap `pmui::QueueToolCallRowW*` (always deleted)
    LRESULT OnQueueToolCall(WPARAM wparam);
    /// Reload the centre viewer when an agent tool writes the currently-previewed file.
    void    OnToolFileWritten(const std::wstring& path);
    void    UpdateFileInfoForSelection(int idx);
    LRESULT OnFindProgress(WPARAM wparam, LPARAM lparam);
    LRESULT OnFindDone(WPARAM wparam, LPARAM lparam);
    void    OnFindItemClicked(int item);
    void    OnFindRevealInExplorer(const std::wstring& path);
    LRESULT OnDuplicatesDone(WPARAM wparam, LPARAM lparam);
    void    OnDuplicateItemClicked(UINT_PTR itemData);
    void    OnDuplicatesSaveSession();
    void    OnDuplicatesOpenSession();

    // ── App commands (Mainfrm.cpp) ───────────────────────────────────────
    /// UWM_APP_COMMAND handler — wparam is a heap-allocated std::string*
    /// (UTF-8 command name) owned by us; we delete it after dispatch.
    /// Posted by the WM_COPYDATA bridge for `pm-image app <verb>` calls.
    LRESULT OnAppCommand(WPARAM wparam);
    /// Run a parsed app command on the UI thread. Shared between the
    /// bridge handler above and in-app keyboard shortcuts (ALT+P).
    void    RunAppCommand(const std::string& utf8_name);
#if defined(_WIN32)
    void    ApplySessionReplayFromPath();
#endif
    void    FlushPendingStartupIfAny();
    void    OnAppOpenChat(const std::vector<std::wstring>& paths);
    /// Semicolon paths from `browse|…` — internal File tree navigates; image paths become Explorer selection.
    void    OnAppBrowseToPaths(const std::vector<std::wstring>& paths);
    void    ApplyFileTreeBrowseForPaths(const std::vector<std::wstring>& paths);
    void    ScheduleFileTreeBrowse(const std::vector<std::wstring>& paths);
    /// Reveal the first regular file from startup-style path args (`--src`, semicolon joined).
    void    SelectFirstStartupFileInFileTree(const std::vector<std::wstring>& raw_paths);
    /// Drain @c m_startupFileTreePaths when Explorer is visible/sized (Phase 4 or View → Explorer).
    void    ApplyPendingStartupFileTreeIfAny();
    /// Parent folder (or directory-only `--src`) for Explorer @c SetInitialFolder while latch active.
    std::wstring StartupFileTreeSeedFolder() const;
    /// Navigate File Tree to folder and select a specific file.
    void    SelectPathInFileTree(const std::wstring& path);
    /// Called after File Tree navigation to select a specific item.
    void    SelectFileInExplorerView(const std::wstring& filename);
#if defined(_WIN32)
    void    OnDeferredPostLayoutInit();
    void    OnDeferredFileTreeNavTimer();
    void    ApplyScreenshotProbeWindowSizeIfSet();
#endif

    /// UWM_RELEASE_PREVIEW_FOR_PATHS handler. Synchronously called from
    /// the Explorer dock just before it asks the Shell to recycle/delete a
    /// set of files, or from batch workers before in-place I/O. If any path
    /// is held open by the preview, drop the preview so the op can succeed.
    /// wparam = pointer to a caller-owned std::vector<std::wstring> (stack
    /// lifetime; do NOT delete). lparam: 0 = release handles only (resize /
    /// compress in-place); non-zero = also remove matching paths from
    /// m_explorerSelectionPaths (delete / recycle).
    LRESULT OnReleasePreviewForPaths(WPARAM wparam, LPARAM lparam);
    // Posted by CExplorerBrowserView when the user clicks an image file.
    // Previews the file, updates FileInfo, and sets m_explorerSelectionPath so
    // Resize / Transform can operate on it directly without going through the queue.
    LRESULT OnExplorerSelection(WPARAM wparam, LPARAM lparam);

    // ── Presets (Mainfrm_presets.cpp) ────────────────────────────────────────
    void LoadPresets();
    void SavePresets();
    void ShowPresetsMenu();
    void AddPreset(const std::string& name, const std::string& prompt);
    void RemovePreset(int index);
    void OnPresets();

    // ── Member data ───────────────────────────────────────────────────────────
    CFileViewer    m_fileViewer;
    pmui::CViewerTabsHost m_viewerTabsHost;
    pmui::CViewerManager m_viewerManager;
    CChatWebView m_workbenchClientChatWeb;
#ifndef FEATURE_USE_OWN_RIBBON
    IUIRibbon*       m_pIUIRibbon  = nullptr;
#endif

#ifdef FEATURE_USE_OWN_RIBBON
    COwnRibbonTab    m_ownRibbon;
#endif

    // Core panels (always present)
    CDockQueue*      m_pDockQueue      = nullptr;
    CDockLog*        m_pDockLog        = nullptr;
    CDockSettings*   m_pDockSettings   = nullptr;
    CDockFindResults* m_pDockFindResults = nullptr;
    CDockDuplicateResults* m_pDockDuplicateResults = nullptr;
    /// Main workbench: m_pDockChatWeb in the right dock.
    /// (Chat workbench has no chat docker — the centre @c SetView is @c m_workbenchClientChatWeb .)
#ifdef FEATURE_CHAT_WEB
    CDockChatWeb*     m_pDockChatWeb     = nullptr;
#endif

    // Optional panels (feature-guarded)
    CDockFileTree*      m_pDockFileTree      = nullptr;
    CDockViewerPanel*   m_pDockViewerPanel   = nullptr;
    CMainViewerContainer m_centerViewerTabs;
    std::vector<CDockViewerPanel*> m_viewerDockTabs;
    CDockViewerPanel*   m_activeViewerDockTab = nullptr;
    int                 m_nextViewerDockTabId = DOCK_ID_VIEWER_TAB_FIRST;
#ifdef FEATURE_CONSOLE
    CDockWebConsole*    m_pDockConsole       = nullptr;
#endif
#ifdef FEATURE_BROWSER
    /// Central manager for all browser popups (multi-instance + routing).
    CWebViewManager     m_webViews;
#endif
#ifdef FEATURE_NODES
    CDockNodes*      m_pDockNodes      = nullptr;
    Workbench        m_activeWorkbench = Workbench::Standard;
#endif

    // Worker thread for resize / AI transform
    std::thread  m_worker;
    bool         m_processing = false;
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    /// Pixlwiz Share runs uploads on a worker thread; suppress concurrent Share.
    bool m_pixlwizShareInProgress = false;
    /// Queue row for `UWM_PIXLWIZ_SHARE_PROGRESS`, or -1 if no row / queue unavailable.
    int m_pixlwizShareQueueRow = -1;
#endif
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
    /// Menu Pixlwiz → Login: headless child in flight (`UWM_PIXLWIZ_LOGIN_DONE` clears).
    bool m_pixlwizLoginBusy = false;
#endif

    // Batch queue control — shared with the active worker thread.
    // Reset to a fresh BatchControl at the start of every Run.
    std::shared_ptr<media::BatchControl> m_batchCtrl;
    // In-memory session state for the current (or most recently completed) batch.
    // Updated by the worker thread via m_batchCtrl's mutex; read by OnSaveSession.
    media::BatchState m_currentSession;
    // Protects m_currentSession.items from concurrent access by UI and worker.
    std::mutex m_sessionMtx;

    // Remembers which action was last triggered from the Home tab so returning
    // to the Home tab keeps the relevant settings panel open (Resize or Compress).
    CSettingsView::Mode m_homeTabLastMode = CSettingsView::MODE_RESIZE;

    // Presets + paths
    std::vector<PromptPreset> m_presets;
    std::string               m_settingsPath;
    /// ReBar menu / status / own-ribbon strip; read in ctor from `workbench.<id>.chrome` (see `load_workbench_chrome`).
    media::settings::WorkbenchChromeSettings m_workbenchChrome{};
    /// Ensures @ref ApplyWorkbenchFrameChromeOnce runs a single time (per frame lifetime).
    bool m_workbenchFrameChromeApplied = false;
    /// Layout/save policy (`CDefaultMainWorkbench` in Phase 1).
    std::unique_ptr<pmui::IWorkbench> m_workbench;
    /// §8: Single owner for the centre file-viewer preview.
    pmui::CPreviewCoordinator m_previewCoord;

    // §13: Explorer selection state — owned by CExplorerSelectionRouter.
    // `m_explorerSelectionPaths` / `m_explorerStatusPaths` are aliases into the router
    // so existing code compiles unchanged; new code should prefer the router API.
    pmui::CExplorerSelectionRouter m_selRouter;
    std::vector<std::wstring>& m_explorerSelectionPaths = m_selRouter.MutableSelectionPaths();
    std::vector<std::wstring>& m_explorerStatusPaths    = m_selRouter.MutableStatusPaths();
    /// Consumed in `OnDeferredPostLayoutInit` — fixes CLI seeding before m_pDockQueue exists.
    std::vector<std::wstring> m_pendingStartupPaths;
    bool m_pendingOpenChat = false;
    /// Raw `--src` retained for file-tree navigation after chat/queue flush (cleared in @ref ApplyPendingStartupFileTreeIfAny).
    std::vector<std::wstring> m_startupFileTreePaths;
    /// §1 startup preview latch — when true, Explorer-driven centre preview updates
    /// that would drop `--src` are suppressed (empty ticks, folder-only picks from
    /// restored `filetree_folder`, etc.). Cleared in `CViewerSimpleWorkbench::OnPreviewChanged`
    /// after an Explorer-sourced preview completes with Ok or Failed.
    bool m_startupPreviewLatch = false;
    /// §12: Navigation generation at the time the startup latch was set.
    /// Empty-selection posts with generation <= this value are stale and discarded.
    uint32_t m_startupNavGeneration = 0;
    /// Viewer workbench: load the first previewable file (image, then markdown, then text) into the active viewer.
    void LoadFirstStartupPreviewFromPaths(const std::vector<std::wstring>& raw_paths);
    /// Enumerate @p folder for the first previewable file and load it via the preview coordinator
    /// using @c PreviewSource::Explorer.  No-op if folder is empty or has no previewable files.
    /// Called from @c CViewerSimpleWorkbench::OnExplorerFolderPath when the file tree navigates.
    void PreviewFirstFileInFolder(const std::wstring& folder);
    /// Centre active viewer from a flat path list (Explorer selection, viewer @c --src, etc.).
    void ApplyCentralFileViewerPreviewFromPaths(const std::vector<std::wstring>& paths);
    /// §11: Browse-request queue (replaces single-slot overwrite).
    /// `ScheduleFileTreeBrowse` pushes; `OnDeferredFileTreeNavTimer` drains.
    std::queue<std::vector<std::wstring>> m_deferredFileTreeNavQueue;
    uint32_t m_fileTreeNavSeq = 0;  ///< monotonic; logs correlate schedule ↔ apply
    /// Filename to select after next File Tree navigation completes (for shift+click reveal).
    std::wstring m_pendingFileToSelect;
    /// True if dock topology was restored from `win32_dock` / migration (not workbench default layout).
    /// `OnDeferredPostLayoutInit` may `RequestRebuildAfterReDock` so IExplorerBrowser matches a fresh default.
    bool m_dockLayoutFromSettingsJson = false;
    /// `layout.pv_filetree` from the last `ApplyWindowLayoutData` (chat file-tree nudge after `LoadLayout` needs this).
    bool m_wlWantsFileTreeVisible = true;

#if defined(_WIN32)
    std::wstring m_screenshotProbeOut;
    int          m_screenshotProbeWaitMs = 0;
    int          m_screenshotProbeWinW   = 0; ///< outer width; 0 = leave size from `LoadLayout`
    int          m_screenshotProbeWinH   = 0; ///< outer height; both must be > 0 to resize
    static constexpr UINT_PTR kScreenshotProbeTimerId = 31042;  ///< one-shot: capture + PostQuit
    /// One-shot: session replay + `FlushPendingStartup` after registry dock restore sizes.
    static constexpr UINT_PTR kPostLayoutInitTimerId = 31045;
    /// One-shot: navigate `IExplorerBrowser` after a short delay (also used by `browse|`).
    static constexpr UINT_PTR kFileTreeNavTimerId    = 31046;
    std::wstring m_sessionReplayPath;   ///< UTF-16 path; consumed in `OnDeferredPostLayoutInit`
    std::wstring m_layoutOverridePath;  ///< UTF-16 path; consumed in `OnInitialUpdate` before first show
    std::wstring m_cliCwdStartupFolder; ///< cwd at CLI launch; overrides saved filetree_folder when no --src
    bool         m_sessionRecording  = false;
    std::wstring m_sessionRecordOutPath; ///< destination for `StopSessionRecording`
#endif
    SIZE m_lastResponsiveClientSize{0, 0};
    bool m_lastResponsiveWasMaximized = false;
    bool m_inResponsiveFrameSize = false;
    // Fullscreen state
    bool             m_isFullscreen    = false;
    WINDOWPLACEMENT  m_savedWinPlacement{};
    LONG             m_savedWinStyle   = 0;
    LONG             m_savedWinExStyle = 0;

    // ── Status bar live stats ────────────────────────────────────────────────
    static constexpr UINT_PTR kStatsTimerId = 1001; ///< 5-second system-stats (CPU/RAM/fps) poll
    // Queue progress tracking (reset at each batch start).
    int    m_queueTotal     = 0;
    int    m_queueDone      = 0;
    /// Rows that receive mirrored status text during Find/Duplicates when inputs came from the queue.
    std::vector<int> m_queueOpRowIndices;
    DWORD  m_batchStartTick = 0;   // GetTickCount64() cast to DWORD
    // Previous CPU times for delta calculation.
    ULONGLONG m_cpuPrevKernel = 0;
    ULONGLONG m_cpuPrevUser   = 0;
    ULONGLONG m_cpuPrevIdle   = 0;
    // DWM timing: previous per-HWND frame counters for [`DwmGetCompositionTimingInfo`] + wall clock.
    ULONGLONG     m_dwmPrevFrameComplete   = 0;
    ULONGLONG     m_dwmPrevFramesDisplayed = 0;
    ULONGLONG     m_dwmPrevCFramesComplete = 0;
    ULONGLONG     m_statusPrevBarTickMs    = 0;
    int           m_statusWmPaintTally     = 0; ///< WM_PAINTs on the frame; fallback when DWM is flat

    void UpdateQueueStatusPart();
    void UpdateSystemStatusPart();
    void UpdateExplorerSelectionStatusPart();
    /// Pushes `text` into a cached `CString` and `SB_SETTEXT` with SBT_OWNERDRAW
    /// so Win32++'s [`DrawStatusBar`] applies [`StatusBarTheme::clrText`] in dark mode.
    void        SetStatusBarPartText(int part, const CString& text);
    void        SetStatusBarPartText(int part, LPCTSTR text) { SetStatusBarPartText(part, CString(text)); }
    void        ReapplyCachedStatusBarTexts();
    /// Recomputes SB_SETPARTS from the status bar client width. On WM_SIZE this runs
    /// after `WndProcDefault` so the bar has already been laid out (avoids squeezed parts).
    void RebuildStatusBarParts();
    /// Status bar parts: 0=hint, 1=Explorer path, 2=Explorer selection + size,
    /// 3=queue ETA, 4=CPU / system RAM / process working set / DWM frame FPS
    void ApplyExplorerFolderToStatusBar(const std::wstring& folder);

    std::wstring m_statusExplorerFolder;
    /// Owner-drawn status strings — must outlive the control (SBT_OWNERDRAW itemData = pointer).
    CString m_sbar0, m_sbar1, m_sbar2, m_sbar3, m_sbar4;

    /// Last successful duplicates run (or opened session) — for Save session as…
    nlohmann::json m_lastDupReport = nlohmann::json::object();

    // Source → generated file map (set by AI Transform's UWM_GENERATED_FILE
    // handler so we can resolve "what did the AI produce for source X".
    // Currently used by the queue-row click logic; the GenPreview dock that
    // used to mirror these is gone.
    std::map<std::wstring, std::wstring> m_generatedMap;

    /// Classic File → Recent files / folders (max 15 each; persisted in `window.recent_*`).
    std::vector<std::wstring> m_recentFiles;
    std::vector<std::wstring> m_recentFolders;
    /// Last Explorer folder (canonical) — used to skip MRU entries when browsing up/down the same tree.
    std::wstring m_explorerLastFolderPathW;
};

#endif // PM_UI_MAINFRM_H
