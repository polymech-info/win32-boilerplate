#include "pm_image_cmd_assistant.hpp"

// All meaningful implementation lives inside the Win32 + FEATURE_ASSISTANT guard.
#if defined(_WIN32) && defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <CLI/CLI.hpp>
#include "pm_image_cli_state.hpp"
#include "core/cli_cancel.hpp"
#include "logger/logger.h"
#include <objbase.h>   // must precede uia_spy.hpp — restores MIDL_INTERFACE
#ifndef interface      // CLI11 #undefs this; UIAutomationCore.h needs it
#  define interface struct __declspec(novtable)
#endif
#include "win/assistant/uia_spy.hpp"
#include "win/assistant/app_targets.hpp"
#include "win/assistant/assistant_bar.hpp"
#include "win/assistant/app_inspect.hpp"
#include "win/assistant/app_use.hpp"
#include "win/assistant/app_batch.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

#if defined(FEATURE_STT) && FEATURE_STT
#include "core/audio.hpp"
#include "stt/elevenlabs.hpp"
#include "core/settings_runtime.hpp"
#endif

// ═══════════════════════════════════════════════════════════════════════════
// Local helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// ─── Wide ↔ UTF-8 converters ──────────────────────────────────────────────────

std::wstring u2w(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                        nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                          out.data(), n);
    return out;
}

// ─── Chat-open helpers ────────────────────────────────────────────────────────

// Try to extract an existing file path from a window title.
// Many apps use "filename - AppName" or "C:\path\file.ext - AppName" patterns.
// Returns the best candidate (UTF-16 path), or empty if nothing plausible found.
std::wstring get_hwnd_doc_path(HWND hwnd) {
    if (!hwnd) return {};
    wchar_t title[1024] = {};
    if (!::GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))))
        return {};
    std::wstring t(title);
    // Strip trailing " - AppName" segments until we find an existing path.
    while (true) {
        const auto dash = t.rfind(L" - ");
        if (dash == std::wstring::npos) break;
        const std::wstring candidate = t.substr(0, dash);
        if (!candidate.empty() && ::GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
        t = t.substr(0, dash);
    }
    return {};
}

// Returns the full exe path of the process owning @p hwnd, or empty on failure.
std::wstring get_hwnd_exe_path(HWND hwnd) {
    if (!hwnd) return {};
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return {};
    HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return {};
    wchar_t path[MAX_PATH] = {};
    DWORD   n              = MAX_PATH;
    ::QueryFullProcessImageNameW(proc, 0, path, &n);
    ::CloseHandle(proc);
    return std::wstring(path, static_cast<size_t>(n));
}

// Returns the path of pm-image.exe in the same directory as pm-image-cli.exe.
std::wstring get_pm_image_path() {
    wchar_t self[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring path(self);
    const auto sep = path.rfind(L'\\');
    if (sep == std::wstring::npos) return L"pm-image.exe";
    return path.substr(0, sep + 1) + L"pm-image.exe";
}

// Escape a path/value for a Windows command-line double-quoted argument.
std::wstring cmd_quote(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 2);
    out += L'"';
    for (wchar_t c : s) {
        if (c == L'"') out += L'\\';
        out += c;
    }
    out += L'"';
    return out;
}

// Convert a wide string to UTF-8 for console/spdlog output.
std::string w2u(const std::wstring& ws) {
    if (ws.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0,
                                        ws.data(), static_cast<int>(ws.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0,
                          ws.data(), static_cast<int>(ws.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

// Truncate text for single-line display and add an ellipsis if needed.
std::string truncate(const std::wstring& ws, size_t max_chars = 120) {
    if (ws.empty()) return "(empty)";
    const std::wstring cut = ws.size() > max_chars
        ? ws.substr(0, max_chars) + L"…"
        : ws;
    // Replace control characters with visible markers.
    std::wstring safe;
    safe.reserve(cut.size());
    for (wchar_t c : cut) {
        if (c == L'\n')  { safe += L"↵"; continue; }
        if (c == L'\r')  { safe += L"←"; continue; }
        if (c == L'\t')  { safe += L"→"; continue; }
        if (c < 32)      { safe += L'·'; continue; }
        safe.push_back(c);
    }
    return w2u(safe);
}

// Simple wall-clock timestamp string (local time).
std::string timestamp_now() {
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}

// Print a labelled row.
void row(const char* label, const std::string& value) {
    std::cout << "  " << std::left << std::setw(14) << label << value << "\n";
}

nlohmann::json rect_json(const media::assistant::app_inspect::Rect& r) {
    return {{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h},
            {"left", r.x}, {"top", r.y}, {"right", r.x + r.w}, {"bottom", r.y + r.h}};
}

nlohmann::json element_json(const media::assistant::app_inspect::ElementInfo& e) {
    return {
        {"index", e.index},
        {"pid", e.pid},
        {"hwnd", reinterpret_cast<std::uintptr_t>(e.hwnd)},
        {"process", w2u(e.process)},
        {"windowTitle", w2u(e.window_title)},
        {"controlType", w2u(e.control_type)},
        {"name", w2u(e.name)},
        {"value", w2u(e.value)},
        {"automationId", w2u(e.automation_id)},
        {"className", w2u(e.class_name)},
        {"frameworkId", w2u(e.framework_id)},
        {"enabled", e.enabled},
        {"offscreen", e.offscreen},
        {"focused", e.focused},
        {"keyboardFocusable", e.keyboard_focusable},
        {"nativeHwnd", e.native_hwnd},
        {"patterns", {
            {"invoke", e.has_invoke},
            {"value", e.has_value},
            {"text", e.has_text},
            {"selectionItem", e.has_selection_item},
            {"expandCollapse", e.has_expand_collapse},
            {"scroll", e.has_scroll},
        }},
        {"rect", rect_json(e.rect)},
        {"center", {{"x", e.rect.x + e.rect.w / 2}, {"y", e.rect.y + e.rect.h / 2}}},
    };
}

std::string ascii_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool control_filter_mentions_cells(const std::string& controls) {
    const std::string c = ascii_lower(controls);
    return c.find("cell") != std::string::npos || c.find("dataitem") != std::string::npos;
}

bool likely_spreadsheet_target(const PmImageCliState& st) {
    const std::string haystack = ascii_lower(st.asst_process + " " + st.asst_title);
    return haystack.find("soffice") != std::string::npos ||
           haystack.find("libreoffice") != std::string::npos ||
           haystack.find("calc") != std::string::npos ||
           haystack.find("excel") != std::string::npos;
}

media::assistant::app_inspect::Query inspect_query_from_state(const PmImageCliState& st) {
    media::assistant::app_inspect::Query q;
    q.foreground = st.asst_foreground;
    q.pid = st.asst_pid > 0 ? static_cast<DWORD>(st.asst_pid) : 0;
    q.hwnd = st.asst_hwnd ? reinterpret_cast<HWND>(static_cast<std::uintptr_t>(st.asst_hwnd)) : nullptr;
    q.process_contains = u2w(st.asst_process);
    q.title_contains = u2w(st.asst_title);
    q.limit = st.asst_limit;
    q.probe_cells = st.asst_probe_cells ||
                    (st.asst_md && (control_filter_mentions_cells(st.asst_controls) || likely_spreadsheet_target(st)));
    return q;
}

bool parse_rect_arg(const std::string& s, media::assistant::app_inspect::Rect& r) {
    if (s.empty()) return false;
    std::vector<int> nums;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == 'x' || c == 'X' || std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                nums.push_back(std::stoi(cur));
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) nums.push_back(std::stoi(cur));
    if (nums.size() != 4) return false;
    r = {nums[0], nums[1], nums[2], nums[3]};
    return true;
}

int run_app_inspect(const PmImageCliState& st) {
    namespace ai = media::assistant::app_inspect;
    if (st.assistant_app_inspect_dump_cmd && st.assistant_app_inspect_dump_cmd->parsed()) {
        std::vector<ai::WindowDump> windows;
        std::string err;
        if (!ai::dump_windows(inspect_query_from_state(st), windows, err)) {
            std::cerr << "assistant app-inspect dump: " << err << "\n";
            return 1;
        }
        if (st.asst_md) {
            ai::MarkdownOptions opts;
            opts.control_types_csv = u2w(st.asst_controls);
            opts.text_max_chars = st.asst_text_max_chars;
            std::cout << ai::dump_windows_markdown(windows, opts);
            return 0;
        }
        nlohmann::json j;
        j["ok"] = true;
        j["windowCount"] = windows.size();
        j["windows"] = nlohmann::json::array();
        for (const auto& w : windows) {
            nlohmann::json jw = {
                {"pid", w.pid},
                {"hwnd", reinterpret_cast<std::uintptr_t>(w.hwnd)},
                {"process", w2u(w.process)},
                {"title", w2u(w.title)},
                {"rect", rect_json(w.rect)},
                {"elements", nlohmann::json::array()},
            };
            for (const auto& e : w.elements)
                jw["elements"].push_back(element_json(e));
            j["windows"].push_back(std::move(jw));
        }
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }

    if (st.assistant_app_inspect_screenshot_cmd && st.assistant_app_inspect_screenshot_cmd->parsed()) {
        ai::Rect rect;
        HWND window_capture = nullptr;
        std::string err;
        if (!st.asst_rect.empty()) {
            try {
                if (!parse_rect_arg(st.asst_rect, rect)) {
                    std::cerr << "assistant app-inspect screenshot: --rect must be x,y,w,h\n";
                    return 1;
                }
            } catch (const std::exception& ex) {
                std::cerr << "assistant app-inspect screenshot: invalid --rect: " << ex.what() << "\n";
                return 1;
            }
        } else if (st.asst_element >= 0) {
            if (!ai::find_element_rect(inspect_query_from_state(st), st.asst_element, rect, err)) {
                std::cerr << "assistant app-inspect screenshot: " << err << "\n";
                return 1;
            }
        } else {
            const auto q = inspect_query_from_state(st);
            if (q.foreground || q.pid || q.hwnd || !q.process_contains.empty() || !q.title_contains.empty()) {
                std::vector<ai::WindowDump> windows;
                if (!ai::dump_windows(q, windows, err)) {
                    std::cerr << "assistant app-inspect screenshot: " << err << "\n";
                    return 1;
                }
                if (windows.empty()) {
                    std::cerr << "assistant app-inspect screenshot: no matching window\n";
                    return 1;
                }
                window_capture = windows.front().hwnd;
                rect = windows.front().rect;
                if (!st.asst_no_activate) {
                    std::string act_err;
                    if (!media::assistant::app_use::activate_window(window_capture, act_err)) {
                        std::cerr << "assistant app-inspect screenshot: " << act_err
                                  << " (pass --no-activate to skip foregrounding)\n";
                        return 1;
                    }
                    // Re-resolve rect: window may have moved during restore/raise.
                    RECT rr{};
                    if (::GetWindowRect(window_capture, &rr) && rr.right > rr.left && rr.bottom > rr.top)
                        rect = {rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top};
                }
            } else {
                RECT sr{};
                sr.left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
                sr.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
                sr.right = sr.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
                sr.bottom = sr.top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
                rect = {sr.left, sr.top, sr.right - sr.left, sr.bottom - sr.top};
            }
        }
        if (st.asst_output.empty()) {
            std::cerr << "assistant app-inspect screenshot: --output is required\n";
            return 1;
        }
        std::filesystem::path out = std::filesystem::u8path(st.asst_output);
        if (out.is_relative()) out = std::filesystem::absolute(out);
        if (out.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(out.parent_path(), ec);
        }
        const char* capture_method = window_capture ? "window" : "screen";
        if (window_capture) {
            if (!ai::save_window_jpeg(window_capture, rect, out.wstring(), st.asst_quality, err)) {
                std::cerr << "assistant app-inspect screenshot: " << err << "\n";
                return 1;
            }
        } else if (!ai::save_screen_rect_jpeg(rect, out.wstring(), st.asst_quality, err)) {
            std::cerr << "assistant app-inspect screenshot: " << err << "\n";
            return 1;
        }
        nlohmann::json j{{"ok", true}, {"output", out.string()}, {"capture", capture_method}, {"rect", rect_json(rect)}};
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }

    std::cerr << "assistant app-inspect: no subcommand selected.\n";
    return 1;
}

int run_app_use(const PmImageCliState& st) {
    namespace au = media::assistant::app_use;
    std::string err;
    nlohmann::json j{{"ok", true}};

    if (st.assistant_app_use_open_cmd && st.assistant_app_use_open_cmd->parsed()) {
        au::LaunchOptions opts;
        opts.exe = u2w(st.asst_exe);
        opts.args = u2w(st.asst_args);
        opts.cwd = u2w(st.asst_cwd);
        opts.x = st.asst_x; opts.y = st.asst_y; opts.w = st.asst_w; opts.h = st.asst_h;
        opts.wait_ms = st.asst_wait_ms;
        au::LaunchResult res;
        if (!au::open_app(opts, res, err)) {
            std::cerr << "assistant app-use open-app: " << err << "\n";
            return 1;
        }
        j["pid"] = res.pid;
        j["hwnd"] = reinterpret_cast<std::uintptr_t>(res.hwnd);
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }
    if (st.assistant_app_use_click_cmd && st.assistant_app_use_click_cmd->parsed()) {
        au::Point pt{st.asst_x, st.asst_y};
        if (st.asst_api_width > 0 && st.asst_api_height > 0)
            pt = au::scale_point_from_api(pt, st.asst_api_width, st.asst_api_height);
        if (!au::click_point(pt.x, pt.y, u2w(st.asst_button), st.asst_count, st.asst_virtual, err)) {
            std::cerr << "assistant app-use click: " << err << "\n";
            return 1;
        }
        j["x"] = pt.x; j["y"] = pt.y; j["button"] = st.asst_button.empty() ? "left" : st.asst_button;
        j["count"] = st.asst_count; j["virtual"] = st.asst_virtual;
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }
    if (st.assistant_app_use_mouse_move_cmd && st.assistant_app_use_mouse_move_cmd->parsed()) {
        au::Point pt{st.asst_x, st.asst_y};
        if (st.asst_api_width > 0 && st.asst_api_height > 0)
            pt = au::scale_point_from_api(pt, st.asst_api_width, st.asst_api_height);
        if (!au::move_mouse(pt.x, pt.y, err)) {
            std::cerr << "assistant app-use mouse-move: " << err << "\n";
            return 1;
        }
        j["x"] = pt.x; j["y"] = pt.y;
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }
    if (st.assistant_app_use_type_cmd && st.assistant_app_use_type_cmd->parsed()) {
        if (!au::type_text(u2w(st.asst_text), err)) {
            std::cerr << "assistant app-use type: " << err << "\n";
            return 1;
        }
        j["chars"] = st.asst_text.size();
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }
    if (st.assistant_app_use_hotkey_cmd && st.assistant_app_use_hotkey_cmd->parsed()) {
        auto keys = au::split_keys(u2w(st.asst_keys));
        if (!au::send_hotkey(keys, err)) {
            std::cerr << "assistant app-use hotkey: " << err << "\n";
            return 1;
        }
        j["keys"] = st.asst_keys;
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }
    if (st.assistant_app_use_key_press_cmd && st.assistant_app_use_key_press_cmd->parsed()) {
        // Single-key down -> hold(asst_hold_ms) -> up. The shell adds the
        // inter-keystroke gap; this command focuses on per-note sustain so
        // melody / chord timing can be scripted from the harness without a
        // batch JSON file.
        if (st.asst_key.empty()) {
            std::cerr << "assistant app-use key-press: --key is required\n";
            return 1;
        }
        std::vector<std::wstring> mods;
        if (!st.asst_keys.empty()) {
            for (auto& m : au::split_keys(u2w(st.asst_keys))) mods.push_back(std::move(m));
        }
        if (!au::press_key(u2w(st.asst_key), mods, st.asst_hold_ms, err)) {
            std::cerr << "assistant app-use key-press: " << err << "\n";
            return 1;
        }
        j["key"] = st.asst_key;
        j["hold_ms"] = st.asst_hold_ms;
        if (!mods.empty()) {
            nlohmann::json mj = nlohmann::json::array();
            for (const auto& m : mods) mj.push_back(w2u(m));
            j["modifiers"] = std::move(mj);
        }
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }
    if (st.assistant_app_use_cursor_cmd && st.assistant_app_use_cursor_cmd->parsed()) {
        au::Point pt = au::cursor_position();
        j["x"] = pt.x;
        j["y"] = pt.y;
        if (st.asst_api_width > 0 && st.asst_api_height > 0) {
            au::Point api = au::scale_point_to_api(pt, st.asst_api_width, st.asst_api_height);
            j["apiX"] = api.x;
            j["apiY"] = api.y;
            j["apiWidth"] = st.asst_api_width;
            j["apiHeight"] = st.asst_api_height;
        }
        auto screen = au::virtual_screen();
        j["screen"] = {{"x", screen.x}, {"y", screen.y}, {"width", screen.width}, {"height", screen.height}};
        std::cout << j.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }
    if (st.assistant_app_use_batch_cmd && st.assistant_app_use_batch_cmd->parsed()) {
        if (st.asst_batch_file.empty()) {
            std::cerr << "assistant app-use batch: --file is required\n";
            return 1;
        }
        std::ifstream in(std::filesystem::u8path(st.asst_batch_file), std::ios::binary);
        if (!in) {
            std::cerr << "assistant app-use batch: could not open " << st.asst_batch_file << "\n";
            return 1;
        }
        nlohmann::json doc;
        try {
            in >> doc;
        } catch (const std::exception& ex) {
            std::cerr << "assistant app-use batch: invalid JSON: " << ex.what() << "\n";
            return 1;
        }
        media::assistant::app_batch::RunOptions opts;
        opts.default_delay_ms = std::max(0, st.asst_wait_ms);
        opts.stop_on_error = !st.asst_continue_on_error;
        nlohmann::json report;
        if (!media::assistant::app_batch::run_json_batch(doc, opts, report, err)) {
            std::cout << report.dump(st.asst_json ? 2 : -1) << "\n";
            return 1;
        }
        std::cout << report.dump(st.asst_json ? 2 : -1) << "\n";
        return 0;
    }

    std::cerr << "assistant app-use: no subcommand selected.\n";
    return 1;
}

// ─── Format and print a full snapshot ────────────────────────────────────────
void print_snapshot(const media::assistant::FocusSnapshot& snap,
                    bool identity_changed,
                    bool content_changed,
                    bool verbose_text)
{
    const std::string ts = timestamp_now();

    if (identity_changed) {
        std::cout << "\n";
        std::cout << "╔══════════════════════════════════════════════════════════╗\n";
        std::cout << "║  [" << ts << "]  FOCUS CHANGED\n";
        std::cout << "╚══════════════════════════════════════════════════════════╝\n";
    } else {
        std::cout << "\n── [" << ts << "]  content changed ──\n";
    }

    // ── Identity ─────────────────────────────────────────────────────────────
    if (identity_changed) {
        row("Process:",   w2u(snap.process_name)
                          + "  (PID " + std::to_string(snap.process_id) + ")");
        row("App:",       snap.target.label);
        row("Window:",    snap.window_title.empty()
                              ? "(none)"
                              : "\"" + w2u(snap.window_title) + "\"");
        row("Control:",   w2u(snap.control_type_name)
                          + "  (UIA type " + std::to_string(snap.control_type_id) + ")");
        row("ClassName:", snap.class_name.empty()  ? "(none)" : w2u(snap.class_name));
        row("AutomId:",   snap.automation_id.empty()? "(none)" : w2u(snap.automation_id));
        row("Name:",      snap.name.empty()         ? "(none)" : "\"" + w2u(snap.name) + "\"");
        row("Framework:", snap.framework_id.empty() ? "(none)" : w2u(snap.framework_id));

        const RECT& r = snap.bounds;
        std::ostringstream br;
        br << "left=" << r.left << "  top=" << r.top
           << "  right=" << r.right << "  bottom=" << r.bottom
           << "  (" << (r.right - r.left) << "×" << (r.bottom - r.top) << " px)";
        row("Bounds:",    br.str());

        // Read-strategy hints for this app
        const auto& h = snap.target.hints;
        std::string hints;
        if (h.try_value_pattern)  hints += "ValuePattern ";
        if (h.try_text_pattern)   hints += "TextPattern ";
        if (h.try_clipboard_copy) hints += "ClipboardCopy";
        row("Hints:",     hints.empty() ? "(none)" : hints);
    }

    // ── Content ───────────────────────────────────────────────────────────────
    if (content_changed || identity_changed) {
        std::cout << "  ──── content ────\n";

        // ValuePattern
        if (snap.value_text.empty()) {
            std::cout << "  Value:         (no ValuePattern or empty)\n";
        } else {
            std::cout << "  Value:         " << truncate(snap.value_text) << "\n";
            if (verbose_text && snap.value_text.size() > 120) {
                std::cout << "  [full value " << snap.value_text.size() << " chars]\n";
                std::cout << w2u(snap.value_text) << "\n";
            }
        }

        // TextPattern — selection
        if (snap.selected_text.empty()) {
            std::cout << "  Selection:     (none / no TextPattern selection)\n";
        } else {
            std::cout << "  Selection:     \"" << truncate(snap.selected_text, 200) << "\"\n";
            if (verbose_text && snap.selected_text.size() > 200) {
                std::cout << "  [full selection " << snap.selected_text.size() << " chars]\n";
                std::cout << w2u(snap.selected_text) << "\n";
            }
        }

        // TextPattern — full document
        if (snap.full_text.empty()) {
            std::cout << "  FullText:      (no TextPattern document or empty)\n";
        } else {
            std::cout << "  FullText:      " << truncate(snap.full_text) << "\n";
            if (verbose_text) {
                // Print the full buffer when --verbose is active
                std::cout << "  [full text " << snap.full_text.size() << " chars]\n";
                // Split by newlines and emit up to 50 lines to avoid drowning the console.
                size_t start = 0, lines = 0;
                const auto& ft = snap.full_text;
                while (start < ft.size() && lines < 50) {
                    const size_t nl = ft.find(L'\n', start);
                    const size_t end = (nl == std::wstring::npos) ? ft.size() : nl;
                    std::cout << "    " << w2u(ft.substr(start, end - start)) << "\n";
                    start = end + 1;
                    ++lines;
                }
                if (lines == 50 && start < ft.size()) {
                    const size_t remaining_lines = std::count(ft.begin() + static_cast<long long>(start),
                                                              ft.end(), L'\n');
                    std::cout << "    … (" << remaining_lines << " more lines)\n";
                }
            }
        }
    }

    std::cout.flush();
}

#if defined(FEATURE_STT) && FEATURE_STT

// ─── STT write-back helpers ───────────────────────────────────────────────
// Inject a UTF-8 string into the currently focused element by simulating
// KEYEVENTF_UNICODE key events via SendInput.  Works with every target app
// (Notepad, LibreOffice, Chrome, etc.) because the OS treats synthetic events
// identically to real keyboard input.  Thread-safe — SendInput is reentrant.

static void send_text_to_focused(const std::string& utf8) {
    if (utf8.empty()) return;
    const int n = ::MultiByteToWideChar(CP_UTF8, 0,
                                        utf8.data(), static_cast<int>(utf8.size()),
                                        nullptr, 0);
    if (n <= 0) return;
    std::wstring ws(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0,
                          utf8.data(), static_cast<int>(utf8.size()),
                          ws.data(), n);
    std::vector<INPUT> inp;
    inp.reserve(ws.size() * 2);
    for (wchar_t c : ws) {
        INPUT k{};
        k.type       = INPUT_KEYBOARD;
        k.ki.wScan   = c;
        k.ki.dwFlags = KEYEVENTF_UNICODE;
        inp.push_back(k);
        k.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inp.push_back(k);
    }
    ::SendInput(static_cast<UINT>(inp.size()), inp.data(), sizeof(INPUT));
}

// ─── Full STT dictation loop ─────────────────────────────────────────────
// Connects ElevenLabs Scribe v2 Realtime, starts microphone capture with VAD,
// and writes each committed utterance segment into the currently focused
// UI element.  Blocks until cancel_fn() returns true.

// stt_muted: when true the mic is suppressed — audio is captured but not sent to
// the server.  Shared with the toolbar mic button toggle callback.
static void run_stt_dictate(const PmImageCliState&           st,
                             std::function<bool()>            cancel_fn,
                             std::shared_ptr<std::atomic<bool>> stt_muted = nullptr)
{
    namespace chr = std::chrono;
    using clk = chr::steady_clock;

    // ── Provider resolution (CLI > chat settings) ─────────────────────────
    media::runtime_settings::ChatProviderSettings cs;
    { std::string e; media::runtime_settings::load_chat_provider(cs, e); }
    media::runtime_settings::ProviderMap pmap;
    { std::string e; media::runtime_settings::load_providers(pmap, e); }

    const std::string provider = !st.asst_stt_provider.empty()
                                 ? st.asst_stt_provider : cs.stt_provider;
    std::string api_key = st.asst_stt_api_key;
    if (api_key.empty()) {
        if (auto it = pmap.find(provider); it != pmap.end())
            api_key = it->second.api_key;
    }
    if (provider.empty() || api_key.empty()) {
        std::cerr << "[spy --stt] No STT provider configured.\n"
                  << "  Set in App Settings -> Chat Provider -> Voice & Audio,\n"
                  << "  or pass --stt-provider and --stt-api-key.\n";
        return;
    }

    // ── ElevenLabs Scribe v2 Realtime ─────────────────────────────────────
    pm::stt::ElevenLabsSTT stt;
    std::mutex stt_mtx;
    std::string last_committed_text;

    stt.on_partial = [](const std::string& t) {
        const auto snippet = t.size() > 60 ? t.substr(t.size() - 60) : t;
        std::cout << "\r  \u25d1 " << snippet
                  << std::string(4, ' ') << std::flush;
    };

    stt.on_committed = [&](const std::string& t) {
        if (t.empty()) return;
        std::string segment;
        {
            std::lock_guard<std::mutex> lk(stt_mtx);
            if (t == last_committed_text) return;
            // ElevenLabs returns the cumulative transcript; extract delta.
            if (t.size() > last_committed_text.size() &&
                t.compare(0, last_committed_text.size(), last_committed_text) == 0) {
                segment = t.substr(last_committed_text.size());
                const auto p = segment.find_first_not_of(" \t");
                if (p != std::string::npos && p > 0) segment = segment.substr(p);
            } else {
                segment = t; // server corrected/reset — use whole transcript
            }
            last_committed_text = t;
        }
        if (segment.empty()) return;
        std::cout << "\r[stt] " << segment << "\n" << std::flush;
        // Inject into focused element, followed by a space separator.
        send_text_to_focused(segment);
        send_text_to_focused(" ");
    };

    stt.on_error = [](const std::string& e) {
        std::cerr << "\n[stt error] " << e << "\n" << std::flush;
    };

    pm::stt::ElevenLabsSTT::Config scfg;
    scfg.api_key     = api_key;
    scfg.sample_rate = 16000;
    if (!cs.stt_model.empty() && cs.stt_model != "pixlwiz-speech-to-text")
        scfg.model_id = cs.stt_model; // default stays "scribe_v2_realtime"

    std::cout << "[spy --stt] connecting to ElevenLabs STT (" << provider << ")...\n"
              << std::flush;
    std::string conn_err;
    std::atomic<bool> conn_done{false};
    std::thread conn_thread([&] {
        try { stt.connect(scfg); }
        catch (const std::exception& ex) { conn_err = ex.what(); }
        conn_done.store(true);
    });
    while (!conn_done.load() && !cancel_fn())
        std::this_thread::sleep_for(chr::milliseconds(100));
    if (cancel_fn()) { stt.close(); conn_thread.join(); return; }
    conn_thread.join();
    if (!conn_err.empty()) {
        std::cerr << "[spy --stt] connect failed: " << conn_err << "\n";
        return;
    }
    std::cout << "[spy --stt] STT session ready — speak now\n" << std::flush;

    // ── Audio capture + VAD ───────────────────────────────────────────────
    const int      silence_ms      = std::max(300, st.asst_stt_silence_ms);
    const bool     live_send       = st.asst_stt_live;
    constexpr double k_rms_thresh  = 200.0; // RMS on 0-32767 scale
    bool   vad_has_speech          = false;
    bool   vad_committed           = false;
    auto   vad_last_speech_tp      = clk::now();
    // Utterance buffer used in non-live mode: accumulate PCM until VAD commit,
    // then flush the whole utterance to the server in one shot.
    std::vector<int16_t> pcm_buf;

    auto on_pcm = [&](const int16_t* data, size_t frames) {
        if (frames == 0) return;

        // When the mic button has been toggled off, suppress audio to the server
        // but keep the VAD running so we can pick up immediately on re-enable.
        if (stt_muted && stt_muted->load()) return;

        if (live_send) {
            // Stream every chunk immediately → server sends partials in real time.
            stt.send_pcm(data, frames);
        } else {
            // Buffer until VAD commit → send the complete utterance at once.
            pcm_buf.insert(pcm_buf.end(), data, data + frames);
        }

        // VAD: compute RMS and track silence duration.
        double sum = 0.0;
        for (size_t i = 0; i < frames; ++i)
            sum += static_cast<double>(data[i]) * data[i];
        const double rms = std::sqrt(sum / static_cast<double>(frames));
        const auto   now = clk::now();
        if (rms >= k_rms_thresh) {
            vad_last_speech_tp = now;
            if (!vad_has_speech) { vad_has_speech = true; vad_committed = false; }
        } else if (vad_has_speech && !vad_committed) {
            const auto silent_ms = chr::duration_cast<chr::milliseconds>(
                now - vad_last_speech_tp).count();
            if (silent_ms >= silence_ms) {
                vad_committed = true; vad_has_speech = false;
                if (!live_send && !pcm_buf.empty()) {
                    // Flush buffered utterance now that silence is confirmed.
                    stt.send_pcm(pcm_buf.data(), pcm_buf.size());
                    pcm_buf.clear();
                }
                stt.commit(); // thread-safe: just enqueues a WS frame
            }
        }
    };

    pm::audio::AudioInput mic;
    try { mic.start(on_pcm); }
    catch (const std::exception& ex) {
        std::cerr << "[spy --stt] audio capture failed: " << ex.what() << "\n";
        stt.close(); return;
    }
    std::cout << "[spy --stt] mic: " << mic.opened_device_name() << "\n"
              << "[spy --stt] VAD: auto-commit after " << silence_ms << " ms silence\n"
              << std::flush;

    while (!cancel_fn())
        std::this_thread::sleep_for(chr::milliseconds(100));

    mic.stop();
    // Drain: commit any open utterance and give the server up to 3 s.
    stt.commit();
    const auto drain_end = clk::now() + chr::seconds(3);
    while (clk::now() < drain_end)
        std::this_thread::sleep_for(chr::milliseconds(50));
    stt.close();
    std::cout << "\n[spy --stt] stopped.\n";
}

#endif // FEATURE_STT

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// CLI handler
// ═══════════════════════════════════════════════════════════════════════════

int pm_image_cmd_assistant(CLI::App& /*app*/, PmImageCliState& st)
{
    if (st.assistant_app_inspect_cmd && st.assistant_app_inspect_cmd->parsed())
        return run_app_inspect(st);
    if (st.assistant_app_use_cmd && st.assistant_app_use_cmd->parsed())
        return run_app_use(st);
    if (!st.assistant_spy_cmd || !st.assistant_spy_cmd->parsed()) return 0;

    media::cli::install_cli_interrupt_handlers();

    // Build options from CLI state.
    media::assistant::SpyOptions opts;
    opts.interval_ms    = std::max(50, st.asst_interval_ms);
    opts.dump_value     = !st.asst_no_value;
    opts.dump_selection = !st.asst_no_selection;
    opts.dump_full_text = !st.asst_no_text;
    opts.log_unchanged  = st.asst_log_all;
    opts.text_max_chars = st.asst_text_max_chars;
    const bool verbose  = st.asst_verbose;

    std::cout << "pm-image auto spy — UIAutomation focus probe (Win32)\n";
    std::cout << "  interval:    " << opts.interval_ms << " ms\n";
    std::cout << "  value:       " << (opts.dump_value     ? "on" : "off") << "\n";
    std::cout << "  selection:   " << (opts.dump_selection ? "on" : "off") << "\n";
    std::cout << "  full-text:   " << (opts.dump_full_text ? "on" : "off") << "\n";
    std::cout << "  text-limit:  " << opts.text_max_chars  << " chars\n";
    std::cout << "  verbose:     " << (verbose             ? "on" : "off") << "\n";
    std::cout << "  log-all:     " << (opts.log_unchanged  ? "on" : "off") << "\n";
#if defined(FEATURE_STT) && FEATURE_STT
    if (st.asst_stt)
        std::cout << "  stt:         on (ElevenLabs Scribe v2 Realtime + write-back)\n"
                  << "  stt-live:    " << (st.asst_stt_live ? "on (stream each chunk)" : "off (buffer per utterance)") << "\n"
                  << "  stt-silence: " << st.asst_stt_silence_ms << " ms VAD threshold\n";
#endif
    std::cout << "  Press Ctrl+C to stop.\n\n";
    std::cout.flush();

    // ── Last spy context (selected/value text) — shared with chat-open ───────
    auto last_spy_wtext = std::make_shared<std::wstring>();
    auto last_spy_mutex = std::make_shared<std::mutex>();

    // ── Shared spy-paused flag ────────────────────────────────────────────────
    // Toggled by the bar's spy button; opts.paused causes the loop to yield.
    auto spy_paused = std::make_shared<std::atomic<bool>>(false);
    opts.paused = spy_paused;

    // ── Shared STT mute flag (toolbar mic button ↔ on_pcm) ───────────────────
#if defined(FEATURE_STT) && FEATURE_STT
    auto stt_muted = std::make_shared<std::atomic<bool>>(false);
#endif

    // ── Toolbar on its own thread (Win32 message loop) ────────────────────────
    media::assistant::AssistantBar* bar_ptr = nullptr;
    std::thread bar_thread;
    if (st.asst_ui) {
        auto* bar = new media::assistant::AssistantBar();
        bar_ptr   = bar;

        bar_thread = std::thread([bar, &st, spy_paused, last_spy_wtext, last_spy_mutex
#if defined(FEATURE_STT) && FEATURE_STT
            , stt_muted
#endif
        ] {
            media::assistant::AssistantBarConfig bcfg;
            bcfg.spy_on = true;
            bcfg.stt_on = st.asst_stt;

            media::assistant::AssistantBarCallbacks bcbs;

            bcbs.on_close = [] { media::cli::test_request_cancel(); };

            bcbs.on_spy_toggle = [spy_paused](bool on) {
                spy_paused->store(!on);  // spy_on=true → not paused
                std::cout << "\n[assistant] spy " << (on ? "resumed" : "paused")
                          << "\n" << std::flush;
            };

#if defined(FEATURE_STT) && FEATURE_STT
            bcbs.on_stt_toggle = [stt_muted](bool on) {
                stt_muted->store(!on);  // on=true → not muted
                std::cout << "\n[assistant] mic " << (on ? "unmuted" : "muted")
                          << "\n" << std::flush;
            };
#endif

            // Bar is WS_EX_NOACTIVATE so the foreground window is still the
            // user's application when the screenshot or chat button is clicked.
            bcbs.get_target_hwnd = []() -> HWND {
                return ::GetForegroundWindow();
            };

            bcbs.on_chat_open = [last_spy_wtext, last_spy_mutex] {
                const std::wstring pm_image = get_pm_image_path();

                // Base command
                std::wstring cmd = cmd_quote(pm_image) + L" --ui-preset chat";

                // --src: document file from the window title of the foreground app
                // (e.g. "C:\path\file.txt - Notepad" → "C:\path\file.txt").
                const HWND fg = ::GetForegroundWindow();
                const std::wstring src = get_hwnd_doc_path(fg);
                if (!src.empty()) {
                    cmd += L" --src " + cmd_quote(src);
                    logger::info("[assistant] chat open: src=" + w2u(src));
                } else {
                    logger::info("[assistant] chat open: no document path in window title");
                }

                // --prompt: last selected / value text captured by spy
                std::wstring prompt;
                {
                    std::lock_guard<std::mutex> lk(*last_spy_mutex);
                    prompt = *last_spy_wtext;
                }
                if (!prompt.empty()) {
                    // Truncate to avoid absurdly long command lines (max ~4096 chars)
                    if (prompt.size() > 512) prompt = prompt.substr(0, 512) + L"\u2026";
                    cmd += L" --prompt " + cmd_quote(prompt);
                    logger::info("[assistant] chat open: prompt=" + w2u(prompt));
                }

                logger::info("[assistant] launching: " + w2u(cmd));

                STARTUPINFOW si = {};
                si.cb           = sizeof(si);
                PROCESS_INFORMATION pi = {};
                std::wstring cmd_buf = cmd;  // CreateProcessW may modify the buffer
                if (!::CreateProcessW(nullptr, cmd_buf.data(),
                                      nullptr, nullptr, FALSE,
                                      CREATE_NEW_CONSOLE, nullptr, nullptr,
                                      &si, &pi)) {
                    logger::error("[assistant] chat open: CreateProcessW failed, err="
                                  + std::to_string(::GetLastError()));
                } else {
                    ::CloseHandle(pi.hProcess);
                    ::CloseHandle(pi.hThread);
                }
            };

            bar->run(bcfg, bcbs);
            delete bar;
        });
        std::cout << "[assistant] toolbar started\n" << std::flush;
    }

    // ── STT dictation thread ──────────────────────────────────────────────────
#if defined(FEATURE_STT) && FEATURE_STT
    std::thread stt_thread;
    if (st.asst_stt) {
        stt_thread = std::thread([&st, stt_muted, bar_ptr] {
            run_stt_dictate(st,
                [] { return media::cli::cancel_requested(); },
                stt_muted);
            if (bar_ptr) bar_ptr->set_stt_state(false);
        });
    }
#endif

    // ── Spy loop (main thread) ────────────────────────────────────────────────
    const bool stt_mode =
#if defined(FEATURE_STT) && FEATURE_STT
        st.asst_stt;
#else
        false;
#endif

    run_spy_loop(
        opts,
        [] { return media::cli::cancel_requested(); },
        [&verbose, stt_mode, last_spy_wtext, last_spy_mutex]
        (const media::assistant::FocusSnapshot& snap, bool id_changed, bool cnt_changed)
        {
            // Track the latest meaningful text for the chat-open --prompt
            {
                std::lock_guard<std::mutex> lk(*last_spy_mutex);
                // Prefer selected text; fall back to value text
                const std::wstring& txt = snap.selected_text.empty()
                                          ? snap.value_text
                                          : snap.selected_text;
                if (!txt.empty()) *last_spy_wtext = txt;
            }
            if (stt_mode && !id_changed) return;
            print_snapshot(snap, id_changed, cnt_changed, verbose);
        });

    // ── Shutdown ──────────────────────────────────────────────────────────────
#if defined(FEATURE_STT) && FEATURE_STT
    if (stt_thread.joinable()) stt_thread.join();
#endif
    if (bar_ptr) bar_ptr->request_close();
    if (bar_thread.joinable()) bar_thread.join();

    std::cout << "\n[assistant spy] stopped.\n";
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// CLI registration:  pm-image auto spy [options]
// ═══════════════════════════════════════════════════════════════════════════

void pm_image_register_assistant(CLI::App& app, PmImageCliState& s)
{
    s.assistant_cmd = app.add_subcommand(
        "assistant",
        "AI assistant branch (UIA focus spy, STT dictation, write-back, toolbar UI).");
    s.assistant_cmd->require_subcommand(1);

    // ── computer-use one-shot commands ───────────────────────────────────────
    s.assistant_app_inspect_cmd = s.assistant_cmd->add_subcommand(
        "app-inspect",
        "Inspect desktop applications for computer-use.");
    s.assistant_app_inspect_cmd->require_subcommand(1);
    s.assistant_app_inspect_dump_cmd = s.assistant_app_inspect_cmd->add_subcommand(
        "dump",
        "Dump visible windows and useful UIA elements with screen coordinates.");
    s.assistant_app_inspect_dump_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_inspect_dump_cmd->add_flag("--md", s.asst_md, "Print a compact Markdown tree for humans and LLMs.");
    s.assistant_app_inspect_dump_cmd->add_flag("--probe-cells", s.asst_probe_cells, "Probe visible virtualized cells with ElementFromPoint (slower; auto-enabled for spreadsheet-looking --md targets).");
    s.assistant_app_inspect_dump_cmd->add_flag("--foreground", s.asst_foreground, "Inspect only the foreground window.");
    s.assistant_app_inspect_dump_cmd->add_option("--pid", s.asst_pid, "Inspect windows for a process id.");
    s.assistant_app_inspect_dump_cmd->add_option("--process", s.asst_process, "Case-insensitive process-name substring filter, e.g. notepad.exe.");
    s.assistant_app_inspect_dump_cmd->add_option("--title", s.asst_title, "Case-insensitive title substring filter.");
    s.assistant_app_inspect_dump_cmd->add_option("--controls", s.asst_controls, "Markdown control filter, e.g. button,menuitem,edit,input,text.");
    s.assistant_app_inspect_dump_cmd
        ->add_option("--limit", s.asst_limit, "Maximum useful elements per window.")
        ->default_val(500)
        ->check(CLI::Range(1, 10000));
    s.assistant_app_inspect_dump_cmd
        ->add_option("--text-max-chars", s.asst_text_max_chars, "Maximum characters per Markdown text/value field.")
        ->default_val(4096)
        ->check(CLI::Range(20, 200000));

    s.assistant_app_inspect_screenshot_cmd = s.assistant_app_inspect_cmd->add_subcommand(
        "screenshot",
        "Save a screen crop as JPEG. Use --rect=x,y,w,h or --element from a matching dump.");
    s.assistant_app_inspect_screenshot_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_inspect_screenshot_cmd->add_flag("--foreground", s.asst_foreground, "Resolve --element in the foreground window.");
    s.assistant_app_inspect_screenshot_cmd->add_option("--pid", s.asst_pid, "Resolve --element for a process id.");
    s.assistant_app_inspect_screenshot_cmd->add_option("--process", s.asst_process, "Resolve window/element by process-name substring.");
    s.assistant_app_inspect_screenshot_cmd->add_option("--title", s.asst_title, "Resolve --element by title substring.");
    s.assistant_app_inspect_screenshot_cmd->add_option("--element", s.asst_element, "Element index from app-inspect dump.");
    s.assistant_app_inspect_screenshot_cmd->add_option("--rect", s.asst_rect, "Screen rectangle x,y,w,h.");
    s.assistant_app_inspect_screenshot_cmd
        ->add_option("-o,--output", s.asst_output, "Destination .jpg path.")
        ->required();
    s.assistant_app_inspect_screenshot_cmd
        ->add_option("--quality", s.asst_quality, "JPEG quality 1..100.")
        ->default_val(85)
        ->check(CLI::Range(1, 100));
    s.assistant_app_inspect_screenshot_cmd
        ->add_flag("--no-activate", s.asst_no_activate,
                   "Skip bringing the target window to foreground before capture. "
                   "Default: when a window is resolved by --title/--process/--pid/--foreground, "
                   "it is activated first so the capture matches what the user sees.");

    s.assistant_app_use_cmd = s.assistant_cmd->add_subcommand(
        "app-use",
        "Perform replayable computer-use actions: open apps, click, type, and send hotkeys.");
    s.assistant_app_use_cmd->require_subcommand(1);

    s.assistant_app_use_open_cmd = s.assistant_app_use_cmd->add_subcommand(
        "open-app",
        "Launch an app with optional args/cwd and place its first visible window.");
    s.assistant_app_use_open_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_use_open_cmd->add_option("--exe", s.asst_exe, "Executable path or name.")->required();
    s.assistant_app_use_open_cmd->add_option("--args", s.asst_args, "Raw command-line args passed after --exe.");
    s.assistant_app_use_open_cmd->add_option("--cwd", s.asst_cwd, "Working directory.");
    s.assistant_app_use_open_cmd->add_option("--x", s.asst_x, "Window left coordinate.");
    s.assistant_app_use_open_cmd->add_option("--y", s.asst_y, "Window top coordinate.");
    s.assistant_app_use_open_cmd->add_option("--width", s.asst_w, "Window width.");
    s.assistant_app_use_open_cmd->add_option("--height", s.asst_h, "Window height.");
    s.assistant_app_use_open_cmd
        ->add_option("--wait-ms", s.asst_wait_ms, "Milliseconds to wait for first visible window.")
        ->default_val(1000)
        ->check(CLI::Range(0, 60000));

    s.assistant_app_use_mouse_move_cmd = s.assistant_app_use_cmd->add_subcommand(
        "mouse-move",
        "Move the cursor to a screen coordinate. Use --api-width/--api-height for scaled LLM coordinates.");
    s.assistant_app_use_mouse_move_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_use_mouse_move_cmd->add_option("--x", s.asst_x, "X coordinate.")->required();
    s.assistant_app_use_mouse_move_cmd->add_option("--y", s.asst_y, "Y coordinate.")->required();
    s.assistant_app_use_mouse_move_cmd->add_option("--api-width", s.asst_api_width, "Coordinate-space width from an LLM computer-use API.");
    s.assistant_app_use_mouse_move_cmd->add_option("--api-height", s.asst_api_height, "Coordinate-space height from an LLM computer-use API.");

    s.assistant_app_use_click_cmd = s.assistant_app_use_cmd->add_subcommand(
        "click",
        "Click a screen coordinate. Supports --button, --count, scaled API coords, and --virtual HWND messages.");
    s.assistant_app_use_click_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_use_click_cmd->add_flag("--virtual", s.asst_virtual, "Do not move the physical cursor; post mouse messages.");
    s.assistant_app_use_click_cmd->add_option("--x", s.asst_x, "Screen x coordinate.")->required();
    s.assistant_app_use_click_cmd->add_option("--y", s.asst_y, "Screen y coordinate.")->required();
    s.assistant_app_use_click_cmd
        ->add_option("--button", s.asst_button, "Mouse button: left, right, or middle.")
        ->default_val("left");
    s.assistant_app_use_click_cmd
        ->add_option("--count", s.asst_count, "Click count (2 = double click).")
        ->default_val(1)
        ->check(CLI::Range(1, 10));
    s.assistant_app_use_click_cmd->add_option("--api-width", s.asst_api_width, "Coordinate-space width from an LLM computer-use API.");
    s.assistant_app_use_click_cmd->add_option("--api-height", s.asst_api_height, "Coordinate-space height from an LLM computer-use API.");

    s.assistant_app_use_type_cmd = s.assistant_app_use_cmd->add_subcommand(
        "type",
        "Type UTF-8 text into the currently focused control via Unicode SendInput.");
    s.assistant_app_use_type_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_use_type_cmd->add_option("--text", s.asst_text, "Text to type.")->required();

    s.assistant_app_use_hotkey_cmd = s.assistant_app_use_cmd->add_subcommand(
        "hotkey",
        "Send a hotkey sequence such as ctrl+s, alt+f, or f5.");
    s.assistant_app_use_hotkey_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_use_hotkey_cmd->add_option("--keys", s.asst_keys, "Keys separated by +, comma, or spaces.")->required();

    s.assistant_app_use_key_press_cmd = s.assistant_app_use_cmd->add_subcommand(
        "key-press",
        "Press a single key, hold for --hold-ms, release. Use this when key DURATION matters "
        "(virtual pianos, rhythm games, held-modifier window managers). For chords / shortcuts, "
        "prefer `hotkey` (atomic) and for melodies use `batch` with key-press steps.");
    s.assistant_app_use_key_press_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_use_key_press_cmd->add_option("--key", s.asst_key,
                                                  "Single key name (a-z, 0-9, f1..f12, enter, space, left, up, ...).")->required();
    s.assistant_app_use_key_press_cmd->add_option("--hold-ms", s.asst_hold_ms,
                                                  "How long to hold the key down (ms). FreePiano / similar apps treat this as note sustain.")
        ->default_val(50)
        ->check(CLI::Range(0, 30000));
    s.assistant_app_use_key_press_cmd->add_option("--modifiers", s.asst_keys,
                                                  "Optional modifiers held during the press, separated by +/,/space (e.g. \"ctrl\" or \"ctrl+shift\").");

    s.assistant_app_use_cursor_cmd = s.assistant_app_use_cmd->add_subcommand(
        "cursor-position",
        "Report cursor position and virtual-screen dimensions; optionally include scaled API coordinates.");
    s.assistant_app_use_cursor_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_use_cursor_cmd->add_option("--api-width", s.asst_api_width, "Coordinate-space width from an LLM computer-use API.");
    s.assistant_app_use_cursor_cmd->add_option("--api-height", s.asst_api_height, "Coordinate-space height from an LLM computer-use API.");

    s.assistant_app_use_batch_cmd = s.assistant_app_use_cmd->add_subcommand(
        "batch",
        "Run a JSON app-use action sequence in one process, with per-step delays and wait-element polling.");
    s.assistant_app_use_batch_cmd->add_flag("--json", s.asst_json, "Pretty-print JSON output.");
    s.assistant_app_use_batch_cmd->add_flag("--continue-on-error", s.asst_continue_on_error, "Continue running later steps after a step fails.");
    s.assistant_app_use_batch_cmd
        ->add_option("--file", s.asst_batch_file, "Batch JSON file: an array or {\"steps\":[...]} document.")
        ->required();
    s.assistant_app_use_batch_cmd
        ->add_option("--default-delay-ms", s.asst_wait_ms, "Default delay after each step.")
        ->default_val(50)
        ->check(CLI::Range(0, 60000));

    // ── auto spy ─────────────────────────────────────────────────────────────
    s.assistant_spy_cmd = s.assistant_cmd->add_subcommand(
        "spy",
        "Foreground UIAutomation focus spy: polls the focused element and logs all "
        "available UIA properties + text content (ValuePattern, TextPattern). "
        "Target apps: Notepad, LibreOffice, Chrome. Press Ctrl+C to stop.\n"
        "\n"
        "Targets with special handling in src/win/assistant/:\n"
        "  notepad.exe  — class RichEditD2DPT; ValuePattern + TextPattern both work.\n"
        "  soffice.bin  — class SALFRAME; TextPattern in Writer, clipboard in Calc/Impress.\n"
        "  chrome.exe   — framework 'Chrome'; address bar via ValuePattern, content via TextPattern.\n"
        "  msedge.exe   — same as Chrome.\n"
        "  code.exe     — VSCode Electron; Monaco a11y bridge exposes TextPattern.");

    s.assistant_spy_cmd
        ->add_option("--interval-ms", s.asst_interval_ms,
                     "Poll interval in milliseconds (default 500; minimum 50).")
        ->default_val(500)
        ->check(CLI::Range(50, 60000));

    s.assistant_spy_cmd->add_flag(
        "--no-value", s.asst_no_value,
        "Skip IUIAutomationValuePattern (edit fields, cells, address bars).");
    s.assistant_spy_cmd->add_flag(
        "--no-selection", s.asst_no_selection,
        "Skip IUIAutomationTextPattern selection ranges.");
    s.assistant_spy_cmd->add_flag(
        "--no-text", s.asst_no_text,
        "Skip IUIAutomationTextPattern document range (full buffer).");
    s.assistant_spy_cmd->add_flag(
        "--all", s.asst_log_all,
        "Log every poll tick even when nothing changed (very verbose).");
    s.assistant_spy_cmd->add_flag(
        "--verbose,-v", s.asst_verbose,
        "Print the full text content of each snapshot (up to 50 lines / --text-max-chars).");

    s.assistant_spy_cmd
        ->add_option("--text-max-chars", s.asst_text_max_chars,
                     "Maximum characters to extract from the TextPattern document range "
                     "(default 4096; -1 = no cap — caution: can be very large).")
        ->default_val(4096);

#if defined(FEATURE_STT) && FEATURE_STT
    // ── --stt dictation options ───────────────────────────────────────────────
    s.assistant_spy_cmd->add_flag(
        "--ui", s.asst_ui,
        "Show the assistant toolbar window (48×176 px, topmost, left-edge snap).\n"
        "The toolbar provides mic (STT) and speaker (TTS) toggle buttons.\n"
        "Drag to reposition; position is saved to %APPDATA%\\PolyMech\\pm-image\\assistant-bar.pos.\n"
        "The toolbar can also be launched via the daemon (CTRL+ALT+F5, see daemon.json).");

    s.assistant_spy_cmd->add_flag(
        "--stt", s.asst_stt,
        "Enable live STT dictation: microphone → ElevenLabs Scribe v2 Realtime →\n"
        "write-back to the currently focused UI element via SendInput (KEYEVENTF_UNICODE).\n"
        "Ctrl+C stops both spy and STT. In --stt mode, spy output shows focus changes only.");

    s.assistant_spy_cmd->add_flag(
        "--stt-live,!--no-stt-live", s.asst_stt_live,
        "Stream audio to the STT server as each chunk is captured (default: on).\n"
        "With --no-stt-live, PCM is buffered per utterance and sent in one shot on VAD commit\n"
        "(no partial transcript feedback; may improve accuracy for short phrases).")
        ->default_val(true);

    s.assistant_spy_cmd
        ->add_option("--stt-provider", s.asst_stt_provider,
                     "STT provider name (default: chat settings stt_provider, e.g. \"elevenlabs\").");

    s.assistant_spy_cmd
        ->add_option("--stt-api-key", s.asst_stt_api_key,
                     "API key for the STT provider (default: from app provider settings).");

    s.assistant_spy_cmd
        ->add_option("--stt-silence-ms", s.asst_stt_silence_ms,
                     "VAD silence threshold in ms before auto-committing an utterance (default 1200).")
        ->default_val(1200)
        ->check(CLI::Range(300, 10000));
#endif
}

#else // ── Stub for non-Win32 or FEATURE_ASSISTANT=0 builds ─────────────────

int pm_image_cmd_assistant(CLI::App& /*app*/, PmImageCliState& /*st*/) { return 0; }

void pm_image_register_assistant(CLI::App& /*app*/, PmImageCliState& /*s*/) {}

#endif
