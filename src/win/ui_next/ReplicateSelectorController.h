#pragma once

#include <Windows.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ProviderDlg_Replicate.h"

namespace pmui {

struct ReplicateSelectorState {
    std::string selected_collection = "official";
    std::string selected_model_url;
    std::vector<provider_dlg_replicate::CollectionInfo> collections;
    std::vector<provider_dlg_replicate::ModelInfo> models;
    std::unordered_map<std::string, provider_dlg_replicate::ModelInfo> model_by_slug;
};

struct ReplicateSelectorRowRenderSpec {
    HWND parent{};
    HINSTANCE inst{};
    HFONT font{};
    int label_x = 0;
    int label_w = 0;
    int field_x = 0;
    int y = 0;
    int row_h = 22;
    int combo_w = 240;
    int gap = 6;
    int refresh_w = 30;
    int collection_id = 0;
    int refresh_id = 0;
    std::wstring label_text = L"Collection:";
    std::wstring refresh_text = L"\x21BB";
    bool show_refresh = true;
    bool initially_visible = false;
};

struct ReplicateSelectorRowHandles {
    HWND h_collection{};
    HWND h_refresh{};
    /// Label STATIC created beside the combo; must be toggled with the row (same as combo/refresh).
    HWND h_label{};
};

class ReplicateSelectorController {
public:
    void set_display_language(std::string_view display_language);

    ReplicateSelectorRowHandles render_collection_row(const ReplicateSelectorRowRenderSpec& spec) const;
    void set_row_visible(const ReplicateSelectorRowHandles& h, bool visible) const;

    bool reload(const std::string& api_key,
                const std::string& base_url,
                HWND h_collection_combo,
                HWND h_model_combo,
                ReplicateSelectorState& st,
                const std::string& keep_model_slug,
                bool allow_collection_guess_from_model,
                bool force_refresh,
                std::wstring* err_out = nullptr) const;

    /// Collections + models fetch only (no HWND). Safe to call from a worker thread.
    bool reload_fetch_state(const std::string& api_key,
                            const std::string& base_url,
                            ReplicateSelectorState& st,
                            const std::string& keep_model_slug,
                            bool allow_collection_guess_from_model,
                            bool force_refresh,
                            std::string& err_utf8) const;

    /// Populate collection/model combos from @p st after @ref reload_fetch_state (UI thread only).
    void reload_apply_ui(HWND h_collection_combo,
                         HWND h_model_combo,
                         ReplicateSelectorState& st,
                         const std::string& keep_model_slug) const;

    bool reload_using_saved_settings(HWND h_collection_combo,
                                     HWND h_model_combo,
                                     ReplicateSelectorState& st,
                                     const std::string& keep_model_slug,
                                     bool allow_collection_guess_from_model,
                                     bool force_refresh,
                                     std::wstring* err_out = nullptr) const;

    bool apply_collection_from_combo(HWND h_collection_combo, ReplicateSelectorState& st) const;
    std::string selected_model_slug(HWND h_model_combo) const;
    void update_meta_text(HWND h_meta_edit, ReplicateSelectorState& st, const std::string& model_slug) const;

private:
    std::string display_language_{"en"};
};

} // namespace pmui

