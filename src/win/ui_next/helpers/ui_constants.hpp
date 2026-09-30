#pragma once
// Central layout metrics for the Win32 / Win32xx UI chrome, settings panes, and related controls.
// See docs/win32xx-next.md (Policy) — values stay in sync with CSettingsView::CreateControls.

namespace pmui {
namespace ui {

/// Spacing and sizes shared across Resize / Compress / Meta / Transform / Find / Duplicates panels.
struct SettingsPaneLayout {
    static constexpr int pad_x           = 16;
    static constexpr int label_w         = 96;
    static constexpr int gap               = 8;
    static constexpr int control_h         = 24;
    static constexpr int row_gap           = 8;
    static constexpr int label_h           = 18; ///< Field labels (STATIC, SS_RIGHT)
    static constexpr int browse_min_w      = 72;
    static constexpr int browse_field_gap  = 8;
    static constexpr int section_header_h  = 16; ///< “Dimensions”, “Output”, …
    static constexpr int after_sep_pad     = 10;
    static constexpr int after_header_pad  = 18; ///< Line under section title before first row
    static constexpr int tight_check_h     = 22; ///< Legacy rows that still use 22px (Transform until unified)
    static constexpr int scrollbar_right_pad = 24; ///< Reserve space for vertical scrollbar so content isn't clipped

    static constexpr int field_x() { return pad_x + label_w + gap; }
    static constexpr int row_dy() { return control_h + row_gap; }
};

/// Small fixed-size modals that stack `BS_GROUPBOX` sections (e.g. `ChatProviderDlg.cpp`).
/// Tighter than `SettingsPaneLayout` (right-dock); use for DIALOGEX client metrics + group insets.
struct GroupedModalLayout {
    static constexpr int client_margin_x  = 14; ///< from dialog client edge to group frame (outer)
    static constexpr int group_inset_x     = 10; ///< label/field inset *inside* the group frame
    static constexpr int label_w           = 92;
    static constexpr int field_gap         = 8; ///< label column → control ([SettingsPaneLayout::gap] match)
    static constexpr int group_top_pad     = 28; ///< first row below the group title
    static constexpr int group_bottom_pad  = 14; ///< padding counted into groupbox height under last control
    static constexpr int inter_group_gap   = 14; ///< vertical space between stacked groups
    static constexpr int content_x0() { return client_margin_x + group_inset_x; }
    static constexpr int field_x() { return content_x0() + label_w + field_gap; }
};

/// Single-row own-ribbon toolbar metrics. Values are logical pixels; call `DpiScaleInt` at use sites.
struct RibbonStripLayout {
    static constexpr int icon_large          = 24;
    static constexpr int icon_small          = 16;
    static constexpr int icon_visual_max     = 28;
    static constexpr int menu_icon           = 16;

    static constexpr int toolbar_pad_x       = 12;
    static constexpr int toolbar_pad_y       = 8;
    static constexpr int toolbar_indent      = 6;
    static constexpr int icon_only_button_w  = 36;

    static constexpr int dropdown_arrow_w    = 14;
    static constexpr int dropdown_half_w_min = 3;
    static constexpr int dropdown_half_w     = 4;
    static constexpr int dropdown_half_h_min = 2;
    static constexpr int dropdown_half_h     = 3;

    static constexpr int dark_state_inset_x  = 1;
    static constexpr int dark_state_inset_y  = 1;
    static constexpr int content_inset_x     = 2;
    static constexpr int labeled_icon_top    = 4;
    static constexpr int labeled_icon_gap    = 3;
    static constexpr int label_inset_x       = 3;
    static constexpr int label_bottom_slack  = 2;

    static constexpr int fallback_text_h     = 22;
    static constexpr int fallback_pad        = 10;
};

} // namespace ui
} // namespace pmui
