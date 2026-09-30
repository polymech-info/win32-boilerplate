#include "stdafx.h"

#include "PixlwizSharePostDlg.h"

#include "Resource.h"

#include "helpers/default_shell.hpp"
#include "helpers/theme.hpp"

#include "helpers/ui_font.hpp"
#include "helpers/ui_dialog_relayout.hpp"

#include <algorithm>
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")

namespace {

struct PixlwizShareDlgCtx {

  PixlwizSharePostFields *fields = nullptr;
};

struct PixlwizShareDlgState {

  PixlwizSharePostFields *fields = nullptr;

  HBRUSH hDlgBg = nullptr;

  HBRUSH hCtlBg = nullptr;
};

static void sync_private_and_feed_controls(HWND h)

{

  const bool priv =
      ::IsDlgButtonChecked(h, IDC_PIXLWIZ_SHARE_PRIVATE) == BST_CHECKED;

  HWND hFeeds = ::GetDlgItem(h, IDC_PIXLWIZ_SHARE_IN_FEEDS);

  if (!hFeeds)

    return;

  if (priv) {

    ::SendMessageW(hFeeds, BM_SETCHECK, BST_UNCHECKED, 0);

    ::EnableWindow(hFeeds, FALSE);

  } else {

    ::EnableWindow(hFeeds, TRUE);
  }
}

static void apply_visibility_to_controls(HWND h, const std::string &vis)

{

  const bool priv = (vis == "private");

  ::SendDlgItemMessageW(h, IDC_PIXLWIZ_SHARE_PRIVATE, BM_SETCHECK,
                        priv ? BST_CHECKED : BST_UNCHECKED, 0);

  HWND hFeeds = ::GetDlgItem(h, IDC_PIXLWIZ_SHARE_IN_FEEDS);

  if (!hFeeds)

    return;

  if (priv) {

    ::SendMessageW(hFeeds, BM_SETCHECK, BST_UNCHECKED, 0);

    ::EnableWindow(hFeeds, FALSE);

  } else {

    ::EnableWindow(hFeeds, TRUE);

    const bool inFeeds = (vis != "listed");

    ::SendMessageW(hFeeds, BM_SETCHECK, inFeeds ? BST_CHECKED : BST_UNCHECKED,
                   0);
  }
}

INT_PTR CALLBACK PixlwizSharePostDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)

{

  switch (msg) {

  case WM_INITDIALOG: {

    auto *inCtx = reinterpret_cast<PixlwizShareDlgCtx *>(lp);

    if (!inCtx || !inCtx->fields)

      return TRUE;

    auto *st = new PixlwizShareDlgState{};

    st->fields = inCtx->fields;

    ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

    const HFONT hf = pmui::ui_font();

    ::EnumChildWindows(
        h,

        [](HWND c, LPARAM f) -> BOOL {
          ::SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);

          return TRUE;
        },

        (LPARAM)hf);

    pmui::relayout_dialog_children_vertical_stack_for_ui_font(h);

    pmui::theme_init_from_settings();

    {

      const auto &pal = pmui::theme_palette();

      pmui::apply_dark_titlebar(h, pal.dark);

      pmui::enable_app_dark_mode(true);

      pmui::apply_window_theme_recursive(h, pal.dark);

      if (st->hDlgBg)

        ::DeleteObject(st->hDlgBg);

      st->hDlgBg = ::CreateSolidBrush(pal.window_bg);

      if (st->hCtlBg)

        ::DeleteObject(st->hCtlBg);

      st->hCtlBg = ::CreateSolidBrush(pal.control_bg);
    }

    ::InvalidateRect(h, nullptr, TRUE);

    PixlwizSharePostFields &f = *st->fields;

    if (!f.title.empty())

      ::SetDlgItemTextW(h, IDC_PIXLWIZ_SHARE_TITLE, f.title.c_str());

    if (!f.description.empty())

      ::SetDlgItemTextW(h, IDC_PIXLWIZ_SHARE_DESC, f.description.c_str());

    apply_visibility_to_controls(h, f.visibility);

    ::SetFocus(::GetDlgItem(h, IDC_PIXLWIZ_SHARE_TITLE));

    return FALSE;
  }

  case WM_CTLCOLORDLG:

  case WM_CTLCOLORSTATIC: {

    auto *st = reinterpret_cast<PixlwizShareDlgState *>(
        ::GetWindowLongPtrW(h, GWLP_USERDATA));

    if (!st || !st->hDlgBg)

      break;

    HDC hdc = reinterpret_cast<HDC>(wp);

    const auto &pal = pmui::theme_palette();

    ::SetBkColor(hdc, pal.window_bg);

    ::SetTextColor(hdc, pal.window_fg);

    return reinterpret_cast<INT_PTR>(st->hDlgBg);
  }

  case WM_CTLCOLOREDIT: {

    auto *st = reinterpret_cast<PixlwizShareDlgState *>(
        ::GetWindowLongPtrW(h, GWLP_USERDATA));

    if (!st || !st->hCtlBg)

      break;

    HDC hdc = reinterpret_cast<HDC>(wp);

    const auto &pal = pmui::theme_palette();

    ::SetBkColor(hdc, pal.control_bg);

    ::SetTextColor(hdc, pal.control_fg);

    return reinterpret_cast<INT_PTR>(st->hCtlBg);
  }

  case WM_CTLCOLORBTN: {

    auto *st = reinterpret_cast<PixlwizShareDlgState *>(
        ::GetWindowLongPtrW(h, GWLP_USERDATA));

    if (!st || !st->hDlgBg)

      break;

    HWND hCtl = reinterpret_cast<HWND>(lp);

    if (!hCtl)

      break;

    const int t =
        static_cast<int>(::GetWindowLongW(hCtl, GWL_STYLE) & BS_TYPEMASK);

    HDC hdc = reinterpret_cast<HDC>(wp);

    const auto &pal = pmui::theme_palette();

    if (t == BS_GROUPBOX) {

      (void)::SetBkMode(hdc, TRANSPARENT);

      ::SetTextColor(hdc, pal.window_fg);

      ::SetBkColor(hdc, pal.window_bg);

      return reinterpret_cast<INT_PTR>(st->hDlgBg);
    }

    if (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE || t == BS_AUTORADIOBUTTON) {

      (void)::SetBkMode(hdc, TRANSPARENT);

      ::SetTextColor(hdc, pal.window_fg);

      return reinterpret_cast<INT_PTR>(st->hDlgBg);
    }

    if (!st->hCtlBg)

      break;

    (void)::SetBkMode(hdc, OPAQUE);

    ::SetTextColor(hdc, pal.control_fg);

    ::SetBkColor(hdc, pal.control_bg);

    return reinterpret_cast<INT_PTR>(st->hCtlBg);
  }

  case WM_DESTROY: {

    auto *st = reinterpret_cast<PixlwizShareDlgState *>(
        ::GetWindowLongPtrW(h, GWLP_USERDATA));

    if (st) {

      if (st->hDlgBg) {

        ::DeleteObject(st->hDlgBg);

        st->hDlgBg = nullptr;
      }

      if (st->hCtlBg) {

        ::DeleteObject(st->hCtlBg);

        st->hCtlBg = nullptr;
      }

      delete st;

      ::SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    }

    break;
  }

  case WM_COMMAND: {

    const int id = LOWORD(wp);

    const int code = HIWORD(wp);

    if (code == BN_CLICKED && id == IDC_PIXLWIZ_SHARE_PRIVATE) {

      sync_private_and_feed_controls(h);

      return TRUE;
    }

    if (id != IDOK && id != IDCANCEL)

      return FALSE;

    if (id == IDCANCEL) {

      ::EndDialog(h, IDCANCEL);

      return TRUE;
    }

    auto *st = reinterpret_cast<PixlwizShareDlgState *>(
        ::GetWindowLongPtrW(h, GWLP_USERDATA));

    if (!st || !st->fields) {

      ::EndDialog(h, IDCANCEL);

      return TRUE;
    }

    PixlwizSharePostFields *fields = st->fields;

    wchar_t title[512]{};

    wchar_t desc[32768]{};

    ::GetDlgItemTextW(h, IDC_PIXLWIZ_SHARE_TITLE, title, 512);

    ::GetDlgItemTextW(h, IDC_PIXLWIZ_SHARE_DESC, desc, 32768);

    fields->title = title;

    fields->description = desc;

    const bool is_private =
        ::IsDlgButtonChecked(h, IDC_PIXLWIZ_SHARE_PRIVATE) == BST_CHECKED;

    if (is_private)

      fields->visibility = "private";

    else if (::IsDlgButtonChecked(h, IDC_PIXLWIZ_SHARE_IN_FEEDS) == BST_CHECKED)

      fields->visibility = "public";

    else

      fields->visibility = "listed";

    ::EndDialog(h, IDOK);

    return TRUE;
  }

  case WM_CLOSE:

    ::EndDialog(h, IDCANCEL);

    return TRUE;

  default:

    return FALSE;
  }

  return FALSE;
}

struct PixlwizSuccessDlgInitCtx {
  const std::wstring *url_w{};
  size_t              pic_count = 0;
};

struct PixlwizSuccessDlgState {
  std::wstring url_w;
  size_t       pic_count = 0;
  HBRUSH       hDlgBg = nullptr;
  HBRUSH       hCtlBg = nullptr;
};

INT_PTR CALLBACK PixlwizShareSuccessDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg) {
  case WM_INITDIALOG: {
    auto *in = reinterpret_cast<PixlwizSuccessDlgInitCtx *>(lp);
    if (!in || !in->url_w)
      return TRUE;
    auto *st = new PixlwizSuccessDlgState{};
    st->url_w    = *in->url_w;
    st->pic_count = in->pic_count;
    ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

    const HFONT hf = pmui::ui_font();
    ::EnumChildWindows(
        h,
        [](HWND c, LPARAM f) -> BOOL {
          ::SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
          return TRUE;
        },
        (LPARAM)hf);

    pmui::relayout_dialog_children_vertical_stack_for_ui_font(h);

    pmui::theme_init_from_settings();
    {
      const auto &pal = pmui::theme_palette();
      pmui::apply_dark_titlebar(h, pal.dark);
      pmui::enable_app_dark_mode(true);
      pmui::apply_window_theme_recursive(h, pal.dark);
      if (st->hDlgBg)
        ::DeleteObject(st->hDlgBg);
      st->hDlgBg = ::CreateSolidBrush(pal.window_bg);
      if (st->hCtlBg)
        ::DeleteObject(st->hCtlBg);
      st->hCtlBg = ::CreateSolidBrush(pal.control_bg);
    }
    ::InvalidateRect(h, nullptr, TRUE);

    {
      std::wstring line = L"Pictures attached: ";
      line += std::to_wstring(st->pic_count);
      ::SetDlgItemTextW(h, IDC_PIXLWIZ_SUCCESS_STATUS, line.c_str());
    }

    if (HWND ho = ::GetDlgItem(h, IDC_PIXLWIZ_SUCCESS_OPEN))
      ::SetFocus(ho);
    return FALSE;
  }
  case WM_CTLCOLORDLG:
  case WM_CTLCOLORSTATIC: {
    auto *st = reinterpret_cast<PixlwizSuccessDlgState *>(
        ::GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!st || !st->hDlgBg)
      break;
    HDC                 hdc = reinterpret_cast<HDC>(wp);
    const auto         &pal = pmui::theme_palette();
    ::SetBkColor(hdc, pal.window_bg);
    ::SetTextColor(hdc, pal.window_fg);
    return reinterpret_cast<INT_PTR>(st->hDlgBg);
  }
  case WM_CTLCOLORBTN: {
    auto *st = reinterpret_cast<PixlwizSuccessDlgState *>(
        ::GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!st || !st->hCtlBg)
      break;
    HWND hCtl = reinterpret_cast<HWND>(lp);
    if (!hCtl)
      break;
    HDC         hdc = reinterpret_cast<HDC>(wp);
    const auto &pal = pmui::theme_palette();
    (void)::SetBkMode(hdc, OPAQUE);
    ::SetTextColor(hdc, pal.control_fg);
    ::SetBkColor(hdc, pal.control_bg);
    return reinterpret_cast<INT_PTR>(st->hCtlBg);
  }
  case WM_DESTROY: {
    auto *st = reinterpret_cast<PixlwizSuccessDlgState *>(
        ::GetWindowLongPtrW(h, GWLP_USERDATA));
    if (st) {
      if (st->hDlgBg) {
        ::DeleteObject(st->hDlgBg);
        st->hDlgBg = nullptr;
      }
      if (st->hCtlBg) {
        ::DeleteObject(st->hCtlBg);
        st->hCtlBg = nullptr;
      }
      delete st;
      ::SetWindowLongPtrW(h, GWLP_USERDATA, 0);
    }
    break;
  }
  case WM_COMMAND: {
    const int id = LOWORD(wp);
    if (id == IDCANCEL) {
      ::EndDialog(h, IDCANCEL);
      return TRUE;
    }
    if (id == IDC_PIXLWIZ_SUCCESS_OPEN) {
      auto *st = reinterpret_cast<PixlwizSuccessDlgState *>(
          ::GetWindowLongPtrW(h, GWLP_USERDATA));
      if (st && !st->url_w.empty())
        pmui::shell::open_url(h, st->url_w);
      return TRUE;
    }
    break;
  }
  case WM_CLOSE:
    ::EndDialog(h, IDCANCEL);
    return TRUE;
  default:
    return FALSE;
  }
  return FALSE;
}

} // namespace

bool RunPixlwizSharePostDialog(HWND parent, PixlwizSharePostFields &fields)

{

  PixlwizShareDlgCtx ctx{&fields};

  const INT_PTR r = ::DialogBoxParamW(
      ::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PIXLWIZ_SHARE_POST),
      parent,

      PixlwizSharePostDlgProc, reinterpret_cast<LPARAM>(&ctx));

  return r == IDOK;
}

void RunPixlwizShareSuccessDialog(HWND parent, const std::wstring &url_w,
                                  size_t picture_count)
{
  PixlwizSuccessDlgInitCtx ctx{&url_w, picture_count};
  (void)::DialogBoxParamW(::GetModuleHandleW(nullptr),
                           MAKEINTRESOURCEW(IDD_PIXLWIZ_SHARE_SUCCESS), parent,
                           PixlwizShareSuccessDlgProc,
                           reinterpret_cast<LPARAM>(&ctx));
}
