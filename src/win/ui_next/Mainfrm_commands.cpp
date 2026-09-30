// User-facing command handlers for CMainFrame.
// Queue (add / clear / save-as / selection): Mainfrm_queue.cpp
// Remaining: resize, compress, meta, transform, find, duplicates, chat, batch, sessions.
#include "stdafx.h"
#include "constants.hpp"
#include "Mainfrm.h"
#include "ProviderDlg.h"
#include "queue_path_enumeration.hpp"
#include "helpers/text_conv.hpp"
#include "file_extensions.hpp"
#include "helpers/chat_context_attach.hpp"
#include "core/batch_queue.hpp"
#include "core/compress.hpp"
#include "core/find.hpp"
#include "core/duplicates.hpp"
#include "DuplicatePanel.h"
#include "core/app_image_provider.hpp"
#include "core/glob_paths.hpp"
#include "core/meta.hpp"
#include "core/resize.hpp"
#include "core/settings_runtime.hpp"
#include "win/settings_store.hpp"
#if defined(FEATURE_CHAT_WEB)
#  include "ChatWebResource.h"
#endif
#ifdef FEATURE_PNG_COMPRESSOR
#include "core/png_compress.hpp"
#endif
#include <vips/vips.h>
#include <shlobj.h>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

// From a log line, pick a single listview row when the line refers to a known
// (path,row) from the running queue op. Return -1 for log-only / global lines.
int pick_queue_status_row(const std::string&                 msg,
                          const std::vector<std::string>&    inputs,
                          const std::vector<int>&            opRows)
{
    if (opRows.empty() || inputs.size() != opRows.size() || msg.empty()) return -1;
    int n = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), (int)msg.size(), nullptr, 0);
    if (n <= 0) return -1;
    std::wstring wmsg((size_t)n, L'\0');
    n = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), (int)msg.size(), wmsg.data(), n);
    wmsg.resize((size_t)n);
    for (auto& c : wmsg) {
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        if (c == L'/') c = L'\\';
    }
    std::vector<size_t> order(inputs.size());
    for (size_t j = 0; j < order.size(); ++j) order[j] = j;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return inputs[a].size() > inputs[b].size();
    });
    for (size_t k = 0; k < order.size(); ++k) {
        const size_t          i    = order[k];
        const std::string&    p    = inputs[i];
        if (p.empty()) continue;
        int pn = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), (int)p.size(), nullptr, 0);
        if (pn <= 0) continue;
        std::wstring wpath((size_t)pn, L'\0');
        pn = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), (int)p.size(), wpath.data(), pn);
        wpath.resize((size_t)pn);
        std::wstring pfull = wpath;
        for (auto& c : pfull) {
            if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
            if (c == L'/') c = L'\\';
        }
        if (pfull.size() >= 2 && wmsg.find(pfull) != std::wstring::npos) return opRows[i];
        std::wstring base = fs::path(wpath).filename().wstring();
        for (auto& c : base) {
            if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        }
        // Require a real filename shape so we do not match short tokens from log lines
        // (e.g. "kb" in "43 kb image" or "image" in the word "image)" ).
        if (base.size() >= 3u && base.find(L'.') != std::wstring::npos
            && wmsg.find(base) != std::wstring::npos)
            return opRows[i];
    }
    return -1;
}

// Log lines with no path (e.g. Meta: "POST model (N KB image)") follow a line that
// did name the file. Attribute them to the last matched queue row in-sequence.
static bool queue_status_row_sibling_hint(const std::string& m)
{
    if (m.rfind("POST ", 0) == 0) return true;
    if (m == "Parsing response") return true;
    if (m.rfind("Resizing in memory to ", 0) == 0) return true;
    if (m == "Reading EXIF") return true;
    if (m.rfind("Dry-run OK", 0) == 0) return true;
    if (m == "EXIF ImageDescription updated") return true;
    if (m.rfind("  ", 0) == 0) {
        if (m.rfind("  meta", 0) == 0) return true;
        if (m.rfind("  no ", 0) == 0) return true;
        if (m.rfind("  judge", 0) == 0) return true;
        if (m.rfind("  generating", 0) == 0) return true;
        if (m.rfind("  re-generating", 0) == 0) return true;
        if (m.rfind("  \xE2\x9C\x93", 0) == 0) return true;  // "  ✓" (judge)
        if (m.rfind("  \xE2\x9C\x97", 0) == 0) return true;  // "  ✗"
    }
    if (m.rfind("duplicates: meta JSON LLM:", 0) == 0 && m.find("failed:") != std::string::npos)
        return true;
    return false;
}

// Clear "Cancelled" / previous op status on rows we are about to run again.
static void queue_reset_rows_queued(const std::vector<std::pair<int, std::string>>& items,
                                    CQueueListView&                                 lv)
{
    for (const auto& e : items) {
        if (e.first >= 0 && e.first < lv.QueueCount()) lv.SetItemStatus(e.first, L"Queued");
    }
}

/// When re-running, only pre-mark "Done" for skip if the *previous* batch was not
/// fully complete (left errors and/or unprocessed). If every item succeeded, the user
/// clicking Resize/Compress/… again must re-encode in-place, not be silently skipped.
static bool should_carry_forward_done_skip(const media::BatchState& s)
{
    if (s.empty()) return false;
    return s.count_error() > 0 || s.count_pending() > 0;
}

} // namespace

using pmui::wide_to_utf8;
using pmui::utf8_to_wide;
using pmui::is_image_ext;

// ── Resize ────────────────────────────────────────────────────────────────────

void CMainFrame::OnResize()
{
    if (m_processing) {
        ::MessageBox(GetHwnd(), L"Already processing.", pm::brand::k_app_id_w, MB_ICONWARNING);
        return;
    }
    if (!m_pDockSettings) return;

    bool               batchUiArmed = false;
    const auto         rollbackBatchUi = [this]() {
        m_processing = false;
        m_batchCtrl.reset();
        m_queueTotal     = 0;
        m_queueDone      = 0;
        m_batchStartTick = 0;
        UpdateQueueStatusPart();
        InvalidateBatchUi();
    };

    try {
    media::ResizeOptions opt;
    std::string out_dir;
    m_pDockSettings->GetSettingsContainer().GetSettingsView().ReadOptions(opt, out_dir);

    // ── Build item list ───────────────────────────────────────────────────────
    // Explorer selection: add the file to the queue first so it is visible,
    // shows progress badges, and lands in the done list like any queued file.
    std::vector<std::pair<int, std::string>> items;
    if (!m_explorerSelectionPaths.empty()) {
        if (!m_pDockQueue) return;
        // Enqueue everything selected (a folder can expand to many list rows), then
        // pair (row index, path) from the list — not from selection size — so firstIdx
        // is never negative and folder picks include every file the queue received.
        auto& lv   = m_pDockQueue->GetQueueContainer().GetListView();
        const int  nBefore = lv.QueueCount();
        AddFilesToQueue(m_explorerSelectionPaths, false);
        const int  nAfter = lv.QueueCount();
        const int  added  = nAfter - nBefore;
        if (added <= 0) {
            ::MessageBoxW(
                GetHwnd(),
                L"No image files were enqueued for the current Explorer selection "
                L"(empty folder or nothing to add).",
                pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }
        items.reserve(static_cast<size_t>(added));
        for (int i = 0; i < added; ++i) {
            CString p = lv.GetItemPath(nBefore + i);
            items.emplace_back(nBefore + i, wide_to_utf8(std::wstring(p.c_str())));
        }
    } else {
        if (!m_pDockQueue) return;
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        if (lv.QueueCount() == 0) (void)TryEnqueueCurrentExplorerFolderIfEmpty();
        if (lv.QueueCount() == 0) {
            ::MessageBox(GetHwnd(), L"Queue is empty \u2014 drop files or select one in the Explorer panel.",
                         pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }
        items.reserve(lv.QueueCount());
        for (int i = 0; i < lv.QueueCount(); ++i) {
            CString p = lv.GetItemPath(i);
            items.emplace_back(i, wide_to_utf8(std::wstring(p.c_str())));
        }
    }

    // Drop GDI+ handles on any paths we may rewrite in-place; avoids conflicts with
    // the worker (vips) and "user-mapped section" on Windows. Safe for any destination.
    if (!items.empty() && !IsChatWorkbench())
        m_viewerManager.ActiveView().ClearPicture();

    m_batchCtrl  = std::make_shared<media::BatchControl>();
    m_processing = true;
    batchUiArmed = true;
    m_queueTotal     = (int)items.size();
    m_queueDone      = 0;
    m_batchStartTick = (DWORD)::GetTickCount64();
    UpdateQueueStatusPart();
    InvalidateBatchUi();

    // Preserve "already Done" only when the last batch was incomplete (so retry only
    // the remaining work). A 100% successful run does not pre-mark Done, so a second
    // in-place run actually re-encodes.
    {
        std::unordered_set<std::string> done_paths;
        if (should_carry_forward_done_skip(m_currentSession)) {
            for (const auto& bi : m_currentSession.items)
                if (bi.status == media::BatchItemStatus::Done)
                    done_paths.insert(bi.path);
        }

        m_currentSession = {};
        m_currentSession.session_id = media::new_session_id();
        m_currentSession.op         = "resize";
        m_currentSession.created_at = media::utc_now_iso8601();
        m_currentSession.items.reserve(items.size());
        for (auto& [idx, path] : items) {
            media::BatchItem bi;
            bi.path   = path;
            bi.sha256 = media::file_sha256_fast(path);
            if (done_paths.count(path))
                bi.status = media::BatchItemStatus::Done;
            m_currentSession.items.push_back(std::move(bi));
        }
    }

    HWND hwnd   = GetHwnd();
    auto ctrl   = m_batchCtrl;
    auto bItems = m_currentSession.items; // worker-owned copy

    LogMessage(L"--- Resize started ---");
    CString rinfo;
    rinfo.Format(L"%d file(s)  |  Max: %dx%d  |  Q: %d",
                 (int)items.size(), opt.max_width, opt.max_height, opt.quality);
    LogMessage(rinfo);

    if (m_worker.joinable()) m_worker.join();
    if (m_pDockQueue) queue_reset_rows_queued(items, m_pDockQueue->GetQueueContainer().GetListView());
    m_worker = std::thread([items = std::move(items), opt, out_dir, hwnd,
                            ctrl, bItems = std::move(bItems)]() mutable {
        auto postLog = [hwnd](const std::string& u8) {
            std::wstring ws = utf8_to_wide(u8);
            auto* p = new wchar_t[ws.size() + 1];
            std::copy(ws.begin(), ws.end(), p);
            p[ws.size()] = L'\0';
            ::PostMessage(hwnd, UWM_LOG_MESSAGE, reinterpret_cast<WPARAM>(p), 0);
        };

        const int total = (int)items.size();
        int ok = 0, fail = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            auto& [idx, input] = items[i];

            // ── Skip items already Done from a loaded session ────────────────
            if (bItems[i].status == media::BatchItemStatus::Done) {
                ++ok;
                // Queue row already shows "✓ (skipped)" from Load Session.
                continue;
            }

            // ── Pause / cancel check ─────────────────────────────────────────
            if (ctrl->paused.load()) {
                auto* partial = new media::BatchState;
                partial->items      = bItems;
                partial->updated_at = media::utc_now_iso8601();
                ::PostMessage(hwnd, UWM_BATCH_PAUSED, reinterpret_cast<WPARAM>(partial), 0);
            }
            ctrl->check_pause();
            if (ctrl->cancel.load()) break;

            const std::string inName = fs::path(input).filename().string();
            postLog("[" + std::to_string(ok + fail + 1) + "/" + std::to_string(total)
                    + "] " + inName);

            ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)idx, 1);

            // Explicit format → override output extension (e.g. ARW → WebP).
            const std::string out_ext = opt.format.empty() ? "" : ("." + opt.format);

            std::string output_path;
            if (!out_dir.empty()) {
                if (media::has_dst_template(out_dir)) {
                    // Full path template: ${SRC_DIR}/${SRC_NAME}_sd.webp, etc.
                    // pair_resize_paths expands ${SRC_*} variables; the resulting
                    // path already has the correct name — no filename appending.
                    std::string pair_err;
                    auto jobs = media::pair_resize_paths(input, out_dir, pair_err);
                    if (jobs.empty()) {
                        bItems[i].status = media::BatchItemStatus::Error;
                        bItems[i].error  = pair_err.empty()
                            ? "template expansion failed" : pair_err;
                        ++fail;
                        ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)idx, 3);
                        postLog("  \u2717 " + input + ": " + bItems[i].error);
                        continue;
                    }
                    output_path = jobs[0].second.string();
                } else {
                    // Plain directory: append filename (with format-aware extension).
                    const fs::path p(input);
                    const std::string name = p.stem().string()
                        + (out_ext.empty() ? p.extension().string() : out_ext);
                    fs::path op = fs::path(out_dir) / name;
                    output_path = op.string();
                    std::error_code ec2;
                    fs::create_directories(fs::path(out_dir), ec2);
                }
            } else if (!opt.output_stem_suffix.empty()) {
                const fs::path p(input);
                const std::string ext = out_ext.empty() ? p.extension().string() : out_ext;
                output_path = (p.parent_path() /
                    (p.stem().string() + opt.output_stem_suffix + ext)).string();
            } else {
                // Next to source — change extension only when format is explicitly set.
                if (!out_ext.empty()) {
                    const fs::path p(input);
                    output_path = (p.parent_path() /
                        (p.stem().string() + out_ext)).string();
                } else {
                    output_path = input;
                }
            }

            // Synchronously drop the central preview (GDI+ can memory-map the input/output
            // path). If that handle stays open, libvips / libpng often print errors and
            // read or in-place write can fail on Windows.
            {
                std::vector<std::wstring> rel;
                rel.push_back(utf8_to_wide(input));
                if (output_path != input) {
                    const std::wstring wout = utf8_to_wide(output_path);
                    if (wout != rel[0]) rel.push_back(wout);
                }
                ::SendMessageW(hwnd, UWM_RELEASE_PREVIEW_FOR_PATHS,
                    reinterpret_cast<WPARAM>(&rel), 0);
            }

            std::string err;
            bool        success = false;
            try {
                success = media::resize_file(input, output_path, opt, err);
            } catch (const std::exception& ex) {
                err     = ex.what() ? ex.what() : "std::exception";
                success = false;
            } catch (...) {
                err     = "C++ exception (unknown) during resize";
                success = false;
            }
            ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)idx, success ? 2 : 3);

            // Record outcome in batch items.
            bItems[i].status = success ? media::BatchItemStatus::Done
                                       : media::BatchItemStatus::Error;
            if (!success) bItems[i].error = err;

            if (success) {
                ++ok;
                postLog("  \u2192 " + output_path);
            } else {
                ++fail;
                postLog("  \u2717 " + input + " \u2192 " + output_path);
                postLog("    " + err);
            }
        }
        vips_thread_shutdown();

        // Build final/partial state and hand it to the UI thread.
        auto* finalState = new media::BatchState;
        finalState->items      = std::move(bItems);
        finalState->updated_at = media::utc_now_iso8601();

        if (ctrl->cancel.load()) {
            ::PostMessage(hwnd, UWM_BATCH_CANCELLED,
                          reinterpret_cast<WPARAM>(finalState), 0);
        } else {
            ::PostMessage(hwnd, UWM_BATCH_STATE_UPDATE,
                          reinterpret_cast<WPARAM>(finalState), 0);
            ::PostMessage(hwnd, UWM_QUEUE_DONE, (WPARAM)ok, (LPARAM)fail);
        }
    });
    SetStatusBarPartText(0, L"Resizing\u2026");

    } catch (const std::exception& e) {
        if (m_batchCtrl) m_batchCtrl->request_cancel();
        if (m_worker.joinable()) m_worker.join();
        if (batchUiArmed) rollbackBatchUi();
        const std::string  what  = e.what() ? e.what() : "exception";
        const std::wstring wmsg  = utf8_to_wide(std::string("Resize: ") + what);
        LogMessage(CString(wmsg.c_str()));
        ::MessageBoxW(GetHwnd(), wmsg.c_str(), pm::brand::k_ui_job_title_resize_w, MB_ICONERROR);
    } catch (...) {
        if (m_batchCtrl) m_batchCtrl->request_cancel();
        if (m_worker.joinable()) m_worker.join();
        if (batchUiArmed) rollbackBatchUi();
        LogMessage(L"Resize: unknown C++ exception while starting.");
        ::MessageBoxW(GetHwnd(),
            L"An unexpected C++ exception occurred while starting resize. "
            L"Check the log for details.",
            pm::brand::k_ui_job_title_resize_w, MB_ICONERROR);
    }
}

// ── Compress ──────────────────────────────────────────────────────────────────

void CMainFrame::OnCompress()
{
    if (m_processing) {
        ::MessageBox(GetHwnd(), L"Already processing.", pm::brand::k_app_id_w, MB_ICONWARNING);
        return;
    }
    if (!m_pDockSettings) return;

    // Read compress settings from settings panel.
    CompressSettings cs;
    m_pDockSettings->GetSettingsContainer().GetSettingsView().ReadCompressSettings(cs);

    // Build item list — Explorer selection is enqueued first (same pattern as Resize).
    std::vector<std::pair<int, std::string>> items;
    if (!m_explorerSelectionPaths.empty()) {
        if (!m_pDockQueue) return;
        auto& lv   = m_pDockQueue->GetQueueContainer().GetListView();
        const int  nBefore = lv.QueueCount();
        AddFilesToQueue(m_explorerSelectionPaths, false);
        const int  nAfter = lv.QueueCount();
        const int  added  = nAfter - nBefore;
        if (added <= 0) {
            ::MessageBoxW(
                GetHwnd(),
                L"No image files were enqueued for the current Explorer selection "
                L"(empty folder or nothing to add).",
                pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }
        items.reserve(static_cast<size_t>(added));
        for (int i = 0; i < added; ++i) {
            CString p = lv.GetItemPath(nBefore + i);
            items.emplace_back(nBefore + i, wide_to_utf8(std::wstring(p.c_str())));
        }
    } else {
        if (!m_pDockQueue) return;
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        if (lv.QueueCount() == 0) (void)TryEnqueueCurrentExplorerFolderIfEmpty();
        if (lv.QueueCount() == 0) {
            ::MessageBox(GetHwnd(), L"Queue is empty \u2014 drop files or select one in the Explorer panel.",
                         pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }
        items.reserve(lv.QueueCount());
        for (int i = 0; i < lv.QueueCount(); ++i) {
            CString p = lv.GetItemPath(i);
            items.emplace_back(i, wide_to_utf8(std::wstring(p.c_str())));
        }
    }

    if (!items.empty() && !IsChatWorkbench())
        m_viewerManager.ActiveView().ClearPicture();

    m_processing = true;
    m_batchCtrl  = std::make_shared<media::BatchControl>();
    m_queueTotal     = (int)items.size();
    m_queueDone      = 0;
    m_batchStartTick = (DWORD)::GetTickCount64();
    UpdateQueueStatusPart();
    InvalidateBatchUi();
    {
        std::unordered_set<std::string> done_paths;
        if (should_carry_forward_done_skip(m_currentSession)) {
            for (const auto& bi : m_currentSession.items)
                if (bi.status == media::BatchItemStatus::Done)
                    done_paths.insert(bi.path);
        }

        m_currentSession = {};
        m_currentSession.session_id = media::new_session_id();
        m_currentSession.op         = "compress";
        m_currentSession.created_at = media::utc_now_iso8601();
        m_currentSession.items.reserve(items.size());
        for (auto& [idx, path] : items) {
            media::BatchItem bi;
            bi.path   = path;
            bi.sha256 = media::file_sha256_fast(path);
            if (done_paths.count(path))
                bi.status = media::BatchItemStatus::Done;
            m_currentSession.items.push_back(std::move(bi));
        }
    }
    HWND hwnd   = GetHwnd();
    auto ctrl   = m_batchCtrl;
    auto bItems = m_currentSession.items;

    LogMessage(L"--- Compress started ---");
    CString info;
    info.Format(L"%d file(s)  |  Level: %d  |  Quantise: %s  |  Zopfli: %s",
                (int)items.size(), cs.level,
                cs.quantize ? L"yes" : L"no",
                cs.zopfli   ? L"yes" : L"no");
    LogMessage(info);

    if (m_worker.joinable()) m_worker.join();
    if (m_pDockQueue) queue_reset_rows_queued(items, m_pDockQueue->GetQueueContainer().GetListView());
    m_worker = std::thread([items = std::move(items), cs, hwnd,
                            ctrl, bItems = std::move(bItems)]() mutable {
        auto postLog = [hwnd](const std::string& u8) {
            std::wstring ws = utf8_to_wide(u8);
            auto* p = new wchar_t[ws.size() + 1];
            std::copy(ws.begin(), ws.end(), p);
            p[ws.size()] = L'\0';
            ::PostMessage(hwnd, UWM_LOG_MESSAGE, reinterpret_cast<WPARAM>(p), 0);
        };

        const int total = (int)items.size();
        int ok = 0, fail = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            auto& [idx, input] = items[i];

            if (bItems[i].status == media::BatchItemStatus::Done) { ++ok; continue; }

            if (ctrl->paused.load()) {
                auto* partial = new media::BatchState;
                partial->items      = bItems;
                partial->updated_at = media::utc_now_iso8601();
                ::PostMessage(hwnd, UWM_BATCH_PAUSED, reinterpret_cast<WPARAM>(partial), 0);
            }
            ctrl->check_pause();
            if (ctrl->cancel.load()) break;

            const std::string inName = fs::path(input).filename().string();
            postLog("[" + std::to_string(ok + fail + 1) + "/" + std::to_string(total)
                    + "] " + inName);

            ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)(LPARAM)idx, 4); // "Compressing..."

            // Build output path — extension depends on compressor.
            std::string output;
            {
                fs::path p(input);
                std::string stem = p.stem().string();
                if (cs.output_preset == 1) stem += "_compressed";
                const std::string ext = cs.use_mozjpeg ? ".jpg" : ".png";
                const std::string out_name = stem + ext;
                if (cs.output_preset == 2 && !cs.out_dir.empty()) {
                    std::error_code ec;
                    fs::create_directories(cs.out_dir, ec);
                    output = (fs::path(cs.out_dir) / out_name).string();
                } else {
                    output = (p.parent_path() / out_name).string();
                }
            }

            std::string err;

            if (cs.use_mozjpeg) {
                // ── MozJPEG path via compress_file() ─────────────────────────
                media::CompressOptions copts;
                copts.compressor               = media::Compressor::MozJPEG;
                copts.strip_metadata           = cs.strip_metadata;
                copts.jpeg_quality             = cs.jpeg_quality;
                copts.jpeg_progressive         = cs.jpeg_progressive;
                copts.jpeg_trellis_quant       = cs.jpeg_trellis;
                copts.jpeg_overshoot_deringing = true;
                err = media::compress_file(input, output, copts);
            } else {
#ifdef FEATURE_PNG_COMPRESSOR
                // ── PNG: libimagequant + libpng [+ zopfli] ────────────────────
                media::PngCompressOptions pc{};
                pc.quantize         = cs.quantize;
                pc.quantize_colors  = cs.colors;
                pc.quantize_quality = cs.q_quality;
                pc.libpng_level     = cs.level;
                pc.use_zopfli       = cs.zopfli;
                pc.zopfli_iter      = cs.zopfli_iter;
                err = media::png_compress(input, output, pc);
#else
                // ── PNG fallback: vips pngsave ────────────────────────────────
                {
                    VipsImage* img = vips_image_new_from_file(input.c_str(), nullptr);
                    if (!img) {
                        err = "vips: cannot load " + input;
                    } else {
                        if (vips_pngsave(img, output.c_str(),
                                "compression", cs.level,
                                "strip", (int)cs.strip_metadata,
                                nullptr) != 0)
                            err = "vips: pngsave failed for " + output;
                        g_object_unref(img);
                    }
                }
#endif
            }

            const bool compOk = err.empty();
            bItems[i].status = compOk ? media::BatchItemStatus::Done
                                      : media::BatchItemStatus::Error;
            if (!compOk) bItems[i].error = err;

            if (compOk) {
                ++ok;
                ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)(LPARAM)idx, 5);
                postLog("  \u2192 " + output);
            } else {
                ++fail;
                ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)(LPARAM)idx, 6);
                postLog("  \u2717 " + input + " \u2192 " + output);
                postLog("    " + err);
            }
        }
        vips_thread_shutdown();

        auto* finalState = new media::BatchState;
        finalState->items      = std::move(bItems);
        finalState->updated_at = media::utc_now_iso8601();

        if (ctrl->cancel.load()) {
            ::PostMessage(hwnd, UWM_BATCH_CANCELLED,
                          reinterpret_cast<WPARAM>(finalState), 0);
        } else {
            ::PostMessage(hwnd, UWM_BATCH_STATE_UPDATE,
                          reinterpret_cast<WPARAM>(finalState), 0);
            ::PostMessage(hwnd, UWM_COMPRESS_DONE, (WPARAM)ok, (LPARAM)fail);
        }
    });
    SetStatusBarPartText(0, L"Compressing\u2026");
}

// ── Meta (LLM description / EXIF / JSON / Markdown) ─────────────────────────

void CMainFrame::OnMeta()
{
    if (m_processing) {
        ::MessageBox(GetHwnd(), L"Already processing.", pm::brand::k_app_id_w, MB_ICONWARNING);
        return;
    }
    if (!m_pDockSettings) return;

    MetaSettings ms;
    m_pDockSettings->GetSettingsContainer().GetSettingsView().ReadMetaSettings(ms);

    if (!ms.out_md && !ms.out_json && !ms.update_exif) {
        ::MessageBox(GetHwnd(),
                     L"Pick at least one output: Markdown, JSON, or EXIF update.",
                     pm::brand::k_app_id_w, MB_ICONINFORMATION);
        return;
    }

    // Resolve API key and Gemini base URL (app provider settings, then env).
    std::string                      provider_name;
    media::ActiveImageProviderFromApp pe;
    (void)media::try_load_active_image_provider_from_app(pe, provider_name);
    std::string api_key = pe.api_key;
    if (api_key.empty()) {
        int choice = ::MessageBoxW(GetHwnd(),
            L"No API key configured.\n\nOpen the API Keys dialog now?",
            pm::brand::k_app_id_w, MB_ICONWARNING | MB_YESNO);
        if (choice == IDYES) ShowProviderSettingsDlg(GetHwnd());
        return;
    }

    // Build item list — same Explorer/Queue logic as Resize/Compress.
    std::vector<std::pair<int, std::string>> items;
    if (!m_explorerSelectionPaths.empty()) {
        if (!m_pDockQueue) return;
        auto& lv   = m_pDockQueue->GetQueueContainer().GetListView();
        const int  nBefore = lv.QueueCount();
        AddFilesToQueue(m_explorerSelectionPaths, false);
        const int  nAfter = lv.QueueCount();
        const int  added  = nAfter - nBefore;
        if (added <= 0) {
            ::MessageBoxW(
                GetHwnd(),
                L"No image files were enqueued for the current Explorer selection "
                L"(empty folder or nothing to add).",
                pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }
        items.reserve(static_cast<size_t>(added));
        for (int i = 0; i < added; ++i) {
            CString p = lv.GetItemPath(nBefore + i);
            items.emplace_back(nBefore + i, wide_to_utf8(std::wstring(p.c_str())));
        }
    } else {
        if (!m_pDockQueue) return;
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        if (lv.QueueCount() == 0) (void)TryEnqueueCurrentExplorerFolderIfEmpty();
        if (lv.QueueCount() == 0) {
            ::MessageBox(GetHwnd(), L"Queue is empty \u2014 drop files or select one in the Explorer panel.",
                         pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }
        items.reserve(lv.QueueCount());
        for (int i = 0; i < lv.QueueCount(); ++i) {
            CString p = lv.GetItemPath(i);
            items.emplace_back(i, wide_to_utf8(std::wstring(p.c_str())));
        }
    }

    if (!items.empty() && !IsChatWorkbench())
        m_viewerManager.ActiveView().ClearPicture();

    m_processing = true;
    m_batchCtrl  = std::make_shared<media::BatchControl>();
    m_queueTotal     = (int)items.size();
    m_queueDone      = 0;
    m_batchStartTick = (DWORD)::GetTickCount64();
    UpdateQueueStatusPart();
    InvalidateBatchUi();
    {
        std::unordered_set<std::string> done_paths;
        if (should_carry_forward_done_skip(m_currentSession)) {
            for (const auto& bi : m_currentSession.items)
                if (bi.status == media::BatchItemStatus::Done)
                    done_paths.insert(bi.path);
        }

        m_currentSession = {};
        m_currentSession.session_id = media::new_session_id();
        m_currentSession.op         = "meta";
        m_currentSession.created_at = media::utc_now_iso8601();
        m_currentSession.items.reserve(items.size());
        for (auto& [idx, path] : items) {
            media::BatchItem bi;
            bi.path   = path;
            bi.sha256 = media::file_sha256_fast(path);
            if (done_paths.count(path))
                bi.status = media::BatchItemStatus::Done;
            m_currentSession.items.push_back(std::move(bi));
        }
    }
    HWND hwnd   = GetHwnd();
    auto ctrl   = m_batchCtrl;
    auto bItems = m_currentSession.items;

    LogMessage(L"--- Meta started ---");
    CString info;
    info.Format(L"%d file(s)  |  %S  |  resize: %s (%dpx)  |  outputs: %s%s%s",
                (int)items.size(), ms.model.c_str(),
                ms.resize_first ? L"yes" : L"no", ms.resize_width,
                ms.out_md      ? L"md "      : L"",
                ms.out_json    ? L"json "    : L"",
                ms.update_exif ? L"exif"     : L"");
    LogMessage(info);

    if (m_worker.joinable()) m_worker.join();
    if (m_pDockQueue) queue_reset_rows_queued(items, m_pDockQueue->GetQueueContainer().GetListView());
    m_worker = std::thread([items = std::move(items), ms, api_key, pe, hwnd,
                            ctrl, bItems = std::move(bItems)]() mutable {
        auto postLog = [hwnd](const std::string& u8) {
            std::wstring ws = utf8_to_wide(u8);
            auto* p = new wchar_t[ws.size() + 1];
            std::copy(ws.begin(), ws.end(), p);
            p[ws.size()] = L'\0';
            ::PostMessage(hwnd, UWM_LOG_MESSAGE, reinterpret_cast<WPARAM>(p), 0);
        };

        media::MetaOptions opts;
        opts.provider     = ms.provider;
        opts.model        = ms.model;
        opts.api_key      = api_key;
        opts.base_url     = pe.base_url;
        opts.prompt       = ms.prompt;
        opts.resize_first = ms.resize_first;
        opts.resize_width = ms.resize_width;
        opts.out_md       = ms.out_md;
        opts.out_json     = ms.out_json;
        opts.update_exif  = ms.update_exif;
        opts.out_dir      = ms.out_dir;

        const int total = (int)items.size();
        int ok = 0, fail = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            auto& [idx, input] = items[i];

            if (bItems[i].status == media::BatchItemStatus::Done) { ++ok; continue; }

            if (ctrl->paused.load()) {
                auto* partial = new media::BatchState;
                partial->items      = bItems;
                partial->updated_at = media::utc_now_iso8601();
                ::PostMessage(hwnd, UWM_BATCH_PAUSED, reinterpret_cast<WPARAM>(partial), 0);
            }
            ctrl->check_pause();
            if (ctrl->cancel.load()) break;

            const std::string inName = fs::path(input).filename().string();
            postLog("[" + std::to_string(ok + fail + 1) + "/" + std::to_string(total)
                    + "] " + inName);

            ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)(LPARAM)idx, 7);

            auto progress = [&postLog](const std::string& s) { postLog("    " + s); };
            media::MetaResult r;
            try {
                r = media::meta_extract(input, opts, progress);
            } catch (const std::exception& e) {
                r.ok = false;
                r.error = std::string("worker exception: ") + e.what();
            } catch (...) {
                r.ok = false;
                r.error = "worker exception (unknown)";
            }

            bItems[i].status = r.ok ? media::BatchItemStatus::Done
                                    : media::BatchItemStatus::Error;
            if (!r.ok) bItems[i].error = r.error;

            if (r.ok) {
                ++ok;
                ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)(LPARAM)idx, 8);
                if (!r.md_path.empty())   postLog("  \u2192 " + r.md_path);
                if (!r.json_path.empty()) postLog("  \u2192 " + r.json_path);
                if (r.exif_updated)       postLog("  \u2192 EXIF updated");
            } else {
                ++fail;
                ::PostMessage(hwnd, UWM_QUEUE_PROGRESS, (WPARAM)(LPARAM)idx, 9);
                postLog("  \u2717 " + input);
                postLog("    " + r.error);
            }
        }
        vips_thread_shutdown();

        auto* finalState = new media::BatchState;
        finalState->items      = std::move(bItems);
        finalState->updated_at = media::utc_now_iso8601();

        if (ctrl->cancel.load()) {
            ::PostMessage(hwnd, UWM_BATCH_CANCELLED,
                          reinterpret_cast<WPARAM>(finalState), 0);
        } else {
            ::PostMessage(hwnd, UWM_BATCH_STATE_UPDATE,
                          reinterpret_cast<WPARAM>(finalState), 0);
            ::PostMessage(hwnd, UWM_META_DONE, (WPARAM)ok, (LPARAM)fail);
        }
    });
    SetStatusBarPartText(0, L"Meta\u2026");
}

// ── AI Transform run ──────────────────────────────────────────────────────────
// (The standalone Prompt dialog is gone; the prompt now lives in the Settings panel.)

void CMainFrame::OnRun()
{
    if (m_processing) {
        ::MessageBox(GetHwnd(), L"Already processing.", pm::brand::k_app_id_w, MB_ICONWARNING);
        return;
    }
    if (!m_pDockSettings) return;

    // Read prompt + model/aspect/size directly from the Settings panel
    // (the AI options live in MODE_TRANSFORM controls).
    TransformSettings ts;
    m_pDockSettings->GetSettingsContainer().GetSettingsView().ReadTransformSettings(ts);
    if (ts.prompt.empty()) {
        ::MessageBox(GetHwnd(),
            L"No prompt set.\n\nType the edit you want in the Prompt field, "
            L"or pick a preset from the Settings panel.",
            pm::brand::k_app_id_w, MB_ICONINFORMATION);
        return;
    }

    media::settings::ProviderMap providers;
    std::string                  perr;
    (void)media::settings::load_providers(providers, perr);
    std::string chat_image_provider;
    {
        media::settings::ChatProviderSettings cs;
        if (media::settings::load_chat_provider(cs, perr) && !cs.image_provider.empty())
            chat_image_provider = cs.image_provider;
    }
    const std::string selected_provider =
        ts.provider.empty() ? (chat_image_provider.empty() ? "google" : chat_image_provider) : ts.provider;
    const auto pit = providers.find(selected_provider);
    const std::string provider_base_url = (pit != providers.end()) ? pit->second.base_url : std::string{};
    std::string api_key = (pit != providers.end()) ? pit->second.api_key : std::string{};
    if (api_key.empty()) {
        const char* env_key = (selected_provider == "replicate")
            ? std::getenv("IMAGE_TRANSFORM_REPLICATE_API_KEY")
            : std::getenv("IMAGE_TRANSFORM_GOOGLE_API_KEY");
        if (env_key && env_key[0] != '\0') api_key = env_key;
        if (api_key.empty() && selected_provider == "replicate") {
            const char* rt = std::getenv("REPLICATE_API_TOKEN");
            if (rt && rt[0] != '\0') api_key = rt;
        }
    }
    if (api_key.empty()) {
        int choice = ::MessageBoxW(GetHwnd(),
            L"No API key configured.\n\nOpen the API Keys dialog now?",
            pm::brand::k_app_id_w, MB_ICONWARNING | MB_YESNO);
        if (choice == IDYES) ShowProviderSettingsDlg(GetHwnd());
        return;
    }

    media::TransformOptions base_opts;
    base_opts.provider         = selected_provider;
    base_opts.model            = ts.model;
    base_opts.aspect_ratio     = ts.aspect_ratio;
    base_opts.image_size       = ts.image_size;
    base_opts.prompt           = ts.prompt;
    base_opts.api_key          = api_key;
    base_opts.base_url         = provider_base_url;
    base_opts.reference_images = ts.reference_images;
    base_opts.resize_first     = ts.preresize_in_memory.enabled;
    base_opts.resize_width     = ts.preresize_in_memory.enabled ? ts.preresize_in_memory.width : 0;
    base_opts.preresize_raw_only = ts.preresize_in_memory.raw_only;

    // ── Build item list ───────────────────────────────────────────────────────
    // Explorer selection: add source to the queue first (same as Resize / Compress)
    // so progress and "done" status appear on a queue row.
    std::vector<std::pair<int, std::string>> items;
    if (!m_explorerSelectionPaths.empty()) {
        if (!m_pDockQueue) return;
        auto& lv   = m_pDockQueue->GetQueueContainer().GetListView();
        const int  nBefore = lv.QueueCount();
        AddFilesToQueue(m_explorerSelectionPaths, false);
        const int  nAfter = lv.QueueCount();
        const int  added  = nAfter - nBefore;
        if (added <= 0) {
            ::MessageBoxW(
                GetHwnd(),
                L"No image files were enqueued for the current Explorer selection "
                L"(empty folder or nothing to add).",
                pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }
        items.reserve(static_cast<size_t>(added));
        for (int i = 0; i < added; ++i) {
            CString p = lv.GetItemPath(nBefore + i);
            items.emplace_back(nBefore + i, wide_to_utf8(std::wstring(p.c_str())));
        }
    } else {
        if (!m_pDockQueue) return;
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        if (lv.QueueCount() == 0) (void)TryEnqueueCurrentExplorerFolderIfEmpty();
        auto indices = GetSelectedQueueItems();
        if (indices.empty()) {
            ::MessageBox(GetHwnd(), L"Queue is empty \u2014 drop files or select one in the Explorer panel.",
                         pm::brand::k_app_id_w, MB_ICONINFORMATION);
            return;
        }
        for (int idx : indices) {
            CString path = lv.GetItemPath(idx);
            items.emplace_back(idx, wide_to_utf8(std::wstring(path.c_str())));
        }
    }

    if (!items.empty() && !IsChatWorkbench())
        m_viewerManager.ActiveView().ClearPicture();

    m_processing = true;
    m_batchCtrl  = std::make_shared<media::BatchControl>();
    m_queueTotal     = (int)items.size();
    m_queueDone      = 0;
    m_batchStartTick = (DWORD)::GetTickCount64();
    UpdateQueueStatusPart();
    InvalidateBatchUi();
    {
        std::unordered_set<std::string> done_paths;
        if (should_carry_forward_done_skip(m_currentSession)) {
            for (const auto& bi : m_currentSession.items)
                if (bi.status == media::BatchItemStatus::Done)
                    done_paths.insert(bi.path);
        }

        m_currentSession = {};
        m_currentSession.session_id = media::new_session_id();
        m_currentSession.op         = "transform";
        m_currentSession.created_at = media::utc_now_iso8601();
        m_currentSession.items.reserve(items.size());
        for (auto& [idx, path] : items) {
            media::BatchItem bi;
            bi.path   = path;
            bi.sha256 = media::file_sha256_fast(path);
            if (done_paths.count(path))
                bi.status = media::BatchItemStatus::Done;
            m_currentSession.items.push_back(std::move(bi));
        }
    }
    const std::string sessionId = m_currentSession.session_id;
    HWND              hwnd     = GetHwnd();
    auto              ctrl     = m_batchCtrl;
    auto              bItems   = m_currentSession.items;

    LogMessage(L"--- Transform started ---");
    CString info;
    info.Format(L"Model: %S  |  %d file(s)  |  Aspect: %S  |  Size: %S",
        base_opts.model.c_str(), (int)items.size(),
        base_opts.aspect_ratio.empty() ? "auto"    : base_opts.aspect_ratio.c_str(),
        base_opts.image_size.empty()   ? "default" : base_opts.image_size.c_str());
    LogMessage(info);

    if (m_worker.joinable()) m_worker.join();
    if (m_pDockQueue) queue_reset_rows_queued(items, m_pDockQueue->GetQueueContainer().GetListView());
    m_worker = std::thread([items = std::move(items), base_opts, sessionId, hwnd, ctrl,
                            bItems = std::move(bItems)]() mutable {
        const auto on_before_pause = [hwnd, sessionId, &bItems] {
            auto* partial = new media::BatchState;
            partial->session_id  = sessionId;
            partial->op         = "transform";
            partial->items      = bItems;
            partial->updated_at = media::utc_now_iso8601();
            ::PostMessage(hwnd, UWM_BATCH_PAUSED, reinterpret_cast<WPARAM>(partial), 0);
        };
        int ok = 0, fail = 0;
        for (int i = 0; i < (int)items.size(); ++i) {
            auto& [idx, input] = items[i];

            if (bItems[i].status == media::BatchItemStatus::Done) { ++ok; continue; }

            if (ctrl) {
                if (ctrl->paused.load()) on_before_pause();
                ctrl->check_pause();
            }
            if (ctrl && ctrl->cancel.load()) break;

            ::PostMessage(hwnd, UWM_TRANSFORM_PROGRESS, (WPARAM)idx, 1);

            auto progress = [hwnd](const std::string& msg) {
                auto* ws = new wchar_t[msg.size() * 2 + 2];
                int n = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), (int)msg.size(),
                                             ws, (int)(msg.size() * 2 + 1));
                ws[n] = L'\0';
                ::PostMessage(hwnd, UWM_LOG_MESSAGE, (WPARAM)ws, 0);
            };

            media::TransformOptions topts = base_opts;
            std::string output = media::default_transform_output(input, topts.prompt);
            auto result = media::transform_image(input, output, topts, progress,
                                                 ctrl.get(), on_before_pause);

            if (result.error == "transform: cancelled") {
                auto* st = new media::BatchState;
                st->session_id  = sessionId;
                st->op         = "transform";
                st->items      = bItems;
                st->updated_at = media::utc_now_iso8601();
                ::PostMessage(hwnd, UWM_BATCH_CANCELLED, reinterpret_cast<WPARAM>(st), 0);
                vips_thread_shutdown();
                return;
            }

            bItems[i].status = result.ok ? media::BatchItemStatus::Done
                                         : media::BatchItemStatus::Error;
            if (!result.ok) bItems[i].error = result.error;

            if (result.ok) {
                ++ok;
                ::PostMessage(hwnd, UWM_TRANSFORM_PROGRESS, (WPARAM)idx, 2);
                std::wstring wsrc = utf8_to_wide(input);
                std::wstring wout = utf8_to_wide(result.output_path);
                auto* pair = new std::pair<std::wstring, std::wstring>(wsrc, wout);
                ::PostMessage(hwnd, UWM_GENERATED_FILE, (WPARAM)pair, 0);
            } else {
                ++fail;
                ::PostMessage(hwnd, UWM_TRANSFORM_PROGRESS, (WPARAM)idx, 3);
                std::wstring errmsg = L"Error: " + utf8_to_wide(result.error);
                auto* ws = new wchar_t[errmsg.size() + 1];
                wcscpy_s(ws, errmsg.size() + 1, errmsg.c_str());
                ::PostMessage(hwnd, UWM_LOG_MESSAGE, (WPARAM)ws, 0);
            }
        }

        auto* finalState = new media::BatchState;
        finalState->session_id  = sessionId;
        finalState->op         = "transform";
        finalState->items      = std::move(bItems);
        finalState->updated_at = media::utc_now_iso8601();

        if (ctrl->cancel.load()) {
            ::PostMessage(hwnd, UWM_BATCH_CANCELLED,
                          reinterpret_cast<WPARAM>(finalState), 0);
        } else {
            ::PostMessage(hwnd, UWM_BATCH_STATE_UPDATE,
                          reinterpret_cast<WPARAM>(finalState), 0);
            ::PostMessage(hwnd, UWM_TRANSFORM_DONE, (WPARAM)ok, (LPARAM)fail);
        }
        vips_thread_shutdown();
    });
    SetStatusBarPartText(0, L"Transforming\u2026");
}

// ── Find ─────────────────────────────────────────────────────────────────────


void CMainFrame::OnFind()
{
    if (m_processing) {
        LogMessage(L"Find: another operation is in progress, please wait.");
        return;
    }
    if (!m_pDockSettings) return;

    FindSettings fs;
    m_pDockSettings->GetSettingsContainer().GetSettingsView().ReadFindSettings(fs);
    if (fs.prompt.empty()) {
        LogMessage(L"Find: prompt is required (type a query in the Find Settings panel).");
        ::MessageBoxW(GetHwnd(),
            L"Type a search query in the Find Settings panel first.",
            L"Find", MB_ICONINFORMATION | MB_OK);
        return;
    }

    // Inputs: prefer the Explorer selection (folder or files) — falls back to
    // every queue path. Both paths and folders are valid; find_images expands.
    std::vector<std::string> inputs;
    inputs.reserve(8);
    std::vector<int>         queueRowIdx; ///< parallel: inputs from queue (for status column)
    m_queueOpRowIndices.clear();

    // Use the *currently navigated* Explorer folder if no images are selected.
    if (!m_explorerSelectionPaths.empty()) {
        for (const auto& p : m_explorerSelectionPaths) inputs.push_back(wide_to_utf8(p));
    } else if (m_pDockFileTree) {
        const auto& folder = m_pDockFileTree->GetFileTreeContainer()
                                 .GetBrowserView().GetCurrentFolder();
        if (!folder.empty()) inputs.push_back(wide_to_utf8(folder));
    }

    if (inputs.empty() && m_pDockQueue) {
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        for (int i = 0; i < lv.QueueCount(); ++i) {
            CString p = lv.GetItemPath(i);
            if (p.IsEmpty()) continue;
            queueRowIdx.push_back(i);
            inputs.push_back(wide_to_utf8(std::wstring(p.c_str())));
        }
    }

    if (inputs.empty()) {
        LogMessage(L"Find: no inputs (select a folder in the Explorer panel, "
                   L"or add files to the queue first).");
        ::MessageBoxW(GetHwnd(),
            L"No inputs to search.\n\n"
            L"Either select a folder in the Explorer panel, "
            L"select one or more images there, or add files to the queue.",
            L"Find", MB_ICONINFORMATION | MB_OK);
        return;
    }

    // Build the lib options.
    media::FindOptions opts;
    opts.mode             = fs.llm ? media::FindMode::Llm : media::FindMode::Name;
    opts.prompt           = fs.prompt;
    opts.case_insensitive = fs.case_insensitive;
    opts.match_folders    = fs.match_folders;
    opts.recursive        = fs.recursive;
    opts.bypass_cache     = fs.bypass_cache;
    opts.generate         = fs.generate;
    opts.use_md           = fs.use_md;
    opts.use_json         = fs.use_json;
    opts.use_exif         = fs.use_exif;
    opts.max_results      = fs.max_results;
    opts.meta.provider    = fs.provider;
    opts.meta.model       = fs.model;
    opts.meta.resize_first= fs.resize_first;
    opts.meta.resize_width= fs.resize_width;
    // Persist generated cache so the next search is a hit.
    opts.meta.out_md      = true;
    opts.meta.out_json    = true;
    opts.meta.update_exif = false;
    opts.reference_images = fs.reference_images;

    if (opts.mode == media::FindMode::Llm && opts.meta.api_key.empty()) {
        // Match Meta / find core: Google Gemini (settings.json providers.google, then env).
        opts.meta.api_key = media::settings::get_google_provider_api_key();
        if (opts.meta.api_key.empty()) {
            LogMessage(L"Find LLM: no Google API key (set providers.google in settings).");
            ::MessageBoxW(GetHwnd(),
                L"LLM search needs a Google (Gemini) API key.\n\n"
                L"Save it under App Settings (AI Provider) for Google.",
                L"Find", MB_ICONWARNING | MB_OK);
            return;
        }
    }

    // Reset the panel and pop it open before kicking off the worker.
    if (m_pDockFindResults) {
        m_pDockFindResults->GetFindResultsContainer().GetView().ClearAll();
        EnsurePanelVisible(m_pDockFindResults, DS_DOCKED_RIGHT,
                           GetDockAncestor(), DpiScaleInt(360),
                           IDC_CMD_VIEW_FINDRESULTS);
    }

    LogMessage(L"--- Find started ---");
    {
        CString info;
        info.Format(L"Mode: %s  |  inputs: %d  |  prompt: \"%S\"",
                    fs.llm ? L"llm" : L"name",
                    (int)inputs.size(), fs.prompt.c_str());
        LogMessage(info);
    }
    // Verbose: log every input + every reference + the LLM toggles so the user
    // can see exactly what the worker is about to do.
    for (size_t i = 0; i < inputs.size(); ++i) {
        CString line; line.Format(L"  input[%zu]: %S", i, inputs[i].c_str());
        LogMessage(line);
    }
    if (fs.llm) {
        CString line;
        line.Format(L"  llm: model=%S  resize=%dpx  bypass-cache=%s  generate=%s  use=md/%s json/%s exif/%s  max=%d",
                    fs.model.c_str(), fs.resize_width,
                    fs.bypass_cache  ? L"yes" : L"no",
                    fs.generate      ? L"yes" : L"no",
                    fs.use_md        ? L"on"  : L"off",
                    fs.use_json      ? L"on"  : L"off",
                    fs.use_exif      ? L"on"  : L"off",
                    fs.max_results);
        LogMessage(line);
        for (size_t i = 0; i < fs.reference_images.size(); ++i) {
            CString rline; rline.Format(L"  reference[%zu]: %S", i, fs.reference_images[i].c_str());
            LogMessage(rline);
        }
        if (fs.reference_images.empty())
            LogMessage(L"  (no reference images — judgement is text-only)");
    } else {
        CString line;
        line.Format(L"  name: case-insensitive=%s  match-folders=%s  recursive=%s",
                    fs.case_insensitive ? L"yes" : L"no",
                    fs.match_folders    ? L"yes" : L"no",
                    fs.recursive        ? L"yes" : L"no");
        LogMessage(line);
    }

    m_queueOpRowIndices = std::move(queueRowIdx);
    if (m_pDockQueue && !m_queueOpRowIndices.empty()) {
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        for (int idx : m_queueOpRowIndices) {
            if (idx >= 0 && idx < lv.QueueCount()) lv.SetItemStatus(idx, L"Find\u2026");
        }
    }

    m_processing = true;
    m_batchCtrl  = std::make_shared<media::BatchControl>();
    m_queueTotal     = (int)inputs.size();
    m_queueDone      = 0;
    m_batchStartTick = (DWORD)::GetTickCount64();
    UpdateQueueStatusPart();
    InvalidateBatchUi();
    {
        // Find operates on folders/paths, not per-file queue entries; store inputs
        // as BatchItems so a cancelled find can be noted in the session.
        m_currentSession = {};
        m_currentSession.session_id = media::new_session_id();
        m_currentSession.op         = "find";
        m_currentSession.created_at = media::utc_now_iso8601();
        for (const auto& p : inputs) {
            media::BatchItem bi;
            bi.path   = p;
            bi.sha256 = media::file_sha256_fast(p);
            m_currentSession.items.push_back(std::move(bi));
        }
    }
    const std::string            sessionId = m_currentSession.session_id;
    std::vector<media::BatchItem> bItemsForPause = m_currentSession.items;
    HWND                          hwnd         = GetHwnd();
    auto                          ctrl         = m_batchCtrl;

    if (m_worker.joinable()) m_worker.join();
    std::vector<int> opRows = m_queueOpRowIndices;
    m_worker = std::thread([inputs = std::move(inputs), opts, sessionId, hwnd, ctrl, opRows = std::move(opRows),
                            bItemsForPause = std::move(bItemsForPause)]() mutable {
        const auto lastRow = std::make_shared<int>(-1);
        auto       progress  = [hwnd, opRows, inputs, lastRow](const std::string& msg) {
            auto* ws = new wchar_t[msg.size() * 2 + 2];
            int n = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), (int)msg.size(),
                                         ws, (int)(msg.size() * 2 + 1));
            ws[n] = L'\0';
            ::PostMessage(hwnd, UWM_LOG_MESSAGE, (WPARAM)ws, 0);
            if (!opRows.empty()) {
                int qrow = pick_queue_status_row(msg, inputs, opRows);
                if (qrow < 0 && *lastRow >= 0 && queue_status_row_sibling_hint(msg))
                    qrow = *lastRow;
                if (qrow < 0) return;
                int outn = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), (int)msg.size(), nullptr, 0);
                if (outn > 0) {
                    std::wstring w(outn, L'\0');
                    outn = MultiByteToWideChar(
                        CP_UTF8, 0, msg.c_str(), (int)msg.size(), w.data(), outn);
                    w.resize((size_t)outn);
                    auto* pstatus = new std::wstring(std::move(w));
                    ::PostMessage(hwnd, UWM_QUEUE_OP_STATUS, (WPARAM)(INT_PTR)qrow, (LPARAM)pstatus);
                    *lastRow = qrow;
                }
            }
        };

        const auto on_before_pause = [hwnd, sessionId, bItemsForPause] {
            auto* partial     = new media::BatchState;
            partial->session_id  = sessionId;
            partial->op         = "find";
            partial->items      = bItemsForPause;
            partial->updated_at = media::utc_now_iso8601();
            ::PostMessage(hwnd, UWM_BATCH_PAUSED, reinterpret_cast<WPARAM>(partial), 0);
        };

        media::FindResult r
            = media::find_images(inputs, opts, progress, ctrl.get(), on_before_pause);

        if (!r.ok && r.error == "find: cancelled") {
            ::PostMessage(hwnd, UWM_BATCH_CANCELLED, 0, 0);
            vips_thread_shutdown();
            return;
        }

        // Post find-specific done message unless cancelled via progress_cb.
        if (!r.ok) {
            const std::string e = "Find error: " + r.error;
            auto* ws = new wchar_t[e.size() * 2 + 2];
            int n = MultiByteToWideChar(CP_UTF8, 0, e.c_str(), (int)e.size(),
                                         ws, (int)(e.size() * 2 + 1));
            ws[n] = L'\0';
            ::PostMessage(hwnd, UWM_LOG_MESSAGE, (WPARAM)ws, 0);
            ::PostMessage(hwnd, UWM_FIND_DONE, 0, 0);
            ::PostMessage(hwnd, UWM_BATCH_STATE_UPDATE, 0, 0);
            vips_thread_shutdown();
            return;
        }

        if (!r.matches.empty()) {
            auto* batch = new std::vector<FindProgressRow>;
            batch->reserve(r.matches.size());
            for (const auto& m : r.matches) {
                FindProgressRow row;
                row.path   = utf8_to_wide(m.path);
                row.score  = m.score;
                row.source = utf8_to_wide(m.source);
                row.reason = utf8_to_wide(m.reason);
                batch->push_back(std::move(row));
            }
            ::PostMessage(hwnd, UWM_FIND_PROGRESS, (WPARAM)batch, 0);
        }
        ::PostMessage(hwnd, UWM_FIND_DONE, (WPARAM)1, (LPARAM)(int)r.matches.size());
        ::PostMessage(hwnd, UWM_BATCH_STATE_UPDATE, 0, 0);
        vips_thread_shutdown();
    });
    SetStatusBarPartText(0, L"Searching\u2026");
}

// ── Duplicates ─────────────────────────────────────────────────────────────

namespace {

void fill_duplicates_llm_keys(media::DuplicatesOptions& dopts)
{
    if (!dopts.meta_json_llm_compare) return;
#if defined(_WIN32)
    {
        std::string            cs_err;
        media::settings::ChatProviderSettings cs;
        if (media::settings::load_chat_provider(cs, cs_err)) {
            if (dopts.llm_router.empty()) dopts.llm_router = cs.router;
            if (dopts.llm_model.empty()) dopts.llm_model = cs.model;
            if (dopts.llm_timeout_ms <= 0 && cs.timeout_ms > 0) dopts.llm_timeout_ms = cs.timeout_ms;
        }
        media::runtime_settings::merge_provider_credentials(
            dopts.llm_router, false, dopts.llm_api_key, dopts.llm_base_url);
    }
#endif
    if (dopts.llm_router.empty()) dopts.llm_router = "openrouter";
    if (dopts.llm_model.empty()) dopts.llm_model = "openai/gpt-4o-mini";
    if (dopts.llm_timeout_ms <= 0) dopts.llm_timeout_ms = 60'000;
#if defined(_WIN32)
    if (dopts.llm_api_key.empty()) {
        std::string          err;
        media::settings::ProviderMap pm;
        if (media::settings::load_providers(pm, err)) {
            auto try_key = [&](const std::string& name) -> std::string {
                auto it = pm.find(name);
                return (it != pm.end()) ? it->second.api_key : std::string{};
            };
            dopts.llm_api_key = try_key(dopts.llm_router);
            if (dopts.llm_api_key.empty()) {
                for (const auto& [name, entry] : pm) {
                    if (!entry.api_key.empty()) {
                        dopts.llm_api_key = entry.api_key;
                        (void)name;
                        break;
                    }
                }
            }
        }
    }
#endif
}

} // namespace

void CMainFrame::OnDuplicates()
{
    if (m_processing) {
        LogMessage(L"Duplicates: another operation is in progress, please wait.");
        return;
    }
    if (!m_pDockSettings) return;

    media::DuplicatesOptions dopts;
    m_pDockSettings->GetSettingsContainer().GetSettingsView().ReadDuplicatesSettings(dopts);

    std::vector<std::string> inputs;
    inputs.reserve(8);
    std::vector<int>         queueRowIdx;
    m_queueOpRowIndices.clear();

    bool         usedExplorerFolderOnly = false;
    std::wstring explorerFolderW;
    if (!m_explorerSelectionPaths.empty()) {
        for (const auto& p : m_explorerSelectionPaths) inputs.push_back(wide_to_utf8(p));
    } else if (m_pDockFileTree) {
        const auto& folder = m_pDockFileTree->GetFileTreeContainer()
                                 .GetBrowserView().GetCurrentFolder();
        if (!folder.empty()) {
            inputs.push_back(wide_to_utf8(folder));
            usedExplorerFolderOnly = true;
            explorerFolderW        = folder;
        }
    }
    if (inputs.empty() && m_pDockQueue) {
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        for (int i = 0; i < lv.QueueCount(); ++i) {
            CString p = lv.GetItemPath(i);
            if (p.IsEmpty()) continue;
            queueRowIdx.push_back(i);
            inputs.push_back(wide_to_utf8(std::wstring(p.c_str())));
        }
    }
    // No Explorer selection, only a navigated folder: the scan used to pass the folder
    // path only, so the file queue stayed empty and status / session had no per-file rows.
    // Match other batch commands: fill the queue with images from that folder, then
    // run on those paths (same recursive setting as Duplicates).
    if (usedExplorerFolderOnly && m_pDockQueue) {
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        if (lv.QueueCount() == 0) {
            if (dopts.recursive) {
                AddFilesToQueue({ explorerFolderW }, false);
            } else {
                std::error_code ec;
                const fs::path base(explorerFolderW);
                for (const auto& p : pmui::queue_paths::paths_to_enqueue(base, false, ec))
                    lv.AddFile(CString(p.c_str()));
                CString s;
                s.Format(L"%d file(s) in queue", lv.QueueCount());
                SetStatusBarPartText(0, s);
                // No LoadPicture: duplicates scan runs on the worker; same vips race as batch.
                InvalidateBatchUi();
            }
            inputs.clear();
            queueRowIdx.clear();
            for (int i = 0; i < lv.QueueCount(); ++i) {
                CString p = lv.GetItemPath(i);
                if (p.IsEmpty()) continue;
                queueRowIdx.push_back(i);
                inputs.push_back(wide_to_utf8(std::wstring(p.c_str())));
            }
        }
    }
    if (inputs.empty()) {
        LogMessage(L"Duplicates: no inputs (select in Explorer or add to the queue).");
        ::MessageBoxW(GetHwnd(),
            L"No inputs. Select a folder in Explorer, select files there, or add files to the queue.",
            L"Duplicates", MB_ICONINFORMATION | MB_OK);
        return;
    }

    if (dopts.mode == media::DuplicatesMode::Meta && dopts.meta_json_llm_compare) {
        fill_duplicates_llm_keys(dopts);
        if (dopts.llm_api_key.empty()) {
            LogMessage(L"Duplicates: meta LLM compare needs an API key in App Settings (Chat / providers).");
            ::MessageBoxW(GetHwnd(),
                L"Set an API key in App Settings under Chat, or in the global providers list.",
                L"Duplicates", MB_ICONWARNING | MB_OK);
            return;
        }
    }
    if (dopts.mode == media::DuplicatesMode::Meta && dopts.meta_json_llm_compare
        && dopts.meta_json_implicit_generate) {
        std::string                      provider_name;
        media::ActiveImageProviderFromApp pe;
        (void)media::try_load_active_image_provider_from_app(pe, provider_name);
        dopts.meta.api_key  = pe.api_key;
        dopts.meta.base_url = pe.base_url;
    }

    if (m_pDockDuplicateResults) {
        m_pDockDuplicateResults->GetDuplicateResultsContainer().GetView().ClearAll();
        EnsurePanelVisible(m_pDockDuplicateResults, DS_DOCKED_RIGHT, GetDockAncestor(), DpiScaleInt(360),
                          IDC_CMD_VIEW_DUPLICATERESULTS);
    }
    m_lastDupReport = nlohmann::json::object();

    LogMessage(L"--- Duplicates started ---");
    for (size_t i = 0; i < inputs.size(); ++i) {
        CString line;
        line.Format(L"  input[%zu]: %S", i, inputs[i].c_str());
        LogMessage(line);
    }

    m_queueOpRowIndices = std::move(queueRowIdx);
    if (m_pDockQueue && !m_queueOpRowIndices.empty()) {
        auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
        for (int idx : m_queueOpRowIndices) {
            if (idx >= 0 && idx < lv.QueueCount()) lv.SetItemStatus(idx, L"Duplicates\u2026");
        }
    }

    m_processing  = true;
    m_batchCtrl   = std::make_shared<media::BatchControl>();
    m_queueTotal  = 0;
    m_queueDone   = 0;
    m_batchStartTick = (DWORD)::GetTickCount64();
    UpdateQueueStatusPart();
    InvalidateBatchUi();
    {
        m_currentSession = {};
        m_currentSession.session_id = media::new_session_id();
        m_currentSession.op         = "duplicates";
        m_currentSession.created_at = media::utc_now_iso8601();
        m_currentSession.options    = nlohmann::json::object();
        for (const auto& p : inputs) {
            media::BatchItem bi;
            bi.path   = p;
            bi.sha256 = media::file_sha256_fast(p);
            m_currentSession.items.push_back(std::move(bi));
        }
    }
    const std::string              sessionId = m_currentSession.session_id;
    HWND                            hwnd     = GetHwnd();
    auto                            ctrl     = m_batchCtrl;
    std::vector<media::BatchItem>   bItems   = m_currentSession.items;
    if (m_worker.joinable()) m_worker.join();
    std::vector<int> opRows = m_queueOpRowIndices;
    m_worker        = std::thread([dopts, inputs, hwnd, opRows = std::move(opRows), sessionId, ctrl,
                              bItems = std::move(bItems)]() mutable {
        const auto lastRow = std::make_shared<int>(-1);
        auto       progress  = [hwnd, opRows, inputs, lastRow](const std::string& msg) {
            auto* ws = new wchar_t[msg.size() * 2 + 2];
            int  n   = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), (int)msg.size(), ws,
                                          (int)(msg.size() * 2 + 1));
            ws[n] = L'\0';
            ::PostMessage(hwnd, UWM_LOG_MESSAGE, (WPARAM)ws, 0);
            if (!opRows.empty()) {
                int qrow = pick_queue_status_row(msg, inputs, opRows);
                if (qrow < 0 && *lastRow >= 0 && queue_status_row_sibling_hint(msg))
                    qrow = *lastRow;
                if (qrow < 0) return;
                int outn = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), (int)msg.size(), nullptr, 0);
                if (outn > 0) {
                    std::wstring w(outn, L'\0');
                    outn = MultiByteToWideChar(
                        CP_UTF8, 0, msg.c_str(), (int)msg.size(), w.data(), outn);
                    w.resize((size_t)outn);
                    auto* pstatus = new std::wstring(std::move(w));
                    ::PostMessage(hwnd, UWM_QUEUE_OP_STATUS, (WPARAM)(INT_PTR)qrow, (LPARAM)pstatus);
                    *lastRow = qrow;
                }
            }
        };
        auto on_before_pause = [hwnd, sessionId, bItems] {
            auto* partial = new media::BatchState;
            partial->session_id  = sessionId;
            partial->op          = "duplicates";
            partial->items       = bItems;
            partial->updated_at  = media::utc_now_iso8601();
            ::PostMessage(hwnd, UWM_BATCH_PAUSED, reinterpret_cast<WPARAM>(partial), 0);
        };
        auto dr = media::find_duplicates(inputs, dopts, progress, ctrl.get(), on_before_pause);
        if (dr.error == "duplicates: cancelled") {
            auto* finalState = new media::BatchState;
            finalState->session_id  = sessionId;
            finalState->op         = "duplicates";
            finalState->items      = std::move(bItems);
            finalState->updated_at = media::utc_now_iso8601();
            ::PostMessage(hwnd, UWM_BATCH_CANCELLED, reinterpret_cast<WPARAM>(finalState), 0);
            vips_thread_shutdown();
            return;
        }
        for (auto& it : bItems) {
            if (dr.ok) {
                it.status = media::BatchItemStatus::Done;
                it.error.clear();
            } else {
                it.status = media::BatchItemStatus::Error;
                it.error  = dr.error;
            }
        }
        {
            auto* st = new media::BatchState;
            st->session_id  = sessionId;
            st->op         = "duplicates";
            st->items      = std::move(bItems);
            st->updated_at = media::utc_now_iso8601();
            ::PostMessage(hwnd, UWM_BATCH_STATE_UPDATE, reinterpret_cast<WPARAM>(st), 0);
        }
        auto* out = new DuplicatesUiResult;
        out->report = dr.ok ? dr.report : nlohmann::json::object();
        if (!dr.ok) {
            out->ok   = false;
            out->error= utf8_to_wide(dr.error.empty() ? "duplicates: unknown error" : dr.error);
        } else {
            out->ok = true;
            for (const auto& g : dr.groups) {
                DupListRow row;
                row.method = utf8_to_wide(g.method);
                row.key    = utf8_to_wide(g.key);
                row.count  = static_cast<int>(g.paths.size());
                for (const auto& p : g.paths) row.paths.push_back(utf8_to_wide(p));
                if (!row.paths.empty()) row.samplePath = row.paths[0];
                out->rows.push_back(std::move(row));
            }
            enrich_dup_rows_from_report(out->rows, out->report);
        }
        ::PostMessage(hwnd, UWM_DUPLICATES_DONE, (WPARAM)out, 0);
        vips_thread_shutdown();
    });
    SetStatusBarPartText(0, L"Duplicates: scanning\u2026");
}

// -- RefreshChatContext / OnChat ------------------------------------------
// Push the current Explorer selection + folder into the chat view. Called
// from OnExplorerSelection (live updates), from OnChat (when the user opens
// the panel), and from CChatWebView (synchronously, just before
// a turn) so the model never sees stale context.
void CMainFrame::RefreshChatContext(bool explorer_ctrl_additive)
{
    std::wstring folder;
    const std::wstring viewerOpenPath = m_viewerManager.ActiveView().PreviewPathW();
    if (m_pDockFileTree) {
        folder = m_pDockFileTree->GetFileTreeContainer().GetBrowserView().GetCurrentFolder();
    } else {
        // Viewer preset can run without Explorer. Fall back to viewer state:
        // open file path (selection) + cwd/parent folder (folder context).
        if (!viewerOpenPath.empty()) {
            std::error_code ec_parent;
            const auto parent = fs::path(viewerOpenPath).parent_path();
            if (!parent.empty())
                folder = parent.wstring();
        }
        if (folder.empty()) {
            std::error_code ec_cwd;
            const auto cwd = fs::current_path(ec_cwd);
            if (!ec_cwd)
                folder = cwd.wstring();
        }
    }
    std::vector<std::wstring> fallback_sel = m_explorerSelectionPaths;
    if (!m_pDockFileTree && fallback_sel.empty() && !viewerOpenPath.empty())
        fallback_sel.push_back(viewerOpenPath);

    std::vector<std::wstring> chat_sel_filtered;
    const std::vector<std::wstring>* sel_use = &fallback_sel;
    if (IsChatWorkbench()) {
        chat_sel_filtered.reserve(fallback_sel.size());
        for (const auto& w : fallback_sel) {
            if (pmui::chat_context_path_allowed(w))
                chat_sel_filtered.push_back(w);
        }
        sel_use = &chat_sel_filtered;
    }
    const auto& sel = *sel_use;
#if defined(FEATURE_BROWSER) && defined(FEATURE_XBLOX) && FEATURE_XBLOX
    m_viewerManager.ActiveView().SetXbloxContext(sel, folder);
#endif
    if (IsChatWorkbench()) {
#ifdef FEATURE_CHAT_WEB
        if (pmui::chat_web_available() && m_workbenchClientChatWeb.IsWindow()) {
            std::string chat_lang;
            {
                media::settings::AppearanceSettings a{};
                std::string err;
                if (media::settings::load_appearance(a, err))
                    chat_lang = a.display_language;
            }
            m_workbenchClientChatWeb.SetContext(sel, folder, chat_lang, explorer_ctrl_additive);
        }
#endif
        return;
    }
#ifdef FEATURE_CHAT_WEB
    if (m_pDockChatWeb) {
        std::string chat_lang;
        {
            media::settings::AppearanceSettings a{};
            std::string                        err;
            if (media::settings::load_appearance(a, err))
                chat_lang = a.display_language;
        }
        m_pDockChatWeb->GetChatWebContainer().GetChatWebView().SetContext(sel, folder, chat_lang,
                                                                            explorer_ctrl_additive);
        return;
    }
#endif
}

// Open / focus the Chat dock and seed it with the current Explorer selection
// (or current folder when none). The chat agent runs in its own worker thread
// inside the view; m_worker / m_processing are NOT touched here. Routes to
// the WebView2 host (CDockChatWeb).
void CMainFrame::OnChat()
{
    if (IsChatWorkbench()) {
        RefreshChatContext();
#ifdef FEATURE_CHAT_WEB
        if (pmui::chat_web_available() && m_workbenchClientChatWeb.IsWindow()) {
            m_workbenchClientChatWeb.FocusInput();
            return;
        }
#endif
        return;
    }
    CDocker* dock = nullptr;
    int      defaultSize = DpiScaleInt(420);
#ifdef FEATURE_CHAT_WEB
    if (m_pDockChatWeb) {
        dock = m_pDockChatWeb;
    }
#endif

    EnsurePanelVisible(dock, DS_DOCKED_RIGHT, GetDockAncestor(), defaultSize, IDC_CMD_VIEW_CHAT);
#ifdef FEATURE_CHAT_WEB
    dock = m_pDockChatWeb;
#endif
    if (!dock) return;

    RefreshChatContext();
#ifdef FEATURE_CHAT_WEB
    if (m_pDockChatWeb) {
        m_pDockChatWeb->GetChatWebContainer().GetChatWebView().FocusInput();
        return;
    }
#endif
}

// ── Batch queue control ───────────────────────────────────────────────────────

void CMainFrame::OnPauseBatch()
{
    if (!m_batchCtrl || !m_processing) return;
    m_batchCtrl->pause();
    // Ribbon button state is driven by the own-ribbon batch sync.
    // The worker will post UWM_BATCH_PAUSED with the partial state once it
    // reaches the next check_pause() call between items.
    InvalidateBatchUi();
}

void CMainFrame::OnResumeBatch()
{
    if (!m_batchCtrl) return;
    m_batchCtrl->resume();
    ::PostMessage(GetHwnd(), UWM_BATCH_RESUMED, 0, 0);
    InvalidateBatchUi();
}

void CMainFrame::OnCancelBatch()
{
    if (!m_batchCtrl) return;
    m_batchCtrl->request_cancel();
    // UWM_BATCH_CANCELLED is posted by the worker when it exits the loop.
    InvalidateBatchUi();
}

// ── Save Session ──────────────────────────────────────────────────────────────
// Available any time the queue is non-empty.
// - If a batch has already run (m_currentSession has items + status): saves it
//   as-is with done/error/pending markers so resume can skip finished files.
// - If no batch has run yet (m_currentSession is empty): builds a fresh session
//   from the current queue + current mode (op). The settings options are left as
//   {} and the user re-applies them after Load Session — the primary value is
//   preserving the file list + op.

void CMainFrame::OnSaveSession()
{
    if (!m_pDockQueue) return;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
    const int qCount = lv.QueueCount();

    if (qCount == 0) {
        ::MessageBoxW(GetHwnd(),
            L"The queue is empty — add files first.",
            L"Save Session", MB_ICONINFORMATION);
        return;
    }

    // ── Determine op name from the current mode ───────────────────────────────
    auto opForMode = [](CSettingsView::Mode m) -> const char* {
        switch (m) {
        case CSettingsView::MODE_COMPRESS:   return "compress";
        case CSettingsView::MODE_META:       return "meta";
        case CSettingsView::MODE_TRANSFORM:  return "transform";
        case CSettingsView::MODE_FIND:       return "find";
        case CSettingsView::MODE_DUPLICATES: return "duplicates";
        default:                             return "resize";
        }
    };
    const std::string opName = opForMode(m_homeTabLastMode);

    // ── Build the PersistedSession ────────────────────────────────────────────
    media::settings::PersistedSession ps;

    if (!m_currentSession.empty()) {
        // A run has started (or completed/cancelled) — use the live state which
        // already has per-item done/error/pending markers.
        ps.session_id = m_currentSession.session_id;
        ps.op         = m_currentSession.op.empty() ? opName : m_currentSession.op;
        ps.options    = m_currentSession.options;
        ps.created_at = m_currentSession.created_at;
        ps.updated_at = media::utc_now_iso8601();
        for (const auto& bi : m_currentSession.items) {
            media::settings::SessionItem si;
            si.path   = bi.path;
            si.sha256 = bi.sha256;
            si.error  = bi.error;
            switch (bi.status) {
            case media::BatchItemStatus::Done:  si.status = "done";    break;
            case media::BatchItemStatus::Error: si.status = "error";   break;
            default:                            si.status = "pending";  break;
            }
            ps.items.push_back(std::move(si));
        }
    } else {
        // No run yet — build from the current queue.  All items are Pending.
        // Options are left empty; the user will re-select settings after Load.
        ps.session_id = media::new_session_id();
        ps.op         = opName;
        ps.options    = nlohmann::json::object();
        ps.created_at = media::utc_now_iso8601();
        ps.updated_at = ps.created_at;
        for (int i = 0; i < qCount; ++i) {
            CString p = lv.GetItemPath(i);
            const std::string path = wide_to_utf8(std::wstring(p.c_str()));
            media::settings::SessionItem si;
            si.path   = path;
            si.sha256 = media::file_sha256_fast(path);
            si.status = "pending";
            ps.items.push_back(std::move(si));
        }
    }

    std::string err;
    if (!media::settings::save_session(ps, err)) {
        CString msg(L"Failed to save session: ");
        msg += CString(err.c_str());
        ::MessageBoxW(GetHwnd(), msg, L"Save Session", MB_ICONERROR);
        return;
    }

    // Count done/err/pending for the status message.
    int done = 0, errc = 0, pend = 0;
    for (const auto& si : ps.items) {
        if      (si.status == "done")  ++done;
        else if (si.status == "error") ++errc;
        else                           ++pend;
    }
    CString status;
    status.Format(L"Session saved — op: %S  %d done  %d err  %d pending  id: %S",
                  ps.op.c_str(), done, errc, pend, ps.session_id.c_str());
    LogMessage(status);
    SetStatusBarPartText(0, L"Session saved");
}

// ── Load Session ──────────────────────────────────────────────────────────────

void CMainFrame::OnLoadSession()
{
    if (m_processing) {
        ::MessageBoxW(GetHwnd(), L"Cannot load a session while processing.",
                      L"Load Session", MB_ICONWARNING);
        return;
    }

    std::string err;
    const auto sessions = media::settings::list_sessions();
    if (sessions.empty()) {
        ::MessageBoxW(GetHwnd(),
            L"No saved sessions found.\n\n"
            L"Run a batch operation and click Save Session to create one.",
            L"Load Session", MB_ICONINFORMATION);
        return;
    }

    // Build a selection list shown in an InputBox-style dialog.
    // Format: "op | done/err/pending | session_id  (date)"
    std::wstring choices;
    for (const auto& s : sessions) {
        int d = 0, e = 0, p = 0;
        for (const auto& it : s.items) {
            if (it.status == "done")    ++d;
            else if (it.status == "error") ++e;
            else ++p;
        }
        wchar_t line[512];
        swprintf_s(line, 512,
            L"%-12S  %3d done  %3d err  %3d pending   %S\n",
            s.op.c_str(), d, e, p,
            s.session_id.c_str());
        choices += line;
    }

    // Use a simple dialog: show the list in a message box and ask for an index.
    // A more polished list picker can replace this later.
    std::wstring prompt = L"Saved sessions (enter number 1–"
        + std::to_wstring(sessions.size()) + L"):\n\n" + choices;
    wchar_t buf[8] = L"1";
    if (!::GetDlgItemTextW(GetHwnd(), 0, buf, 8)) {}  // just zero-init

    // Use InputBox pattern via a raw dialog since Win32 has no built-in.
    // Fall back to a single prompt iteration for simplicity.
    HWND hwnd = GetHwnd();
    (void)hwnd;

    const int choice = ::MessageBoxW(GetHwnd(),
        (prompt + L"\nLoad session 1? (Yes = 1, No = cancel)").c_str(),
        L"Load Session", MB_YESNO | MB_ICONQUESTION);
    if (choice != IDYES) return;

    int idx = 0; // load first session (simplified: full picker dialog TODO)
    if (idx < 0 || idx >= (int)sessions.size()) return;

    const auto& ps = sessions[idx];

    // Restore session into m_currentSession.
    m_currentSession = {};
    m_currentSession.session_id = ps.session_id;
    m_currentSession.op         = ps.op;
    m_currentSession.options    = ps.options;
    m_currentSession.created_at = ps.created_at;
    m_currentSession.updated_at = ps.updated_at;

    // Populate queue — Done items shown as skipped, Error items will be retried.
    if (!m_pDockQueue) return;
    auto& lv = m_pDockQueue->GetQueueContainer().GetListView();
    lv.ClearAll();

    for (const auto& si : ps.items) {
        media::BatchItem bi;
        bi.path   = si.path;
        bi.sha256 = si.sha256;
        bi.error  = si.error;
        // Error items are reset to Pending so they retry on next Run.
        if (si.status == "done")
            bi.status = media::BatchItemStatus::Done;
        else
            bi.status = media::BatchItemStatus::Pending;

        int rowIdx = lv.AddFile(CString(pmui::utf8_to_wide(si.path).c_str()));
        if (bi.status == media::BatchItemStatus::Done)
            lv.SetItemStatus(rowIdx, L"\u2713 (skipped)");
        else if (si.status == "error")
            lv.SetItemStatus(rowIdx, L"\u21ba retry");

        m_currentSession.items.push_back(std::move(bi));
    }

    // Switch Settings panel to the session's op mode.
    auto modeFor = [](const std::string& op) -> CSettingsView::Mode {
        if (op == "compress")   return CSettingsView::MODE_COMPRESS;
        if (op == "meta")       return CSettingsView::MODE_META;
        if (op == "transform")  return CSettingsView::MODE_TRANSFORM;
        if (op == "find")       return CSettingsView::MODE_FIND;
        if (op == "duplicates") return CSettingsView::MODE_DUPLICATES;
        return CSettingsView::MODE_RESIZE;
    };
    const auto mode = modeFor(ps.op);
    m_homeTabLastMode = mode;
    SwitchSettingsMode(mode);

    CString status;
    status.Format(L"Session loaded: op=%S  %d done (skip)  %d error (retry)  %d pending",
        ps.op.c_str(),
        m_currentSession.count_done(),
        m_currentSession.count_error(),
        m_currentSession.count_pending());
    LogMessage(status);
    SetStatusBarPartText(0, L"Session loaded — click Run to continue");
}

