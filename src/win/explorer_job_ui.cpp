#include "explorer_job_ui.hpp"

#if defined(_WIN32)

#include "constants.hpp"
#include "core/batch_queue.hpp"
#include "core/glob_paths.hpp"
#include "core/resize.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/theme.hpp"
#include "win/ui_next/helpers/default_shell.hpp"
// Ribbon bitmaps + i18n label / tooltip string ids (RibbonUI.h) — same as the main app toolbar strip.
#include "RibbonUI.h"
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
#include "win/ui_next/PixlwizSharePostDlg.h"
#endif

#include <UxTheme.h>
#include <algorithm>
#include <atomic>
#include <commctrl.h>
#include <condition_variable>
#include <cwchar>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

using pmui::utf8_to_wide;

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "UxTheme.lib")

#ifndef BTNS_SHOWTEXT
#define BTNS_SHOWTEXT 0x00000040
#endif
#ifndef BTNS_AUTOSIZE
#define BTNS_AUTOSIZE 0x00000010
#endif
#ifndef TBSTYLE_EX_MIXEDBUTTONS
#define TBSTYLE_EX_MIXEDBUTTONS 0x00000008
#endif
#ifndef TBSTYLE_EX_HIDECLIPPEDBUTTONS
#define TBSTYLE_EX_HIDECLIPPEDBUTTONS 0x00000010
#endif

namespace media::win {
namespace {

namespace fs = std::filesystem;

enum : int { COL_NAME = 0, COL_STATUS = 1, COL_PATH = 2, COL_N = 3 };
enum : int { IDC_LIST = 100, IDC_TOOLBAR = 105, IDC_PAUSE = 101, IDC_RESUME = 102, IDC_CANCEL = 103, IDC_OPEN_MAIN = 104 };

enum : UINT {
    WM_EJU_SET_STATUS = WM_APP + 64,
    WM_EJU_END        = WM_APP + 65,
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    WM_EJU_PIXLWIZ_SHARE_DLG       = WM_APP + 130,
    WM_EJU_PIXLWIZ_SHARE_SUCCESS   = WM_APP + 131,
#endif
    WM_EJU_REVEAL = WM_APP + 132,
};

#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
struct EjuPixlwizShareDlgPack {
    PixlwizSharePostFields* pfields{};
    bool*                   ok_out{};
};
#endif

struct JobUiState {
    HWND     hwnd{};
    HWND     h_list{};
    HWND     h_toolbar{};
    int      list_rows = 0;
    std::atomic<bool>   cancel{false};
    std::atomic<bool>   pause_req{false};
    std::mutex          pause_mu;
    std::condition_variable pause_cv;
    std::wstring        exe_path;
    bool                worker_ended{false};
    media::BatchControl* ext_batch{nullptr};
    HIMAGELIST          tb_imagelist{nullptr};
};

struct WndData {
    JobUiState*   st            = nullptr;
    HBRUSH        bg_brush      = nullptr;
    std::thread*  worker_thread = nullptr;
    /// When set, @c run_explorer_job_ui creates the frame hidden until @c WM_EJU_REVEAL (Pixlwiz post dialog first).
    bool          hide_until_pixlwiz_post_confirm = false;
};

static void list_set_subitem_status(HWND h_list, int row, const wchar_t* st) {
    if (!h_list || row < 0) return;
    ListView_SetItemText(h_list, row, COL_STATUS, const_cast<LPWSTR>(st));
}

static void style_job_listview(HWND h_list) {
    if (!h_list) return;
    const auto&               pal = pmui::theme_palette();
    ListView_SetBkColor(h_list, pal.control_bg);
    ListView_SetTextBkColor(h_list, pal.control_bg);
    ListView_SetTextColor(h_list, pal.control_fg);
    pmui::theme_listview_report_header(h_list);
    HWND h_hdr = ListView_GetHeader(h_list);
    if (h_hdr) {
        LONG_PTR st = GetWindowLongPtrW(h_hdr, GWL_STYLE);
        SetWindowLongPtrW(h_hdr, GWL_STYLE, st | HDS_FLAT);
    }
    InvalidateRect(h_list, nullptr, TRUE);
}

static HBITMAP load_ribbon_bmp_sized(UINT res_id, int cx, int cy) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    return (HBITMAP)LoadImageW(
        hi, MAKEINTRESOURCEW(res_id), IMAGE_BITMAP, cx, cy,
        LR_CREATEDIBSECTION | LR_DEFAULTCOLOR);
}

/// Match `COwnRibbonToolStrip::ApplyChrome`: Explorer theme in light; empty theme in dark so
/// `NMTBCUSTOMDRAW` + transparent strip paints reliably (DarkMode_Explorer alone keeps a light bar).
static void apply_job_toolbar_chrome(HWND h_tb, bool dark) {
    if (!h_tb) return;
    if (dark)
        SetWindowTheme(h_tb, L"", L"");
    else
        SetWindowTheme(h_tb, L"Explorer", nullptr);
    InvalidateRect(h_tb, nullptr, TRUE);
}

static int tb_index_from_spec(HWND h_tb, DWORD_PTR dw) {
    int n = (int)SendMessageW(h_tb, TB_BUTTONCOUNT, 0, 0);
    if (n <= 0) return -1;
    int idx = (int)SendMessageW(h_tb, TB_COMMANDTOINDEX, (WPARAM)dw, 0);
    if (idx >= 0) return idx;
    if (dw < (DWORD_PTR)n) {
        TBBUTTON b{};
        if (SendMessageW(h_tb, TB_GETBUTTON, (int)dw, (LPARAM)&b)) return (int)dw;
    }
    return -1;
}

static void set_job_tb_button_label_by_cmd(HWND h_tb, int cmd_id, const wchar_t* text) {
    if (!h_tb || !text) return;
    TBBUTTONINFOW bi{};
    bi.cbSize  = sizeof(bi);
    bi.dwMask  = TBIF_TEXT | TBIF_STYLE;
    bi.fsStyle = static_cast<BYTE>(BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE);
    bi.pszText = const_cast<LPWSTR>(text);
    (void)SendMessageW(h_tb, TB_SETBUTTONINFO, static_cast<WPARAM>(cmd_id), reinterpret_cast<LPARAM>(&bi));
}

static LRESULT CALLBACK explorer_job_list_subclass(HWND h, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR /*id*/,
    DWORD_PTR /*data*/) {
    if (msg == WM_NOTIFY) {
        LRESULT out = 0;
        if (pmui::theme_header_customdraw_notify(h, lparam, out))
            return out;
    }
    const LRESULT r = DefSubclassProc(h, msg, wparam, lparam);
    if (msg == WM_NCDESTROY)
        (void)RemoveWindowSubclass(h, explorer_job_list_subclass, 1);
    return r;
}

static LRESULT toolbar_on_customdraw(HWND h_tb, NMTBCUSTOMDRAW* tbcd) {
    const auto&        pal    = pmui::theme_palette();
    const COLORREF     stripBg = pal.window_bg;
    const COLORREF     normal  = pal.window_fg;
    const UINT         st      = tbcd->nmcd.dwDrawStage;
    switch (st) {
    case CDDS_PREPAINT: {
        if (pal.dark) {
            RECT cr{};
            GetClientRect(h_tb, &cr);
            HBRUSH br = CreateSolidBrush(stripBg);
            FillRect(tbcd->nmcd.hdc, &cr, br);
            DeleteObject(br);
        }
        return CDRF_NOTIFYITEMDRAW;
    }
    case CDDS_ITEMPREPAINT: {
        const int idx = tb_index_from_spec(h_tb, tbcd->nmcd.dwItemSpec);
        if (idx < 0) return CDRF_DODEFAULT;
        TBBUTTON btn{};
        if (!SendMessageW(h_tb, TB_GETBUTTON, idx, (LPARAM)&btn)) return CDRF_DODEFAULT;
        if (btn.fsStyle & TBSTYLE_SEP) return CDRF_DODEFAULT;
        const LRESULT stTb    = SendMessageW(h_tb, TB_GETSTATE, (WPARAM)btn.idCommand, 0);
        const bool    off     = (stTb & TBSTATE_ENABLED) == 0;
        const bool    dis     = (tbcd->nmcd.uItemState & CDIS_DISABLED) != 0;
        const bool    inactive = dis || off;
        const COLORREF labelFg = inactive ? pal.caption_fg_inactive : normal;
        tbcd->clrText = labelFg;
        if (pal.dark) {
            tbcd->nStringBkMode = TRANSPARENT;
            LRESULT r = CDRF_DODEFAULT;
#ifdef TBCDRF_USECDCOLORS
            r |= TBCDRF_USECDCOLORS;
#endif
            if (inactive) r |= TBCDRF_NOETCHEDEFFECT;
            return r;
        }
        return CDRF_DODEFAULT;
    }
    default:
        break;
    }
    return CDRF_DODEFAULT;
}

static bool add_tb_images_and_buttons(HWND w, JobUiState& s) {
    HINSTANCE  hi  = GetModuleHandleW(nullptr);
    const int  dpi = GetDpiForWindow(w);
    const int  px  = MulDiv(32, dpi, 96);
    s.tb_imagelist = ImageList_Create(px, px, ILC_COLOR32 | ILC_MASK, 4, 2);
    if (!s.tb_imagelist) return false;
    const UINT res_lg[4]   = {IDC_CMD_PAUSE_LargeImages_RESID, IDC_CMD_RESUME_LargeImages_RESID,
                              IDC_CMD_CANCEL_LargeImages_RESID, IDC_CMD_APP_SETTINGS_LargeImages_RESID};
    HBITMAP   bm[4]        = {};
    int       first_ok     = -1;
    for (int j = 0; j < 4; ++j) {
        bm[j]   = load_ribbon_bmp_sized(res_lg[j], px, px);
        if (bm[j]) {
            const int idx = ImageList_Add(s.tb_imagelist, bm[j], nullptr);
            DeleteObject(bm[j]);
            bm[j] = nullptr;
            if (first_ok < 0 && idx >= 0) first_ok = idx;
        }
    }
    if (first_ok < 0)
        first_ok = 0;
    s.h_toolbar
        = CreateWindowExW(0, TOOLBARCLASSNAMEW, L"",
                            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TRANSPARENT
                                | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE
                                | WS_CLIPCHILDREN,
                            0, 0, 0, 0, w, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TOOLBAR)), hi, nullptr);
    if (!s.h_toolbar) return false;
    SendMessageW(
        s.h_toolbar, TB_SETEXTENDEDSTYLE, 0,
        TBSTYLE_EX_DOUBLEBUFFER | TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);
    SendMessageW(s.h_toolbar, TB_SETMAXTEXTROWS, 1, 0);
    {
        const int p = MulDiv(8, dpi, 96);
        (void)SendMessageW(s.h_toolbar, TB_SETPADDING, 0, MAKELPARAM(p, 0));
        (void)SendMessageW(s.h_toolbar, TB_SETINDENT, p, 0);
    }
    SendMessageW(s.h_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(s.h_toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(s.tb_imagelist));
    TBBUTTON bt[4] = {};
    int      img[4] = {0, 1, 2, 3};
    for (int k = 0; k < 4; ++k) {
        if (img[k] >= ImageList_GetImageCount(s.tb_imagelist)) img[k] = first_ok;
    }
    for (int k = 0; k < 4; ++k) {
        bt[k].iBitmap   = img[k];
        bt[k].idCommand = (k == 0) ? IDC_PAUSE
                          : (k == 1)  ? IDC_RESUME
                          : (k == 2)  ? IDC_CANCEL
                                    : IDC_OPEN_MAIN;
        bt[k].fsState  = TBSTATE_ENABLED;
        bt[k].fsStyle  = static_cast<BYTE>(BTNS_BUTTON);
        bt[k].iString  = -1;
    }
    if (!SendMessageW(s.h_toolbar, TB_ADDBUTTONS, 4, reinterpret_cast<LPARAM>(bt)))
        return false;
    // Labels under icons — same i18n ids as the ribbon (OwnRibbonLayout::LayoutItem::labelStringId).
    const UINT label_id[4] = {IDC_CMD_PAUSE_LabelTitle_RESID, IDC_CMD_RESUME_LabelTitle_RESID,
        IDC_CMD_CANCEL_LabelTitle_RESID, IDC_CMD_APP_SETTINGS_LabelTitle_RESID};
    const int  cmd[4]      = {IDC_PAUSE, IDC_RESUME, IDC_CANCEL, IDC_OPEN_MAIN};
    for (int k = 0; k < 4; ++k) {
        wchar_t   buf[512]{};
        wchar_t*  title = nullptr;
        if (label_id[k] && ::LoadStringW(hi, label_id[k], buf, int(sizeof(buf) / sizeof(buf[0]))) > 0)
            title = buf;
        if (title) {
            set_job_tb_button_label_by_cmd(s.h_toolbar, cmd[k], title);
        } else {
            // Rare: missing string table; fall back to English.
            const wchar_t* en[4] = {L"Pause", L"Resume", L"Cancel", L"App settings"};
            set_job_tb_button_label_by_cmd(s.h_toolbar, cmd[k], en[k]);
        }
    }
    (void)SendMessageW(s.h_toolbar, TB_AUTOSIZE, 0, 0);
    return true;
}

static const wchar_t k_class[] = L"PmExplorerJobUi";

static LRESULT CALLBACK ExplorerJobWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    WndData* d = reinterpret_cast<WndData*>(GetWindowLongPtrW(w, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs  = reinterpret_cast<CREATESTRUCTW*>(lp);
        d         = static_cast<WndData*>(cs->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
        if (!d || !d->st) return -1;
        JobUiState& s = *d->st;
        s.hwnd = w;
        s.h_list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL
                                                                        | LVS_SHOWSELALWAYS,
                                   0, 0, 0, 0, w, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)),
                                   GetModuleHandleW(nullptr), nullptr);
        if (!s.h_list) return -1;
        if (!SetWindowSubclass(s.h_list, explorer_job_list_subclass, 1, 0))
            return -1;
        ListView_SetExtendedListViewStyle(
            s.h_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        LVCOLUMNW col{};
        col.mask     = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.fmt      = LVCFMT_LEFT;
        const int    dpi = GetDpiForWindow(w);
        col.pszText  = const_cast<LPWSTR>(L"Name");
        col.cx       = MulDiv(200, dpi, 96);
        col.iSubItem = 0;
        ListView_InsertColumn(s.h_list, 0, &col);
        col.pszText = const_cast<LPWSTR>(L"Status");
        col.cx      = MulDiv(200, dpi, 96);
        col.iSubItem = 1;
        ListView_InsertColumn(s.h_list, 1, &col);
        col.pszText = const_cast<LPWSTR>(L"Path");
        col.cx      = MulDiv(400, dpi, 96);
        col.iSubItem = 2;
        ListView_InsertColumn(s.h_list, 2, &col);

        if (!add_tb_images_and_buttons(w, s)) return -1;
        return 0;
    }
    case WM_SIZE: {
        if (d && d->st) {
            RECT r{};
            GetClientRect(w, &r);
            int W  = r.right, H = r.bottom;
            int    pad  = 8;
            int    y_list  = pad;
            const int dpi  = GetDpiForWindow(w);
            // Large ribbon bitmaps + one text row (OwnRibbonLayout-style) — reserve before the bar is
            // laid out at the final client width.
            int    tbh     = MulDiv(68, dpi, 96);
            int    bottom_h = pad + tbh + pad;
            int    list_h  = (std::max)(0, H - y_list - bottom_h);
            int    list_w  = (std::max)(0, W - pad * 2);
            if (d->st->h_list) MoveWindow(d->st->h_list, pad, y_list, list_w, list_h, TRUE);
            if (d->st->h_toolbar) {
                int y_btn = y_list + list_h + pad;
                MoveWindow(d->st->h_toolbar, pad, y_btn, list_w, tbh, TRUE);
                (void)SendMessageW(d->st->h_toolbar, TB_AUTOSIZE, 0, 0);
                // Second pass: intrinsic strip can exceed the 68*96 estimate once laid out at full width.
                SIZE ms{};
                if (SendMessageW(d->st->h_toolbar, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&ms)) && ms.cy > tbh) {
                    tbh   = (int)ms.cy;
                    list_h = (std::max)(0, H - y_list - (pad + tbh + pad));
                    if (d->st->h_list) MoveWindow(d->st->h_list, pad, y_list, list_w, list_h, TRUE);
                    y_btn = y_list + list_h + pad;
                    MoveWindow(d->st->h_toolbar, pad, y_btn, list_w, tbh, TRUE);
                    (void)SendMessageW(d->st->h_toolbar, TB_AUTOSIZE, 0, 0);
                }
            }
        }
        return 0;
    }
    case WM_NOTIFY: {
        if (d && d->st) {
            const NMHDR* nh = reinterpret_cast<NMHDR*>(lp);
            if (nh && d->st->h_toolbar && nh->hwndFrom == d->st->h_toolbar) {
                if (nh->code == TBN_GETINFOTIP) {
                    auto*     g  = reinterpret_cast<NMTBGETINFOTIPW*>(lp);
                    TBBUTTON  btn{};
                    if (g->pszText && g->cchTextMax > 0
                        && g->iItem >= 0
                        && SendMessageW(d->st->h_toolbar, TB_GETBUTTON, (WPARAM)g->iItem, (LPARAM)&btn)) {
                        HINSTANCE  hi  = GetModuleHandleW(nullptr);
                        UINT       tip = 0;
                        switch (btn.idCommand) {
                        case IDC_PAUSE: tip   = IDC_CMD_PAUSE_TooltipDescription_RESID; break;
                        case IDC_RESUME: tip  = IDC_CMD_RESUME_TooltipDescription_RESID; break;
                        case IDC_CANCEL: tip  = IDC_CMD_CANCEL_TooltipDescription_RESID; break;
                        case IDC_OPEN_MAIN: tip = IDC_CMD_APP_SETTINGS_TooltipDescription_RESID; break;
                        default: break;
                        }
                        if (!(tip
                                && ::LoadStringW(
                                    hi, tip, g->pszText, static_cast<int>(g->cchTextMax)) > 0)) {
                            const wchar_t* en = L"";
                            switch (btn.idCommand) {
                            case IDC_PAUSE: en  = L"Pauses the batch after the current step."; break;
                            case IDC_RESUME: en = L"Resume the batch."; break;
                            case IDC_CANCEL: en = L"Cancel the batch."; break;
                            case IDC_OPEN_MAIN: en = L"App settings and shell integration."; break;
                            default: en = L""; break;
                            }
                            wcsncpy_s(
                                g->pszText, static_cast<size_t>(g->cchTextMax), en, _TRUNCATE);
                        }
                    }
                    return 0;
                }
                if (nh->code == NM_CUSTOMDRAW) {
                    auto* tbcd = reinterpret_cast<NMTBCUSTOMDRAW*>(lp);
                    return toolbar_on_customdraw(d->st->h_toolbar, tbcd);
                }
            }
        }
        return DefWindowProcW(w, msg, wp, lp);
    }
    case WM_ERASEBKGND: {
        if (!d || !d->bg_brush) return DefWindowProcW(w, msg, wp, lp);
        HDC  hdc = reinterpret_cast<HDC>(wp);
        RECT r{};
        GetClientRect(w, &r);
        FillRect(hdc, &r, d->bg_brush);
        return 1;
    }
    case WM_EJU_SET_STATUS: {
        int     row    = static_cast<int>(static_cast<INT_PTR>(wp));
        wchar_t* t     = reinterpret_cast<wchar_t*>(lp);
        if (d && d->st && d->st->h_list && t) {
            if (row >= 0 && row < ListView_GetItemCount(d->st->h_list)) list_set_subitem_status(d->st->h_list, row, t);
        }
        delete[] t;
        return 0;
    }
    case WM_EJU_END: {
        if (d) d->st->worker_ended = true;
        if (d && d->st) d->st->ext_batch = nullptr;
        if (d && d->worker_thread) {
            if (d->worker_thread->joinable()) d->worker_thread->join();
            d->worker_thread = nullptr;
        }
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
        // User never saw the job frame (post dialog cancelled before reveal) — close instead of leaving a hidden window.
        if (d && d->hide_until_pixlwiz_post_confirm && !IsWindowVisible(w))
            DestroyWindow(w);
#endif
        return 0;
    }
    case WM_EJU_REVEAL: {
        if (!IsWindowVisible(w)) {
            ShowWindow(w, SW_SHOW);
            UpdateWindow(w);
            (void)SendMessageW(w, WM_SIZE, 0, 0);
            (void)::SetForegroundWindow(w);
        }
        return 0;
    }
#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
    case WM_EJU_PIXLWIZ_SHARE_DLG: {
        auto* p = reinterpret_cast<EjuPixlwizShareDlgPack*>(lp);
        if (!p || !p->pfields || !p->ok_out)
            return 0;
        *p->ok_out = RunPixlwizSharePostDialog(w, *p->pfields);
        return 0;
    }
    case WM_EJU_PIXLWIZ_SHARE_SUCCESS: {
        auto* ws = reinterpret_cast<std::wstring*>(lp);
        if (ws) {
            RunPixlwizShareSuccessDialog(w, *ws, static_cast<size_t>(static_cast<UINT_PTR>(wp)));
            delete ws;
        }
        return 0;
    }
#endif
    case WM_COMMAND: {
        int      id  = LOWORD(wp);
        const int hc = HIWORD(wp);
        if (hc == 0 || hc == 1) {
        } // menu / toolbar
        if (!d || !d->st) return 0;
        JobUiState& s = *d->st;
        if (id == IDC_PAUSE) {
            if (s.ext_batch)
                s.ext_batch->pause();
            else
                s.pause_req = true;
        } else if (id == IDC_RESUME) {
            if (s.ext_batch)
                s.ext_batch->resume();
            else {
                s.pause_req = false;
                s.pause_cv.notify_all();
            }
        } else if (id == IDC_CANCEL) {
            s.cancel = true;
            s.pause_cv.notify_all();
            if (s.ext_batch) s.ext_batch->request_cancel();
        } else if (id == IDC_OPEN_MAIN) {
            if (!s.exe_path.empty()) pmui::shell::open_path(w, s.exe_path);
        }
        return 0;
    }
    case WM_CLOSE: {
        if (d && d->st) {
            d->st->cancel = true;
            d->st->pause_cv.notify_all();
            if (d->st->ext_batch) d->st->ext_batch->request_cancel();
        }
        DestroyWindow(w);
        return 0;
    }
    case WM_DESTROY: {
        if (d && d->st) {
            if (d->st->tb_imagelist) {
                if (d->st->h_toolbar) {
                    SendMessageW(d->st->h_toolbar, TB_SETIMAGELIST, 0, 0);
                    d->st->h_toolbar = nullptr;
                }
                ImageList_Destroy(d->st->tb_imagelist);
                d->st->tb_imagelist = nullptr;
            }
        }
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(w, msg, wp, lp);
    }
}

static bool reg_class() {
    static bool ok = false;
    if (ok) return true;
    WNDCLASSEXW wcx{};
    wcx.cbSize      = sizeof(wcx);
    wcx.lpszClassName = k_class;
    wcx.hInstance  = GetModuleHandleW(nullptr);
    wcx.hCursor    = LoadCursor(nullptr, IDC_ARROW);
    wcx.hbrBackground = nullptr; // WM_ERASEBKGND paints theme background
    wcx.lpfnWndProc   = ExplorerJobWndProc;
    wcx.style         = CS_DBLCLKS;
    if (RegisterClassExW(&wcx) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    ok = true;
    return true;
}

} // namespace

void ExplorerJobHost::set_status(int row, const wchar_t* st) {
    if (!st || !state_) return;
    auto* s   = static_cast<JobUiState*>(state_);
    if (!s->h_list) return;
    const std::size_t wlen = std::wcslen(st) + 1;
    auto*             buf  = new(std::nothrow) wchar_t[wlen];
    if (!buf) return;
    std::wmemcpy(buf, st, wlen);
    if (!PostMessageW(s->hwnd, WM_EJU_SET_STATUS, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(buf)))
        delete[] buf;
}
void ExplorerJobHost::link_batch_control(media::BatchControl* b) {
    if (state_) static_cast<JobUiState*>(state_)->ext_batch = b;
}
void ExplorerJobHost::set_status(int row, std::wstring_view sv) {
    std::wstring t(sv);
    set_status(row, t.c_str());
}
bool ExplorerJobHost::cancel_requested() const {
    if (!state_) return true;
    return static_cast<JobUiState*>(state_)->cancel.load();
}
void ExplorerJobHost::wait_if_paused() {
    if (!state_) return;
    auto* s = static_cast<JobUiState*>(state_);
    for (;;) {
        if (s->cancel.load()) return;
        std::unique_lock<std::mutex> lk(s->pause_mu);
        s->pause_cv.wait(lk, [&] { return !s->pause_req.load() || s->cancel.load(); });
        if (s->cancel.load()) return;
        if (!s->pause_req.load()) return;
    }
}

HWND ExplorerJobHost::job_window() const noexcept
{
    if (!state_)
        return nullptr;
    return static_cast<JobUiState*>(state_)->hwnd;
}

#if defined(FEATURE_PIXLWIZ_SHARE) && FEATURE_PIXLWIZ_SHARE
bool explorer_job_sync_pixlwiz_share_post_dialog(HWND job_hwnd, PixlwizSharePostFields& fields)
{
    if (!job_hwnd)
        return false;
    bool ok = false;
    EjuPixlwizShareDlgPack pack{&fields, &ok};
    (void)::SendMessageW(job_hwnd, WM_EJU_PIXLWIZ_SHARE_DLG, 0, reinterpret_cast<LPARAM>(&pack));
    return ok;
}

void explorer_job_sync_pixlwiz_share_success_dialog(HWND job_hwnd, const std::wstring& url_w, size_t picture_count)
{
    if (!job_hwnd)
        return;
    auto* heap = new std::wstring(url_w);
    (void)::SendMessageW(job_hwnd, WM_EJU_PIXLWIZ_SHARE_SUCCESS, static_cast<WPARAM>(picture_count),
        reinterpret_cast<LPARAM>(heap));
}
#else
bool explorer_job_sync_pixlwiz_share_post_dialog(HWND /*job_hwnd*/, PixlwizSharePostFields& /*fields*/)
{
    return false;
}

void explorer_job_sync_pixlwiz_share_success_dialog(HWND /*job_hwnd*/, const std::wstring& /*url_w*/, size_t /*picture_count*/)
{
}
#endif

void explorer_job_ui_reveal_job_window(HWND job_hwnd)
{
    if (!job_hwnd || IsWindowVisible(job_hwnd))
        return;
    (void)::SendMessageW(job_hwnd, WM_EJU_REVEAL, 0, 0);
}

bool run_explorer_job_ui(
    const std::wstring& title, const std::vector<ExplorerJobRow>& rows,
    const std::function<void(ExplorerJobHost&)>& work,
    bool                                          hide_job_window_until_post_details_confirmed) {
    if (rows.empty() || !work) return true;

    // Console subsystem: hide the host console for this run so only the job window shows
    // (e.g. Explorer + --job-ui). Skip when stdin is a character-mode console so an interactive
    // terminal session keeps Ctrl+C / the TTY (see docs/main.md TODO A).
    if (::GetConsoleWindow() != nullptr) {
        const HANDLE hin = ::GetStdHandle(STD_INPUT_HANDLE);
        DWORD        ftin = FILE_TYPE_UNKNOWN;
        if (hin && hin != INVALID_HANDLE_VALUE)
            ftin = ::GetFileType(hin);
        const bool stdin_is_console = (ftin == FILE_TYPE_CHAR);
        if (!stdin_is_console)
            (void)::FreeConsole();
    }

    pmui::theme_init_from_settings();
    pmui::sync_preferred_app_mode();
    if (pmui::is_dark_active()) pmui::enable_app_dark_mode(true);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    if (!reg_class()) return false;

    JobUiState  st;
    WndData     wd{};
    wd.st       = &st;
    wd.hide_until_pixlwiz_post_confirm = hide_job_window_until_post_details_confirmed;
    const auto& pal = pmui::theme_palette();
    wd.bg_brush     = CreateSolidBrush(pal.window_bg);

    wchar_t   exe_path[MAX_PATH]{};
    (void)::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    st.exe_path  = exe_path;
    ExplorerJobHost host;
    const auto     wcopy = work;
    host.state_  = &st;
    std::thread    t;

    RECT warea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &warea, 0);
    int  pw  = 780, ph = 420, sx = warea.left + ((warea.right - warea.left) - pw) / 2,
         sy  = warea.top + ((warea.bottom - warea.top) - ph) / 2;

    DWORD wstyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_SIZEBOX | WS_CLIPCHILDREN;
    if (!hide_job_window_until_post_details_confirmed)
        wstyle |= WS_VISIBLE;
    HWND wnd = CreateWindowExW(0, k_class, title.c_str(), wstyle, sx, sy, pw, ph, nullptr, nullptr,
                               GetModuleHandleW(nullptr), &wd);
    if (!wnd) {
        if (wd.bg_brush) {
            DeleteObject(wd.bg_brush);
            wd.bg_brush = nullptr;
        }
        return false;
    }

    pmui::apply_dark_titlebar(wnd, pmui::is_dark_active());
    // Match DWM border to client chrome (replaces the old `SetWindowRgn` pass, which clashed with
    // the non-client area and read as a bad / double “border” around the window).
    pmui::apply_dwm_toplevel_frame_tint(wnd, pal.window_bg);
    pmui::apply_window_theme_recursive(wnd, pmui::is_dark_active());
    if (st.h_toolbar) apply_job_toolbar_chrome(st.h_toolbar, pmui::is_dark_active());

    st.h_list = GetDlgItem(wnd, IDC_LIST);
    if (!st.h_list) {
        DestroyWindow(wnd);
        if (wd.bg_brush) {
            DeleteObject(wd.bg_brush);
            wd.bg_brush = nullptr;
        }
        return false;
    }
    style_job_listview(st.h_list);
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const ExplorerJobRow& r = rows[static_cast<std::size_t>(i)];
        LVITEMW     it{};
        it.iItem    = i;
        it.iSubItem = 0;
        it.mask     = LVIF_TEXT;
        it.pszText  = const_cast<LPWSTR>(r.name.c_str());
        ListView_InsertItem(st.h_list, &it);
        list_set_subitem_status(st.h_list, i, L"Queued");
        if (!r.path.empty()) ListView_SetItemText(st.h_list, i, 2, const_cast<LPWSTR>(r.path.c_str()));
    }
    st.list_rows = static_cast<int>(rows.size());

    if (!hide_job_window_until_post_details_confirmed) {
        ShowWindow(wnd, SW_SHOW);
        UpdateWindow(wnd);
    }
    (void)SendMessageW(wnd, WM_SIZE, 0, 0);

    HWND wnd_t = wnd;
    wd.worker_thread = &t;
    t                = std::thread([wcopy, wnd_t, &host]() {
        try {
            wcopy(host);
        } catch (...) {
        }
        (void)PostMessageW(wnd_t, WM_EJU_END, 0, 0);
    });
    MSG  msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (t.joinable()) t.join();
    if (wd.bg_brush) {
        DeleteObject(wd.bg_brush);
        wd.bg_brush = nullptr;
    }
    if (IsWindow(wnd)) DestroyWindow(wnd);
    host.state_ = nullptr;
    return true;
}

bool run_resize_batch_with_job_ui(
    const std::string& input_spec, const std::string& output_spec, const ResizeOptions& opt,
    std::string& err_out, ResizeBatchResult* out_stats, bool always_show) {
    std::string  pair_err;
    auto         jobs
        = media::pair_resize_paths(input_spec, output_spec, pair_err, output_spec.empty() ? &opt.format : nullptr,
                                 output_spec.empty() ? &opt.output_stem_suffix : nullptr);
    if (!pair_err.empty()) {
        err_out = pair_err;
        return false;
    }
    if (jobs.empty()) {
        err_out = "no resize jobs";
        return false;
    }
    if (!always_show && jobs.size() == 1) return media::resize_batch(input_spec, output_spec, opt, err_out, out_stats);

    std::vector<ExplorerJobRow> rows;
    for (const auto& j : jobs) {
        fs::path   p  = fs::u8path(j.first);
        ExplorerJobRow e;
        e.name = p.filename().wstring();
        e.path = p.wstring();
        rows.push_back(std::move(e));
    }
    std::string worker_err;
    bool        ok_all = true;
    bool        uok    = run_explorer_job_ui(
        std::wstring(pm::brand::k_ui_job_title_resize_w), rows, [&](ExplorerJobHost& h) {
        if (out_stats) {
            out_stats->count = 0;
            out_stats->outputs.clear();
        }
        for (int i = 0; i < static_cast<int>(jobs.size()); ++i) {
            h.wait_if_paused();
            if (h.cancel_requested()) break;
            h.set_status(i, L"Running");
            const auto& job  = jobs[static_cast<std::size_t>(i)];
            std::error_code ec;
            fs::create_directories(job.second.parent_path(), ec);
            std::string     one;
            if (!media::resize_file(job.first, job.second.string(), opt, one)) {
                ok_all     = false;
                worker_err = job.first + ": " + one;
                std::wstring werr = L"Error: " + utf8_to_wide(one);
                if (werr.size() > 200) werr.resize(200);
                h.set_status(i, werr.c_str());
                if (!h.cancel_requested()) {
                    // continue other files
                }
                break;
            }
            h.set_status(i, L"Done");
            if (out_stats) {
                out_stats->count += 1;
                out_stats->outputs.push_back(job.second.string());
            }
        }
    });
    if (!uok) return false;
    if (!ok_all && !worker_err.empty()) err_out = std::move(worker_err);
    return ok_all;
}

} // namespace media::win

#else

// Non-Windows: stubs not used; implementation omitted.

#endif
