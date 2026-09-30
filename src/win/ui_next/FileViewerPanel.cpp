#include "stdafx.h"
#include "FileViewerPanel.h"
#include "helpers/dock_chrome_i18n.hpp"
#include "win/settings_store.hpp"

namespace {
constexpr UINT_PTR kViewerPinButtonId = 44011;

void PutPixel(std::vector<DWORD>& pixels, int size, int x, int y, DWORD argb)
{
    if (x < 0 || y < 0 || x >= size || y >= size)
        return;
    pixels[static_cast<size_t>(y * size + x)] = argb;
}

void DrawLine(std::vector<DWORD>& pixels, int size, int x0, int y0, int x1, int y1, DWORD argb)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        PutPixel(pixels, size, x0, y0, argb);
        PutPixel(pixels, size, x0 + 1, y0, argb);
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

HICON CreatePinTabIcon(bool pinned)
{
    constexpr int size = 16;
    const DWORD ink = pinned ? 0xFF25D0FF : 0xFF8A929E;
    std::vector<DWORD> pixels(static_cast<size_t>(size * size), 0x00000000);

    for (int y = 3; y <= 5; ++y)
        for (int x = 3; x <= 12; ++x)
            PutPixel(pixels, size, x, y, ink);
    for (int y = 5; y <= 13; ++y)
        for (int x = 7; x <= 9; ++x)
            PutPixel(pixels, size, x, y, ink);
    if (!pinned) {
        for (int y = 4; y <= 12; ++y)
            for (int x = 4; x <= 11; ++x)
                if (x != 7 && x != 8 && x != 9 && y != 4)
                    PutPixel(pixels, size, x, y, 0x00000000);
    } else {
        for (int x = 3; x <= 12; ++x)
            PutPixel(pixels, size, x, 14, 0x8025D0FF);
        for (int y = 3; y <= 14; ++y)
            PutPixel(pixels, size, 13, y, 0x8025D0FF);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = ::GetDC(nullptr);
    HBITMAP color = ::CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, hdc);
    if (!color || !bits)
        return nullptr;
    std::memcpy(bits, pixels.data(), pixels.size() * sizeof(DWORD));

    HBITMAP mask = ::CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = ::CreateIconIndirect(&ii);
    if (mask)
        ::DeleteObject(mask);
    if (color)
        ::DeleteObject(color);
    return icon;
}
}

// ── CViewerPanelContainer ────────────────────────────────────────────────────

CViewerPanelContainer::CViewerPanelContainer()
{
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    m_baseTabText = d.viewer_tab;
    m_baseCaption = d.viewer_caption;
    SetTabText(m_baseTabText);
    SetDockCaption(m_baseCaption);
    SetTabIcon(TabIconForState(false));
    SetView(m_viewer);
}

CViewerPanelContainer::~CViewerPanelContainer()
{
    if (m_hPinnedIcon)
        ::DestroyIcon(m_hPinnedIcon);
    if (m_hUnpinnedIcon)
        ::DestroyIcon(m_hUnpinnedIcon);
}

void CViewerPanelContainer::SetPinned(bool pinned)
{
    if (m_pinned == pinned)
        return;
    m_pinned = pinned;
    RefreshTitle();
    UpdatePinButton();
    Invalidate();
}

void CViewerPanelContainer::SetTitle(const CString& title)
{
    m_baseTabText = title;
    m_baseCaption = title;
    RefreshTitle();
}

int CViewerPanelContainer::OnCreate(CREATESTRUCT& cs)
{
    const int r = CDockContainerBase::OnCreate(cs);
    m_hPinButton = ::CreateWindowExW(0, L"BUTTON", L"Pin",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_TABSTOP,
        0, 0, 1, 1, GetHwnd(), reinterpret_cast<HMENU>(kViewerPinButtonId),
        GetApp()->GetInstanceHandle(), nullptr);
    UpdatePinButton();
    LayoutPinButton();
    return r;
}

LRESULT CViewerPanelContainer::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wparam) == kViewerPinButtonId && HIWORD(wparam) == BN_CLICKED) {
            SetPinned(!m_pinned);
            return 0;
        }
        break;
    case WM_SIZE:
    case WM_WINDOWPOSCHANGED:
        LayoutPinButton();
        break;
    }
    return CDockContainerBase::WndProc(msg, wparam, lparam);
}

void CViewerPanelContainer::LayoutPinButton()
{
    if (!m_hPinButton || !::IsWindow(m_hPinButton))
        return;
    const CRect rc = GetClientRect();
    const int w = DpiScaleInt(46);
    const int h = std::max<int>(DpiScaleInt(20), GetTabHeight() - DpiScaleInt(4));
    const int margin = DpiScaleInt(3);
    const int x = std::max<int>(0, static_cast<int>(rc.right) - w - margin);
    ::SetWindowPos(m_hPinButton, HWND_TOP, x, rc.top + margin, w, h, SWP_SHOWWINDOW);
}

void CViewerPanelContainer::RefreshTitle()
{
    SetTabText(m_baseTabText);
    SetDockCaption(m_baseCaption);
    SetTabIcon(TabIconForState(m_pinned));
}

void CViewerPanelContainer::UpdatePinButton()
{
    if (!m_hPinButton || !::IsWindow(m_hPinButton))
        return;
    ::SendMessageW(m_hPinButton, BM_SETCHECK, m_pinned ? BST_CHECKED : BST_UNCHECKED, 0);
}

HICON CViewerPanelContainer::TabIconForState(bool pinned)
{
    HICON& icon = pinned ? m_hPinnedIcon : m_hUnpinnedIcon;
    if (!icon)
        icon = CreatePinTabIcon(pinned);
    return icon;
}

// ── CMainViewerContainer ─────────────────────────────────────────────────────

int CMainViewerContainer::OnCreate(CREATESTRUCT& cs)
{
    const int r = CDockContainerBase::OnCreate(cs);
    m_hPinButton = ::CreateWindowExW(0, L"BUTTON", L"Pin",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_TABSTOP,
        0, 0, 1, 1, GetHwnd(), reinterpret_cast<HMENU>(kViewerPinButtonId),
        GetApp()->GetInstanceHandle(), nullptr);
    UpdatePinButton();
    LayoutPinButton();
    return r;
}

void CMainViewerContainer::AddContainer(CDockContainer* pContainer, BOOL insert, BOOL selectPage)
{
    CDockContainerBase::AddContainer(pContainer, insert, selectPage);

    DWORD dockStyle = GetDocker()->GetDockStyle();
    dockStyle &= ~(DS_NO_CLOSE | DS_NO_UNDOCK);
    GetDocker()->SetDockStyle(dockStyle);
    UpdatePinButton();
    LayoutPinButton();
}

void CMainViewerContainer::RemoveContainer(CDockContainer* pContainer, BOOL updateParent)
{
    CDockContainerBase::RemoveContainer(pContainer, updateParent);

    DWORD dockStyle = GetDocker()->GetDockStyle();
    dockStyle &= ~(DS_NO_CLOSE | DS_NO_UNDOCK);
    GetDocker()->SetDockStyle(dockStyle);
    UpdatePinButton();
    LayoutPinButton();
}

LRESULT CMainViewerContainer::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wparam) == kViewerPinButtonId && HIWORD(wparam) == BN_CLICKED) {
            ToggleActivePinned();
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (TogglePinnedFromTabIcon(lparam))
            return 0;
        break;
    case WM_SIZE:
    case WM_WINDOWPOSCHANGED:
        LayoutPinButton();
        break;
    }

    LRESULT result = CDockContainerBase::WndProc(msg, wparam, lparam);

    switch (msg) {
    case WM_LBUTTONUP:
    case WM_NOTIFY:
    case WM_SIZE:
    case WM_WINDOWPOSCHANGED:
        UpdatePinButton();
        LayoutPinButton();
        break;
    default:
        break;
    }
    return result;
}

bool CMainViewerContainer::TogglePinnedFromTabIcon(LPARAM lparam)
{
    TCHITTESTINFO info{};
    info.pt = CPoint(lparam);
    const int tab = HitTest(info);
    if (tab < 0)
        return false;

    CRect rcItem;
    GetItemRect(tab, rcItem);
    const CSize iconSize = GetImages().GetIconSize();
    const int padding = DpiScaleInt(1);
    const int iconTop = static_cast<int>(rcItem.top) +
        std::max<int>(0, (static_cast<int>(rcItem.Height()) - iconSize.cy) / 2);
    CRect rcIcon;
    rcIcon.left = rcItem.left + padding;
    rcIcon.top = iconTop;
    rcIcon.right = rcItem.left + padding + iconSize.cx + DpiScaleInt(4);
    rcIcon.bottom = iconTop + iconSize.cy;
    if (!rcIcon.PtInRect(info.pt))
        return false;

    SelectPage(tab);
    if (CViewerPanelContainer* container =
            dynamic_cast<CViewerPanelContainer*>(GetContainerFromIndex(static_cast<size_t>(tab)))) {
        container->SetPinned(!container->IsPinned());
        UpdatePinButton();
        return true;
    }
    return false;
}

CViewerPanelContainer* CMainViewerContainer::ActiveViewerContainer() const
{
    return dynamic_cast<CViewerPanelContainer*>(GetActiveContainer());
}

void CMainViewerContainer::ToggleActivePinned()
{
    if (CViewerPanelContainer* active = ActiveViewerContainer())
        active->SetPinned(!active->IsPinned());
    UpdatePinButton();
}

bool CMainViewerContainer::IsActivePinned() const
{
    CViewerPanelContainer* active = ActiveViewerContainer();
    return active && active->IsPinned();
}

void CMainViewerContainer::LayoutPinButton()
{
    if (!m_hPinButton || !::IsWindow(m_hPinButton))
        return;
    const CRect rc = GetClientRect();
    const int w = DpiScaleInt(46);
    const int h = std::max<int>(DpiScaleInt(20), GetTabHeight() - DpiScaleInt(4));
    const int margin = DpiScaleInt(3);
    const int x = std::max<int>(0, static_cast<int>(rc.right) - w - margin);
    const int y = rc.top + margin;
    ::SetWindowPos(m_hPinButton, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

void CMainViewerContainer::UpdatePinButton()
{
    if (!m_hPinButton || !::IsWindow(m_hPinButton))
        return;
    CViewerPanelContainer* active = ActiveViewerContainer();
    ::EnableWindow(m_hPinButton, active ? TRUE : FALSE);
    ::SendMessageW(m_hPinButton, BM_SETCHECK,
                   (active && active->IsPinned()) ? BST_CHECKED : BST_UNCHECKED, 0);
}

// ── CDockViewerPanel ─────────────────────────────────────────────────────────

CDockViewerPanel::CDockViewerPanel()
{
    SetView(m_container);
}
