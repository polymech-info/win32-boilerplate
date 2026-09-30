#pragma once

#include "stdafx.h"
#include "FileViewer.h"

namespace pmui {

class CViewerTabsHost : public CWnd {
public:
    CViewerTabsHost() = default;

    void AttachViewer(CFileViewer& viewer);
    CFileViewer* ActiveViewer() const noexcept;
    CFileViewer& LiveViewerForExplorer(bool* created = nullptr);
    bool IsPinned() const noexcept;
    void SetPinned(bool pinned);
    void SetTabTitle(const std::wstring& title);

protected:
    void PreCreate(CREATESTRUCT& cs) override;
    int OnCreate(CREATESTRUCT& cs) override;
    LRESULT WndProc(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    struct ViewerTab {
        CFileViewer* viewer = nullptr;
        std::unique_ptr<CFileViewer> owned;
        std::wstring title = L"Preview";
        bool pinned = false;
    };

    void EnsureTab();
    CFileViewer& AddOwnedLiveTab();
    void SelectTab(size_t index);
    void SyncTabText(size_t index);
    void ShowActiveViewer();
    void EnsurePinButton();
    void LayoutChildren();
    void UpdatePinButton();
    CString DisplayTitle(const ViewerTab& tab) const;

    CTab m_tabs;
    HWND m_hPinButton = nullptr;
    std::vector<ViewerTab> m_tabsModel;
    size_t m_activeIndex = 0;
};

} // namespace pmui
