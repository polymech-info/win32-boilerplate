#include "win/resize_ui.hpp"

#include "win/ui_singleton.hpp"

#include <windows.h>
#include <winerror.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <objbase.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

namespace media::win {

static std::string wide_to_utf8(const std::wstring &w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring utf8_to_wide(const std::string &s) {
    if (s.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

enum : int {
    IDC_STATIC_IN = 100,
    IDC_EDIT_IN = 101,
    IDC_BTN_IN = 102,
    IDC_STATIC_OUT = 103,
    IDC_EDIT_OUT = 104,
    IDC_BTN_OUT = 105,
    IDC_STATIC_MW = 106,
    IDC_EDIT_MW = 107,
    IDC_STATIC_MH = 108,
    IDC_EDIT_MH = 109,
    IDC_STATIC_FIT = 110,
    IDC_COMBO_FIT = 111,
    IDC_STATIC_Q = 112,
    IDC_EDIT_Q = 113,
    IDC_CHK_ENLARGE = 114,
    IDC_CHK_AUTOROT = 115,
    IDC_CHK_STRIP = 116,
    IDC_STATIC_PRESET = 117,
    IDC_COMBO_OUT_PRESET = 118,
    IDC_BTN_OK = IDOK,
    IDC_BTN_CANCEL = IDCANCEL,
};

static HFONT g_msg_font{};

static void ensure_message_font() {
    if (g_msg_font)
        return;
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_msg_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_msg_font)
        g_msg_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

static BOOL CALLBACK set_child_font(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
    return TRUE;
}

static void apply_message_font(HWND root) {
    ensure_message_font();
    SendMessageW(root, WM_SETFONT, reinterpret_cast<WPARAM>(g_msg_font), TRUE);
    EnumChildWindows(root, set_child_font, reinterpret_cast<LPARAM>(g_msg_font));
}

struct UiState {
    HWND root{};
    HWND h_in{};
    HWND h_out{};
    HWND h_btn_in{};
    HWND h_btn_out{};
    HWND h_mw{};
    HWND h_mh{};
    HWND h_fit{};
    HWND h_q{};
    HWND h_enlarge{};
    HWND h_autorot{};
    HWND h_strip{};
    HWND h_out_preset{};
    int y_preset{};
    int x0{16};
    int lw{108};
    int gap{8};
    int btn_w{90};
    int margin_r{14};
    int y_in{};
    int y_out{};
    ResizeOptions *opt{};
    std::string *in_path{};
    std::string *out_path{};
    bool accepted{false};
};

static void layout_stretch_path_rows(UiState *st, int client_w) {
    if (!st || !st->h_in || !st->h_out || client_w < 320)
        return;
    const int edit_x = st->x0 + st->lw + st->gap;
    const int browse_x = client_w - st->margin_r - st->btn_w;
    if (browse_x < edit_x + 80)
        return;
    const int ew = browse_x - st->gap - edit_x;
    const int eh = 26;
    MoveWindow(st->h_in, edit_x, st->y_in, ew, eh, TRUE);
    MoveWindow(st->h_btn_in, browse_x, st->y_in, st->btn_w, 28, TRUE);
    if (st->h_out_preset)
        MoveWindow(st->h_out_preset, edit_x, st->y_preset, ew, 180, TRUE);
    MoveWindow(st->h_out, edit_x, st->y_out, ew, eh, TRUE);
    MoveWindow(st->h_btn_out, browse_x, st->y_out, st->btn_w, 28, TRUE);
}

static void set_utf8_edit(HWND h, const std::string &utf8) {
    SetWindowTextW(h, utf8_to_wide(utf8).c_str());
}

static std::string get_utf8_edit(HWND h) {
    const int n = GetWindowTextLengthW(h);
    if (n <= 0)
        return {};
    std::wstring w(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, w.data(), n + 1);
    w.resize(static_cast<size_t>(n));
    return wide_to_utf8(w);
}

/** Append semicolon-separated paths (same convention as --src / singleton bridge). */
static void merge_utf8_paths_into_edit(HWND h_edit, const std::vector<std::string> &add_utf8) {
    if (add_utf8.empty())
        return;
    std::wstring cur;
    const int n = GetWindowTextLengthW(h_edit);
    if (n > 0) {
        cur.assign(static_cast<size_t>(n) + 1, L'\0');
        GetWindowTextW(h_edit, cur.data(), n + 1);
        cur.resize(static_cast<size_t>(n));
    }
    for (const auto &p : add_utf8) {
        if (p.empty())
            continue;
        std::wstring w = utf8_to_wide(p);
        if (w.empty())
            continue;
        if (!cur.empty())
            cur += L';';
        cur += w;
    }
    SetWindowTextW(h_edit, cur.c_str());
}

static void append_hdrop_to_input_edit(HWND h_edit, HDROP hdrop) {
    std::vector<std::string> paths;
    const UINT nfiles = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
    paths.reserve(nfiles);
    for (UINT i = 0; i < nfiles; ++i) {
        const UINT len = DragQueryFileW(hdrop, i, nullptr, 0);
        if (len == 0)
            continue;
        std::wstring w(static_cast<size_t>(len) + 1, L'\0');
        if (DragQueryFileW(hdrop, i, w.data(), len + 1) == 0)
            continue;
        w.resize(len);
        paths.push_back(wide_to_utf8(w));
    }
    DragFinish(hdrop);
    merge_utf8_paths_into_edit(h_edit, paths);
}

static LRESULT CALLBACK in_edit_drop_subclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclass_id,
                                              DWORD_PTR parent_hwnd) {
    if (msg == WM_DROPFILES) {
        SendMessageW(reinterpret_cast<HWND>(parent_hwnd), WM_DROPFILES, wp, 0);
        return 0;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(h, in_edit_drop_subclass, subclass_id);
    return DefSubclassProc(h, msg, wp, lp);
}

static void browse_open(HWND owner, HWND h_edit) {
    wchar_t buf[MAX_PATH * 4]{};
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = owner;
    of.lpstrFilter =
        L"Images\0*.jpg;*.jpeg;*.png;*.webp;*.tif;*.tiff;*.bmp;*.gif;*.avif;*.heic;*.jpe;*.jfif;*.jf\0"
        L"All\0*.*\0\0";
    of.nFilterIndex = 1;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH * 4;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&of))
        SetWindowTextW(h_edit, buf);
}

/** Longest existing directory prefix of `raw` (or parent if `raw` is a file path). Used to seed folder dialog. */
static std::wstring folder_browse_seed_from_text(const wchar_t *raw) {
    if (!raw || !raw[0])
        return {};
    std::wstring s(raw);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t'))
        s.pop_back();
    if (s.empty())
        return {};
    DWORD attr = GetFileAttributesW(s.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
            return s;
        const size_t pos = s.find_last_of(L"\\/");
        if (pos != std::wstring::npos && pos > 0)
            return s.substr(0, pos);
        return {};
    }
    for (int depth = 0; depth < 128; ++depth) {
        size_t pos = s.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
            break;
        if (pos == 0) {
            s = s.substr(0, 1);
            break;
        }
        s.resize(pos);
        if (s.size() == 2 && s[1] == L':')
            s += L'\\';
        attr = GetFileAttributesW(s.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return s;
    }
    return {};
}

static int CALLBACK browse_output_folder_proc(HWND hwnd, UINT msg, LPARAM, LPARAM data) {
    if (msg == BFFM_INITIALIZED && data)
        SendMessageW(hwnd, BFFM_SETSELECTIONW, FALSE, data);
    return 0;
}

static void browse_output_folder(HWND owner, HWND h_edit) {
    wchar_t raw[MAX_PATH * 4]{};
    GetWindowTextW(h_edit, raw, static_cast<int>(sizeof(raw) / sizeof(raw[0])));
    std::wstring seed = folder_browse_seed_from_text(raw);
    std::vector<wchar_t> seed_z(seed.begin(), seed.end());
    seed_z.push_back(L'\0');

    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Choose output folder (files keep their names)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    wchar_t display_name[MAX_PATH]{};
    bi.pszDisplayName = display_name;
    if (!seed.empty()) {
        bi.lpfn = browse_output_folder_proc;
        bi.lParam = reinterpret_cast<LPARAM>(seed_z.data());
    }
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return;
    wchar_t path[MAX_PATH * 4]{};
    if (SHGetPathFromIDListW(pidl, path))
        SetWindowTextW(h_edit, path);
    CoTaskMemFree(pidl);
}

static LRESULT CALLBACK UiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UiState *st = reinterpret_cast<UiState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        st = reinterpret_cast<UiState *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->root = hwnd;
        st->x0 = 14;
        st->lw = 108;
        st->gap = 8;
        st->btn_w = 90;
        st->margin_r = 14;
        const int x0 = st->x0;
        const int lw = st->lw;
        const int gap = st->gap;
        const int btn_w = st->btn_w;
        const int margin_r = st->margin_r;
        int y = 10;
        const int ew = 300;
        const int btn_h = 26;
        const int eh = 22;
        const int edit_x = x0 + lw + gap;
        const int browse_x = edit_x + ew + gap;

        CreateWindowExW(0, L"STATIC", L"Input image:", WS_CHILD | WS_VISIBLE, x0, y + 1, lw, 18, hwnd,
                        (HMENU)IDC_STATIC_IN, nullptr, nullptr);
        st->y_in = y;
        st->h_in = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, edit_x, y, ew, eh + 2, hwnd, (HMENU)IDC_EDIT_IN, nullptr,
            nullptr);
        st->h_btn_in = CreateWindowExW(0, L"BUTTON", L"Browse…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, browse_x, y,
                                       btn_w, btn_h, hwnd, (HMENU)IDC_BTN_IN, nullptr, nullptr);
        y += 34;

        st->y_preset = y;
        CreateWindowExW(0, L"STATIC", L"Output preset:", WS_CHILD | WS_VISIBLE, x0, y + 1, lw, 18, hwnd,
                        (HMENU)IDC_STATIC_PRESET, nullptr, nullptr);
        st->h_out_preset = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS, edit_x, y - 1, ew, 180, hwnd,
            (HMENU)IDC_COMBO_OUT_PRESET, nullptr, nullptr);
        {
            const wchar_t *presets[] = {L"Next to source (default name)", L"Next to source (stem_resized)",
                                        L"Use path below (custom)"};
            for (const wchar_t *t : presets)
                SendMessageW(st->h_out_preset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t));
            int pr = 0;
            if (!st->out_path->empty())
                pr = 2;
            else if (st->opt->output_stem_suffix == "_resized")
                pr = 1;
            SendMessageW(st->h_out_preset, CB_SETCURSEL, static_cast<WPARAM>(pr), 0);
        }
        y += 34;

        CreateWindowExW(0, L"STATIC", L"Custom output folder:", WS_CHILD | WS_VISIBLE, x0, y + 1, lw + 20, 18, hwnd,
                        (HMENU)IDC_STATIC_OUT, nullptr, nullptr);
        st->y_out = y;
        st->h_out = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, edit_x, y, ew, eh + 2, hwnd, (HMENU)IDC_EDIT_OUT,
            nullptr, nullptr);
        st->h_btn_out = CreateWindowExW(0, L"BUTTON", L"Browse…", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, browse_x, y,
                                        btn_w, btn_h, hwnd, (HMENU)IDC_BTN_OUT, nullptr, nullptr);
        y += 34;

        CreateWindowExW(0, L"STATIC", L"Max width:", WS_CHILD | WS_VISIBLE, x0, y + 1, lw, 18, hwnd,
                        (HMENU)IDC_STATIC_MW, nullptr, nullptr);
        wchar_t mw[32]{};
        swprintf_s(mw, L"%d", st->opt->max_width);
        st->h_mw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", mw,
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, edit_x, y, 96, eh + 2, hwnd,
                                   (HMENU)IDC_EDIT_MW, nullptr, nullptr);

        const int mh_label_x = edit_x + 96 + gap + 16;
        CreateWindowExW(0, L"STATIC", L"Max height:", WS_CHILD | WS_VISIBLE, mh_label_x, y + 1, 88, 18, hwnd,
                        (HMENU)IDC_STATIC_MH, nullptr, nullptr);
        wchar_t mh[32]{};
        swprintf_s(mh, L"%d", st->opt->max_height);
        st->h_mh = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", mh,
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, mh_label_x + 88 + gap, y, 96, eh + 2, hwnd,
                                   (HMENU)IDC_EDIT_MH, nullptr, nullptr);
        y += 34;

        CreateWindowExW(0, L"STATIC", L"Fit:", WS_CHILD | WS_VISIBLE, x0, y + 1, lw, 18, hwnd, (HMENU)IDC_STATIC_FIT,
                        nullptr, nullptr);
        st->h_fit = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS, edit_x, y - 1, 220,
                                    180, hwnd, (HMENU)IDC_COMBO_FIT, nullptr, nullptr);
        const wchar_t *fits[] = {L"inside", L"cover", L"contain", L"fill", L"outside"};
        for (const wchar_t *f : fits)
            SendMessageW(st->h_fit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(f));
        {
            std::wstring cur = utf8_to_wide(st->opt->fit);
            for (auto &ch : cur) {
                if (ch >= L'A' && ch <= L'Z')
                    ch = static_cast<wchar_t>(ch - L'A' + L'a');
            }
            int sel = 0;
            for (int i = 0; i < 5; ++i) {
                if (cur == fits[i]) {
                    sel = i;
                    break;
                }
            }
            SendMessageW(st->h_fit, CB_SETCURSEL, static_cast<WPARAM>(sel), 0);
        }
        y += 32;

        CreateWindowExW(0, L"STATIC", L"Quality (1–100):", WS_CHILD | WS_VISIBLE, x0, y + 1, lw + 40, 18, hwnd,
                        (HMENU)IDC_STATIC_Q, nullptr, nullptr);
        wchar_t q[16]{};
        swprintf_s(q, L"%d", st->opt->quality);
        st->h_q = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", q,
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, edit_x, y, 64, eh + 2, hwnd,
                                  (HMENU)IDC_EDIT_Q, nullptr, nullptr);
        y += 32;

        st->h_enlarge =
            CreateWindowExW(0, L"BUTTON", L"Allow enlargement", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x0, y, 210,
                            22, hwnd, (HMENU)IDC_CHK_ENLARGE, nullptr, nullptr);
        SendMessageW(st->h_enlarge, BM_SETCHECK,
                      st->opt->without_enlargement ? BST_UNCHECKED : BST_CHECKED, 0);
        st->h_autorot =
            CreateWindowExW(0, L"BUTTON", L"EXIF autorotate", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x0 + 228, y, 188,
                            22, hwnd, (HMENU)IDC_CHK_AUTOROT, nullptr, nullptr);
        SendMessageW(st->h_autorot, BM_SETCHECK, st->opt->autorotate ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 26;
        st->h_strip = CreateWindowExW(0, L"BUTTON", L"Strip metadata on save", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                      x0, y, 268, 22, hwnd, (HMENU)IDC_CHK_STRIP, nullptr, nullptr);
        SendMessageW(st->h_strip, BM_SETCHECK, st->opt->strip_metadata ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 34;

        const int btn_row_w = 92;
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, browse_x - btn_row_w - gap - btn_row_w,
                        y, btn_row_w, 28, hwnd, (HMENU)IDC_BTN_OK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, browse_x - btn_row_w, y, btn_row_w,
                        28, hwnd, (HMENU)IDC_BTN_CANCEL, nullptr, nullptr);

        set_utf8_edit(st->h_in, *st->in_path);
        set_utf8_edit(st->h_out, *st->out_path);
        media::win::set_ui_merge_input_edit(st->h_in);
        DragAcceptFiles(hwnd, TRUE);
        SetWindowSubclass(st->h_in, in_edit_drop_subclass, 1, reinterpret_cast<DWORD_PTR>(hwnd));
        if (HWND h_lab_in = GetDlgItem(hwnd, IDC_STATIC_IN))
            SetWindowSubclass(h_lab_in, in_edit_drop_subclass, 2, reinterpret_cast<DWORD_PTR>(hwnd));
        apply_message_font(hwnd);
        {
            RECT cr{};
            GetClientRect(hwnd, &cr);
            layout_stretch_path_rows(st, cr.right);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (!st)
            return 0;
        const int id = LOWORD(wp);
        if (id == IDC_BTN_IN) {
            browse_open(hwnd, st->h_in);
            return 0;
        }
        if (id == IDC_BTN_OUT) {
            browse_output_folder(hwnd, st->h_out);
            return 0;
        }
        if (id == IDC_BTN_CANCEL) {
            st->accepted = false;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDC_BTN_OK) {
            *st->in_path = get_utf8_edit(st->h_in);
            if (st->in_path->empty()) {
                MessageBoxW(hwnd, L"Choose an input image.", L"media-img", MB_OK | MB_ICONWARNING);
                return 0;
            }
            {
                const int ps = static_cast<int>(SendMessageW(st->h_out_preset, CB_GETCURSEL, 0, 0));
                st->opt->output_stem_suffix.clear();
                const std::string custom_out = get_utf8_edit(st->h_out);
                // Non-empty custom folder always applies (presets 0/1 used to clear out_path and ignored this field).
                if (!custom_out.empty()) {
                    *st->out_path = custom_out;
                    if (ps == 1)
                        st->opt->output_stem_suffix = "_resized";
                } else if (ps == 0) {
                    *st->out_path = {};
                } else if (ps == 1) {
                    *st->out_path = {};
                    st->opt->output_stem_suffix = "_resized";
                } else {
                    *st->out_path = {};
                }
            }

            wchar_t bmw[32]{};
            GetWindowTextW(st->h_mw, bmw, 32);
            wchar_t bmh[32]{};
            GetWindowTextW(st->h_mh, bmh, 32);
            st->opt->max_width = _wtoi(bmw);
            st->opt->max_height = _wtoi(bmh);
            if (st->opt->max_width < 0)
                st->opt->max_width = 0;
            if (st->opt->max_height < 0)
                st->opt->max_height = 0;

            const int fi = static_cast<int>(SendMessageW(st->h_fit, CB_GETCURSEL, 0, 0));
            const wchar_t *fits[] = {L"inside", L"cover", L"contain", L"fill", L"outside"};
            if (fi >= 0 && fi < 5)
                st->opt->fit = wide_to_utf8(fits[fi]);

            wchar_t bq[32]{};
            GetWindowTextW(st->h_q, bq, 32);
            st->opt->quality = _wtoi(bq);
            if (st->opt->quality < 1)
                st->opt->quality = 1;
            if (st->opt->quality > 100)
                st->opt->quality = 100;

            st->opt->without_enlargement =
                SendMessageW(st->h_enlarge, BM_GETCHECK, 0, 0) != BST_CHECKED;
            st->opt->autorotate = SendMessageW(st->h_autorot, BM_GETCHECK, 0, 0) == BST_CHECKED;
            st->opt->strip_metadata = SendMessageW(st->h_strip, BM_GETCHECK, 0, 0) == BST_CHECKED;

            st->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    }
    case WM_DROPFILES: {
        if (!st)
            return 0;
        append_hdrop_to_input_edit(st->h_in, reinterpret_cast<HDROP>(wp));
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hdc, GetSysColor(COLOR_3DFACE));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_3DFACE));
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_SIZE: {
        if (st && (wp == SIZE_RESTORED || wp == SIZE_MAXIMIZED)) {
            RECT cr{};
            GetClientRect(hwnd, &cr);
            layout_stretch_path_rows(st, cr.right);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    case WM_GETMINMAXINFO: {
        auto *mmi = reinterpret_cast<MINMAXINFO *>(lp);
        mmi->ptMinTrackSize.x = 460;
        mmi->ptMinTrackSize.y = 300;
        return 0;
    }
    case WM_CLOSE:
        if (st)
            st->accepted = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        media::win::clear_ui_merge_input_edit();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static const wchar_t kClassName[] = L"MediaImgResizeUi";

bool show_resize_ui(ResizeOptions &opt, std::string &input_path, std::string &output_path, const ResizeOptions &initial) {
    opt = initial;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = UiWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return false;
        }
        reg = true;
    }

    UiState state{};
    state.opt = &opt;
    state.in_path = &input_path;
    state.out_path = &output_path;

    const int W = 520;
    const int H = 430;
    RECT r{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
    const int sx = r.left + ((r.right - r.left) - W) / 2;
    const int sy = r.top + ((r.bottom - r.top) - H) / 2;

    const DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN;
    HWND hwnd = CreateWindowExW(WS_EX_WINDOWEDGE, kClassName, L"media-img — resize", style, sx, sy, W, H, nullptr,
                                nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd)
        return false;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return state.accepted;
}

} // namespace media::win
