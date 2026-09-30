#pragma once

#include "core/resize.hpp"

#include <functional>
#include <string>
#include <vector>

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <string_view>

struct PixlwizSharePostFields;

namespace media {
struct BatchControl;
}

namespace media::win {

/** Shown in list column 0 (name) and 2 (path); per-row status in column 1. */
struct ExplorerJobRow {
    std::wstring name;   // display name
    std::wstring path;   // full path
};

class ExplorerJobHost {
public:
    void            set_status(int row, const wchar_t* status);
    void            set_status(int row, std::wstring_view status);
    bool            cancel_requested() const;
    void            wait_if_paused();
    /// Optional: forward Pause / Resume / Cancel to core `transform_image` / `create_image` (BatchControl).
    void            link_batch_control(media::BatchControl* b);
    /// Toplevel job window (valid for the lifetime of the @p work callback in @c run_explorer_job_ui).
    HWND            job_window() const noexcept;

private:
    void* state_ = nullptr; // internal JobUiState; set by run_explorer_job_ui
    friend bool run_explorer_job_ui(const std::wstring& title, const std::vector<ExplorerJobRow>& rows,
                                    const std::function<void(ExplorerJobHost&)>& work,
                                    bool hide_job_window_until_post_details_confirmed);
};

/**
 * List-view job window: Name / Status / Path (same columns as the main file queue);
 * Pause, Resume, Cancel, Open main app; minimizable. Worker thread runs @p work.
 */
bool run_explorer_job_ui(
    const std::wstring& title, const std::vector<ExplorerJobRow>& rows,
    const std::function<void(ExplorerJobHost&)>& work,
    bool hide_job_window_until_post_details_confirmed = false);

/** After @c run_explorer_job_ui(..., hide=true), call from the worker once post fields are confirmed so the job window appears. */
void explorer_job_ui_reveal_job_window(HWND job_hwnd);

/** Run @c RunPixlwizSharePostDialog on the explorer job UI thread (synchronous from a worker thread). */
bool explorer_job_sync_pixlwiz_share_post_dialog(HWND job_hwnd, PixlwizSharePostFields& fields);
/** Run @c RunPixlwizShareSuccessDialog on the job UI thread (consumes heap-allocated url copy internally). */
void explorer_job_sync_pixlwiz_share_success_dialog(HWND job_hwnd, const std::wstring& url_w, size_t picture_count);

/**
 * Replaces the old `resize_progress_ui` single bar: run resize batch with the explorer job list.
 * If @p always_show is false and there is only one job, calls `resize_batch` with no window.
 */
bool run_resize_batch_with_job_ui(
    const std::string& input_spec, const std::string& output_spec, const ResizeOptions& opt,
    std::string& err_out, ResizeBatchResult* out_stats, bool always_show_job_ui = false);

} // namespace media::win

#endif // _WIN32
