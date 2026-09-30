#include "ui_dialog_relayout.hpp"

#include "ui_constants.hpp"
#include "ui_font.hpp"

#include <algorithm>
#include <vector>

namespace pmui {

RECT child_rect_client_of_parent(HWND parent, HWND child)
{
    RECT wr{};
    if (!parent || !child || !::GetWindowRect(child, &wr))
        return wr;
    POINT tl{wr.left, wr.top};
    POINT br{wr.right, wr.bottom};
    ::ScreenToClient(parent, &tl);
    ::ScreenToClient(parent, &br);
    RECT rc{tl.x, tl.y, br.x, br.y};
    return rc;
}

void relayout_dialog_children_vertical_stack_for_ui_font(HWND hDlg)
{
    const HFONT hf = ui_font();
    if (!hf || !::IsWindow(hDlg))
        return;

    struct Row {
        HWND  hwnd{};
        RECT  orig{};
        WCHAR cls[16]{};
        int   newH{};
    };
    std::vector<Row> rows;
    rows.reserve(32);

    ::EnumChildWindows(
        hDlg,
        [](HWND w, LPARAM p) -> BOOL {
            auto *out = reinterpret_cast<std::vector<Row> *>(p);
            Row r{};
            r.hwnd = w;
            r.orig = child_rect_client_of_parent(::GetParent(w), w);
            (void)::GetClassNameW(w, r.cls, static_cast<int>(std::size(r.cls)));
            out->push_back(r);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&rows));

    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.orig.top != b.orig.top)
            return a.orig.top < b.orig.top;
        return a.orig.left < b.orig.left;
    });

    HDC hdc = ::GetDC(hDlg);
    if (!hdc)
        return;
    HFONT old = (HFONT)::SelectObject(hdc, hf);
    TEXTMETRICW tm{};
    (void)::GetTextMetricsW(hdc, &tm);
    const int lineH = tm.tmHeight + tm.tmExternalLeading;
    constexpr UINT kDt = DT_LEFT | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT;

    for (Row &r : rows) {
        const int w = r.orig.right - r.orig.left;
        int       newH = r.orig.bottom - r.orig.top;

        if (::_wcsicmp(r.cls, L"Static") == 0) {
            wchar_t text[512]{};
            const int n = ::GetWindowTextW(r.hwnd, text, static_cast<int>(std::size(text)));
            const int tw = w > 1 ? w : 1;
            RECT      tr{0, 0, tw, 0};
            ::DrawTextW(hdc, text, (n > 0) ? n : -1, &tr, kDt);
            const int need = (tr.bottom - tr.top) + 4;
            if (need > newH)
                newH = need;
        }
        else if (::_wcsicmp(r.cls, L"Button") == 0) {
            const int t = static_cast<int>(::GetWindowLongW(r.hwnd, GWL_STYLE) & BS_TYPEMASK);
            if (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE || t == BS_AUTORADIOBUTTON) {
                wchar_t text[512]{};
                const int n = ::GetWindowTextW(r.hwnd, text, static_cast<int>(std::size(text)));
                const int rawTw = w - 24;
                const int textW = rawTw > 1 ? rawTw : 1;
                RECT      tr{0, 0, textW, 0};
                ::DrawTextW(hdc, text, (n > 0) ? n : -1, &tr, kDt);
                int need = (tr.bottom - tr.top) + 4;
                if (need < lineH + 6)
                    need = lineH + 6;
                if (need > newH)
                    newH = need;
            }
            else {
                const int minPush = lineH + 10;
                if (minPush > newH)
                    newH = minPush;
            }
        }
        else if (::_wcsicmp(r.cls, L"Edit") == 0) {
            const LONG es = ::GetWindowLongW(r.hwnd, GWL_STYLE);
            const bool multi = (es & ES_MULTILINE) != 0;
            const int  minMulti = lineH * 2 + 12;
            const int  minH = multi ? (newH > minMulti ? newH : minMulti) : (lineH + 8);
            if (minH > newH)
                newH = minH;
        }
        r.newH = newH;
    }

    (void)::SelectObject(hdc, old);
    ::ReleaseDC(hDlg, hdc);

    int yShift = 0;
    for (Row &r : rows) {
        const int oldH = r.orig.bottom - r.orig.top;
        const int x = r.orig.left;
        const int top = r.orig.top + yShift;
        const int wid = r.orig.right - r.orig.left;
        ::SetWindowPos(r.hwnd, nullptr, x, top, wid, r.newH, SWP_NOZORDER | SWP_NOACTIVATE);
        yShift += (r.newH - oldH);
    }

    if (yShift <= 0)
        return;

    int maxBottom = 0;
    ::EnumChildWindows(
        hDlg,
        [](HWND w, LPARAM p) -> BOOL {
            int *mb = reinterpret_cast<int *>(p);
            RECT rc = child_rect_client_of_parent(::GetParent(w), w);
            if (rc.bottom > *mb)
                *mb = rc.bottom;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&maxBottom));

    RECT cr{};
    ::GetClientRect(hDlg, &cr);
    const int margin = 8;
    const int newClientH = (maxBottom + margin > cr.bottom) ? (maxBottom + margin) : cr.bottom;
    if (newClientH <= cr.bottom)
        return;

    const DWORD style = static_cast<DWORD>(::GetWindowLongPtr(hDlg, GWL_STYLE));
    const DWORD exstyle = static_cast<DWORD>(::GetWindowLongPtr(hDlg, GWL_EXSTYLE));
    RECT adj{0, 0, cr.right, newClientH};
    if (!::AdjustWindowRectEx(&adj, style, FALSE, exstyle))
        return;
    const int winW = adj.right - adj.left;
    const int winH = adj.bottom - adj.top;
    ::SetWindowPos(hDlg, nullptr, 0, 0, winW, winH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void relayout_settings_scroll_host_statics_for_ui_font(HWND host, HFONT hf, LONG_PTR settings_sep_user_data,
    LONG_PTR settings_card_user_data)
{
    if (!::IsWindow(host) || !hf)
        return;

    using L = pmui::ui::SettingsPaneLayout;
    const int rowDy = L::row_dy();

    struct Ctx {
        HWND                     host{};
        HFONT                    hf{};
        LONG_PTR                 sep_ud{};
        LONG_PTR                 card_ud{};
        int                      host_client_w{};
        std::vector<std::pair<HWND, RECT>> *positions{};
    };
    std::vector<std::pair<HWND, RECT>> items;
    RECT hostCr{};
    (void)::GetClientRect(host, &hostCr);
    Ctx ctx{host, hf, settings_sep_user_data, settings_card_user_data, (int)hostCr.right, &items};

    ::EnumChildWindows(
        host,
        [](HWND w, LPARAM lp) -> BOOL {
            auto *c = reinterpret_cast<Ctx *>(lp);
            if (::GetParent(w) != c->host)
                return TRUE;
            const LONG_PTR ud = ::GetWindowLongPtr(w, GWLP_USERDATA);
            if (ud == c->sep_ud || ud == c->card_ud)
                return TRUE;
            RECT rc = child_rect_client_of_parent(c->host, w);
            c->positions->push_back({w, rc});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));

    HDC hdc = ::GetDC(host);
    if (!hdc)
        return;
    HFONT old = (HFONT)::SelectObject(hdc, hf);
    TEXTMETRICW tm{};
    (void)::GetTextMetricsW(hdc, &tm);
    const int lineH = tm.tmHeight + tm.tmExternalLeading;
    constexpr UINT kDt = DT_LEFT | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT;

    for (auto &pr : items) {
        HWND w = pr.first;
        RECT rc = pr.second;
        const int oldH = rc.bottom - rc.top;
        const int wid = rc.right - rc.left;
        if (wid < 1)
            continue;

        WCHAR cls[16]{};
        (void)::GetClassNameW(w, cls, static_cast<int>(std::size(cls)));

        int newH = oldH;

        if (::_wcsicmp(cls, L"Static") == 0) {
            const DWORD st = static_cast<DWORD>(::GetWindowLongW(w, GWL_STYLE));
            const UINT  type = st & SS_TYPEMASK;
            const bool  ss_right = (type == SS_RIGHT);

            wchar_t text[768]{};
            const int n = ::GetWindowTextW(w, text, static_cast<int>(std::size(text)));
            const int tw = wid > 1 ? wid : 1;
            RECT      tr{0, 0, tw, 0};
            ::DrawTextW(hdc, text, (n > 0) ? n : -1, &tr, kDt);
            int need = (tr.bottom - tr.top) + 4;

            int cap = rowDy + 8;
            if (ss_right) {
                cap = rowDy - 2;
            }
            else if (wid >= (std::max)(120, ctx.host_client_w / 2 - 8)) {
                cap = (std::max)(L::section_header_h + 6, rowDy - 4);
            }
            if (need > cap)
                need = cap;
            if (need > newH)
                newH = need;
        }
        else if (::_wcsicmp(cls, L"Button") == 0) {
            const int t = static_cast<int>(::GetWindowLongW(w, GWL_STYLE) & BS_TYPEMASK);
            if (t == BS_AUTOCHECKBOX || t == BS_AUTO3STATE || t == BS_AUTORADIOBUTTON) {
                wchar_t text[512]{};
                const int n = ::GetWindowTextW(w, text, static_cast<int>(std::size(text)));
                const int rawTw = wid - 24;
                const int textW = rawTw > 1 ? rawTw : 1;
                RECT      tr{0, 0, textW, 0};
                ::DrawTextW(hdc, text, (n > 0) ? n : -1, &tr, kDt);
                int need = (tr.bottom - tr.top) + 4;
                if (need < lineH + 6)
                    need = lineH + 6;
                if (need > rowDy + 4)
                    need = rowDy + 4;
                if (need > newH)
                    newH = need;
            }
            else {
                const int minPush = lineH + 10;
                const int capped = (std::min)(minPush, rowDy + 6);
                if (capped > newH)
                    newH = capped;
            }
        }
        else if (::_wcsicmp(cls, L"Edit") == 0) {
            const LONG es = ::GetWindowLongW(w, GWL_STYLE);
            if ((es & ES_MULTILINE) == 0) {
                const int minH = lineH + 8;
                const int capped = (std::min)(minH, rowDy + 6);
                if (capped > newH)
                    newH = capped;
            }
        }

        if (newH != oldH) {
            (void)::SetWindowPos(w, nullptr, rc.left, rc.top, wid, newH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    (void)::SelectObject(hdc, old);
    ::ReleaseDC(host, hdc);
}

} // namespace pmui
