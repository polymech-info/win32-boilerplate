// Dock layout management for CMainFrame.
// Implements: dock topology (build / restore / reset / save / load / debug),
// panel visibility toggles, and per-session window placement.
#include "stdafx.h"
#include "constants.hpp"
#include "Mainfrm.h"
#include "helpers/splash_window.hpp"
#include "helpers/theme.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/win_ui_debug.hpp"
#include "helpers/win_initial_show.hpp"
#include "log_sink.h"
#include "ui_log_file.hpp"
#include "pm_win32_dock_json.hpp"
#include "win/ui_singleton.hpp"

#ifdef FEATURE_CHAT_WEB
#include "ChatWebResource.h"          // pmui::chat_web_available()
#include "ChatWebPanel.h"             // pmui::debug_snapshot_chat_web_ui
#endif

#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace fs = std::filesystem;
using json = nlohmann::json;

using pmui::utf8_to_wide;
using pmui::wide_to_utf8;

namespace {
int dock_id_from_view_command(UINT32 cmdID)
{
    switch (cmdID) {
#if FEATURE_COMMAND_QUEUE_VIEW
    case IDC_CMD_VIEW_QUEUE: return CMainFrame::DOCK_ID_QUEUE;
#endif
#if FEATURE_COMMAND_LOG_VIEW
    case IDC_CMD_VIEW_LOG: return CMainFrame::DOCK_ID_LOG;
#endif
    case IDC_CMD_VIEW_SETTINGS: return CMainFrame::DOCK_ID_SETTINGS;
#if FEATURE_COMMAND_FIND
    case IDC_CMD_VIEW_FINDRESULTS: return CMainFrame::DOCK_ID_FINDRESULTS;
#endif
    case IDC_CMD_VIEW_CHAT: return CMainFrame::DOCK_ID_CHAT;
#if FEATURE_COMMAND_DUPLICATES
    case IDC_CMD_VIEW_DUPLICATERESULTS: return CMainFrame::DOCK_ID_DUPLICATERESULTS;
#endif
    case IDC_CMD_VIEW_FILETREE: return CMainFrame::DOCK_ID_FILETREE;
    case IDC_CMD_VIEW_VIEWER_PANEL: return CMainFrame::DOCK_ID_VIEWER_PANEL;
#ifdef FEATURE_NODES
    case IDC_CMD_VIEW_NODES: return CMainFrame::DOCK_ID_NODES;
#endif
#ifdef FEATURE_CONSOLE
    case IDC_CMD_VIEW_CONSOLE: return CMainFrame::DOCK_ID_CONSOLE;
#endif
    default: return 0;
    }
}

// ── Column spec conversion: ExplorerColumnSpec ↔ FiletreeColumnSpec ──────────
// ExplorerColumnSpec (FileTreePanel) uses native GUIDs for direct IColumnManager use.
// FiletreeColumnSpec (LayoutStore) stores the GUID as a formatted string for JSON.

media::layout::FiletreeColumnSpec col_to_layout(const ExplorerColumnSpec& c)
{
    wchar_t buf[40]{};
    ::StringFromGUID2(c.fmtid, buf, 40);
    media::layout::FiletreeColumnSpec out;
    out.pid   = c.pid;
    out.width = c.width;
    // StringFromGUID2 produces "{XXXXXXXX-...}" in wide; narrow-encode as UTF-8
    out.fmtid_str = pmui::wide_to_utf8(buf);
    return out;
}

bool col_from_layout(const media::layout::FiletreeColumnSpec& c, ExplorerColumnSpec& out)
{
    if (c.fmtid_str.empty()) return false;
    std::wstring ws = pmui::utf8_to_wide(c.fmtid_str);
    GUID g{};
    if (FAILED(::CLSIDFromString(ws.c_str(), &g))) return false;
    out.fmtid = g;
    out.pid   = c.pid;
    out.width = c.width;
    return true;
}

} // namespace

// ── NewDockerFromID ───────────────────────────────────────────────────────────
// Called by Win32++ `CDocker::LoadDockLayout()` (frame override) to recreate each docker by ID.
// Do NOT store the raw pointer here — Win32++ may call CloseAllDockers() on
// failure, leaving any stored pointer dangling.  Member pointers are assigned
// safely via GetDockFromID() after a confirmed successful JSON restore.
DockPtr CMainFrame::NewDockerFromID(int id)
{
    switch (id) {
#if FEATURE_COMMAND_QUEUE_VIEW
    case DOCK_ID_QUEUE:       return std::make_unique<CDockQueue>();
#endif
#if FEATURE_COMMAND_LOG_VIEW
    case DOCK_ID_LOG:         return std::make_unique<CDockLog>();
#endif
    case DOCK_ID_SETTINGS:    return std::make_unique<CDockSettings>();
    // case 4 (DOCK_ID_GENPREVIEW) intentionally not handled — old registries
    // with a "Generated Preview" entry will fail restore and rebuild from
    // the default workbench layout which doesn't include it.
    // DOCK_ID_FILEINFO (5) — File Info dock removed; metadata is in the central preview.
    case DOCK_ID_FILEINFO:    return nullptr;
#if FEATURE_COMMAND_FIND
    case DOCK_ID_FINDRESULTS: return std::make_unique<CDockFindResults>();
#endif
#if FEATURE_COMMAND_DUPLICATES
    case DOCK_ID_DUPLICATERESULTS: return std::make_unique<CDockDuplicateResults>();
#endif
    case 11: // legacy second preview docker (removed)
        return nullptr;
    case DOCK_ID_CHAT: {
#ifdef FEATURE_CHAT_WEB
        if (pmui::chat_web_available()) return std::make_unique<CDockChatWeb>();
#endif
        return nullptr;
    }
    case DOCK_ID_FILETREE:    return std::make_unique<CDockFileTree>();
    case DOCK_ID_VIEWER_PANEL: return std::make_unique<CDockViewerPanel>();
    default:
        if (id >= DOCK_ID_VIEWER_TAB_FIRST && id <= DOCK_ID_VIEWER_TAB_LAST)
            return std::make_unique<CDockViewerPanel>();
        break;
#ifdef FEATURE_NODES
    case DOCK_ID_NODES:      return std::make_unique<CDockNodes>();
#endif
#ifdef FEATURE_CONSOLE
    case DOCK_ID_CONSOLE:    return std::make_unique<CDockWebConsole>();
#endif
    }
    return nullptr;
}

// ── OnInitialUpdate ───────────────────────────────────────────────────────────
void CMainFrame::OnInitialUpdate()
{
    pmui::ui_log_file_event("OnInitialUpdate enter (win32_dock in settings.json or default layout)");
    ApplyWorkbenchFrameChromeOnce();
    // Prime `defer_dock_container_load` (main frame + below) before `LoadDockLayout` — second
    // `load_window_layout` in `LoadLayout` hits settings cache.
    {
        std::string prime_err;
        media::settings::WindowLayout prime_wl;
        (void)media::settings::load_window_layout(prime_wl, prime_err, m_workbench->workbenchSettingsId());
    }
    pmui::ui_log_file_event("OnInitialUpdate: after load_window_layout prime (defer_dock flag; uses settings cache if warm)");
    bool dock_topology_loaded = false; // true if `LoadDockLayout` restored from `win32_dock`; drives deferred container load
    {
    // Suppress painting while dock restore + LoadLayout + ApplyAppearance run; the
    // main HWND can be visible for hundreds of ms before theme/font land — without this,
    // users see a light/default frame flash, then a second pass when ApplyAppearance
    // invalidates. Unlocks before first Show (see PreCreate: WS_VISIBLE cleared on Create).
    struct InitialUpdateDrawLock {
        explicit InitialUpdateDrawLock(HWND h)
        {
            if (h)
                (void)::LockWindowUpdate(h);
        }
        ~InitialUpdateDrawLock() { (void)::LockWindowUpdate(nullptr); }
    } const draw_lock{GetHwnd()};

    m_pDockQueue = nullptr; m_pDockLog  = nullptr; m_pDockSettings  = nullptr;
    m_pDockFindResults     = nullptr;
    m_pDockDuplicateResults = nullptr;
#ifdef FEATURE_CHAT_WEB
    m_pDockChatWeb = nullptr;
#endif
    m_pDockFileTree = nullptr;
#ifdef FEATURE_NODES
    m_pDockNodes = nullptr;
#endif
#ifdef FEATURE_CONSOLE
    m_pDockConsole = nullptr;
#endif

    try {
        dock_topology_loaded = LoadDockLayout();
        pmui::ui_log_file_event("OnInitialUpdate: after LoadDockLayout (win32_dock JSON, AddDockedChild/CreateWindow; tab order may defer)");

        if (dock_topology_loaded) {
            m_workbench->BindDockPointers(*this);
            std::string reject_reason;
            if (!m_workbench->AcceptRestoredDockLayout(*this, reject_reason)) {
                CString msg(L"[Layout] Saved dock layout rejected by workbench policy");
                if (!reject_reason.empty())
                    msg += CString(L": ") + pmui::utf8_to_wide(reject_reason).c_str();
                msg += L" — using default layout.";
                LogMessage(msg);
                dock_topology_loaded = false;
            }
        }
    }
    catch (const std::exception& ex) {
        LogMessage(CString(L"[Layout] LoadDockLayout threw: ") + pmui::utf8_to_wide(ex.what()).c_str());
        dock_topology_loaded = false;
    }
    catch (...) {
        LogMessage(L"[Layout] LoadDockLayout threw unknown exception.");
        dock_topology_loaded = false;
    }

    pmui::ui_log_file_event(dock_topology_loaded
        ? "OnInitialUpdate phase: LoadDock + dock pointers (layout source: settings win32_dock)"
        : "OnInitialUpdate phase: dock miss — will clear win32_dock and workbench BuildInitialDockLayout");
    if (!dock_topology_loaded) {
        pm::win32_dock::erase_from_settings(m_workbench->workbenchSettingsId());
        m_workbench->BuildInitialDockLayout(*this);
        pmui::ui_log_file_event("OnInitialUpdate phase: workbench BuildInitialDockLayout returned");
    }
    // Restored topology (e.g. after re-dock) can deserialize a valid `win32_dock` but leave
    // IExplorerBrowser in a bad state until full destroy/init — same as View → Explorer toggle.
    m_dockLayoutFromSettingsJson = dock_topology_loaded;

    // Spdlog UI sink: register the main frame as soon as the frame HWND exists.
    // The Log dock can be lazy-created/hidden/recreated; routing through the
    // frame keeps logger::* connected and LogMessage decides whether to append
    // to the live panel or the pm-image.log mirror.
    if (GetHwnd())
        pmui::set_ui_log_target(GetHwnd());

    const bool     chat_wb   = (std::strcmp(m_workbench->workbenchSettingsId(), "chat") == 0);
    const bool     viewer_wb = (std::strcmp(m_workbench->workbenchSettingsId(), "viewer") == 0);

    // Chat: file-tree normalization (float→dock, nudge) runs **after** `LoadLayout` so we
    // do not `EnsurePanelVisible` while Explorer is still meant to be hidden (saved `pv_filetree`),
    // which previously forced a show + Shell init before `LoadLayout` hid the dock again
    // and could crash IExplorerBrowser on the next run.

    pmui::ui_log_file_event("OnInitialUpdate phase: after visible dock graph creation (hidden optional panels are lazy)");
    m_workbench->FinishDockRestore(*this);
#if defined(FEATURE_BROWSER) && defined(FEATURE_CONSOLE)
    WireConsoleBus();
#endif
#if defined(FEATURE_BROWSER) && defined(FEATURE_CHAT_WEB)
    WireChatBus();
#endif
#if defined(FEATURE_BROWSER) && (defined(FEATURE_VIEWER_WEB) || defined(FEATURE_HOME_PAGE))
    WireFileViewerBus();
#endif

    pmui::ui_log_file_event("OnInitialUpdate phase: after SetupDockContainers + caption height");
    DragAcceptFiles(TRUE);
    SetWindowText(pm::brand::k_app_id_w);
    if (viewer_wb)
        SetStatusBarPartText(0, L"Drop an image or use File → Open.");
    else
        SetStatusBarPartText(0, L"Drop files or use Add Files to begin.");

    pmui::ui_log_file_event("OnInitialUpdate before LoadLayout (window placement + panel visibility JSON)");
    LoadLayout();   // window placement + panel visibility + Explorer folder
    pmui::ui_log_file_event("OnInitialUpdate after LoadLayout");
    ApplyLayoutOverrideFromPath();

    if (viewer_wb && !m_pDockFileTree && m_wlWantsFileTreeVisible) {
        EnsurePanelVisible(nullptr, DS_DOCKED_LEFT, GetDockAncestor(), DpiScaleInt(280), IDC_CMD_VIEW_FILETREE);
        if (m_pDockFileTree)
            m_pDockFileTree->GetFileTreeContainer().GetBrowserView().SetShowShellFrames(false);
    }
    if ((chat_wb || viewer_wb) && m_pDockFileTree) {
        // Stale `win32_dock` can float the Explorer. Re-dock *after* saved `pv_filetree` / shell chrome
        // and folder are applied in `LoadLayout` so a hidden strip stays hidden and we never
        // init Shell twice (show→hide) in one `OnInitialUpdate` pass.
        bool need_filetree_rebuild = false;
        if (m_pDockFileTree->IsUndocked() && GetDockAncestor()) {
            GetDockAncestor()->Dock(m_pDockFileTree, DS_DOCKED_LEFT);
            m_pDockFileTree->SetDockSize(DpiScaleInt(300));
            need_filetree_rebuild = true;
        }
        if (!m_wlWantsFileTreeVisible) {
            if (m_pDockFileTree->IsWindow() && IsPanelVisible(m_pDockFileTree))
                m_pDockFileTree->Hide();
        } else if (IsPanelVisible(m_pDockFileTree)) {
            NudgeLayoutAfterDockChange();
            if (need_filetree_rebuild)
                m_pDockFileTree->GetFileTreeContainer().GetBrowserView().RequestRebuildAfterReDock();
        }
        InvalidateToggle(IDC_CMD_VIEW_FILETREE);
    }

    // Apply theme + font preferences NOW (after every panel exists). Replaces
    // the old hard-coded caption colours that lived in this function.
    // launch_ui_next + ctor already read appearance — no second disk read.
    ApplyAppearance(false);
    pmui::ui_log_file_event("OnInitialUpdate after ApplyAppearance");

    // spdlog UI target is set earlier (right after the Log dock exists); see set_ui_log_target above.
    // Hook the WM_COPYDATA bridge (created in launch_ui_next when this is the
    // primary instance) so external `<app> app <verb>` calls can reach us
    // via UWM_APP_COMMAND. No-op when no bridge was created (secondary
    // instance launched after the primary mutex was already taken).
    media::win::set_ui_command_target(GetHwnd(), UWM_APP_COMMAND);

    // Set up the 3-part status bar (messages | queue stats | system stats)
    // and start the 5-second system-stats (CPU, RAM, fps) polling timer.
    RebuildStatusBarParts();
    ::SetTimer(GetHwnd(), kStatsTimerId, pm::ui::k_stats_poll_ms, nullptr);

#if defined(_WIN32)
    if (!m_screenshotProbeOut.empty()) {
        // `<app> test screenshot` should capture the settings strip (dividers, combos), not a blank workbench.
        SwitchSettingsMode(CSettingsView::MODE_RESIZE);
        // PNG capture timer is set in `OnDeferredPostLayoutInit` after layout + `LockWindowUpdate`
        // so IExplorerBrowser gets a real `SetRect` (otherwise the left pane stays white in the PNG).
    }
#endif

    // Defer session replay, queue seeding, and `IExplorerBrowser` folder sync
    // until the dock layout from settings has a chance to finish sizing.
#if defined(_WIN32)
    ::SetTimer(GetHwnd(), kPostLayoutInitTimerId, pm::ui::k_post_layout_init_delay_ms, nullptr);
    pmui::ui_log_file_event("OnInitialUpdate leave; posted 150ms kPostLayoutInitTimerId");
#else
    FlushPendingStartupIfAny();
#endif
    } // draw_lock: unlock before first show

    // TRIAL: CMainFrame::PreCreate cleared WS_VISIBLE so CWnd::Create did not ShowWindow; show
    // after theme + layout (pair with PreCreate if this causes focus/taskbar issues).
    if (GetHwnd()) {
        int showCmd = pmui::win32_effective_main_frame_show_cmd();
        WINDOWPLACEMENT wp{sizeof(wp)};
        if (GetWindowPlacement(wp) && wp.showCmd == SW_SHOWMAXIMIZED)
            showCmd = SW_SHOWMAXIMIZED;
        (void)ShowWindow(showCmd);
    }
    // RegisterHotKey paths (WM_HOTKEY) — WebView2 often never posts WM_KEY* to our pump.
    RegisterForegroundHotkeys();
    if (dock_topology_loaded && GetHwnd() && media::settings::defer_dock_container_load()) {
        if (!::PostMessage(GetHwnd(), UWM_PM_LOAD_DOCK_CONTAINERS, 0, 0)) {
            pmui::ui_log_file_event(
                "OnInitialUpdate: PostMessage UWM_PM_LOAD_DOCK_CONTAINERS failed; LoadDockContainers sync");
            (void)LoadDockContainers();
        } else
            pmui::ui_log_file_event("OnInitialUpdate: posted UWM_PM_LOAD_DOCK_CONTAINERS (tab order after first show)");
    }
}

void CMainFrame::SetupToolBar()
{
    // Ribbon provides all commands; toolbar is intentionally empty.
}

// ── IsPanelVisible ────────────────────────────────────────────────────────────
bool CMainFrame::IsPanelVisible(const CDocker* pDock) const
{
    if (!pDock || !pDock->IsWindow()) return false;
    // Active tabs and standalone dockers: window is visible.
    if (::IsWindowVisible(pDock->GetHwnd())) return true;
    // Inactive tabs: window is hidden but they're part of a visible container.
    // Win32++ may hide the inactive tab container/page; the parent tab group is
    // the visible layout member that keeps the tab alive.
    if (CDockContainer* pContainer = pDock->GetContainer()) {
        if (::IsWindowVisible(pContainer->GetHwnd())) return true;
        if (CDockContainer* pParent = pContainer->GetContainerParent()) {
            if (::IsWindowVisible(pParent->GetHwnd())) return true;
        }
    }
    return false;
}

// ── TogglePanelView ───────────────────────────────────────────────────────────
void CMainFrame::TogglePanelView(CDocker* pDock, UINT dockStyle,
                                  CDocker* pParent, int defaultSize, UINT32 cmdID)
{
    if (!pParent)
        pParent = GetDockAncestor();
    if (!pParent) return;
    if (cmdID == IDC_CMD_VIEW_LOG && !IsPanelVisible(pDock)) {
        if (m_pDockQueue && IsPanelVisible(m_pDockQueue)) {
            pParent = m_pDockQueue;
            dockStyle = DS_DOCKED_RIGHT;
            defaultSize = DpiScaleInt(360);
        } else {
            pParent = GetDockAncestor();
            dockStyle = DS_DOCKED_BOTTOM;
            defaultSize = DpiScaleInt(220);
        }
        if (!pParent)
            return;
    }
    if (!pDock) {
        EnsurePanelVisible(nullptr, dockStyle, pParent, defaultSize, cmdID);
        return;
    }

    if (IsPanelVisible(pDock)) {
        pDock->Hide();
        InvalidateToggle(cmdID);
        if (HWND h = GetHwnd(); h && ::IsWindow(h))
            (void)::PostMessageW(h, UWM_PM_SAVE_WORKBENCH_LAYOUT, 0, 0);
    } else {
        EnsurePanelVisible(pDock, dockStyle, pParent, defaultSize, cmdID);
    }
}

// ── EnsurePanelVisible ────────────────────────────────────────────────────────
// Idempotent "make sure this panel is visible right now". Safe to call from
// Action handlers (Resize/Compress/Meta/Transform/Find/Chat) so the settings /
// chat dock always pops into view when the user invokes the feature.
//
// Important guard: if the docker's HWND has been destroyed for any reason
// (legacy Win32++ X-button behaviour, layout restore failure, …), bail out
// safely — Win32++'s `Dock()` would call `SetStyle()` and assert.  In normal
// operation this guard is a no-op because `CDockPanelBase::OnClose` keeps the
// HWND alive.
void CMainFrame::FinishChatWorkbenchDefaultDockVisibility()
{
    if (m_pDockFileTree) {
        bool need_filetree_rebuild = false;
        if (m_pDockFileTree->IsUndocked()) {
            if (GetDockAncestor()) {
                GetDockAncestor()->Dock(m_pDockFileTree, DS_DOCKED_LEFT);
                m_pDockFileTree->SetDockSize(DpiScaleInt(300));
                need_filetree_rebuild = true;
            }
        } else if (!IsPanelVisible(m_pDockFileTree)) {
            EnsurePanelVisible(m_pDockFileTree, DS_DOCKED_LEFT, GetDockAncestor(), DpiScaleInt(300),
                IDC_CMD_VIEW_FILETREE);
        }
        m_pDockFileTree->GetFileTreeContainer().GetBrowserView().SetShowShellFrames(true);
        NudgeLayoutAfterDockChange();
        if (need_filetree_rebuild)
            m_pDockFileTree->GetFileTreeContainer().GetBrowserView().RequestRebuildAfterReDock();
        InvalidateToggle(IDC_CMD_VIEW_FILETREE);
        ApplyPendingStartupFileTreeIfAny();
    }
    RefreshChatContext();
#ifdef FEATURE_CHAT_WEB
    if (pmui::chat_web_available() && m_workbenchClientChatWeb.IsWindow())
        m_workbenchClientChatWeb.FocusInput();
#endif
    RecalcLayout();
}

void CMainFrame::FinishViewerWorkbenchDefaultDockVisibility()
{
    m_wlWantsFileTreeVisible = false;
    if (m_pDockFileTree) {
        if (m_pDockFileTree->IsWindow() && IsPanelVisible(m_pDockFileTree))
            m_pDockFileTree->Hide();
        m_pDockFileTree->GetFileTreeContainer().GetBrowserView().SetShowShellFrames(false);
        InvalidateToggle(IDC_CMD_VIEW_FILETREE);
    }
    RecalcLayout();
}

void CMainFrame::EnsurePanelVisible(CDocker* pDock, UINT dockStyle,
                                     CDocker* pParent, int defaultSize, UINT32 cmdID)
{
    
    bool createdNow = (pDock == nullptr);
    if (!pParent)
        pParent = GetDockAncestor();
    if (!pParent) return;
    if (!pDock) {
        const int dockID = dock_id_from_view_command(cmdID);
        if (dockID != 0) {
            // Robust against stale member pointers: if a dock with this id already
            // exists in the live tree, reuse it instead of creating a duplicate.
            if (CDocker* existing = GetDockFromID(dockID)) {
                pDock = existing;
                createdNow = false;
            }
        }
    }
    if (!pDock) {
        const int dockID = dock_id_from_view_command(cmdID);
        DockPtr docker = NewDockerFromID(dockID);
        if (!docker)
            return;

        pDock = pParent->AddDockedChild(std::move(docker), dockStyle, defaultSize, dockID);
        if (!pDock)
            return;

        switch (dockID) {
        case DOCK_ID_QUEUE: m_pDockQueue = static_cast<CDockQueue*>(pDock); break;
        case DOCK_ID_LOG: m_pDockLog = static_cast<CDockLog*>(pDock); break;
        case DOCK_ID_SETTINGS: m_pDockSettings = static_cast<CDockSettings*>(pDock); break;
        case DOCK_ID_FINDRESULTS: m_pDockFindResults = static_cast<CDockFindResults*>(pDock); break;
        case DOCK_ID_DUPLICATERESULTS:
            m_pDockDuplicateResults = static_cast<CDockDuplicateResults*>(pDock);
            break;
        case DOCK_ID_CHAT:
#ifdef FEATURE_CHAT_WEB
            m_pDockChatWeb = dynamic_cast<CDockChatWeb*>(pDock);
            if (m_pDockChatWeb)
                m_pDockChatWeb->GetChatWebContainer().SetHideSingleTab(TRUE);
#ifdef FEATURE_BROWSER
            WireChatBus();
#endif
#endif
            break;
        case DOCK_ID_FILETREE: m_pDockFileTree = static_cast<CDockFileTree*>(pDock); break;
#ifdef FEATURE_NODES
        case DOCK_ID_NODES:
            m_pDockNodes = static_cast<CDockNodes*>(pDock);
            m_activeWorkbench = Workbench::Standard;
            break;
#endif
#ifdef FEATURE_CONSOLE
        case DOCK_ID_CONSOLE:
            m_pDockConsole = static_cast<CDockWebConsole*>(pDock);
#ifdef FEATURE_BROWSER
            WireConsoleBus();
#endif
            break;
#endif
        default: break;
        }
        pDock->SetCaptionHeight(DpiScaleInt(26));
    }
    if (!pDock->IsWindow()) return;          // dead HWND — refuse to re-dock
    if (cmdID == IDC_CMD_VIEW_LOG && GetHwnd())
        pmui::set_ui_log_target(GetHwnd());

    if (!createdNow && IsPanelVisible(pDock) && pDock->IsDocked()) {
        InvalidateToggle(cmdID);
        return;
    }
    if (!createdNow) {
        if (pDock->IsDocked())
            pDock->Hide();                   // detach before re-docking
        pParent->Dock(pDock, dockStyle);
    }
    if (defaultSize > 0)
        pDock->SetDockSize(defaultSize);
    if (pDock == m_pDockFileTree) {
        NudgeLayoutAfterDockChange();
        auto& eb = m_pDockFileTree->GetFileTreeContainer().GetBrowserView();
        if (createdNow)
            eb.SyncShellHostLayout();
        else
            eb.RequestRebuildAfterReDock();
        ApplyPendingStartupFileTreeIfAny();
    }
    InvalidateToggle(cmdID);
    if (createdNow)
        ApplyAppearance(false);
}

// ── ResetLayout ───────────────────────────────────────────────────────────────
void CMainFrame::ResetLayout()
{
    m_workbench->ResetLayout(*this);
}

// ── DebugDockState ────────────────────────────────────────────────────────────
void CMainFrame::DebugDockState()
{
    json j;
    j["kind"] = pm::brand::k_debug_snapshot_json_kind_u8;
    {
        std::time_t t = std::time(nullptr);
        std::tm     utc{};
#ifdef _WIN32
        gmtime_s(&utc, &t);
#else
        gmtime_r(&t, &utc);
#endif
        std::ostringstream ts;
        ts << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        j["timestamp_utc"] = ts.str();
    }
    try {
        j["cwd"] = fs::absolute(fs::current_path()).string();
    } catch (...) {
        j["cwd"] = "(unavailable)";
    }

    {
        const auto& pal = pmui::theme_palette();
        j["theme"] = {
            {"dark",            pal.dark},
            {"window_bg",       pal.window_bg},
            {"web_surface_bg",  pal.web_surface_bg},
            {"windows_dark_os", pmui::windows_is_dark_mode()},
        };
    }
    j["docks"] = SnapshotDockLayoutForSession();
    {
        auto ptr_hex = [](const void* p) {
            std::ostringstream os;
            os << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(p);
            return os.str();
        };
        auto rect_json = [](const CRect& r) {
            return json::array({ r.left, r.top, r.right, r.bottom });
        };
        auto docker_json = [&](CDocker* d) {
            json o;
            if (!d) {
                o["ptr"] = nullptr;
                return o;
            }
            o["ptr"] = ptr_hex(d);
            o["dock_id"] = d->GetDockID();
            o["dock_style"] = d->GetDockStyle();
            o["dock_parent"] = ptr_hex(d->GetDockParent());
            o["is_window"] = d->IsWindow() ? true : false;
            o["is_visible"] = d->IsWindow() && d->IsWindowVisible();
            o["is_docked"] = d->IsDocked() ? true : false;
            o["is_undocked"] = d->IsUndocked() ? true : false;
            o["is_panel_visible"] = IsPanelVisible(d);
            o["is_viewer_tab_docker"] = IsViewerTabDocker(d);
            o["is_viewer_tab_chrome"] = IsViewerTabChromeDocker(d);
            if (d->IsWindow())
                o["window_rect"] = rect_json(d->GetWindowRect());
            if (CDockContainer* c = d->GetContainer()) {
                o["container_ptr"] = ptr_hex(c);
                o["container_item_count"] = c->GetItemCount();
                CDockContainer* active = c->GetActiveContainer();
                o["active_container_ptr"] = ptr_hex(active);
                if (CDocker* active_docker = active ? GetDockFromView(active) : nullptr)
                    o["active_dock_id"] = active_docker->GetDockID();
                json tabs = json::array();
                const auto& all = c->GetAllContainers();
                for (size_t i = 0; i < all.size(); ++i) {
                    CDockContainer* tab = c->GetContainerFromIndex(static_cast<int>(i));
                    json t;
                    t["index"] = i;
                    t["container_ptr"] = ptr_hex(tab);
                    t["is_viewer_container"] = dynamic_cast<CViewerPanelContainer*>(tab) != nullptr;
                    if (CDocker* tab_docker = tab ? GetDockFromView(tab) : nullptr) {
                        t["dock_ptr"] = ptr_hex(tab_docker);
                        t["dock_id"] = tab_docker->GetDockID();
                        t["dock_style"] = tab_docker->GetDockStyle();
                        t["is_viewer_tab_docker"] = IsViewerTabDocker(tab_docker);
                    }
                    tabs.push_back(std::move(t));
                }
                o["tabs"] = std::move(tabs);
            }
            return o;
        };

        json live;
        live["active_docker"] = ptr_hex(GetActiveDocker());
        live["active_viewer_tab"] = ptr_hex(m_activeViewerDockTab);
        live["tracked_viewer_tabs"] = json::array();
        for (CDockViewerPanel* panel : m_viewerDockTabs)
            live["tracked_viewer_tabs"].push_back(ptr_hex(panel));
        live["all_dockers"] = json::array();
        for (CDocker* d : GetAllDockers())
            live["all_dockers"].push_back(docker_json(d));
        live["all_dock_children"] = json::array();
        for (const Win32xx::DockPtr& ptr : GetAllDockChildren())
            live["all_dock_children"].push_back(docker_json(ptr.get()));
        j["live_dock_tree"] = std::move(live);
    }

#ifdef FEATURE_CHAT_WEB
    if (m_workbenchClientChatWeb.IsWindow())
        j["chat_web"] = pmui::debug_snapshot_chat_web_ui(m_workbenchClientChatWeb);
    else if (m_pDockChatWeb && m_pDockChatWeb->IsWindow())
        j["chat_web"] = pmui::debug_snapshot_chat_web_ui(
            m_pDockChatWeb->GetChatWebContainer().GetChatWebView());
    else
        j["chat_web"] = nullptr;
#endif

    const fs::path out_path = media::settings::get_config_dir() / "debug.json";
    {
        std::error_code mk_ec;
        fs::create_directories(out_path.parent_path(), mk_ec);
        std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
        if (f) {
            f << j.dump(2);
            f.close();
        }
    }

    LogMessage(CString(L"── Debug snapshot → debug.json ──────────────────────────────────"));
    LogMessage(CString(utf8_to_wide(out_path.string()).c_str()));

    // ── Selection / reveal state ──────────────────────────────────────────────
    LogMessage(CString(L"── Selection ────────────────────────────────────────────────────"));

    // Explorer selection (frame-level)
    if (m_explorerSelectionPaths.empty()) {
        LogMessage(CString(L"  selection : (none)"));
    } else {
        for (const auto& p : m_explorerSelectionPaths)
            LogMessage(CString((L"  selection : " + p).c_str()));
    }

    // Initial folder + file seed (set from layout / startup, tracking only)
    {
        const std::wstring init = m_pDockFileTree
            ? m_pDockFileTree->GetFileTreeContainer().GetBrowserView().GetInitialFolder()
            : std::wstring{};
        LogMessage(CString((L"  initial   : " + (init.empty() ? L"(none)" : init)).c_str()));
        const std::vector<std::wstring> initFiles = m_pDockFileTree
            ? m_pDockFileTree->GetFileTreeContainer().GetBrowserView().GetInitialFiles()
            : std::vector<std::wstring>{};
        if (initFiles.empty()) {
            LogMessage(CString(L"  initfiles : (none)"));
        } else {
            for (size_t i = 0; i < initFiles.size(); ++i)
                LogMessage(CString((L"  initfiles[" + std::to_wstring(i) + L"] : " + initFiles[i]).c_str()));
        }
    }

    // Current folder + pending reveal (frame-level)
    {
        const std::wstring folder = m_pDockFileTree
            ? m_pDockFileTree->GetFileTreeContainer().GetBrowserView().GetCurrentFolder()
            : std::wstring{};
        LogMessage(CString((L"  folder    : " + (folder.empty() ? L"(none)" : folder)).c_str()));
    }
    {
        const std::wstring& pend = m_pendingFileToSelect;
        LogMessage(CString((L"  pending   : " + (pend.empty() ? L"(none)" : pend)).c_str()));
    }

    // Reveal retry state (view-level)
    if (m_pDockFileTree) {
        const auto& bv = m_pDockFileTree->GetFileTreeContainer().GetBrowserView();
        const std::wstring& rf = bv.GetSelectRetryFile();
        const int           rc = bv.GetSelectRetryCount();
        if (rf.empty()) {
            LogMessage(CString(L"  retry     : (none)"));
        } else {
            LogMessage(CString((L"  retry     : " + rf
                + L"  (" + std::to_wstring(rc) + L" ticks left)").c_str()));
        }
    }

    // Preview coordinator state
    {
        const auto& ps = m_previewCoord.State();
        const wchar_t* src = L"None";
        switch (ps.source) {
            case pmui::PreviewSource::None:          src = L"None";          break;
            case pmui::PreviewSource::Explorer:      src = L"Explorer";      break;
            case pmui::PreviewSource::CliStartup:    src = L"CliStartup";    break;
            case pmui::PreviewSource::QueueRow:      src = L"QueueRow";      break;
            case pmui::PreviewSource::ChatGenerated: src = L"ChatGenerated"; break;
            case pmui::PreviewSource::RecentFile:    src = L"RecentFile";    break;
            case pmui::PreviewSource::AppBrowse:     src = L"AppBrowse";     break;
            case pmui::PreviewSource::SessionReplay: src = L"SessionReplay"; break;
            case pmui::PreviewSource::Extension:     src = L"Extension";     break;
        }
        LogMessage(CString((std::wstring(L"  preview   : src=") + src
            + L"  active=" + (ps.active.empty() ? L"(none)" : ps.active)).c_str()));
    }
}

nlohmann::json CMainFrame::SnapshotDockLayoutForSession()
{
    auto dock_json = [](const CDocker* p) -> json {
        if (!p || !p->IsWindow())
            return json(nullptr);
        return json{
            { "docked", p->IsDocked() ? true : false },
            { "undocked", p->IsUndocked() ? true : false },
            { "visible", ::IsWindowVisible(p->GetHwnd()) ? true : false },
            { "dock_size", p->GetDockSize() },
            { "hwnd", reinterpret_cast<std::uint64_t>(p->GetHwnd()) },
        };
    };
    return json{ { "queue", dock_json(m_pDockQueue) },
                 { "log", dock_json(m_pDockLog) },
                 { "settings", dock_json(m_pDockSettings) },
                 { "file_info", json(nullptr) },
                 { "find_results", dock_json(m_pDockFindResults) },
                 { "dup_results", dock_json(m_pDockDuplicateResults) },
                 { "file_tree", dock_json(m_pDockFileTree) },
#ifdef FEATURE_NODES
                 { "nodes", dock_json(m_pDockNodes) },
#endif
#ifdef FEATURE_CONSOLE
                 { "console", dock_json(m_pDockConsole) },
#endif
    };
}

// ── SaveLayout ────────────────────────────────────────────────────────────────
bool CMainFrame::FillWindowLayoutFromState(media::settings::WindowLayout& layout)
{
    // Dock topology (sizes, parent/child order, floating rects) → settings.json `win32_dock`.
    // Panel open/closed state + window placement → settings.json `window`.
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (!GetWindowPlacement(wp))
        return false;

    layout = {};
    layout.has_placement = true;
    layout.show_cmd      = (wp.showCmd == SW_SHOWMINIMIZED) ? SW_SHOWNORMAL : (int)wp.showCmd;
    layout.min_pos       = wp.ptMinPosition;
    layout.max_pos       = wp.ptMaxPosition;
    layout.normal_rect   = wp.rcNormalPosition;

    layout.has_panel_visibility = true;
    layout.pv_log        = IsPanelVisible(m_pDockLog);
    layout.pv_queue      = IsPanelVisible(m_pDockQueue);
    layout.pv_settings   = IsPanelVisible(m_pDockSettings);
    layout.pv_fileinfo   = false;   // File Info dock removed — field kept for JSON back-compat
    layout.pv_findresults= IsPanelVisible(m_pDockFindResults);
    layout.pv_dupresults  = IsPanelVisible(m_pDockDuplicateResults);
#ifdef FEATURE_CHAT_WEB
    layout.pv_chat       = IsPanelVisible(m_pDockChatWeb);
#else
    layout.pv_chat       = false;
#endif
    layout.pv_filetree   = IsPanelVisible(m_pDockFileTree);
    if (m_pDockFileTree) {
        auto& bv = m_pDockFileTree->GetFileTreeContainer().GetBrowserView();
        layout.filetree_show_shell_frames = bv.GetShowShellFrames();
        layout.filetree_filter_mask       = wide_to_utf8(bv.GetFilterMask());
        const auto& folder = bv.GetCurrentFolder();
        if (!folder.empty())
            layout.filetree_folder = wide_to_utf8(folder);

        // Capture the live view mode and column state before serialising.
        bv.SnapshotViewMode();
        layout.filetree_view_mode = bv.GetViewMode();
        layout.filetree_icon_size = bv.GetIconSize();

        bv.SnapshotColumnSettings();
        layout.filetree_columns.clear();
        for (const auto& es : bv.GetColumnSpecs())
            layout.filetree_columns.push_back(col_to_layout(es));
    }
#ifdef FEATURE_NODES
    layout.pv_nodes = IsPanelVisible(m_pDockNodes);
#else
    layout.pv_nodes = false;
#endif
#ifdef FEATURE_CONSOLE
    layout.pv_console = IsPanelVisible(m_pDockConsole);
#else
    layout.pv_console = false;
#endif
    layout.defer_dock_containers = media::settings::defer_dock_container_load();
    layout.center_view_tabbed = m_viewerManager.IsTabbed();

    layout.recent_files.clear();
    for (const auto& w : m_recentFiles) {
        if (layout.recent_files.size() >= 15)
            break;
        std::string u8 = wide_to_utf8(w);
        if (!u8.empty())
            layout.recent_files.push_back(std::move(u8));
    }
    layout.recent_folders.clear();
    for (const auto& w : m_recentFolders) {
        if (layout.recent_folders.size() >= 15)
            break;
        std::string u8 = wide_to_utf8(w);
        if (!u8.empty())
            layout.recent_folders.push_back(std::move(u8));
    }
    return true;
}

void CMainFrame::SaveLayout()
{
    m_workbench->SaveLayout(*this);
}

// ── ApplySavedPanelVisibility ─────────────────────────────────────────────────
void CMainFrame::ApplySavedPanelVisibility(const media::settings::WindowLayout& pv)
{
    const bool wantQueue = pv.pv_queue;
    auto restoredLayoutMember = [this](CDocker* d) -> bool {
        if (!m_dockLayoutFromSettingsJson || !d || !d->IsWindow())
            return false;
        // During OnInitialUpdate the frame/floating container can still be hidden even though
        // win32_dock already recreated this docker in its saved position. Do not re-anchor or hide it
        // based on potentially stale window.panels flags.
        return d->GetDockParent() || d->GetContainer() || d->IsUndocked();
    };

    // Inactive tabs have hidden HWNDs, but their visible container means they are
    // part of the restored layout and must stay attached.
    auto isInVisibleContainer = [](CDocker* d) -> bool {
        if (!d) return false;
        if (CDockContainer* pContainer = d->GetContainer()) {
            if (::IsWindowVisible(pContainer->GetHwnd())) return true;
        }
        return false;
    };

    auto hideIf = [this, &isInVisibleContainer, &restoredLayoutMember](CDocker* d, bool want_visible) {
        // Don't hide panels that are in a visible container - they're tabbed with a visible panel
        // and should remain as inactive tabs, not be hidden (which would break the container).
        // Also keep dock members restored from win32_dock even when window.panels booleans are stale.
        if (d && !want_visible && IsPanelVisible(d) && !isInVisibleContainer(d) && !restoredLayoutMember(d))
            d->Hide();
    };
    auto showIf = [this, &restoredLayoutMember](
                      CDocker* d, bool want_visible, UINT dockStyle, CDocker* parent, int size, UINT32 cmdID) {
        if (want_visible && !IsPanelVisible(d) && !restoredLayoutMember(d))
            EnsurePanelVisible(d, dockStyle, parent ? parent : GetDockAncestor(), size, cmdID);
    };

    // Child-before-parent order avoids docking surprises.
    hideIf(m_pDockFileTree, pv.pv_filetree);
    hideIf(m_pDockFindResults, pv.pv_findresults);
    hideIf(m_pDockDuplicateResults, pv.pv_dupresults);
    hideIf(m_pDockLog,         pv.pv_log);
    hideIf(m_pDockQueue,       wantQueue);
    hideIf(m_pDockSettings,    pv.pv_settings);
#ifdef FEATURE_NODES
    hideIf(m_pDockNodes, pv.pv_nodes);
#endif
#ifdef FEATURE_CONSOLE
    hideIf(m_pDockConsole, pv.pv_console);
#endif

    showIf(m_pDockQueue, wantQueue, DS_DOCKED_BOTTOM, GetDockAncestor(), DpiScaleInt(220), IDC_CMD_VIEW_QUEUE);
    const bool logBesideQueue = m_pDockQueue && IsPanelVisible(m_pDockQueue);
    showIf(m_pDockLog, pv.pv_log,
        logBesideQueue ? DS_DOCKED_RIGHT : DS_DOCKED_BOTTOM,
        logBesideQueue ? static_cast<CDocker*>(m_pDockQueue) : GetDockAncestor(),
        logBesideQueue ? DpiScaleInt(360) : DpiScaleInt(220),
        IDC_CMD_VIEW_LOG);
    showIf(m_pDockSettings, pv.pv_settings, DS_DOCKED_RIGHT, GetDockAncestor(), DpiScaleInt(280), IDC_CMD_VIEW_SETTINGS);
    showIf(m_pDockFindResults, pv.pv_findresults, DS_DOCKED_RIGHT, GetDockAncestor(), DpiScaleInt(360),
        IDC_CMD_VIEW_FINDRESULTS);
    showIf(m_pDockDuplicateResults, pv.pv_dupresults, DS_DOCKED_RIGHT, GetDockAncestor(), DpiScaleInt(360),
        IDC_CMD_VIEW_DUPLICATERESULTS);
    showIf(
#ifdef FEATURE_CHAT_WEB
        static_cast<CDocker*>(m_pDockChatWeb),
#else
        nullptr,
#endif
        pv.pv_chat, DS_DOCKED_RIGHT, GetDockAncestor(), DpiScaleInt(420), IDC_CMD_VIEW_CHAT);
#ifdef FEATURE_NODES
    showIf(m_pDockNodes, pv.pv_nodes, DS_DOCKED_BOTTOM, GetDockAncestor(), DpiScaleInt(300), IDC_CMD_VIEW_NODES);
#endif
#ifdef FEATURE_CONSOLE
    showIf(m_pDockConsole, pv.pv_console, DS_DOCKED_BOTTOM, GetDockAncestor(), DpiScaleInt(260), IDC_CMD_VIEW_CONSOLE);
#endif

    InvalidateToggle(IDC_CMD_VIEW_FILETREE);
    InvalidateToggle(IDC_CMD_VIEW_LOG);
    InvalidateToggle(IDC_CMD_VIEW_QUEUE);
    InvalidateToggle(IDC_CMD_VIEW_SETTINGS);
    InvalidateToggle(IDC_CMD_VIEW_FINDRESULTS);
    InvalidateToggle(IDC_CMD_VIEW_DUPLICATERESULTS);
    InvalidateToggle(IDC_CMD_VIEW_CHAT);
#ifdef FEATURE_NODES
    InvalidateToggle(IDC_CMD_VIEW_NODES);
#endif
#ifdef FEATURE_CONSOLE
    InvalidateToggle(IDC_CMD_VIEW_CONSOLE);
#endif
}

// ── LoadLayout ────────────────────────────────────────────────────────────────
void CMainFrame::ApplyWindowLayoutData(const media::settings::WindowLayout& layout)
{
    pmui::ui_log_file_eventf(
        "ApplyWindowLayoutData: enter filetree_ptr=%p cli_cwd=\"%s\" pending_paths=%zu pending_open_chat=%d has_visibility=%d",
        static_cast<void*>(m_pDockFileTree),
        wide_to_utf8(m_cliCwdStartupFolder).c_str(),
        m_pendingStartupPaths.size(),
        m_pendingOpenChat ? 1 : 0,
        layout.has_panel_visibility ? 1 : 0);
    m_wlWantsFileTreeVisible = layout.has_panel_visibility ? layout.pv_filetree : true;
    if (layout.has_placement) {
        media::settings::WindowLayout adj = layout;
        POINT centre = {
            (adj.normal_rect.left + adj.normal_rect.right)  / 2,
            (adj.normal_rect.top  + adj.normal_rect.bottom) / 2,
        };
        HMONITOR hMon = ::MonitorFromPoint(centre, MONITOR_DEFAULTTONULL);
        if (!hMon) {
            HMONITOR hPrimary = ::MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{sizeof(mi)};
            ::GetMonitorInfoW(hPrimary, &mi);
            const RECT& wa = mi.rcWork;
            int w = std::min(1200L, wa.right - wa.left);
            int h = std::min(800L, wa.bottom - wa.top);
            adj.normal_rect = {wa.left + 40, wa.top + 40,
                               wa.left + 40 + w, wa.top + 40 + h};
            adj.show_cmd = SW_SHOWNORMAL;
        }
        WINDOWPLACEMENT wp{sizeof(wp)};
        wp.showCmd          = (UINT)adj.show_cmd;
        wp.ptMinPosition    = adj.min_pos;
        wp.ptMaxPosition    = adj.max_pos;
        wp.rcNormalPosition = adj.normal_rect;
        if (adj.show_cmd != SW_SHOWMAXIMIZED && adj.show_cmd != SW_SHOWMINIMIZED) {
            const RECT& r = adj.normal_rect;
            SetWindowPos(nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        SetWindowPlacement(wp);
    }

    if (layout.has_panel_visibility)
        ApplySavedPanelVisibility(layout);

    ApplyCentreViewerTabbed(layout.center_view_tabbed, false);

    // Restore last Explorer folder.  Must happen before WM_EB_INIT is dispatched
    // (PostMessage guarantees it is processed after the current call stack).
    if (m_pDockFileTree) {
        auto& bv = m_pDockFileTree->GetFileTreeContainer().GetBrowserView();
        bv.SetShowShellFrames(layout.filetree_show_shell_frames);
        bv.SetFilterMask(utf8_to_wide(layout.filetree_filter_mask));
        bv.SetViewMode(layout.filetree_view_mode, layout.filetree_icon_size);

        if (!layout.filetree_columns.empty()) {
            std::vector<ExplorerColumnSpec> cols;
            cols.reserve(layout.filetree_columns.size());
            for (const auto& c : layout.filetree_columns) {
                ExplorerColumnSpec es;
                if (col_from_layout(c, es))
                    cols.push_back(es);
            }
            bv.SetColumnSpecs(std::move(cols));
        }
        pmui::ui_log_file_eventf(
            "ApplyWindowLayoutData: folder-branch state cli_cwd=\"%s\" pending_paths=%zu pending_open_chat=%d saved_filetree_folder=\"%s\"",
            wide_to_utf8(m_cliCwdStartupFolder).c_str(),
            m_pendingStartupPaths.size(),
            m_pendingOpenChat ? 1 : 0,
            layout.filetree_folder.c_str());

        // `--src` parent/dir wins over cwd and saved filetree_folder so a later show/rebuild
        // does not fall back to Desktop when Explorer was hidden at startup.
        if (!m_startupFileTreePaths.empty()) {
            const std::wstring seed = StartupFileTreeSeedFolder();
            if (!seed.empty()) {
                bv.SetInitialFolder(seed);
                pmui::ui_log_file_eventf(
                    "ApplyWindowLayoutData: Explorer SetInitialFolder from --src seed=\"%s\"",
                    wide_to_utf8(seed).c_str());
            }
        } else if (!m_cliCwdStartupFolder.empty()) {
            bv.SetInitialFolder(m_cliCwdStartupFolder);
            pmui::ui_log_file_eventf(
                "ApplyWindowLayoutData: Explorer SetInitialFolder from CLI cwd=\"%s\"",
                wide_to_utf8(m_cliCwdStartupFolder).c_str());
            m_cliCwdStartupFolder.clear();
        } else if (m_pendingStartupPaths.empty() && !m_pendingOpenChat) {
            std::error_code ec_cwd;
            const auto cwd = fs::current_path(ec_cwd);
            if (!ec_cwd) {
                const std::wstring cwd_w = cwd.wstring();
                if (!cwd_w.empty()) {
                    bv.SetInitialFolder(cwd_w);
                    pmui::ui_log_file_eventf(
                        "ApplyWindowLayoutData: Explorer SetInitialFolder from process cwd=\"%s\"",
                        wide_to_utf8(cwd_w).c_str());
                }
            } else
                pmui::ui_log_file_eventf(
                    "ApplyWindowLayoutData: process cwd lookup failed ec=%d",
                    static_cast<int>(ec_cwd.value()));
        } else if (!layout.filetree_folder.empty()) {
            bv.SetInitialFolder(utf8_to_wide(layout.filetree_folder));
            pmui::ui_log_file_eventf(
                "ApplyWindowLayoutData: Explorer SetInitialFolder from window layout filetree_folder=\"%s\"",
                layout.filetree_folder.c_str());
        } else
            pmui::ui_log_file_event("ApplyWindowLayoutData: no initial folder branch matched");
    } else
        pmui::ui_log_file_event("ApplyWindowLayoutData: skip folder branch because m_pDockFileTree is null");

    // Session replay / partial snapshots may omit `recent_*`; do not wipe in-memory MRU.
    if (!layout.recent_files.empty()) {
        m_recentFiles.clear();
        for (const auto& u8 : layout.recent_files) {
            if (m_recentFiles.size() >= 15)
                break;
            std::wstring w = utf8_to_wide(u8);
            if (!w.empty())
                m_recentFiles.push_back(std::move(w));
        }
    }
    if (!layout.recent_folders.empty()) {
        m_recentFolders.clear();
        for (const auto& u8 : layout.recent_folders) {
            if (m_recentFolders.size() >= 15)
                break;
            std::wstring w = utf8_to_wide(u8);
            if (!w.empty())
                m_recentFolders.push_back(std::move(w));
        }
    }
    RestoreExplorerRecentMruBaselineFromLayout(layout.filetree_folder);
    pmui::ui_log_file_event("ApplyWindowLayoutData: leave");
}

void CMainFrame::LoadLayout()
{
    m_workbench->LoadLayout(*this);
}

void CMainFrame::ApplyLayoutOverrideFromPath()
{
    if (m_layoutOverridePath.empty())
        return;

    const std::wstring path = m_layoutOverridePath;
    m_layoutOverridePath.clear();
    pmui::ui_log_file_eventf("LayoutOverride: begin \"%s\"", wide_to_utf8(path).c_str());

    std::ifstream inFile(path, std::ios::binary);
    if (!inFile) {
        LogMessage(CString(L"[layout] override: failed to open ") + path.c_str());
        return;
    }

    std::string jsonStr((std::istreambuf_iterator<char>(inFile)),
                         std::istreambuf_iterator<char>());
    std::string err;
    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        if (!m_workbench->ImportLayoutDocument(*this, j, err, false)) {
            LogMessage(CString(L"[layout] override apply failed: ") + utf8_to_wide(err).c_str());
            return;
        }
        m_dockLayoutFromSettingsJson = true;
        CString ok(L"[layout] override: applied without persisting ");
        ok += path.c_str();
        LogMessage(ok);
    } catch (const std::exception& e) {
        LogMessage(CString(L"[layout] override parse failed: ") + utf8_to_wide(e.what()).c_str());
    }
}

#if defined(_WIN32)
void CMainFrame::ApplyScreenshotProbeWindowSizeIfSet()
{
    if (m_screenshotProbeWinW <= 0 || m_screenshotProbeWinH <= 0)
        return;
    HWND hwnd = GetHwnd();
    if (!hwnd)
        return;
    const CRect r = GetWindowRect();
    if (!::SetWindowPos(
            hwnd, nullptr, r.left, r.top, m_screenshotProbeWinW, m_screenshotProbeWinH, SWP_NOZORDER | SWP_NOACTIVATE)) {
        pmui::ui_log_file_event("ApplyScreenshotProbeWindowSizeIfSet: SetWindowPos failed");
        return;
    }
    pmui::ui_log_file_eventf("ApplyScreenshotProbeWindowSizeIfSet: outer %dx%d", m_screenshotProbeWinW, m_screenshotProbeWinH);
}

// ── §9: Deferred post-layout phase list ─────────────────────────────────────
//
// `OnDeferredPostLayoutInit` orchestrates six conceptually independent
// phases that must run in a specific order after the 150 ms
// `kPostLayoutInitTimerId`. The phases are synchronous (no async
// callbacks between them); the explicit enum + per-phase log lines
// make the **intended order** self-documenting and the "who ran before
// whom?" question answerable from a single log grep.
//
//   Phase 0  SplashDismiss   — fade out the startup splash (unless chat/web defers)
//   Phase 1  SessionReplay   — apply saved window layout from --replay JSON
//   Phase 2  StartupFlush    — seed queue / centre preview from --src paths
//   Phase 3  ScreenshotSize  — resize window for test screenshot probe
//   Phase 4  ShellAndBrowse  — §7b workbench: Shell rebuild + file-tree navigate
//   Phase 5  ScreenshotProbe — arm the one-shot capture timer (if --test screenshot)
//
// Invariant: after Phase 2, `m_pendingStartupPaths` is empty.
// Invariant: after Phase 4, `m_dockLayoutFromSettingsJson` is false.
// Invariant: Phase 5 only arms the timer — actual capture happens in WM_TIMER.

namespace {
enum class DeferredPhase : int {
    SplashDismiss  = 0,
    SessionReplay  = 1,
    StartupFlush   = 2,
    ScreenshotSize = 3,
    ShellAndBrowse = 4,
    ScreenshotProbe = 5,
};
} // namespace

void CMainFrame::OnDeferredPostLayoutInit()
{
    pmui::ui_log_file_event("OnDeferredPostLayoutInit enter (150ms timer)");
    if (GetHwnd()) ::KillTimer(GetHwnd(), kPostLayoutInitTimerId);
    bool appliedSessionReplay = false;

    // ── Phase 0: SplashDismiss ────────────────────────────────────────────────
    {
        pmui::ui_log_file_event("  phase 0: SplashDismiss");
        if (pmui::splash_main_frame_defers_splash_dismissal()) {
            // still waiting for JS `kind:ready` or 10s timer (chat workbench + web)
        } else if (pmui::splash_consume_deferred_splash_dismissal_by_chat_ready()) {
            // `on_ready` already called `splash_fade_out` — skip second fade at 150ms
        } else
            pmui::splash_fade_out_and_hide(240);
    }

    // One paint after replay + file-tree navigation instead of a visible stutter
    // as the Explorer and queue update separately.
    struct DeferInitDrawLock {
        explicit DeferInitDrawLock(HWND h)
        {
            if (h)
                (void)::LockWindowUpdate(h);
        }
        ~DeferInitDrawLock() { (void)::LockWindowUpdate(nullptr); }
    } const draw_lock{GetHwnd()};

    // ── Phase 1: SessionReplay ────────────────────────────────────────────────
    if (!m_sessionReplayPath.empty()) {
        pmui::ui_log_file_event("  phase 1: SessionReplay (applying)");
        ApplySessionReplayFromPath();
        m_sessionReplayPath.clear();
        appliedSessionReplay = true;
    }

    // ── Phase 2: StartupFlush ─────────────────────────────────────────────────
    // Invariant: after this phase, m_pendingStartupPaths is empty.
    {
        pmui::ui_log_file_eventf("  phase 2: StartupFlush (%zu pending paths)",
                                  m_pendingStartupPaths.size());
        const std::vector<std::wstring> pendingCopy = m_pendingStartupPaths;
        const bool pendingOpenChat = m_pendingOpenChat;
        FlushPendingStartupIfAny();

        // ── Phase 3: ScreenshotSize ───────────────────────────────────────────
        {
            pmui::ui_log_file_event("  phase 3: ScreenshotSize");
            ApplyScreenshotProbeWindowSizeIfSet();
        }

        // ── Phase 4: ShellAndBrowse (§7b workbench) ──────────────────────────
        // Default: rebuild IExplorerBrowser if dock layout came from settings
        //          JSON + schedule file-tree browse for --src paths.
        // Viewer:  skips rebuild when startup paths are present (§3 tactical).
        {
            pmui::ui_log_file_event("  phase 4: ShellAndBrowse (workbench deferred init)");
            m_workbench->deferredInit().DeferredPostLayoutInit(*this, pendingCopy);
        }

#if defined(FEATURE_HOME_PAGE) && defined(FEATURE_BROWSER)
        if (!IsChatWorkbench()
            && !IsViewerWorkbench()
            && pendingCopy.empty()
            && !pendingOpenChat
            && !appliedSessionReplay
            && m_screenshotProbeOut.empty()) {
            pmui::ui_log_file_event("  phase 4b: auto-load Home");
            (void)m_viewerManager.ActiveView().LoadHome();
        } else
            pmui::ui_log_file_event("  phase 4b: auto-load Home skipped");
#endif
    }

    // ── Phase 5: ScreenshotProbe ──────────────────────────────────────────────
    if (!m_screenshotProbeOut.empty() && GetHwnd()) {
        pmui::ui_log_file_event("  phase 5: ScreenshotProbe (arming timer)");
        const UINT d = m_screenshotProbeWaitMs <= 0 ? 1u : static_cast<UINT>(m_screenshotProbeWaitMs);
        ::SetTimer(GetHwnd(), kScreenshotProbeTimerId, d, nullptr);
    }

    pmui::ui_log_file_event("OnDeferredPostLayoutInit leave");
}
#endif

// ── File menu layout export/import ────────────────────────────────────────────

void CMainFrame::OnFileSaveLayout()
{
    // Export current workbench layout (dock topology + window placement) to a standalone JSON file.
    // Uses the same serialization as settings.json but exports to a user-chosen file.
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetHwnd();
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    static const wchar_t kFilter[] =
        L"Layout files (*.json)\0*.json\0"
        L"All files\0*.*\0\0";
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt  = L"json";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&ofn))
        return;

    // Build layout document using existing workbench serialization.
    nlohmann::json j;
    j["workbench"] = m_workbench->workbenchSettingsId();
    j["version"] = 1;

    // Serialize dock topology (same as SaveDockToSettings uses).
    j["win32_dock"] = pm::win32_dock::serialize_dock_to_json(*this);

    // Serialize window layout (same as SaveLayout uses).
    media::settings::WindowLayout wl;
    if (FillWindowLayoutFromState(wl)) {
        j["window"] = media::layout::LayoutStore::WindowLayoutToExportJson(wl);
    }

    std::string jsonStr = j.dump(2);
    std::ofstream outFile(buf, std::ios::binary);
    if (outFile) {
        outFile.write(jsonStr.data(), jsonStr.size());
        outFile.close();
        CString msg;
        msg.Format(L"Layout saved for workbench '%S' to %s", m_workbench->workbenchSettingsId(), buf);
        LogMessage(msg);
    } else {
        ::MessageBoxW(GetHwnd(), L"Failed to save layout file.", L"Save Layout", MB_OK | MB_ICONERROR);
    }
}

void CMainFrame::OnFileLoadLayout()
{
    // Import workbench layout from a standalone JSON file.
    // The loaded layout overrides the current workbench's persisted settings in settings.json.
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetHwnd();
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    static const wchar_t kFilter[] =
        L"Layout files (*.json)\0*.json\0"
        L"All files\0*.*\0\0";
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&ofn))
        return;

    std::ifstream inFile(buf, std::ios::binary);
    if (!inFile) {
        ::MessageBoxW(GetHwnd(), L"Failed to open layout file.", L"Load Layout", MB_OK | MB_ICONERROR);
        return;
    }
    std::string jsonStr((std::istreambuf_iterator<char>(inFile)),
                         std::istreambuf_iterator<char>());
    inFile.close();

    std::string err;
    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);

        if (!m_workbench->ImportLayoutDocument(*this, j, err)) {
            std::wstring werr = pmui::utf8_to_wide(err);
            ::MessageBoxW(GetHwnd(), werr.c_str(), L"Load Layout", MB_OK | MB_ICONERROR);
            return;
        }

        CString msg;
        msg.Format(L"Layout loaded and saved to settings.json for workbench '%S' from %s",
                   m_workbench->workbenchSettingsId(), buf);
        LogMessage(msg);

    } catch (const std::exception& e) {
        std::wstring werr = pmui::utf8_to_wide(std::string("Parse error: ") + e.what());
        ::MessageBoxW(GetHwnd(), werr.c_str(), L"Load Layout", MB_OK | MB_ICONERROR);
    }
}
