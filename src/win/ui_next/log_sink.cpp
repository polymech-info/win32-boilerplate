#include "stdafx.h"
#include "log_sink.h"
#include "Resource.h"   // UWM_LOG_MESSAGE
#include "constants.hpp"
#include "helpers/text_conv.hpp"

#include <Windows.h>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>

namespace pmui {

namespace {

// Atomic so the chat / agent worker threads can read it without locking.
std::atomic<HWND> g_target_hwnd{ nullptr };

std::mutex                           s_pm_image_file_mu;
std::optional<std::filesystem::path> s_pm_image_log_dir_override;

/// @p text UTF-8, no trailing newlines. Posts heap `wchar_t*` the frame deletes in
/// `OnLogMessage` — see `LogPanel` / `CLogView::AppendLine` + cwd file mirror.
bool post_log_utf8_to_hwnd(HWND target, const std::string& text)
{
    if (!target || !::IsWindow(target)) return false;
    if (text.empty()) return false;
    const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if (wlen <= 0) return false;
    auto* const buf = new (std::nothrow) wchar_t[static_cast<size_t>(wlen) + 1U];
    if (!buf) return false;
    const int got = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), buf, wlen);
    if (got <= 0) {
        delete[] buf;
        return false;
    }
    buf[got] = L'\0';
    if (!::PostMessageW(target, UWM_LOG_MESSAGE, reinterpret_cast<WPARAM>(buf), 0)) {
        delete[] buf;
        return false;
    }
    return true;
}

template <typename Mutex>
class ui_log_sink : public spdlog::sinks::base_sink<Mutex> {
public:
    ui_log_sink() = default;

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        HWND const target = g_target_hwnd.load(std::memory_order_relaxed);
        if (!target) return;
        // Format with the active formatter (so we get level + payload like the
        // stderr sink does). Then convert UTF-8 to UTF-16 for the Log panel.
        spdlog::memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        std::string text = fmt::to_string(formatted);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        (void)post_log_utf8_to_hwnd(target, text);
    }

    void flush_() override {}
};

bool g_installed = false;

} // namespace

void install_ui_log_sink() {
    if (g_installed) return;
    g_installed = true;
    try {
        auto logger = spdlog::default_logger();
        if (!logger) return;
        auto sink = std::make_shared<ui_log_sink<std::mutex>>();
        logger->sinks().push_back(sink);
    } catch (...) {
        // Best-effort — never let log-sink installation crash the app.
    }
}

void set_ui_log_target(HWND hwnd) {
    g_target_hwnd.store(hwnd, std::memory_order_relaxed);
}

void post_log_panel_line_utf8(const std::string& utf8_line)
{
    if (utf8_line.empty()) return;
    std::string text = utf8_line;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    if (text.empty()) return;
    HWND const target = g_target_hwnd.load(std::memory_order_relaxed);
    (void)post_log_utf8_to_hwnd(target, text);
}

std::filesystem::path get_pm_image_log_file_directory()
{
    std::lock_guard<std::mutex> lock(s_pm_image_file_mu);
    namespace fs = std::filesystem;
    return s_pm_image_log_dir_override ? *s_pm_image_log_dir_override : fs::current_path();
}

void set_pm_image_log_file_directory(const std::filesystem::path& directory)
{
    std::lock_guard<std::mutex> lock(s_pm_image_file_mu);
    if (directory.empty()) {
        s_pm_image_log_dir_override.reset();
        return;
    }
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(directory, ec);
    if (ec)
        s_pm_image_log_dir_override.reset();
    else
        s_pm_image_log_dir_override = abs;
}

void append_pm_image_log_file_line_wide(const std::wstring& line)
{
    std::lock_guard<std::mutex> lock(s_pm_image_file_mu);
    try {
        namespace fs = std::filesystem;
        const fs::path base = s_pm_image_log_dir_override ? *s_pm_image_log_dir_override
                                                         : fs::current_path();
        const fs::path p    = base / fs::path(pm::brand::k_cwd_log_basename_w);
        std::ofstream    f(p, std::ios::app | std::ios::binary);
        if (!f) return;
        if (line.empty()) {
            f << '\n';
        } else {
            f << pmui::wide_to_utf8(line) << '\n';
        }
        f.flush();
    } catch (...) {
    }
}

bool apply_pm_image_log_directory_from_env()
{
    DWORD n = ::GetEnvironmentVariableW(L"PM_IMAGE_LOG_DIR", nullptr, 0);
    if (n == 0 || n > 65536)
        return false;
    std::wstring buf(n, L'\0');
    const DWORD got = ::GetEnvironmentVariableW(L"PM_IMAGE_LOG_DIR", buf.data(), n);
    if (got == 0 || got >= n)
        return false;
    buf.resize(got);
    while (!buf.empty()
           && (buf.back() == L' ' || buf.back() == L'\t' || buf.back() == L'\r' || buf.back() == L'\n'))
        buf.pop_back();
    if (buf.empty())
        return false;
    std::error_code ec;
    const std::filesystem::path dir(buf);
    const std::filesystem::path abs = std::filesystem::absolute(dir, ec);
    if (ec || abs.empty())
        return false;
    set_pm_image_log_file_directory(abs);
    return true;
}

} // namespace pmui
