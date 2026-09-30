#include "stdafx.h"
#include "constants.hpp"
#include "AppSettingsDlg.h"
#include "ProviderDlg.h"
#include "ChatProviderDlg.h"
#include "win/settings_store.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/ui_font.hpp"
#include "helpers/ui_language.hpp"
#include "helpers/app_settings_i18n.hpp"
#include "helpers/theme.hpp"
#if defined(FEATURE_LICENSE_FILE)
#include "win/machine_fingerprint.hpp"
#endif

#include <commctrl.h>
#include <commdlg.h>
#include <cstring>
#include <cwchar>
#include <string>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")

namespace {

constexpr int IDC_LANG_COMBO        = 804;
constexpr int IDC_THEME_COMBO       = 800;
constexpr int IDC_FONT_COMBO        = 801;
constexpr int IDC_BTN_IMG_PROVIDER  = 802;
constexpr int IDC_BTN_CHAT_PROVIDER = 803;
constexpr int IDC_BTN_EXPORT        = 810;
constexpr int IDC_BTN_IMPORT        = 811;
constexpr int IDC_CHK_ENCRYPT_EXPORT = 812;
constexpr int IDC_CHK_FILETREE_FRAMES = 813;
#if defined(FEATURE_LICENSE_FILE)
constexpr int IDC_EDIT_FINGERPRINT = 820;
constexpr int IDC_BTN_COPY_FP      = 821;
#endif

// Client size must match what we pass to AdjustWindowRectEx in WM_INITDIALOG.
// (DLGTEMPLATE cx/cy alone are not reliable dialog-unit → pixel mapping; we set pixels explicitly.)
constexpr int kClientW = 520;   // localized labels + 64-hex fingerprint + Copy + margins
#if defined(FEATURE_LICENSE_FILE)
constexpr int kClientHBase = 424; // + optional file-tree row
#else
constexpr int kClientHBase = 328;
#endif
constexpr int kClientH = kClientHBase + 48;
constexpr int kDlgW      = kClientW + 12; // total dialog width  (adds border)
constexpr int kDlgH      = kClientH + 32; // total dialog height (adds NC area)

struct DlgState {
    media::settings::AppearanceSettings appearance;
    std::string                         display_language_at_open;
    HWND hLangCombo = nullptr;
    HWND hThemeCombo = nullptr;
    HWND hFontCombo  = nullptr;
    HWND hEncryptChk = nullptr;
#if defined(FEATURE_LICENSE_FILE)
    HWND hFingerprintEdit = nullptr;
#endif
    HWND hFiletreeFramesChk = nullptr;
    bool filetree_show_shell_frames = false;
    HBRUSH hDlgBg  = nullptr;
    HBRUSH hCtlBg  = nullptr; // `control_bg` for EDIT and combobox-embedded children
};

#if defined(FEATURE_LICENSE_FILE)
static bool copy_wide_to_clipboard(HWND owner, const std::wstring& text)
{
    if (!::OpenClipboard(owner))
        return false;
    ::EmptyClipboard();
    const size_t nchars = text.size() + 1;
    const size_t nbytes = nchars * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, nbytes);
    if (!hMem) {
        ::CloseClipboard();
        return false;
    }
    void* p = ::GlobalLock(hMem);
    if (!p) {
        ::GlobalFree(hMem);
        ::CloseClipboard();
        return false;
    }
    memcpy(p, text.c_str(), nbytes);
    ::GlobalUnlock(hMem);
    if (!::SetClipboardData(CF_UNICODETEXT, hMem)) {
        ::GlobalFree(hMem);
        ::CloseClipboard();
        return false;
    }
    ::CloseClipboard();
    return true;
}
#endif

static bool pick_save_export_path(HWND hwnd, bool encrypted_default, std::wstring& out_path)
{
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    static const wchar_t kFilter[] =
        L"JSON settings (*.json)\0*.json\0"
        L"Encrypted (*.pmsettings)\0*.pmsettings\0"
        L"All files\0*.*\0\0";
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = encrypted_default ? 2u : 1u;
    ofn.lpstrDefExt  = encrypted_default ? L"pmsettings" : L"json";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&ofn))
        return false;
    out_path.assign(buf);
    return true;
}

static bool pick_open_import_path(HWND hwnd, std::wstring& out_path)
{
    wchar_t buf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    static const wchar_t kFilter[] =
        L"JSON (*.json)\0*.json\0"
        L"Encrypted (*.pmsettings)\0*.pmsettings\0"
        L"All files\0*.*\0\0";
    ofn.lpstrFilter = kFilter;
    ofn.nFilterIndex = 1;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&ofn))
        return false;
    out_path.assign(buf);
    return true;
}

static int lang_combo_index(const std::string& code)
{
    std::string s = code;
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    if (s == "es")
        return 1;
    if (s == "de")
        return 2;
    if (s == "it")
        return 3;
    if (s == "fr")
        return 4;
    return 0;
}

static const char* lang_code_from_index(int i)
{
    static const char* k[] = {"en", "es", "de", "it", "fr"};
    if (i < 0 || i > 4)
        return "en";
    return k[i];
}

thread_local bool g_app_settings_request_process_restart = false;

static int app_settings_dlg_dpi_scale(HWND hwnd, int value)
{
    UINT dpi = 96;
    if (hwnd && ::IsWindow(hwnd))
        dpi = ::GetDpiForWindow(hwnd);
    return ::MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

static LRESULT CALLBACK app_settings_dlg_combo_subclass(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR,
    DWORD_PTR data) {
    auto* st = reinterpret_cast<DlgState*>(data);
    if (m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX || m == WM_CTLCOLORSTATIC) {
        if (st && st->hCtlBg) {
            HDC                 hdc = reinterpret_cast<HDC>(wp);
            const auto&         pal = pmui::theme_palette();
            ::SetBkColor(hdc, pal.control_bg);
            ::SetTextColor(hdc, pal.control_fg);
            return reinterpret_cast<LRESULT>(st->hCtlBg);
        }
    }
    const LRESULT r = ::DefSubclassProc(w, m, wp, lp);
    if (m == WM_NCDESTROY)
        (void)::RemoveWindowSubclass(w, app_settings_dlg_combo_subclass, 1);
    return r;
}

static void app_settings_dlg_subclass_comboboxes(HWND dlg, DlgState& st) {
    (void)::EnumChildWindows(
        dlg,
        [](HWND h, LPARAM p) -> BOOL {
            auto*   stp = reinterpret_cast<DlgState*>(p);
            wchar_t cls[32]{};
            (void)::GetClassNameW(h, cls, 32);
            if (lstrcmpiW(cls, L"ComboBox") == 0) {
                (void)::SetWindowSubclass(h, app_settings_dlg_combo_subclass, 1, reinterpret_cast<DWORD_PTR>(stp));
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&st));
}

INT_PTR CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* s = reinterpret_cast<DlgState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, lp);
        s = reinterpret_cast<DlgState*>(lp);
        auto S = [&](int v) { return app_settings_dlg_dpi_scale(hwnd, v); };
        const int clientW = S(kClientW);
        const int clientH = S(kClientH);

        {
            RECT rc{0, 0, clientW, clientH};
            const DWORD style   = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
            const DWORD exstyle = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            ::AdjustWindowRectEx(&rc, style, FALSE, exstyle);
            const int winW = rc.right - rc.left;
            const int winH = rc.bottom - rc.top;
            RECT ownerRc{};
            HWND owner = ::GetWindow(hwnd, GW_OWNER);
            if (!owner)
                owner = ::GetParent(hwnd);
            if (owner)
                ::GetWindowRect(owner, &ownerRc);
            else
                ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRc, 0);
            const int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - winW) / 2;
            const int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - winH) / 2;
            ::SetWindowPos(hwnd, nullptr, x, y, winW, winH,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }

        const pmui::app_settings_i18n::Strings& tr =
            pmui::app_settings_i18n::strings_for(s->display_language_at_open);

        HINSTANCE inst = ::GetModuleHandleW(nullptr);
        const int x0   = S(16);
        const int lblW = S(152); // de: "Anzeigesprache:", fr: long labels
        const int ctlX = x0 + lblW + S(6), ctlW = clientW - ctlX - x0, rh = S(24);
        const int row_step = rh + S(12);

        // Display language (Win32 ribbon / menu resources — SetThreadUILanguage; restart may refresh ribbon cache)
        ::CreateWindowExW(0, L"STATIC", tr.lbl_display_language,
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            x0, S(22), lblW, S(18), hwnd, nullptr, inst, nullptr);
        s->hLangCombo = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP,
            ctlX, S(18), ctlW, S(200), hwnd,
            (HMENU)(UINT_PTR)IDC_LANG_COMBO, inst, nullptr);
        for (int i = 0; i < 5; ++i)
            ::SendMessageW(s->hLangCombo, CB_ADDSTRING, 0, (LPARAM)tr.lang_combo[i]);
        {
            const int li = lang_combo_index(s->appearance.display_language);
            ::SendMessageW(s->hLangCombo, CB_SETCURSEL, (WPARAM)li, 0);
        }

        const int y2 = S(22) + row_step;

        // Theme
        ::CreateWindowExW(0, L"STATIC", tr.lbl_theme,
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            x0, y2, lblW, S(18), hwnd, nullptr, inst, nullptr);
        s->hThemeCombo = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP,
            ctlX, y2 - S(4), ctlW, S(200), hwnd,
            (HMENU)(UINT_PTR)IDC_THEME_COMBO, inst, nullptr);
        ::SendMessageW(s->hThemeCombo, CB_ADDSTRING, 0, (LPARAM)tr.theme_system);
        ::SendMessageW(s->hThemeCombo, CB_ADDSTRING, 0, (LPARAM)tr.theme_light);
        ::SendMessageW(s->hThemeCombo, CB_ADDSTRING, 0, (LPARAM)tr.theme_dark);
        ::SendMessageW(s->hThemeCombo, CB_SETCURSEL, (WPARAM)(int)s->appearance.theme, 0);

        // Font size
        const int y3 = y2 + row_step;
        ::CreateWindowExW(0, L"STATIC", tr.lbl_font_size,
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            x0, y3, lblW, S(18), hwnd, nullptr, inst, nullptr);
        s->hFontCombo = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP,
            ctlX, y3 - S(4), ctlW, S(200), hwnd,
            (HMENU)(UINT_PTR)IDC_FONT_COMBO, inst, nullptr);
        for (int i = 0; i < 5; ++i)
            ::SendMessageW(s->hFontCombo, CB_ADDSTRING, 0, (LPARAM)tr.font_sz[i]);
        int fs = s->appearance.font_size_extra_pt;
        if (fs < 0) fs = 0; if (fs > 4) fs = 4;
        ::SendMessageW(s->hFontCombo, CB_SETCURSEL, (WPARAM)fs, 0);

        const int y_ft = y3 + row_step;
        s->hFiletreeFramesChk = ::CreateWindowExW(0, L"BUTTON", tr.chk_filetree_shell_frames,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            x0, y_ft - S(4), clientW - 2 * x0, S(40), hwnd,
            (HMENU)(UINT_PTR)IDC_CHK_FILETREE_FRAMES, inst, nullptr);
        ::SendMessageW(s->hFiletreeFramesChk, BM_SETCHECK,
            s->filetree_show_shell_frames ? BST_CHECKED : BST_UNCHECKED, 0);
        const int yNote = y_ft + row_step + S(4);
        ::CreateWindowExW(0, L"STATIC",
            tr.note_paragraph,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x0, yNote, clientW - 2 * x0, S(44),
            hwnd, nullptr, inst, nullptr);

        // Provider shortcut buttons
        const int provY  = yNote + S(48);
        const int provW  = (clientW - 2 * x0 - S(8)) / 2;
        ::CreateWindowExW(0, L"BUTTON", tr.btn_ai_provider,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            x0, provY, provW, S(26), hwnd,
            (HMENU)(UINT_PTR)IDC_BTN_IMG_PROVIDER, inst, nullptr);
        ::CreateWindowExW(0, L"BUTTON", tr.btn_chat_provider,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            x0 + provW + S(8), provY, provW, S(26), hwnd,
            (HMENU)(UINT_PTR)IDC_BTN_CHAT_PROVIDER, inst, nullptr);

        const int exY = provY + S(34);
        const int exBtnW = (clientW - 2 * x0 - S(8)) / 2;
        ::CreateWindowExW(0, L"BUTTON", tr.btn_export,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            x0, exY, exBtnW, S(26), hwnd,
            (HMENU)(UINT_PTR)IDC_BTN_EXPORT, inst, nullptr);
        ::CreateWindowExW(0, L"BUTTON", tr.btn_import,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            x0 + exBtnW + S(8), exY, exBtnW, S(26), hwnd,
            (HMENU)(UINT_PTR)IDC_BTN_IMPORT, inst, nullptr);
        s->hEncryptChk = ::CreateWindowExW(0, L"BUTTON",
            tr.chk_encrypt,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            x0, exY + S(30), clientW - 2 * x0, S(36), hwnd,
            (HMENU)(UINT_PTR)IDC_CHK_ENCRYPT_EXPORT, inst, nullptr);
        ::SendMessageW(s->hEncryptChk, BM_SETCHECK, BST_CHECKED, 0);

#if defined(FEATURE_LICENSE_FILE)
        {
            const int fpY = exY + S(30) + S(36) + S(10);
            ::CreateWindowExW(0, L"STATIC",
                tr.fp_label,
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                x0, fpY, clientW - 2 * x0, S(32), hwnd, nullptr, inst, nullptr);
            const int editY = fpY + S(34);
            const int copyW = S(72);
            s->hFingerprintEdit = ::CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP,
                x0, editY, clientW - 2 * x0 - copyW - S(8), S(24), hwnd,
                (HMENU)(UINT_PTR)IDC_EDIT_FINGERPRINT, inst, nullptr);
            {
                std::string fp = media::win::machine_fingerprint_hex();
                std::wstring wfp(fp.begin(), fp.end());
                ::SetWindowTextW(s->hFingerprintEdit, wfp.c_str());
            }
            ::CreateWindowExW(0, L"BUTTON", tr.btn_copy,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                clientW - x0 - copyW, editY - S(1), copyW, S(26), hwnd,
                (HMENU)(UINT_PTR)IDC_BTN_COPY_FP, inst, nullptr);
        }
#endif

        // Save / Cancel
        const int btnY = clientH - S(40);
        const int btnW = S(76);
        ::CreateWindowExW(0, L"BUTTON", tr.btn_save,
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            clientW - 2 * (btnW + S(8)) - x0, btnY, btnW, S(26), hwnd,
            (HMENU)(UINT_PTR)IDOK, inst, nullptr);
        ::CreateWindowExW(0, L"BUTTON", tr.btn_cancel,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            clientW - btnW - x0, btnY, btnW, S(26), hwnd,
            (HMENU)(UINT_PTR)IDCANCEL, inst, nullptr);

        pmui::apply_font_to_tree(hwnd);
        {
            pmui::theme_init_from_settings();
            const auto& pal = pmui::theme_palette();
            pmui::apply_dark_titlebar(hwnd, pal.dark);
            pmui::enable_app_dark_mode(true);
            pmui::apply_window_theme_recursive(hwnd, pal.dark);
            if (s->hDlgBg)
                ::DeleteObject(s->hDlgBg);
            s->hDlgBg = ::CreateSolidBrush(pal.window_bg);
            if (s->hCtlBg)
                ::DeleteObject(s->hCtlBg);
            s->hCtlBg = ::CreateSolidBrush(pal.control_bg);
            app_settings_dlg_subclass_comboboxes(hwnd, *s);
            ::InvalidateRect(hwnd, nullptr, TRUE);
        }
        ::SetFocus(s->hLangCombo ? s->hLangCombo : s->hThemeCombo);
        return FALSE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        if (!s || !s->hDlgBg) break;
        HDC hdc = reinterpret_cast<HDC>(wp);
        const auto& pal = pmui::theme_palette();
        ::SetBkColor(hdc, pal.window_bg);
        ::SetTextColor(hdc, pal.window_fg);
        return reinterpret_cast<INT_PTR>(s->hDlgBg);
    }
    case WM_CTLCOLOREDIT: {
        if (!s || !s->hCtlBg) break;
        HDC             hdc = reinterpret_cast<HDC>(wp);
        const auto&     pal = pmui::theme_palette();
        ::SetBkColor(hdc, pal.control_bg);
        ::SetTextColor(hdc, pal.control_fg);
        return reinterpret_cast<INT_PTR>(s->hCtlBg);
    }
    case WM_CTLCOLORBTN: {
        if (!s || !s->hDlgBg) break;
        HWND    h = reinterpret_cast<HWND>(lp);
        if (!h) break;
        const int  t  = static_cast<int>(::GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK);
        HDC        hdc = reinterpret_cast<HDC>(wp);
        const auto& pal = pmui::theme_palette();
        if (t == BS_GROUPBOX) {
            (void)::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, pal.window_fg);
            ::SetBkColor(hdc, pal.window_bg);
            return reinterpret_cast<INT_PTR>(s->hDlgBg);
        }
        if (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE || t == BS_AUTORADIOBUTTON) {
            (void)::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, pal.window_fg);
            static HBRUSH s_ch = nullptr;
            static COLORREF s_c = 0xFFFFFFFF;
            if (!s_ch || s_c != pal.window_bg) {
                if (s_ch) ::DeleteObject(s_ch);
                s_ch = ::CreateSolidBrush(pal.window_bg);
                s_c  = pal.window_bg;
            }
            return reinterpret_cast<INT_PTR>(s_ch);
        }
        if (!s->hCtlBg) break;
        (void)::SetBkMode(hdc, OPAQUE);
        ::SetTextColor(hdc, pal.control_fg);
        ::SetBkColor(hdc, pal.control_bg);
        return reinterpret_cast<INT_PTR>(s->hCtlBg);
    }

    case WM_DESTROY:
        if (s) {
            if (s->hDlgBg) {
                ::DeleteObject(s->hDlgBg);
                s->hDlgBg = nullptr;
            }
            if (s->hCtlBg) {
                ::DeleteObject(s->hCtlBg);
                s->hCtlBg = nullptr;
            }
        }
        break;

    case WM_COMMAND: {
        if (!s) break;
        const int id = LOWORD(wp);
        if (id == IDCANCEL) { ::EndDialog(hwnd, IDCANCEL); return TRUE; }
        if (id == IDC_BTN_IMG_PROVIDER) {
            ShowProviderSettingsDlg(hwnd);
            return TRUE;
        }
        if (id == IDC_BTN_CHAT_PROVIDER) {
            ShowChatProviderSettingsDlg(hwnd);
            return TRUE;
        }
        if (id == IDC_BTN_EXPORT) {
            const pmui::app_settings_i18n::Strings& tr =
                pmui::app_settings_i18n::strings_for(s->display_language_at_open);
            const bool enc = ::SendMessageW(s->hEncryptChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
            std::wstring path;
            if (!pick_save_export_path(hwnd, enc, path))
                return TRUE;
            std::string err;
            if (!media::settings::export_settings_file(std::filesystem::path(path), enc, err)) {
                std::wstring msg = tr.msg_export_fail;
                msg += pmui::utf8_to_wide(err);
                ::MessageBoxW(hwnd, msg.c_str(), pm::brand::k_app_id_w, MB_ICONERROR);
            } else {
                ::MessageBoxW(hwnd, tr.msg_export_ok, pm::brand::k_app_id_w, MB_ICONINFORMATION);
            }
            return TRUE;
        }
        if (id == IDC_BTN_IMPORT) {
            const pmui::app_settings_i18n::Strings& tr =
                pmui::app_settings_i18n::strings_for(s->display_language_at_open);
            if (::MessageBoxW(hwnd, tr.msg_import_confirm, pm::brand::k_app_id_w,
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2)
                != IDYES)
                return TRUE;
            std::wstring path;
            if (!pick_open_import_path(hwnd, path))
                return TRUE;
            std::string err;
            if (!media::settings::import_settings_file(std::filesystem::path(path), err)) {
                std::wstring msg = tr.msg_import_fail;
                msg += pmui::utf8_to_wide(err);
                ::MessageBoxW(hwnd, msg.c_str(), pm::brand::k_app_id_w, MB_ICONERROR);
            } else {
                std::string e2;
                if (media::settings::load_appearance(s->appearance, e2)) {
                    int ti = (int)s->appearance.theme;
                    if (ti < 0) ti = 0;
                    if (ti > 2) ti = 2;
                    ::SendMessageW(s->hThemeCombo, CB_SETCURSEL, (WPARAM)ti, 0);
                    int fi = s->appearance.font_size_extra_pt;
                    if (fi < 0) fi = 0;
                    if (fi > 4) fi = 4;
                    ::SendMessageW(s->hFontCombo, CB_SETCURSEL, (WPARAM)fi, 0);
                    ::SendMessageW(s->hLangCombo, CB_SETCURSEL,
                        (WPARAM)lang_combo_index(s->appearance.display_language), 0);
                    s->display_language_at_open = s->appearance.display_language;
                }
                ::MessageBoxW(hwnd, tr.msg_import_ok, pm::brand::k_app_id_w, MB_ICONINFORMATION);
            }
            return TRUE;
        }
#if defined(FEATURE_LICENSE_FILE)
        if (id == IDC_BTN_COPY_FP && s->hFingerprintEdit) {
            const pmui::app_settings_i18n::Strings& tr =
                pmui::app_settings_i18n::strings_for(s->display_language_at_open);
            wchar_t buf[128]{};
            ::GetWindowTextW(s->hFingerprintEdit, buf, 128);
            if (copy_wide_to_clipboard(hwnd, buf)) {
                ::MessageBoxW(hwnd, tr.msg_fp_copied, pm::brand::k_app_id_w, MB_ICONINFORMATION);
            } else {
                ::MessageBoxW(hwnd, tr.msg_fp_copy_fail, pm::brand::k_app_id_w, MB_ICONWARNING);
            }
            return TRUE;
        }
#endif
        if (id == IDOK) {
            int li = (int)::SendMessageW(s->hLangCombo, CB_GETCURSEL, 0, 0);
            int ti = (int)::SendMessageW(s->hThemeCombo, CB_GETCURSEL, 0, 0);
            int fi = (int)::SendMessageW(s->hFontCombo,  CB_GETCURSEL, 0, 0);
            if (li < 0) li = 0;
            if (ti < 0) ti = 0;
            if (fi < 0) fi = 2;
            s->appearance.display_language   = lang_code_from_index(li);
            s->appearance.theme              = static_cast<media::settings::Theme>(ti);
            s->appearance.font_size_extra_pt = fi;

            std::string err;
            {
                const pmui::app_settings_i18n::Strings& tr =
                    pmui::app_settings_i18n::strings_for(s->display_language_at_open);
                if (!media::settings::save_appearance(s->appearance, err)) {
                    std::wstring msg = tr.msg_save_fail;
                    msg += pmui::utf8_to_wide(err);
                    ::MessageBoxW(hwnd, msg.c_str(), pm::brand::k_app_id_w, MB_ICONERROR);
                    return TRUE;
                }

                if (s->hFiletreeFramesChk) {
                    const bool frames =
                        ::SendMessageW(s->hFiletreeFramesChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    if (!media::settings::set_filetree_show_shell_frames(frames, err)) {
                        std::wstring msg = tr.msg_save_fail;
                        msg += pmui::utf8_to_wide(err);
                        ::MessageBoxW(hwnd, msg.c_str(), pm::brand::k_app_id_w, MB_ICONERROR);
                        return TRUE;
                    }
                }

                if (s->appearance.display_language != s->display_language_at_open) {
                    const std::wstring restartQ =
                        std::wstring(tr.msg_restart_pre) + std::wstring(pm::brand::k_app_id_w) + std::wstring(tr.msg_restart_post);
                    const std::wstring restartFail =
                        std::wstring(tr.msg_restart_fail_pre) + std::wstring(pm::brand::k_app_id_w)
                        + std::wstring(tr.msg_restart_fail_post);
                    if (::MessageBoxW(hwnd, restartQ.c_str(), pm::brand::k_app_id_w,
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1)
                        == IDYES) {
                        if (pmui::restart_current_process()) {
                            g_app_settings_request_process_restart = true;
                            // New instance will load settings and SetThreadUILanguage; exit this UI loop.
                            ::PostQuitMessage(0);
                        } else {
                            ::MessageBoxW(hwnd, restartFail.c_str(), pm::brand::k_app_id_w, MB_ICONWARNING);
                        }
                    }
                }
            }

            ::EndDialog(hwnd, IDOK);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        ::EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

} // namespace

bool ShowAppSettingsDlg(HWND parent, bool* out_restarting)
{
    g_app_settings_request_process_restart = false;
    if (out_restarting)
        *out_restarting = false;

    DlgState st;
    std::string err;
    media::settings::load_appearance(st.appearance, err);
    st.display_language_at_open = st.appearance.display_language;
    {
        media::settings::WindowLayout wl{};
        std::string wlerr;
        if (media::settings::load_window_layout(wl, wlerr))
            st.filetree_show_shell_frames = wl.filetree_show_shell_frames;
    }

    alignas(DWORD) BYTE buf[2048]{};
    auto* dlg = reinterpret_cast<DLGTEMPLATE*>(buf);
    dlg->style = DS_MODALFRAME | DS_CENTER | DS_SETFONT
               | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dlg->cdit  = 0;
    dlg->x = 0; dlg->y = 0;
    dlg->cx = (SHORT)(kDlgW * 4 / 7);
    dlg->cy = (SHORT)(kDlgH * 8 / 15);
    WORD* p = reinterpret_cast<WORD*>(dlg + 1);
    *p++ = 0; *p++ = 0;
    {
        const pmui::app_settings_i18n::Strings& ts =
            pmui::app_settings_i18n::strings_for(st.appearance.display_language);
        const size_t n = std::wcslen(ts.window_title) + 1;
        memcpy(p, ts.window_title, n * sizeof(wchar_t));
        p += n * sizeof(wchar_t) / sizeof(WORD);
    }
    *p++ = 9;
    const wchar_t font[] = L"Segoe UI";
    memcpy(p, font, sizeof(font));
    p += sizeof(font) / sizeof(WORD);

    INT_PTR r = ::DialogBoxIndirectParamW(::GetModuleHandleW(nullptr), dlg,
        parent, Proc, reinterpret_cast<LPARAM>(&st));
    if (out_restarting)
        *out_restarting = g_app_settings_request_process_restart;
    return r == IDOK;
}
