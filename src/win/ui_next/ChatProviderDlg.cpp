#include "stdafx.h"
#include "constants.hpp"
#include "ChatProviderDlg.h"
#include "Resource.h"
#include "win/settings_store.hpp"
#include "ProviderModelRegistry.h"
#include "OpenRouterSelectorController.h"
#include "PixlWizSelectorController.h"
#include "OpenAISelectorController.h"
#include "ReplicateSelectorController.h"
#include "AudioSelectorController.h"
#include "ElevenLabsSelectorController.h"
#include "helpers/default_shell.hpp"
#include "helpers/text_conv.hpp"
#include "helpers/ui_font.hpp"
#include "helpers/chat_provider_dlg_i18n.hpp"
#include "helpers/ui_constants.hpp"
#include "helpers/theme.hpp"

#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <shellapi.h>
#include <cwchar>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "UxTheme.lib")

namespace {

using GL = pmui::ui::GroupedModalLayout;

// Same wide/utf8 conversion helpers as ProviderDlg.
const auto& wide_to_utf8 = pmui::wide_to_utf8;
const auto& utf8_to_wide = pmui::utf8_to_wide;

static int chat_dlg_dpi_scale(HWND hwnd, int value)
{
    UINT dpi = 96;
    if (hwnd && ::IsWindow(hwnd))
        dpi = ::GetDpiForWindow(hwnd);
    return ::MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

// ── Control IDs (local; kept disjoint from ProviderDlg's 750-799 block) ────
constexpr int IDC_CHATDLG_ROUTER     = 800;   // COMBOBOX
constexpr int IDC_CHATDLG_MODEL      = 801;   // EDIT
constexpr int IDC_CHATDLG_MAX_ITER   = 805;   // EDIT (numeric)
constexpr int IDC_CHATDLG_OR_REFRESH   = 811; // Refresh OpenRouter model list
constexpr int IDC_CHATDLG_OR_OPEN_URL   = 812; // Open model page on openrouter.ai
constexpr int IDC_CHATDLG_OR_META        = 813; // OpenRouter model details (read-only)
constexpr int IDC_CHATDLG_IMG_PROVIDER = 807; // COMBOBOX
constexpr int IDC_CHATDLG_IMG_MODEL    = 808; // COMBOBOX
constexpr int IDC_CHATDLG_IMG_COLLECTION = 809; // COMBOBOX
constexpr int IDC_CHATDLG_IMG_REFRESH    = 810; // BUTTON
constexpr int IDC_CHATDLG_IR_PROVIDER    = 818; // COMBOBOX (image recognition)
constexpr int IDC_CHATDLG_IR_MODEL        = 819; // COMBOBOX
constexpr int IDC_CHATDLG_IR_COLLECTION  = 820; // Replicate collection
constexpr int IDC_CHATDLG_IR_REFRESH      = 821; // Replicate refresh
constexpr int IDC_CHATDLG_VID_PROVIDER    = 830; // COMBOBOX (video generation)
constexpr int IDC_CHATDLG_VID_MODEL       = 831;
constexpr int IDC_CHATDLG_VID_COLLECTION  = 832;
constexpr int IDC_CHATDLG_VID_REFRESH     = 833;
constexpr int IDC_CHATDLG_SCROLL          = 834; // scroll host (ProviderDlg-style)
constexpr int IDC_CHATDLG_STT_PROVIDER    = 840; // COMBOBOX — speech-to-text provider
constexpr int IDC_CHATDLG_STT_MODEL       = 841; // COMBOBOX — speech-to-text model
constexpr int IDC_CHATDLG_TTS_PROVIDER    = 842; // COMBOBOX — text-to-speech provider
constexpr int IDC_CHATDLG_TTS_MODEL       = 843; // COMBOBOX — text-to-speech model
constexpr int IDC_CHATDLG_TTS_VOICE       = 844; // SearchableCombo — TTS voice (ElevenLabs only)

// ── Layout ────────────────────────────────────────────────────────────────
/// Wider than original 448 so `field_x` + main column fits inside the BS_GROUPBOX
/// (group frame: `kClientW − 2*margin`; inner right = `kFieldTrackRight` — see `GroupedModalLayout`).
constexpr int kClientW     = 528;
/// Inset from the scroll host’s right so fields sit clear of the vertical scrollbar (matches ProviderDlg).
constexpr int kScrollHostRightPad = 24;
/// Pixels: raise section title text so it sits on the top frame line (matches `ProviderDlg`).
constexpr int kChatGroupTitleRise = 2;
/// Last pixel of the main field column: symmetric inset inside the group border.
constexpr int kFieldTrackRight = kClientW - GL::client_margin_x - GL::group_inset_x;
constexpr int kEditW     = (kFieldTrackRight - GL::field_x()) - kScrollHostRightPad;
// Group margins + insets: `pmui::ui::GroupedModalLayout` in `helpers/ui_constants.hpp`.
constexpr int kBtnW   = 70;
constexpr int kEH     = 22;
constexpr int kRowH   = 32;
constexpr int kBtnRowH  = 28;
constexpr int kBtnBottomM = 18; // client margin under Save/Cancel
/// Virtual document height inside the scroll pane (all stacked sections). Keep in sync with WM_INITDIALOG layout.
constexpr int kFormContentH     = 918;
constexpr int kScrollViewportH  = 440;
static_assert(kFormContentH > kScrollViewportH, "ChatProviderDlg: form taller than viewport");
constexpr int kAfterScrollGap   = 16; // between scroll area and Save/Cancel (ProviderDlg `kAfterNoteG`)
constexpr int kBtnY0            = kScrollViewportH + kAfterScrollGap;
/// Dialog client height: scroll viewport + gap + button row + bottom margin (Save/Cancel on dialog surface).
constexpr int kClientH        = kBtnY0 + kBtnRowH + kBtnBottomM;
// OpenRouter model row (matches ProviderDlg heuristics for catalog + meta).
constexpr int kOrRefreshW = 80;
constexpr int kOrOpenW    = 24;
constexpr int kOrMetaH    = 88;
constexpr int kOrMetaGap  = 4; // above / below meta band
/// Pixels from model row top to the bottom of the model/OpenRouter area (model row + OpenRouter meta; meta hidden when not OR).
constexpr int kModelToNextY = 32 + kOrMetaGap + kOrMetaH + kOrMetaGap;

static int chat_dlg_client_width(HWND hwnd)
{
    RECT rc{};
    if (hwnd && ::IsWindow(hwnd) && ::GetClientRect(hwnd, &rc))
        return (std::max)(1, (int)(rc.right - rc.left));
    return chat_dlg_dpi_scale(hwnd, kClientW);
}

static int chat_dlg_label_w(HWND hwnd)
{
    return (std::max)(chat_dlg_dpi_scale(hwnd, GL::label_w), chat_dlg_dpi_scale(hwnd, 126));
}

static int chat_dlg_content_x0(HWND hwnd)
{
    return chat_dlg_dpi_scale(hwnd, GL::content_x0());
}

static int chat_dlg_field_x(HWND hwnd)
{
    return chat_dlg_content_x0(hwnd) + chat_dlg_label_w(hwnd) + chat_dlg_dpi_scale(hwnd, GL::field_gap);
}

static int chat_dlg_edit_w(HWND hwnd)
{
    const int w = chat_dlg_client_width(hwnd) - chat_dlg_field_x(hwnd)
                - chat_dlg_dpi_scale(hwnd, kScrollHostRightPad)
                - chat_dlg_dpi_scale(hwnd, GL::client_margin_x)
                - chat_dlg_dpi_scale(hwnd, GL::group_inset_x);
    return (std::max)(chat_dlg_dpi_scale(hwnd, 180), w);
}

// Routers shown in the combo (display name + canonical id consumed by
// polymech::kbot::LLMClient). Base URLs and API keys are managed in ProviderDlg.
struct RouterEntry {
    const wchar_t* display;
    const char*    id;
    const char*    default_base_url;   // empty for "custom"
    const char*    default_model;
};

const RouterEntry kRouters[] = {
    { L"OpenRouter",  "openrouter",  "https://openrouter.ai/api/v1",                       "openai/gpt-4o-mini"           },
    { L"OpenAI",                        "openai",      "https://api.openai.com/v1",                          "gpt-4o-mini"                  },
    { L"Google Gemini",          "gemini",      "https://generativelanguage.googleapis.com/v1beta",   "gemini-3-pro-image-preview"             },
    { L"Ollama (local)",                "ollama",      "http://localhost:11434/v1",                          "llama3.2"                     },
    { L"PixlWiz",                       "pixlwiz",     "https://llm.polymech.info/v1",                       "text-fast"                    },
    { L"Custom (OpenAI-compatible)",    "custom",      "",                                                   ""                             },
};
constexpr int kRouterCount = static_cast<int>(sizeof(kRouters) / sizeof(kRouters[0]));

struct ChatProviderDlgScrollChild {
    HWND h{};
    RECT logical{}; // virtual document coords before scroll_y
};

/// Painted in the scroll host (`WM_PAINT`), same rationale as `ProviderDlg.cpp`: real `BS_GROUPBOX`
/// children repaint poorly when clipped / repositioned during scroll.
struct ChatProviderDlgGroupFrame {
    RECT         logical{}; // virtual document coords before `scroll_y`
    std::wstring title;
};

struct DlgState {
    media::settings::ChatProviderSettings cur;
    std::string                    display_language{"en"};
    HWND hRouter = nullptr;
    pmui::widgets::SearchableCombo hModel;
    HWND hMax    = nullptr;
    HWND hImgProvider = nullptr;
    HWND hImgCollection = nullptr;
    HWND hImgCollectionLbl = nullptr;
    HWND hImgRefresh = nullptr;
    HWND hImgModel = nullptr;
    HWND hIrProvider = nullptr;
    HWND hIrModel    = nullptr;
    HWND hIrCollection = nullptr;
    HWND hIrCollectionLbl = nullptr;
    HWND hIrRefresh  = nullptr;
    HWND hVidProvider = nullptr;
    HWND hVidModel    = nullptr;
    HWND hVidCollection = nullptr;
    HWND hVidCollectionLbl = nullptr;
    HWND hVidRefresh  = nullptr;
    HWND hSttProvider = nullptr;
    HWND hSttModel    = nullptr;
    HWND hTtsProvider = nullptr;
    HWND hTtsModel    = nullptr;
    pmui::widgets::SearchableCombo hTtsVoice; // ElevenLabs TTS voice (active when tts_provider=elevenlabs)
    std::vector<std::string> image_provider_ids;
    pmui::ReplicateSelectorState img_replicate_state;
    pmui::ReplicateSelectorState ir_replicate_state;
    pmui::ReplicateSelectorState vid_replicate_state;
    pmui::ReplicateSelectorController rep_ctl;
    pmui::OpenRouterSelectorState     or_state;
    pmui::OpenRouterSelectorController or_ctl;
    pmui::PixlWizSelectorState         pw_state;
    pmui::PixlWizSelectorController    pw_ctl;
    pmui::OpenAISelectorState          oai_state;
    pmui::OpenAISelectorController     oai_ctl;
    /// OpenRouter: catalog row; null when not used.
    HWND hOrRefresh = nullptr;
    HWND hOrOpen    = nullptr;
    HWND hOrMeta    = nullptr;
    /// y (client) of the “Model” row top; end of model/OpenRouter block at y + kModelToNextY.
    int  y_model_row = 0;
    bool openrouter_model_ui = false;
    bool pixlwiz_model_ui    = false;
    bool openai_model_ui     = false;
    HBRUSH         hDlgBg{};
    HBRUSH         hCtlBg{};
    /// Vertical scroll: stacked sections; Save/Cancel stay on the dialog footer (ProviderDlg pattern).
    HWND                               hScrollPane{};
    int                                scroll_y = 0;
    bool                               scroll_children_captured = false;
    std::vector<ChatProviderDlgScrollChild> scroll_children;
    std::vector<ChatProviderDlgGroupFrame>  group_frames;
    /// Set in `WM_INITDIALOG`; worker checks before `PostMessageW` so a closing dialog is not updated.
    std::shared_ptr<std::atomic<bool>> replicate_async_cancel;
};

int router_index_for_id(const std::string& id) {
    for (int i = 0; i < kRouterCount; ++i) {
        if (id == kRouters[i].id) return i;
    }
    return 0; // openrouter
}

const char* chat_dlg_default_base_for_router_index(int idx)
{
    if (idx < 0 || idx >= kRouterCount) return "";
    return kRouters[(size_t)idx].default_base_url ? kRouters[(size_t)idx].default_base_url : "";
}

static bool chat_dlg_id_is_openrouter(const char* id) { return id && std::strcmp(id, "openrouter") == 0; }
static bool chat_dlg_id_is_pixlwiz(const char* id)    { return id && std::strcmp(id, "pixlwiz")    == 0; }
static bool chat_dlg_id_is_openai(const char* id)     { return id && std::strcmp(id, "openai")     == 0; }

static LRESULT CALLBACK chat_dlg_scroll_host_WndProc(HWND h, UINT m, WPARAM w, LPARAM l);

static void chat_provider_dlg_register_scroll_host_class(HINSTANCE hInst) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wcx{};
    wcx.cbSize        = sizeof(wcx);
    wcx.lpfnWndProc   = chat_dlg_scroll_host_WndProc;
    wcx.hInstance     = hInst;
    wcx.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wcx.hbrBackground = nullptr; // WM_ERASEBKGND
    wcx.lpszClassName = L"PM_ChatProviderDlgScrollHost";
    if (!::RegisterClassExW(&wcx) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        (void)0; // best-effort; CreateWindowEx will fail visibly if registration failed
    registered = true;
}

struct ChatDlgCaptureCtx {
    DlgState* st{};
    HWND      parent{};
};

static BOOL CALLBACK chat_dlg_capture_immediate_child(HWND child, LPARAM p) {
    auto* ctx = reinterpret_cast<ChatDlgCaptureCtx*>(p);
    if (!ctx || !ctx->st || ::GetParent(child) != ctx->parent) return TRUE;
    RECT r{};
    (void)::GetWindowRect(child, &r);
    (void)::MapWindowPoints(nullptr, ctx->parent, reinterpret_cast<LPPOINT>(&r), 2);
    (void)::OffsetRect(&r, 0, ctx->st->scroll_y);
    ctx->st->scroll_children.push_back(ChatProviderDlgScrollChild{ child, r });
    return TRUE;
}

static void chat_dlg_capture_scroll_children(HWND hScroll, DlgState& st) {
    if (!hScroll || st.scroll_children_captured) return;
    st.scroll_children.clear();
    ChatDlgCaptureCtx ctx{ &st, hScroll };
    (void)::EnumChildWindows(hScroll, chat_dlg_capture_immediate_child, reinterpret_cast<LPARAM>(&ctx));
    st.scroll_children_captured = true;
}

static void chat_dlg_capture_scroll_children_now(HWND hScroll, DlgState& st) {
    if (!hScroll) return;
    st.scroll_children_captured = false;
    st.scroll_children.clear();
    chat_dlg_capture_scroll_children(hScroll, st);
}

static void chat_dlg_paint_group_frames(HWND hScroll, DlgState& st, HDC hdc) {
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
            const int title_top = (int)r.top - chat_dlg_dpi_scale(hScroll, kChatGroupTitleRise);
            SIZE sz{};
            (void)::GetTextExtentPoint32W(hdc, gf.title.c_str(), (int)gf.title.size(), &sz);
            RECT bg{ r.left + chat_dlg_dpi_scale(hScroll, 10), title_top,
                r.left + chat_dlg_dpi_scale(hScroll, 18) + sz.cx, title_top + chat_dlg_dpi_scale(hScroll, 18) };
            if (st.hDlgBg) {
                (void)::FillRect(hdc, &bg, st.hDlgBg);
            } else {
                HBRUSH b = ::CreateSolidBrush(pal.window_bg);
                (void)::FillRect(hdc, &bg, b);
                (void)::DeleteObject(b);
            }
            RECT tr{ r.left + chat_dlg_dpi_scale(hScroll, 14), title_top,
                r.right - chat_dlg_dpi_scale(hScroll, 8), title_top + chat_dlg_dpi_scale(hScroll, 18) };
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

static void chat_dlg_record_group_frame(
    DlgState& st, int x0, int y0, int y_end, int width_px, int bottom_pad, int bottom_trim, const wchar_t* title) {
    ChatProviderDlgGroupFrame gf{};
    gf.logical.left   = (LONG)x0;
    gf.logical.top    = (LONG)y0;
    gf.logical.right  = (LONG)(x0 + width_px);
    gf.logical.bottom = (LONG)(y_end + bottom_pad - bottom_trim);
    gf.title = title;
    st.group_frames.push_back(std::move(gf));
}

static void chat_dlg_apply_scroll_layout(HWND hScroll, DlgState& st, int y, int form_h) {
    if (!hScroll) return;
    chat_dlg_capture_scroll_children(hScroll, st);

    RECT rcScroll{};
    (void)::GetClientRect(hScroll, &rcScroll);
    const int viewport_h = (std::max)(1, (int)(rcScroll.bottom - rcScroll.top));
    const int max_y      = (std::max)(0, form_h - viewport_h);
    y = (std::clamp)(y, 0, max_y);

    (void)::SendMessageW(hScroll, WM_SETREDRAW, FALSE, 0);
    for (const auto& c : st.scroll_children) {
        if (!c.h) continue;
        const int x  = (int)c.logical.left;
        const int yy = (int)c.logical.top - y;
        const int ww = (int)(c.logical.right - c.logical.left);
        const int hh = (int)(c.logical.bottom - c.logical.top);
        (void)::SetWindowPos(c.h, nullptr, x, yy, ww, hh,
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

static void chat_dlg_enable_clip_siblings(HWND root) {
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

static void chat_dlg_set_font_tree(HWND root, HFONT f) {
    if (!root || !f) return;
    (void)::SendMessageW(root, WM_SETFONT, (WPARAM)f, TRUE);
    (void)::EnumChildWindows(
        root,
        [](HWND h, LPARAM p) -> BOOL {
            chat_dlg_set_font_tree(h, reinterpret_cast<HFONT>(p));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(f));
}

static LRESULT CALLBACK chat_dlg_desc_wheel_forward(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR /*uid*/, DWORD_PTR data) {
    auto* st = reinterpret_cast<DlgState*>(data);
    if (m == WM_NCDESTROY) {
        (void)::RemoveWindowSubclass(w, chat_dlg_desc_wheel_forward, 3);
        return ::DefSubclassProc(w, m, wp, lp);
    }
    if (m == WM_MOUSEWHEEL && st && st->hScrollPane)
        return ::SendMessageW(st->hScrollPane, WM_MOUSEWHEEL, wp, lp);
    return ::DefSubclassProc(w, m, wp, lp);
}

static void chat_dlg_subclass_desc_wheel_in(HWND scroll_root, DlgState& st) {
    if (!scroll_root) return;
    (void)::EnumChildWindows(
        scroll_root,
        [](HWND c, LPARAM p) -> BOOL {
            auto& stp = *reinterpret_cast<DlgState*>(p);
            if (c != stp.hOrMeta) {
                (void)::SetWindowSubclass(c, chat_dlg_desc_wheel_forward, 3, reinterpret_cast<DWORD_PTR>(&stp));
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&st));
}

static void chat_dlg_refresh_scroll_layout(DlgState& st) {
    if (!st.hScrollPane) return;
    chat_dlg_capture_scroll_children_now(st.hScrollPane, st);
    chat_dlg_apply_scroll_layout(st.hScrollPane, st, st.scroll_y, chat_dlg_dpi_scale(st.hScrollPane, kFormContentH));
}

static LRESULT CALLBACK chat_dlg_scroll_host_WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
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
            HDC         hdc = reinterpret_cast<HDC>(w);
            const auto& pal = pmui::theme_palette();
            RECT        rCl{};
            const int   cbox = (int)::GetClipBox(hdc, &rCl);
            if (cbox == 0) return 0;
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
            chat_dlg_paint_group_frames(h, *st, hdc);
            ::EndPaint(h, &ps);
            return 0;
        }
        if (m == WM_VSCROLL || m == WM_MOUSEWHEEL) {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_ALL;
            (void)::GetScrollInfo(h, SB_VERT, &si);
            int  y = (int)si.nPos;
            RECT rc{};
            (void)::GetClientRect(h, &rc);
            const int viewport_h = (std::max)(1, (int)(rc.bottom - rc.top));
            int       my         = (std::max)(0, chat_dlg_dpi_scale(h, kFormContentH) - viewport_h);
            if (m == WM_MOUSEWHEEL) {
                const int delta  = (int)(short)HIWORD(w);
                const int lines8 = 3 * chat_dlg_dpi_scale(h, 16);
                y += (delta < 0) ? lines8 : -lines8;
            } else {
                const int code = (int)LOWORD(w);
                if (code == SB_LINEUP) y -= chat_dlg_dpi_scale(h, 16);
                else if (code == SB_LINEDOWN) y += chat_dlg_dpi_scale(h, 16);
                else if (code == SB_PAGEUP) y -= (int)si.nPage;
                else if (code == SB_PAGEDOWN) y += (int)si.nPage;
                else if (code == SB_THUMBTRACK) {
                    si.fMask = SIF_TRACKPOS;
                    (void)::GetScrollInfo(h, SB_VERT, &si);
                    y = (int)si.nTrackPos;
                } else if (code == SB_THUMBPOSITION)
                    y = (int)HIWORD(w);
                else
                    return ::DefWindowProcW(h, m, w, l);
            }
            y = (y < 0) ? 0 : ((y > my) ? my : y);
            chat_dlg_apply_scroll_layout(h, *st, y, chat_dlg_dpi_scale(h, kFormContentH));
            return 0;
        }
    }
    return ::DefWindowProcW(h, m, w, l);
}

static void chat_dlg_set_or_meta(DlgState& st)
{
    if (!st.hOrMeta || !st.hModel.get()) return;
    st.or_ctl.update_meta_text(st.hOrMeta, st.or_state, st.or_ctl.selected_model_id(st.hModel));
}

static void chat_dlg_set_pw_meta(DlgState& st)
{
    if (!st.hOrMeta || !st.hModel.get()) return;
    st.pw_ctl.update_meta_text(st.hOrMeta, st.pw_state, st.pw_ctl.selected_model_id(st.hModel));
}

static bool chat_dlg_reload_openrouter_llm(DlgState& st, std::wstring* err_out, const bool force_refresh, const std::string* keep_override)
{
    if (err_out) err_out->clear();
    if (!st.hModel.get() || !st.hRouter) return false;
    std::string api_key;
    std::string base_url;
    {
        media::settings::ProviderMap pm;
        std::string load_err;
        if (media::settings::load_providers(pm, load_err)) {
            auto it = pm.find("openrouter");
            if (it != pm.end()) {
                api_key = it->second.api_key;
                base_url = it->second.base_url;
            }
        }
    }
    if (base_url.empty()) {
        const int r = (int)::SendMessageW(st.hRouter, CB_GETCURSEL, 0, 0);
        if (r >= 0 && r < kRouterCount) {
            const char* b = chat_dlg_default_base_for_router_index(r);
            if (b) base_url = b;
        }
    }
    std::string keep = st.or_ctl.selected_model_id(st.hModel);
    if (keep_override && !keep_override->empty()) keep = *keep_override;
    if (!st.or_ctl.reload(api_key, base_url, st.hModel, st.or_state, keep, force_refresh, err_out)) return false;
    chat_dlg_set_or_meta(st);
    return true;
}

static bool chat_dlg_reload_pixlwiz_llm(DlgState& st, std::wstring* err_out, const bool force_refresh, const std::string* keep_override)
{
    if (err_out) err_out->clear();
    if (!st.hModel.get()) return false;
    std::string api_key;
    std::string base_url;
    if (api_key.empty() || base_url.empty()) {
        media::settings::ProviderMap pm;
        std::string load_err;
        if (media::settings::load_providers(pm, load_err)) {
            auto it = pm.find("pixlwiz");
            if (it != pm.end()) {
                if (api_key.empty()) api_key = it->second.api_key;
                if (base_url.empty()) base_url = it->second.base_url;
            }
        }
    }
    if (base_url.empty()) base_url = "https://llm.polymech.info/v1";
    std::string keep = st.pw_ctl.selected_model_id(st.hModel);
    if (keep_override && !keep_override->empty()) keep = *keep_override;
    if (!st.pw_ctl.reload(api_key, base_url, st.hModel, st.pw_state, keep, force_refresh, err_out)) return false;
    chat_dlg_set_pw_meta(st);
    return true;
}

static bool chat_dlg_reload_openai_llm(DlgState& st, std::wstring* err_out, const bool force_refresh, const std::string* keep_override)
{
    if (err_out) err_out->clear();
    if (!st.hModel.get()) return false;
    std::string api_key;
    std::string base_url;
    {
        media::settings::ProviderMap pm;
        std::string load_err;
        if (media::settings::load_providers(pm, load_err)) {
            auto it = pm.find("openai");
            if (it != pm.end()) {
                api_key  = it->second.api_key;
                base_url = it->second.base_url;
            }
        }
    }
    if (base_url.empty()) base_url = "https://api.openai.com/v1";
    std::string keep = st.oai_ctl.selected_model_id(st.hModel);
    if (keep_override && !keep_override->empty()) keep = *keep_override;
    if (!st.oai_ctl.reload(api_key, base_url, st.hModel, st.oai_state, keep, force_refresh, err_out)) return false;
    if (st.hOrMeta)
        st.oai_ctl.update_meta_text(st.hOrMeta, st.oai_state, st.oai_ctl.selected_model_id(st.hModel));
    return true;
}

/// Creates / swaps model line (edit vs catalog). Preserves or reads model id; `hOrMeta` is created once.
static void chat_dlg_ensure_model_row_mode(HWND parent, DlgState& st, const char* router_id, const char* default_model_str,
    const HINSTANCE inst, const HFONT hFont) {
    auto S = [&](int v) { return chat_dlg_dpi_scale(parent, v); };
    const bool  want_or  = chat_dlg_id_is_openrouter(router_id);
    const bool  want_pw  = chat_dlg_id_is_pixlwiz(router_id);
    const bool  want_oai = chat_dlg_id_is_openai(router_id);
    const bool  want_catalog = want_or || want_pw || want_oai;
    const int   field_x = chat_dlg_field_x(parent);
    const int   edit_w  = chat_dlg_edit_w(parent);
    const int   y       = st.y_model_row - st.scroll_y;
    const auto& tr      = pmui::chat_provider_dlg_i18n::strings_for(st.display_language);
    // Save current value before updating items.
    std::string prev;
    if (st.hModel.get()) {
        if (st.openrouter_model_ui)
            prev = st.or_ctl.selected_model_id(st.hModel);
        else if (st.pixlwiz_model_ui)
            prev = st.pw_ctl.selected_model_id(st.hModel);
        else
            prev = wide_to_utf8(st.hModel.get_value());
    }
    if (prev.empty() && !st.cur.model.empty()) prev = st.cur.model;

    // Create once; subsequent router switches just update the item list.
    if (!st.hModel.get()) {
        st.hModel.create(pmui::widgets::SearchableComboParams{
            parent, inst, IDC_CHATDLG_MODEL,
            field_x, y, edit_w, S(200),
        });
    }
    st.openrouter_model_ui = want_or;
    st.pixlwiz_model_ui    = want_pw;
    st.openai_model_ui     = want_oai;

    if (!want_catalog) {
        // Free-text mode: clear the suggestion list, set the default / saved model string.
        if (prev.empty() && default_model_str && default_model_str[0])
            prev = default_model_str;
        st.hModel.set_items({});
        st.hModel.set_value(utf8_to_wide(prev));
    }

    if (!st.hOrMeta) {
        st.hOrMeta = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            field_x, y + S(32) + S(kOrMetaGap), edit_w, S(kOrMetaH), parent, (HMENU)(UINT_PTR)IDC_CHATDLG_OR_META, inst, nullptr);
    }
    (void)::SetWindowPos(st.hOrMeta, nullptr, field_x, y + S(32) + S(kOrMetaGap), edit_w, S(kOrMetaH), SWP_NOZORDER);
    (void)::ShowWindow(st.hOrMeta, want_catalog ? SW_SHOW : SW_HIDE);

    if (hFont) {
        (void)::SendMessageW(st.hModel.get(), WM_SETFONT, (WPARAM)hFont, TRUE);
        (void)::SendMessageW(st.hOrMeta, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    if (want_or) {
        (void)chat_dlg_reload_openrouter_llm(st, nullptr, false, prev.empty() ? nullptr : &prev);
    } else if (want_pw) {
        (void)chat_dlg_reload_pixlwiz_llm(st, nullptr, false, prev.empty() ? nullptr : &prev);
    } else if (want_oai) {
        (void)chat_dlg_reload_openai_llm(st, nullptr, false, prev.empty() ? nullptr : &prev);
    } else {
        st.or_state  = {};
        st.or_state.index_by_id.clear();
        st.pw_state  = {};
        st.pw_state.index_by_id.clear();
        st.oai_state = {};
        st.oai_state.index_by_id.clear();
        (void)::SetWindowTextW(st.hOrMeta, L"");
    }
}

void reload_image_models(DlgState* st, bool defer_replicate_network = false)
{
    if (!st || !st->hImgProvider || !st->hImgModel) return;
    const int sel = (int)::SendMessageW(st->hImgProvider, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)st->image_provider_ids.size()) return;
    const std::string provider = st->image_provider_ids[(size_t)sel];

    const bool is_replicate = provider == "replicate";
    st->rep_ctl.set_row_visible({st->hImgCollection, st->hImgRefresh, st->hImgCollectionLbl}, is_replicate);
    if (is_replicate) {
        if (defer_replicate_network) return;
        std::wstring rep_err;
        (void)st->rep_ctl.reload_using_saved_settings(
            st->hImgCollection, st->hImgModel,
            st->img_replicate_state, st->cur.image_model, true, false, &rep_err);
        st->rep_ctl.update_meta_text(nullptr, st->img_replicate_state,
                                     st->rep_ctl.selected_model_slug(st->hImgModel));
        return;
    }
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find(provider);
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    std::string pop_err;
    if (!pmui::provider_models::populate_model_combo(
            st->hImgModel, provider, api_key, base_url, st->cur.image_model, pop_err)) {
        std::string ignore;
        (void)pmui::provider_models::populate_model_combo(
            st->hImgModel, provider, "", "", st->cur.image_model, ignore);
    }
}

void reload_video_models(DlgState* st, bool defer_replicate_network = false)
{
    if (!st || !st->hVidProvider || !st->hVidModel) return;
    const int sel = (int)::SendMessageW(st->hVidProvider, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)st->image_provider_ids.size()) return;
    const std::string provider = st->image_provider_ids[(size_t)sel];

    const bool is_replicate = provider == "replicate";
    st->rep_ctl.set_row_visible({st->hVidCollection, st->hVidRefresh, st->hVidCollectionLbl}, is_replicate);
    if (is_replicate) {
        if (defer_replicate_network) return;
        std::wstring rep_err;
        (void)st->rep_ctl.reload_using_saved_settings(
            st->hVidCollection, st->hVidModel,
            st->vid_replicate_state, st->cur.video_model, true, false, &rep_err);
        st->rep_ctl.update_meta_text(nullptr, st->vid_replicate_state,
                                     st->rep_ctl.selected_model_slug(st->hVidModel));
        return;
    }
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find(provider);
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    std::string pop_err;
    if (!pmui::provider_models::populate_model_combo(
            st->hVidModel, provider, api_key, base_url, st->cur.video_model, pop_err)) {
        std::string ignore;
        (void)pmui::provider_models::populate_model_combo(
            st->hVidModel, provider, "", "", st->cur.video_model, ignore);
    }
}

void reload_image_recognition_models(DlgState* st, bool defer_replicate_network = false)
{
    if (!st || !st->hIrProvider || !st->hIrModel) return;
    const int sel = (int)::SendMessageW(st->hIrProvider, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)st->image_provider_ids.size()) return;
    const std::string provider = st->image_provider_ids[(size_t)sel];

    const bool is_replicate = provider == "replicate";
    st->rep_ctl.set_row_visible({st->hIrCollection, st->hIrRefresh, st->hIrCollectionLbl}, is_replicate);
    if (is_replicate) {
        if (defer_replicate_network) return;
        std::wstring rep_err;
        (void)st->rep_ctl.reload_using_saved_settings(
            st->hIrCollection, st->hIrModel,
            st->ir_replicate_state, st->cur.image_recognition_model, true, false, &rep_err);
        st->rep_ctl.update_meta_text(nullptr, st->ir_replicate_state,
                                     st->rep_ctl.selected_model_slug(st->hIrModel));
        return;
    }
    std::string api_key;
    std::string base_url;
    std::string err;
    media::settings::ProviderMap pm;
    if (media::settings::load_providers(pm, err)) {
        auto it = pm.find(provider);
        if (it != pm.end()) {
            api_key = it->second.api_key;
            base_url = it->second.base_url;
        }
    }
    std::string pop_err;
    if (!pmui::provider_models::populate_model_combo(
            st->hIrModel, provider, api_key, base_url, st->cur.image_recognition_model, pop_err)) {
        std::string ignore;
        (void)pmui::provider_models::populate_model_combo(
            st->hIrModel, provider, "", "", st->cur.image_recognition_model, ignore);
    }
}

/// Posted when background Replicate metadata fetch for `WM_INITDIALOG` completes (`lp` → heap payload).
constexpr UINT WM_PM_CHATDLG_REPINIT = WM_APP + 64;

struct ChatDlgRepinitPayload {
    pmui::ReplicateSelectorState img;
    pmui::ReplicateSelectorState vid;
    pmui::ReplicateSelectorState ir;
    bool ok_img = false;
    bool ok_vid = false;
    bool ok_ir  = false;
};

static void chat_dlg_repinit_worker(HWND                      hwnd,
                                    std::shared_ptr<std::atomic<bool>> cancel,
                                    bool                      want_img,
                                    bool                      want_vid,
                                    bool                      want_ir,
                                    std::string               keep_img,
                                    std::string               keep_vid,
                                    std::string               keep_ir,
                                    std::string               api_key,
                                    std::string               base_url) {
    pmui::ReplicateSelectorController ctl;
    auto*                             p = new ChatDlgRepinitPayload{};
    std::string                       err;
    if (want_img) {
        p->ok_img = ctl.reload_fetch_state(api_key, base_url, p->img, keep_img, true, false, err);
        if (cancel->load()) {
            delete p;
            return;
        }
    }
    if (want_vid) {
        p->ok_vid = ctl.reload_fetch_state(api_key, base_url, p->vid, keep_vid, true, false, err);
        if (cancel->load()) {
            delete p;
            return;
        }
    }
    if (want_ir)
        p->ok_ir = ctl.reload_fetch_state(api_key, base_url, p->ir, keep_ir, true, false, err);
    if (cancel->load()) {
        delete p;
        return;
    }
    if (!::IsWindow(hwnd)) {
        delete p;
        return;
    }
    (void)::PostMessageW(hwnd, WM_PM_CHATDLG_REPINIT, 0, reinterpret_cast<LPARAM>(p));
}

static void chat_dlg_start_replicate_async_init(HWND hwnd, DlgState& st) {
    auto read_pid = [](HWND combo, const std::vector<std::string>& ids) -> std::string {
        if (!combo || ids.empty()) return {};
        const int s = (int)::SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (s < 0 || s >= (int)ids.size()) return {};
        return ids[(size_t)s];
    };
    const std::string pid_i = read_pid(st.hImgProvider, st.image_provider_ids);
    const std::string pid_v = read_pid(st.hVidProvider, st.image_provider_ids);
    const std::string pid_r = read_pid(st.hIrProvider, st.image_provider_ids);
    const bool        want_img = pid_i == "replicate";
    const bool        want_vid = pid_v == "replicate";
    const bool        want_ir  = pid_r == "replicate";
    if (!want_img && !want_vid && !want_ir) return;
    const std::shared_ptr<std::atomic<bool>> cancel = st.replicate_async_cancel;
    if (!cancel) return;
    std::string api_key;
    std::string base_url;
    std::string load_err;
    media::settings::ProviderMap pm;
    (void)media::settings::load_providers(pm, load_err);
    if (auto it = pm.find("replicate"); it != pm.end()) {
        api_key  = it->second.api_key;
        base_url = it->second.base_url;
    }
    std::thread(chat_dlg_repinit_worker, hwnd, cancel, want_img, want_vid, want_ir, st.cur.image_model,
        st.cur.video_model, st.cur.image_recognition_model, std::move(api_key), std::move(base_url))
        .detach();
}

static LRESULT CALLBACK chat_provider_dlg_combo_subclass(HWND w, UINT m, WPARAM wp, LPARAM lp, UINT_PTR,
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
        (void)::RemoveWindowSubclass(w, chat_provider_dlg_combo_subclass, 1);
    return r;
}

static void chat_provider_dlg_subclass_comboboxes(HWND root, DlgState& st) {
    (void)::EnumChildWindows(
        root,
        [](HWND h, LPARAM p) -> BOOL {
            auto*   stp = reinterpret_cast<DlgState*>(p);
            wchar_t cls[32]{};
            (void)::GetClassNameW(h, cls, 32);
            if (lstrcmpiW(cls, L"ComboBox") == 0) {
                (void)::SetWindowSubclass(
                    h, chat_provider_dlg_combo_subclass, 1, reinterpret_cast<DWORD_PTR>(stp));
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&st));
}

INT_PTR CALLBACK ChatDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto* state = reinterpret_cast<DlgState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<DlgState*>(lp);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->replicate_async_cancel = std::make_shared<std::atomic<bool>>(false);
        const pmui::chat_provider_dlg_i18n::Strings& tr =
            pmui::chat_provider_dlg_i18n::strings_for(state->display_language);
        state->rep_ctl.set_display_language(state->display_language);
        state->or_ctl.set_display_language(state->display_language);
        state->pw_ctl.set_display_language(state->display_language);
        state->oai_ctl.set_display_language(state->display_language);
        HINSTANCE inst = ::GetModuleHandleW(nullptr);
        HFONT hFont = pmui::ui_font();
        auto S = [&](int v) { return chat_dlg_dpi_scale(hwnd, v); };
        const int desiredClientW = S(kClientW);
        const int desiredClientH = S(kClientH);
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
        const int clientW = chat_dlg_client_width(hwnd);
        const int scrollViewportH = S(kScrollViewportH);
        const int formContentH = S(kFormContentH);
        const int eh = S(kEH);
        const int rowH = S(kRowH);
        const int btnRowH = S(kBtnRowH);
        const int btnW = S(kBtnW);
        const int btnY0 = scrollViewportH + S(kAfterScrollGap);
        const int contentX0 = chat_dlg_content_x0(hwnd);
        const int labelW = chat_dlg_label_w(hwnd);
        const int fieldX = chat_dlg_field_x(hwnd);
        const int editW = chat_dlg_edit_w(hwnd);
        const int clientMarginX = S(GL::client_margin_x);
        const int groupBottomPad = S(GL::group_bottom_pad);
        const int groupBottomTrim = S(6);
        const int groupTopPad = S(GL::group_top_pad);
        const int interGroupGap = S(GL::inter_group_gap);

        chat_provider_dlg_register_scroll_host_class(inst);
        state->hScrollPane = ::CreateWindowExW(0, L"PM_ChatProviderDlgScrollHost", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0, 0, clientW, scrollViewportH, hwnd, (HMENU)(UINT_PTR)IDC_CHATDLG_SCROLL, inst, nullptr);
        (void)::SetWindowLongPtrW(state->hScrollPane, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
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
        HWND const content = state->hScrollPane;
        state->group_frames.clear();

        int                 y  = S(8);
        const int kGrpW = clientW - 2 * clientMarginX - S(kScrollHostRightPad);
        auto                label = [&](const wchar_t* text) {
            ::CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
                contentX0, y, labelW, eh, content, nullptr, inst, nullptr);
        };

        // 1) Image creation
        {
            const int y0 = y;
            y += groupTopPad;
            // Replicate: collection/refresh + model (reload_image_models / IDC_*_IMG_*).
            label(tr.lbl_image_provider);
            state->hImgProvider = ::CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                fieldX, y, editW, S(200), content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_IMG_PROVIDER, inst, nullptr);
            state->image_provider_ids.clear();
            int selected_provider_idx = 0;
            int idx = 0;
            for (const auto& p : pmui::provider_models::providers()) {
                state->image_provider_ids.push_back(p.id);
                ::SendMessageW(state->hImgProvider, CB_ADDSTRING, 0, (LPARAM)utf8_to_wide(p.label).c_str());
                if (p.id == state->cur.image_provider) selected_provider_idx = idx;
                ++idx;
            }
            ::SendMessageW(state->hImgProvider, CB_SETCURSEL, selected_provider_idx, 0);
            y += rowH;

            label(tr.lbl_image_model);
            state->hImgModel = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                fieldX, y, editW, eh, content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_IMG_MODEL, inst, nullptr);
            y += rowH + S(4);

            pmui::ReplicateSelectorRowRenderSpec img_rep_row{};
            img_rep_row.parent = content;
            img_rep_row.inst = inst;
            img_rep_row.font = hFont;
            img_rep_row.label_x = contentX0;
            img_rep_row.label_w = labelW;
            img_rep_row.field_x = fieldX;
            img_rep_row.y         = y;
            img_rep_row.row_h     = eh;
            img_rep_row.combo_w   = editW;
            img_rep_row.gap       = S(6);
            img_rep_row.refresh_w = S(30);
            img_rep_row.collection_id  = IDC_CHATDLG_IMG_COLLECTION;
            img_rep_row.refresh_id     = IDC_CHATDLG_IMG_REFRESH;
            img_rep_row.label_text     = tr.lbl_collection;
            img_rep_row.refresh_text   = L"\x21BB";
            img_rep_row.show_refresh   = true;
            img_rep_row.initially_visible = false;
            const auto img_rep_handles  = state->rep_ctl.render_collection_row(img_rep_row);
            state->hImgCollection       = img_rep_handles.h_collection;
            state->hImgRefresh          = img_rep_handles.h_refresh;
            state->hImgCollectionLbl    = img_rep_handles.h_label;
            reload_image_models(state, true);
            y += rowH;
            chat_dlg_record_group_frame(*state, clientMarginX, y0, y, kGrpW, groupBottomPad, groupBottomTrim,
                tr.group_image_creation);
        }
        y += interGroupGap;

        // 2) Video generation (create_video path tool; Replicate slug)
        {
            const int y0 = y;
            y += groupTopPad;
            label(tr.lbl_video_provider);
            state->hVidProvider = ::CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                fieldX, y, editW, S(200), content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_VID_PROVIDER, inst, nullptr);
            {
                int v_sel = 0, v_ii = 0;
                for (const auto& p : pmui::provider_models::providers()) {
                    (void)::SendMessageW(state->hVidProvider, CB_ADDSTRING, 0, (LPARAM)utf8_to_wide(p.label).c_str());
                    if (p.id == state->cur.video_provider) v_sel = v_ii;
                    ++v_ii;
                }
                (void)::SendMessageW(state->hVidProvider, CB_SETCURSEL, v_sel, 0);
            }
            y += rowH;

            label(tr.lbl_video_model);
            state->hVidModel = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                fieldX, y, editW, eh, content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_VID_MODEL, inst, nullptr);
            y += rowH + S(4);

            pmui::ReplicateSelectorRowRenderSpec vid_rep_row{};
            vid_rep_row.parent = content;
            vid_rep_row.inst = inst;
            vid_rep_row.font = hFont;
            vid_rep_row.label_x = contentX0;
            vid_rep_row.label_w = labelW;
            vid_rep_row.field_x = fieldX;
            vid_rep_row.y         = y;
            vid_rep_row.row_h     = eh;
            vid_rep_row.combo_w   = editW;
            vid_rep_row.gap       = S(6);
            vid_rep_row.refresh_w = S(30);
            vid_rep_row.collection_id  = IDC_CHATDLG_VID_COLLECTION;
            vid_rep_row.refresh_id     = IDC_CHATDLG_VID_REFRESH;
            vid_rep_row.label_text     = tr.lbl_collection;
            vid_rep_row.refresh_text   = L"\x21BB";
            vid_rep_row.show_refresh   = true;
            vid_rep_row.initially_visible = false;
            const auto vid_rep_handles   = state->rep_ctl.render_collection_row(vid_rep_row);
            state->hVidCollection        = vid_rep_handles.h_collection;
            state->hVidRefresh             = vid_rep_handles.h_refresh;
            state->hVidCollectionLbl       = vid_rep_handles.h_label;
            reload_video_models(state, true);
            y += rowH;
            chat_dlg_record_group_frame(*state, clientMarginX, y0, y, kGrpW, groupBottomPad, groupBottomTrim,
                tr.group_video_generation);
        }
        y += interGroupGap;

        // 3) Image recognition
        {
            const int y0 = y;
            y += groupTopPad;
            label(tr.lbl_recognition_provider);
            state->hIrProvider = ::CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                fieldX, y, editW, S(200), content, (HMENU)(UINT_PTR)IDC_CHATDLG_IR_PROVIDER, inst, nullptr);
            {
                int ir_sel = 0, ir_ii = 0;
                for (const auto& p : pmui::provider_models::providers()) {
                    (void)::SendMessageW(state->hIrProvider, CB_ADDSTRING, 0, (LPARAM)utf8_to_wide(p.label).c_str());
                    if (p.id == state->cur.image_recognition_provider) ir_sel = ir_ii;
                    ++ir_ii;
                }
                (void)::SendMessageW(state->hIrProvider, CB_SETCURSEL, ir_sel, 0);
            }
            y += rowH;

            label(tr.lbl_recognition_model);
            state->hIrModel = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                fieldX, y, editW, eh, content, (HMENU)(UINT_PTR)IDC_CHATDLG_IR_MODEL, inst, nullptr);
            y += rowH + S(4);

            pmui::ReplicateSelectorRowRenderSpec ir_rep_row{};
            ir_rep_row.parent      = content;
            ir_rep_row.inst        = inst;
            ir_rep_row.font        = hFont;
            ir_rep_row.label_x     = contentX0;
            ir_rep_row.label_w     = labelW;
            ir_rep_row.field_x     = fieldX;
            ir_rep_row.y           = y;
            ir_rep_row.row_h       = eh;
            ir_rep_row.combo_w     = editW;
            ir_rep_row.gap         = S(6);
            ir_rep_row.refresh_w   = S(30);
            ir_rep_row.collection_id   = IDC_CHATDLG_IR_COLLECTION;
            ir_rep_row.refresh_id      = IDC_CHATDLG_IR_REFRESH;
            ir_rep_row.label_text      = tr.lbl_collection;
            ir_rep_row.refresh_text    = L"\x21BB";
            ir_rep_row.show_refresh    = true;
            ir_rep_row.initially_visible = false;
            const auto ir_handles      = state->rep_ctl.render_collection_row(ir_rep_row);
            state->hIrCollection       = ir_handles.h_collection;
            state->hIrRefresh          = ir_handles.h_refresh;
            state->hIrCollectionLbl    = ir_handles.h_label;
            reload_image_recognition_models(state, true);
            y += rowH;
            chat_dlg_record_group_frame(*state, clientMarginX, y0, y, kGrpW, groupBottomPad, groupBottomTrim,
                tr.group_image_recognition);
        }
        y += interGroupGap;

        // 4) Chat (text & LLM): router, model
        {
            const int y0 = y;
            y += groupTopPad;
            label(tr.lbl_router);
            state->hRouter = ::CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                fieldX, y, editW, S(200), content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_ROUTER, inst, nullptr);
            for (int i = 0; i < kRouterCount; ++i)
                ::SendMessageW(state->hRouter, CB_ADDSTRING, 0, (LPARAM)kRouters[i].display);
            ::SendMessageW(state->hRouter, CB_SETCURSEL, router_index_for_id(state->cur.router), 0);
            y += rowH;

            label(tr.lbl_model);
            state->y_model_row = y;
            {
                const int ridx = router_index_for_id(state->cur.router);
                chat_dlg_ensure_model_row_mode(content, *state, kRouters[ridx].id, kRouters[ridx].default_model, inst, hFont);
            }
            y += S(kModelToNextY);
            chat_dlg_record_group_frame(*state, clientMarginX, y0, y, kGrpW, groupBottomPad, groupBottomTrim,
                tr.group_chat);
        }
        y += interGroupGap;

        // 5) Voice & Audio — Speech-to-Text and Text-to-Speech provider / model selection.
        {
            const int y0 = y;
            y += groupTopPad;

            label(tr.lbl_stt_provider);
            state->hSttProvider = ::CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                fieldX, y, editW, S(200), content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_STT_PROVIDER, inst, nullptr);
            pmui::AudioSelectorController::populate_stt_provider_combo(
                state->hSttProvider, state->cur.stt_provider);
            y += rowH;

            label(tr.lbl_stt_model);
            state->hSttModel = ::CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                fieldX, y, editW, S(300), content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_STT_MODEL, inst, nullptr);
            {
                const std::string stt_pid = pmui::AudioSelectorController::selected_provider_id(
                    state->hSttProvider, true);
                pmui::AudioSelectorController::populate_model_combo(
                    state->hSttModel, stt_pid, true, state->cur.stt_model);
            }
            y += rowH;

            label(tr.lbl_tts_provider);
            state->hTtsProvider = ::CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                fieldX, y, editW, S(200), content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_TTS_PROVIDER, inst, nullptr);
            pmui::AudioSelectorController::populate_tts_provider_combo(
                state->hTtsProvider, state->cur.tts_provider);
            y += rowH;

            label(tr.lbl_tts_model);
            state->hTtsModel = ::CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                fieldX, y, editW, S(300), content,
                (HMENU)(UINT_PTR)IDC_CHATDLG_TTS_MODEL, inst, nullptr);
            {
                const std::string tts_pid = pmui::AudioSelectorController::selected_provider_id(
                    state->hTtsProvider, false);
                pmui::AudioSelectorController::populate_model_combo(
                    state->hTtsModel, tts_pid, false, state->cur.tts_model);
            }
            y += rowH;

            // Voice combo — active only when TTS provider is ElevenLabs (direct).
            // Disabled (greyed) for PixlWiz; always present to avoid layout shifts.
            label(tr.lbl_tts_voice);
            {
                pmui::widgets::SearchableComboParams p{};
                p.parent = content;
                p.inst   = inst;
                p.id     = IDC_CHATDLG_TTS_VOICE;
                p.x      = fieldX;
                p.y      = y;
                p.w      = editW;
                p.drop_h = S(220);
                state->hTtsVoice.create(p);
            }
            {
                const std::string tts_pid = pmui::AudioSelectorController::selected_provider_id(
                    state->hTtsProvider, false);
                const bool is_el = (tts_pid == "elevenlabs");
                if (is_el)
                    pmui::ElevenLabsSelectorController::populate_tts_voice_combo(
                        state->hTtsVoice, state->cur.tts_voice_id);
                else
                    state->hTtsVoice.set_items({}, {});
                ::EnableWindow(state->hTtsVoice.get(), is_el ? TRUE : FALSE);
            }
            y += rowH;

            chat_dlg_record_group_frame(*state, clientMarginX, y0, y, kGrpW, groupBottomPad, groupBottomTrim,
                tr.group_audio);
        }
        y += interGroupGap;

        // 6) Agent settings
        {
            const int y0 = y;
            y += groupTopPad;
            label(tr.lbl_max_iter);
            wchar_t ibuf[16];
            swprintf_s(ibuf, L"%d", state->cur.max_iterations);
            state->hMax = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", ibuf,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
                fieldX, y, S(60), eh, content, (HMENU)(UINT_PTR)IDC_CHATDLG_MAX_ITER, inst, nullptr);
            y += rowH;
            chat_dlg_record_group_frame(*state, clientMarginX, y0, y, kGrpW, groupBottomPad, groupBottomTrim,
                tr.group_agent);
        }

        // Save / Cancel: fixed footer below the scroll viewport (`kBtnY0` === `kScrollViewportH` + gap).
        const int btnY = btnY0;
        ::CreateWindowExW(0, L"BUTTON", tr.btn_save,
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            clientW - 2 * (btnW + S(12)) - clientMarginX, btnY, btnW + S(12), btnRowH, hwnd,
            (HMENU)(UINT_PTR)IDOK, inst, nullptr);
        ::CreateWindowExW(0, L"BUTTON", tr.btn_cancel,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            clientW - btnW - clientMarginX - S(12), btnY, btnW + S(12), btnRowH, hwnd,
            (HMENU)(UINT_PTR)IDCANCEL, inst, nullptr);

        chat_dlg_enable_clip_siblings(state->hScrollPane);
        chat_dlg_capture_scroll_children_now(state->hScrollPane, *state);

        chat_dlg_set_font_tree(hwnd, hFont);
        if (state->hScrollPane) chat_dlg_set_font_tree(state->hScrollPane, hFont);

        pmui::theme_init_from_settings();
        {
            const auto& pal = pmui::theme_palette();
            pmui::apply_dark_titlebar(hwnd, pal.dark);
            pmui::enable_app_dark_mode(true);
            pmui::apply_window_theme_recursive(hwnd, pal.dark);
            if (state->hScrollPane) {
                pmui::apply_window_theme_recursive(state->hScrollPane, pal.dark);
                if (pal.dark) (void)::SetWindowTheme(state->hScrollPane, L"DarkMode_Explorer", nullptr);
            }
            if (state->hDlgBg)
                ::DeleteObject(state->hDlgBg);
            state->hDlgBg = ::CreateSolidBrush(pal.window_bg);
            if (state->hCtlBg)
                ::DeleteObject(state->hCtlBg);
            state->hCtlBg = ::CreateSolidBrush(pal.control_bg);
            const auto untheme_group = [](HWND h, LPARAM) -> BOOL {
                wchar_t c[32]{};
                (void)::GetClassNameW(h, c, static_cast<int>(sizeof(c) / sizeof(c[0])));
                if (lstrcmpiW(c, L"Button") == 0) {
                    if ((::GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) (void)::SetWindowTheme(h, L"", L"");
                }
                return TRUE;
            };
            (void)::EnumChildWindows(hwnd, untheme_group, 0);
            if (state->hScrollPane) (void)::EnumChildWindows(state->hScrollPane, untheme_group, 0);
            if (state->hScrollPane) {
                chat_provider_dlg_subclass_comboboxes(state->hScrollPane, *state);
                chat_dlg_subclass_desc_wheel_in(state->hScrollPane, *state);
            }
            chat_dlg_apply_scroll_layout(state->hScrollPane, *state, 0, formContentH);
        }
        ::InvalidateRect(hwnd, nullptr, TRUE);

        chat_dlg_start_replicate_async_init(hwnd, *state);

        if (state->hImgProvider)
            ::SetFocus(state->hImgProvider);
            else if (state->hModel.get())
                    ::SetFocus(state->hModel.get());
        else
            ::SetFocus(state->hRouter);
        return FALSE;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: {
        if (!state || !state->hDlgBg) break;
        HDC                 hdc = reinterpret_cast<HDC>(wp);
        const auto&         pal = pmui::theme_palette();
        ::SetBkColor(hdc, pal.window_bg);
        ::SetTextColor(hdc, pal.window_fg);
        return reinterpret_cast<INT_PTR>(state->hDlgBg);
    }
    case WM_CTLCOLOREDIT: {
        if (!state || !state->hCtlBg) break;
        HDC         hdc = reinterpret_cast<HDC>(wp);
        const auto& pal = pmui::theme_palette();
        ::SetBkColor(hdc, pal.control_bg);
        ::SetTextColor(hdc, pal.control_fg);
        return reinterpret_cast<INT_PTR>(state->hCtlBg);
    }
    case WM_CTLCOLORBTN: {
        if (!state || !state->hDlgBg) break;
        HWND hCtl = reinterpret_cast<HWND>(lp);
        if (!hCtl) break;
        const int  t  = static_cast<int>(::GetWindowLongW(hCtl, GWL_STYLE) & BS_TYPEMASK);
        HDC        hdc = reinterpret_cast<HDC>(wp);
        const auto& pal = pmui::theme_palette();
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
    case WM_PM_CHATDLG_REPINIT: {
        auto* p = reinterpret_cast<ChatDlgRepinitPayload*>(lp);
        if (!p) return TRUE;
        if (!state) {
            delete p;
            return TRUE;
        }
        if (p->ok_img && state->hImgCollection && state->hImgModel) {
            state->img_replicate_state = std::move(p->img);
            state->rep_ctl.reload_apply_ui(state->hImgCollection, state->hImgModel, state->img_replicate_state,
                state->cur.image_model);
            state->rep_ctl.update_meta_text(nullptr, state->img_replicate_state,
                state->rep_ctl.selected_model_slug(state->hImgModel));
        }
        if (p->ok_vid && state->hVidCollection && state->hVidModel) {
            state->vid_replicate_state = std::move(p->vid);
            state->rep_ctl.reload_apply_ui(state->hVidCollection, state->hVidModel, state->vid_replicate_state,
                state->cur.video_model);
            state->rep_ctl.update_meta_text(nullptr, state->vid_replicate_state,
                state->rep_ctl.selected_model_slug(state->hVidModel));
        }
        if (p->ok_ir && state->hIrCollection && state->hIrModel) {
            state->ir_replicate_state = std::move(p->ir);
            state->rep_ctl.reload_apply_ui(state->hIrCollection, state->hIrModel, state->ir_replicate_state,
                state->cur.image_recognition_model);
            state->rep_ctl.update_meta_text(nullptr, state->ir_replicate_state,
                state->rep_ctl.selected_model_slug(state->hIrModel));
        }
        delete p;
        return TRUE;
    }
    case WM_DESTROY: {
        if (state) {
            if (state->replicate_async_cancel) state->replicate_async_cancel->store(true);
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
        const int code = HIWORD(wp);

        if (id == IDCANCEL) {
            ::EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        if (id == IDOK) {
            // Read fields back.
            const int idx = (int)::SendMessageW(state->hRouter, CB_GETCURSEL, 0, 0);
            if (idx >= 0 && idx < kRouterCount) state->cur.router = kRouters[idx].id;

            auto read_edit = [](HWND h) -> std::string {
                if (!h) return {};
                int n = ::GetWindowTextLengthW(h);
                std::wstring w(n, L'\0');
                if (n > 0) ::GetWindowTextW(h, w.data(), n + 1);
                return wide_to_utf8(w);
            };
            if (state->openrouter_model_ui && state->hModel.get())
                state->cur.model = state->or_ctl.selected_model_id(state->hModel);
            else if (state->pixlwiz_model_ui && state->hModel.get())
                state->cur.model = state->pw_ctl.selected_model_id(state->hModel);
            else if (state->openai_model_ui && state->hModel.get())
                state->cur.model = state->oai_ctl.selected_model_id(state->hModel);
            else
                state->cur.model = wide_to_utf8(state->hModel.get_value());
            // api_key / base_url: edited in Settings → API Providers, not in this dialog.
            state->cur.api_key.clear();
            state->cur.base_url.clear();
            const std::string mi_s = read_edit(state->hMax);
            try { int mi = std::stoi(mi_s); if (mi > 0 && mi < 100) state->cur.max_iterations = mi; } catch (...) {}
            const int ip_idx = (int)::SendMessageW(state->hImgProvider, CB_GETCURSEL, 0, 0);
            if (ip_idx >= 0 && ip_idx < (int)state->image_provider_ids.size())
                state->cur.image_provider = state->image_provider_ids[(size_t)ip_idx];
            state->cur.image_model = (state->cur.image_provider == "replicate")
                ? state->rep_ctl.selected_model_slug(state->hImgModel)
                : pmui::provider_models::combo_text(state->hImgModel);
            const int ir_idx = (int)::SendMessageW(state->hIrProvider, CB_GETCURSEL, 0, 0);
            if (ir_idx >= 0 && ir_idx < (int)state->image_provider_ids.size())
                state->cur.image_recognition_provider = state->image_provider_ids[(size_t)ir_idx];
            state->cur.image_recognition_model     = (state->cur.image_recognition_provider == "replicate")
                ? state->rep_ctl.selected_model_slug(state->hIrModel)
                : pmui::provider_models::combo_text(state->hIrModel);
            const int vid_idx = (int)::SendMessageW(state->hVidProvider, CB_GETCURSEL, 0, 0);
            if (vid_idx >= 0 && vid_idx < (int)state->image_provider_ids.size())
                state->cur.video_provider = state->image_provider_ids[(size_t)vid_idx];
            state->cur.video_model = (state->cur.video_provider == "replicate")
                ? state->rep_ctl.selected_model_slug(state->hVidModel)
                : pmui::provider_models::combo_text(state->hVidModel);

            state->cur.stt_provider = pmui::AudioSelectorController::selected_provider_id(
                state->hSttProvider, true);
            state->cur.stt_model    = pmui::AudioSelectorController::selected_model_id(state->hSttModel);
            state->cur.tts_provider = pmui::AudioSelectorController::selected_provider_id(
                state->hTtsProvider, false);
            state->cur.tts_model    = pmui::AudioSelectorController::selected_model_id(state->hTtsModel);
            state->cur.tts_voice_id = (state->cur.tts_provider == "elevenlabs")
                ? pmui::ElevenLabsSelectorController::selected_tts_voice(state->hTtsVoice)
                : std::string{};

            std::string err;
            if (!media::settings::save_chat_provider(state->cur, err)) {
                std::wstring msg =
                    pmui::chat_provider_dlg_i18n::strings_for(state->display_language).msg_save_fail;
                msg += utf8_to_wide(err);
                ::MessageBoxW(hwnd, msg.c_str(), pm::brand::k_app_id_w, MB_ICONERROR);
                return TRUE;
            }
            ::EndDialog(hwnd, IDOK);
            return TRUE;
        }
        if (id == IDC_CHATDLG_ROUTER && code == CBN_SELCHANGE) {
            const int   idx  = (int)::SendMessageW(state->hRouter, CB_GETCURSEL, 0, 0);
            const HINSTANCE   inst0 = ::GetModuleHandleW(nullptr);
            const HFONT       f0    = pmui::ui_font();
            if (idx >= 0 && idx < kRouterCount) {
                state->cur.base_url.clear();
            }
            if (idx < 0 || idx >= kRouterCount) return TRUE;
            const bool was_or  = state->openrouter_model_ui;
            const bool was_pw  = state->pixlwiz_model_ui;
            const bool was_oai = state->openai_model_ui;
            const bool new_or  = chat_dlg_id_is_openrouter(kRouters[idx].id);
            const bool new_pw  = chat_dlg_id_is_pixlwiz(kRouters[idx].id);
            const bool new_oai = chat_dlg_id_is_openai(kRouters[idx].id);
            if (was_or == new_or && was_pw == new_pw && was_oai == new_oai) {
                if (!new_or && !new_pw && !new_oai && state->hModel.get() && kRouters[idx].default_model[0]) {
                    if (state->hModel.get_value().empty())
                        state->hModel.set_value(utf8_to_wide(kRouters[idx].default_model));
                }
                return TRUE;
            }
            chat_dlg_ensure_model_row_mode(state->hScrollPane, *state, kRouters[idx].id, kRouters[idx].default_model, inst0, f0);
            pmui::theme_init_from_settings();
            {
                const auto& pal = pmui::theme_palette();
                // Newly rebuilt controls are direct children of the scroll pane. The theme helper
                // themes descendants, not the root HWND itself, so run it from the pane.
                if (state->hScrollPane) {
                    (void)pmui::apply_window_theme_recursive(state->hScrollPane, pal.dark);
                    if (pal.dark) (void)::SetWindowTheme(state->hScrollPane, L"DarkMode_Explorer", nullptr);
                }
            }
            if (state->hScrollPane) {
                chat_provider_dlg_subclass_comboboxes(state->hScrollPane, *state);
                chat_dlg_subclass_desc_wheel_in(state->hScrollPane, *state);
            }
            chat_dlg_refresh_scroll_layout(*state);
            return TRUE;
        }
        // Note: Refresh/Open URL buttons removed for text-LLM; handled in ProviderDlg.
        if (id == IDC_CHATDLG_MODEL && code == CBN_SELCHANGE
            && (state->openrouter_model_ui || state->pixlwiz_model_ui || state->openai_model_ui)) {
            if (state->pixlwiz_model_ui)
                chat_dlg_set_pw_meta(*state);
            else if (state->openai_model_ui && state->hOrMeta)
                state->oai_ctl.update_meta_text(state->hOrMeta, state->oai_state,
                                                state->oai_ctl.selected_model_id(state->hModel));
            else
                chat_dlg_set_or_meta(*state);
            return TRUE;
        }
        if (id == IDC_CHATDLG_IMG_PROVIDER && code == CBN_SELCHANGE) {
            reload_image_models(state);
            return TRUE;
        }
        if (id == IDC_CHATDLG_IMG_COLLECTION && code == CBN_SELCHANGE) {
            if (state->rep_ctl.apply_collection_from_combo(state->hImgCollection, state->img_replicate_state)) {
                const std::string keep = state->rep_ctl.selected_model_slug(state->hImgModel);
                std::wstring rep_err;
                (void)state->rep_ctl.reload_using_saved_settings(
                    state->hImgCollection, state->hImgModel,
                    state->img_replicate_state, keep, false, false, &rep_err);
            }
            return TRUE;
        }
        if (id == IDC_CHATDLG_IMG_REFRESH && code == BN_CLICKED) {
            const std::string keep = state->rep_ctl.selected_model_slug(state->hImgModel);
            std::wstring rep_err;
            (void)state->rep_ctl.reload_using_saved_settings(
                state->hImgCollection, state->hImgModel,
                state->img_replicate_state, keep, true, true, &rep_err);
            return TRUE;
        }
        if (id == IDC_CHATDLG_VID_PROVIDER && code == CBN_SELCHANGE) {
            reload_video_models(state);
            return TRUE;
        }
        if (id == IDC_CHATDLG_VID_COLLECTION && code == CBN_SELCHANGE) {
            if (state->rep_ctl.apply_collection_from_combo(state->hVidCollection, state->vid_replicate_state)) {
                const std::string keep = state->rep_ctl.selected_model_slug(state->hVidModel);
                std::wstring rep_err;
                (void)state->rep_ctl.reload_using_saved_settings(
                    state->hVidCollection, state->hVidModel,
                    state->vid_replicate_state, keep, false, false, &rep_err);
            }
            return TRUE;
        }
        if (id == IDC_CHATDLG_VID_REFRESH && code == BN_CLICKED) {
            const std::string keep = state->rep_ctl.selected_model_slug(state->hVidModel);
            std::wstring rep_err;
            (void)state->rep_ctl.reload_using_saved_settings(
                state->hVidCollection, state->hVidModel,
                state->vid_replicate_state, keep, true, true, &rep_err);
            return TRUE;
        }
        if (id == IDC_CHATDLG_IR_PROVIDER && code == CBN_SELCHANGE) {
            reload_image_recognition_models(state);
            return TRUE;
        }
        if (id == IDC_CHATDLG_IR_COLLECTION && code == CBN_SELCHANGE) {
            if (state->rep_ctl.apply_collection_from_combo(state->hIrCollection, state->ir_replicate_state)) {
                const std::string keep = state->rep_ctl.selected_model_slug(state->hIrModel);
                std::wstring rep_err;
                (void)state->rep_ctl.reload_using_saved_settings(
                    state->hIrCollection, state->hIrModel,
                    state->ir_replicate_state, keep, false, false, &rep_err);
            }
            return TRUE;
        }
        if (id == IDC_CHATDLG_IR_REFRESH && code == BN_CLICKED) {
            const std::string keep = state->rep_ctl.selected_model_slug(state->hIrModel);
            std::wstring rep_err;
            (void)state->rep_ctl.reload_using_saved_settings(
                state->hIrCollection, state->hIrModel,
                state->ir_replicate_state, keep, true, true, &rep_err);
            return TRUE;
        }
        if (id == IDC_CHATDLG_STT_PROVIDER && code == CBN_SELCHANGE) {
            const std::string pid = pmui::AudioSelectorController::selected_provider_id(
                state->hSttProvider, true);
            const std::string cur = pmui::AudioSelectorController::selected_model_id(state->hSttModel);
            pmui::AudioSelectorController::populate_model_combo(state->hSttModel, pid, true, cur);
            return TRUE;
        }
        if (id == IDC_CHATDLG_TTS_PROVIDER && code == CBN_SELCHANGE) {
            const std::string pid = pmui::AudioSelectorController::selected_provider_id(
                state->hTtsProvider, false);
            const std::string cur = pmui::AudioSelectorController::selected_model_id(state->hTtsModel);
            pmui::AudioSelectorController::populate_model_combo(state->hTtsModel, pid, false, cur);
            // Refresh voice combo: populate + enable for ElevenLabs, clear + disable otherwise.
            const bool is_el = (pid == "elevenlabs");
            if (is_el)
                pmui::ElevenLabsSelectorController::populate_tts_voice_combo(
                    state->hTtsVoice, state->cur.tts_voice_id);
            else {
                state->hTtsVoice.set_items({}, {});
                state->hTtsVoice.set_value(L"");
            }
            ::EnableWindow(state->hTtsVoice.get(), is_el ? TRUE : FALSE);
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

bool ShowChatProviderSettingsDlg(HWND parent)
{
    DlgState state;
    std::string err;
    {
        media::settings::AppearanceSettings app{};
        media::settings::load_appearance(app, err);
        state.display_language = app.display_language;
        err.clear();
    }
    media::settings::load_chat_provider(state.cur, err);   // best-effort

    // In-memory dialog template (matches ProviderDlg's pattern).
    alignas(DWORD) BYTE buf[1024]{};
    DLGTEMPLATE* dlg = reinterpret_cast<DLGTEMPLATE*>(buf);
    dlg->style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    dlg->cdit  = 0;
    dlg->x = 0; dlg->y = 0;
    // `DLGTEMPLATE` `cx`/`cy` are dialog *client* dimensions in dialog units (DS_SETFONT).
    dlg->cx = (SHORT)(kClientW * 4 / 7);
    dlg->cy = (SHORT)(kClientH * 8 / 15);

    WORD* p = reinterpret_cast<WORD*>(dlg + 1);
    *p++ = 0;  // menu atom
    *p++ = 0;  // class atom (default dialog)
    {
        const pmui::chat_provider_dlg_i18n::Strings& tr_title =
            pmui::chat_provider_dlg_i18n::strings_for(state.display_language);
        const size_t n = std::wcslen(tr_title.window_title) + 1;
        memcpy(p, tr_title.window_title, n * sizeof(wchar_t));
        p += n * sizeof(wchar_t) / sizeof(WORD);
    }
    *p++ = 9;  // point size
    const wchar_t font[] = L"Segoe UI";
    memcpy(p, font, sizeof(font));
    p += sizeof(font) / sizeof(WORD);

    INT_PTR result = ::DialogBoxIndirectParamW(
        ::GetModuleHandleW(nullptr), dlg, parent,
        ChatDlgProc, reinterpret_cast<LPARAM>(&state));

    return result == IDOK;
}
