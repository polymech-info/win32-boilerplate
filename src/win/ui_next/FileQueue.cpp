#include "stdafx.h"
#include "FileQueue.h"
#include "queue_path_enumeration.hpp"
#include "win/settings_store.hpp"
#include "helpers/dock_chrome_i18n.hpp"
#include "Resource.h"
#include "helpers/theme.hpp"
#include <filesystem>
#include <shellapi.h>
#include <vector>

namespace fs = std::filesystem;

//////////////////////////////////////////
// CQueueListView
//////////////////////////////////////////

void CQueueListView::OnAttach()
{
    CListView::OnAttach();

    SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetupColumns();
    LayoutColumns();
    DragAcceptFiles(TRUE);

    // Flat header style
    HWND hHeader = ListView_GetHeader(GetHwnd());
    if (hHeader) {
        LONG_PTR s = ::GetWindowLongPtr(hHeader, GWL_STYLE);
        ::SetWindowLongPtr(hHeader, GWL_STYLE, s | HDS_FLAT);
    }

    RefreshThemeColors();
}

void CQueueListView::RefreshThemeColors()
{
    if (!IsWindow()) return;
    const auto& pal = pmui::theme_palette();
    // Use window_bg instead of control_bg for seamless edge-to-edge look
    ListView_SetBkColor   (GetHwnd(), pal.window_bg);
    ListView_SetTextBkColor(GetHwnd(), pal.window_bg);
    ListView_SetTextColor (GetHwnd(), pal.control_fg);
    pmui::theme_listview_report_header(GetHwnd());
    LayoutColumns();
    // Force aggressive redraw to ensure background color takes effect
    ::RedrawWindow(GetHwnd(), nullptr, nullptr,
        RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

void CQueueListView::PreCreate(CREATESTRUCT& cs)
{
    CListView::PreCreate(cs);
    cs.style |= LVS_REPORT | LVS_SHOWSELALWAYS;
}

void CQueueListView::SetupColumns()
{
    DeleteAllItems();

    LV_COLUMN col{};
    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    col.fmt = LVCFMT_LEFT;

    col.pszText = const_cast<LPWSTR>(L"Name");
    col.cx = DpiScaleInt(200);
    col.iSubItem = COL_NAME;
    InsertColumn(COL_NAME, col);

    col.pszText = const_cast<LPWSTR>(L"Operation");
    col.cx = DpiScaleInt(120);
    col.iSubItem = COL_OPERATION;
    InsertColumn(COL_OPERATION, col);

    col.pszText = const_cast<LPWSTR>(L"Status");
    col.cx = DpiScaleInt(170);
    col.iSubItem = COL_STATUS;
    InsertColumn(COL_STATUS, col);

    col.pszText = const_cast<LPWSTR>(L"Path");
    col.cx = DpiScaleInt(360);
    col.iSubItem = COL_PATH;
    InsertColumn(COL_PATH, col);
}

void CQueueListView::LayoutColumns()
{
    if (!IsWindow())
        return;

    RECT rc{};
    ::GetClientRect(GetHwnd(), &rc);
    const int total   = (std::max)(0, static_cast<int>(rc.right - rc.left));
    const int nameW   = DpiScaleInt(200);
    const int opW     = DpiScaleInt(120);
    const int statusW = DpiScaleInt(170);
    const int pathW   = (std::max)(DpiScaleInt(160), total - nameW - opW - statusW - DpiScaleInt(6));

    ListView_SetColumnWidth(GetHwnd(), COL_NAME,      nameW);
    ListView_SetColumnWidth(GetHwnd(), COL_OPERATION, opW);
    ListView_SetColumnWidth(GetHwnd(), COL_STATUS,    statusW);
    ListView_SetColumnWidth(GetHwnd(), COL_PATH,      pathW);
}

int CQueueListView::AddFile(const CString& path)
{
    fs::path p(std::wstring(path.c_str()));
    CString name = p.filename().c_str();

    int idx = GetItemCount();
    int item = InsertItem(idx, name);
    SetItemText(item, COL_OPERATION, L"");
    SetItemText(item, COL_STATUS, L"Queued");
    SetItemText(item, COL_PATH, path);
    return item;
}

int CQueueListView::AddToolCallRow(const CString& name, const CString& operation, const CString& status, const CString& path)
{
    int idx  = GetItemCount();
    int item = InsertItem(idx, name);
    SetItemText(item, COL_OPERATION, operation);
    SetItemText(item, COL_STATUS, status);
    SetItemText(item, COL_PATH, path);
    return item;
}

void CQueueListView::SetItemStatus(int item, LPCWSTR status)
{
    SetItemText(item, COL_STATUS, status);
}

void CQueueListView::ClearAll()
{
    DeleteAllItems();
}

void CQueueListView::RemoveSelectedItems()
{
    HWND hList = GetHwnd();
    std::vector<int> sel;
    for (int i = -1;;) {
        i = ListView_GetNextItem(hList, i, LVNI_SELECTED);
        if (i < 0) break;
        sel.push_back(i);
    }
    if (sel.empty())
        return;
    std::sort(sel.begin(), sel.end(), std::greater<int>());
    for (int idx : sel)
        ListView_DeleteItem(hList, idx);
}

int CQueueListView::QueueCount() const
{
    return GetItemCount();
}

CString CQueueListView::GetItemPath(int item)
{
    wchar_t buf[MAX_PATH * 4]{};
    LVITEMW lvi{};
    lvi.iSubItem = COL_PATH;
    lvi.pszText = buf;
    lvi.cchTextMax = MAX_PATH * 4;
    ::SendMessageW(GetHwnd(), LVM_GETITEMTEXTW, (WPARAM)item, (LPARAM)&lvi);
    return CString(buf);
}

LRESULT CQueueListView::OnDropFiles(UINT, WPARAM wparam, LPARAM)
{
    HDROP hDrop = reinterpret_cast<HDROP>(wparam);
    for (const auto& p : pmui::queue_paths::image_paths_from_hdrop(hDrop))
        AddFile(CString(p.c_str()));

    ::DragFinish(hDrop);

    // Notify the main frame to update status bar.
    GetAncestor().PostMessage(WM_COMMAND, MAKEWPARAM(0, 0), 0);
    return 0;
}

LRESULT CQueueListView::OnNotifyReflect(WPARAM, LPARAM lparam)
{
    LPNMHDR pnm = reinterpret_cast<LPNMHDR>(lparam);
    if (pnm->code == NM_CLICK) {
        LPNMITEMACTIVATE pia = reinterpret_cast<LPNMITEMACTIVATE>(lparam);
        if (pia->iItem >= 0)
            GetAncestor().SendMessage(UWM_QUEUE_ITEM_CLICKED, (WPARAM)pia->iItem, 0);
    } else if (pnm->code == LVN_ITEMCHANGED) {
        LPNMLISTVIEW plv = reinterpret_cast<LPNMLISTVIEW>(lparam);
        if (plv->iItem >= 0 && (plv->uChanged & LVIF_STATE) &&
            (plv->uNewState & LVIS_SELECTED) && !(plv->uOldState & LVIS_SELECTED))
            GetAncestor().SendMessage(UWM_QUEUE_ITEM_CLICKED, (WPARAM)plv->iItem, 0);
    }
    return 0;
}

LRESULT CQueueListView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {
        case WM_KEYDOWN:
            if (wparam == VK_DELETE) {
                GetAncestor().SendMessage(UWM_QUEUE_DELETE_SELECTION, 0, 0);
                return 0;
            }
            break;
        case WM_SIZE:
            LayoutColumns();
            break;
        case WM_NOTIFY: {
            LRESULT cd = 0;
            if (pmui::theme_header_customdraw_notify(GetHwnd(), lparam, cd))
                return cd;
            break;
        }
        case WM_DROPFILES: return OnDropFiles(msg, wparam, lparam);
        }
        return WndProcDefault(msg, wparam, lparam);
    }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

//////////////////////////////////////////
// CQueueContainer
//////////////////////////////////////////

CQueueContainer::CQueueContainer()
{
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    SetTabText(d.queue_tab);
    SetDockCaption(d.file_queue_caption);
    SetView(m_listView);
}

CDockQueue::CDockQueue()
{
    SetView(m_container);
}

namespace pmui {

bool PmPostQueueToolCallRow(HWND frame_hwnd, std::wstring col_name, std::wstring col_operation,
                            std::wstring col_status, std::wstring col_path)
{
    if (!frame_hwnd || !::IsWindow(frame_hwnd))
        return false;
    auto* p = new (std::nothrow) QueueToolCallRowW();
    if (!p)
        return false;
    p->col_name      = std::move(col_name);
    p->col_operation = std::move(col_operation);
    p->col_status    = std::move(col_status);
    p->col_path      = std::move(col_path);
    if (!::PostMessageW(frame_hwnd, UWM_QUEUE_TOOL_CALL, reinterpret_cast<WPARAM>(p), 0)) {
        delete p;
        return false;
    }
    return true;
}

} // namespace pmui
