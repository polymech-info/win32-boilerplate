#pragma once

#include <string_view>

namespace pmui::settings_panel_i18n {

/** Dock tab + caption strings (Settings panel on the right). */
struct DockStrings {
    const wchar_t* tab_generic;
    const wchar_t* caption_resize;
    const wchar_t* caption_compress;
    const wchar_t* caption_meta;
    const wchar_t* caption_transform;
    const wchar_t* caption_find;
    const wchar_t* caption_duplicates;
    const wchar_t* tab_resize;
    const wchar_t* tab_compress;
    const wchar_t* tab_meta;
    const wchar_t* tab_transform;
    const wchar_t* tab_find;
    const wchar_t* tab_duplicates;
};

/** Resize mode controls in `CSettingsView::CreateControls`. */
struct ResizeStrings {
    const wchar_t* sec_dimensions;
    const wchar_t* sec_output;
    const wchar_t* sec_quality;
    const wchar_t* sec_options;

    const wchar_t* lbl_resolution;
    const wchar_t* lbl_ratio;
    const wchar_t* lbl_size; ///< Single row: width × height
    const wchar_t* lbl_width; // legacy (unused when lbl_size is used for the row)
    const wchar_t* lbl_height; // legacy
    const wchar_t* lbl_fit;
    const wchar_t* lbl_destination;
    const wchar_t* lbl_folder;
    const wchar_t* lbl_format;
    const wchar_t* lbl_kernel;

    const wchar_t* chk_autorot;
    const wchar_t* chk_enlarge;
    const wchar_t* chk_strip;

    const wchar_t* browse_output_folder_title;
    const wchar_t* msg_error_title;

    const wchar_t* res_preset[18];
    const wchar_t* ratio_preset[10];
    const wchar_t* fit[5];
    const wchar_t* dest[3];
    const wchar_t* format[6];
    const wchar_t* kernel[5];
};

[[nodiscard]] const DockStrings& dock_strings_for(std::string_view display_language);
[[nodiscard]] const ResizeStrings& resize_strings_for(std::string_view display_language);

/// Label for folder browse buttons (Resize / Compress / Meta output rows). Ellipsis is U+2026.
[[nodiscard]] const wchar_t* browse_button_caption(std::string_view display_language);

/** Compress mode — PNG / MozJPEG panel (see `FEATURE_*` in `SettingsPanel.cpp`). */
struct CompressStrings {
    const wchar_t* sec_output;
    const wchar_t* lbl_destination;
    const wchar_t* lbl_folder;
    const wchar_t* dest[3];
    const wchar_t* sec_format;
    const wchar_t* lbl_compressor;
    const wchar_t* compressor[2];
    const wchar_t* sec_png;
    const wchar_t* lbl_level;
    const wchar_t* sec_quantise;
    const wchar_t* chk_quantize;
    const wchar_t* lbl_colors;
    const wchar_t* colors[4];
    const wchar_t* lbl_png_quality;
    const wchar_t* sec_advanced;
    const wchar_t* chk_zopfli;
    const wchar_t* sec_mozjpeg;
    const wchar_t* lbl_jpeg_quality;
    const wchar_t* chk_progressive;
    const wchar_t* chk_trellis;
    const wchar_t* sec_options;
    const wchar_t* chk_strip;
    const wchar_t* browse_folder_title;
};

/** AI Transform mode. */
struct TransformStrings {
    const wchar_t* sec_model;
    const wchar_t* lbl_provider;
    const wchar_t* lbl_collection;
    const wchar_t* lbl_model;
    const wchar_t* model[2];
    const wchar_t* sec_output;
    const wchar_t* lbl_aspect;
    const wchar_t* aspect[6];
    const wchar_t* lbl_size;
    const wchar_t* size[5];
    const wchar_t* sec_prompt;
    const wchar_t* btn_presets;
    const wchar_t* btn_api_keys;
    const wchar_t* sec_ref;
    const wchar_t* btn_add_ref;
    const wchar_t* btn_clear;
    /// Pre-resize: uses Meta’s section title / “resize first” / width combo strings (see meta_strings_for).
    const wchar_t* chk_preresize_raw_only;
    const wchar_t* tt_refresh;   // ↻
    const wchar_t* tt_presets;
    const wchar_t* tt_api_keys;
    const wchar_t* tt_add_ref;
    const wchar_t* tt_clear_ref;
};

/** Meta / cataloguing mode. */
struct MetaStrings {
    const wchar_t* sec_output;
    const wchar_t* lbl_folder;
    const wchar_t* sec_write;
    const wchar_t* out_md;
    const wchar_t* out_json;
    const wchar_t* update_exif;
    const wchar_t* sec_preresize;
    const wchar_t* chk_resize_first;
    const wchar_t* lbl_width;
    const wchar_t* width_presets[4];
    const wchar_t* sec_provider;
    const wchar_t* lbl_provider;
    const wchar_t* lbl_collection;
    const wchar_t* provider_google;
    const wchar_t* lbl_model;
    const wchar_t* model[2];
    const wchar_t* sec_prompt;
    const wchar_t* lbl_preset;
    const wchar_t* preset[4];
    const wchar_t* btn_api_keys;
    const wchar_t* browse_folder_title;
    const wchar_t* tt_refresh;
};

/** Find / search mode. */
struct FindStrings {
    const wchar_t* sec_search;
    const wchar_t* sec_mode;
    const wchar_t* llm_semantic;
    const wchar_t* recurse;
    const wchar_t* match_parents;
    const wchar_t* sec_llm_cache;
    const wchar_t* bypass_cache;
    const wchar_t* no_generate;
    const wchar_t* use_md;
    const wchar_t* use_json;
    const wchar_t* use_exif;
    const wchar_t* sec_llm;
    const wchar_t* lbl_model;
    const wchar_t* model[2];
    const wchar_t* lbl_resize;
    const wchar_t* resize_width[4];
    const wchar_t* resize_first;
    const wchar_t* lbl_max;
    const wchar_t* sec_ref;
    const wchar_t* btn_add_ref;
    const wchar_t* btn_clear;
    const wchar_t* btn_api_keys;
    const wchar_t* tt_refresh;
    const wchar_t* tt_add_ref;
    const wchar_t* tt_clear_ref;
    const wchar_t* tt_api_keys;
};

[[nodiscard]] const CompressStrings& compress_strings_for(std::string_view display_language);
[[nodiscard]] const TransformStrings& transform_strings_for(std::string_view display_language);
[[nodiscard]] const MetaStrings& meta_strings_for(std::string_view display_language);
[[nodiscard]] const FindStrings& find_strings_for(std::string_view display_language);

} // namespace pmui::settings_panel_i18n
