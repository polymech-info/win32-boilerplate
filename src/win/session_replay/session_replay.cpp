#include "win/session_replay/session_replay.hpp"
#include "constants.hpp"

#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <shellapi.h> // high-DPI: optional
#endif

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace media::win::session_replay {
namespace {

std::string utc_iso8601_rough()
{
    const auto  now = std::chrono::system_clock::now();
    const auto  t   = std::chrono::system_clock::to_time_t(now);
    std::tm     tm{};
    std::ostringstream os;
#if defined(_MSC_VER)
    (void)gmtime_s(&tm, &t);
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
#else
    (void)gmtime_r(&t, &tm);
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
#endif
    return os.str();
}

} // namespace

std::filesystem::path default_sessions_dir()
{
    fs::path d = media::settings::get_config_dir() / "sessions";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

bool resolve_session_replay_cli_input(const std::string& utf8, fs::path& out_abs, std::string& err)
{
    err.clear();
    out_abs.clear();
    if (utf8.empty()) {
        err = "empty path";
        return false;
    }
    const auto is_regular = [](const fs::path& c) {
        std::error_code e;
        return fs::exists(c, e) && fs::is_regular_file(c, e);
    };
    const auto commit = [&](const fs::path& c) -> bool {
        if (!is_regular(c))
            return false;
        std::error_code e2;
        out_abs = fs::absolute(c, e2);
        return true;
    };

    const fs::path p(utf8);
    if (commit(p))
        return true;
    if (p.is_absolute()) {
        err = "not a file or does not exist: " + utf8;
        return false;
    }
    fs::path cfg;
    try {
        cfg = media::settings::get_config_dir();
    }
    catch (const std::exception& ex) {
        err = std::string("replay path: ") + ex.what();
        return false;
    }
    if (commit(cfg / p))
        return true;
    const fs::path leaf = p.filename();
    if (!leaf.empty() && commit(cfg / "sessions" / leaf))
        return true;
    err = "not a file or does not exist: " + utf8;
    return false;
}

std::wstring make_session_filename()
{
    using namespace std::chrono;
    const auto  now = system_clock::now();
    const auto  t   = system_clock::to_time_t(now);
    std::tm     tm{};
#if defined(_MSC_VER)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::wostringstream w;
    w << L"session-"
      << std::put_time(&tm, L"%H%M%S")
      << L".json";
    return w.str();
}

json window_layout_to_json(const media::settings::WindowLayout& w)
{
    return media::layout::LayoutStore::WindowLayoutToJson(w);
}

bool json_to_window_layout(const json& j, media::settings::WindowLayout& w, std::string& err)
{
    return media::layout::LayoutStore::ParseWindowLayout(j, w, err);
}

json build_session_json(const media::settings::WindowLayout& wl, HWND main_hwnd, const json* input_events,
                        const json* snapshot_docks)
{
    int dpi = 96;
#if defined(_WIN32)
    if (main_hwnd && IsWindow(main_hwnd)) {
        using PFN_GDFW = UINT (WINAPI*)(HWND);
        static HMODULE hUser   = GetModuleHandleW(L"user32.dll");
        static auto      pGdfw = hUser
                                    ? (PFN_GDFW)(void*)GetProcAddress(hUser, "GetDpiForWindow")
                                    : nullptr;
        if (pGdfw)
            dpi = (int)pGdfw(main_hwnd);
        else
            dpi = 96; // GDI default
    }
#endif
    json root;
    root["version"] = kSessionFileVersion;
    root["app"]     = json{{"name", pm::brand::k_session_replay_app_name_u8}};
    root["captured"] = json{
        { "utc_rough", utc_iso8601_rough()},
    };
    root["display"]  = json{{"dpi", dpi}, {"per_monitor", true}};
    json snap = json{{"window_layout", window_layout_to_json(wl)}};
    if (snapshot_docks && snapshot_docks->is_object()) {
        snap["docks"] = *snapshot_docks;
    }
    root["snapshot"] = std::move(snap);
    if (input_events && input_events->is_array()) {
        root["events"] = *input_events;
    } else {
        root["events"] = json::array();
    }
    return root;
}

bool read_session_file(const std::filesystem::path& p, nlohmann::json& out, std::string& err)
{
    try {
        std::ifstream ifs(p, std::ios::binary);
        if (!ifs) {
            err = "open failed: " + p.string();
            return false;
        }
        ifs >> out;
    }
    catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    return out.is_object();
}

bool write_session_file(const std::filesystem::path& p, const nlohmann::json& j, std::string& err)
{
    try {
        if (p.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
        }
        std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            err = "open for write: " + p.string();
            return false;
        }
        ofs << j.dump(2);
    }
    catch (const std::exception& e) {
        err = e.what();
        return false;
    }
    return true;
}

} // namespace media::win::session_replay
