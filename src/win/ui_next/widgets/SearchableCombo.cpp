#include "SearchableCombo.hpp"

#include <Windows.h>
#include <commctrl.h>
#pragma comment(lib, "Comctl32.lib")

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace pmui::widgets {
namespace {

// ── Per-HWND state ────────────────────────────────────────────────────────────

struct SCState {
    std::vector<std::wstring> labels;   ///< Full (unfiltered) display strings
    std::vector<std::wstring> values;   ///< Parallel canonical values; empty = use labels
    int  min_query_len{0};
    bool trim_value{true};
    bool filtering{false};              ///< Re-entrancy guard for refilter()
};

static const wchar_t* const kStateProp = L"PMSC_State";

inline SCState* state_of(HWND h)
{
    return reinterpret_cast<SCState*>(::GetPropW(h, kStateProp));
}

// ── String helpers ────────────────────────────────────────────────────────────

static std::wstring to_lower_w(std::wstring s)
{
    for (wchar_t& c : s)
        c = static_cast<wchar_t>(::towlower(static_cast<wint_t>(c)));
    return s;
}

static std::wstring trim_ws(const std::wstring& s)
{
    const wchar_t* b = s.c_str();
    const wchar_t* e = b + s.size();
    while (b < e && (*b == L' ' || *b == L'\t')) ++b;
    while (e > b && (e[-1] == L' ' || e[-1] == L'\t')) --e;
    return std::wstring(b, e);
}

static std::wstring edit_text(HWND combo)
{
    wchar_t buf[2048]{};
    ::GetWindowTextW(combo, buf, static_cast<int>(std::size(buf)));
    return buf;
}

static HWND edit_child(HWND combo)
{
    COMBOBOXINFO cbi{ sizeof(cbi) };
    return ::GetComboBoxInfo(combo, &cbi) ? cbi.hwndItem : nullptr;
}

// ── Drop-width helper ─────────────────────────────────────────────────────────
// Widens the closed combo's drop-down to fit the longest item text.
// DPI-aware padding (docs/combobox.md §5.4).

static void ensure_drop_width(HWND h)
{
    if (!h) return;
    const int count = static_cast<int>(::SendMessageW(h, CB_GETCOUNT, 0, 0));
    if (count <= 0) return;

    RECT rc{};
    ::GetWindowRect(h, &rc);
    int max_w = rc.right - rc.left;

    HDC hdc = ::GetDC(h);
    if (!hdc) return;
    const HFONT hf  = reinterpret_cast<HFONT>(::SendMessageW(h, WM_GETFONT, 0, 0));
    const HGDIOBJ o = hf ? ::SelectObject(hdc, hf) : nullptr;

    for (int i = 0; i < count; ++i) {
        const int len = static_cast<int>(::SendMessageW(h, CB_GETLBTEXTLEN, static_cast<WPARAM>(i), 0));
        if (len <= 0) continue;
        std::wstring txt(static_cast<size_t>(len) + 1, L'\0');
        ::SendMessageW(h, CB_GETLBTEXT, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(txt.data()));
        txt.resize(static_cast<size_t>(len));
        SIZE sz{};
        if (::GetTextExtentPoint32W(hdc, txt.c_str(), static_cast<int>(txt.size()), &sz)) {
            const UINT dpi = ::GetDpiForWindow(h);
            const int  pad = ::MulDiv(28, static_cast<int>(dpi ? dpi : 96), 96);
            max_w = (std::max)(max_w, static_cast<int>(sz.cx) + pad);
        }
    }

    if (o) ::SelectObject(hdc, o);
    ::ReleaseDC(h, hdc);
    ::SendMessageW(h, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(max_w), 0);
}

// ── Core refilter logic ───────────────────────────────────────────────────────

static void do_refilter(HWND h, SCState& st)
{
    if (st.filtering) return;
    st.filtering = true;

    // 1. Snapshot the edit text and caret so we can restore them after CB_RESETCONTENT
    //    clears the CBS_DROPDOWN edit child (docs/combobox.md §3.4).
    const std::wstring raw = edit_text(h);
    const HWND         ed  = edit_child(h);
    DWORD sel_s = 0, sel_e = 0;
    if (ed) ::SendMessageW(ed, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_s),
                                           reinterpret_cast<LPARAM>(&sel_e));

    // 2. Was the drop list visible before rebuild?
    const bool was_open = (::SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0) != 0);

    // 3. Build query string.
    const std::wstring query   = st.trim_value ? trim_ws(raw) : raw;
    const std::wstring q_lower = to_lower_w(query);
    const bool apply_filter    = static_cast<int>(q_lower.size()) >= st.min_query_len
                                 && !q_lower.empty();

    // 4. Rebuild the list (CB_RESETCONTENT clears the edit child — restored below).
    ::SendMessageW(h, WM_SETREDRAW, FALSE, 0);
    ::SendMessageW(h, CB_RESETCONTENT, 0, 0);

    int match_count = 0;
    for (const std::wstring& label : st.labels) {
        if (!apply_filter || to_lower_w(label).find(q_lower) != std::wstring::npos) {
            ::SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            ++match_count;
        }
    }

    // 5. Restore the edit text directly on the EDIT child (avoids CB_SELECTSTRING
    //    side-effects and keeps caret position sensible).
    if (ed) {
        ::SetWindowTextW(ed, raw.c_str());
        ::SendMessageW(ed, EM_SETSEL, sel_s, sel_e);
    }

    ::SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(h, nullptr, FALSE);

    // 6. Keep the drop list open if it was open and we still have items to show.
    if (was_open && match_count > 0)
        ::SendMessageW(h, CB_SHOWDROPDOWN, TRUE, 0);

    st.filtering = false;
}

// ── Subclass proc ─────────────────────────────────────────────────────────────

static constexpr UINT_PTR kSubclassId = 0xCA5C0001u;

static LRESULT CALLBACK sc_subclass_proc(
    HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR /*id*/, DWORD_PTR /*ref*/)
{
    if (msg == WM_COMMAND) {
        // The CBS_DROPDOWN EDIT child sends WM_COMMAND(EN_CHANGE) to the COMBOBOX (its
        // parent).  The COMBOBOX default proc then translates this into CBN_EDITCHANGE
        // and sends it further up to the dialog.  We intercept here — at combo level —
        // so we see EN_CHANGE (0x0300), NOT CBN_EDITCHANGE (5).
        // Only the embedded EDIT child can emit EN_CHANGE to a COMBOBOX, so checking
        // the notification code alone is sufficient.
        if (HIWORD(wp) == EN_CHANGE) {
            if (SCState* st = state_of(h))
                do_refilter(h, *st);
            // Fall through so DefSubclassProc relays CBN_EDITCHANGE to the parent dialog.
        }
    }

    if (msg == WM_NCDESTROY) {
        ::RemoveWindowSubclass(h, sc_subclass_proc, kSubclassId);
        if (SCState* st = state_of(h)) {
            ::RemovePropW(h, kStateProp);
            delete st;
        }
    }

    return ::DefSubclassProc(h, msg, wp, lp);
}

} // namespace

// ── SearchableCombo public API ────────────────────────────────────────────────

HWND SearchableCombo::create(const SearchableComboParams& p)
{
    // Allocate state before creating the window so it's always available in the
    // subclass proc even if the first WM_COMMAND fires synchronously.
    auto* st        = new SCState{};
    st->min_query_len = p.min_query_len;
    st->trim_value    = p.trim_value;

    // 1px top nudge — same convention as create_preset_combo (docs/combobox.md §5.1).
    const UINT dpi  = p.parent ? ::GetDpiForWindow(p.parent) : 96u;
    const int  nudge = ::MulDiv(1, static_cast<int>(dpi ? dpi : 96), 96);

    m_h = ::CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_HASSTRINGS | WS_VSCROLL,
        p.x, p.y - nudge, p.w, p.drop_h,
        p.parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(p.id)), p.inst, nullptr);

    if (!m_h) {
        delete st;
        return nullptr;
    }

    // Attach state to HWND so it survives C++ object moves/copies.
    ::SetPropW(m_h, kStateProp, reinterpret_cast<HANDLE>(st));

    // Intercept CBN_EDITCHANGE from the embedded EDIT child.
    ::SetWindowSubclass(m_h, sc_subclass_proc, kSubclassId, 0);

    // Cuebanner on the embedded EDIT child (docs/combobox.md §4.3).
    if (const HWND ed = edit_child(m_h))
        ::SendMessageW(ed, EM_SETCUEBANNER, FALSE,
                       reinterpret_cast<LPARAM>(L"type to filter\u2026"));

    return m_h;
}

void SearchableCombo::set_items(std::vector<std::wstring> labels,
                                std::vector<std::wstring> values)
{
    if (!m_h) return;
    SCState* st = state_of(m_h);
    if (!st) return;

    st->labels = std::move(labels);
    st->values = std::move(values);

    // Rebuild the visible list (honours whatever is in the edit field right now).
    do_refilter(m_h, *st);

    // Widen the closed combo to fit the longest label in the full list.
    // Temporarily load all items to measure, then re-filter back.
    // Simpler: measure from st->labels directly (avoid a second rebuild).
    HDC hdc = ::GetDC(m_h);
    if (hdc) {
        RECT rc{};
        ::GetWindowRect(m_h, &rc);
        int max_w = rc.right - rc.left;
        const HFONT   hf = reinterpret_cast<HFONT>(::SendMessageW(m_h, WM_GETFONT, 0, 0));
        const HGDIOBJ o  = hf ? ::SelectObject(hdc, hf) : nullptr;
        const UINT    dpi = ::GetDpiForWindow(m_h);
        const int     pad = ::MulDiv(28, static_cast<int>(dpi ? dpi : 96), 96);
        for (const std::wstring& lbl : st->labels) {
            SIZE sz{};
            if (::GetTextExtentPoint32W(hdc, lbl.c_str(), static_cast<int>(lbl.size()), &sz))
                max_w = (std::max)(max_w, static_cast<int>(sz.cx) + pad);
        }
        if (o) ::SelectObject(hdc, o);
        ::ReleaseDC(m_h, hdc);
        ::SendMessageW(m_h, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(max_w), 0);
    }
}

void SearchableCombo::set_value(const std::wstring& ws)
{
    if (!m_h) return;
    // Try to highlight the matching list item; fall back to free text.
    if (::SendMessageW(m_h, CB_SELECTSTRING, static_cast<WPARAM>(-1),
                       reinterpret_cast<LPARAM>(ws.c_str())) == CB_ERR) {
        ::SetWindowTextW(m_h, ws.c_str());
    }
}

std::wstring SearchableCombo::get_value() const
{
    if (!m_h) return {};
    const SCState* st = state_of(m_h);

    wchar_t buf[2048]{};
    ::GetWindowTextW(m_h, buf, static_cast<int>(std::size(buf)));
    std::wstring raw(buf);

    if (st && st->trim_value)
        return trim_ws(raw);
    return raw;
}

void SearchableCombo::refilter()
{
    if (!m_h) return;
    if (SCState* st = state_of(m_h))
        do_refilter(m_h, *st);
}

} // namespace pmui::widgets
