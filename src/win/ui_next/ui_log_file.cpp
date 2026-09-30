// UI startup / layout trace: routes to the process default spdlog logger (`logger::*`) with
// channel `[startup]` via `pm::log`. Same session bookkeeping as before (QPC monotonic ms,
// settings-load miss tracing). No separate `ui-log.log` file — see `docs/logging.md`.
#include "stdafx.h"
#include "ui_log_file.hpp"
#include "log_sink.h"
#include "logging/pm_log.hpp"
#include "logger/logger.h"
#include "win/settings_store.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include <windows.h>

namespace pmui {
namespace {

std::mutex      g_mutex;
int             g_session_depth = 0;
LARGE_INTEGER   g_qpc_start{};
LARGE_INTEGER   g_qpc_freq{};
int             g_load_count = 0;
#if defined(_WIN32)
bool            g_process_t0_set     = false;
LARGE_INTEGER   g_qpc_process_t0{};
bool            g_after_cli_parse_set = false;
LARGE_INTEGER   g_qpc_after_cli_parse{};
std::atomic<bool> g_ui_chat_standalone{false};
#endif

uint64_t elapsed_ms_unlocked()
{
    if (g_qpc_freq.QuadPart == 0)
        return 0;
    LARGE_INTEGER c;
    (void)::QueryPerformanceCounter(&c);
    return static_cast<uint64_t>((c.QuadPart - g_qpc_start.QuadPart) * 1000 / g_qpc_freq.QuadPart);
}

void on_settings_load(const char* reason)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_session_depth <= 0)
        return;    
}

} // namespace

#if defined(_WIN32)
void ui_log_file_set_process_t0()
{
    if (g_process_t0_set)
        return;
    if (g_qpc_freq.QuadPart == 0)
        (void)::QueryPerformanceFrequency(&g_qpc_freq);
    (void)::QueryPerformanceCounter(&g_qpc_process_t0);
    g_process_t0_set = true;
}

void ui_log_file_mark_after_cli_parse()
{
    if (g_after_cli_parse_set)
        return;
    if (g_qpc_freq.QuadPart == 0)
        (void)::QueryPerformanceFrequency(&g_qpc_freq);
    (void)::QueryPerformanceCounter(&g_qpc_after_cli_parse);
    g_after_cli_parse_set = true;
}
#endif

void ui_log_file_event(const char* message)
{
    if (!message)
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_session_depth <= 0)
        return;
    const uint64_t ms = elapsed_ms_unlocked();
    pm::log::trace_lazy("startup", [&] {
        char b[800];
        (void)std::snprintf(b, sizeof b, "%5llu ms  %s", static_cast<unsigned long long>(ms), message);
        return std::string(b);
    });
}

void ui_log_file_eventf(const char* fmt, ...)
{
    if (!fmt)
        return;
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    (void)std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = '\0';
    ui_log_file_event(buf);
}

void ui_log_file_set_log_directory(const std::filesystem::path& directory)
{
    set_pm_image_log_file_directory(directory);
}

void ui_log_file_begin_session()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_session_depth;
    if (g_session_depth > 1)
        return;

    g_load_count = 0;
    (void)::QueryPerformanceFrequency(&g_qpc_freq);
    (void)::QueryPerformanceCounter(&g_qpc_start);

#if defined(_WIN32)
    uint64_t ms_entry_to_session     = 0;
    uint64_t ms_entry_to_cli_parse   = 0;
    uint64_t ms_cli_parse_to_session = 0;
    if (g_process_t0_set && g_qpc_freq.QuadPart) {
        ms_entry_to_session = static_cast<uint64_t>(
            (g_qpc_start.QuadPart - g_qpc_process_t0.QuadPart) * 1000 / g_qpc_freq.QuadPart);
        if (g_after_cli_parse_set) {
            ms_entry_to_cli_parse = static_cast<uint64_t>(
                (g_qpc_after_cli_parse.QuadPart - g_qpc_process_t0.QuadPart) * 1000 / g_qpc_freq.QuadPart);
            if (g_qpc_start.QuadPart >= g_qpc_after_cli_parse.QuadPart) {
                ms_cli_parse_to_session = static_cast<uint64_t>(
                    (g_qpc_start.QuadPart - g_qpc_after_cli_parse.QuadPart) * 1000 / g_qpc_freq.QuadPart);
            }
        }
    }
#endif

    pm::log::info("startup", std::string("----"));
    pm::log::info("startup", std::string("0 ms  session start (monotonic ms in following lines)"));
#if defined(_WIN32)
    if (g_process_t0_set) {
        if (g_after_cli_parse_set) {
            pm::log::info_lazy("startup", [&] {
                return std::string("0 ms  pre-session: ") + std::to_string(ms_entry_to_cli_parse)
                       + " ms process entry to after CLI11 parse; " + std::to_string(ms_cli_parse_to_session)
                       + " ms after parse to this session; " + std::to_string(ms_entry_to_session)
                       + " ms entry to session (before CoInit, …)";
            });
        } else {
            pm::log::info_lazy("startup", [&] {
                return std::string("0 ms  pre-session: ") + std::to_string(ms_entry_to_session)
                       + " ms from process entry to session (no CLI11 parse mark; "
                         "add ui_log_file_mark_after_cli_parse in pm_image_run for a split)";
            });
        }
    }
#endif

    media::settings::set_settings_load_tracing(&on_settings_load);
}

void ui_log_file_end_session()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_session_depth <= 0)
        return;
    --g_session_depth;
    if (g_session_depth > 0)
        return;

    media::settings::clear_settings_load_tracing();
    const int misses = g_load_count;
    const uint64_t ms = elapsed_ms_unlocked();
    pm::log::info_lazy("startup", [ms, misses] {
        return std::to_string(ms) + " ms  end session (load_settings cache-misses logged: " + std::to_string(misses)
               + ")";
    });
    logger::flush();
}

#if defined(_WIN32)
void set_ui_chat_standalone_session(bool standalone)
{
    g_ui_chat_standalone.store(standalone, std::memory_order_relaxed);
}

bool is_ui_chat_standalone_session()
{
    return g_ui_chat_standalone.load(std::memory_order_relaxed);
}

std::uint64_t ui_log_file_ms_from_process_entry()
{
    if (!g_process_t0_set || g_qpc_freq.QuadPart == 0)
        return 0;
    LARGE_INTEGER c;
    (void)::QueryPerformanceCounter(&c);
    return static_cast<std::uint64_t>(
        (c.QuadPart - g_qpc_process_t0.QuadPart) * 1000 / g_qpc_freq.QuadPart);
}

void ui_log_file_event_from_process_entry(const char* message)
{
    if (!message)
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_session_depth <= 0)
        return;
    const std::uint64_t ms = ui_log_file_ms_from_process_entry();
    pm::log::info_lazy("startup", [&] {
        return std::to_string(ms) + " ms (since process entry)  " + message;
    });
}
#endif

} // namespace pmui
