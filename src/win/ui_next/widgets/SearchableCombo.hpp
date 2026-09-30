#pragma once
// SearchableCombo — CBS_DROPDOWN with live substring filtering and free-text fallback.
// See docs/combobox.md for design rationale, dark-mode and HiDPI notes.
//
// Theming: handled entirely by apply_window_theme_recursive (COMBOBOX path); no extra
// theming calls are needed from the caller.
//
// Ownership: the Win32 HWND is owned by the parent dialog, not by this object.
// SearchableCombo is a thin HWND wrapper (copyable / moveable after create()).
// Internal state is heap-allocated and attached to the HWND; it is freed automatically
// on WM_NCDESTROY.

#include <Windows.h>
#include <string>
#include <vector>

namespace pmui::widgets {

struct SearchableComboParams {
    HWND      parent{};
    HINSTANCE inst{};
    int       id{};
    int       x{};
    int       y{};
    int       w{};          ///< Width in device pixels — caller pre-scales with dpi_scale()
    int       drop_h{220};  ///< Total combo height (display + list) in device pixels
    int       min_query_len{0};  ///< Min chars typed before filtering kicks in (0 = always)
    bool      trim_value{true};  ///< Strip leading/trailing whitespace from get_value()
};

/// CBS_DROPDOWN combo with live substring-filter and free-text fallback.
///
/// Usage:
///   combo.create(params);
///   combo.set_items(labels, values);   // load catalog
///   combo.set_value(L"gpt-4o");        // pre-select (or sets free text)
///   // …later…
///   std::wstring v = combo.get_value(); // always the raw EDIT text
///
/// Notifications: CBN_SELCHANGE and CBN_EDITCHANGE are forwarded to the parent
/// dialog as usual — no change to existing WM_COMMAND handlers required.
class SearchableCombo {
public:
    SearchableCombo() = default;
    explicit SearchableCombo(HWND h) : m_h(h) {}

    /// Create the control and attach internal state to the HWND.
    HWND create(const SearchableComboParams& p);

    /// Replace the full candidate list and re-filter against the current edit text.
    /// @param labels  Display strings shown in the list.
    /// @param values  Canonical values returned by get_value() on list selection.
    ///                If empty, labels serve as values.
    void set_items(std::vector<std::wstring> labels,
                   std::vector<std::wstring> values = {});

    /// Set the edit field text.
    /// Tries CB_SELECTSTRING against the current (filtered) list first so that the
    /// highlighted selection is consistent; falls back to SetWindowText for free text.
    void set_value(const std::wstring& v);

    /// Returns the current edit-field text, optionally trimmed (see trim_value).
    /// The returned string may not match any list item — that is intentional.
    [[nodiscard]] std::wstring get_value() const;

    /// Rebuild the visible drop list filtered by the current edit text.
    /// Called automatically on CBN_EDITCHANGE; call explicitly after set_items() when
    /// the edit field already contains text that should restrict the list.
    void refilter();

    [[nodiscard]] HWND get() const { return m_h; }
    [[nodiscard]] explicit operator HWND() const { return m_h; }

private:
    HWND m_h{};
};

} // namespace pmui::widgets
