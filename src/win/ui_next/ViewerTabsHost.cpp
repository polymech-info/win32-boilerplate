#include "stdafx.h"
#include "ViewerTabsHost.h"
#include "helpers/theme.hpp"

namespace pmui {

namespace {
constexpr UINT_PTR kPinButtonId = 44001;
}

void CViewerTabsHost::AttachViewer(CFileViewer& viewer)
{
    if (m_tabsModel.empty()) {
        ViewerTab tab;
        tab.viewer = &viewer;
        tab.title = L"Preview";
        m_tabsModel.push_back(std::move(tab));
        m_activeIndex = 0;
    } else {
        m_tabsModel.front().viewer = &viewer;
    }

    if (IsWindow()) {
        if (!viewer.IsWindow())
            viewer.Create(*this);
        else
            viewer.SetParent(*this);
        viewer.ShowWindow(SW_SHOW);
        EnsureTab();
        SelectTab(m_activeIndex);
        LayoutChildren();
    }
}

CFileViewer* CViewerTabsHost::ActiveViewer() const noexcept
{
    if (m_tabsModel.empty() || m_activeIndex >= m_tabsModel.size())
        return nullptr;
    return m_tabsModel[m_activeIndex].viewer;
}

CFileViewer& CViewerTabsHost::LiveViewerForExplorer(bool* created)
{
    if (created)
        *created = false;

    if (m_tabsModel.empty()) {
        if (created)
            *created = true;
        return AddOwnedLiveTab();
    }

    if (IsPinned()) {
        if (created)
            *created = true;
        return AddOwnedLiveTab();
    }

    return *m_tabsModel[m_activeIndex].viewer;
}

bool CViewerTabsHost::IsPinned() const noexcept
{
    return !m_tabsModel.empty() && m_activeIndex < m_tabsModel.size()
        && m_tabsModel[m_activeIndex].pinned;
}

void CViewerTabsHost::SetPinned(bool pinned)
{
    if (m_tabsModel.empty() || m_activeIndex >= m_tabsModel.size())
        return;
    ViewerTab& tab = m_tabsModel[m_activeIndex];
    if (tab.pinned == pinned)
        return;
    tab.pinned = pinned;
    SyncTabText(m_activeIndex);
    UpdatePinButton();
}

void CViewerTabsHost::SetTabTitle(const std::wstring& title)
{
    if (m_tabsModel.empty() || m_activeIndex >= m_tabsModel.size())
        return;
    m_tabsModel[m_activeIndex].title = title.empty() ? L"Preview" : title;
    SyncTabText(m_activeIndex);
}

void CViewerTabsHost::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);
    cs.style |= WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
}

int CViewerTabsHost::OnCreate(CREATESTRUCT&)
{
    if (!m_tabs.Create(*this))
        return -1;

    const auto& pal = pmui::theme_palette();
    m_tabs.SetBlankPageColor(pal.window_bg);
    m_tabs.SetTabsAtTop(TRUE);
    m_tabs.SetShowButtons(FALSE);
    EnsureTab();
    EnsurePinButton();
    LayoutChildren();
    return 0;
}

LRESULT CViewerTabsHost::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wparam) == kPinButtonId && HIWORD(wparam) == BN_CLICKED) {
            SetPinned(!IsPinned());
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (auto* hdr = reinterpret_cast<LPNMHDR>(lparam);
            hdr && hdr->hwndFrom == m_tabs.GetHwnd() && hdr->code == TCN_SELCHANGE) {
            const int sel = m_tabs.GetCurSel();
            if (sel >= 0)
                SelectTab(static_cast<size_t>(sel));
            return 0;
        }
        break;
    case WM_SIZE:
        LayoutChildren();
        break;
    case WM_ERASEBKGND:
        return TRUE;
    }
    return WndProcDefault(msg, wparam, lparam);
}

void CViewerTabsHost::EnsureTab()
{
    if (!m_tabs.IsWindow())
        return;

    while (m_tabs.GetItemCount() < static_cast<int>(m_tabsModel.size())) {
        const size_t index = static_cast<size_t>(m_tabs.GetItemCount());
        TCITEM tie{};
        CString title = DisplayTitle(m_tabsModel[index]);
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<LPWSTR>(title.c_str());
        m_tabs.InsertItem(static_cast<int>(index), &tie);
    }

    for (size_t i = 0; i < m_tabsModel.size(); ++i)
        SyncTabText(i);
}

CFileViewer& CViewerTabsHost::AddOwnedLiveTab()
{
    ViewerTab tab;
    tab.owned = std::make_unique<CFileViewer>();
    tab.viewer = tab.owned.get();
    tab.title = L"Live";
    tab.pinned = false;
    m_tabsModel.push_back(std::move(tab));

    CFileViewer& viewer = *m_tabsModel.back().viewer;
    if (IsWindow()) {
        viewer.Create(*this);
        viewer.ShowWindow(SW_HIDE);
        EnsureTab();
        SelectTab(m_tabsModel.size() - 1);
        LayoutChildren();
    } else {
        m_activeIndex = m_tabsModel.size() - 1;
    }
    return viewer;
}

void CViewerTabsHost::SelectTab(size_t index)
{
    if (index >= m_tabsModel.size())
        return;

    m_activeIndex = index;
    if (m_tabs.IsWindow() && m_tabs.GetCurSel() != static_cast<int>(index))
        m_tabs.SetCurSel(static_cast<int>(index));

    ShowActiveViewer();
    UpdatePinButton();
    LayoutChildren();
}

void CViewerTabsHost::SyncTabText(size_t index)
{
    if (!m_tabs.IsWindow() || index >= m_tabsModel.size()
        || index >= static_cast<size_t>(m_tabs.GetItemCount()))
        return;
    m_tabs.SetTabText(static_cast<int>(index), DisplayTitle(m_tabsModel[index]));
}

void CViewerTabsHost::ShowActiveViewer()
{
    for (size_t i = 0; i < m_tabsModel.size(); ++i) {
        CFileViewer* viewer = m_tabsModel[i].viewer;
        if (viewer && viewer->IsWindow())
            viewer->ShowWindow(i == m_activeIndex ? SW_SHOW : SW_HIDE);
    }
}

void CViewerTabsHost::EnsurePinButton()
{
    if (m_hPinButton && ::IsWindow(m_hPinButton))
        return;

    m_hPinButton = ::CreateWindowExW(0, L"BUTTON", L"\xD83D\xDCCC",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_TABSTOP,
        0, 0, 1, 1, GetHwnd(), reinterpret_cast<HMENU>(kPinButtonId),
        GetApp()->GetInstanceHandle(), nullptr);
    UpdatePinButton();
}

void CViewerTabsHost::LayoutChildren()
{
    if (!IsWindow())
        return;

    const CRect rc = GetClientRect();
    const int tabH = m_tabs.IsWindow() ? std::max(m_tabs.GetTabHeight(), DpiScaleInt(26)) : 0;
    const int gap = DpiScaleInt(4);
    const int pinW = tabH > 0 ? tabH : DpiScaleInt(26);
    const int pinH = tabH > 0 ? tabH - DpiScaleInt(4) : DpiScaleInt(22);
    if (m_tabs.IsWindow()) {
        const int tabsW = std::max(0, rc.Width() - pinW - gap);
        m_tabs.SetWindowPos(HWND_TOP, rc.left, rc.top, tabsW, tabH, SWP_SHOWWINDOW);
    }

    if (m_hPinButton && ::IsWindow(m_hPinButton)) {
        const int x = std::max(rc.left, rc.right - pinW);
        const int y = rc.top + std::max(0, (tabH - pinH) / 2);
        ::SetWindowPos(m_hPinButton, HWND_TOP, x, y, pinW, pinH, SWP_SHOWWINDOW);
    }

    for (size_t i = 0; i < m_tabsModel.size(); ++i) {
        CFileViewer* viewer = m_tabsModel[i].viewer;
        if (viewer && viewer->IsWindow()) {
            viewer->SetWindowPos(HWND_TOP, rc.left, rc.top + tabH, rc.Width(),
                                 std::max(0, rc.Height() - tabH),
                                 i == m_activeIndex ? SWP_SHOWWINDOW : SWP_NOZORDER);
            if (i != m_activeIndex)
                viewer->ShowWindow(SW_HIDE);
        }
    }
}

void CViewerTabsHost::UpdatePinButton()
{
    if (!m_hPinButton || !::IsWindow(m_hPinButton))
        return;
    ::SendMessageW(m_hPinButton, BM_SETCHECK, IsPinned() ? BST_CHECKED : BST_UNCHECKED, 0);
}

CString CViewerTabsHost::DisplayTitle(const ViewerTab& tab) const
{
    CString title;
    title.Format(L"%s%s", tab.pinned ? L"[pinned] " : L"", tab.title.c_str());
    return title;
}

} // namespace pmui
