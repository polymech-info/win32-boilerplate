// PreviewCoordinator.cpp — §8 single owner for the centre file-viewer preview.
#include "stdafx.h"
#include "PreviewCoordinator.h"
#include "FileViewer.h"
#include "Mainfrm.h"
#include "workbench/MainWorkbench.h"
#include "ui_log_file.hpp"
#include "helpers/text_conv.hpp"

namespace pmui {

static const char* SourceTag(PreviewSource s)
{
    switch (s) {
    case PreviewSource::None:           return "None";
    case PreviewSource::Explorer:       return "Explorer";
    case PreviewSource::CliStartup:     return "CliStartup";
    case PreviewSource::QueueRow:       return "QueueRow";
    case PreviewSource::ChatGenerated:  return "ChatGenerated";
    case PreviewSource::RecentFile:     return "RecentFile";
    case PreviewSource::AppBrowse:      return "AppBrowse";
    case PreviewSource::SessionReplay:  return "SessionReplay";
    case PreviewSource::Extension:      return "Extension";
    }
    return "?";
}

static const char* StatusTag(PreviewStatus s)
{
    switch (s) {
    case PreviewStatus::Ok:               return "Ok";
    case PreviewStatus::Failed:           return "Failed";
    case PreviewStatus::NothingPreviewable: return "NothingPreviewable";
    case PreviewStatus::Suppressed:       return "Suppressed";
    }
    return "?";
}

PreviewStatus CPreviewCoordinator::Request(
    PreviewSource                     source,
    const std::vector<std::wstring>&  paths,
    CFileViewer&                      viewer,
    IWorkbench&                       wb,
    CMainFrame&                       frame)
{
    // ── 1. Policy gate ──────────────────────────────────────────────────────
    // For Explorer-sourced requests, the workbench may suppress empty selections
    // (e.g. viewer startup latch) or all updates (chat workbench).
    // Non-Explorer sources (CLI, queue-row, recent, etc.) always pass.
    if (source == PreviewSource::Explorer) {
        const bool isEmpty = paths.empty();
        const bool latch   = frame.StartupPreviewLatchActive();
        if (!wb.previewPolicy().ShouldUpdateCentrePreview(isEmpty, latch, paths)) {
            if (!isEmpty && !paths.empty()) {
                ui_log_file_eventf(
                    "PreviewCoord: SUPPRESSED src=%s paths=%zu latch=%d first=\"%s\"",
                    SourceTag(source), paths.size(), latch ? 1 : 0,
                    wide_to_utf8(paths.front()).c_str());
            } else {
                ui_log_file_eventf("PreviewCoord: SUPPRESSED src=%s paths=%zu latch=%d",
                                   SourceTag(source), paths.size(), latch ? 1 : 0);
            }
            return PreviewStatus::Suppressed;
        }
    }

    // ── 2. Log the request ──────────────────────────────────────────────────
    if (paths.empty()) {
        ui_log_file_eventf("PreviewCoord: Request src=%s paths=0 (clear)", SourceTag(source));
    } else {
        const std::string first_u8 = wide_to_utf8(paths.front());
        ui_log_file_eventf("PreviewCoord: Request src=%s paths=%zu first=\"%s\"",
                           SourceTag(source), paths.size(), first_u8.c_str());
    }

    // ── 3. Dispatch to viewer ───────────────────────────────────────────────
    PreviewStatus status = viewer.OpenFile(paths, source);

    // ── 4. Update coordinator state ─────────────────────────────────────────
    m_state.source = source;
    m_state.paths  = paths;
    if (status == PreviewStatus::Ok && !paths.empty())
        m_state.active = paths.front();
    else
        m_state.active.clear();

    ui_log_file_eventf("PreviewCoord: result=%s active=\"%s\"",
                       StatusTag(status),
                       m_state.active.empty() ? "" : wide_to_utf8(m_state.active).c_str());

    // ── 5. Notify workbench ─────────────────────────────────────────────────
    wb.previewPolicy().OnPreviewChanged(frame, source, paths, status);

    return status;
}

void CPreviewCoordinator::AdoptNavigatedImage(const std::wstring& path)
{
    m_state.paths  = {path};
    m_state.active = path;
}

} // namespace pmui
