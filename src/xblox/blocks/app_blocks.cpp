// XBlox "App" block family — desktop automation.
//
// Ports every `app_*` computer-use tool from src/llm/tools/computer_use into
// the visual block runtime. Each handler:
//   1. Resolves a target window from { hwnd | pid | title | process | foreground }
//      (most actions auto-activate the window first, matching the agent path).
//   2. Calls the same underlying `media::assistant::app_use|app_inspect|app_batch`
//      libraries the agent uses — there is intentionally no behavioural drift.
//   3. Emits an ExecutionEvent with an "ok" envelope (jsonable data + message),
//      so test fixtures can assert on `event.data.*` exactly as `Tool_ComputerUse`'s
//      `tool_ok` envelope.
//
// Win32-only. On other platforms (or when FEATURE_ASSISTANT is off) the registry
// stays empty and the handlers are no-ops.

#include "blocks/app_blocks.hpp"

#include "logger/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32) && defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "win/assistant/app_batch.hpp"
#include "win/assistant/app_inspect.hpp"
#include "win/assistant/app_use.hpp"

#endif

namespace media::xblox::blocks {
namespace {

// ── small JSON accessors (lenient, never throw) ────────────────────────────
std::string jstr(const nlohmann::json& o, const char* key, std::string fallback = {})
{
    return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::move(fallback);
}

int jint(const nlohmann::json& o, const char* key, int fallback = 0)
{
    if (!o.is_object() || !o.contains(key))
        return fallback;
    const auto& v = o[key];
    if (v.is_number_integer())
        return v.get<int>();
    if (v.is_number_unsigned())
        return static_cast<int>(v.get<std::uint64_t>());
    if (v.is_number_float())
        return static_cast<int>(v.get<double>());
    return fallback;
}

std::uint64_t juint64(const nlohmann::json& o, const char* key, std::uint64_t fallback = 0)
{
    if (!o.is_object() || !o.contains(key))
        return fallback;
    const auto& v = o[key];
    if (v.is_number_unsigned())
        return v.get<std::uint64_t>();
    if (v.is_number_integer())
        return static_cast<std::uint64_t>(v.get<std::int64_t>());
    if (v.is_number_float())
        return static_cast<std::uint64_t>(v.get<double>());
    return fallback;
}

bool jbool(const nlohmann::json& o, const char* key, bool fallback = false)
{
    return o.is_object() && o.contains(key) && o[key].is_boolean() ? o[key].get<bool>() : fallback;
}

// ── event helpers ──────────────────────────────────────────────────────────
//
// Emit a single block event matching the shape used by shell/network blocks.
// `payload` ends up under `data` so harnesses can assert against
// `event.data.tool`, `event.data.x`, etc.
void emit_event(BlockRuntime& runtime,
                const std::string& path,
                const std::string& kind,
                bool ok,
                std::string message,
                nlohmann::json payload,
                const std::string& block_id)
{
    payload["ok"] = ok;
    if (!payload.contains("kind"))
        payload["kind"] = kind;
    ExecutionEvent event;
    event.path = path;
    event.kind = kind;
    event.status = ok ? "ok" : "error";
    event.message = std::move(message);
    event.exit_code = ok ? 0 : 1;
    event.error_code = ok ? 0 : 1;
    event.data = std::move(payload);
    event.block_id = block_id;
    event.type = "app";
    runtime.emit(runtime.user_data, std::move(event));
}

void emit_error(BlockRuntime& runtime,
                const std::string& path,
                const std::string& kind,
                std::string message,
                const std::string& block_id,
                nlohmann::json extra = nlohmann::json::object())
{
    if (!extra.is_object())
        extra = nlohmann::json::object();
    extra["error"] = message;
    emit_event(runtime, path, kind, false, std::move(message), std::move(extra), block_id);
}

// Attach a "useful single value" to the envelope and mirror it to PREVIOUS so
//   { "storeAs": "win" }      → context.win  = <value>
//   next block reads PREVIOUS → also <value>
// Without this, the runtime's `store_block_result_if_requested` would store
// the whole envelope (tool/ok/kind/...) — noisy when chaining.
void attach_result(BlockRuntime& runtime,
                   nlohmann::json& payload,
                   nlohmann::json value)
{
    payload["result"] = value;
    runtime.set_context_value(runtime.user_data, "PREVIOUS", std::move(value));
}

#if defined(_WIN32) && defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT

namespace ab = media::assistant::app_batch;
namespace ai = media::assistant::app_inspect;
namespace au = media::assistant::app_use;

// ── unicode bridges (kept private to this TU; no leakage) ──────────────────
std::wstring u2w(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string w2u(const std::wstring& ws)
{
    if (ws.empty())
        return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

bool contains_ci(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty())
        return true;
    std::wstring h = haystack;
    std::wstring n = needle;
    auto lower = [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); };
    std::transform(h.begin(), h.end(), h.begin(), lower);
    std::transform(n.begin(), n.end(), n.begin(), lower);
    return h.find(n) != std::wstring::npos;
}

// ── JSON shape helpers (mirrors Tool_ComputerUse for envelope parity) ──────
nlohmann::json rect_json(const ai::Rect& r)
{
    return {
        {"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h},
        {"left", r.x}, {"top", r.y}, {"right", r.x + r.w}, {"bottom", r.y + r.h},
        {"center", {{"x", r.x + r.w / 2}, {"y", r.y + r.h / 2}}},
    };
}

nlohmann::json window_meta_json(HWND hwnd)
{
    nlohmann::json out = nlohmann::json::object();
    if (!hwnd || !::IsWindow(hwnd))
        return out;
    wchar_t title[1024] = {};
    ::GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    out["hwnd"] = reinterpret_cast<std::uintptr_t>(hwnd);
    out["title"] = w2u(title);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid)
        out["pid"] = pid;
    RECT r{};
    if (::GetWindowRect(hwnd, &r) && r.right > r.left && r.bottom > r.top) {
        ai::Rect rr{r.left, r.top, r.right - r.left, r.bottom - r.top};
        out["rect"] = rect_json(rr);
    }
    return out;
}

nlohmann::json element_json(const ai::ElementInfo& e)
{
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

// ── target resolution (selectors: hwnd | pid | title | process | foreground)
HWND resolve_target_window(const nlohmann::json& block, std::string& err)
{
    const std::uint64_t hwnd_raw = juint64(block, "hwnd");
    if (hwnd_raw) {
        HWND h = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(hwnd_raw));
        if (::IsWindow(h))
            return h;
        err = "hwnd is not a valid window";
        return nullptr;
    }
    const DWORD pid = static_cast<DWORD>(juint64(block, "pid"));
    const std::wstring title = u2w(jstr(block, "title"));
    const std::wstring process = u2w(jstr(block, "process"));
    const bool foreground = jbool(block, "foreground");

    if (foreground)
        return ::GetForegroundWindow();
    if (!pid && title.empty() && process.empty())
        return nullptr;

    if (pid || !title.empty()) {
        HWND out = nullptr;
        if (au::find_window(pid, nullptr, title, out, err))
            return out;
        if (process.empty())
            return nullptr;
        err.clear();
    }
    ai::Query q;
    q.process_contains = process;
    q.title_contains = title;
    std::vector<ai::WindowDump> windows;
    if (!ai::dump_windows(q, windows, err))
        return nullptr;
    if (windows.empty()) {
        err = "no matching window for selector";
        return nullptr;
    }
    return windows.front().hwnd;
}

// Activate when caller provided any selector. `used` is nullptr if no selector
// was supplied (caller keeps the current foreground).
bool activate_target_if_specified(const nlohmann::json& block, HWND& used, std::string& err)
{
    HWND target = resolve_target_window(block, err);
    if (!target) {
        if (!err.empty())
            return false;
        used = nullptr;
        return true;
    }
    if (!au::activate_window(target, err))
        return false;
    used = target;
    return true;
}

ai::Query query_from_block(const nlohmann::json& block)
{
    ai::Query q;
    q.foreground = jbool(block, "foreground");
    q.process_contains = u2w(jstr(block, "process"));
    q.title_contains = u2w(jstr(block, "title"));
    q.limit = jint(block, "limit", 160);
    q.probe_cells = jbool(block, "probeCells", jbool(block, "probe_cells"));
    return q;
}

// Resolve (x|xw, y|yw) against an optional anchor window rect. Returns false
// if no coords supplied (caller's choice whether that's an error).
bool resolve_xy(const nlohmann::json& block,
                const RECT* anchor,
                const char* x_abs, const char* y_abs,
                const char* x_win, const char* y_win,
                int& out_x, int& out_y,
                std::string& err)
{
    const bool has_abs = block.is_object() && (block.contains(x_abs) || block.contains(y_abs));
    const bool has_win = block.is_object() && (block.contains(x_win) || block.contains(y_win));
    if (!has_abs && !has_win)
        return false;
    out_x = jint(block, x_abs);
    out_y = jint(block, y_abs);
    if (has_win) {
        if (!anchor) {
            err = "window-relative coords need an activated window (set process/title/pid/hwnd)";
            return false;
        }
        if (block.contains(x_win))
            out_x = anchor->left + jint(block, x_win);
        if (block.contains(y_win))
            out_y = anchor->top + jint(block, y_win);
    }
    return true;
}

// ── individual block handlers ──────────────────────────────────────────────

bool app_activate_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    std::string err;
    HWND used = nullptr;
    if (!activate_target_if_specified(block, used, err))
        return (emit_error(runtime, path, "appActivate", "appActivate: " + err, id), true);
    if (!used)
        return (emit_error(runtime, path, "appActivate",
                          "appActivate: a target selector is required (process/title/pid/hwnd/foreground)", id),
                true);
    auto meta = window_meta_json(used);
    nlohmann::json payload{{"tool", "app_activate"}, {"activated", meta}};
    attach_result(runtime, payload, std::move(meta));
    emit_event(runtime, path, "appActivate", true, "window activated", std::move(payload), id);
    return true;
}

bool app_open_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    au::LaunchOptions opts;
    opts.exe = u2w(jstr(block, "exe"));
    if (opts.exe.empty())
        return (emit_error(runtime, path, "appOpen", "appOpen: 'exe' is required", id), true);
    opts.args = u2w(jstr(block, "args"));
    opts.cwd = u2w(jstr(block, "cwd"));
    opts.x = jint(block, "x", 0);
    opts.y = jint(block, "y", 0);
    opts.w = jint(block, "width", 0);
    opts.h = jint(block, "height", 0);
    opts.wait_ms = jint(block, "waitMs", jint(block, "wait_ms", 3000));

    au::LaunchResult res;
    std::string err;
    if (!au::open_app(opts, res, err))
        return (emit_error(runtime, path, "appOpen", "appOpen: " + err, id), true);

    nlohmann::json payload{{"tool", "app_open"}, {"pid", res.pid}};
    nlohmann::json result{{"pid", res.pid}};
    if (res.hwnd) {
        auto meta = window_meta_json(res.hwnd);
        if (meta.contains("hwnd"))   { payload["hwnd"]  = meta["hwnd"];  result["hwnd"]  = meta["hwnd"]; }
        if (meta.contains("title"))  { payload["title"] = meta["title"]; result["title"] = meta["title"]; }
        if (meta.contains("rect"))   { payload["rect"]  = meta["rect"];  result["rect"]  = meta["rect"]; }
    } else {
        payload["note"] = "no main window resolved within waitMs; try appInspectDump to locate it";
    }
    attach_result(runtime, payload, std::move(result));
    emit_event(runtime, path, "appOpen", true, "process launched", std::move(payload), id);
    return true;
}

bool app_close_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    const bool force = jbool(block, "force");
    std::string err;
    HWND target = resolve_target_window(block, err);
    DWORD pid = static_cast<DWORD>(juint64(block, "pid"));
    if (target && pid == 0)
        ::GetWindowThreadProcessId(target, &pid);

    if (!target && !pid)
        return (emit_error(runtime, path, "appClose",
                          "appClose: requires hwnd, pid, process, title, or foreground", id),
                true);
    if (!target && pid && !force && !err.empty())
        return (emit_error(runtime, path, "appClose", "appClose: " + err, id), true);

    if (force) {
        if (!pid)
            return (emit_error(runtime, path, "appClose", "appClose: force requires a resolvable pid", id), true);
        HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!h)
            return (emit_error(runtime, path, "appClose", "appClose: OpenProcess failed", id), true);
        const BOOL ok = ::TerminateProcess(h, 0);
        ::CloseHandle(h);
        if (!ok)
            return (emit_error(runtime, path, "appClose", "appClose: TerminateProcess failed", id), true);
        nlohmann::json payload{{"tool", "app_close"}, {"pid", pid}, {"method", "terminate"}};
        attach_result(runtime, payload, nlohmann::json{{"pid", pid}, {"method", "terminate"}});
        emit_event(runtime, path, "appClose", true, "process terminated", std::move(payload), id);
        return true;
    }
    if (!target)
        return (emit_error(runtime, path, "appClose",
                          "appClose: no window resolved for graceful close (try force=true with pid)", id),
                true);
    if (!::PostMessageW(target, WM_CLOSE, 0, 0))
        return (emit_error(runtime, path, "appClose", "appClose: PostMessage(WM_CLOSE) failed", id), true);
    nlohmann::json payload{{"tool", "app_close"},
                           {"hwnd", reinterpret_cast<std::uintptr_t>(target)},
                           {"method", "wm_close"}};
    nlohmann::json result{{"hwnd", reinterpret_cast<std::uintptr_t>(target)}, {"method", "wm_close"}};
    if (pid) {
        payload["pid"] = pid;
        result["pid"] = pid;
    }
    attach_result(runtime, payload, std::move(result));
    emit_event(runtime, path, "appClose", true, "WM_CLOSE posted", std::move(payload), id);
    return true;
}

// Resolve a screenshot path against the block's own cwd hint or the runtime's
// default. Unlike the LLM tool we don't apply the fs guard — block scripts
// are user-owned, not agent-owned.
std::string resolve_output_path(const std::string& raw, const BlockRuntime& runtime)
{
    if (raw.empty())
        return raw;
    std::error_code ec;
    std::filesystem::path p(raw);
    if (p.is_absolute())
        return std::filesystem::weakly_canonical(p, ec).string();
    std::filesystem::path base = runtime.options.default_cwd.empty()
        ? std::filesystem::current_path(ec)
        : runtime.options.default_cwd;
    if (ec)
        base = std::filesystem::current_path();
    return (base / p).lexically_normal().string();
}

bool parse_rect(const std::string& s, ai::Rect& r)
{
    std::vector<int> nums;
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == 'x' || c == 'X' || std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { nums.push_back(std::stoi(cur)); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
        nums.push_back(std::stoi(cur));
    if (nums.size() != 4)
        return false;
    r = {nums[0], nums[1], nums[2], nums[3]};
    return true;
}

bool app_screenshot_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    const std::string raw_out = jstr(block, "outputPath", jstr(block, "output_path"));
    if (raw_out.empty())
        return (emit_error(runtime, path, "appScreenshot", "appScreenshot: 'outputPath' is required", id), true);
    const std::string out_abs = resolve_output_path(raw_out, runtime);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(out_abs).parent_path(), ec);

    ai::Rect rect{};
    HWND window_capture = nullptr;
    std::string err;
    const std::string rect_s = jstr(block, "rect");
    if (!rect_s.empty()) {
        try {
            if (!parse_rect(rect_s, rect))
                return (emit_error(runtime, path, "appScreenshot", "appScreenshot: rect must be x,y,w,h", id), true);
        } catch (const std::exception& ex) {
            return (emit_error(runtime, path, "appScreenshot",
                              std::string("appScreenshot: invalid rect: ") + ex.what(), id), true);
        }
    } else if (block.contains("elementIndex") || block.contains("element_index")) {
        const int idx = jint(block, "elementIndex", jint(block, "element_index", 0));
        if (!ai::find_element_rect(query_from_block(block), idx, rect, err))
            return (emit_error(runtime, path, "appScreenshot", "appScreenshot: " + err, id), true);
    } else {
        std::vector<ai::WindowDump> windows;
        if (!ai::dump_windows(query_from_block(block), windows, err))
            return (emit_error(runtime, path, "appScreenshot", "appScreenshot: " + err, id), true);
        if (windows.empty())
            return (emit_error(runtime, path, "appScreenshot", "appScreenshot: no matching window", id), true);
        window_capture = windows.front().hwnd;
        rect = windows.front().rect;
    }

    HWND activated = nullptr;
    if (window_capture && jbool(block, "activate", true)) {
        std::string act_err;
        if (!au::activate_window(window_capture, act_err))
            return (emit_error(runtime, path, "appScreenshot", "appScreenshot: " + act_err, id), true);
        activated = window_capture;
        RECT r{};
        if (::GetWindowRect(window_capture, &r) && r.right > r.left && r.bottom > r.top)
            rect = ai::Rect{r.left, r.top, r.right - r.left, r.bottom - r.top};
    }
    const int quality = jint(block, "quality", 85);
    if (window_capture) {
        if (!ai::save_window_jpeg(window_capture, rect, std::filesystem::path(out_abs).wstring(), quality, err))
            return (emit_error(runtime, path, "appScreenshot", "appScreenshot: " + err, id), true);
    } else if (!ai::save_screen_rect_jpeg(rect, std::filesystem::path(out_abs).wstring(), quality, err)) {
        return (emit_error(runtime, path, "appScreenshot", "appScreenshot: " + err, id), true);
    }
    nlohmann::json payload{{"tool", "app_screenshot"},
                           {"outputPath", out_abs},
                           {"capture", window_capture ? "window" : "screen"},
                           {"rect", rect_json(rect)}};
    if (activated)
        payload["activated"] = window_meta_json(activated);
    attach_result(runtime, payload, out_abs);
    emit_event(runtime, path, "appScreenshot", true, "screenshot saved", std::move(payload), id);
    return true;
}

bool app_click_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    std::string err;
    HWND activated = nullptr;
    if (!activate_target_if_specified(block, activated, err))
        return (emit_error(runtime, path, "appClick", "appClick: " + err, id), true);

    RECT anchor{};
    const bool have_anchor = activated && ::GetWindowRect(activated, &anchor);
    int x = 0, y = 0;
    if (!resolve_xy(block, have_anchor ? &anchor : nullptr, "x", "y", "xw", "yw", x, y, err)) {
        if (!err.empty())
            return (emit_error(runtime, path, "appClick", "appClick: " + err, id), true);
        return (emit_error(runtime, path, "appClick",
                          "appClick: 'x'/'y' (screen) or 'xw'/'yw' (window-relative) required", id),
                true);
    }
    const std::string button = jstr(block, "button", "left");
    const int count = jint(block, "count", 1);
    const bool virtual_click = jbool(block, "virtual");
    if (!au::click_point(x, y, u2w(button), count, virtual_click, err))
        return (emit_error(runtime, path, "appClick", "appClick: " + err, id), true);

    nlohmann::json payload{{"tool", "app_click"},
                           {"x", x}, {"y", y},
                           {"button", button}, {"count", count},
                           {"virtual", virtual_click}};
    if (activated)
        payload["activated"] = window_meta_json(activated);
    attach_result(runtime, payload, nlohmann::json{{"x", x}, {"y", y}, {"button", button}, {"count", count}});
    emit_event(runtime, path, "appClick", true, "click delivered", std::move(payload), id);
    return true;
}

bool app_mouse_move_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    std::string err;
    HWND activated = nullptr;
    if (!activate_target_if_specified(block, activated, err))
        return (emit_error(runtime, path, "appMouseMove", "appMouseMove: " + err, id), true);

    RECT anchor{};
    const bool have_anchor = activated && ::GetWindowRect(activated, &anchor);
    int x = 0, y = 0;
    if (!resolve_xy(block, have_anchor ? &anchor : nullptr, "x", "y", "xw", "yw", x, y, err))
        return (emit_error(runtime, path, "appMouseMove",
                          err.empty() ? std::string("appMouseMove: coords required") : "appMouseMove: " + err, id),
                true);
    const int duration_ms = jint(block, "durationMs", jint(block, "duration_ms", -1));
    const bool smooth = jbool(block, "smooth", duration_ms != 0);
    if (smooth ? !au::smooth_move(x, y, duration_ms, err) : !au::move_mouse(x, y, err))
        return (emit_error(runtime, path, "appMouseMove", "appMouseMove: " + err, id), true);

    nlohmann::json payload{{"tool", "app_mouse_move"}, {"x", x}, {"y", y},
                           {"smooth", smooth}, {"durationMs", duration_ms}};
    if (activated)
        payload["activated"] = window_meta_json(activated);
    attach_result(runtime, payload, nlohmann::json{{"x", x}, {"y", y}});
    emit_event(runtime, path, "appMouseMove", true, "cursor moved", std::move(payload), id);
    return true;
}

bool app_type_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    const std::string text = jstr(block, "text");
    if (text.empty())
        return (emit_error(runtime, path, "appType", "appType: 'text' is required", id), true);
    HWND activated = nullptr;
    std::string err;
    if (!activate_target_if_specified(block, activated, err))
        return (emit_error(runtime, path, "appType", "appType: " + err, id), true);
    if (!au::type_text(u2w(text), err))
        return (emit_error(runtime, path, "appType", "appType: " + err, id), true);
    nlohmann::json payload{{"tool", "app_type"}, {"chars", static_cast<int>(text.size())}};
    if (activated)
        payload["activated"] = window_meta_json(activated);
    attach_result(runtime, payload, text);
    emit_event(runtime, path, "appType", true, "text typed", std::move(payload), id);
    return true;
}

bool app_hotkey_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    const std::string keys = jstr(block, "keys");
    if (keys.empty())
        return (emit_error(runtime, path, "appHotkey", "appHotkey: 'keys' is required", id), true);
    HWND activated = nullptr;
    std::string err;
    if (!activate_target_if_specified(block, activated, err))
        return (emit_error(runtime, path, "appHotkey", "appHotkey: " + err, id), true);
    const auto parts = au::split_keys(u2w(keys));
    if (parts.empty())
        return (emit_error(runtime, path, "appHotkey", "appHotkey: keys did not parse", id), true);
    if (!au::send_hotkey(parts, err))
        return (emit_error(runtime, path, "appHotkey", "appHotkey: " + err, id), true);
    nlohmann::json payload{{"tool", "app_hotkey"}, {"keys", keys}};
    if (activated)
        payload["activated"] = window_meta_json(activated);
    attach_result(runtime, payload, keys);
    emit_event(runtime, path, "appHotkey", true, "hotkey sent", std::move(payload), id);
    return true;
}

bool app_key_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    const std::string key = jstr(block, "key");
    if (key.empty())
        return (emit_error(runtime, path, "appKey", "appKey: 'key' is required", id), true);
    HWND activated = nullptr;
    std::string err;
    if (!activate_target_if_specified(block, activated, err))
        return (emit_error(runtime, path, "appKey", "appKey: " + err, id), true);
    std::vector<std::wstring> mods;
    if (block.contains("modifiers") && block["modifiers"].is_array()) {
        for (const auto& m : block["modifiers"])
            if (m.is_string())
                mods.push_back(u2w(m.get<std::string>()));
    }
    const int hold_ms = jint(block, "holdMs", jint(block, "hold_ms", 0));
    if (!au::press_key(u2w(key), mods, hold_ms, err))
        return (emit_error(runtime, path, "appKey", "appKey: " + err, id), true);
    nlohmann::json payload{{"tool", "app_key"}, {"key", key}, {"holdMs", hold_ms}};
    if (!mods.empty()) {
        nlohmann::json mods_json = nlohmann::json::array();
        for (const auto& m : mods)
            mods_json.push_back(w2u(m));
        payload["modifiers"] = std::move(mods_json);
    }
    if (activated)
        payload["activated"] = window_meta_json(activated);
    attach_result(runtime, payload, key);
    emit_event(runtime, path, "appKey", true, "key pressed", std::move(payload), id);
    return true;
}

bool app_drag_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    HWND activated = nullptr;
    std::string err;
    if (!activate_target_if_specified(block, activated, err))
        return (emit_error(runtime, path, "appDrag", "appDrag: " + err, id), true);

    RECT anchor{};
    const bool have_anchor = activated && ::GetWindowRect(activated, &anchor);
    const RECT* anchor_ptr = have_anchor ? &anchor : nullptr;

    std::vector<au::Point> path_pts;
    const char* mode = nullptr;
    if (block.contains("arc") && block["arc"].is_object()) {
        const auto& a = block["arc"];
        int cx = 0, cy = 0;
        if (!resolve_xy(a, anchor_ptr, "cx", "cy", "cxw", "cyw", cx, cy, err))
            return (emit_error(runtime, path, "appDrag",
                              err.empty() ? std::string("appDrag: arc requires center (cx/cy or cxw/cyw)")
                                          : "appDrag: " + err, id),
                    true);
        const double radius = static_cast<double>(jint(a, "radius"));
        if (radius < 1.0)
            return (emit_error(runtime, path, "appDrag", "appDrag: arc 'radius' must be > 0", id), true);
        const double start_deg = static_cast<double>(jint(a, "startDeg", jint(a, "start_deg", 0)));
        const double end_deg = static_cast<double>(jint(a, "endDeg", jint(a, "end_deg", 360)));
        const double sweep = end_deg - start_deg;
        if (std::abs(sweep) < 0.5)
            return (emit_error(runtime, path, "appDrag", "appDrag: arc sweep is zero", id), true);
        int segments = jint(a, "segments");
        if (segments <= 0)
            segments = std::max(8, std::min(120, static_cast<int>(std::abs(sweep) / 8.0)));
        if (segments > 360)
            segments = 360;
        constexpr double kPi = 3.14159265358979323846;
        path_pts.reserve(static_cast<std::size_t>(segments + 1));
        for (int i = 0; i <= segments; ++i) {
            const double t = start_deg + (sweep * i) / segments;
            const double rad = (t * kPi) / 180.0;
            path_pts.push_back({cx + static_cast<int>(std::cos(rad) * radius),
                                cy + static_cast<int>(std::sin(rad) * radius)});
        }
        mode = "arc";
    } else if (block.contains("path") && block["path"].is_array()) {
        for (const auto& pt : block["path"]) {
            if (!pt.is_object())
                continue;
            int px = 0, py = 0;
            if (!resolve_xy(pt, anchor_ptr, "x", "y", "xw", "yw", px, py, err))
                return (emit_error(runtime, path, "appDrag",
                                  "appDrag: path point requires x/y or xw/yw" + (err.empty() ? "" : " (" + err + ")"),
                                  id),
                        true);
            path_pts.push_back({px, py});
        }
        if (path_pts.size() < 2)
            return (emit_error(runtime, path, "appDrag", "appDrag: 'path' needs >= 2 points", id), true);
        mode = "path";
    } else {
        int x1 = 0, y1 = 0;
        if (!resolve_xy(block, anchor_ptr, "x", "y", "xw", "yw", x1, y1, err))
            return (emit_error(runtime, path, "appDrag",
                              "appDrag: start point required (x,y or xw,yw)" + (err.empty() ? "" : " (" + err + ")"),
                              id),
                    true);
        int x2 = std::numeric_limits<int>::min();
        int y2 = std::numeric_limits<int>::min();
        if (block.contains("toX") || block.contains("toY") || block.contains("toXw") || block.contains("toYw") ||
            block.contains("to_x") || block.contains("to_y")) {
            nlohmann::json end_obj = nlohmann::json::object();
            if (block.contains("toX"))   end_obj["x"]  = block["toX"];
            if (block.contains("toY"))   end_obj["y"]  = block["toY"];
            if (block.contains("to_x"))  end_obj["x"]  = block["to_x"];
            if (block.contains("to_y"))  end_obj["y"]  = block["to_y"];
            if (block.contains("toXw"))  end_obj["xw"] = block["toXw"];
            if (block.contains("toYw"))  end_obj["yw"] = block["toYw"];
            if (!resolve_xy(end_obj, anchor_ptr, "x", "y", "xw", "yw", x2, y2, err))
                return (emit_error(runtime, path, "appDrag", "appDrag: end point invalid", id), true);
        } else if (block.contains("dx") || block.contains("dy")) {
            x2 = x1 + jint(block, "dx");
            y2 = y1 + jint(block, "dy");
        } else {
            return (emit_error(runtime, path, "appDrag",
                              "appDrag: end point required (toX/toY, toXw/toYw, dx/dy, or arc/path)", id),
                    true);
        }
        if (x2 == x1 && y2 == y1)
            return (emit_error(runtime, path, "appDrag", "appDrag: end point equals start", id), true);
        path_pts = {{x1, y1}, {x2, y2}};
        mode = "linear";
    }

    const std::string button = jstr(block, "button", "left");
    const int duration_ms = jint(block, "durationMs", jint(block, "duration_ms", 250));
    if (duration_ms < 0 || duration_ms > 30000)
        return (emit_error(runtime, path, "appDrag", "appDrag: 'durationMs' must be in [0,30000]", id), true);
    if (!au::drag_path(path_pts, u2w(button), duration_ms, err))
        return (emit_error(runtime, path, "appDrag", "appDrag: " + err, id), true);

    nlohmann::json from_pt{{"x", path_pts.front().x}, {"y", path_pts.front().y}};
    nlohmann::json to_pt  {{"x", path_pts.back().x},  {"y", path_pts.back().y}};
    nlohmann::json payload{
        {"tool", "app_drag"},
        {"mode", mode},
        {"from", from_pt},
        {"to", to_pt},
        {"points", static_cast<int>(path_pts.size())},
        {"button", button},
        {"durationMs", duration_ms},
    };
    if (activated)
        payload["activated"] = window_meta_json(activated);
    attach_result(runtime, payload, nlohmann::json{{"from", from_pt}, {"to", to_pt}, {"mode", mode}});
    emit_event(runtime, path, "appDrag", true, "drag complete", std::move(payload), id);
    return true;
}

bool app_scroll_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    HWND activated = nullptr;
    std::string err;
    if (!activate_target_if_specified(block, activated, err))
        return (emit_error(runtime, path, "appScroll", "appScroll: " + err, id), true);

    RECT anchor{};
    const bool have_anchor = activated && ::GetWindowRect(activated, &anchor);
    // If caller provides target coords, move the cursor there first — wheel
    // events go to whichever window is under the cursor.
    int tx = 0, ty = 0;
    if (resolve_xy(block, have_anchor ? &anchor : nullptr, "x", "y", "xw", "yw", tx, ty, err)) {
        if (!au::smooth_move(tx, ty, jint(block, "moveDurationMs", -1), err))
            return (emit_error(runtime, path, "appScroll", "appScroll: " + err, id), true);
    } else if (!err.empty()) {
        return (emit_error(runtime, path, "appScroll", "appScroll: " + err, id), true);
    }
    const int clicks = jint(block, "clicks", 0);
    if (clicks == 0)
        return (emit_error(runtime, path, "appScroll", "appScroll: 'clicks' must be non-zero", id), true);
    const std::string axis = jstr(block, "axis", "vertical");
    const bool horizontal = axis == "horizontal" || axis == "h" || axis == "x";
    if (!au::scroll_wheel(clicks, horizontal, err))
        return (emit_error(runtime, path, "appScroll", "appScroll: " + err, id), true);

    nlohmann::json payload{{"tool", "app_scroll"}, {"clicks", clicks},
                           {"axis", horizontal ? "horizontal" : "vertical"}};
    if (activated)
        payload["activated"] = window_meta_json(activated);
    attach_result(runtime, payload,
                  nlohmann::json{{"clicks", clicks}, {"axis", horizontal ? "horizontal" : "vertical"}});
    emit_event(runtime, path, "appScroll", true, "scroll complete", std::move(payload), id);
    return true;
}

bool app_batch_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    nlohmann::json doc;
    if (block.contains("steps") && block["steps"].is_array()) {
        doc = nlohmann::json{{"steps", block["steps"]}};
    } else if (block.contains("actions") && block["actions"].is_array()) {
        doc = nlohmann::json{{"steps", block["actions"]}};
    } else {
        return (emit_error(runtime, path, "appBatch",
                          "appBatch: 'steps' (or 'actions') array is required", id),
                true);
    }
    for (auto& step : doc["steps"]) {
        if (step.is_object() && !step.contains("action") && step.contains("type") && step["type"].is_string())
            step["action"] = step["type"];
    }
    ab::RunOptions opts;
    opts.default_delay_ms = jint(block, "defaultDelayMs", jint(block, "default_delay_ms", 50));
    opts.default_wait_timeout_ms = jint(block, "defaultWaitTimeoutMs",
                                        jint(block, "default_wait_timeout_ms", 5000));
    opts.default_wait_interval_ms = jint(block, "defaultWaitIntervalMs",
                                         jint(block, "default_wait_interval_ms", 100));
    opts.stop_on_error = !jbool(block, "continueOnError", jbool(block, "continue_on_error", false));

    nlohmann::json report;
    std::string err;
    const bool ok = ab::run_json_batch(doc, opts, report, err);
    auto steps = report.value("steps", nlohmann::json::array());
    nlohmann::json payload{
        {"tool", "app_batch"},
        {"steps", steps},
        {"allOk", ok},
    };
    if (!err.empty())
        payload["error"] = err;
    if (!ok) {
        for (const auto& s : payload["steps"]) {
            if (!s.value("ok", true)) { payload["firstFailure"] = s; break; }
        }
    }
    attach_result(runtime, payload, nlohmann::json{{"allOk", ok}, {"steps", std::move(steps)}});
    emit_event(runtime, path, "appBatch", ok,
               ok ? "batch complete" : (err.empty() ? "batch had failures" : err),
               std::move(payload), id);
    return true;
}

bool app_inspect_dump_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    std::vector<ai::WindowDump> windows;
    std::string err;
    if (!ai::dump_windows(query_from_block(block), windows, err))
        return (emit_error(runtime, path, "appInspectDump", "appInspectDump: " + err, id), true);
    const std::string fmt = jstr(block, "format", "md");
    nlohmann::json payload{{"tool", "app_inspect_dump"}, {"windowCount", static_cast<int>(windows.size())}};
    if (fmt == "json") {
        nlohmann::json windows_json = nlohmann::json::array();
        for (const auto& w : windows) {
            nlohmann::json jw = {{"pid", w.pid},
                                 {"hwnd", reinterpret_cast<std::uintptr_t>(w.hwnd)},
                                 {"process", w2u(w.process)},
                                 {"title", w2u(w.title)},
                                 {"rect", rect_json(w.rect)},
                                 {"elements", nlohmann::json::array()}};
            for (const auto& e : w.elements)
                jw["elements"].push_back(element_json(e));
            windows_json.push_back(std::move(jw));
        }
        payload["windows"] = windows_json;
        attach_result(runtime, payload, std::move(windows_json));
    } else {
        ai::MarkdownOptions mo;
        mo.control_types_csv = u2w(jstr(block, "controls", "buttons,menus,editable"));
        mo.text_max_chars = jint(block, "textMaxChars", jint(block, "text_max_chars", 1200));
        std::string md = ai::dump_windows_markdown(windows, mo);
        payload["markdown"] = md;
        attach_result(runtime, payload, std::move(md));
    }
    emit_event(runtime, path, "appInspectDump", true, "dump complete", std::move(payload), id);
    return true;
}

bool app_inspect_find_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    std::vector<ai::WindowDump> windows;
    std::string err;
    if (!ai::dump_windows(query_from_block(block), windows, err))
        return (emit_error(runtime, path, "appInspectFind", "appInspectFind: " + err, id), true);

    const std::wstring name = u2w(jstr(block, "name"));
    const std::wstring value = u2w(jstr(block, "value"));
    const std::wstring automation_id = u2w(jstr(block, "automationId"));
    const std::wstring class_name = u2w(jstr(block, "className"));

    nlohmann::json matches_json = nlohmann::json::array();
    int matched = 0;
    for (const auto& w : windows) {
        for (const auto& e : w.elements) {
            if (!contains_ci(e.name, name))
                continue;
            if (!contains_ci(e.value, value))
                continue;
            if (!contains_ci(e.automation_id, automation_id))
                continue;
            if (!contains_ci(e.class_name, class_name))
                continue;
            matches_json.push_back(element_json(e));
            ++matched;
        }
    }
    nlohmann::json payload{{"tool", "app_inspect_find"}, {"count", matched}, {"matches", matches_json}};
    const int nth = jint(block, "nth", -1);
    nlohmann::json result;
    if (nth >= 0 && nth < matched) {
        payload["selected"] = matches_json[static_cast<std::size_t>(nth)];
        result = matches_json[static_cast<std::size_t>(nth)];
    } else {
        result = std::move(matches_json);
    }
    attach_result(runtime, payload, std::move(result));
    emit_event(runtime, path, "appInspectFind", true, "find complete", std::move(payload), id);
    return true;
}

#else // !(_WIN32 && FEATURE_ASSISTANT)

// Stub handler: emits a clear error so cross-platform fixtures fail fast.
bool app_unavailable_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string id = jstr(block, "id");
    const std::string kind = jstr(block, "kind");
    emit_error(runtime, path, kind.empty() ? std::string("app") : kind,
               "app.* blocks require Windows + FEATURE_ASSISTANT", id);
    return true;
}

#endif // _WIN32 && FEATURE_ASSISTANT

struct AppBlockSpec {
    const char* kind;
    const char* label;
    const char* description;
    BlockHandler handler;
    nlohmann::json (*default_block)();
    nlohmann::json (*params)();
};

// Runtime flags applied to every block by `block_run_flags()`. We surface them
// on every app block's param list so the palette / `xblox info --json` show
// them as first-class options (the runtime handles them — we don't read them
// in the handlers).
nlohmann::json inherited_runtime_params()
{
    return nlohmann::json::array({
        {{"name", "enabled"}, {"type", "boolean"}, {"default", true},
         {"description", "Skip this block when false."}},
        {{"name", "continueOnError"}, {"type", "boolean"}, {"default", false},
         {"description", "If this block errors, the runtime still runs the next sibling."}},
        {{"name", "onError"}, {"type", "string"},
         {"description", "'continue' | 'abort' (alias for continueOnError)."}},
        {{"name", "abortOnError"}, {"type", "boolean"},
         {"description", "Force-abort the surrounding block list on error (default for app blocks)."}},
        {{"name", "background"}, {"type", "boolean"}, {"default", false},
         {"description", "Run the block in a detached thread; the parent loop does not wait."}},
        {{"name", "storeAs"}, {"type", "string"},
         {"description", "Bind this block's 'result' field to a context variable for later blocks."}},
    });
}

// Append the inherited runtime params to a block-specific param list.
nlohmann::json with_inherited(nlohmann::json block_params)
{
    if (!block_params.is_array())
        block_params = nlohmann::json::array();
    for (const auto& p : inherited_runtime_params())
        block_params.push_back(p);
    return block_params;
}

#if defined(_WIN32) && defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT

nlohmann::json app_activate_default()
{
    return {{"kind", "appActivate"}, {"title", "Notepad"}};
}
nlohmann::json app_activate_params()
{
    return nlohmann::json::array({
        {{"name", "title"}, {"type", "string"}, {"description", "Window title substring"}},
        {{"name", "process"}, {"type", "string"}, {"description", "Process-name substring"}},
        {{"name", "pid"}, {"type", "integer"}, {"description", "Exact PID"}},
        {{"name", "hwnd"}, {"type", "integer"}, {"description", "Exact HWND"}},
        {{"name", "foreground"}, {"type", "boolean"}, {"description", "Target the current foreground window"}},
    });
}

nlohmann::json app_open_default()
{
    return {{"kind", "appOpen"}, {"exe", "notepad.exe"}, {"waitMs", 3000}};
}
nlohmann::json app_open_params()
{
    return nlohmann::json::array({
        {{"name", "exe"}, {"type", "string"}, {"required", true}},
        {{"name", "args"}, {"type", "string"}},
        {{"name", "cwd"}, {"type", "string"}},
        {{"name", "x"}, {"type", "integer"}},
        {{"name", "y"}, {"type", "integer"}},
        {{"name", "width"}, {"type", "integer"}},
        {{"name", "height"}, {"type", "integer"}},
        {{"name", "waitMs"}, {"type", "integer"}, {"default", 3000}},
    });
}

nlohmann::json app_close_default()
{
    return {{"kind", "appClose"}, {"title", "Notepad"}, {"force", false}};
}
nlohmann::json app_close_params()
{
    return nlohmann::json::array({
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "pid"}, {"type", "integer"}},
        {{"name", "hwnd"}, {"type", "integer"}},
        {{"name", "force"}, {"type", "boolean"}, {"default", false}, {"description", "TerminateProcess instead of WM_CLOSE"}},
    });
}

nlohmann::json app_screenshot_default()
{
    return {{"kind", "appScreenshot"}, {"title", "Notepad"}, {"outputPath", "out/shot.jpg"}, {"quality", 85}};
}
nlohmann::json app_screenshot_params()
{
    return nlohmann::json::array({
        {{"name", "outputPath"}, {"type", "string"}, {"required", true}},
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "foreground"}, {"type", "boolean"}},
        {{"name", "rect"}, {"type", "string"}, {"description", "Optional 'x,y,w,h' screen rect"}},
        {{"name", "elementIndex"}, {"type", "integer"}},
        {{"name", "quality"}, {"type", "integer"}, {"default", 85}},
        {{"name", "activate"}, {"type", "boolean"}, {"default", true}},
    });
}

nlohmann::json app_click_default()
{
    return {{"kind", "appClick"}, {"title", "Notepad"}, {"xw", 100}, {"yw", 100}, {"button", "left"}, {"count", 1}};
}
nlohmann::json app_click_params()
{
    return nlohmann::json::array({
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "x"}, {"type", "integer"}, {"description", "Absolute screen X"}},
        {{"name", "y"}, {"type", "integer"}},
        {{"name", "xw"}, {"type", "integer"}, {"description", "Window-relative X (preferred)"}},
        {{"name", "yw"}, {"type", "integer"}},
        {{"name", "button"}, {"type", "string"}, {"default", "left"}},
        {{"name", "count"}, {"type", "integer"}, {"default", 1}},
        {{"name", "virtual"}, {"type", "boolean"}, {"default", false}, {"description", "PostMessage click; for background apps"}},
    });
}

nlohmann::json app_mouse_move_default()
{
    return {{"kind", "appMouseMove"}, {"title", "Notepad"}, {"xw", 200}, {"yw", 200}, {"smooth", true}};
}
nlohmann::json app_mouse_move_params()
{
    return nlohmann::json::array({
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "x"}, {"type", "integer"}},
        {{"name", "y"}, {"type", "integer"}},
        {{"name", "xw"}, {"type", "integer"}},
        {{"name", "yw"}, {"type", "integer"}},
        {{"name", "smooth"}, {"type", "boolean"}, {"default", true}},
        {{"name", "durationMs"}, {"type", "integer"}, {"default", -1}, {"description", "-1 = auto from distance, 0 = teleport"}},
    });
}

nlohmann::json app_type_default()
{
    return {{"kind", "appType"}, {"title", "Notepad"}, {"text", "Hello"}};
}
nlohmann::json app_type_params()
{
    return nlohmann::json::array({
        {{"name", "text"}, {"type", "string"}, {"required", true}},
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "foreground"}, {"type", "boolean"}},
    });
}

nlohmann::json app_hotkey_default()
{
    return {{"kind", "appHotkey"}, {"title", "Notepad"}, {"keys", "ctrl+s"}};
}
nlohmann::json app_hotkey_params()
{
    return nlohmann::json::array({
        {{"name", "keys"}, {"type", "string"}, {"required", true}, {"description", "Combo e.g. ctrl+shift+t"}},
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "foreground"}, {"type", "boolean"}},
    });
}

nlohmann::json app_key_default()
{
    return {{"kind", "appKey"}, {"key", "a"}, {"holdMs", 100}};
}
nlohmann::json app_key_params()
{
    return nlohmann::json::array({
        {{"name", "key"}, {"type", "string"}, {"required", true}},
        {{"name", "modifiers"}, {"type", "string[]"}, {"description", "ctrl/shift/alt/win"}},
        {{"name", "holdMs"}, {"type", "integer"}, {"default", 0}, {"description", "Note-sustain for piano-style apps"}},
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "foreground"}, {"type", "boolean"}},
    });
}

nlohmann::json app_drag_default()
{
    return {{"kind", "appDrag"}, {"title", "Notepad"}, {"xw", 100}, {"yw", 100},
            {"toXw", 300}, {"toYw", 200}, {"durationMs", 250}};
}
nlohmann::json app_drag_params()
{
    return nlohmann::json::array({
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "x"}, {"type", "integer"}, {"description", "Start (screen)"}},
        {{"name", "y"}, {"type", "integer"}},
        {{"name", "xw"}, {"type", "integer"}, {"description", "Start (window-relative)"}},
        {{"name", "yw"}, {"type", "integer"}},
        {{"name", "toX"}, {"type", "integer"}},
        {{"name", "toY"}, {"type", "integer"}},
        {{"name", "toXw"}, {"type", "integer"}},
        {{"name", "toYw"}, {"type", "integer"}},
        {{"name", "dx"}, {"type", "integer"}},
        {{"name", "dy"}, {"type", "integer"}},
        {{"name", "path"}, {"type", "json"}, {"description", "Polyline: [{x,y}|{xw,yw}, …]"}},
        {{"name", "arc"}, {"type", "json"}, {"description", "{cx,cy,radius,startDeg,endDeg,segments}"}},
        {{"name", "button"}, {"type", "string"}, {"default", "left"}},
        {{"name", "durationMs"}, {"type", "integer"}, {"default", 250}},
    });
}

nlohmann::json app_scroll_default()
{
    return {{"kind", "appScroll"}, {"title", "Notepad"}, {"clicks", -3}};
}
nlohmann::json app_scroll_params()
{
    return nlohmann::json::array({
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "clicks"}, {"type", "integer"}, {"required", true}, {"description", "Positive = up/right"}},
        {{"name", "axis"}, {"type", "string"}, {"default", "vertical"}, {"description", "vertical | horizontal"}},
        {{"name", "x"}, {"type", "integer"}, {"description", "Pre-target cursor X (optional)"}},
        {{"name", "y"}, {"type", "integer"}},
        {{"name", "xw"}, {"type", "integer"}},
        {{"name", "yw"}, {"type", "integer"}},
    });
}

nlohmann::json app_batch_default()
{
    return {{"kind", "appBatch"},
            {"defaultDelayMs", 50},
            {"steps", nlohmann::json::array({
                {{"action", "activate"}, {"title", "Notepad"}},
                {{"action", "type"}, {"text", "Hello"}},
            })}};
}
nlohmann::json app_batch_params()
{
    return nlohmann::json::array({
        {{"name", "steps"}, {"type", "json"}, {"required", true}, {"description", "Array of {action, …}"}},
        {{"name", "defaultDelayMs"}, {"type", "integer"}, {"default", 50}},
        {{"name", "defaultWaitTimeoutMs"}, {"type", "integer"}, {"default", 5000}},
        {{"name", "defaultWaitIntervalMs"}, {"type", "integer"}, {"default", 100}},
        {{"name", "continueOnError"}, {"type", "boolean"}, {"default", false}},
    });
}

nlohmann::json app_inspect_dump_default()
{
    return {{"kind", "appInspectDump"}, {"title", "Notepad"}, {"format", "md"}};
}
nlohmann::json app_inspect_dump_params()
{
    return nlohmann::json::array({
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "foreground"}, {"type", "boolean"}},
        {{"name", "format"}, {"type", "string"}, {"default", "md"}, {"description", "md | json"}},
        {{"name", "limit"}, {"type", "integer"}, {"default", 160}},
        {{"name", "controls"}, {"type", "string"}, {"default", "buttons,menus,editable"}},
        {{"name", "textMaxChars"}, {"type", "integer"}, {"default", 1200}},
        {{"name", "probeCells"}, {"type", "boolean"}, {"default", false}},
        {{"name", "storeAs"}, {"type", "string"}, {"description", "Bind the result to this context var"}},
    });
}

nlohmann::json app_inspect_find_default()
{
    return {{"kind", "appInspectFind"}, {"title", "Notepad"}, {"name", "File"}};
}
nlohmann::json app_inspect_find_params()
{
    return nlohmann::json::array({
        {{"name", "title"}, {"type", "string"}},
        {{"name", "process"}, {"type", "string"}},
        {{"name", "foreground"}, {"type", "boolean"}},
        {{"name", "name"}, {"type", "string"}},
        {{"name", "value"}, {"type", "string"}},
        {{"name", "automationId"}, {"type", "string"}},
        {{"name", "className"}, {"type", "string"}},
        {{"name", "nth"}, {"type", "integer"}, {"default", -1}},
        {{"name", "storeAs"}, {"type", "string"}},
    });
}

const std::vector<AppBlockSpec>& app_block_specs()
{
    static const std::vector<AppBlockSpec> specs = {
        {"appActivate",     "App Activate",      "Bring a target window to the foreground.",
            app_activate_block,     app_activate_default,     app_activate_params},
        {"appOpen",         "App Open",          "Launch an executable and (optionally) wait for its window.",
            app_open_block,         app_open_default,         app_open_params},
        {"appClose",        "App Close",         "Send WM_CLOSE (or TerminateProcess with force=true).",
            app_close_block,        app_close_default,        app_close_params},
        {"appScreenshot",   "App Screenshot",    "Capture a window / element / explicit rect to a JPEG file.",
            app_screenshot_block,   app_screenshot_default,   app_screenshot_params},
        {"appClick",        "App Click",         "Click at (x,y) or (xw,yw) on a target window.",
            app_click_block,        app_click_default,        app_click_params},
        {"appMouseMove",    "App Mouse Move",    "Smoothly move the cursor (auto-paced) or teleport.",
            app_mouse_move_block,   app_mouse_move_default,   app_mouse_move_params},
        {"appType",         "App Type",          "Type a string into the activated window.",
            app_type_block,         app_type_default,         app_type_params},
        {"appHotkey",       "App Hotkey",        "Send a key combination (e.g. ctrl+shift+t).",
            app_hotkey_block,       app_hotkey_default,       app_hotkey_params},
        {"appKey",          "App Key",           "Press one key with optional modifiers and hold duration.",
            app_key_block,          app_key_default,          app_key_params},
        {"appDrag",         "App Drag",          "Drag linear / polyline / arc with smooth pacing.",
            app_drag_block,         app_drag_default,         app_drag_params},
        {"appScroll",       "App Scroll",        "Wheel scroll (vertical or horizontal) at an optional point.",
            app_scroll_block,       app_scroll_default,       app_scroll_params},
        {"appBatch",        "App Batch",         "Run a sequence of app_batch actions in one block.",
            app_batch_block,        app_batch_default,        app_batch_params},
        {"appInspectDump",  "App Inspect Dump",  "Dump UI Automation tree of matching windows.",
            app_inspect_dump_block, app_inspect_dump_default, app_inspect_dump_params},
        {"appInspectFind",  "App Inspect Find",  "Filter UIA elements by name/value/automationId/className.",
            app_inspect_find_block, app_inspect_find_default, app_inspect_find_params},
    };
    return specs;
}

#else

const std::vector<AppBlockSpec>& app_block_specs()
{
    static const std::vector<AppBlockSpec> specs; // empty
    return specs;
}

#endif

} // namespace

BlockHandler app_block_handler(const std::string& kind)
{
    for (const auto& spec : app_block_specs()) {
        if (kind == spec.kind)
            return spec.handler;
    }
    return nullptr;
}

void register_app_blocks(BlockRegistry& registry)
{
    for (const auto& spec : app_block_specs()) {
        nlohmann::json params = spec.params ? spec.params() : nlohmann::json::array();
        registry[spec.kind] = block_descriptor(
            spec.kind,
            spec.handler,
            spec.label,
            "App",
            spec.description,
            spec.default_block ? spec.default_block() : nlohmann::json::object(),
            with_inherited(std::move(params)));
    }
}

} // namespace media::xblox::blocks
