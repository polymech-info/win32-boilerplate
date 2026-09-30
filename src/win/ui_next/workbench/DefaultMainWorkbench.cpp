#include "stdafx.h"
#include "workbench/MainWorkbench.h"
#include "constants.hpp"
#include "file_extensions.hpp"
#include "Mainfrm.h"
#include "pm_win32_dock_json.hpp"
#include "ui_log_file.hpp"
#include "win/LayoutStore.hpp"
#include "win/settings_store.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>

using nlohmann::json;
using Win32xx::CDocker;

namespace pmui {

namespace {

struct DefaultPanelSpec {
    int preferred_width  = 0;
    int preferred_height = 0;
    int side_min         = 0;
    int side_max         = 0;
    int bottom_min       = 0;
    int bottom_max       = 0;
};

struct MainDefaultLayoutSpec {
    DefaultPanelSpec explorer{240, 220, 200, 420, 140, 320};
    DefaultPanelSpec queue{360, 220, 280, 620, 160, 380};
    DefaultPanelSpec log{360, 220, 280, 640, 160, 380};
    DefaultPanelSpec settings{340, 260, 300, 640, 180, 440};
    DefaultPanelSpec results{360, 240, 300, 680, 180, 440};
    DefaultPanelSpec chat{420, 300, 340, 760, 220, 520};
    DefaultPanelSpec nodes{380, 300, 320, 760, 220, 520};
};

constexpr MainDefaultLayoutSpec kMainDefaultLayout{};

void apply_main_default_panel_visibility(media::settings::WindowLayout& layout)
{
    layout.has_panel_visibility = true;
#if FEATURE_COMMAND_QUEUE_VIEW
    layout.pv_queue             = true;
#else
    layout.pv_queue             = false;
#endif
#if FEATURE_COMMAND_LOG_VIEW
    layout.pv_log               = true;
#else
    layout.pv_log               = false;
#endif
    layout.pv_settings          = false;
    layout.pv_fileinfo          = false;
    layout.pv_findresults       = false;
    layout.pv_dupresults        = false;
    layout.pv_chat              = false;
    layout.pv_filetree          = true;
    layout.pv_nodes             = false;
    layout.defer_dock_containers = true;
}

int scaled_clamped_size(CMainFrame& frame, int current, double ratio, int minPx96, int maxPx96)
{
    if (current <= 0 || ratio <= 0.0 || minPx96 <= 0 || maxPx96 <= 0)
        return current;
    const int minPx = frame.DpiScaleInt(minPx96);
    const int maxPx = frame.DpiScaleInt(maxPx96);
    const int scaled = static_cast<int>(std::lround(static_cast<double>(current) * ratio));
    return std::clamp(scaled, minPx, maxPx);
}

} // namespace

const char* CDefaultMainWorkbench::workbenchSettingsId() const noexcept
{
    return "main";
}

void CDefaultMainWorkbench::LoadLayout(CMainFrame& frame)
{
    media::settings::WindowLayout layout;
    std::string                err;
    if (!media::settings::load_window_layout(layout, err, workbenchSettingsId())) {
        pmui::ui_log_file_eventf(
            "CDefaultMainWorkbench::LoadLayout: load_window_layout failed slot='%s' err='%s'",
            workbenchSettingsId(), err.c_str());
        return;
    }
    pmui::ui_log_file_eventf(
        "CDefaultMainWorkbench::LoadLayout: applying layout slot='%s' has_visibility=%d filetree=%d chat=%d",
        workbenchSettingsId(), layout.has_panel_visibility ? 1 : 0,
        layout.pv_filetree ? 1 : 0, layout.pv_chat ? 1 : 0);
    frame.ApplyWindowLayoutData(layout);
    pmui::ui_log_file_event("CDefaultMainWorkbench::LoadLayout: ApplyWindowLayoutData returned");
}

void CDefaultMainWorkbench::SaveLayout(CMainFrame& frame)
{
    if (frame.m_pDockSettings) {
        frame.m_pDockSettings->GetSettingsContainer().GetSettingsView().SaveCommandProviderOverrides();
    }
    media::settings::WindowLayout layout;
    if (!frame.FillWindowLayoutFromState(layout))
        return;
    std::string err;
    media::settings::save_window_layout(layout, err, workbenchSettingsId());
}

bool CDefaultMainWorkbench::ImportLayoutDocument(CMainFrame& frame, const json& j, std::string& err, bool persist)
{
    err.clear();

    if (j.contains("workbench") && j["workbench"].is_string()) {
        std::string savedWorkbench = j["workbench"];
        const char* currentWorkbench = workbenchSettingsId();
        if (savedWorkbench != currentWorkbench) {
            CString warn;
            warn.Format(L"Layout was saved for workbench '%S' but current is '%S'. "
                        L"Layout will be imported into current workbench.",
                        savedWorkbench.c_str(), currentWorkbench);
            if (persist)
                ::MessageBoxW(frame.GetHwnd(), warn, L"Load Layout", MB_OK | MB_ICONWARNING);
            else
                frame.LogMessage(warn);
        }
    }

    if (j.contains("win32_dock") && j["win32_dock"].is_object()) {
        const auto& dockDoc = j["win32_dock"];
        if (dockDoc.value("v", 0) == pm::win32_dock::kJsonVersion && dockDoc.contains("children")) {
            HWND h = frame.GetHwnd();
            if (h)
                (void)::SendMessage(h, WM_SETREDRAW, FALSE, 0);

            frame.CloseAllDockers();
            ClearDockPointers(frame);

            const bool dockTreeApplied = pm::win32_dock::apply_dock_tree_from_json(frame, dockDoc, err);
            if (!dockTreeApplied) {
                ClearDockPointers(frame);
                BuildInitialDockLayout(frame);
                FinishDockRestore(frame);
                if (h) {
                    (void)::SendMessage(h, WM_SETREDRAW, TRUE, 0);
                    ::RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
                }
                err = std::string("Dock layout restore failed: ") + err;
                return false;
            }

            BindDockPointers(frame);
            FinishDockRestore(frame);

            if (dockDoc.contains("containers") && dockDoc["containers"].is_array()) {
                if (!pm::win32_dock::apply_containers_from_json(frame, dockDoc["containers"], err)) {
                    if (h) {
                        (void)::SendMessage(h, WM_SETREDRAW, TRUE, 0);
                        ::RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
                    }
                    err = std::string("Dock container restore failed: ") + err;
                    return false;
                }
            }
            if (persist)
                pm::win32_dock::save_win32_dock_doc(dockDoc, err, workbenchSettingsId());

            if (h)
                (void)::SendMessage(h, WM_SETREDRAW, TRUE, 0);
        }
    }

    media::settings::WindowLayout wl;
    if (j.contains("window") && j["window"].is_object()) {
        if (!media::layout::LayoutStore::ParseWindowLayout(j["window"], wl, err))
            return false;

        frame.ApplyWindowLayoutData(wl);

        if (persist) {
            std::string saveErr;
            media::settings::save_window_layout(wl, saveErr, workbenchSettingsId());
        }
    }

    frame.RecalcLayout();
    return true;
}

BOOL CDefaultMainWorkbench::LoadDockFromSettings(CMainFrame& frame)
{
    using clock = std::chrono::steady_clock;
    using ms    = std::chrono::milliseconds;
    const auto t_all = clock::now();

    HWND h = frame.GetHwnd();
    if (h)
        (void)::SendMessage(h, WM_SETREDRAW, FALSE, 0);
    std::string err;
    json        doc;
    BOOL        ok = FALSE;
    {
        const auto   t_read = clock::now();
        const bool   have_doc
            = pm::win32_dock::load_win32_dock_doc(doc, err, workbenchSettingsId()) && doc.is_object()
            && doc.value("v", 0) == pm::win32_dock::kJsonVersion && doc.contains("children")
            && doc["children"].is_array();
        const long long read_ms = std::chrono::duration_cast<ms>(clock::now() - t_read).count();
        if (have_doc) {
            pmui::ui_log_file_eventf("LoadDockFromSettings: read win32_dock JSON from settings %lld ms",
                read_ms);
            if (pm::win32_dock::apply_dock_tree_from_json(frame, doc, err)) {
                ok = TRUE;
                if (doc.contains("containers") && doc["containers"].is_array() && !doc["containers"].empty()
                    && !media::settings::defer_dock_container_load()) {
                    const auto t_cont = clock::now();
                    if (!pm::win32_dock::apply_containers_from_json(frame, doc["containers"], err)) {
                        pmui::ui_log_file_eventf(
                            "LoadDockFromSettings: apply_containers failed: %s", err.c_str());
                    }
                    const long long cont_ms = std::chrono::duration_cast<ms>(clock::now() - t_cont).count();
                    pmui::ui_log_file_eventf(
                        "LoadDockFromSettings: apply_containers (tab order, not deferred) %lld ms", cont_ms);
                }
            } else
                (void)frame.CloseAllDockers();
        } else {
            if (read_ms > 0)
                pmui::ui_log_file_eventf("LoadDockFromSettings: read settings (no usable win32_dock) %lld ms",
                    read_ms);
            pmui::ui_log_file_event(
                "LoadDockFromSettings: no usable win32_dock — use workbench.*.win32_dock in settings.json (or default layout)");
        }
    }
    if (h)
        (void)::SendMessage(h, WM_SETREDRAW, TRUE, 0);
    const long long all_ms = std::chrono::duration_cast<ms>(clock::now() - t_all).count();
    pmui::ui_log_file_eventf("LoadDockFromSettings: total wall time %lld ms (settings.json win32_dock only)",
        all_ms);
    return ok;
}

BOOL CDefaultMainWorkbench::SaveDockToSettings(CMainFrame& frame)
{
    std::string err;
    json        j = pm::win32_dock::serialize_dock_to_json(frame);
    if (j.is_object() && j.value("v", 0) == pm::win32_dock::kJsonVersion) {
        if (pm::win32_dock::save_win32_dock_doc(j, err, workbenchSettingsId())) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CDefaultMainWorkbench::LoadDockContainersFromSettings(CMainFrame& frame)
{
    std::string err, err2;
    json        doc;
    if (pm::win32_dock::load_win32_dock_doc(doc, err, workbenchSettingsId()) && doc.is_object() && doc.contains("containers")
        && doc["containers"].is_array() && !doc["containers"].empty()) {
        const BOOL ok = pm::win32_dock::apply_containers_from_json(frame, doc["containers"], err2) ? TRUE : FALSE;
        if (!ok && !err2.empty())
            pmui::ui_log_file_eventf("LoadDockContainersFromSettings: apply_containers failed: %s", err2.c_str());
        return ok;
    }
    return FALSE; // missing or empty `containers` in win32_dock (or deferred UWM re-read)
}

bool CDefaultMainWorkbench::AcceptRestoredDockLayout(CMainFrame& /*frame*/, std::string& reason) const
{
    reason.clear();
    // Main workbench: any subset of panels is a valid user preference.
    // Dock-tree corruption is handled by apply_dock_tree_from_json().
    return true;
}

void CDefaultMainWorkbench::ResetLayout(CMainFrame& frame)
{
    BuildInitialDockLayout(frame);
#ifdef FEATURE_NODES
    frame.m_activeWorkbench = CMainFrame::Workbench::Standard;
#endif

    FinishDockRestore(frame);
    frame.RecalcLayout();

    (void)SaveDockToSettings(frame);
    std::string err;
    media::settings::WindowLayout def;
    if (!frame.FillWindowLayoutFromState(def))
        return;
    apply_main_default_panel_visibility(def);
    media::settings::save_window_layout(def, err, workbenchSettingsId());
}

void CDefaultMainWorkbench::BuildInitialDockLayout(CMainFrame& frame)
{
    frame.CloseAllDockers();
    ClearDockPointers(frame);

    CDocker* ancestor = frame.GetDockAncestor();
    if (!ancestor)
        return;

    frame.m_pDockFileTree = static_cast<CDockFileTree*>(
        ancestor->AddDockedChild(std::make_unique<CDockFileTree>(),
                                 Win32xx::DS_DOCKED_LEFT, frame.DpiScaleInt(kMainDefaultLayout.explorer.preferred_width),
                                 CMainFrame::DOCK_ID_FILETREE));

#if FEATURE_COMMAND_QUEUE_VIEW
    frame.m_pDockQueue = static_cast<CDockQueue*>(
        ancestor->AddDockedChild(std::make_unique<CDockQueue>(),
                                 Win32xx::DS_DOCKED_BOTTOM, frame.DpiScaleInt(kMainDefaultLayout.queue.preferred_height),
                                 CMainFrame::DOCK_ID_QUEUE));
#endif

#if FEATURE_COMMAND_LOG_VIEW
    if (frame.m_pDockQueue) {
        frame.m_pDockLog = static_cast<CDockLog*>(
            frame.m_pDockQueue->AddDockedChild(std::make_unique<CDockLog>(),
                                               Win32xx::DS_DOCKED_RIGHT, frame.DpiScaleInt(kMainDefaultLayout.log.preferred_width),
                                               CMainFrame::DOCK_ID_LOG));
    } else {
        frame.m_pDockLog = static_cast<CDockLog*>(
            ancestor->AddDockedChild(std::make_unique<CDockLog>(),
                                     Win32xx::DS_DOCKED_BOTTOM, frame.DpiScaleInt(kMainDefaultLayout.log.preferred_height),
                                     CMainFrame::DOCK_ID_LOG));
    }
#endif

    // Default visible-on-launch set:
    // Explorer plus enabled Queue/Log panels are visible. Optional/action panels
    // stay lazy and are opened by View toggles or action handlers.
}

void CDefaultMainWorkbench::OnFrameMaximizeTransition(CMainFrame& frame, SIZE previousClient, SIZE currentClient)
{
    if (previousClient.cx <= 0 || previousClient.cy <= 0 || currentClient.cx <= 0 || currentClient.cy <= 0)
        return;

    const double xRatio = static_cast<double>(currentClient.cx) / static_cast<double>(previousClient.cx);
    const double yRatio = static_cast<double>(currentClient.cy) / static_cast<double>(previousClient.cy);
    bool changed = false;

    auto scaleDock = [&](CDocker* dock, const DefaultPanelSpec& spec) {
        if (!dock || !dock->IsDocked() || !frame.IsPanelVisible(dock))
            return;
        if (dock->GetDockStyle() & Win32xx::DS_DOCKED_CONTAINER)
            return;

        const DWORD side = dock->GetDockStyle() & 0xF;
        const bool horizontalSize = (side == Win32xx::DS_DOCKED_LEFT || side == Win32xx::DS_DOCKED_RIGHT);
        const bool verticalSize = (side == Win32xx::DS_DOCKED_TOP || side == Win32xx::DS_DOCKED_BOTTOM);
        if (!horizontalSize && !verticalSize)
            return;

        const double ratio = horizontalSize ? xRatio : yRatio;
        const int minPx = horizontalSize ? spec.side_min : spec.bottom_min;
        const int maxPx = horizontalSize ? spec.side_max : spec.bottom_max;
        const int next = scaled_clamped_size(frame, dock->GetDockSize(), ratio, minPx, maxPx);
        if (next > 0 && next != dock->GetDockSize()) {
            dock->SetDockSize(next);
            changed = true;
        }
    };

    scaleDock(frame.m_pDockFileTree, kMainDefaultLayout.explorer);
#if FEATURE_COMMAND_QUEUE_VIEW
    scaleDock(frame.m_pDockQueue, kMainDefaultLayout.queue);
#endif
#if FEATURE_COMMAND_LOG_VIEW
    scaleDock(frame.m_pDockLog, kMainDefaultLayout.log);
#endif
    scaleDock(frame.m_pDockSettings, kMainDefaultLayout.settings);
#if FEATURE_COMMAND_FIND
    scaleDock(frame.m_pDockFindResults, kMainDefaultLayout.results);
#endif
#if FEATURE_COMMAND_DUPLICATES
    scaleDock(frame.m_pDockDuplicateResults, kMainDefaultLayout.results);
#endif
#ifdef FEATURE_CHAT_WEB
    scaleDock(frame.m_pDockChatWeb, kMainDefaultLayout.chat);
#endif
#ifdef FEATURE_NODES
    scaleDock(frame.m_pDockNodes, kMainDefaultLayout.nodes);
#endif

    if (changed)
        frame.RecalcLayout();
}

void CDefaultMainWorkbench::AttachClientView(CMainFrame& frame)
{
    frame.SetView(frame.m_viewerManager.ActiveView());
}

void CDefaultMainWorkbench::SetupDockContainers(CMainFrame& frame)
{
#if FEATURE_COMMAND_QUEUE_VIEW
    if (frame.m_pDockQueue)
        frame.m_pDockQueue->GetQueueContainer().SetHideSingleTab(TRUE);
#endif
#if FEATURE_COMMAND_LOG_VIEW
    if (frame.m_pDockLog)
        frame.m_pDockLog->GetLogContainer().SetHideSingleTab(TRUE);
#endif
    if (frame.m_pDockSettings)
        frame.m_pDockSettings->GetSettingsContainer().SetHideSingleTab(TRUE);
#if FEATURE_COMMAND_FIND
    if (frame.m_pDockFindResults)
        frame.m_pDockFindResults->GetFindResultsContainer().SetHideSingleTab(TRUE);
#endif
#if FEATURE_COMMAND_DUPLICATES
    if (frame.m_pDockDuplicateResults)
        frame.m_pDockDuplicateResults->GetDuplicateResultsContainer().SetHideSingleTab(TRUE);
#endif
    if (frame.m_pDockFileTree)
        frame.m_pDockFileTree->GetFileTreeContainer().SetHideSingleTab(TRUE);
#ifdef FEATURE_NODES
    if (frame.m_pDockNodes)
        frame.m_pDockNodes->GetNodesContainer().SetHideSingleTab(TRUE);
#endif
}

void CDefaultMainWorkbench::ClearDockPointers(CMainFrame& frame)
{
    frame.m_pDockQueue            = nullptr;
    frame.m_pDockLog              = nullptr;
    frame.m_pDockSettings         = nullptr;
    frame.m_pDockFindResults      = nullptr;
    frame.m_pDockDuplicateResults = nullptr;
#ifdef FEATURE_CHAT_WEB
    frame.m_pDockChatWeb = nullptr;
#endif
    frame.m_pDockFileTree = nullptr;
    frame.m_pDockViewerPanel = nullptr;
#ifdef FEATURE_NODES
    frame.m_pDockNodes = nullptr;
#endif
}

void CDefaultMainWorkbench::BindDockPointers(CMainFrame& frame)
{
#if FEATURE_COMMAND_QUEUE_VIEW
    frame.m_pDockQueue            = static_cast<CDockQueue*>(frame.GetDockFromID(CMainFrame::DOCK_ID_QUEUE));
#endif
#if FEATURE_COMMAND_LOG_VIEW
    frame.m_pDockLog              = static_cast<CDockLog*>(frame.GetDockFromID(CMainFrame::DOCK_ID_LOG));
#endif
    frame.m_pDockSettings         = static_cast<CDockSettings*>(frame.GetDockFromID(CMainFrame::DOCK_ID_SETTINGS));
#if FEATURE_COMMAND_FIND
    frame.m_pDockFindResults      = static_cast<CDockFindResults*>(frame.GetDockFromID(CMainFrame::DOCK_ID_FINDRESULTS));
#endif
#if FEATURE_COMMAND_DUPLICATES
    frame.m_pDockDuplicateResults = static_cast<CDockDuplicateResults*>(
        frame.GetDockFromID(CMainFrame::DOCK_ID_DUPLICATERESULTS));
#endif

    if (auto* d = frame.GetDockFromID(CMainFrame::DOCK_ID_CHAT)) {
#ifdef FEATURE_CHAT_WEB
        frame.m_pDockChatWeb = dynamic_cast<CDockChatWeb*>(d);
#endif
        (void)d;
    }
    frame.m_pDockFileTree = static_cast<CDockFileTree*>(frame.GetDockFromID(CMainFrame::DOCK_ID_FILETREE));
    frame.m_pDockViewerPanel = static_cast<CDockViewerPanel*>(frame.GetDockFromID(CMainFrame::DOCK_ID_VIEWER_PANEL));
#ifdef FEATURE_NODES
    frame.m_pDockNodes = static_cast<CDockNodes*>(frame.GetDockFromID(CMainFrame::DOCK_ID_NODES));
#endif
}

void CDefaultMainWorkbench::FinishDockRestore(CMainFrame& frame)
{
    SetupDockContainers(frame);
    auto styleHeight = [&frame](CDocker* d) {
        if (d)
            d->SetCaptionHeight(frame.DpiScaleInt(26));
    };
#if FEATURE_COMMAND_QUEUE_VIEW
    styleHeight(frame.m_pDockQueue);
#endif
#if FEATURE_COMMAND_LOG_VIEW
    styleHeight(frame.m_pDockLog);
#endif
    styleHeight(frame.m_pDockSettings);
#if FEATURE_COMMAND_FIND
    styleHeight(frame.m_pDockFindResults);
#endif
#if FEATURE_COMMAND_DUPLICATES
    styleHeight(frame.m_pDockDuplicateResults);
#endif
#ifdef FEATURE_CHAT_WEB
    styleHeight(frame.m_pDockChatWeb);
#endif
    styleHeight(frame.m_pDockFileTree);
    styleHeight(frame.m_pDockViewerPanel);
#ifdef FEATURE_NODES
    styleHeight(frame.m_pDockNodes);
#endif
}

// ── §7: Default selection / preview policy ──────────────────────────────────

std::vector<std::wstring> IPreviewPolicy::FilterExplorerSelection(
    const std::vector<std::wstring>& raw) const
{
    std::vector<std::wstring> out;
    out.reserve(raw.size());
    for (const auto& p : raw) {
        std::error_code ec;
        if (std::filesystem::is_directory(std::filesystem::path(p), ec)) {
            out.push_back(p);
            continue;
        }
        std::wstring ext = std::filesystem::path(p).extension().wstring();
        for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
        if (pmui::is_image_ext(ext) || pmui::is_browser_image_ext(ext)
            || pmui::is_text_preview_eligible_for_path(p)
            || pmui::is_viewer_3d_ext(ext) || pmui::is_viewer_pdf_ext(ext)
            || pmui::is_viewer_spreadsheet_ext(ext) || pmui::is_video_preview_eligible_for_path(p)) {
            std::error_code ec2;
            if (std::filesystem::is_regular_file(std::filesystem::path(p), ec2))
                out.push_back(p);
        }
    }
    return out;
}

bool IPreviewPolicy::ShouldUpdateCentrePreview(bool /*is_empty*/, bool /*startup_latch*/,
                                               const std::vector<std::wstring>& /*explorer_paths*/) const
{
    return true;  // main workbench: always update
}

void IPreviewPolicy::OnPreviewChanged(CMainFrame& /*frame*/,
                                       PreviewSource /*source*/,
                                       const std::vector<std::wstring>& /*paths*/,
                                       PreviewStatus /*status*/)
{
    // Default: no-op.  Workbenches override to update latches, push chat context, etc.
}

void IPreviewPolicy::OnExplorerFolderPath(CMainFrame& /*frame*/, const std::wstring& /*folder*/)
{
    // Default: no-op.  Viewer workbench overrides to auto-preview first file in folder.
}

// ── §7c: Default status-bar layout ──────────────────────────────────────────

IStatusBarModel::Layout IStatusBarModel::GetStatusBarLayout() const
{
    // Main workbench: hint, explorer, selection, optional queue, system.
    Layout layout{};
#if !FEATURE_COMMAND_QUEUE_VIEW
    layout.count = 4;
    layout.queue = -1;
    layout.system = 3;
#endif
    return layout;
}

void IStatusBarModel::RebuildStatusBarParts(CMainFrame& frame)
{
    HWND hSb = frame.GetStatusBar().GetHwnd();
    if (!::IsWindow(hSb)) return;

    RECT rc{};
    ::GetClientRect(hSb, &rc);
    const int total = rc.right - rc.left;

    const auto layout = GetStatusBarLayout();

    const int partSys   = 320;
    const int partQueue = (layout.queue >= 0)     ? 230 : 0;
    const int partExpl  = 300;
    const int partSel   = (layout.selection >= 0) ? 220 : 0;

    const int part0 = std::max(80, total - partExpl - partSel - partQueue - partSys);

    // Build widths array based on active parts.
    std::vector<int> widths;
    widths.push_back(part0);                         // hint
    widths.push_back(part0 + partExpl);              // explorer
    if (layout.selection >= 0)
        widths.push_back(part0 + partExpl + partSel);                    // selection
    if (layout.queue >= 0)
        widths.push_back(part0 + partExpl + partSel + partQueue);        // queue
    widths.push_back(-1);                                                // system (always last)

    ::SendMessageW(hSb, SB_SETPARTS, widths.size(),
                   reinterpret_cast<LPARAM>(widths.data()));

    frame.ApplyExplorerFolderToStatusBar(frame.m_statusExplorerFolder);
    if (layout.selection >= 0)
        frame.UpdateExplorerSelectionStatusPart();
    frame.ReapplyCachedStatusBarTexts();
}

// ── §7b: Default deferred post-layout sequencing ────────────────────────────

void IDeferredInitSequence::DeferredPostLayoutInit(CMainFrame& frame,
                                        const std::vector<std::wstring>& pendingPaths)
{
    if (frame.m_pDockFileTree && frame.m_pDockFileTree->IsWindowVisible()) {
        auto& eb = frame.m_pDockFileTree->GetFileTreeContainer().GetBrowserView();
        if (frame.m_dockLayoutFromSettingsJson && pendingPaths.empty())
            eb.RequestRebuildAfterReDock();
        else
            eb.SyncShellHostLayout();
        frame.m_dockLayoutFromSettingsJson = false;
    }
    (void)pendingPaths;
    frame.ApplyPendingStartupFileTreeIfAny();
}

// ── §7a: Default startup path handling ──────────────────────────────────────

void IStartupHandler::ApplyStartupFilePaths(CMainFrame& frame,
                                       const std::vector<std::wstring>& paths)
{
    // Fallback for workbenches that do not override (queue + file-tree latch only).
    if (frame.m_pDockQueue)
        frame.AddFilesToQueue(paths);
    frame.SelectFirstStartupFileInFileTree(paths);
}

void CDefaultMainWorkbench::ApplyStartupFilePaths(CMainFrame& frame,
                                                const std::vector<std::wstring>& paths)
{
    // Queue rows for batch ops; centre preview via PreviewCoordinator (same path as viewer).
    // openSingleFilePreview=false — LoadPicture is image-only and races the coordinator.
    if (frame.m_pDockQueue)
        frame.AddFilesToQueue(paths, false);
    frame.LoadFirstStartupPreviewFromPaths(paths);
}

} // namespace pmui
