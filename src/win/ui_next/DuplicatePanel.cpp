#include "stdafx.h"
#include <commctrl.h> // TVN_SELCHANGED, NM_CLICK
#include "DuplicatePanel.h"
#include "Resource.h"
#include "win/settings_store.hpp"
#include "helpers/default_shell.hpp"
#include "helpers/dock_chrome_i18n.hpp"
#include "helpers/theme.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

/// Win32++'s CWnd::GetAncestor() uses GA_ROOTOWNER, which is not always the
/// CMainFrame that owns UWM_* custom messages. The frame is GA_ROOT of this view.
static HWND DupFrameHwnd(HWND viewHwnd)
{
    return (viewHwnd) ? ::GetAncestor(viewHwnd, GA_ROOT) : nullptr;
}

static constexpr UINT CTX_REVEAL_EXPLORER = 1;
static constexpr UINT CTX_REVEAL_SHELL    = 2;
static constexpr UINT CTX_OPEN         = 3;
static constexpr UINT CTX_COPY_PATH     = 4;
static constexpr UINT CTX_COPY_ALL     = 5;
static constexpr UINT CTX_SAVE_SESSION  = 6;
static constexpr UINT CTX_OPEN_SESSION  = 7;
static constexpr UINT CTX_REMOVE       = 8;

static constexpr UINT_PTR kDupChildFlag = 0x8000'0000u;

// TVM_* (avoid some SDK macro/header orders)
static constexpr UINT kTvmSetBkColor   = 0x1100 + 29;
static constexpr UINT kTvmSetTextColor = 0x1100 + 30;

static UINT_PTR encode_parent_data(int groupIndex)
{
    return static_cast<UINT_PTR>(1 + groupIndex);
}

static UINT_PTR encode_child_data(int groupIndex, int pathIndex)
{
    return kDupChildFlag
        | (static_cast<UINT_PTR>(groupIndex & 0xFFFF) << 16)
        | static_cast<UINT_PTR>(pathIndex & 0xFFFF);
}

} // namespace

// ── CDuplicateResultsView ───────────────────────────────────────────────────

void CDuplicateResultsView::OnAttach()
{
    CTreeView::OnAttach();
    ::SendMessageW(GetHwnd(), TVM_SETINDENT, 14, 0);
}

void CDuplicateResultsView::PreCreate(CREATESTRUCT& cs)
{
    CTreeView::PreCreate(cs);
    cs.style |= TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS;
}

int CDuplicateResultsView::GroupIndexForItemData(UINT_PTR d) const
{
    if (d == 0) return -1;
    if (d & kDupChildFlag) return (int)((d >> 16) & 0xFFFF);
    return (int)d - 1;
}

void CDuplicateResultsView::OnSelChanged(HTREEITEM h)
{
    if (!h) return;
    const UINT_PTR data = (UINT_PTR)GetItemData(h);
    if (const HWND frame = DupFrameHwnd(GetHwnd()); data && frame)
        ::SendMessageW(frame, UWM_DUPLICATES_ITEM_CLICKED, (WPARAM)data, 0);
}

void CDuplicateResultsView::ClearAll()
{
    m_rows.clear();
    DeleteAllItems();
}

void CDuplicateResultsView::SetRows(const std::vector<DupListRow>& rows)
{
    m_rows = rows;
    DeleteAllItems();
    for (int g = 0; g < (int)m_rows.size(); ++g) {
        const auto& r = m_rows[static_cast<size_t>(g)];
        wchar_t     line[600]{};
        const auto* m = r.method.c_str();
        const auto* k = r.key.c_str();
        if (r.count <= 0) {
            swprintf_s(line, L"%s  |  %s", m, k);
        } else {
            swprintf_s(line, L"%s  |  %s  —  %d files", m, k, r.count);
        }
        const HTREEITEM parent = InsertItem(
            TVIF_TEXT | TVIF_PARAM, line, 0, 0, 0, 0, (LPARAM)encode_parent_data(g), TVI_ROOT, TVI_LAST);
        for (int pi = 0; pi < (int)r.paths.size(); ++pi) {
            const auto&  pth = r.paths[static_cast<size_t>(pi)];
            fs::path     fp(pth);
            std::wstring leaf = fp.filename().wstring();
            if (leaf.empty()) leaf = pth;
            std::wstring line = leaf;
            if (pi < (int)r.pathSubtext.size() && !r.pathSubtext[static_cast<size_t>(pi)].empty()) {
                line += L"  ·  ";
                line += r.pathSubtext[static_cast<size_t>(pi)];
            }
            (void)InsertItem(
                TVIF_TEXT | TVIF_PARAM, line.c_str(), 0, 0, 0, 0,
                (LPARAM)encode_child_data(g, pi), parent, TVI_LAST);
        }
        (void)Expand(parent, TVE_EXPAND);
    }
    if (m_rows.empty()) return;
    if (HTREEITEM c = GetChild(TVI_ROOT)) (void)SelectItem(c);
}

std::wstring CDuplicateResultsView::GetPathForItemData(UINT_PTR d) const
{
    if (d == 0) return {};
    int g;
    if (d & kDupChildFlag) {
        g  = (int)((d >> 16) & 0xFFFF);
        const int pi = (int)(d & 0xFFFF);
        if (g < 0 || g >= (int)m_rows.size()) return {};
        const auto& r = m_rows[static_cast<size_t>(g)];
        if (pi >= 0 && pi < (int)r.paths.size()) return r.paths[static_cast<size_t>(pi)];
        return {};
    }
    g = (int)d - 1;
    if (g < 0 || g >= (int)m_rows.size()) return {};
    const auto& r = m_rows[static_cast<size_t>(g)];
    if (!r.samplePath.empty()) return r.samplePath;
    if (!r.paths.empty()) return r.paths[0];
    return {};
}

const std::vector<std::wstring>* CDuplicateResultsView::GetPathsForItemData(UINT_PTR d) const
{
    const int g = GroupIndexForItemData(d);
    if (g < 0 || g >= (int)m_rows.size()) return nullptr;
    return &m_rows[static_cast<size_t>(g)].paths;
}

const DupListRow* CDuplicateResultsView::GetRowForItemData(UINT_PTR d) const
{
    const int g = GroupIndexForItemData(d);
    if (g < 0 || g >= (int)m_rows.size()) return nullptr;
    return &m_rows[static_cast<size_t>(g)];
}

void CDuplicateResultsView::RemoveSelectedItems()
{
    const HTREEITEM h = GetSelection();
    if (!h) return;
    const int g = GroupIndexForItemData((UINT_PTR)GetItemData(h));
    if (g < 0 || g >= (int)m_rows.size()) return;
    m_rows.erase(m_rows.begin() + g);
    const std::vector<DupListRow> next = m_rows;
    SetRows(next);
}

void CDuplicateResultsView::RefreshThemeColors()
{
    if (!IsWindow()) return;
    const auto& pal = pmui::theme_palette();
    ::SendMessageW(GetHwnd(), kTvmSetBkColor, 0, (LPARAM)pal.control_bg);
    ::SendMessageW(GetHwnd(), kTvmSetTextColor, 0, (LPARAM)pal.control_fg);
    ::InvalidateRect(GetHwnd(), nullptr, TRUE);
}

void CDuplicateResultsView::ShowContextMenuFor(HTREEITEM hit, POINT screenPt)
{
    HMENU h = ::CreatePopupMenu();
    if (!h) return;
    const UINT_PTR  data  = hit ? (UINT_PTR)GetItemData(hit) : 0;
    const bool valid = (data != 0);

    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED), CTX_REVEAL_EXPLORER,
        L"Reveal in &Explorer panel");
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED), CTX_REVEAL_SHELL,
        L"Show in Windows &File Explorer");
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED), CTX_OPEN, L"&Open");
    ::AppendMenuW(h, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED), CTX_COPY_PATH, L"Copy &path (focused file)");
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED), CTX_COPY_ALL, L"Copy &all paths in group");
    ::AppendMenuW(h, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(h, MF_STRING, CTX_SAVE_SESSION, L"Save session as\u2026");
    ::AppendMenuW(h, MF_STRING, CTX_OPEN_SESSION, L"Open session\u2026");
    ::AppendMenuW(h, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(h, MF_STRING | (valid ? 0 : MF_GRAYED), CTX_REMOVE, L"&Remove from list\tDel");

    const UINT cmd = ::TrackPopupMenu(
        h, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, GetHwnd(), nullptr);
    ::DestroyMenu(h);
    if (cmd == 0) return;

    const CString   path0c = GetPathForItemData(data).c_str();
    const HWND     frame  = DupFrameHwnd(GetHwnd());

    switch (cmd) {
    case CTX_REVEAL_EXPLORER:
        if (valid && !path0c.IsEmpty() && frame) {
            auto* heap = new std::wstring(path0c.c_str());
            ::PostMessageW(frame, UWM_FIND_REVEAL_IN_EXPLORER, (WPARAM)heap, 0);
        }
        break;
    case CTX_REVEAL_SHELL:
        if (valid && !path0c.IsEmpty()) {
            pmui::shell::reveal_in_explorer(nullptr, path0c.GetString());
        }
        break;
    case CTX_OPEN:
        if (valid && !path0c.IsEmpty())
            pmui::shell::open_path(nullptr, path0c.GetString());
        break;
    case CTX_COPY_PATH:
        if (valid && !path0c.IsEmpty() && frame) {
            // Clipboard owner: top-level frame (some hosts reject tree views as owner).
            if (::OpenClipboard(frame)) {
            ::EmptyClipboard();
            const SIZE_T  bytes = (path0c.GetLength() + 1) * sizeof(wchar_t);
            const HGLOBAL h2    = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (h2) {
                if (auto* dst = static_cast<wchar_t*>(::GlobalLock(h2))) {
                    memcpy(dst, path0c.c_str(), bytes);
                    ::GlobalUnlock(h2);
                    ::SetClipboardData(CF_UNICODETEXT, h2);
                }
            }
            ::CloseClipboard();
            }
        }
        break;
    case CTX_COPY_ALL: {
        const std::vector<std::wstring>* pths = GetPathsForItemData(data);
        if (pths && !pths->empty() && frame) {
            if (::OpenClipboard(frame)) {
            ::EmptyClipboard();
            std::wstring joined;
            for (size_t i = 0; i < pths->size(); ++i) {
                if (i) joined += L"\r\n";
                joined += (*pths)[i];
            }
            const SIZE_T  bytes = (joined.size() + 1) * sizeof(wchar_t);
            const HGLOBAL h2    = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (h2) {
                if (auto* dst = static_cast<wchar_t*>(::GlobalLock(h2))) {
                    memcpy(dst, joined.c_str(), bytes);
                    ::GlobalUnlock(h2);
                    ::SetClipboardData(CF_UNICODETEXT, h2);
                }
            }
            ::CloseClipboard();
            }
        }
        break;
    }
    case CTX_SAVE_SESSION: if (frame) ::PostMessageW(frame, UWM_DUPLICATES_SAVE_SESSION, 0, 0); break;
    case CTX_OPEN_SESSION: if (frame) ::PostMessageW(frame, UWM_DUPLICATES_OPEN_SESSION, 0, 0); break;
    case CTX_REMOVE:     if (frame) ::PostMessageW(frame, UWM_DUPLICATES_DELETE_SELECTION, 0, 0); break;
    }
}

LRESULT CDuplicateResultsView::OnNotifyReflect(WPARAM, LPARAM lparam)
{
    LPNMHDR pnm = reinterpret_cast<LPNMHDR>(lparam);
    switch (pnm->code) {
    case TVN_SELCHANGED: {
        auto* n = reinterpret_cast<LPNMTREEVIEW>(lparam);
        if (n->itemNew.hItem) OnSelChanged(n->itemNew.hItem);
        return 0;
    }
    case NM_CLICK: {
        // Single click on the group/file row: load the internal preview (tree focus
        // can move without a TVN_SELCHANGED in a few host cases; this covers them).
        const DWORD pos = ::GetMessagePos();
        POINT       c{GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
        ::ScreenToClient(GetHwnd(), &c);
        TVHITTESTINFO hti{};
        hti.pt = c;
        if (const HTREEITEM hit = HitTest(hti)) {
            if (const UINT_PTR data = (UINT_PTR)GetItemData(hit)) {
                if (const HWND frame = DupFrameHwnd(GetHwnd()); frame)
                    ::SendMessageW(frame, UWM_DUPLICATES_ITEM_CLICKED, (WPARAM)data, 0);
            }
        }
        return 0;
    }
    case NM_DBLCLK: {
        if (const HTREEITEM h = GetSelection()) {
            const std::wstring p = GetPathForItemData((UINT_PTR)GetItemData(h));
            if (!p.empty())
                pmui::shell::open_path(nullptr, p);
        }
        return 0;
    }
    case NM_RCLICK: {
        const DWORD     pos  = ::GetMessagePos();
        const POINT     sp{GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
        POINT           c  = sp;
        ::ScreenToClient(GetHwnd(), &c);
        TVHITTESTINFO   ht2{};
        ht2.hItem  = nullptr;
        ht2.pt     = c;
        ht2.flags  = 0;
        HTREEITEM     hit  = HitTest(ht2);
        if (!hit) hit = GetSelection();
        ShowContextMenuFor(hit, sp);
        return 0;
    }
    }
    return 0;
}

LRESULT CDuplicateResultsView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {
        case WM_KEYDOWN:
            if (wparam == VK_DELETE) {
                if (const HWND frame = DupFrameHwnd(GetHwnd()); frame)
                    ::SendMessageW(frame, UWM_DUPLICATES_DELETE_SELECTION, 0, 0);
                return 0;
            }
            break;
        case WM_CONTEXTMENU: {
            const POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            HTREEITEM hit  = nullptr;
            if (pt.x == (short)0xFFFF && (unsigned short)pt.y == (unsigned short)0xFFFF) {
                hit = GetSelection();
            } else {
                POINT c = pt;
                ::ScreenToClient(GetHwnd(), &c);
                TVHITTESTINFO htt{};
                htt.pt = c;
                hit    = HitTest(htt);
            }
            ShowContextMenuFor(hit, pt);
            return 0;
        }
        }
        return WndProcDefault(msg, wparam, lparam);
    } catch (const CException& e) {
        CString s; s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s, L"Error", MB_ICONERROR);
    }
    return 0;
}

// ── Container / docker ──────────────────────────────────────────────────────

CDuplicateResultsContainer::CDuplicateResultsContainer()
{
    std::string err;
    media::settings::AppearanceSettings app{};
    media::settings::load_appearance(app, err);
    const auto& d = pmui::dock_chrome_i18n::strings_for(app.display_language);
    SetTabText(d.dup_tab);
    SetDockCaption(d.dup_results_caption);
    SetView(m_view);
}

CDockDuplicateResults::CDockDuplicateResults()
{
    SetView(m_container);
}

// ── Report enrichment (LLM / fingerprint) for tree labels and File info ───

static std::string path_key_norm(const std::string& p)
{
    std::string o = p;
    for (auto& c : o) {
        if (c == '/')
            c = '\\';
    }
    for (auto& c : o)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

void enrich_dup_rows_from_report(std::vector<DupListRow>& rows, const nlohmann::json& report)
{
    for (auto& row : rows) {
        row.pathSubtext.clear();
        row.pathSubtext.assign(row.paths.size(), L"");
    }
    if (report.contains("meta_json_compare") && report["meta_json_compare"].is_object()
        && report["meta_json_compare"].contains("pairs") && report["meta_json_compare"]["pairs"].is_array()) {
        const auto& pairs = report["meta_json_compare"]["pairs"];
        for (auto& row : rows) {
            if (row.method != L"meta" || row.key.rfind(L"llm_json:", 0) != 0)
                continue;
            for (size_t i = 0; i < row.paths.size(); ++i) {
                int best = -1;
                const std::string pi = path_key_norm(pmui::wide_to_utf8(row.paths[i]));
                for (size_t j = 0; j < row.paths.size(); ++j) {
                    if (i == j)
                        continue;
                    const std::string pj = path_key_norm(pmui::wide_to_utf8(row.paths[j]));
                    for (const auto& pr : pairs) {
                        if (!pr.is_object())
                            continue;
                        const std::string a = path_key_norm(pr.value("path_a", std::string{}));
                        const std::string b = path_key_norm(pr.value("path_b", std::string{}));
                        if ((a == pi && b == pj) || (a == pj && b == pi)) {
                            const int sim = pr.value("similarity_0_10", -1);
                            if (sim > best)
                                best = sim;
                        }
                    }
                }
                if (best >= 0) {
                    wchar_t buf[40];
                    swprintf_s(buf, L"sim %d/10", best);
                    row.pathSubtext[i] = buf;
                }
            }
        }
    }
    if (report.contains("fingerprint") && report["fingerprint"].is_object()
        && report["fingerprint"].contains("pairwise_in_output_groups")
        && report["fingerprint"]["pairwise_in_output_groups"].is_array()) {
        const auto& fpairs = report["fingerprint"]["pairwise_in_output_groups"];
        for (auto& row : rows) {
            if (row.method != L"fingerprint")
                continue;
            for (size_t i = 0; i < row.paths.size(); ++i) {
                int minH = -1;
                const std::string pi = path_key_norm(pmui::wide_to_utf8(row.paths[i]));
                for (size_t j = 0; j < row.paths.size(); ++j) {
                    if (i == j)
                        continue;
                    const std::string pj = path_key_norm(pmui::wide_to_utf8(row.paths[j]));
                    for (const auto& pr : fpairs) {
                        if (!pr.is_object())
                            continue;
                        const std::string a = path_key_norm(pr.value("path_a", std::string{}));
                        const std::string b = path_key_norm(pr.value("path_b", std::string{}));
                        if ((a == pi && b == pj) || (a == pj && b == pi)) {
                            const int h = pr.value("hamming", -1);
                            if (h >= 0) {
                                if (minH < 0 || h < minH)
                                    minH = h;
                            }
                        }
                    }
                }
                if (minH >= 0) {
                    if (minH == 0)
                        row.pathSubtext[i] = L"identical";
                    else {
                        wchar_t buf[40];
                        swprintf_s(buf, L"min Ham %d", minH);
                        row.pathSubtext[i] = buf;
                    }
                }
            }
        }
    }
}

std::wstring build_duplicate_report_detail_for_selection(const nlohmann::json& report,
    const std::wstring& selectedPath, const std::vector<std::wstring>& peerPaths)
{
    (void)peerPaths;
    if (selectedPath.empty() || !report.is_object())
        return {};
    const std::string sel = path_key_norm(pmui::wide_to_utf8(selectedPath));

    if (report.contains("meta_json_compare") && report["meta_json_compare"].is_object()
        && report["meta_json_compare"].contains("pairs")
        && report["meta_json_compare"]["pairs"].is_array()) {
        std::wostringstream out;
        bool                 any = false;
        for (const auto& p : report["meta_json_compare"]["pairs"]) {
            if (!p.is_object())
                continue;
            const std::string a  = path_key_norm(p.value("path_a", std::string{}));
            const std::string b  = path_key_norm(p.value("path_b", std::string{}));
            std::string         oth;
            if (a == sel)
                oth = b;
            else if (b == sel)
                oth = a;
            else
                continue;
            any  = true;
            const int        sim  = p.value("similarity_0_10", 0);
            std::string      notes = p.value("notes", std::string{});
            if (notes.size() > 200)
                notes = notes.substr(0, 197) + std::string("...");
            out << L"  " << pmui::utf8_to_wide(oth) << L"  —  " << sim << L"/10";
            if (!notes.empty())
                out << L"  —  " << pmui::utf8_to_wide(notes);
            out << L"\r\n";
        }
        if (any)
            return L"LLM pair scores (vs. selected file):\r\n" + out.str() + L"\r\n";
    }
    if (report.contains("fingerprint") && report["fingerprint"].is_object()
        && report["fingerprint"].contains("pairwise_in_output_groups")
        && report["fingerprint"]["pairwise_in_output_groups"].is_array()) {
        std::wostringstream out;
        bool                 any = false;
        for (const auto& p : report["fingerprint"]["pairwise_in_output_groups"]) {
            if (!p.is_object())
                continue;
            const std::string a = path_key_norm(p.value("path_a", std::string{}));
            const std::string b = path_key_norm(p.value("path_b", std::string{}));
            std::string         oth;
            if (a == sel)
                oth = b;
            else if (b == sel)
                oth = a;
            else
                continue;
            any       = true;
            const int h = p.value("hamming", -1);
            out << L"  " << pmui::utf8_to_wide(oth) << L"  —  dHash " << h << L"\r\n";
        }
        if (any)
            return L"Perceptual hash vs. selected file:\r\n" + out.str() + L"\r\n";
    }
    return {};
}
