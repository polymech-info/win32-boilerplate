#include "stdafx.h"
#include "workbench/MainWorkbench.h"
#include "Resource.h"
#include "Mainfrm.h"
#include "win/settings_store.hpp"
#include "helpers/chat_context_attach.hpp"
#if defined(FEATURE_CHAT_WEB)
#  include "ChatWebResource.h" // pmui::chat_web_available
#endif

#include <algorithm>
#include <memory>

namespace pmui {

const char* CChatSimpleWorkbench::workbenchSettingsId() const noexcept
{
    return "chat";
}

void CChatSimpleWorkbench::BuildInitialDockLayout(CMainFrame& frame)
{
    frame.CloseAllDockers();
    ClearDockPointers(frame);

    Win32xx::CDocker* ancestor = frame.GetDockAncestor();
    if (!ancestor)
        return;

    // Explorer: visible by default, shell frames (address bar / nav) on; chat is the frame SetView.
    frame.m_pDockFileTree = static_cast<CDockFileTree*>(
        ancestor->AddDockedChild(std::make_unique<CDockFileTree>(),
                                 Win32xx::DS_DOCKED_LEFT, frame.DpiScaleInt(300),
                                 CMainFrame::DOCK_ID_FILETREE));
    if (frame.m_pDockFileTree) {
        frame.m_pDockFileTree->GetFileTreeContainer().GetBrowserView().SetShowShellFrames(true);
        frame.InvalidateToggle(IDC_CMD_VIEW_FILETREE);
    }
    frame.InvalidateToggle(IDC_CMD_VIEW_CHAT);
}

void CChatSimpleWorkbench::AttachClientView(CMainFrame& frame)
{
#if defined(FEATURE_CHAT_WEB)
    if (pmui::chat_web_available()) {
        frame.SetView(frame.m_workbenchClientChatWeb);
        return;
    }
#endif
}

void CChatSimpleWorkbench::SetupDockContainers(CMainFrame& frame)
{
    if (frame.m_pDockFileTree)
        frame.m_pDockFileTree->GetFileTreeContainer().SetHideSingleTab(TRUE);
    // Chat surface is the frame SetView, not a docker — no tab container to tune here.
}

void CChatSimpleWorkbench::LoadLayout(CMainFrame& frame)
{
    media::settings::WindowLayout layout;
    std::string                err;
    if (!media::settings::load_window_layout(layout, err, workbenchSettingsId()))
        return;
    frame.ApplyWindowLayoutData(layout);
    if (!layout.has_panel_visibility)
        frame.FinishChatWorkbenchDefaultDockVisibility();
}

bool CChatSimpleWorkbench::AcceptRestoredDockLayout(CMainFrame& frame, std::string& reason) const
{
    reason.clear();
    const bool no_visible_docks = frame.GetDockAncestor()
        && frame.GetDockAncestor()->GetDockChildren().empty();
    if (no_visible_docks)
        return true;

    // Chat workbench allows Explorer to be hidden by user preference; do not
    // reject layouts just because DOCK_ID_FILETREE is absent.
    if (frame.m_pDockChatWeb) {
        reason = "chat workbench saved dock layout contains stale docked Chat panel";
        return false;
    }
    return true;
}

void CChatSimpleWorkbench::ResetLayout(CMainFrame& frame)
{
    BuildInitialDockLayout(frame);
    frame.RecalcLayout();

    (void)frame.SaveDockLayout();
    std::string err;
    media::settings::WindowLayout def;
    if (!frame.FillWindowLayoutFromState(def))
        return;
    def.has_panel_visibility  = true;
    def.pv_queue              = false;
    def.pv_log                = false;
    def.pv_settings           = false;
    def.pv_fileinfo           = false;
    def.pv_findresults        = false;
    def.pv_dupresults         = false;
    def.pv_chat               = false;
    def.pv_filetree                 = true;
    def.filetree_show_shell_frames  = true; // "Advanced" Explorer: address bar, shell frames
    def.pv_nodes   = false;
    def.defer_dock_containers = true;
    media::settings::save_window_layout(def, err, workbenchSettingsId());
}

void CChatSimpleWorkbench::ApplyStartupFilePaths(CMainFrame& frame,
                                                 const std::vector<std::wstring>& paths)
{
    IStartupHandler::ApplyStartupFilePaths(frame, paths);
}

// ── §7: Chat selection / preview policy ─────────────────────────────────────

std::vector<std::wstring> CChatSimpleWorkbench::FilterExplorerSelection(
    const std::vector<std::wstring>& raw) const
{
    std::vector<std::wstring> out;
    out.reserve(raw.size());
    for (const auto& p : raw) {
        if (pmui::chat_context_path_allowed(p))
            out.push_back(p);
    }
    return out;
}

bool CChatSimpleWorkbench::ShouldUpdateCentrePreview(bool /*is_empty*/, bool /*startup_latch*/,
                                                     const std::vector<std::wstring>& /*explorer_paths*/) const
{
    return false;  // chat workbench: no centre preview
}

// ── §7c: Chat status-bar layout ─────────────────────────────────────────────

IStatusBarModel::Layout CChatSimpleWorkbench::GetStatusBarLayout() const
{
    // Chat: 3-part — no selection count, no queue ETA. hint(0), explorer(1), system(2).
    return Layout{ 3, /*hint*/0, /*explorer*/1, /*selection*/-1, /*queue*/-1, /*system*/2 };
}

} // namespace pmui
