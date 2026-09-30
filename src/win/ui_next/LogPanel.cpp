// Log dock: consumes log lines (list UI + `pm-image.log` via `append_pm_image_log_file_line_wide`).
// Does not call `logger::*` / `ui_log_file_*`. Ingestion: `UWM_LOG_MESSAGE` → `LogMessage` → `AppendLine`.
#include "stdafx.h"
#include <CommCtrl.h>
#include "LogPanel.h"
#include "Resource.h"
#include "constants.hpp"
#include "win/settings_store.hpp"
#include "helpers/dock_chrome_i18n.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/ui_constants.hpp"
#include "helpers/ui_font.hpp"
#include "helpers/theme.hpp"
#include "log_sink.h"
#include "settings_controls.hpp"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <regex>
#include <vector>

namespace {

std::wstring strip_ansi_wide(const std::wstring& s)
{
    std::wstring o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == L'\x1b' && i + 1 < s.size() && s[i + 1] == L'[') {
            i += 2;
            while (i < s.size() && s[i] != L'm' && s[i] != L'K')
                ++i;
            if (i < s.size())
                ++i;
            continue;
        }
        o.push_back(s[i]);
        ++i;
    }
    return o;
}

void trim_inplace_w(std::wstring& s)
{
    while (!s.empty() && iswspace(static_cast<wint_t>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && iswspace(static_cast<wint_t>(s.back())))
        s.pop_back();
}

std::wstring truncate_units(const std::wstring& s, size_t max_units)
{
    if (s.size() <= max_units)
        return s;
    if (max_units <= 2)
        return L"\u2026";
    return s.substr(0, max_units - 1) + L"\u2026";
}

bool is_level_token(const std::wstring& w)
{
    if (w.empty())
        return false;
    std::wstring t = w;
    (void)::CharLowerBuffW(t.data(), static_cast<DWORD>(t.size()));
    return t == L"trace" || t == L"debug" || t == L"info" || t == L"warn" || t == L"warning" || t == L"error"
        || t == L"critical" || t == L"err";
}

std::wstring canonical_level_norm(std::wstring t)
{
    if (t.empty())
        return t;
    (void)::CharLowerBuffW(t.data(), static_cast<DWORD>(t.size()));
    if (t == L"err")
        return L"error";
    return t;
}

std::wstring compact_time_display(const std::wstring& first_bracket)
{
    static const std::wregex rt(L"(\\d{2}:\\d{2}:\\d{2})(?:\\.\\d+)?");
    std::wsmatch rm;
    if (std::regex_search(first_bracket, rm, rt))
        return rm[0].str();
    if (first_bracket.size() > 18)
        return first_bracket.substr(0, 18) + L"\u2026";
    return first_bracket;
}

void init_level_combo(HWND h)
{
    static const wchar_t* items[] = {
        L"(All)", L"trace", L"debug", L"info", L"warn", L"error", L"err", L"critical", L"ui"};
    for (auto* t : items)
        (void)::SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
    (void)::SendMessageW(h, CB_SETCURSEL, 0, 0);
}

} // namespace

static DWORD_PTR log_list_row_lparam(HWND list, int row)
{
    LVITEMW it{};
    it.mask     = LVIF_PARAM;
    it.iItem    = row;
    it.iSubItem = 0;
    if (!::SendMessageW(list, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&it)))
        return static_cast<DWORD_PTR>(-1);
    return static_cast<DWORD_PTR>(it.lParam);
}

static bool log_put_clipboard_unicode(HWND owner, const CStringW& text)
{
    if (!owner || !::IsWindow(owner) || text.IsEmpty())
        return false;
    if (!::OpenClipboard(owner))
        return false;
    (void)::EmptyClipboard();
    const SIZE_T bytes = static_cast<SIZE_T>(text.GetLength() + 1) * sizeof(wchar_t);
    HGLOBAL        h   = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) {
        ::CloseClipboard();
        return false;
    }
    if (void* p = ::GlobalLock(h)) {
        memcpy(p, text.c_str(), bytes);
        ::GlobalUnlock(h);
        if (!::SetClipboardData(CF_UNICODETEXT, h))
            ::GlobalFree(h);
    } else {
        ::GlobalFree(h);
    }
    ::CloseClipboard();
    return true;
}

//////////////////////////////////////////
// CLogView
//////////////////////////////////////////

void CLogView::PreCreate(CREATESTRUCT& cs)
{
    CListView::PreCreate(cs);
    cs.style |= LVS_REPORT | LVS_SHOWSELALWAYS;
}

void CLogView::OnAttach()
{
    CListView::OnAttach();
    SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_INFOTIP);
    setup_columns();
    HWND hHeader = ListView_GetHeader(GetHwnd());
    if (hHeader) {
        LONG_PTR s = ::GetWindowLongPtr(hHeader, GWL_STYLE);
        ::SetWindowLongPtr(hHeader, GWL_STYLE, s | HDS_FLAT);
    }
    RefreshThemeColors();
}

void CLogView::setup_columns()
{
    DeleteAllItems();
    LV_COLUMNW col{};
    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    col.fmt  = LVCFMT_LEFT;
    const auto add = [&](int sub, int w_px, LPCWSTR title) {
        col.iSubItem = sub;
        col.cx       = DpiScaleInt(w_px);
        col.pszText  = const_cast<LPWSTR>(title);
        InsertColumn(sub, col);
    };
    add(0, 76, L"Time");
    add(1, 68, L"Level");
    add(2, 100, L"Source");
    add(3, 200, L"Message");
    add(4, 260, L"Detail");
}

void CLogView::layout_last_column()
{
    if (!IsWindow())
        return;
    RECT r{};
    ::GetClientRect(GetHwnd(), &r);
    const int total = (std::max)(0, (int)(r.right - r.left));
    const int w0    = DpiScaleInt(76);
    const int w1    = DpiScaleInt(68);
    const int w2    = DpiScaleInt(100);
    const int w3    = DpiScaleInt(160);
    const int w4    = (std::max)(DpiScaleInt(120), total - w0 - w1 - w2 - w3 - DpiScaleInt(6));
    ListView_SetColumnWidth(GetHwnd(), 0, w0);
    ListView_SetColumnWidth(GetHwnd(), 1, w1);
    ListView_SetColumnWidth(GetHwnd(), 2, w2);
    ListView_SetColumnWidth(GetHwnd(), 3, w3);
    ListView_SetColumnWidth(GetHwnd(), 4, w4);
}

void CLogView::refresh_rows_after_mutation()
{
    if (!IsWindow())
        return;
    ::RedrawWindow(GetHwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CLogView::parse_into_entry(const CString& text, LogEntry& e)
{
    e.raw.assign(text.GetString().begin(), text.GetString().end());
    std::wstring s = strip_ansi_wide(e.raw);
    trim_inplace_w(s);

    e.time.clear();
    e.level_norm = L"ui";
    e.level_disp = L"\u2014";
    e.source     = L"(ui)";
    e.detail     = s;

    auto apply_body = [&](std::wstring& body) {
        trim_inplace_w(body);
        try {
            static const std::wregex rch(L"^\\[([^\\]]+)\\]\\s*(.*)$", std::regex_constants::optimize);
            std::wsmatch m2;
            if (std::regex_match(body, m2, rch)) {
                e.source = m2[1].str();
                e.detail = m2[2].str();
                trim_inplace_w(e.detail);
            } else {
                e.source = L"log";
                e.detail = std::move(body);
            }
        } catch (...) {
            e.source = L"log";
            e.detail = std::move(body);
        }
    };

    try {
        // e.g. `[2026-05-04 13:47:03.253] [pm-image] [info] [startup] …` (file / multi-sink)
        static const std::wregex re3(
            L"^\\[([^\\]]+)\\]\\s+\\[([^\\]]+)\\]\\s+\\[([^\\]]+)\\]\\s+(.*)$", std::regex_constants::optimize);
        std::wsmatch m;
        if (std::regex_match(s, m, re3) && is_level_token(m[3].str())) {
            e.time       = compact_time_display(m[1].str());
            e.level_disp = m[3].str();
            e.level_norm = canonical_level_norm(m[3].str());
            std::wstring body = m[4].str();
            apply_body(body);
            return;
        }
        // `[stamp] [level] body` — stamp may include date; second bracket is spdlog level
        static const std::wregex re2(L"^\\[([^\\]]+)\\]\\s+\\[([^\\]]+)\\]\\s+(.*)$", std::regex_constants::optimize);
        if (std::regex_match(s, m, re2) && is_level_token(m[2].str())) {
            e.time       = compact_time_display(m[1].str());
            e.level_disp = m[2].str();
            e.level_norm = canonical_level_norm(m[2].str());
            std::wstring body = m[3].str();
            apply_body(body);
            return;
        }
        // Short console pattern `[HH:MM:SS] [level] …`
        static const std::wregex reShort(
            L"^\\[(\\d{2}:\\d{2}:\\d{2})\\]\\s+\\[([^\\]]+)\\]\\s+(.*)$", std::regex_constants::optimize);
        if (std::regex_match(s, m, reShort)) {
            e.time       = m[1].str();
            e.level_disp = m[2].str();
            e.level_norm = canonical_level_norm(m[2].str());
            std::wstring body = m[3].str();
            apply_body(body);
        }
    } catch (...) {
    }
}

COLORREF CLogView::level_text_color(const std::wstring& level_norm) const
{
    const bool dark = pmui::theme_palette().dark;
    if (level_norm == L"error" || level_norm == L"critical" || level_norm == L"err")
        return dark ? RGB(255, 130, 130) : RGB(196, 32, 32);
    if (level_norm == L"warn" || level_norm == L"warning")
        return dark ? RGB(255, 200, 110) : RGB(170, 90, 0);
    if (level_norm == L"debug" || level_norm == L"trace")
        return dark ? RGB(150, 190, 220) : RGB(70, 100, 130);
    if (level_norm == L"ui")
        return dark ? RGB(180, 190, 200) : RGB(90, 95, 105);
    return dark ? RGB(210, 230, 245) : RGB(35, 55, 90); // info default
}

bool CLogView::entry_passes_filter(const LogEntry& e) const
{
    if (!m_hComboLevel || !m_hComboSource)
        return true;

    const int li = static_cast<int>(::SendMessageW(m_hComboLevel, CB_GETCURSEL, 0, 0));
    if (li > 0) {
        wchar_t want[64]{};
        if (::SendMessageW(m_hComboLevel, CB_GETLBTEXT, static_cast<WPARAM>(li), reinterpret_cast<LPARAM>(want))
            != CB_ERR) {
            CString w(want);
            w.MakeLower();
            if (w.Compare(e.level_norm.c_str()) != 0)
                return false;
        }
    }

    const int si = static_cast<int>(::SendMessageW(m_hComboSource, CB_GETCURSEL, 0, 0));
    if (si > 0) {
        wchar_t want[256]{};
        if (::SendMessageW(m_hComboSource, CB_GETLBTEXT, static_cast<WPARAM>(si), reinterpret_cast<LPARAM>(want))
            != CB_ERR) {
            if (_wcsicmp(want, e.source.c_str()) != 0)
                return false;
        }
    }
    return true;
}

void CLogView::rebuild_sources_from_entries()
{
    m_sources.clear();
    for (const auto& e : m_entries) {
        if (!e.source.empty())
            m_sources.insert(e.source);
    }
}

void CLogView::rebuild_source_combo_preserving_sel()
{
    if (!m_hComboSource)
        return;
    wchar_t prev[256]{};
    int     sel = static_cast<int>(::SendMessageW(m_hComboSource, CB_GETCURSEL, 0, 0));
    if (sel > 0
        && ::SendMessageW(m_hComboSource, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(prev))
            == CB_ERR)
        prev[0] = L'\0';

    (void)::SendMessageW(m_hComboSource, CB_RESETCONTENT, 0, 0);
    (void)::SendMessageW(m_hComboSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(All)"));
    for (const auto& s : m_sources)
        (void)::SendMessageW(m_hComboSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));

    if (prev[0]) {
        const int found = static_cast<int>(
            ::SendMessageW(m_hComboSource, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(prev)));
        if (found != CB_ERR)
            (void)::SendMessageW(m_hComboSource, CB_SETCURSEL, static_cast<WPARAM>(found), 0);
        else
            (void)::SendMessageW(m_hComboSource, CB_SETCURSEL, 0, 0);
    } else
        (void)::SendMessageW(m_hComboSource, CB_SETCURSEL, 0, 0);
}

void CLogView::note_source(const std::wstring& src)
{
    if (src.empty())
        return;
    if (m_sources.insert(src).second)
        rebuild_source_combo_preserving_sel();
}

void CLogView::rebuild_list()
{
    if (!IsWindow())
        return;
    ::SendMessageW(GetHwnd(), WM_SETREDRAW, FALSE, 0);
    DeleteAllItems();
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (!entry_passes_filter(m_entries[i]))
            continue;
        const int row = GetItemCount();
        const LogEntry& r = m_entries[i];
        InsertItem(row, r.time.c_str());
        SetItemText(row, 1, r.level_disp.c_str());
        SetItemText(row, 2, r.source.c_str());
        SetItemText(row, 3, r.summary.c_str());
        SetItemText(row, 4, r.detail.c_str());
        SetItemData(row, static_cast<DWORD_PTR>(i));
    }
    layout_last_column();
    ::SendMessageW(GetHwnd(), WM_SETREDRAW, TRUE, 0);
    refresh_rows_after_mutation();
}

void CLogView::trim_if_needed()
{
    if (m_entries.size() < k_max_entries)
        return;
    const size_t drop = m_entries.size() - k_trim_to;
    m_entries.erase(m_entries.begin(), m_entries.begin() + static_cast<std::ptrdiff_t>(drop));
    rebuild_sources_from_entries();
    rebuild_source_combo_preserving_sel();
    rebuild_list();
}

void CLogView::push_entry(LogEntry&& e)
{
    trim_if_needed();
    m_entries.push_back(std::move(e));
    const size_t idx = m_entries.size() - 1;
    note_source(m_entries[idx].source);
    if (!entry_passes_filter(m_entries[idx])) {
        layout_last_column();
        return;
    }
    const int row = GetItemCount();
    const LogEntry& r = m_entries[idx];
    InsertItem(row, r.time.c_str());
    SetItemText(row, 1, r.level_disp.c_str());
    SetItemText(row, 2, r.source.c_str());
    SetItemText(row, 3, r.summary.c_str());
    SetItemText(row, 4, r.detail.c_str());
    SetItemData(row, static_cast<DWORD_PTR>(idx));
    (void)ListView_EnsureVisible(GetHwnd(), row, FALSE);
    layout_last_column();
    refresh_rows_after_mutation();
}

void CLogView::SetFilterCombos(HWND levelCombo, HWND sourceCombo)
{
    m_hComboLevel  = levelCombo;
    m_hComboSource = sourceCombo;
}

void CLogView::OnFilterCombosChanged()
{
    rebuild_list();
}

bool CLogView::TryCustomDrawNotify(LPARAM lparam, LRESULT& out)
{
    if (!IsWindow())
        return false;
    auto* pnm = reinterpret_cast<LPNMHDR>(lparam);
    if (pnm->hwndFrom != GetHwnd() || pnm->code != NM_CUSTOMDRAW)
        return false;

    auto*   lvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lparam);
    const DWORD stg = lvcd->nmcd.dwDrawStage;
    if (stg == CDDS_PREPAINT) {
        out = CDRF_NOTIFYITEMDRAW;
        return true;
    }
    if (stg == CDDS_ITEMPREPAINT) {
        out = CDRF_NOTIFYSUBITEMDRAW;
        return true;
    }
    if (stg == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
        const auto& pal = pmui::theme_palette();
        const bool selected = (lvcd->nmcd.uItemState & CDIS_SELECTED) != 0;
        lvcd->clrTextBk = selected
            ? (pal.dark ? RGB(42, 54, 66) : RGB(220, 235, 252))
            : pal.window_bg;
        const int sub  = lvcd->iSubItem;
        const int item = static_cast<int>(lvcd->nmcd.dwItemSpec);
        if (item < 0) {
            out = CDRF_DODEFAULT;
            return true;
        }
        LVITEMW       lvi{};
        lvi.mask     = LVIF_PARAM;
        lvi.iItem    = item;
        lvi.iSubItem = 0;
        if (!::SendMessageW(GetHwnd(), LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&lvi))) {
            lvcd->clrText = pal.control_fg;
            out           = CDRF_NEWFONT;
            return true;
        }
        const DWORD_PTR d = static_cast<DWORD_PTR>(lvi.lParam);
        if (d >= m_entries.size()) {
            lvcd->clrText = pal.control_fg;
            out           = CDRF_NEWFONT;
            return true;
        }
        const LogEntry& e = m_entries[static_cast<size_t>(d)];
        if (selected)
            lvcd->clrText = pal.dark ? RGB(245, 248, 252) : RGB(20, 32, 46);
        else if (sub == 0)
            lvcd->clrText = pal.dark ? RGB(155, 168, 180) : RGB(95, 100, 110);
        else if (sub == 1)
            lvcd->clrText = level_text_color(e.level_norm);
        else if (sub == 2)
            lvcd->clrText = pal.dark ? RGB(175, 190, 255) : RGB(45, 70, 150);
        else
            lvcd->clrText = pal.control_fg;
        out = CDRF_NEWFONT;
        return true;
    }
    return false;
}

bool CLogView::TryInfoTipNotify(LPARAM lparam, LRESULT& out)
{
    if (!IsWindow())
        return false;
    auto* pnm = reinterpret_cast<LPNMHDR>(lparam);
    if (pnm->hwndFrom != GetHwnd() || pnm->code != LVN_GETINFOTIPW)
        return false;
    auto*          tip = reinterpret_cast<NMLVGETINFOTIPW*>(lparam);
    const DWORD_PTR d  = static_cast<DWORD_PTR>(tip->lParam);
    if (d < m_entries.size()) {
        const std::wstring& text = m_entries[static_cast<size_t>(d)].detail;
        if (!text.empty() && tip->pszText && tip->cchTextMax > 0) {
            const int n = std::min(static_cast<int>(text.size()), tip->cchTextMax - 1);
            wcsncpy_s(tip->pszText, static_cast<size_t>(tip->cchTextMax), text.c_str(), static_cast<size_t>(n));
        }
    }
    out = 0;
    return true;
}

std::wstring CLogView::GetEntryRaw(int listRow) const
{
    const DWORD_PTR d = log_list_row_lparam(GetHwnd(), listRow);
    if (d < m_entries.size())
        return m_entries[static_cast<size_t>(d)].raw;
    return {};
}

void CLogView::AppendLine(const CString& text)
{
    if (!text.IsEmpty()) {
        const std::wstring w(text.GetString().begin(), text.GetString().end());
        pmui::append_pm_image_log_file_line_wide(w);
    } else {
        pmui::append_pm_image_log_file_line_wide(std::wstring{});
    }

    if (!IsWindow())
        return;

    LogEntry e;
    parse_into_entry(text, e);
    e.summary = truncate_units(e.detail, 96);
    push_entry(std::move(e));
}

void CLogView::Clear()
{
    m_entries.clear();
    m_sources.clear();
    if (IsWindow())
        DeleteAllItems();
    if (m_hComboLevel)
        (void)::SendMessageW(m_hComboLevel, CB_SETCURSEL, 0, 0);
    if (m_hComboSource) {
        (void)::SendMessageW(m_hComboSource, CB_RESETCONTENT, 0, 0);
        (void)::SendMessageW(m_hComboSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(All)"));
        (void)::SendMessageW(m_hComboSource, CB_SETCURSEL, 0, 0);
    }
    refresh_rows_after_mutation();
}

void CLogView::CopyAllToClipboard()
{
    if (m_entries.empty())
        return;
    CStringW all;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (i)
            all += L"\r\n";
        all += m_entries[i].raw.c_str();
    }
    (void)log_put_clipboard_unicode(GetHwnd(), all);
}

void CLogView::copy_selected_rows_to_clipboard()
{
    if (!IsWindow())
        return;
    CStringW block;
    const HWND h = GetHwnd();
    for (int row = 0, n = GetItemCount(); row < n; ++row) {
        if ((ListView_GetItemState(h, row, LVIS_SELECTED) & LVIS_SELECTED) == 0)
            continue;
        const DWORD_PTR d = log_list_row_lparam(h, row);
        if (d >= m_entries.size())
            continue;
        if (!block.IsEmpty())
            block += L"\r\n";
        block += m_entries[static_cast<size_t>(d)].raw.c_str();
    }
    if (block.IsEmpty()) {
        const int focus = ListView_GetNextItem(h, -1, LVNI_FOCUSED);
        if (focus >= 0) {
            const DWORD_PTR d = log_list_row_lparam(h, focus);
            if (d < m_entries.size())
                block = m_entries[static_cast<size_t>(d)].raw.c_str();
        }
    }
    if (!block.IsEmpty())
        (void)log_put_clipboard_unicode(h, block);
}

void CLogView::RefreshThemeColors()
{
    if (!IsWindow())
        return;
    const auto& pal = pmui::theme_palette();
    // Use window_bg instead of control_bg for seamless edge-to-edge look
    ListView_SetBkColor(GetHwnd(), pal.window_bg);
    ListView_SetTextBkColor(GetHwnd(), pal.window_bg);
    ListView_SetTextColor(GetHwnd(), pal.control_fg);
    pmui::theme_listview_report_header(GetHwnd());
    // Theme scrollbar for dark mode
    if (pal.dark)
        ::SetWindowTheme(GetHwnd(), L"DarkMode_Explorer", nullptr);
    else
        ::SetWindowTheme(GetHwnd(), L"Explorer", nullptr);
    // Force aggressive redraw to ensure background color takes effect
    ::RedrawWindow(GetHwnd(), nullptr, nullptr,
        RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

LRESULT CLogView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {
        case WM_KEYDOWN:
            if (wparam == 'C' && (::GetKeyState(VK_CONTROL) & 0x8000)) {
                copy_selected_rows_to_clipboard();
                return 0;
            }
            break;
        case WM_SIZE:
            layout_last_column();
            break;
        case WM_NOTIFY: {
            LRESULT cd = 0;
            if (pmui::theme_header_customdraw_notify(GetHwnd(), lparam, cd))
                return cd;
            break;
        }
        }
        return CListView::WndProc(msg, wparam, lparam);
    }
    catch (const CException& e) {
        CString s;
        s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

//////////////////////////////////////////
// CLogContentHost
//////////////////////////////////////////

void CLogContentHost::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);
    // Flat host — same issue as `CChatWebView` / `CChatWebContainer`: sunken
    // `WS_EX_CLIENTEDGE` survives dark theme and reads as a bright box around the filter row.
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    cs.style &= ~(WS_BORDER | WS_DLGFRAME);
    // `ChatProviderDlg`: clip siblings reduces combo/toolbar/list paint bleed that looks like frames.
    cs.style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
}

int CLogContentHost::filter_row_height() const
{
    return DpiScaleInt(pmui::ui::SettingsPaneLayout::control_h + 10);
}

int CLogContentHost::list_top() const
{
    return m_strip.IdealHeight() + filter_row_height();
}

void CLogContentHost::update_detail_pane()
{
    if (!m_hDetail)
        return;
    const HWND lv = m_log.GetHwnd();
    if (!lv) {
        ::SetWindowTextW(m_hDetail, L"");
        return;
    }
    std::wstring text;
    int row = -1;
    while ((row = ListView_GetNextItem(lv, row, LVNI_SELECTED)) >= 0) {
        if (!text.empty())
            text += L"\r\n";
        text += m_log.GetEntryRaw(row);
    }
    ::SetWindowTextW(m_hDetail, text.c_str());
    if (!text.empty()) {
        ::SendMessageW(m_hDetail, EM_SETSEL, 0, 0);
        ::SendMessageW(m_hDetail, EM_SCROLLCARET, 0, 0);
    }
}

void CLogContentHost::ClearLog()
{
    m_log.Clear();
    if (m_hDetail)
        ::SetWindowTextW(m_hDetail, L"");
    invalidate_panel_tree(false);
}

void CLogContentHost::invalidate_panel_tree(bool erase)
{
    if (!IsWindow())
        return;
    const UINT flags = RDW_INVALIDATE | RDW_ALLCHILDREN | (erase ? RDW_ERASE : 0);
    ::RedrawWindow(GetHwnd(), nullptr, nullptr, flags);
}

void CLogContentHost::layout_children(int client_w, int client_h)
{
    const int sh  = m_strip.IdealHeight();
    const int frh = filter_row_height();
    m_strip.SetWindowPos(nullptr, 0, 0, client_w, sh, SWP_NOZORDER);

    const int x0  = DpiScaleInt(8);
    const int y0  = sh + DpiScaleInt(4);
    const int cy  = DpiScaleInt(pmui::ui::SettingsPaneLayout::control_h);
    const int lblw = DpiScaleInt(44);
    int       x   = x0;
    if (m_hLblLevel)
        ::SetWindowPos(m_hLblLevel, nullptr, x, y0 + DpiScaleInt(4), lblw, cy, SWP_NOZORDER);
    x += lblw + DpiScaleInt(4);
    const int cmbw_lvl = DpiScaleInt(118);
    if (m_hComboLevel)
        ::SetWindowPos(m_hComboLevel, nullptr, x, y0, cmbw_lvl, cy + DpiScaleInt(200), SWP_NOZORDER);
    x += cmbw_lvl + DpiScaleInt(14);
    const int lblw2 = DpiScaleInt(52);
    if (m_hLblSource)
        ::SetWindowPos(m_hLblSource, nullptr, x, y0 + DpiScaleInt(4), lblw2, cy, SWP_NOZORDER);
    x += lblw2 + DpiScaleInt(4);
    const int cmbw_src = (std::max)(DpiScaleInt(140), client_w - x - x0);
    if (m_hComboSource)
        ::SetWindowPos(m_hComboSource, nullptr, x, y0, cmbw_src, cy + DpiScaleInt(220), SWP_NOZORDER);

    const int lt      = sh + frh;
    const int split_h = DpiScaleInt(k_split_h);
    const int det_h   = m_hDetail
                            ? (std::min)(DpiScaleInt(k_detail_max_h),
                                         (std::max)(DpiScaleInt(k_detail_min_h), m_detailPaneH))
                            : 0;
    const int list_h  = (std::max)(0, client_h - lt - (m_hDetail ? split_h + det_h : 0));

    if (m_log.IsWindow())
        m_log.SetWindowPos(nullptr, 0, lt, client_w, list_h, SWP_NOZORDER);

    if (m_hDetail) {
        const int detail_y = lt + list_h + split_h;
        ::SetWindowPos(m_hDetail, nullptr, 0, detail_y, client_w,
                       (std::max)(0, client_h - detail_y), SWP_NOZORDER);
    }
}

int CLogContentHost::OnCreate(CREATESTRUCT&)
{
    if (!m_strip.Create(*this))
        return -1;
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    HINSTANCE   inst = ::GetModuleHandleW(nullptr);
    const std::vector<pmui::DockPanelToolBtn> btns = {
        {IDC_LOG_PANEL_COPY, IDC_LOG_PANEL_COPY_SmallImages_RESID, 0, d.log_toolbar_copy_tip, false},
        {IDC_LOG_PANEL_CLEAR, IDC_CMD_CLEAR_SmallImages_RESID, 0, d.log_toolbar_clear_tip, false},
    };
    if (!m_strip.ApplyButtons(inst, *this, btns))
        return -1;

    m_hLblLevel = ::CreateWindowExW(0, L"STATIC", L"Level", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, GetHwnd(), nullptr,
        inst, nullptr);
    m_hLblSource = ::CreateWindowExW(0, L"STATIC", L"Source", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, GetHwnd(), nullptr,
        inst, nullptr);

    pmui::settings_controls::PresetComboParams pc{};
    pc.parent     = GetHwnd();
    pc.inst       = inst;
    pc.id         = IDC_LOG_FILTER_LEVEL;
    pc.x          = 0;
    pc.y          = 0;
    pc.combo_w    = DpiScaleInt(118);
    pc.drop_height = DpiScaleInt(200);
    m_hComboLevel = pmui::settings_controls::create_preset_combo(pc);
    pc.id         = IDC_LOG_FILTER_SOURCE;
    pc.combo_w    = DpiScaleInt(160);
    pc.drop_height = DpiScaleInt(220);
    m_hComboSource = pmui::settings_controls::create_preset_combo(pc);
    pmui::polish_flat_combo_chrome(m_hComboLevel);
    pmui::polish_flat_combo_chrome(m_hComboSource);

    init_level_combo(m_hComboLevel);
    (void)::SendMessageW(m_hComboSource, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(All)"));
    (void)::SendMessageW(m_hComboSource, CB_SETCURSEL, 0, 0);

    HFONT hFont = pmui::ui_font();
    if (m_hLblLevel)
        ::SendMessageW(m_hLblLevel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    if (m_hLblSource)
        ::SendMessageW(m_hLblSource, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    if (m_hComboLevel)
        ::SendMessageW(m_hComboLevel, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    if (m_hComboSource)
        ::SendMessageW(m_hComboSource, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    if (!m_log.Create(*this))
        return -1;
    m_log.SetFilterCombos(m_hComboLevel, m_hComboSource);

    m_detailPaneH = DpiScaleInt(k_detail_def_h);
    m_hDetail = ::CreateWindowExW(
        0, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 0, 0, GetHwnd(),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG_DETAIL_EDIT)), inst, nullptr);
    if (m_hDetail)
        ::SendMessageW(m_hDetail, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

    CRect rc = GetClientRect();
    layout_children(rc.Width(), rc.Height());
    RefreshLogPanelChrome();
    return 0;
}

void CLogContentHost::RefreshLogPanelChrome()
{
    const auto& pal = pmui::theme_palette();
    // `apply_window_theme_recursive` only walks children — strip this HWND + TabPage / tab parents
    // (see `flatten_webview_host_parent_chain` + `ChatWebPanel.cpp` PreCreate).
    pmui::flatten_win32_host_frame(GetHwnd(), pal.window_bg);
    pmui::flatten_webview_host_parent_chain(GetHwnd(), pal.window_bg);
    m_strip.ApplyTheme(pal.dark, pal.window_bg);
    m_log.RefreshThemeColors();
    if (m_hDetail) {
        if (pal.dark)
            ::SetWindowTheme(m_hDetail, L"DarkMode_Explorer", nullptr);
        else
            ::SetWindowTheme(m_hDetail, nullptr, nullptr);
        ::InvalidateRect(m_hDetail, nullptr, TRUE);
    }
    pmui::apply_window_theme_recursive(GetHwnd(), pal.dark);
    if (m_hComboLevel)
        pmui::polish_flat_combo_chrome(m_hComboLevel);
    if (m_hComboSource)
        pmui::polish_flat_combo_chrome(m_hComboSource);
    // Match `ChatProviderDlg` `chat_dlg_enable_clip_siblings`: sibling clip on descendants
    // so themed combo/list edges do not ghost over neighbours after resize/theme.
    (void)::EnumChildWindows(
        GetHwnd(),
        [](HWND h, LPARAM) -> BOOL {
            if (!h || !::IsWindow(h))
                return TRUE;
            const LONG_PTR st = ::GetWindowLongPtrW(h, GWL_STYLE);
            (void)::SetWindowLongPtrW(h, GWL_STYLE, st | WS_CLIPSIBLINGS);
            return TRUE;
        },
        0);
}

LRESULT CLogContentHost::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {
        case WM_ERASEBKGND: {
            // Default Win32++ class uses `WHITE_BRUSH`; without this, gaps around the
            // filter row read as a bright frame in dark mode (same fix as `CChatWebView`).
            HDC hdc = reinterpret_cast<HDC>(wparam);
            if (hdc) {
                RECT rc{};
                ::GetClientRect(GetHwnd(), &rc);
                const auto& pal = pmui::theme_palette();
                HBRUSH br = ::CreateSolidBrush(pal.window_bg);
                if (br) {
                    ::FillRect(hdc, &rc, br);
                    ::DeleteObject(br);
                }
            }
            return 1;
        }
        case WM_SIZE:
            if (m_strip.IsWindow() && m_log.IsWindow()) {
                layout_children(LOWORD(lparam), HIWORD(lparam));
                invalidate_panel_tree(false);
                return 0;
            }
            break;
        case WM_SHOWWINDOW:
        case WM_WINDOWPOSCHANGED:
            invalidate_panel_tree(false);
            break;
        case WM_NOTIFY: {
            LRESULT lr = 0;
            if (m_log.TryCustomDrawNotify(lparam, lr))
                return lr;
            if (m_log.TryInfoTipNotify(lparam, lr))
                return lr;
            LRESULT cd = 0;
            if (pmui::theme_header_customdraw_notify(m_log.GetHwnd(), lparam, cd))
                return cd;
            {
                auto* pnm = reinterpret_cast<LPNMHDR>(lparam);
                if (pnm->hwndFrom == m_log.GetHwnd() && pnm->code == LVN_ITEMCHANGED) {
                    auto* plv = reinterpret_cast<LPNMLISTVIEW>(lparam);
                    if ((plv->uChanged & LVIF_STATE)
                        && ((plv->uNewState ^ plv->uOldState) & LVIS_SELECTED))
                        update_detail_pane();
                }
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HWND h = reinterpret_cast<HWND>(lparam);
            if (h == m_hLblLevel || h == m_hLblSource || h == m_hDetail) {
                HDC           hdc = reinterpret_cast<HDC>(wparam);
                const auto&   pal = pmui::theme_palette();
                static HBRUSH   s_brush = nullptr;
                static COLORREF s_col   = 0xFFFFFFFF;
                (void)::SetBkMode(hdc, TRANSPARENT);
                (void)::SetTextColor(hdc, pal.control_fg);
                if (!s_brush || s_col != pal.window_bg) {
                    if (s_brush)
                        ::DeleteObject(s_brush);
                    s_brush = ::CreateSolidBrush(pal.window_bg);
                    s_col   = pal.window_bg;
                }
                return reinterpret_cast<LRESULT>(s_brush);
            }
            break;
        }
        case WM_SETCURSOR: {
            if (m_hDetail) {
                POINT pt{};
                ::GetCursorPos(&pt);
                ::ScreenToClient(GetHwnd(), &pt);
                RECT rc{};
                ::GetClientRect(GetHwnd(), &rc);
                const int lt      = list_top();
                const int split_h = DpiScaleInt(k_split_h);
                const int det_h   = (std::min)(DpiScaleInt(k_detail_max_h),
                                               (std::max)(DpiScaleInt(k_detail_min_h), m_detailPaneH));
                const int avail   = static_cast<int>(rc.bottom) - lt - split_h - det_h;
                const int list_h  = avail > 0 ? avail : 0;
                const int split_y = lt + list_h;
                if (pt.y >= split_y && pt.y < split_y + split_h) {
                    ::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (m_hDetail) {
                const int pt_y    = static_cast<short>(HIWORD(lparam));
                RECT      rc{};
                ::GetClientRect(GetHwnd(), &rc);
                const int lt      = list_top();
                const int split_h = DpiScaleInt(k_split_h);
                const int det_h   = (std::min)(DpiScaleInt(k_detail_max_h),
                                               (std::max)(DpiScaleInt(k_detail_min_h), m_detailPaneH));
                const int avail   = static_cast<int>(rc.bottom) - lt - split_h - det_h;
                const int list_h  = avail > 0 ? avail : 0;
                const int split_y = lt + list_h;
                if (pt_y >= split_y && pt_y < split_y + split_h) {
                    m_draggingSplitter = true;
                    m_dragStartY       = pt_y;
                    m_dragStartH       = m_detailPaneH;
                    ::SetCapture(GetHwnd());
                    ::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
                    return 0;
                }
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (m_draggingSplitter) {
                const int pt_y   = static_cast<short>(HIWORD(lparam));
                const int delta  = m_dragStartY - pt_y; // drag up → bigger pane
                const int newH   = (std::min)(DpiScaleInt(k_detail_max_h),
                                              (std::max)(DpiScaleInt(k_detail_min_h),
                                                         m_dragStartH + delta));
                if (newH != m_detailPaneH) {
                    m_detailPaneH = newH;
                    RECT rc{};
                    ::GetClientRect(GetHwnd(), &rc);
                    layout_children(rc.right, rc.bottom);
                }
                ::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
                return 0;
            }
            break;
        }
        case WM_LBUTTONUP: {
            if (m_draggingSplitter) {
                m_draggingSplitter = false;
                ::ReleaseCapture();
                return 0;
            }
            break;
        }
        case WM_COMMAND: {
            const int id   = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (code == CBN_SELCHANGE && (id == IDC_LOG_FILTER_LEVEL || id == IDC_LOG_FILTER_SOURCE)) {
                m_log.OnFilterCombosChanged();
                return 0;
            }
            break;
        }
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
// CLogContainer
//////////////////////////////////////////

void CLogContainer::PreCreate(CREATESTRUCT& cs)
{
    CDockContainer::PreCreate(cs);
    cs.dwExStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    cs.style &= ~(WS_BORDER | WS_DLGFRAME);
}

CLogContainer::CLogContainer()
{
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    SetTabText(d.log_tab);
    SetDockCaption(d.log_caption);
    SetView(m_content);
}

void CLogContainer::RefreshTabTheme()
{
    const auto& pal = pmui::theme_palette();
    SetBlankPageColor(pal.window_bg);  // Match panel background
    CDockContainerBase::RefreshTabTheme();
    m_content.RefreshLogPanelChrome();
}

void CLogContainer::ClearLog()
{
    m_content.ClearLog();
}

CDockLog::CDockLog()
{
    SetView(m_container);
}
