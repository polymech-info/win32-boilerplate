#pragma once

#include <string_view>

namespace pmui::dock_chrome_i18n {

struct Strings {
    const wchar_t* chat_tab;
    const wchar_t* chat_caption;
    const wchar_t* files_tab;
    const wchar_t* explorer_caption;
    const wchar_t* queue_tab;
    const wchar_t* file_queue_caption;
    const wchar_t* log_tab;
    const wchar_t* log_caption;
    const wchar_t* file_info_tab;
    const wchar_t* file_info_caption;
    const wchar_t* find_tab;
    const wchar_t* find_results_caption;
    const wchar_t* dup_tab;
    const wchar_t* dup_results_caption;
    const wchar_t* nodes_tab;
    const wchar_t* nodes_caption;
    /// Log panel tool strip — copy button (short label beside icon).
    const wchar_t* log_toolbar_copy;
    /// Tooltip for the copy button.
    const wchar_t* log_toolbar_copy_tip;
    /// Log panel tool strip — clear button (short label beside icon).
    const wchar_t* log_toolbar_clear;
    /// Tooltip for the clear button.
    const wchar_t* log_toolbar_clear_tip;
    /// CDockViewerPanel tab label and dock caption.
    const wchar_t* viewer_tab;
    const wchar_t* viewer_caption;
};

[[nodiscard]] const Strings& strings_for(std::string_view display_language);

} // namespace pmui::dock_chrome_i18n
