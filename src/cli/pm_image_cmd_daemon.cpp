#include "pm_image_cmd_daemon.hpp"

#include "constants.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include "win/settings_store.hpp"
#endif

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string ascii_lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

#if defined(_WIN32)

constexpr const wchar_t* k_run_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* k_run_value = L"PolyMech pm-image daemon";
constexpr const wchar_t* k_mutex_name = L"Local\\PolyMech_pm-image_daemon_mutex";
constexpr const wchar_t* k_stop_event_name = L"Local\\PolyMech_pm-image_daemon_stop";
constexpr const wchar_t* k_tray_window_class = L"PolyMechPmImageDaemonTrayWindow";
constexpr UINT k_tray_callback_msg = WM_APP + 42;
constexpr UINT k_taskbar_created_msg = 0x8000;
constexpr UINT_PTR k_tray_icon_id = 1;
constexpr UINT k_tray_cmd_open_chat = 1001;
constexpr UINT k_tray_cmd_open_viewer = 1002;
constexpr UINT k_tray_cmd_open_main = 1003;
constexpr UINT k_tray_cmd_stop = 1004;
constexpr int k_branding_icon_resource_id = 41;
DWORD g_daemon_message_thread_id = 0;
UINT g_taskbar_created_msg = 0;

std::wstring utf8_to_wide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8(const std::wstring& s)
{
    if (s.empty())
        return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring quote_arg(const std::wstring& arg)
{
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(ch);
            slashes = 0;
        } else {
            out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(ch);
        }
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

fs::path current_exe_path()
{
    std::wstring buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n >= buf.size())
        return {};
    buf.resize(n);
    return fs::path(buf);
}

fs::path sibling_exe(const wchar_t* basename)
{
    const fs::path self = current_exe_path();
    if (self.empty())
        return {};
    return self.parent_path() / basename;
}

fs::path ui_exe_path()
{
    std::wstring name = std::wstring(pm::brand::k_app_id_w) + L".exe";
    fs::path p = sibling_exe(name.c_str());
    std::error_code ec;
    if (!p.empty() && fs::exists(p, ec))
        return p;
    return current_exe_path();
}

fs::path cli_exe_path()
{
    std::wstring name = std::wstring(pm::brand::k_app_id_w) + L"-cli.exe";
    fs::path p = sibling_exe(name.c_str());
    std::error_code ec;
    if (!p.empty() && fs::exists(p, ec))
        return p;
    return current_exe_path();
}

bool spawn_detached(const fs::path& exe, const std::vector<std::string>& args, std::string& err)
{
    if (exe.empty()) {
        err = "could not resolve executable path";
        return false;
    }
    std::wstring cmd = quote_arg(exe.wstring());
    for (const std::string& arg : args) {
        cmd.push_back(L' ');
        cmd += quote_arg(utf8_to_wide(arg));
    }
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;
    if (!::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        err = "CreateProcessW failed: " + std::to_string(static_cast<unsigned long>(::GetLastError()));
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

bool start_daemon_detached(const fs::path& config_path, std::string& err)
{
    return spawn_detached(cli_exe_path(), {"daemon", "--config", wide_to_utf8(config_path.wstring()), "tray"}, err);
}

fs::path default_config_path()
{
    return media::settings::get_daemon_json_path();
}

fs::path resolve_config_path(const std::string& user_path)
{
    if (user_path.empty())
        return default_config_path();
    return fs::absolute(fs::path(user_path));
}

std::string trim_bom_ws(std::string s)
{
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    const auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t b = 0;
    while (b < s.size() && is_ws(static_cast<unsigned char>(s[b])))
        ++b;
    size_t e = s.size();
    while (e > b && is_ws(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

bool read_text_file(const fs::path& p, std::string& out, std::string& err)
{
    out.clear();
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) {
        err = "open failed: " + p.string();
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool write_json_file(const fs::path& p, const json& doc, std::string& err)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) {
        err = "create_directories failed: " + ec.message();
        return false;
    }
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        err = "open for write failed: " + p.string();
        return false;
    }
    out << doc.dump(2) << "\n";
    if (!out.good()) {
        err = "write failed: " + p.string();
        return false;
    }
    return true;
}

bool shortcut_available(UINT modifiers, UINT vk)
{
    constexpr int probe_id = 0x4D49;
    if (!::RegisterHotKey(nullptr, probe_id, modifiers | MOD_NOREPEAT, vk))
        return false;
    ::UnregisterHotKey(nullptr, probe_id);
    return true;
}

std::string seed_accelerator()
{
    const std::vector<std::pair<std::string, UINT>> candidates = {
        {"Ctrl+Alt+F8", VK_F8}, {"Ctrl+Alt+F9", VK_F9}, {"Ctrl+Alt+F10", VK_F10},
        {"Ctrl+Alt+F11", VK_F11}, {"Ctrl+Alt+F12", VK_F12}, {"Ctrl+Alt+F7", VK_F7},
        {"Ctrl+Alt+F6", VK_F6}, {"Ctrl+Alt+F5", VK_F5}, {"Ctrl+Alt+F4", VK_F4},
        {"Ctrl+Alt+F3", VK_F3}, {"Ctrl+Alt+F2", VK_F2}, {"Ctrl+Alt+F1", VK_F1},
    };
    for (const auto& [label, vk] : candidates) {
        if (shortcut_available(MOD_CONTROL | MOD_ALT, vk))
            return label;
    }
    return "Ctrl+Alt+F8";
}

json default_config_doc()
{
    return json{
        {"version", 1},
        {"enabled", true},
        {"shortcuts", json::array({
            {
                {"id", "open-chat"},
                {"enabled", true},
                {"accelerator", seed_accelerator()},
                {"description", "Open PM-Image in the chat workbench."},
                {"action", {
                    {"type", "open-ui"},
                    {"preset", "chat"}
                }}
            },
            {
                {"id", "assistant-bar"},
                {"enabled", false},  // opt-in; user sets 'true' to activate
                {"accelerator", "Ctrl+Alt+F5"},
                {"description", "Show/launch the AI assistant toolbar (UIA spy + STT dictation)."},
                {"action", {
                    {"type", "assistant-bar"}
                }}
            }
        })}
    };
}

bool load_or_seed_config(const fs::path& p, json& out, bool* seeded, std::string& err)
{
    if (seeded)
        *seeded = false;
    std::error_code ec;
    const bool needs_seed = !fs::exists(p, ec) || fs::file_size(p, ec) == 0;
    if (needs_seed) {
        out = default_config_doc();
        if (!write_json_file(p, out, err))
            return false;
        if (seeded)
            *seeded = true;
        return true;
    }
    std::string raw;
    if (!read_text_file(p, raw, err))
        return false;
    raw = trim_bom_ws(std::move(raw));
    if (raw.empty()) {
        out = default_config_doc();
        if (!write_json_file(p, out, err))
            return false;
        if (seeded)
            *seeded = true;
        return true;
    }
    try {
        out = json::parse(raw);
    } catch (const std::exception& e) {
        err = std::string("JSON parse failed: ") + e.what();
        return false;
    }
    if (!out.is_object()) {
        err = "root must be a JSON object";
        return false;
    }
    return true;
}

bool parse_key_name(const std::string& token, UINT& vk)
{
    const std::string t = ascii_lower(token);
    if (t.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(t[0]);
        if (c >= 'a' && c <= 'z') {
            vk = static_cast<UINT>('A' + (c - 'a'));
            return true;
        }
        if (c >= '0' && c <= '9') {
            vk = static_cast<UINT>(c);
            return true;
        }
    }
    if (t.size() >= 2 && t[0] == 'f') {
        const int n = std::atoi(t.c_str() + 1);
        if (n >= 1 && n <= 24) {
            vk = static_cast<UINT>(VK_F1 + (n - 1));
            return true;
        }
    }
    if (t == "space") { vk = VK_SPACE; return true; }
    if (t == "tab") { vk = VK_TAB; return true; }
    if (t == "escape" || t == "esc") { vk = VK_ESCAPE; return true; }
    if (t == "insert" || t == "ins") { vk = VK_INSERT; return true; }
    if (t == "delete" || t == "del") { vk = VK_DELETE; return true; }
    if (t == "home") { vk = VK_HOME; return true; }
    if (t == "end") { vk = VK_END; return true; }
    if (t == "pageup" || t == "pgup") { vk = VK_PRIOR; return true; }
    if (t == "pagedown" || t == "pgdn") { vk = VK_NEXT; return true; }
    return false;
}

bool parse_accelerator(const std::string& accelerator, UINT& modifiers, UINT& vk)
{
    modifiers = MOD_NOREPEAT;
    vk = 0;
    std::string part;
    std::istringstream in(accelerator);
    while (std::getline(in, part, '+')) {
        part.erase(std::remove_if(part.begin(), part.end(),
            [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }),
            part.end());
        const std::string t = ascii_lower(part);
        if (t.empty())
            continue;
        if (t == "ctrl" || t == "control") {
            modifiers |= MOD_CONTROL;
        } else if (t == "alt" || t == "menu") {
            modifiers |= MOD_ALT;
        } else if (t == "shift") {
            modifiers |= MOD_SHIFT;
        } else if (t == "win" || t == "windows" || t == "meta") {
            modifiers |= MOD_WIN;
        } else if (!parse_key_name(t, vk)) {
            return false;
        }
    }
    return vk != 0;
}

std::vector<std::string> json_string_array(const json& j, const char* key)
{
    std::vector<std::string> out;
    if (!j.contains(key) || !j[key].is_array())
        return out;
    for (const auto& v : j[key]) {
        if (v.is_string())
            out.push_back(v.get<std::string>());
    }
    return out;
}

bool dispatch_action(const json& action, std::string& err)
{
    if (!action.is_object()) {
        err = "action must be an object";
        return false;
    }
    const std::string type = ascii_lower(action.value("type", std::string{"open-ui"}));
    if (type == "open-ui" || type == "openui") {
        std::vector<std::string> args;
        args.push_back("--ui-preset");
        args.push_back(action.value("preset", std::string{"chat"}));
        if (action.contains("mic")) {
            const auto& mic = action["mic"];
            const bool start_mic = (mic.is_boolean() && mic.get<bool>())
                || (mic.is_string() && ascii_lower(mic.get<std::string>()) == "start");
            if (start_mic) {
                args.push_back("--mic");
                args.push_back("start");
            }
        }
        if (const std::string prompt = action.value("prompt", std::string{}); !prompt.empty()) {
            args.push_back("--prompt");
            args.push_back(prompt);
        }
        if (const std::string app = action.value("app", std::string{}); !app.empty()) {
            args.push_back("--app");
            args.push_back(app);
        }
        for (const auto& src : json_string_array(action, "src")) {
            args.push_back("--src");
            args.push_back(src);
        }
        for (const auto& arg : json_string_array(action, "args"))
            args.push_back(arg);
        return spawn_detached(ui_exe_path(), args, err);
    }
    if (type == "app-command" || type == "appcommand") {
        const std::string command = action.value("command", std::string{"chat"});
        std::vector<std::string> args = {"app", command};
        if (const std::string paths = action.value("paths", std::string{}); !paths.empty())
            args.push_back(paths);
        return spawn_detached(ui_exe_path(), args, err);
    }
    if (type == "cli") {
        return spawn_detached(cli_exe_path(), json_string_array(action, "args"), err);
    }
    if (type == "external") {
        const std::string command = action.value("command", std::string{});
        if (command.empty()) {
            err = "external action requires command";
            return false;
        }
        return spawn_detached(fs::path(utf8_to_wide(command)), json_string_array(action, "args"), err);
    }
    if (type == "stt-chat" || type == "sttchat") {
        std::vector<std::string> args = {"llm", "agent", "--mic"};
        if (const std::string voice = action.value("voice", std::string{}); !voice.empty()) {
            args.push_back("--stt-voice-id");
            args.push_back(voice);
        }
        return spawn_detached(cli_exe_path(), args, err);
    }
    // assistant-bar: launch the AI assistant toolbar (UIA spy + optional STT write-back).
    // Spawns pm-image-cli.exe assistant spy --ui [--stt] so the daemon itself stays
    // lightweight.  The toolbar window appears snapped to the left monitor edge;
    // it can be dragged and its position is remembered across launches.
    if (type == "assistant-bar" || type == "assistantbar") {
        std::vector<std::string> args = {"assistant", "spy", "--ui"};
        // Optional: propagate --stt flag from the action config.
        if (action.value("stt", false))
            args.push_back("--stt");
        return spawn_detached(cli_exe_path(), args, err);
    }
    err = "unknown action type: " + type;
    return false;
}

struct RegisteredHotkey {
    int id = 0;
    std::string label;
    json action;
};

struct TrayStrings {
    std::wstring tooltip;
    std::wstring open_chat;
    std::wstring open_viewer;
    std::wstring open_main;
    std::wstring stop_daemon;
};

TrayStrings tray_strings_for_language(std::string lang)
{
    lang = ascii_lower(std::move(lang));
    const std::wstring app = pm::brand::k_app_display_w;
    if (lang == "es") {
        return {app + L" daemon", L"Abrir chat", L"Abrir visor", L"Abrir principal", L"Detener daemon"};
    }
    if (lang == "de") {
        return {app + L" Daemon", L"Chat öffnen", L"Viewer öffnen", L"Hauptfenster öffnen", L"Daemon stoppen"};
    }
    if (lang == "it") {
        return {app + L" daemon", L"Apri chat", L"Apri visualizzatore", L"Apri principale", L"Ferma daemon"};
    }
    if (lang == "fr") {
        return {app + L" daemon", L"Ouvrir le chat", L"Ouvrir la visionneuse", L"Ouvrir l'accueil", L"Arrêter le daemon"};
    }
    return {app + L" daemon", L"Open Chat", L"Open Viewer", L"Open Main", L"Stop Daemon"};
}

TrayStrings load_tray_strings()
{
    media::settings::AppearanceSettings appearance{};
    std::string err;
    if (media::settings::load_appearance(appearance, err))
        return tray_strings_for_language(appearance.display_language);
    return tray_strings_for_language("en");
}

HICON load_branding_icon()
{
    HINSTANCE inst = ::GetModuleHandleW(nullptr);
    HICON icon = static_cast<HICON>(::LoadImageW(
        inst,
        MAKEINTRESOURCEW(k_branding_icon_resource_id),
        IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON),
        ::GetSystemMetrics(SM_CYSMICON),
        LR_SHARED));
    if (icon)
        return icon;

    HICON large_icon = nullptr;
    HICON small_icon = nullptr;
    const fs::path ui_exe = ui_exe_path();
    if (!ui_exe.empty() && ::ExtractIconExW(ui_exe.wstring().c_str(), 0, &large_icon, &small_icon, 1) > 0) {
        if (large_icon)
            ::DestroyIcon(large_icon);
        if (small_icon)
            return small_icon;
    }

    return ::LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
}

bool dispatch_open_ui_preset(const char* preset, std::string& err)
{
    return dispatch_action(json{{"type", "open-ui"}, {"preset", preset ? preset : "chat"}}, err);
}

void log_action_error(const std::string& prefix, const std::string& err)
{
    if (!err.empty())
        std::cerr << prefix << ": " << err << "\n";
}

void request_daemon_stop()
{
    HANDLE stop_event = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, k_stop_event_name);
    if (stop_event) {
        (void)::SetEvent(stop_event);
        ::CloseHandle(stop_event);
    }
    if (g_daemon_message_thread_id != 0)
        ::PostThreadMessageW(g_daemon_message_thread_id, WM_QUIT, 0, 0);
}

struct UniqueHandle {
    HANDLE value = nullptr;
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) : value(h) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value(other.value) { other.value = nullptr; }
    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }
    explicit operator bool() const noexcept { return value != nullptr; }
    void reset(HANDLE h = nullptr)
    {
        if (value)
            ::CloseHandle(value);
        value = h;
    }
};

bool tray_icon_update(HWND hwnd, DWORD message)
{
    const TrayStrings tr = load_tray_strings();
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = k_tray_icon_id;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = k_tray_callback_msg;
    nid.hIcon = load_branding_icon();
    wcsncpy_s(nid.szTip, tr.tooltip.c_str(), _TRUNCATE);
    return ::Shell_NotifyIconW(message, &nid) == TRUE;
}

void tray_show_menu(HWND hwnd)
{
    const TrayStrings tr = load_tray_strings();
    HMENU menu = ::CreatePopupMenu();
    if (!menu)
        return;
    ::AppendMenuW(menu, MF_STRING, k_tray_cmd_open_chat, tr.open_chat.c_str());
    ::AppendMenuW(menu, MF_STRING, k_tray_cmd_open_viewer, tr.open_viewer.c_str());
    ::AppendMenuW(menu, MF_STRING, k_tray_cmd_open_main, tr.open_main.c_str());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, k_tray_cmd_stop, tr.stop_daemon.c_str());

    POINT pt{};
    ::GetCursorPos(&pt);
    ::SetForegroundWindow(hwnd);
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
    ::DestroyMenu(menu);
}

LRESULT CALLBACK daemon_tray_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (g_taskbar_created_msg != 0 && msg == g_taskbar_created_msg) {
        tray_icon_update(hwnd, NIM_ADD);
        return 0;
    }
    switch (msg) {
        case k_tray_callback_msg:
            if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
                tray_show_menu(hwnd);
                return 0;
            }
            if (lparam == WM_LBUTTONDBLCLK) {
                std::string err;
                if (!dispatch_open_ui_preset("chat", err))
                    log_action_error("daemon tray open chat", err);
                return 0;
            }
            break;
        case WM_COMMAND: {
            std::string err;
            switch (LOWORD(wparam)) {
                case k_tray_cmd_open_chat:
                    if (!dispatch_open_ui_preset("chat", err))
                        log_action_error("daemon tray open chat", err);
                    return 0;
                case k_tray_cmd_open_viewer:
                    if (!dispatch_open_ui_preset("viewer", err))
                        log_action_error("daemon tray open viewer", err);
                    return 0;
                case k_tray_cmd_open_main:
                    if (!dispatch_open_ui_preset("main", err))
                        log_action_error("daemon tray open main", err);
                    return 0;
                case k_tray_cmd_stop:
                    request_daemon_stop();
                    return 0;
                default:
                    break;
            }
            break;
        }
        case WM_DESTROY:
            tray_icon_update(hwnd, NIM_DELETE);
            break;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND create_tray_window()
{
    HINSTANCE inst = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = daemon_tray_wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = k_tray_window_class;
    wc.hIcon = load_branding_icon();
    wc.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    ::RegisterClassExW(&wc);
    const TrayStrings tr = load_tray_strings();
    return ::CreateWindowExW(0, k_tray_window_class, tr.tooltip.c_str(),
        WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, inst, nullptr);
}

BOOL WINAPI daemon_console_ctrl_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (g_daemon_message_thread_id != 0)
                ::PostThreadMessageW(g_daemon_message_thread_id, WM_QUIT, 0, 0);
            return TRUE;
        default:
            return FALSE;
    }
}

int daemon_run(const fs::path& config_path, bool tray_mode)
{
    UniqueHandle instance_mutex(::CreateMutexW(nullptr, TRUE, k_mutex_name));
    if (!instance_mutex) {
        std::cerr << "daemon: CreateMutexW failed: " << static_cast<unsigned long>(::GetLastError()) << "\n";
        return 1;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << "daemon: already running\n";
        return 0;
    }
    UniqueHandle stop_event(::CreateEventW(nullptr, TRUE, FALSE, k_stop_event_name));
    if (!stop_event) {
        std::cerr << "daemon: CreateEventW failed: " << static_cast<unsigned long>(::GetLastError()) << "\n";
        return 1;
    }
    ::ResetEvent(stop_event.value);

    json cfg;
    bool seeded = false;
    std::string err;
    if (!load_or_seed_config(config_path, cfg, &seeded, err)) {
        std::cerr << "daemon: " << err << "\n";
        return 1;
    }
    if (seeded)
        std::cout << "daemon: seeded " << config_path.string() << "\n";
    if (!cfg.value("enabled", true)) {
        std::cout << "daemon: disabled by config\n";
        return 0;
    }

    const json shortcuts = cfg.value("shortcuts", json::array());
    if (!shortcuts.is_array()) {
        std::cerr << "daemon: shortcuts must be an array\n";
        return 1;
    }

    HWND tray_hwnd = nullptr;
    if (tray_mode) {
        if (g_taskbar_created_msg == 0)
            g_taskbar_created_msg = ::RegisterWindowMessageW(L"TaskbarCreated");
        tray_hwnd = create_tray_window();
        if (!tray_hwnd) {
            std::cerr << "daemon: failed to create tray window: "
                      << static_cast<unsigned long>(::GetLastError()) << "\n";
            return 1;
        }
        if (!tray_icon_update(tray_hwnd, NIM_ADD))
            std::cerr << "daemon: warning: Shell_NotifyIconW(NIM_ADD) failed\n";
    }

    std::vector<RegisteredHotkey> hotkeys;
    int next_id = 100;
    for (const auto& item : shortcuts) {
        if (!item.is_object() || !item.value("enabled", true))
            continue;
        const std::string accelerator = item.value("accelerator", std::string{});
        UINT modifiers = 0, vk = 0;
        if (!parse_accelerator(accelerator, modifiers, vk)) {
            std::cerr << "daemon: invalid accelerator: " << accelerator << "\n";
            continue;
        }
        const int id = next_id++;
        if (!::RegisterHotKey(tray_hwnd, id, modifiers, vk)) {
            std::cerr << "daemon: hotkey collision or registration failure: " << accelerator
                      << " (error " << static_cast<unsigned long>(::GetLastError()) << ")\n";
            continue;
        }
        hotkeys.push_back(RegisteredHotkey{id, accelerator, item.value("action", json::object())});
        std::cout << "daemon: registered " << accelerator << "\n";
    }

    if (hotkeys.empty()) {
        std::cerr << "daemon: no shortcuts registered\n";
        if (tray_hwnd) {
            tray_icon_update(tray_hwnd, NIM_DELETE);
            ::DestroyWindow(tray_hwnd);
        }
        return 1;
    }

    g_daemon_message_thread_id = ::GetCurrentThreadId();
    MSG msg{};
    // Ensure the thread message queue exists before the console handler may post WM_QUIT.
    (void)::PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    const BOOL handler_installed = tray_mode ? FALSE : ::SetConsoleCtrlHandler(daemon_console_ctrl_handler, TRUE);

    std::cout << (tray_mode ? "daemon: tray listening\n" : "daemon: listening; press Ctrl+C to stop\n");
    bool running = true;
    while (running) {
        const DWORD wait = ::MsgWaitForMultipleObjects(1, &stop_event.value, FALSE, INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0)
            break;
        if (wait == WAIT_FAILED) {
            std::cerr << "daemon: MsgWaitForMultipleObjects failed: "
                      << static_cast<unsigned long>(::GetLastError()) << "\n";
            break;
        }
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            if (msg.message != WM_HOTKEY) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
                continue;
            }
            const int id = static_cast<int>(msg.wParam);
            auto it = std::find_if(hotkeys.begin(), hotkeys.end(),
                [id](const RegisteredHotkey& h) { return h.id == id; });
            if (it == hotkeys.end())
                continue;
            std::string action_err;
            if (!dispatch_action(it->action, action_err))
                std::cerr << "daemon: " << it->label << ": " << action_err << "\n";
        }
    }

    if (handler_installed)
        ::SetConsoleCtrlHandler(daemon_console_ctrl_handler, FALSE);
    g_daemon_message_thread_id = 0;
    for (const auto& h : hotkeys)
        ::UnregisterHotKey(tray_hwnd, h.id);
    if (tray_hwnd) {
        tray_icon_update(tray_hwnd, NIM_DELETE);
        ::DestroyWindow(tray_hwnd);
    }
    std::cout << "daemon: stopped\n";
    return 0;
}

int daemon_stop(bool quiet_if_not_running = false)
{
    UniqueHandle stop_event(::OpenEventW(EVENT_MODIFY_STATE, FALSE, k_stop_event_name));
    if (!stop_event) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            if (!quiet_if_not_running)
                std::cout << "daemon stop: not running\n";
            return 0;
        }
        std::cerr << "daemon stop: OpenEventW failed: " << static_cast<unsigned long>(err) << "\n";
        return 1;
    }
    if (!::SetEvent(stop_event.value)) {
        std::cerr << "daemon stop: SetEvent failed: " << static_cast<unsigned long>(::GetLastError()) << "\n";
        return 1;
    }

    for (int i = 0; i < 50; ++i) {
        UniqueHandle mutex(::OpenMutexW(SYNCHRONIZE, FALSE, k_mutex_name));
        if (!mutex) {
            if (::GetLastError() == ERROR_FILE_NOT_FOUND) {
                std::cout << "daemon stop: stopped\n";
                return 0;
            }
            break;
        }
        const DWORD wait = ::WaitForSingleObject(mutex.value, 100);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
            if (wait == WAIT_OBJECT_0)
                ::ReleaseMutex(mutex.value);
            std::cout << "daemon stop: stopped\n";
            return 0;
        }
    }
    std::cout << "daemon stop: stop requested\n";
    return 0;
}

bool is_elevated()
{
    BOOL is_admin = FALSE;
    PSID admin_group = nullptr;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    if (::AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
        ::CheckTokenMembership(nullptr, admin_group, &is_admin);
        ::FreeSid(admin_group);
    }
    return is_admin == TRUE;
}

std::wstring daemon_run_command_line(const fs::path& config_path)
{
    std::wstring cmd = quote_arg(current_exe_path().wstring());
    cmd += L" daemon --config ";
    cmd += quote_arg(config_path.wstring());
    cmd += L" tray";
    return cmd;
}

int relaunch_daemon_elevated(const char* subcommand, const fs::path& config_path)
{
    std::wstring params = L"daemon --config ";
    params += quote_arg(config_path.wstring());
    params.push_back(L' ');
    params += utf8_to_wide(subcommand ? subcommand : "");
    if (std::string(subcommand ? subcommand : "") == "register")
        params += L" --elevated-write-only";

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    const fs::path exe = current_exe_path();
    const std::wstring exe_w = exe.wstring();
    sei.lpFile = exe_w.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!::ShellExecuteExW(&sei)) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_CANCELLED)
            std::cerr << "daemon " << subcommand << ": elevation cancelled by user\n";
        else
            std::cerr << "daemon " << subcommand << ": ShellExecuteExW(runas) failed: "
                      << static_cast<unsigned long>(err) << "\n";
        return 1;
    }

    DWORD exit_code = 1;
    if (sei.hProcess) {
        ::WaitForSingleObject(sei.hProcess, INFINITE);
        (void)::GetExitCodeProcess(sei.hProcess, &exit_code);
        ::CloseHandle(sei.hProcess);
    }
    return static_cast<int>(exit_code);
}

int daemon_register(const fs::path& config_path, bool elevated_write_only)
{
    (void)daemon_stop(true);
    if (!is_elevated()) {
        std::cout << "daemon register: requesting elevation...\n";
        const int elevated_exit = relaunch_daemon_elevated("register", config_path);
        if (elevated_exit != 0)
            return elevated_exit;
        std::string start_err;
        if (!start_daemon_detached(config_path, start_err)) {
            std::cerr << "daemon register: registered, but auto-start failed: " << start_err << "\n";
            return 1;
        }
        std::cout << "daemon register: started daemon\n";
        return 0;
    }
    json cfg;
    bool seeded = false;
    std::string err;
    if (!load_or_seed_config(config_path, cfg, &seeded, err)) {
        std::cerr << "daemon register: " << err << "\n";
        return 1;
    }

    HKEY key{};
    const LSTATUS open_status = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, k_run_key, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (open_status != ERROR_SUCCESS) {
        std::cerr << "daemon register: RegCreateKeyExW failed: " << open_status << "\n";
        return 1;
    }
    const std::wstring cmd = daemon_run_command_line(config_path);
    const DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
    const LSTATUS set_status = ::RegSetValueExW(key, k_run_value, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(cmd.c_str()), bytes);
    ::RegCloseKey(key);
    if (set_status != ERROR_SUCCESS) {
        std::cerr << "daemon register: RegSetValueExW failed: " << set_status << "\n";
        return 1;
    }
    std::cout << "daemon register: wrote HKLM Run entry";
    if (seeded)
        std::cout << " and seeded " << config_path.string();
    std::cout << "\n";
    if (elevated_write_only)
        return 0;
    std::string start_err;
    if (!start_daemon_detached(config_path, start_err)) {
        std::cerr << "daemon register: registered, but auto-start failed: " << start_err << "\n";
        return 1;
    }
    std::cout << "daemon register: started daemon\n";
    return 0;
}

int daemon_unregister()
{
    if (!is_elevated()) {
        std::cout << "daemon unregister: requesting elevation...\n";
        const int elevated_exit = relaunch_daemon_elevated("unregister", default_config_path());
        const int stop_exit = daemon_stop(true);
        return elevated_exit != 0 ? elevated_exit : stop_exit;
    }
    HKEY key{};
    const LSTATUS open_status = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, k_run_key, 0, KEY_SET_VALUE, &key);
    if (open_status != ERROR_SUCCESS) {
        std::cerr << "daemon unregister: RegOpenKeyExW failed: " << open_status << "\n";
        return 1;
    }
    const LSTATUS delete_status = ::RegDeleteValueW(key, k_run_value);
    ::RegCloseKey(key);
    if (delete_status != ERROR_SUCCESS && delete_status != ERROR_FILE_NOT_FOUND) {
        std::cerr << "daemon unregister: RegDeleteValueW failed: " << delete_status << "\n";
        return 1;
    }
    std::cout << "daemon unregister: removed HKLM Run entry\n";
    return daemon_stop(true);
}

#endif // _WIN32

} // namespace

int pm_image_cmd_daemon(CLI::App& /*app*/, PmImageCliState& st)
{
#if defined(_WIN32)
    const fs::path config_path = resolve_config_path(st.daemon_config_path);
    if (st.daemon_path_cmd && st.daemon_path_cmd->parsed()) {
        std::cout << config_path.string() << "\n";
        return 0;
    }
    if (st.daemon_register_cmd && st.daemon_register_cmd->parsed())
        return daemon_register(config_path, st.daemon_elevated_write_only);
    if (st.daemon_unregister_cmd && st.daemon_unregister_cmd->parsed())
        return daemon_unregister();
    if (st.daemon_stop_cmd && st.daemon_stop_cmd->parsed())
        return daemon_stop(false);
    return daemon_run(config_path, (st.daemon_tray_cmd && st.daemon_tray_cmd->parsed()) || st.daemon_tray);
#else
    (void)st;
    std::cerr << "daemon: global shortcut daemon is not implemented on this platform yet.\n";
    return 1;
#endif
}

void pm_image_register_daemon(CLI::App& app, PmImageCliState& s)
{
    s.daemon_cmd = app.add_subcommand(
        "daemon",
        "Global shortcut daemon. Windows currently supports registering a logon daemon, listening for hotkeys, "
        "opening UI presets, forwarding app commands, launching CLI/external commands, and starting STT chat.");
    s.daemon_cmd->require_subcommand(0, 1);
    s.daemon_cmd->add_option(
        "--config",
        s.daemon_config_path,
        "Daemon JSON config path. Default: the app roaming profile daemon.json next to settings.json.");
    s.daemon_cmd
        ->add_flag("--elevated-write-only", s.daemon_elevated_write_only,
                   "Internal: elevated register writes autorun only; caller starts the daemon.")
        ->group("");
    s.daemon_run_cmd = s.daemon_cmd->add_subcommand("run", "Run the foreground hotkey daemon (default action).");
    s.daemon_run_cmd->add_flag("--tray", s.daemon_tray, "Run with a notification-area tray icon.");
    s.daemon_tray_cmd = s.daemon_cmd->add_subcommand(
        "tray",
        "Run the user-session tray daemon with global hotkeys and a notification-area menu.");
    s.daemon_register_cmd = s.daemon_cmd->add_subcommand(
        "register",
        "Windows: register the daemon for logon by writing HKLM Run (requires elevation). Seeds config if missing.");
    s.daemon_unregister_cmd = s.daemon_cmd->add_subcommand(
        "unregister",
        "Windows: remove the daemon HKLM Run entry (requires elevation).");
    s.daemon_stop_cmd = s.daemon_cmd->add_subcommand(
        "stop",
        "Windows: stop the running daemon for this user session.");
    s.daemon_path_cmd = s.daemon_cmd->add_subcommand(
        "path",
        "Print the effective daemon.json path and exit.");
}
