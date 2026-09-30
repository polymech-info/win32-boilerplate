#pragma once

#include <Windows.h>

namespace pmui {

RECT child_rect_client_of_parent(HWND parent, HWND child);

/// Single-column modal dialogs: measure text/control heights and vertically restack children.
void relayout_dialog_children_vertical_stack_for_ui_font(HWND hDlg);

/// Settings scroll host: after `WM_SETFONT`, grow `STATIC` / checkbox / single-line `EDIT` heights in
/// place (two-column layout is preserved — no global Y-sort). Skips `settings_sep_user_data` /
/// `settings_card_user_data` markers on `GWLP_USERDATA`.
void relayout_settings_scroll_host_statics_for_ui_font(HWND host, HFONT hf, LONG_PTR settings_sep_user_data,
    LONG_PTR settings_card_user_data);

} // namespace pmui
