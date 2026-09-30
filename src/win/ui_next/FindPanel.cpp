#include "stdafx.h"
#include "FindPanel.h"
#include "Resource.h"
#include "win/settings_store.hpp"
#include "helpers/default_shell.hpp"
#include "helpers/dock_chrome_i18n.hpp"
#include "helpers/theme.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// CMainFrame receives UWM_*; must be GA_ROOT, not Win32++'s default GetAncestor (GA_ROOTOWNER).
static HWND FindFrameHwnd(HWND listHwnd)
{
    return (listHwnd) ? ::GetAncestor(listHwnd, GA_ROOT) : nullptr;
}

// ── Context menu IDs (panel-local, never collide with global commands) ────────
static constexpr UINT CTX_REVEAL_EXPLORER = 1;   // navigate the in-app Explorer panel
static constexpr UINT CTX_REVEAL_SHELL    = 2;   // open native Explorer with the file selected
static constexpr UINT CTX_OPEN            = 3;   // ShellExecute "open"
static constexpr UINT CTX_COPY_PATH       = 4;
static constexpr UINT CTX_REMOVE          = 5;

//////////////////////////////////////////
// CFindResultsView
//////////////////////////////////////////

void CFindResultsView::OnAttach()
{
    CListView::OnAttach();
    SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    SetupColumns();

    HWND hHeader = ListView_GetHeader(GetHwnd());
    if (hHeader) {
        LONG_PTR s = ::GetWindowLongPtr(hHeader, GWL_STYLE);
        ::SetWindowLongPtr(hHeader, GWL_STYLE, s | HDS_FLAT);
    }

    // Apply current theme colours to the listview rows.
    const auto& pal = pmui::theme_palette();
    ListView_SetBkColor   (GetHwnd(), pal.control_bg);
    ListView_SetTextBkColor(GetHwnd(), pal.control_bg);
    ListView_SetTextColor (GetHwnd(), pal.control_fg);
    pmui::theme_listview_report_header(GetHwnd());
}

void CFindResultsView::PreCreate(CREATESTRUCT& cs)
{
    CListView::PreCreate(cs);
    cs.style |= LVS_REPORT | LVS_SHOWSELALWAYS;
}

void CFindResultsView::SetupColumns()
{
    DeleteAllItems();

    LV_COLUMN col{};
    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    col.fmt  = LVCFMT_LEFT;

    auto add = [&](FindCol c, LPCWSTR text, int w) {
        col.pszText  = const_cast<LPWSTR>(text);
        col.cx       = DpiScaleInt(w);
        col.iSubItem = c;
        InsertColumn(c, col);
    };
    add(FIND_COL_NAME,   L"Name",   220);
    add(FIND_COL_SCORE,  L"Score",   60);
    add(FIND_COL_SOURCE, L"Source",  70);
    add(FIND_COL_REASON, L"Reason", 320);
    add(FIND_COL_PATH,   L"Path",   500);
}

int CFindResultsView::AddResult(LPCWSTR path, double score,
                                 LPCWSTR source, LPCWSTR reason)
{
    fs::path p(path ? path : L"");
    CString name = p.filename().c_str();

    int idx = GetItemCount();
    int item = InsertItem(idx, name);

    wchar_t scoreBuf[32];
    swprintf_s(scoreBuf, L"%.2f", score);
    SetItemText(item, FIND_COL_SCORE,  scoreBuf);
    SetItemText(item, FIND_COL_SOURCE, source ? source : L"");
    SetItemText(item, FIND_COL_REASON, reason ? reason : L"");
    SetItemText(item, FIND_COL_PATH,   path   ? path   : L"");
    return item;
}

void CFindResultsView::ClearAll() { DeleteAllItems(); }

void CFindResultsView::RemoveSelectedItems()
{
    HWND hList = GetHwnd();
    std::vector<int> sel;
    for (int i = -1;;) {
        i = ListView_GetNextItem(hList, i, LVNI_SELECTED);
        if (i < 0) break;
        sel.push_back(i);
    }
    if (sel.empty()) return;
    std::sort(sel.begin(), sel.end(), std::greater<int>());
    for (int idx : sel) ListView_DeleteItem(hList, idx);
}

void CFindResultsView::RefreshThemeColors()
{
    if (!IsWindow()) return;
    const auto& pal = pmui::theme_palette();
    ListView_SetBkColor   (GetHwnd(), pal.control_bg);
    ListView_SetTextBkColor(GetHwnd(), pal.control_bg);
    ListView_SetTextColor (GetHwnd(), pal.control_fg);
    pmui::theme_listview_report_header(GetHwnd());
    ::InvalidateRect(GetHwnd(), nullptr, TRUE);
}

CString CFindResultsView::GetItemPath(int item) const
{
    if (item < 0 || item >= GetItemCount()) return CString();
    wchar_t buf[MAX_PATH * 4]{};
    LVITEMW lvi{};
    lvi.iSubItem   = FIND_COL_PATH;
    lvi.pszText    = buf;
    lvi.cchTextMax = MAX_PATH * 4;
    ::SendMessageW(GetHwnd(), LVM_GETITEMTEXTW, (WPARAM)item, (LPARAM)&lvi);
    return CString(buf);
}

void CFindResultsView::ShowContextMenuFor(int item, POINT screenPt)
{
    HMENU h = ::CreatePopupMenu();
    if (!h) return;
    const bool valid = (item >= 0);
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED),
                  CTX_REVEAL_EXPLORER, L"Reveal in &Explorer panel");
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED),
                  CTX_REVEAL_SHELL,    L"Show in Windows &File Explorer");
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED),
                  CTX_OPEN,            L"&Open");
    ::AppendMenuW(h, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED),
                  CTX_COPY_PATH,       L"&Copy path");
    ::AppendMenuW(h, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED),
                  CTX_REMOVE,          L"&Remove from results\tDel");

    UINT cmd = ::TrackPopupMenu(h, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                screenPt.x, screenPt.y, 0, GetHwnd(), nullptr);
    ::DestroyMenu(h);
    if (cmd == 0) return;

    CString path  = GetItemPath(item);
    const HWND frame = FindFrameHwnd(GetHwnd());

    switch (cmd) {
    case CTX_REVEAL_EXPLORER:
        if (!path.IsEmpty() && frame) {
            // Frame turns this into a navigation in the Explorer dock.
            auto* heap = new std::wstring(path.c_str());
            ::PostMessageW(frame, UWM_FIND_REVEAL_IN_EXPLORER, (WPARAM)heap, 0);
        }
        break;
    case CTX_REVEAL_SHELL:
        if (!path.IsEmpty()) {
            pmui::shell::reveal_in_explorer(nullptr, path.GetString());
        }
        break;
    case CTX_OPEN:
        if (!path.IsEmpty())
            pmui::shell::open_path(nullptr, path.GetString());
        break;
    case CTX_COPY_PATH:
        if (!path.IsEmpty() && frame) {
            if (::OpenClipboard(frame)) {
            ::EmptyClipboard();
            const SIZE_T bytes = (path.GetLength() + 1) * sizeof(wchar_t);
            HGLOBAL h2 = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (h2) {
                if (auto* dst = static_cast<wchar_t*>(::GlobalLock(h2))) {
                    memcpy(dst, path.c_str(), bytes);
                    ::GlobalUnlock(h2);
                    ::SetClipboardData(CF_UNICODETEXT, h2);
                }
            }
            ::CloseClipboard();
            }
        }
        break;
    case CTX_REMOVE:
        if (frame) ::PostMessageW(frame, UWM_FIND_DELETE_SELECTION, 0, 0);
        break;
    }
}

LRESULT CFindResultsView::OnNotifyReflect(WPARAM, LPARAM lparam)
{
    LPNMHDR pnm = reinterpret_cast<LPNMHDR>(lparam);
    if (pnm->code == NM_CLICK) {
        auto* pia = reinterpret_cast<LPNMITEMACTIVATE>(lparam);
        if (pia->iItem >= 0) {
            if (const HWND frame = FindFrameHwnd(GetHwnd()); frame)
                ::SendMessageW(frame, UWM_FIND_ITEM_CLICKED, (WPARAM)pia->iItem, 0);
        }
    } else if (pnm->code == LVN_ITEMCHANGED) {
        auto* plv = reinterpret_cast<LPNMLISTVIEW>(lparam);
        if (plv->iItem >= 0 && (plv->uChanged & LVIF_STATE) &&
            (plv->uNewState & LVIS_SELECTED) && !(plv->uOldState & LVIS_SELECTED)) {
            if (const HWND frame = FindFrameHwnd(GetHwnd()); frame)
                ::SendMessageW(frame, UWM_FIND_ITEM_CLICKED, (WPARAM)plv->iItem, 0);
        }
    } else if (pnm->code == NM_RCLICK) {
        auto* pia = reinterpret_cast<LPNMITEMACTIVATE>(lparam);
        POINT screenPt = pia->ptAction;
        ::ClientToScreen(GetHwnd(), &screenPt);
        ShowContextMenuFor(pia->iItem, screenPt);
    } else if (pnm->code == NM_DBLCLK) {
        auto* pia = reinterpret_cast<LPNMITEMACTIVATE>(lparam);
        if (pia->iItem >= 0) {
            CString path = GetItemPath(pia->iItem);
            if (!path.IsEmpty())
                pmui::shell::open_path(nullptr, path.GetString());
        }
    }
    return 0;
}

LRESULT CFindResultsView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {
        case WM_KEYDOWN:
            if (wparam == VK_DELETE) {
                if (const HWND frame = FindFrameHwnd(GetHwnd()); frame)
                    ::SendMessageW(frame, UWM_FIND_DELETE_SELECTION, 0, 0);
                return 0;
            }
            break;
        case WM_CONTEXTMENU: {
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            // Keyboard menu key delivers (-1,-1) — anchor on the focused item.
            int item = -1;
            if (pt.x == -1 && pt.y == -1) {
                item = ListView_GetNextItem(GetHwnd(), -1, LVNI_FOCUSED);
                RECT r{};
                if (item >= 0 && ListView_GetItemRect(GetHwnd(), item, &r, LVIR_BOUNDS)) {
                    pt.x = r.left; pt.y = r.bottom;
                    ::ClientToScreen(GetHwnd(), &pt);
                } else {
                    ::GetWindowRect(GetHwnd(), &r);
                    pt.x = r.left + 20; pt.y = r.top + 20;
                }
            } else {
                LVHITTESTINFO hti{};
                hti.pt = pt; ::ScreenToClient(GetHwnd(), &hti.pt);
                item = ListView_HitTest(GetHwnd(), &hti);
            }
            ShowContextMenuFor(item, pt);
            return 0;
        }
        case WM_NOTIFY: {
            LRESULT cd = 0;
            if (pmui::theme_header_customdraw_notify(GetHwnd(), lparam, cd))
                return cd;
            break;
        }
        }
        return WndProcDefault(msg, wparam, lparam);
    } catch (const CException& e) {
        CString s; s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

//////////////////////////////////////////
// CFindResultsContainer / CDockFindResults
//////////////////////////////////////////

CFindResultsContainer::CFindResultsContainer()
{
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    SetTabText(d.find_tab);
    SetDockCaption(d.find_results_caption);
    SetView(m_view);
}

CDockFindResults::CDockFindResults()
{
    SetView(m_container);
}
