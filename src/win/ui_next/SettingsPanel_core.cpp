#include "stdafx.h"
#include "SettingsPanel.h"
#include "Resource.h"
#include "core/meta.hpp"
#include <commctrl.h> // SetWindowSubclass, DefSubclassProc, trackbar, …
#include <shlobj.h>
#include <uxtheme.h>
#include <algorithm>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

using pmui::wide_to_utf8;
using pmui::utf8_to_wide;

// UI font lives in a single place now (pmui::ui_font) so the global
// "App Settings → Font size" toggle re-themes every panel at once.
#include "helpers/ui_font.hpp"
#include "helpers/ui_dialog_relayout.hpp"
#include "helpers/theme.hpp"
#include "helpers/settings_panel_i18n.hpp"
#include "win/settings_store.hpp"
#include "settings_panel_helpers.hpp"
#include "ProviderModelRegistry.h"
#include "settings_controls.hpp"

static HFONT GetUIFont() { return pmui::ui_font(); }

using settings_panel_internals::add_settings_tooltip;
using settings_panel_internals::get_edit_int;
using settings_panel_internals::get_edit_text;
using settings_panel_internals::kKernelCount;
using settings_panel_internals::kKernelValues;
using settings_panel_internals::kResDims;
using settings_panel_internals::kResPresetCount;
using settings_panel_internals::kRatioPresetCount;
using settings_panel_internals::kRatioVals;
using settings_panel_internals::kSettingsCardUserData;
using settings_panel_internals::kSettingsSepUserData;

// ── Themed section dividers (replaces SS_ETCHEDHORZ — that style ignores dark WM_CTLCOLORS) ─

namespace {

static COLORREF settings_blend_card_bkg(const pmui::ThemePalette& p)
{
    if (!p.dark)
        return RGB(255, 255, 255); // Fluent cards: white on neutral shell
    return RGB(
        (GetRValue(p.window_bg) * 2 + GetRValue(p.control_bg) + 1) / 3,
        (GetGValue(p.window_bg) * 2 + GetGValue(p.control_bg) + 1) / 3,
        (GetBValue(p.window_bg) * 2 + GetBValue(p.control_bg) + 1) / 3);
}

/// Same `control_bg` as `WM_CTLCOLOREDIT` — for push `WM_CTLCOLORBTN` after `SetWindowTheme(…, L"…")`.
static HBRUSH settings_control_fill_brush()
{
    const auto& pal   = pmui::theme_palette();
    static HBRUSH s   = nullptr;
    static COLORREF c = 0xFFFFFFFF;
    if (!s || c != pal.control_bg) {
        if (s) ::DeleteObject(s);
        s = ::CreateSolidBrush(pal.control_bg);
        c = pal.control_bg;
    }
    return s;
}

LRESULT CALLBACK SettingsPanelCardProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR /*id*/, DWORD_PTR)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC         hdc = ::BeginPaint(h, &ps);
        RECT        rc;
        (void)::GetClientRect(h, &rc);
        const auto& pal  = pmui::theme_palette();
        const COLORREF    fill   = settings_blend_card_bkg(pal);
        HRGN rgn = ::CreateRoundRectRgn(
            (int)rc.left, (int)rc.top, (int)rc.right, (int)rc.bottom, 6, 6);
        if (rgn) {
            HBRUSH fbr = ::CreateSolidBrush(fill);
            (void)::FillRgn(hdc, rgn, fbr);
            (void)::DeleteObject(fbr);
            HBRUSH brd = ::CreateSolidBrush(pal.caption_pen);
            (void)::FrameRgn(hdc, rgn, brd, 1, 1);
            (void)::DeleteObject(brd);
            (void)::DeleteObject(rgn);
        }
        else {
            HBRUSH fbr = ::CreateSolidBrush(fill);
            (void)::FillRect(hdc, &rc, fbr);
            (void)::DeleteObject(fbr);
        }
        ::EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        (void)::RemoveWindowSubclass(h, SettingsPanelCardProc, 0);
        break;
    default:
        break;
    }
    return ::DefSubclassProc(h, msg, wp, lp);
}

LRESULT CALLBACK SettingsPanelHorzSepProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR /*id*/, DWORD_PTR)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC         hdc = BeginPaint(h, &ps);
        RECT        rc;
        (void)::GetClientRect(h, &rc);
        const auto& pal  = pmui::theme_palette();
        HBRUSH        bg = ::CreateSolidBrush(pal.window_bg);
        (void)::FillRect(hdc, &rc, bg);
        (void)::DeleteObject(bg);
        const int midY = (rc.top + rc.bottom) / 2;
        // Hairline: caption outline colour tracks light / dark in theme_palette.
        HPEN  pen = ::CreatePen(PS_SOLID, 1, pal.caption_pen);
        HGDIOBJ old = ::SelectObject(hdc, pen);
        (void)::MoveToEx(hdc, rc.left, midY, nullptr);
        (void)::LineTo(hdc, rc.right, midY);
        (void)::SelectObject(hdc, old);
        (void)::DeleteObject(pen);
        ::EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // All pixels drawn in WM_PAINT.
    case WM_NCDESTROY:
        (void)::RemoveWindowSubclass(h, SettingsPanelHorzSepProc, 0);
        break;
    default:
        break;
    }
    return ::DefSubclassProc(h, msg, wp, lp);
}

} // namespace

// Subclass: theme the host, forward messages so WM_COMMAND/WM_HSCROLL/WM_CTLCOLOR* reach
// the outer CSettingsView (children are not direct children of the view after scroll host).
LRESULT CSettingsView::ContentHostSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR /*id*/, DWORD_PTR dw)
{
    CSettingsView* self = reinterpret_cast<CSettingsView*>(dw);
    if (!self) return DefSubclassProc(h, msg, wp, lp);
    const HWND p = self->GetHwnd();
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC                         hdc = reinterpret_cast<HDC>(wp);
        RECT                        rc;
        (void)::GetClientRect(h, &rc);
        const auto& pal  = pmui::theme_palette();
        HBRUSH        br = ::CreateSolidBrush(pal.window_bg);
        (void)::FillRect(hdc, &rc, br);
        (void)::DeleteObject(br);
        return 1;
    }
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_HSCROLL: // trackbars, etc. — wparam/lparam unchanged
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_MOUSEWHEEL: // gaps / empty band over the content host
        return ::SendMessageW(p, msg, wp, lp);
    default:
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ── CSettingsView ─────────────────────────────────────────────────────────────

void CSettingsView::PreCreate(CREATESTRUCT& cs)
{
    CWnd::PreCreate(cs);
    cs.style |= WS_VSCROLL | WS_CLIPCHILDREN;
}

int CSettingsView::MaxChildBottom(HWND contentHost)
{
    if (!contentHost) return 0;
    int maxB = 0;
    (void)::EnumChildWindows(
        contentHost,
        [](HWND w, LPARAM p) -> BOOL {
            int* pMaxB = reinterpret_cast<int*>(p);
            RECT    r;
            (void)::GetWindowRect(w, &r);
            const HWND paren = ::GetParent(w);
            (void)::MapWindowPoints(nullptr, paren, reinterpret_cast<LPPOINT>(&r), 2);
            if (r.bottom > *pMaxB) *pMaxB = (int)r.bottom;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&maxB));
    return std::max(0, maxB);
}

void CSettingsView::UpdateSettingsPanelScroll()
{
    if (!m_hScrollContent) return;
    m_contentHeight = std::max(1, MaxChildBottom(m_hScrollContent) + 12);
    RECT vrc{};
    (void)::GetClientRect(GetHwnd(), &vrc);
    int vw   = (int)(vrc.right - vrc.left);
    int vh   = (int)(vrc.bottom - vrc.top);
    if (vh < 1) vh = 1;
    const int needBar  = m_contentHeight > vh;
    const int barW     = (int)::GetSystemMetrics(SM_CXVSCROLL);
    int       contentW = (std::max)(1, needBar ? (vw - barW) : vw);
    int       maxScroll  = (std::max)(0, m_contentHeight - vh);
    m_scrollPos         = (std::clamp)(m_scrollPos, 0, maxScroll);
    (void)::SetWindowPos(
        m_hScrollContent, nullptr, 0, -m_scrollPos, contentW, m_contentHeight,
        SWP_NOZORDER | SWP_NOACTIVATE);
    SCROLLINFO si{};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = m_contentHeight - 1;
    si.nPage  = (UINT)vh;
    si.nPos   = m_scrollPos;
    (void)::SetScrollInfo(GetHwnd(), SB_VERT, &si, TRUE);
    (void)::ShowScrollBar(GetHwnd(), SB_VERT, needBar ? TRUE : FALSE);
    (void)::InvalidateRect(GetHwnd(), nullptr, FALSE);
}

int CSettingsView::SettingsContentClientWidth() const
{
    if (!GetHwnd()) return 280;
    RECT vrc{};
    (void)::GetClientRect(GetHwnd(), &vrc);
    int vw = (int)(vrc.right - vrc.left);
    if (vw < 1) vw = 1;
    const int vh = (std::max)(1, (int)(vrc.bottom - vrc.top));
    // Match `UpdateSettingsPanelScroll`: the scroll host is narrowed when a vertical bar is present.
    const int barW    = (int)::GetSystemMetrics(SM_CXVSCROLL);
    const bool needBar = m_contentHeight > vh;
    return (std::max)(1, needBar ? (vw - barW) : vw);
}

void CSettingsView::OnSettingsVScroll(int code, int posHiword)
{
    RECT vrc{};
    (void)::GetClientRect(GetHwnd(), &vrc);
    const int vh        = (std::max)(1, (int)(vrc.bottom - vrc.top));
    int       maxScroll = (std::max)(0, m_contentHeight - vh);
    int       newPos    = m_scrollPos;
    switch (code) {
    case SB_LINEUP:   newPos -= 32; break;
    case SB_LINEDOWN: newPos += 32; break;
    case SB_PAGEUP:   newPos -= vh; break;
    case SB_PAGEDOWN: newPos += vh; break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: {
        SCROLLINFO tsi{sizeof(SCROLLINFO), SIF_TRACKPOS};
        (void)::GetScrollInfo(GetHwnd(), SB_VERT, &tsi);
        newPos = (int)tsi.nTrackPos;
        break;
    }
    case SB_TOP:    newPos = 0; break;
    case SB_BOTTOM: newPos = maxScroll; break;
    default: return;
    }
    m_scrollPos = (std::clamp)(newPos, 0, maxScroll);
    (void)posHiword; // nTrackPos is authoritative; HIWORD is legacy
    UpdateSettingsPanelScroll();
}

HWND CSettingsView::MakeLabel(LPCWSTR t, int y, int w) {
    using L = pmui::settings_controls::Layout;
    HINSTANCE inst   = GetModuleHandleW(nullptr);
    HWND      parent = m_hScrollContent ? m_hScrollContent : GetHwnd();
    if (w <= 0) w = DpiScaleInt(L::label_w);
    HWND h = pmui::settings_controls::create_field_label(
        {parent, inst, DpiScaleInt(L::pad_x), y, w, t, DpiScaleInt(L::label_h)});
    m_resizeControls.push_back(h);
    return h;
}

int CSettingsView::OnCreate(CREATESTRUCT&)
{
    m_replicateAsyncCancel = std::make_shared<std::atomic<bool>>(false);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    RECT      rc0{};
    (void)::GetClientRect(GetHwnd(), &rc0);
    int w0 = (std::max)(1, (int)rc0.right);
    m_hScrollContent
        = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, w0,
            1, GetHwnd(), nullptr, inst, nullptr);
    if (!m_hScrollContent) return -1;
    (void)::SetWindowSubclass(m_hScrollContent, CSettingsView::ContentHostSubclass, 0,
                              reinterpret_cast<DWORD_PTR>(this));
    CreateControls();
    UpdateSettingsPanelScroll();
    UpdateSettingsLayoutGeometry();
    ApplyFont(GetUIFont());
    SetMode(MODE_RESIZE);
    // Theme scrollbar for dark mode
    const auto& pal = pmui::theme_palette();
    if (pal.dark)
        ::SetWindowTheme(GetHwnd(), L"DarkMode_Explorer", nullptr);
    else
        ::SetWindowTheme(GetHwnd(), L"Explorer", nullptr);
    return 0;
}

void CSettingsView::ApplyFont(HFONT hf)
{
    // Children live under the scroll area host, not the view directly.
    HWND target = m_hScrollContent ? m_hScrollContent : GetHwnd();
    ::EnumChildWindows(target, [](HWND hwnd, LPARAM lp) -> BOOL {
        (void)::SendMessageW(hwnd, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)hf);
    RelayoutForUiFont();
}

void CSettingsView::RelayoutForUiFont()
{
    if (!m_hScrollContent)
        return;
    pmui::relayout_settings_scroll_host_statics_for_ui_font(
        m_hScrollContent, pmui::ui_font(), kSettingsSepUserData, kSettingsCardUserData);
    m_settingsLayoutContentW = -1;
    UpdateSettingsPanelScroll();
    UpdateSettingsLayoutGeometry();
    (void)::RedrawWindow(m_hScrollContent, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void CSettingsView::SetMode(Mode mode)
{
    m_mode = mode;
    int showR = (mode == MODE_RESIZE)    ? SW_SHOW : SW_HIDE;
    int showT = (mode == MODE_TRANSFORM) ? SW_SHOW : SW_HIDE;
    int showC = (mode == MODE_COMPRESS)  ? SW_SHOW : SW_HIDE;
    int showM = (mode == MODE_META)      ? SW_SHOW : SW_HIDE;
    int showF = (mode == MODE_FIND)      ? SW_SHOW : SW_HIDE;
    int showD = (mode == MODE_DUPLICATES) ? SW_SHOW : SW_HIDE;
    for (HWND h : m_resizeControls)    if (h) ::ShowWindow(h, showR);
    for (HWND h : m_transformControls) if (h) ::ShowWindow(h, showT);
    for (HWND h : m_compressControls)  if (h) ::ShowWindow(h, showC);
    for (HWND h : m_metaControls)      if (h) ::ShowWindow(h, showM);
    for (HWND h : m_findControls)      if (h) ::ShowWindow(h, showF);
    for (HWND h : m_dupControls)       if (h) ::ShowWindow(h, showD);
    if (mode == MODE_DUPLICATES) {
        RefreshDupControlStates();
        RefreshDupLlmInfoText();
    }
    if (mode == MODE_COMPRESS) {
        OnCmpFormatChanged();   // show the right PNG / MozJPEG sub-section
    } else {
        // Hide both compressor sub-sections when not in compress mode.
        for (HWND h : m_cmpPngControls)     if (h) ::ShowWindow(h, SW_HIDE);
        for (HWND h : m_cmpMozjpegControls) if (h) ::ShowWindow(h, SW_HIDE);
    }
    UpdateSettingsPanelScroll();
    UpdateSettingsLayoutGeometry();
    SyncReplicateRowVisibilityForMode();
    if (m_hScrollContent) {
        (void)::RedrawWindow(m_hScrollContent, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

void CSettingsView::OnCmpFormatChanged()
{
    bool mozjpeg = m_hCmpFormat &&
                   ::SendMessageW(m_hCmpFormat, CB_GETCURSEL, 0, 0) == 1;
    int showPng = mozjpeg ? SW_HIDE : SW_SHOW;
    int showMoz = mozjpeg ? SW_SHOW : SW_HIDE;
    for (HWND h : m_cmpPngControls)     if (h) ::ShowWindow(h, showPng);
    for (HWND h : m_cmpMozjpegControls) if (h) ::ShowWindow(h, showMoz);
    UpdateSettingsPanelScroll();
    m_settingsLayoutContentW = -1;
    UpdateSettingsLayoutGeometry();
}

void CSettingsView::AddThemedSep(std::vector<HWND>& group, HWND parent, HINSTANCE inst, int x0, int sepW, int sy)
{
    HWND h = ::CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE, x0, sy, sepW, 2, parent, nullptr, inst, nullptr);
    if (h) {
        ::SetWindowLongPtr(h, GWLP_USERDATA, kSettingsSepUserData);
        (void)::SetWindowSubclass(h, SettingsPanelHorzSepProc, 0, 0);
    }
    group.push_back(h);
}

void CSettingsView::AddSectionCard(std::vector<HWND>& group, HWND parent, HINSTANCE inst, int x, int y, int w, int h)
{
    if (!parent || w < 2 || h < 2) return;
    HWND c = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, inst, nullptr);
    if (!c) return;
    ::SetWindowLongPtr(c, GWLP_USERDATA, kSettingsCardUserData);
    (void)::SetWindowSubclass(c, SettingsPanelCardProc, 0, 0);
    group.push_back(c);
    (void)::SetWindowPos(c, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CSettingsView::CreateControls()
{
    std::string appearance_err;
    media::settings::AppearanceSettings appearance{};
    media::settings::load_appearance(appearance, appearance_err);
    m_display_language = appearance.display_language;
    m_repCtl.set_display_language(m_display_language);

    HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!m_hScrollContent) return;
    HWND hwnd = m_hScrollContent;

    // Shared layout metrics (`helpers/ui_constants.hpp` via `settings_controls.hpp`).
    using L = pmui::settings_controls::Layout;
    const int x0  = DpiScaleInt(L::pad_x);
    const int lw  = DpiScaleInt(L::label_w);
    const int gap = DpiScaleInt(L::gap);
    const int cx  = x0 + lw + gap;
    const int rh  = DpiScaleInt(L::control_h);
    const int dy  = DpiScaleInt(L::row_dy());

    RECT rcClient{};
    (void)::GetClientRect(GetHwnd(), &rcClient);
    int       rawW = (int)rcClient.right;
    int       parentW = 0;
    if (HWND ph = ::GetParent(GetHwnd())) {
        RECT prc{};
        if (::GetClientRect(ph, &prc)) parentW = (int)prc.right;
    }
    // First `Create` often runs before the docker assigns a non-zero size; comctl then builds
    // every combo with `cw` derived from this. Never let the initial pass use a near-zero width.
    // `SettingsContentClientWidth()` matches the scroll content host (narrows if a v-scrollbar shows).
    const int kLayoutMinW = DpiScaleInt(280);
    const int fromScroll  = SettingsContentClientWidth();
    const int clientW     = (std::max)({kLayoutMinW, rawW, parentW, fromScroll, 1});
    const int sepW = (std::max)(1, clientW - 2 * x0);
    int cw = clientW - cx - x0;
    if (cw < 0) cw = 0;
    const int trackW = (std::max)(0, sepW - DpiScaleInt(44));
    const int cmpSliderW = (std::max)(0, cw - DpiScaleInt(4));

    CreateResizeControls(hwnd, inst, x0, cx, rh, dy, sepW, cw, trackW);
    CreateTransformControls(hwnd, inst, x0, lw, cx, rh, dy, sepW, cw);
    CreateCompressControls(hwnd, inst, x0, lw, cx, rh, sepW, cw, cmpSliderW);
    CreateMetaControls(hwnd, inst, x0, lw, cx, rh, sepW, cw);
    CreateFindControls(hwnd, inst, x0, lw, cx, rh, sepW, cw);
    CreateDuplicatesControls(hwnd, inst, x0, lw, cx, rh, sepW, cw);
    InstallSettingsPanelTooltips();
    m_settingsLayoutContentW = clientW;
    // Replicate + saved provider/model restore: off the create path so AddDockedChild(settings) does
    // not block first paint for seconds (load_cache / JSON / triple reload). UWM_SETTINGS_DEFERRED_PROVIDER.
    if (GetHwnd())
        (void)::PostMessageW(GetHwnd(), UWM_SETTINGS_DEFERRED_PROVIDER, 0, 0);
}

namespace {

struct SettingsLayoutMetrics {
    int x0{};
    int cx{};
    int sepW{};
    int cw{};
    int trackW{};
    int cmpSliderW{};
    int contentW{}; /// matches `CSettingsView::m_hScrollContent` inner width
};

static SettingsLayoutMetrics layoutMetricsForContentWidth(HWND dpiHwnd, int contentWIn)
{
    const int   contentW = (std::max)(1, contentWIn);
    using L    = pmui::settings_controls::Layout;
    const int x0   = pmui::settings_controls::dpi_scale(dpiHwnd, L::pad_x);
    const int lw   = pmui::settings_controls::dpi_scale(dpiHwnd, L::label_w);
    const int gap  = pmui::settings_controls::dpi_scale(dpiHwnd, L::gap);
    const int cx   = x0 + lw + gap;
    const int sepW = (std::max)(1, contentW - 2 * x0);
    int         cw   = contentW - cx - x0;
    if (cw < 0) cw = 0;
    const int trackW     = (std::max)(0, sepW - pmui::settings_controls::dpi_scale(dpiHwnd, 44));
    const int cmpSliderW = (std::max)(0, cw - pmui::settings_controls::dpi_scale(dpiHwnd, 4));
    return {x0, cx, sepW, cw, trackW, cmpSliderW, contentW};
}

static void relayoutFieldAtCx(HWND h, HWND parent, int cx, int newW)
{
    if (!h) return;
    RECT r{};
    (void)::GetWindowRect(h, &r);
    (void)::MapWindowPoints(nullptr, parent, reinterpret_cast<LPPOINT>(&r), 2);
    (void)::SetWindowPos(h, nullptr, cx, r.top, newW, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void relayoutLeftWidth(HWND h, HWND parent, int x, int newW)
{
    if (!h) return;
    RECT r{};
    (void)::GetWindowRect(h, &r);
    (void)::MapWindowPoints(nullptr, parent, reinterpret_cast<LPPOINT>(&r), 2);
    (void)::SetWindowPos(h, nullptr, x, r.top, newW, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void relayoutReplicateCollectionRow(HWND hColl, HWND hRef, int cx, int cw, HWND parent)
{
    const int kRefW = 56;
    const int kGap  = 6;
    if (!hColl) return;
    RECT rc{};
    (void)::GetWindowRect(hColl, &rc);
    (void)::MapWindowPoints(nullptr, parent, reinterpret_cast<LPPOINT>(&rc), 2);
    int inner = cw - kRefW - kGap;
    if (inner < 48) inner = 48;
    (void)::SetWindowPos(hColl, nullptr, cx, rc.top, inner, 220, SWP_NOZORDER | SWP_NOACTIVATE);
    if (hRef) {
        RECT rr{};
        (void)::GetWindowRect(hRef, &rr);
        (void)::MapWindowPoints(nullptr, parent, reinterpret_cast<LPPOINT>(&rr), 2);
        (void)::SetWindowPos(
            hRef, nullptr, cx + inner + kGap, rr.top, kRefW, rr.bottom - rr.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

} // namespace

void CSettingsView::UpdateSettingsLayoutGeometry()
{
    if (!m_hScrollContent) return;
    const SettingsLayoutMetrics m = layoutMetricsForContentWidth(m_hScrollContent, SettingsContentClientWidth());
    if (m_settingsLayoutContentW >= 0 && m.contentW == m_settingsLayoutContentW) return;
    m_settingsLayoutContentW = m.contentW;
    namespace sc = pmui::settings_controls;
    HWND       host = m_hScrollContent;

    struct SepsData {
        CSettingsView* self;
        int            x0;
        int            sepW;
    } sdata{this, m.x0, m.sepW};
    (void)::EnumChildWindows(
        host,
        [](HWND w, LPARAM lp) -> BOOL {
            auto* d = reinterpret_cast<SepsData*>(lp);
            if (::GetParent(w) != d->self->m_hScrollContent) return TRUE;
            const LONG_PTR ud = ::GetWindowLongPtr(w, GWLP_USERDATA);
            RECT         wr{};
            (void)::GetWindowRect(w, &wr);
            (void)::MapWindowPoints(nullptr, d->self->m_hScrollContent, reinterpret_cast<LPPOINT>(&wr), 2);
            const int gh = (int)(wr.bottom - wr.top);
            if (ud == kSettingsSepUserData && gh >= 1 && gh <= 4) {
                (void)::SetWindowPos(w, nullptr, d->x0, wr.top, d->sepW, gh, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            else if (ud == kSettingsCardUserData) {
                (void)::SetWindowPos(
                    w, nullptr, d->x0 - 4, wr.top, d->sepW + 8, gh, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&sdata));

    // Resize mode
    relayoutFieldAtCx(m_hResPreset, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hRatio, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hFit, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hPreset, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hFormat, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hKernel, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hOutDir, host, m.cx, sc::field_width_with_browse(host, m.cw));
    {
        RECT rbb{};
        if (m_hBtnBrowse) {
            (void)::GetWindowRect(m_hBtnBrowse, &rbb);
            (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&rbb), 2);
            (void)::SetWindowPos(m_hBtnBrowse, nullptr, sc::browse_button_x(host, m.cx, m.cw), rbb.top,
                rbb.right - rbb.left, rbb.bottom - rbb.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    {
        RECT rq{};
        if (m_hSliderQuality) {
            (void)::GetWindowRect(m_hSliderQuality, &rq);
            (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&rq), 2);
            (void)::SetWindowPos(m_hSliderQuality, nullptr, m.x0, rq.top, m.trackW, rq.bottom - rq.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (m_hLblQuality) {
            (void)::GetWindowRect(m_hLblQuality, &rq);
            (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&rq), 2);
            (void)::SetWindowPos(m_hLblQuality, nullptr, m.x0 + m.trackW + sc::dpi_scale(host, 4), rq.top,
                sc::dpi_scale(host, 34), rq.bottom - rq.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    auto stretchCheck = [&](HWND h) {
        if (h) relayoutLeftWidth(h, host, m.x0, m.sepW);
    };
    stretchCheck(m_hAutorot);
    stretchCheck(m_hEnlarge);
    stretchCheck(m_hStrip);

    // Transform
    relayoutFieldAtCx(m_hTfProvider, host, m.cx, m.cw);
    relayoutReplicateCollectionRow(m_hTfCollection, m_hTfRefresh, m.cx, m.cw, host);
    relayoutFieldAtCx(m_hTfModel, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hTfAspect, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hTfSize, host, m.cx, m.cw);
    // Preresize checkboxes clear of scrollbar
    const int tfChkW = m.sepW - sc::dpi_scale(host, sc::Layout::scrollbar_right_pad) - sc::dpi_scale(host, 8);
    if (m_hTfPreresizeFirst) relayoutLeftWidth(m_hTfPreresizeFirst, host, m.x0, tfChkW);
    // Preresize width combo is indented to show it's dependent on the checkbox above
    const int indent = sc::dpi_scale(host, 18);
    relayoutFieldAtCx(m_hTfPreresizeW, host, m.cx + indent, m.cw - indent);
    if (m_hTfPreresizeRawOnly) relayoutLeftWidth(m_hTfPreresizeRawOnly, host, m.x0, tfChkW);
    relayoutLeftWidth(m_hTfPrompt, host, m.x0, m.sepW);
    {
        // Presets button only - centered
        if (m_hTfPresets) {
            RECT r{};
            (void)::GetWindowRect(m_hTfPresets, &r);
            (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&r), 2);
            const int wPresets = (std::min)(sc::dpi_scale(host, 132), m.sepW);
            const int xPresets = m.x0 + (m.sepW - wPresets) / 2;
            (void)::SetWindowPos(
                m_hTfPresets, nullptr, xPresets, r.top, wPresets, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    relayoutLeftWidth(m_hTfRefList, host, m.x0, m.sepW);
    {
        const int g    = sc::dpi_scale(host, 8);
        int       wCol = (m.sepW - g) / 2;
        int       wR   = m.sepW - wCol - g;
        if (m_hTfRefAdd) {
            RECT r{};
            (void)::GetWindowRect(m_hTfRefAdd, &r);
            (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&r), 2);
            (void)::SetWindowPos(m_hTfRefAdd, nullptr, m.x0, r.top, wCol, r.bottom - r.top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            if (m_hTfRefClear)
                (void)::SetWindowPos(
                    m_hTfRefClear, nullptr, m.x0 + wCol + g, r.top, wR, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // Compress
    relayoutFieldAtCx(m_hCmpDest, host, m.cx, m.cw);
    relayoutFieldAtCx(m_hCmpDir, host, m.cx, sc::field_width_with_browse(host, m.cw));
    {
        RECT rbb{};
        if (m_hCmpBrowse) {
            (void)::GetWindowRect(m_hCmpBrowse, &rbb);
            (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&rbb), 2);
            (void)::SetWindowPos(m_hCmpBrowse, nullptr, sc::browse_button_x(host, m.cx, m.cw), rbb.top,
                rbb.right - rbb.left, rbb.bottom - rbb.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    relayoutFieldAtCx(m_hCmpFormat, host, m.cx, m.cw);
    auto relayoutTBS = [&](HWND sl, HWND lbl) {
        if (sl) {
            RECT r{};
            (void)::GetWindowRect(sl, &r);
            (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&r), 2);
            (void)::SetWindowPos(sl, nullptr, m.cx, r.top, m.cmpSliderW, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (lbl) {
            RECT lr{};
            (void)::GetWindowRect(lbl, &lr);
            (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&lr), 2);
            (void)::SetWindowPos(lbl, nullptr, m.cx + m.cmpSliderW + sc::dpi_scale(host, 4), lr.top,
                sc::dpi_scale(host, 34), lr.bottom - lr.top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    };
    relayoutTBS(m_hCmpLevel, m_hCmpLevelLbl);
    relayoutTBS(m_hCmpQualSlider, m_hCmpQualLbl);
    relayoutTBS(m_hCmpJpegSlider, m_hCmpJpegLbl);
    relayoutFieldAtCx(m_hCmpColors, host, m.cx, m.cw);
    if (m_hCmpStrip) relayoutLeftWidth(m_hCmpStrip, host, m.x0, m.sepW);
    auto stretchCmpChk = [&](HWND h) {
        if (h) relayoutLeftWidth(h, host, m.x0, m.sepW);
    };
    stretchCmpChk(m_hCmpQuantize);
    stretchCmpChk(m_hCmpZopfli);
    stretchCmpChk(m_hCmpProgressive);
    stretchCmpChk(m_hCmpTrellis);

    // Meta
    relayoutFieldAtCx(m_hMetaOutDir, host, m.cx, sc::field_width_with_browse(host, m.cw));
    if (m_hMetaBrowse) {
        RECT rbb{};
        (void)::GetWindowRect(m_hMetaBrowse, &rbb);
        (void)::MapWindowPoints(nullptr, host, reinterpret_cast<LPPOINT>(&rbb), 2);
        (void)::SetWindowPos(m_hMetaBrowse, nullptr, sc::browse_button_x(host, m.cx, m.cw), rbb.top,
            rbb.right - rbb.left, rbb.bottom - rbb.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    auto metaChk = [&](HWND h) {
        if (h) relayoutLeftWidth(h, host, m.x0, m.sepW);
    };
    metaChk(m_hMetaOutMd);
    metaChk(m_hMetaOutJson);
    metaChk(m_hMetaUpdateExif);
    metaChk(m_hMetaResizeFirst);
    // Meta resize width combo is indented to show it's dependent on the checkbox above
    relayoutFieldAtCx(m_hMetaResizeW, host, m.cx + sc::dpi_scale(host, 18), m.cw - sc::dpi_scale(host, 18));
    relayoutFieldAtCx(m_hMetaProvider, host, m.cx, m.cw);
    relayoutReplicateCollectionRow(m_hMetaCollection, m_hMetaRefresh, m.cx, m.cw, host);
    relayoutFieldAtCx(m_hMetaModel, host, m.cx, m.cw);
    relayoutLeftWidth(m_hMetaPrompt, host, m.x0, m.sepW);
    relayoutFieldAtCx(m_hMetaPreset, host, m.cx, m.cw);

    // Find
    relayoutLeftWidth(m_hFindPrompt, host, m.x0, m.sepW);
    const int findChkW = m.sepW - sc::dpi_scale(host, sc::Layout::scrollbar_right_pad) - sc::dpi_scale(host, 8);
    auto findChk = [&](HWND h) {
        if (h) relayoutLeftWidth(h, host, m.x0, findChkW);
    };
    findChk(m_hFindLlm);
    findChk(m_hFindRecursive);
    findChk(m_hFindMatchFolders);
    findChk(m_hFindBypassCache);
    findChk(m_hFindNoGenerate);
    findChk(m_hFindUseMd);
    findChk(m_hFindUseJson);
    findChk(m_hFindUseExif);
    relayoutFieldAtCx(m_hFindProvider, host, m.cx, m.cw);
    relayoutReplicateCollectionRow(m_hFindCollection, m_hFindRefresh, m.cx, m.cw, host);
    relayoutFieldAtCx(m_hFindModel, host, m.cx, m.cw);
    // Find resize width combo is indented to show it's dependent on the checkbox above
    relayoutFieldAtCx(m_hFindResizeW, host, m.cx + sc::dpi_scale(host, 18), m.cw - sc::dpi_scale(host, 18));
    findChk(m_hFindResizeFirst);
    relayoutFieldAtCx(m_hFindMax, host, m.cx, m.cw);
    relayoutLeftWidth(m_hFindRefList, host, m.x0, m.sepW);

    // Duplicates
    if (m_hDupMode) relayoutFieldAtCx(m_hDupMode, host, m.cx, m.sepW);
    relayoutFieldAtCx(m_hDupMinGroup, host, m.cx, m.cw);
    const int dupChkW = m.sepW - sc::dpi_scale(host, sc::Layout::scrollbar_right_pad) - sc::dpi_scale(host, 8);
    auto dupChk = [&](HWND h) {
        if (h) relayoutLeftWidth(h, host, m.x0, dupChkW);
    };
    dupChk(m_hDupRecursive);
    relayoutFieldAtCx(m_hDupMaxHam, host, m.cx, m.cw);
    dupChk(m_hDupFpSameSize);
    dupChk(m_hDupUseMd);
    dupChk(m_hDupUseJson);
    dupChk(m_hDupUseExif);
    relayoutLeftWidth(m_hDupMetaPrompt, host, m.x0, m.sepW);
    dupChk(m_hDupMetaLlm);
    relayoutFieldAtCx(m_hDupLlmSource, host, m.cx, m.cw);
    if (m_hDupLlmInfo) relayoutLeftWidth(m_hDupLlmInfo, host, m.x0, m.sepW);
    relayoutFieldAtCx(m_hDupMinSim, host, m.cx, m.cw);
    dupChk(m_hDupImplicitMeta);

    (void)::InvalidateRect(host, nullptr, TRUE);
}

// ── Interaction handlers ──────────────────────────────────────────────────────

void CSettingsView::OnResPresetChanged()
{
    int sel = (int)::SendMessageW(m_hResPreset, CB_GETCURSEL, 0, 0);
    if (sel <= 0 || sel >= kResPresetCount) return;
    const auto& p = kResDims[sel];
    if (p.w <= 0 && p.h <= 0) return;

    m_updatingDims = true;
    wchar_t buf[16]{};
    if (p.w > 0) { swprintf_s(buf, L"%d", p.w); ::SetWindowTextW(m_hMaxW, buf); }
    if (p.h > 0) { swprintf_s(buf, L"%d", p.h); ::SetWindowTextW(m_hMaxH, buf); }
    else          { ::SetWindowTextW(m_hMaxH, L"0"); }
    m_updatingDims = false;

    // Auto-pick closest ratio if both dimensions set.
    if (p.w > 0 && p.h > 0) {
        for (int i = 1; i < kRatioPresetCount; ++i) {
            const auto& r = kRatioVals[i];
            if (r.rw > 0 && r.rh > 0 &&
                (p.w * r.rh == p.h * r.rw)) {
                ::SendMessageW(m_hRatio, CB_SETCURSEL, i, 0);
                m_ratioW = r.rw; m_ratioH = r.rh;
                return;
            }
        }
    }
    // No matching ratio — reset lock.
    ::SendMessageW(m_hRatio, CB_SETCURSEL, 0, 0);
    m_ratioW = m_ratioH = 0;
}

void CSettingsView::OnRatioChanged()
{
    int sel = (int)::SendMessageW(m_hRatio, CB_GETCURSEL, 0, 0);
    if (sel <= 0 || sel >= kRatioPresetCount) {
        m_ratioW = m_ratioH = 0;
        return;
    }
    m_ratioW = kRatioVals[sel].rw;
    m_ratioH = kRatioVals[sel].rh;

    // If width is already set, derive height.
    if (m_ratioW > 0 && m_ratioH > 0) {
        int w = get_edit_int(m_hMaxW);
        if (w > 0) {
            m_updatingDims = true;
            int h = (w * m_ratioH + m_ratioW / 2) / m_ratioW;
            wchar_t buf[16]{}; swprintf_s(buf, L"%d", h);
            ::SetWindowTextW(m_hMaxH, buf);
            m_updatingDims = false;
        }
    }
}

void CSettingsView::OnWidthChanged()
{
    if (m_updatingDims || m_ratioW <= 0 || m_ratioH <= 0) return;
    int w = get_edit_int(m_hMaxW);
    if (w <= 0) return;
    m_updatingDims = true;
    int h = (w * m_ratioH + m_ratioW / 2) / m_ratioW;
    wchar_t buf[16]{}; swprintf_s(buf, L"%d", h);
    ::SetWindowTextW(m_hMaxH, buf);
    m_updatingDims = false;
    // Reset res preset to (custom) since the user overrode it.
    ::SendMessageW(m_hResPreset, CB_SETCURSEL, 0, 0);
}

void CSettingsView::OnHeightChanged()
{
    if (m_updatingDims || m_ratioW <= 0 || m_ratioH <= 0) return;
    int h = get_edit_int(m_hMaxH);
    if (h <= 0) return;
    m_updatingDims = true;
    int w = (h * m_ratioW + m_ratioH / 2) / m_ratioH;
    wchar_t buf[16]{}; swprintf_s(buf, L"%d", w);
    ::SetWindowTextW(m_hMaxW, buf);
    m_updatingDims = false;
    ::SendMessageW(m_hResPreset, CB_SETCURSEL, 0, 0);
}

void CSettingsView::OnQualitySlider()
{
    int q = (int)::SendMessageW(m_hSliderQuality, TBM_GETPOS, 0, 0);
    wchar_t buf[8]{}; swprintf_s(buf, L"%d", q);
    ::SetWindowTextW(m_hLblQuality, buf);
}

void CSettingsView::OnBrowseOutput()
{
    BROWSEINFOW bi{};
    bi.hwndOwner = GetHwnd();
    bi.lpszTitle =
        pmui::settings_panel_i18n::resize_strings_for(m_display_language).browse_output_folder_title;
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    wchar_t dn[MAX_PATH]{};
    bi.pszDisplayName = dn;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return;
    wchar_t path[MAX_PATH * 4]{};
    if (SHGetPathFromIDListW(pidl, path))
        ::SetWindowTextW(m_hOutDir, path);
    CoTaskMemFree(pidl);
}

LRESULT CSettingsView::WndProc(UINT msg, WPARAM wparam, LPARAM lparam)
{
    try {
        switch (msg) {
        case UWM_SETTINGS_DEFERRED_PROVIDER:
            ApplyDeferredCommandProviderOverrides();
            return 0;
        case UWM_SETTINGS_REPLICATE_INIT_DONE:
            handleReplicateNetworkBatchDone(reinterpret_cast<SettingsRepinitPayload*>(lparam));
            return 0;
        case WM_NCDESTROY:
            if (m_replicateAsyncCancel)
                m_replicateAsyncCancel->store(true);
            break;
        case WM_SIZE:
            UpdateSettingsPanelScroll();
            UpdateSettingsLayoutGeometry();
            return 0;
        case WM_DPICHANGED:
        case WM_DPICHANGED_AFTERPARENT:
            m_settingsLayoutContentW = -1;
            ApplyFont(GetUIFont());
            UpdateSettingsPanelScroll();
            UpdateSettingsLayoutGeometry();
            return 0;
        case WM_SHOWWINDOW:
            if (wparam) {
                UpdateSettingsPanelScroll();
                m_settingsLayoutContentW = -1; // scrollbar / dock may have changed while hidden
                UpdateSettingsLayoutGeometry();
            }
            break;
        case WM_VSCROLL:
            if (lparam == 0) {
                OnSettingsVScroll(LOWORD(wparam), HIWORD(wparam));
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (m_hScrollContent) {
                int delta = (int)GET_WHEEL_DELTA_WPARAM(wparam);
                RECT  vrc2{};
                (void)::GetClientRect(GetHwnd(), &vrc2);
                const int vh2
                    = (std::max)(1, (int)(vrc2.bottom - vrc2.top));
                m_scrollPos -= (delta * 64) / WHEEL_DELTA; // ≈1 line per notch at 32px/line
                m_scrollPos = (std::clamp)(m_scrollPos, 0,
                    (std::max)(0, m_contentHeight - vh2));
                UpdateSettingsPanelScroll();
            }
            return 0;
        // ── Themed checkbox text (NM_CUSTOMDRAW from BUTTON children) ──────
        // BS_AUTOCHECKBOX with a DarkMode_Explorer theme draws its glyph
        // dark, but the *label text* is rendered by uxtheme's DrawText path
        // which ignores the colour we set in WM_CTLCOLORBTN.  Intercepting
        // the button's NM_CUSTOMDRAW at PREPAINT and returning CDRF_NEWFONT
        // after SetTextColor on the HDC actually sticks.
        case WM_NOTIFY: {
            auto* pnmh = reinterpret_cast<LPNMHDR>(lparam);
            if (pnmh && pnmh->code == NM_CUSTOMDRAW) {
                wchar_t cls[32]{};
                ::GetClassNameW(pnmh->hwndFrom, cls, 32);
                if (lstrcmpiW(cls, L"BUTTON") == 0) {
                    auto* nmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lparam);
                    if (nmcd->dwDrawStage == CDDS_PREPAINT) {
                        const int t = static_cast<int>(
                            ::GetWindowLongPtrW(pnmh->hwndFrom, GWL_STYLE) & BS_TYPEMASK);
                        const bool checkLike
                            = (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE
                               || t == BS_AUTORADIOBUTTON);
                        const bool group    = (t == BS_GROUPBOX);
                        const auto& pal     = pmui::theme_palette();
                        if (group || checkLike) {
                            (void)::SetTextColor(nmcd->hdc, pal.window_fg);
                            (void)::SetBkColor(nmcd->hdc, pal.window_bg);
                        } else { // push / defpush — same ink as `WM_CTLCOLORBTN` + `control_fill`.
                            (void)::SetTextColor(nmcd->hdc, pal.control_fg);
                            (void)::SetBkColor(nmcd->hdc, pal.control_bg);
                        }
                        return CDRF_NEWFONT;
                    }
                }
            }
            break;
        }
        // ── Panel background — themed (light/dark)
        case WM_ERASEBKGND: {
            const auto& pal = pmui::theme_palette();
            HBRUSH br = ::CreateSolidBrush(pal.window_bg);
            RECT rc{};
            ::GetClientRect(GetHwnd(), &rc);
            ::FillRect(reinterpret_cast<HDC>(wparam), &rc, br);
            ::DeleteObject(br);
            return 1;
        }
        // ── Static labels + the quality value label — transparent over the background
        case WM_CTLCOLORSTATIC: {
            HWND hStatic = reinterpret_cast<HWND>(lparam);
            if (hStatic && ::GetWindowLongPtr(hStatic, GWLP_USERDATA) == kSettingsSepUserData) {
                HDC hdc = reinterpret_cast<HDC>(wparam);
                ::SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(::GetStockObject(HOLLOW_BRUSH));
            }
            if (hStatic && ::GetWindowLongPtr(hStatic, GWLP_USERDATA) == kSettingsCardUserData) {
                HDC hdc = reinterpret_cast<HDC>(wparam);
                ::SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(::GetStockObject(HOLLOW_BRUSH));
            }
            const auto& pal = pmui::theme_palette();
            HDC hdc = reinterpret_cast<HDC>(wparam);
            ::SetBkMode(hdc, TRANSPARENT);
            ::SetTextColor(hdc, pal.window_fg);
            ::SetBkColor(hdc,   pal.window_bg);
            // Solid background brush for the static control, themed.
            static HBRUSH s_bgBrush     = nullptr;
            static COLORREF s_bgColor   = 0xFFFFFFFF;
            if (!s_bgBrush || s_bgColor != pal.window_bg) {
                if (s_bgBrush) ::DeleteObject(s_bgBrush);
                s_bgBrush  = ::CreateSolidBrush(pal.window_bg);
                s_bgColor  = pal.window_bg;
            }
            return reinterpret_cast<LRESULT>(s_bgBrush);
        }
        // ── Buttons: check/radio = transparent on `window_bg`; push = `control_bg` (flat, like EDIT).
        case WM_CTLCOLORBTN: {
            HWND     hbtn = reinterpret_cast<HWND>(lparam);
            const int     t
                = static_cast<int>(hbtn ? ::GetWindowLongPtrW(hbtn, GWL_STYLE) & BS_TYPEMASK : 0);
            const bool     checkLike = (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE
                || t == BS_AUTORADIOBUTTON);
            const bool     group     = (t == BS_GROUPBOX);
            const auto&   pal         = pmui::theme_palette();
            HDC           hdc         = reinterpret_cast<HDC>(wparam);
            if (group) {
                (void)::SetBkMode(hdc, TRANSPARENT);
                (void)::SetTextColor(hdc, pal.window_fg);
                (void)::SetBkColor(hdc, pal.window_bg);
                static HBRUSH s_grp = nullptr;
                static COLORREF s_gcol = 0xFFFFFFFF;
                if (!s_grp || s_gcol != pal.window_bg) {
                    if (s_grp) ::DeleteObject(s_grp);
                    s_grp  = ::CreateSolidBrush(pal.window_bg);
                    s_gcol = pal.window_bg;
                }
                return reinterpret_cast<LRESULT>(s_grp);
            }
            if (checkLike) {
                (void)::SetBkMode(hdc, TRANSPARENT);
                (void)::SetTextColor(hdc, pal.window_fg);
                static HBRUSH s_btnBrush  = nullptr;
                static COLORREF s_btnColor = 0xFFFFFFFF;
                if (!s_btnBrush || s_btnColor != pal.window_bg) {
                    if (s_btnBrush) ::DeleteObject(s_btnBrush);
                    s_btnBrush = ::CreateSolidBrush(pal.window_bg);
                    s_btnColor = pal.window_bg;
                }
                return reinterpret_cast<LRESULT>(s_btnBrush);
            }
            (void)::SetBkMode(hdc, OPAQUE);
            (void)::SetTextColor(hdc, pal.control_fg);
            (void)::SetBkColor(hdc, pal.control_bg);
            return reinterpret_cast<LRESULT>(settings_control_fill_brush());
        }
        // ── Edit / listbox / combobox text fields — themed in dark mode.
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            const auto& pal = pmui::theme_palette();
            HDC hdc = reinterpret_cast<HDC>(wparam);
            (void)::SetTextColor(hdc, pal.control_fg);
            (void)::SetBkColor(hdc, pal.control_bg);
            return reinterpret_cast<LRESULT>(settings_control_fill_brush());
        }
        // ── Trackbar sliders
        case WM_HSCROLL: {
            HWND h = reinterpret_cast<HWND>(lparam);
            if (h == m_hSliderQuality) { OnQualitySlider(); return 0; }
            auto liveSlider = [&](HWND sl, HWND lbl) {
                int v = (int)::SendMessageW(sl, TBM_GETPOS, 0, 0);
                wchar_t buf[8]{}; swprintf_s(buf, L"%d", v);
                ::SetWindowTextW(lbl, buf);
            };
            if (h == m_hCmpLevel)       { liveSlider(m_hCmpLevel,       m_hCmpLevelLbl);   return 0; }
            if (h == m_hCmpQualSlider)  { liveSlider(m_hCmpQualSlider,  m_hCmpQualLbl);    return 0; }
            if (h == m_hCmpJpegSlider)  { liveSlider(m_hCmpJpegSlider,  m_hCmpJpegLbl);    return 0; }
            break;
        }
        case WM_COMMAND: {
            int id   = LOWORD(wparam);
            int code = HIWORD(wparam);
            switch (id) {
            case IDC_BTN_BROWSE_OUT:
                OnBrowseOutput(); return 0;
            case IDC_CMP_BROWSE:
                OnCmpBrowse(); return 0;
            case IDC_CMP_FORMAT:
                if (code == CBN_SELCHANGE) { OnCmpFormatChanged(); return 0; } break;
            case IDC_META_BROWSE:
                OnMetaBrowse(); return 0;
            case IDC_META_PRESET:
                if (code == CBN_SELCHANGE) { OnMetaPresetChanged(); return 0; } break;
            case IDC_META_PROVIDER:
                if (code == CBN_SELCHANGE) { OnMetaProviderChanged(); return 0; } break;
            case IDC_META_COLLECTION:
                if (code == CBN_SELCHANGE) { OnMetaCollectionChanged(); return 0; } break;
            case IDC_META_REFRESH:
                if (code == BN_CLICKED) { OnMetaRefreshReplicateModels(); return 0; } break;
            case IDC_TF_PROVIDER:
                if (code == CBN_SELCHANGE) { OnTfProviderChanged(); return 0; } break;
            case IDC_TF_COLLECTION:
                if (code == CBN_SELCHANGE) { OnTfCollectionChanged(); return 0; } break;
            case IDC_TF_REFRESH:
                if (code == BN_CLICKED) { OnTfRefreshReplicateModels(); return 0; } break;
            case IDC_FIND_PROVIDER:
                if (code == CBN_SELCHANGE) { OnFindProviderChanged(); return 0; } break;
            case IDC_FIND_COLLECTION:
                if (code == CBN_SELCHANGE) { OnFindCollectionChanged(); return 0; } break;
            case IDC_FIND_REFRESH:
                if (code == BN_CLICKED) { OnFindRefreshReplicateModels(); return 0; } break;
            case IDC_CMD_PRESETS:
            case IDC_CMD_PROVIDER_KEYS:
                // Forward to the main frame (button lives in Transform mode panel).
                ::SendMessage(::GetAncestor(GetHwnd(), GA_ROOT), WM_COMMAND, wparam, lparam);
                return 0;
            case IDC_TF_REF_ADD:
                OnTfRefAdd();   return 0;
            case IDC_TF_REF_CLEAR:
                OnTfRefClear(); return 0;
            case IDC_FIND_REF_ADD:
                OnFindRefAdd();   return 0;
            case IDC_FIND_REF_CLEAR:
                OnFindRefClear(); return 0;
            case IDC_COMBO_RES_PRESET:
                if (code == CBN_SELCHANGE) { OnResPresetChanged(); return 0; } break;
            case IDC_COMBO_RATIO:
                if (code == CBN_SELCHANGE) { OnRatioChanged(); return 0; } break;
            case IDC_EDIT_MAX_W:
                if (code == EN_CHANGE) { OnWidthChanged();  return 0; } break;
            case IDC_EDIT_MAX_H:
                if (code == EN_CHANGE) { OnHeightChanged(); return 0; } break;
            case IDC_DUP_MODE:
                if (code == CBN_SELCHANGE) {
                    RefreshDupControlStates();
                    return 0;
                }
                break;
            case IDC_DUP_META_LLM:
            case IDC_DUP_IMPLICIT_META:
                if (code == BN_CLICKED) {
                    RefreshDupControlStates();
                    return 0;
                }
                break;
            case IDC_DUP_LLM_SOURCE:
                if (code == CBN_SELCHANGE) {
                    RefreshDupLlmInfoText();
                    return 0;
                }
                break;
            }
            break;
        }
        default:
            break;
        }
        return WndProcDefault(msg, wparam, lparam);
    }
    catch (const CException& e) {
        CString s; s << e.GetText() << L'\n' << e.GetErrorString();
        ::MessageBox(nullptr, s,
            pmui::settings_panel_i18n::resize_strings_for(m_display_language).msg_error_title,
            MB_ICONERROR);
    }
    return 0;
}