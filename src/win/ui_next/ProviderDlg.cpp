#include "stdafx.h"
#include "constants.hpp"
#include "ProviderDlg.h"
#include "Resource.h"
#include "win/settings_store.hpp"
#include "helpers/ui_font.hpp"
#include "helpers/provider_dlg_i18n.hpp"
#include "helpers/theme.hpp"

#include <commctrl.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "Comctl32.lib")

// Forward aliases so existing code that used prov_* names still compiles.
static const auto& prov_wide_to_utf8 = pmui::wide_to_utf8;
static const auto& prov_utf8_to_wide = pmui::utf8_to_wide;

// ============================================================
// Control ID layout
// Each provider row uses a block of 5 IDs:
//   base + 0  = password EDIT (API key)
//   base + 1  = show/hide BUTTON
//   base + 2  = (unused/reserved)
//   base + 3  = base-URL EDIT
//   base + 4  = (unused/reserved)
// Save / Cancel: IDOK / IDCANCEL
// ============================================================
static constexpr int IDC_PROVDLG_ROW0   = 760;   // Google
static constexpr int IDC_PROVDLG_ROW1   = 765;   // OpenAI
static constexpr int IDC_PROVDLG_ROW2   = 770;   // Replicate
static constexpr int IDC_PROVDLG_STRIDE = 5;     // IDs per provider

static constexpr int kProviderCount = 6;
/// Renders inside the virtual document (`hFormContent`); not the same as the dialog client height.
static constexpr int IDC_PROVDLG_SCROLL = 840;

static LRESULT CALLBACK provider_dlg_scroll_host_WndProc(HWND h, UINT m, WPARAM w, LPARAM l);

struct ProviderRow {
    HWND hKeyEdit{};
    HWND hShowBtn{};
    HWND hUrlEdit{};
    bool keyVisible = false;
};

struct ProviderDlgScrollChild {
    HWND h{};
    RECT logical{}; // coordinates in the virtual provider document, before applying scroll_y
};

struct ProviderDlgGroupFrame {
    RECT         logical{}; // coordinates in the virtual provider document
    std::wstring title;
};

struct DlgState {
    media::settings::ProviderMap providers;
    std::string               display_language{"en"};
    ProviderRow               rows[kProviderCount];
    HBRUSH                    hDlgBg{};
    /// Filled with `control_bg` — for `WM_CTLCOLOREDIT` (single-line EDIT) and combobox-embedded
    /// child controls, which do not notify the dialog directly.
    HBRUSH                    hCtlBg{};
    /// Vertical scroll: tall provider rows + metas; OK/Cancel stay in a fixed footer.
    HWND hScrollPane{};
    HWND hFormContent{}; // legacy alias for hScrollPane; controls are direct children of hScrollPane
    int  scroll_y = 0;
    bool scroll_children_captured = false;
    std::vector<ProviderDlgScrollChild> scroll_children;
    std::vector<ProviderDlgGroupFrame>  group_frames;
};

// ============================================================
// Layout constants (all in pixels)
// ============================================================

// Controls are positioned in client-area pixels.  In-memory `DLGTEMPLATE` sizes
// (`cx`/`cy`) are the dialog *client* in dialog units (DS_SETFONT) — not full
// window. Use kClientW/kClientH for both layout and the DU → template conversion
// in `ShowProviderSettingsDlg` (rough 96-DPI heuristics: *4/7, *8/15).
static constexpr int kClientW = 620;   // client width  (controls stay within this)
static constexpr int kX0         = 12;
static constexpr int kLblW       = 124;  // label width for API key / Base URL
static constexpr int kEditW      = 320;
static constexpr int kShowBtnW   = 56;   // Show / hide API key
static constexpr int kEH           = 22;
static constexpr int kGap          = 6;     // horizontal (e.g. between adjacent controls)
static constexpr int kRowVGap      = 8;     // vertical padding between stacked rows within a group
// Left edge of text fields: inner padding (6) + label + gap + 6, aligned for all rows.
static constexpr int kFieldX     = kX0 + 6 + kLblW + kGap + 6; // 148
static constexpr int kActionsW   = kShowBtnW; // right action column width
static constexpr int kTotalCtrlW = kEditW + kGap + kActionsW;
static constexpr int kActionX    = kFieldX + kEditW + kGap;
// Base URL: fit inside the client
static constexpr int kUrlW       = kEditW;
// BS_GROUPBOX height per provider: `LayoutProviderRow` uses `rowH - 6` for the frame.
// All providers: API key row + base URL row only.
static constexpr int kGroupH_Standard  = 88;
// First provider row y
static constexpr int kFirstProviderY   = 48;
/// Space between provider group boxes (in addition to each `kGroupH_*` block).
static constexpr int kGroupVGap = 10;
/// Inset from the scroll host’s right so painted frames and the path note sit clear of the v‑scrollbar.
static constexpr int kScrollHostRightPad = 24;
/// Pixels: raise section title text/eraser so it sits on the top frame line (GDI `Rectangle` top is `r.top`).
static constexpr int kGroupTitleRise = 2;
// Note: path text at bottom of scroll (above Save/Cancel which live in a fixed client band)
static constexpr int kNoteH        = 56;
static constexpr int kAfterNoteG   = 16; // between scroll area and Save/Cancel
static constexpr int kBtnRowH      = 30; // min button height
static constexpr int kBottomM      = 18; // client bottom margin under buttons
// Wider for es/de/…; gap between (no overlap, esp. on high-DPI)
static constexpr int kBtnMinSaveW  = 108;
static constexpr int kBtnMinCancelW = 116;
static constexpr int kBtnBetween   = 18; // between Save and Cancel
static constexpr int kBtnRPad      = 16;  // from client right edge
// Order = known_providers: 6 providers, each kGroupH_Standard tall; 5 gaps between them.
static constexpr int kNoteY = kFirstProviderY + kProviderCount * kGroupH_Standard
    + (kProviderCount - 1) * kGroupVGap + 8;
/// Pixels: virtual document in `hFormContent` = active + rows + path note
static constexpr int kFormContentH  = kNoteY + kNoteH;
// Visible area for the scroll (rest of client is buttons + margin)
static constexpr int kScrollViewportH = 440;
static_assert(kFormContentH > kScrollViewportH, "content scrolls: form taller than viewport");
static constexpr int kBtnY0   = kScrollViewportH + kAfterNoteG;
static constexpr int kClientH = kBtnY0 + kBtnRowH + kBottomM; // dialog client height (Save/Cancel footer)

// ============================================================
// Dialog proc helpers
// ============================================================

static int provider_dlg_dpi_scale(HWND hwnd, int value)
{
    UINT dpi = 96;
    if (hwnd && ::IsWindow(hwnd))
        dpi = ::GetDpiForWindow(hwnd);
    return ::MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

static int provider_dlg_client_width(HWND hwnd)
{
    RECT rc{};
    if (hwnd && ::IsWindow(hwnd) && ::GetClientRect(hwnd, &rc))
        return (std::max)(1, (int)(rc.right - rc.left));
    return provider_dlg_dpi_scale(hwnd, kClientW);
}

static int provider_dlg_label_w(HWND hwnd)
{
    return (std::max)(provider_dlg_dpi_scale(hwnd, kLblW), provider_dlg_dpi_scale(hwnd, 132));
}

static int provider_dlg_field_x(HWND hwnd)
{
    return provider_dlg_dpi_scale(hwnd, kX0 + 6) + provider_dlg_label_w(hwnd) + provider_dlg_dpi_scale(hwnd, kGap + 6);
}

static int provider_dlg_total_ctrl_w(HWND hwnd)
{
    const int w = provider_dlg_client_width(hwnd) - provider_dlg_field_x(hwnd)
        - provider_dlg_dpi_scale(hwnd, kX0 + kScrollHostRightPad);
    return (std::max)(provider_dlg_dpi_scale(hwnd, 260), w);
}


/// Win32 `STATIC` does not deliver a working `WS_VSCROLL` (no `WM_VSCROLL` / wheel to the parent).
/// Use a plain registered class. Scroll is handled in `provider_dlg_scroll_host_WndProc` (comctl
/// `SetWindowSubclass` is unreliable for the pair `SetWindowPos` + child STATIC content).

static void provider_dlg_register_scroll_host_class(HINSTANCE hInst) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wcx{};
    wcx.cbSize     = sizeof(wcx);
    wcx.lpfnWndProc   = provider_dlg_scroll_host_WndProc;
    wcx.hInstance  = hInst;
    wcx.hCursor    = ::LoadCursorW(nullptr, IDC_ARROW);
    wcx.hbrBackground = nullptr; // WndProc `WM_ERASEBKGND` fills with `window_bg`
    wcx.lpszClassName = L"PM_ProviderDlgScrollHost";
    if (!::RegisterClassExW(&wcx) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        spdlog::error("ProviderDlg: RegisterClassExW(PM_ProviderDlgScrollHost) failed ({})", (int)::GetLastError());
    }
    registered = true;
}

static std::string provider_dlg_get_text(HWND h) {
    wchar_t b[2048]{};
    ::GetWindowTextW(h, b, (int)std::size(b));
    return prov_wide_to_utf8(b);
}


static void LayoutProviderRow(HWND parent, int yBase, int rowIdx, const media::settings::ProviderDefaults& def,
                              const media::settings::ProviderEntry& entry, ProviderRow& row, DlgState* st,
                              const pmui::provider_dlg_i18n::Strings& tr)
{
    HINSTANCE inst = ::GetModuleHandleW(nullptr);
    const int baseID = IDC_PROVDLG_ROW0 + rowIdx * IDC_PROVDLG_STRIDE;
    auto S = [&](int v) { return provider_dlg_dpi_scale(parent, v); };
    const int clientW = provider_dlg_client_width(parent);
    const int x0 = S(kX0);
    const int lblW = provider_dlg_label_w(parent);
    const int showBtnW = S(kShowBtnW);
    const int eh = S(kEH);
    const int gap = S(kGap);
    const int rowVGap = S(kRowVGap);
    const int fieldX = provider_dlg_field_x(parent);
    const int actionsW = showBtnW;
    const int totalCtrlW = provider_dlg_total_ctrl_w(parent);
    const int editW = (std::max)(S(120), totalCtrlW - gap - actionsW);
    const int actionX = fieldX + editW + gap;
    const int urlW = totalCtrlW;

    // Group frame (painted by the scroll host; BS_GROUPBOX is intentionally avoided because
    // it is a transparent BUTTON child that repaints poorly while clipped/scrolled).
    std::wstring groupTitle = prov_utf8_to_wide(def.display_name);
    const int    rowH      = S(kGroupH_Standard);
    if (st) {
        ProviderDlgGroupFrame gf{};
        // Extend group frame to cover controls (which reach to clientW - S(kScrollHostRightPad))
        gf.logical = { (LONG)x0, (LONG)yBase, (LONG)(clientW - S(kScrollHostRightPad)),
            (LONG)(yBase + rowH - S(6)) };
        gf.title   = groupTitle;
        st->group_frames.push_back(std::move(gf));
    }

    int y = yBase + S(20);

    // API key row
    ::CreateWindowExW(0, L"STATIC", tr.lbl_api_key,
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        x0 + S(6), y, lblW, eh, parent, nullptr, inst, nullptr);

    row.hKeyEdit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        prov_utf8_to_wide(entry.api_key).c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
        fieldX, y, editW, eh, parent,
        (HMENU)(UINT_PTR)(baseID + 0), inst, nullptr);

    row.hShowBtn = ::CreateWindowExW(0, L"BUTTON", tr.btn_show,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        actionX, y, showBtnW, eh, parent,
        (HMENU)(UINT_PTR)(baseID + 1), inst, nullptr);
    row.keyVisible = false;

    y += eh + rowVGap;

    // Base URL row
    ::CreateWindowExW(0, L"STATIC", tr.lbl_base_url,
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        x0 + S(6), y, lblW, eh, parent, nullptr, inst, nullptr);

    row.hUrlEdit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        prov_utf8_to_wide(entry.base_url).c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        fieldX, y, urlW, eh, parent,
        (HMENU)(UINT_PTR)(baseID + 3), inst, nullptr);
}

static void ReadProviderRow(int rowIdx, DlgState& st, media::settings::ProviderEntry& out)
{
    const ProviderRow& row = st.rows[rowIdx];
    wchar_t buf[2048]{};
    ::GetWindowTextW(row.hKeyEdit, buf, (int)std::size(buf));
    out.api_key = prov_wide_to_utf8(buf);
    out.default_model.clear();
    buf[0] = L'\0';
    ::GetWindowTextW(row.hUrlEdit, buf, (int)std::size(buf));
    out.base_url = prov_wide_to_utf8(buf);
}

static void ToggleKeyVisibility(ProviderRow& row, const pmui::provider_dlg_i18n::Strings& tr)
{
    row.keyVisible = !row.keyVisible;
    // ES_PASSWORD is toggled by removing / adding it via SetWindowLong
    LONG style = ::GetWindowLongW(row.hKeyEdit, GWL_STYLE);
    if (row.keyVisible) {
        style &= ~ES_PASSWORD;
        ::SetWindowLongW(row.hKeyEdit, GWL_STYLE, style);
        // EM_SETPASSWORDCHAR 0 = no password char
        ::SendMessageW(row.hKeyEdit, EM_SETPASSWORDCHAR, 0, 0);
        ::SetWindowTextW(row.hShowBtn, tr.btn_hide);
    } else {
        style |= ES_PASSWORD;
        ::SetWindowLongW(row.hKeyEdit, GWL_STYLE, style);
        ::SendMessageW(row.hKeyEdit, EM_SETPASSWORDCHAR, (WPARAM)L'\u2022', 0);
        ::SetWindowTextW(row.hShowBtn, tr.btn_show);
    }
    ::InvalidateRect(row.hKeyEdit, nullptr, TRUE);
    ::UpdateWindow(row.hKeyEdit);
}

// `apply_window_theme_recursive` keeps single-line EDIT and COMBO children on a classic path so
// GDI can honor parent colors; the combo's *parent* is the COMBOBOX, so we subclass each combo
// to supply `control_bg` / `control_fg` the same as `WM_CTLCOLOREDIT` on the dialog.
static LRESULT CALLBACK provider_dlg_combo_subclass(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR /*id*/,
    DWORD_PTR data) {
    auto* st = reinterpret_cast<DlgState*>(data);
    if (m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX || m == WM_CTLCOLORSTATIC) {
        if (st && st->hCtlBg) {
            HDC hdc = reinterpret_cast<HDC>(wp);
            const auto& p = pmui::theme_palette();
            ::SetBkColor(hdc, p.control_bg);
            ::SetTextColor(hdc, p.control_fg);
            return reinterpret_cast<LRESULT>(st->hCtlBg);
        }
    }
    const LRESULT r = ::DefSubclassProc(w, m, wp, lp);
    if (m == WM_NCDESTROY)
        (void)::RemoveWindowSubclass(w, provider_dlg_combo_subclass, 1);
    return r;
}

static void provider_dlg_subclass_comboboxes_in(HWND root, DlgState& st) {
    (void)::EnumChildWindows(
        root,
        [](HWND h, LPARAM p) -> BOOL {
            auto*   stp = reinterpret_cast<DlgState*>(p);
            wchar_t cls[32]{};
            (void)::GetClassNameW(h, cls, static_cast<int>(sizeof(cls) / sizeof(cls[0])));
            if (lstrcmpiW(cls, L"ComboBox") == 0) {
                (void)::SetWindowSubclass(h, provider_dlg_combo_subclass, 1, reinterpret_cast<DWORD_PTR>(stp));
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&st));
}

static void provider_dlg_set_font_tree(HWND root, HFONT f) {
    (void)::SendMessageW(root, WM_SETFONT, (WPARAM)f, TRUE);
    (void)::EnumChildWindows(
        root,
        [](HWND h, LPARAM p) -> BOOL {
            (void)::SendMessageW(h, WM_SETFONT, (WPARAM)p, TRUE);
            return TRUE;
        },
        (LPARAM)f);
}

static void provider_dlg_enable_clip_siblings(HWND root) {
    if (!root) return;
    (void)::EnumChildWindows(
        root,
        [](HWND h, LPARAM) -> BOOL {
            const LONG_PTR style = ::GetWindowLongPtrW(h, GWL_STYLE);
            (void)::SetWindowLongPtrW(h, GWL_STYLE, style | WS_CLIPSIBLINGS);
            return TRUE;
        },
        0);
}

struct ProviderDlgCaptureCtx {
    DlgState* st{};
    HWND      parent{};
};

static BOOL CALLBACK provider_dlg_capture_immediate_child(HWND child, LPARAM p) {
    auto* ctx = reinterpret_cast<ProviderDlgCaptureCtx*>(p);
    if (!ctx || !ctx->st || ::GetParent(child) != ctx->parent) return TRUE;
    RECT r{};
    (void)::GetWindowRect(child, &r);
    (void)::MapWindowPoints(nullptr, ctx->parent, reinterpret_cast<LPPOINT>(&r), 2);
    // If capture ever happens after a non-zero scroll, convert current viewport coords back to
    // virtual document coords. In normal WM_INITDIALOG capture happens at scroll_y == 0.
    (void)::OffsetRect(&r, 0, ctx->st->scroll_y);
    ctx->st->scroll_children.push_back(ProviderDlgScrollChild{ child, r });
    return TRUE;
}

static void provider_dlg_capture_scroll_children(HWND hScroll, DlgState& st) {
    if (!hScroll || st.scroll_children_captured) return;
    st.scroll_children.clear();
    ProviderDlgCaptureCtx ctx{ &st, hScroll };
    (void)::EnumChildWindows(hScroll, provider_dlg_capture_immediate_child, reinterpret_cast<LPARAM>(&ctx));
    st.scroll_children_captured = true;
}

static void provider_dlg_capture_scroll_children_now(HWND hScroll, DlgState& st) {
    if (!hScroll) return;
    st.scroll_children_captured = false;
    st.scroll_children.clear();
    provider_dlg_capture_scroll_children(hScroll, st);
}

static void provider_dlg_paint_group_frames(HWND hScroll, DlgState& st, HDC hdc) {
    if (!hScroll || !hdc) return;
    const auto& pal = pmui::theme_palette();
    const COLORREF frame_c = pal.dark ? RGB(
        (GetRValue(pal.window_bg) * 3 + GetRValue(pal.window_fg)) / 4,
        (GetGValue(pal.window_bg) * 3 + GetGValue(pal.window_fg)) / 4,
        (GetBValue(pal.window_bg) * 3 + GetBValue(pal.window_fg)) / 4)
        : RGB(160, 160, 160);

    RECT client{};
    (void)::GetClientRect(hScroll, &client);
    HPEN pen = ::CreatePen(PS_SOLID, 1, frame_c);
    HGDIOBJ old_pen = pen ? ::SelectObject(hdc, pen) : nullptr;
    HGDIOBJ old_brush = ::SelectObject(hdc, ::GetStockObject(HOLLOW_BRUSH));
    HFONT font = (HFONT)::SendMessageW(hScroll, WM_GETFONT, 0, 0);
    HGDIOBJ old_font = font ? ::SelectObject(hdc, font) : nullptr;
    const int old_bkmode = ::SetBkMode(hdc, OPAQUE);
    const COLORREF old_bk = ::SetBkColor(hdc, pal.window_bg);
    const COLORREF old_tx = ::SetTextColor(hdc, pal.window_fg);

    for (const auto& gf : st.group_frames) {
        RECT r = gf.logical;
        (void)::OffsetRect(&r, 0, -st.scroll_y);
        RECT inter{};
        if (!::IntersectRect(&inter, &r, &client)) continue;
        (void)::Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        if (!gf.title.empty()) {
            const int title_top = (int)r.top - provider_dlg_dpi_scale(hScroll, kGroupTitleRise);
            SIZE sz{};
            (void)::GetTextExtentPoint32W(hdc, gf.title.c_str(), (int)gf.title.size(), &sz);
            RECT bg{ r.left + provider_dlg_dpi_scale(hScroll, 10), title_top,
                r.left + provider_dlg_dpi_scale(hScroll, 18) + sz.cx,
                title_top + provider_dlg_dpi_scale(hScroll, 18) };
            if (st.hDlgBg) {
                (void)::FillRect(hdc, &bg, st.hDlgBg);
            } else {
                HBRUSH b = ::CreateSolidBrush(pal.window_bg);
                (void)::FillRect(hdc, &bg, b);
                (void)::DeleteObject(b);
            }
            RECT tr{ r.left + provider_dlg_dpi_scale(hScroll, 14), title_top,
                r.right - provider_dlg_dpi_scale(hScroll, 8),
                title_top + provider_dlg_dpi_scale(hScroll, 18) };
            (void)::DrawTextW(hdc, gf.title.c_str(), (int)gf.title.size(), &tr,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    (void)::SetTextColor(hdc, old_tx);
    (void)::SetBkColor(hdc, old_bk);
    (void)::SetBkMode(hdc, old_bkmode);
    if (old_font) (void)::SelectObject(hdc, old_font);
    if (old_brush) (void)::SelectObject(hdc, old_brush);
    if (old_pen) (void)::SelectObject(hdc, old_pen);
    if (pen) (void)::DeleteObject(pen);
}

static void provider_dlg_apply_scroll_layout(HWND hScroll, DlgState& st, int y, int form_h) {
    if (!hScroll) return;
    provider_dlg_capture_scroll_children(hScroll, st);

    RECT rcScroll{};
    (void)::GetClientRect(hScroll, &rcScroll);
    const int viewport_h = (std::max)(1, (int)(rcScroll.bottom - rcScroll.top));
    const int max_y      = (std::max)(0, form_h - viewport_h);
    y = (std::clamp)(y, 0, max_y);

    // Robust settings-dialog scrolling: children are direct children of the scroll host and are
    // positioned from immutable virtual-layout rects on every scroll. No moving giant child window,
    // no ScrollWindowEx bit preservation, no cumulative deltas. We suppress intermediate redraws and
    // repaint the whole viewport once; correctness beats clever pixel reuse here.
    (void)::SendMessageW(hScroll, WM_SETREDRAW, FALSE, 0);
    for (const auto& c : st.scroll_children) {
        if (!c.h) continue;
        const int x = (int)c.logical.left;
        const int yy = (int)c.logical.top - y;
        const int w = (int)(c.logical.right - c.logical.left);
        const int h = (int)(c.logical.bottom - c.logical.top);
        (void)::SetWindowPos(c.h, nullptr, x, yy, w, h,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    }
    st.scroll_y = y;
    (void)::SendMessageW(hScroll, WM_SETREDRAW, TRUE, 0);

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = form_h - 1;
    si.nPage  = (UINT)viewport_h;
    si.nPos   = y;
    (void)::SetScrollInfo(hScroll, SB_VERT, &si, TRUE);

    (void)::RedrawWindow(hScroll, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}

static LRESULT CALLBACK provider_dlg_scroll_host_WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<DlgState*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCDESTROY) {
        (void)::SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        return ::DefWindowProcW(h, m, w, l);
    }
    if (st) {
        if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORBTN || m == WM_CTLCOLORLISTBOX) {
            HWND dlg = ::GetParent(h);
            if (dlg) return ::SendMessageW(dlg, m, w, l);
        }
        if (m == WM_COMMAND || m == WM_NOTIFY) {
            HWND dlg = ::GetParent(h);
            if (dlg) return ::SendMessageW(dlg, m, w, l);
        }
        if (m == WM_ERASEBKGND) {
            HDC         hdc  = reinterpret_cast<HDC>(w);
            const auto& pal  = pmui::theme_palette();
            RECT        rCl{};
            const int   cbox = (int)::GetClipBox(hdc, &rCl);
            if (cbox == 0) return 0; // GDI failure (per GetClipBox docs)
            if (cbox == NULLREGION) return 1;
            if (st->hDlgBg) {
                (void)::FillRect(hdc, &rCl, st->hDlgBg);
                return 1;
            }
            {
                HBRUSH b = ::CreateSolidBrush(pal.window_bg);
                (void)::FillRect(hdc, &rCl, b);
                (void)::DeleteObject(b);
                return 1;
            }
        }
        if (m == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC hdc = ::BeginPaint(h, &ps);
            provider_dlg_paint_group_frames(h, *st, hdc);
            ::EndPaint(h, &ps);
            return 0;
        }
        if (m == WM_VSCROLL || m == WM_MOUSEWHEEL) {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_ALL;
            (void)::GetScrollInfo(h, SB_VERT, &si);
            int  y  = (int)si.nPos;
            RECT rc{};
            (void)::GetClientRect(h, &rc);
            const int viewport_h = (std::max)(1, (int)(rc.bottom - rc.top));
            int  my = (std::max)(0, provider_dlg_dpi_scale(h, kFormContentH) - viewport_h);
            if (m == WM_MOUSEWHEEL) {
                const int delta  = (int)(short)HIWORD(w);
                const int lines8 = 3 * provider_dlg_dpi_scale(h, 16);
                y += (delta < 0) ? lines8 : -lines8;
            } else {
                const int code = (int)LOWORD(w);
                if (code == SB_LINEUP) y -= provider_dlg_dpi_scale(h, 16);
                else if (code == SB_LINEDOWN) y += provider_dlg_dpi_scale(h, 16);
                else if (code == SB_PAGEUP) y -= (int)si.nPage;
                else if (code == SB_PAGEDOWN) y += (int)si.nPage;
                else if (code == SB_THUMBTRACK) {
                    si.fMask = SIF_TRACKPOS;
                    (void)::GetScrollInfo(h, SB_VERT, &si);
                    y = (int)si.nTrackPos;
                } else if (code == SB_THUMBPOSITION) {
                    y = (int)HIWORD(w);
                } else
                    return ::DefWindowProcW(h, m, w, l);
            }
            y = (y < 0) ? 0 : ((y > my) ? my : y);
            provider_dlg_apply_scroll_layout(h, *st, y, provider_dlg_dpi_scale(h, kFormContentH));
            return 0;
        }
    }
    return ::DefWindowProcW(h, m, w, l);
}

// ============================================================
// Dialog proc
// ============================================================

static INT_PTR CALLBACK ProviderDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* state = reinterpret_cast<DlgState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, lp);
        state = reinterpret_cast<DlgState*>(lp);
        pmui::theme_init_from_settings();
        const pmui::provider_dlg_i18n::Strings& tr =
            pmui::provider_dlg_i18n::strings_for(state->display_language);
        const std::wstring provNoteW = std::wstring(tr.note_path_pre) + std::wstring(pm::brand::k_config_subpath_w)
            + std::wstring(tr.note_path_post);

        HINSTANCE inst = ::GetModuleHandleW(nullptr);
        HFONT hFont = pmui::ui_font();
        auto S = [&](int v) { return provider_dlg_dpi_scale(hwnd, v); };
        const int desiredClientW = S(kClientW);
        const int desiredClientH = S(kClientH);
        const int scrollViewportH = S(kScrollViewportH);
        const int formContentH = S(kFormContentH);
        const int x0 = S(kX0);
        const int btnY0 = scrollViewportH + S(kAfterNoteG);

        // Scroll pane + tall virtual form (so Save/Cancel stay visible)
        provider_dlg_register_scroll_host_class(inst);
        {
            RECT rc{0, 0, desiredClientW, desiredClientH};
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
            ::SetWindowPos(hwnd, nullptr, x, y, winW, winH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        const int clientW = provider_dlg_client_width(hwnd);
        state->hScrollPane = ::CreateWindowExW(0, L"PM_ProviderDlgScrollHost", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, clientW, scrollViewportH, hwnd, (HMENU)(UINT_PTR)IDC_PROVDLG_SCROLL, inst, nullptr);
        (void)::SetWindowLongPtrW(state->hScrollPane, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        // Controls are direct children of the scroll host. This avoids the classic ghosting from
        // moving a tall STATIC/form HWND that contains many child HWND controls.
        state->hFormContent = state->hScrollPane;
        {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin   = 0;
            si.nMax   = formContentH - 1;
            si.nPage  = (UINT)scrollViewportH;
            si.nPos   = 0;
            (void)::SetScrollInfo(state->hScrollPane, SB_VERT, &si, TRUE);
        }
        // --- Provider rows ---
        int yb = S(kFirstProviderY);
        int n = 0;
        const auto* defs = media::settings::known_providers(&n);
        for (int i = 0; i < kProviderCount && i < n; ++i) {
            const auto& def = defs[i];
            auto it = state->providers.find(def.name);
            media::settings::ProviderEntry blank;
            blank.base_url = def.base_url;
            const auto& entry = (it != state->providers.end()) ? it->second : blank;
            const int   row_h = S(kGroupH_Standard);
            LayoutProviderRow(state->hFormContent, yb, i, def, entry, state->rows[i], state, tr);
            yb += row_h;
            if (i + 1 < kProviderCount && i + 1 < n) yb += S(kGroupVGap);
        }

        // --- Note (tall enough for 2+ lines; path can wrap) ---
        ::CreateWindowExW(0, L"STATIC", provNoteW.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            x0, S(kNoteY), clientW - 2 * x0 - S(kScrollHostRightPad), S(kNoteH), state->hFormContent, nullptr, inst, nullptr);

        provider_dlg_enable_clip_siblings(state->hScrollPane);
        // Capture immutable virtual-layout rects after all scroll-host children are created, then
        // apply initial position. Later scrolls reposition from these rects (no cumulative deltas).
        provider_dlg_capture_scroll_children_now(state->hScrollPane, *state);

        // --- Buttons: right-aligned, min widths for long locales, gap between (no overlap) ---
        {
            const int    cancelW = S(kBtnMinCancelW);
            const int    saveW   = S(kBtnMinSaveW);
            const int    cancelX = clientW - S(kBtnRPad) - cancelW;
            const int    saveX   = cancelX - S(kBtnBetween) - saveW;
            (void)::CreateWindowExW(0, L"BUTTON", tr.btn_save,
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                saveX, btnY0, saveW, S(kBtnRowH), hwnd,
                (HMENU)(UINT_PTR)IDOK, inst, nullptr);
            (void)::CreateWindowExW(0, L"BUTTON", tr.btn_cancel,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                cancelX, btnY0, cancelW, S(kBtnRowH), hwnd,
                (HMENU)(UINT_PTR)IDCANCEL, inst, nullptr);
        }

        // Apply font to the dialog surface (incl. scrollable subtree)
        provider_dlg_set_font_tree(hwnd, hFont);

        // Match AppSettingsDlg: dark titlebar + themed comctl, flat dialog surface.
        {
            const auto& pal = pmui::theme_palette();
            pmui::apply_dark_titlebar(hwnd, pal.dark);
            pmui::enable_app_dark_mode(true);
            pmui::apply_window_theme_recursive(hwnd, pal.dark);
            if (state->hScrollPane) {
                if (pal.dark) (void)::SetWindowTheme(state->hScrollPane, L"DarkMode_Explorer", nullptr);
            }
            // hFormContent aliases hScrollPane; avoid running the recursive theme pass twice.
            if (state->hDlgBg)
                ::DeleteObject(state->hDlgBg);
            state->hDlgBg = ::CreateSolidBrush(pal.window_bg);
            if (state->hCtlBg)
                ::DeleteObject(state->hCtlBg);
            state->hCtlBg = ::CreateSolidBrush(pal.control_bg);
            if (state->hScrollPane) provider_dlg_subclass_comboboxes_in(state->hScrollPane, *state);
            // Let `WM_CTLCOLORBTN` style group captions; themed groupbox can force black text.
            const auto untheme_group = [](HWND h, LPARAM) -> BOOL {
                wchar_t c[32]{};
                (void)::GetClassNameW(h, c, static_cast<int>(sizeof(c) / sizeof(c[0])));
                if (lstrcmpiW(c, L"Button") == 0) {
                    if ((::GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) (void)::SetWindowTheme(h, L"", L"");
                }
                return TRUE;
            };
            (void)::EnumChildWindows(hwnd, untheme_group, 0);
        }
        provider_dlg_apply_scroll_layout(state->hScrollPane, *state, 0, formContentH);
        ::InvalidateRect(hwnd, nullptr, TRUE);

        ::SetFocus(state->rows[0].hKeyEdit);
        return FALSE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        if (!state || !state->hDlgBg) break;
        HDC hdc = reinterpret_cast<HDC>(wp);
        const auto& pal = pmui::theme_palette();
        ::SetBkColor(hdc, pal.window_bg);
        ::SetTextColor(hdc, pal.window_fg);
        return reinterpret_cast<INT_PTR>(state->hDlgBg);
    }
    // Single-line EDIT: theme helper clears `SetWindowTheme` so this supplies control colours.
    case WM_CTLCOLOREDIT: {
        if (!state || !state->hCtlBg) break;
        HDC hdc = reinterpret_cast<HDC>(wp);
        const auto& pal = pmui::theme_palette();
        ::SetBkColor(hdc, pal.control_bg);
        ::SetTextColor(hdc, pal.control_fg);
        return reinterpret_cast<INT_PTR>(state->hCtlBg);
    }
    // Group captions: `BS_GROUPBOX` (transparent on dialog). Push = `control_bg` (flat, like edits).
    case WM_CTLCOLORBTN: {
        if (!state || !state->hDlgBg) break;
        HWND hCtl = reinterpret_cast<HWND>(lp);
        if (!hCtl) break;
        const LONG   stl = ::GetWindowLongW(hCtl, GWL_STYLE);
        const int    t   = static_cast<int>(stl & BS_TYPEMASK);
        HDC          hdc = reinterpret_cast<HDC>(wp);
        const auto&  pal = pmui::theme_palette();
        if (t == BS_GROUPBOX) {
            (void)::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, pal.window_fg);
            ::SetBkColor(hdc, pal.window_bg);
            return reinterpret_cast<INT_PTR>(state->hDlgBg);
        }
        if (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE || t == BS_AUTORADIOBUTTON) {
            (void)::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, pal.window_fg);
            return reinterpret_cast<INT_PTR>(state->hDlgBg);
        }
        if (!state->hCtlBg) break;
        (void)::SetBkMode(hdc, OPAQUE);
        ::SetTextColor(hdc, pal.control_fg);
        ::SetBkColor(hdc, pal.control_bg);
        return reinterpret_cast<INT_PTR>(state->hCtlBg);
    }

    case WM_DESTROY: {
        if (state) {
            if (state->hDlgBg) {
                ::DeleteObject(state->hDlgBg);
                state->hDlgBg = nullptr;
            }
            if (state->hCtlBg) {
                ::DeleteObject(state->hCtlBg);
                state->hCtlBg = nullptr;
            }
        }
        break;
    }

    case WM_COMMAND: {
        if (!state) break;
        const int id = LOWORD(wp);

        if (id == IDCANCEL || (id == IDOK && HIWORD(wp) == BN_CLICKED && id == IDCANCEL)) {
            ::EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        if (id == IDOK) {
            // Read all rows back
            int n = 0;
            const auto* defs = media::settings::known_providers(&n);
            for (int i = 0; i < kProviderCount && i < n; ++i) {
                auto& entry = state->providers[defs[i].name];
                ReadProviderRow(i, *state, entry);
            }

            // Persist
            std::string err;
            if (!media::settings::save_providers(state->providers, err)) {
                std::wstring wmsg = pmui::provider_dlg_i18n::strings_for(state->display_language).msg_save_fail;
                wmsg += prov_utf8_to_wide(err);
                ::MessageBoxW(hwnd, wmsg.c_str(), pm::brand::k_app_id_w, MB_ICONERROR);
                return TRUE;
            }
            ::EndDialog(hwnd, IDOK);
            return TRUE;
        }

        // Show/Hide toggle buttons
        for (int i = 0; i < kProviderCount; ++i) {
            int base = IDC_PROVDLG_ROW0 + i * IDC_PROVDLG_STRIDE;
            if (id == base + 1 && HIWORD(wp) == BN_CLICKED) {
                ToggleKeyVisibility(state->rows[i],
                    pmui::provider_dlg_i18n::strings_for(state->display_language));
                return TRUE;
            }
        }
        break;
    }

    case WM_CLOSE:
        ::EndDialog(hwnd, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// ============================================================
// Public API
// ============================================================

bool ShowProviderSettingsDlg(HWND parent)
{
    DlgState state;
    std::string err;
    {
        media::settings::AppearanceSettings appearance{};
        media::settings::load_appearance(appearance, err);
        state.display_language = appearance.display_language;
        err.clear();
    }
    const pmui::provider_dlg_i18n::Strings& tr_open = pmui::provider_dlg_i18n::strings_for(state.display_language);

    if (!media::settings::load_providers(state.providers, err)) {
        std::wstring wmsg = tr_open.msg_load_fail;
        wmsg += prov_utf8_to_wide(err);
        ::MessageBoxW(parent, wmsg.c_str(), pm::brand::k_app_id_w, MB_ICONWARNING);
        // Continue anyway with defaults so user can still enter keys
    }

    // Build dialog template in memory
    alignas(DWORD) BYTE buf[4096]{};
    DLGTEMPLATE* dlg = reinterpret_cast<DLGTEMPLATE*>(buf);

    dlg->style   = DS_MODALFRAME | DS_CENTER | DS_SETFONT
                 | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dlg->cdit    = 0;  // no items — all created in WM_INITDIALOG via CreateWindowEx
    dlg->x = 0; dlg->y = 0;
    // Convert pixels to dialog units (~4px per DU on 96 DPI)
    dlg->cx = (SHORT)(kClientW * 4 / 7);
    dlg->cy = (SHORT)(kClientH * 8 / 15);

    WORD* p = reinterpret_cast<WORD*>(dlg + 1);
    *p++ = 0;  // menu atom: none
    *p++ = 0;  // class atom: default dialog
    // title
    {
        const pmui::provider_dlg_i18n::Strings& ts =
            pmui::provider_dlg_i18n::strings_for(state.display_language);
        const size_t n = std::wcslen(ts.window_title) + 1;
        memcpy(p, ts.window_title, n * sizeof(wchar_t));
        p += n * sizeof(wchar_t) / sizeof(WORD);
    }
    // font (for DS_SETFONT)
    *p++ = 9;  // point size
    const wchar_t font[] = L"Segoe UI";
    memcpy(p, font, sizeof(font));
    p += sizeof(font) / sizeof(WORD);

    INT_PTR result = ::DialogBoxIndirectParamW(
        ::GetModuleHandleW(nullptr),
        dlg, parent,
        ProviderDlgProc,
        reinterpret_cast<LPARAM>(&state));

    return result == IDOK;
}
