#pragma once

#include "FileViewer.h"
#include "ViewerTabsHost.h"

namespace pmui {

/**
 * Phase-1 centre preview indirection.
 *
 * Today this wraps the existing CMainFrame::m_fileViewer and returns it as the
 * active view. Future tab work can change the active/live viewer policy here
 * without forcing every caller to know about tabs.
 */
class CViewerManager {
public:
    CViewerManager() = default;

    void Attach(CFileViewer& viewer) noexcept
    {
        m_primary = &viewer;
        m_active = &viewer;
    }

    void AttachHost(CViewerTabsHost& host) noexcept { m_host = &host; }
    void SetActive(CFileViewer& viewer) noexcept { m_active = &viewer; }

    CFileViewer& ActiveView() noexcept
    {
        if (m_tabbed && m_host) {
            if (CFileViewer* viewer = m_host->ActiveViewer())
                return *viewer;
        }
        return *m_active;
    }
    const CFileViewer& ActiveView() const noexcept
    {
        if (m_tabbed && m_host) {
            if (CFileViewer* viewer = m_host->ActiveViewer())
                return *viewer;
        }
        return *m_active;
    }

    CFileViewer& LiveViewForExplorer(bool* created = nullptr)
    {
        if (m_tabbed && m_host)
            return m_host->LiveViewerForExplorer(created);
        if (created)
            *created = false;
        return *m_active;
    }

    bool IsTabbed() const noexcept { return m_tabbed; }
    void SetTabbed(bool tabbed) noexcept { m_tabbed = tabbed; }
    bool IsActivePinned() const noexcept { return m_tabbed && m_host && m_host->IsPinned(); }
    CViewerTabsHost* Host() const noexcept { return m_host; }
    CFileViewer* PrimaryView() const noexcept { return m_primary; }

private:
    CFileViewer* m_primary = nullptr;
    CFileViewer* m_active = nullptr;
    CViewerTabsHost* m_host = nullptr;
    bool m_tabbed = false;
};

} // namespace pmui
