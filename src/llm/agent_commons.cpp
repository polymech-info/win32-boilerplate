#include "agent_internal.hpp"

#include "constants.hpp"
#include "app_exe_directory.hpp"

#include "core/settings_store.hpp"

#include "path_tool_executor.hpp"  // set_agent_tool_blocklist / clear / set_agent_path_base

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace media::llm::agent::detail {

// ── String utilities ──────────────────────────────────────────────────────────

void str_tolower_in_place(std::string& s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

// ── Event emission ────────────────────────────────────────────────────────────

bool emit(const EventCallback& cb, const Event& ev) {
    if (!cb) return true;
    return cb(ev);
}

// ── System-prompt file loading ────────────────────────────────────────────────

std::string trim_system_prompt_utf8(std::string s) {
    while (!s.empty() && static_cast<unsigned char>(s.front()) <= 32U) s.erase(0, 1);
    while (!s.empty() && static_cast<unsigned char>(s.back())  <= 32U) s.pop_back();
    return s;
}

std::filesystem::path system_prompt_config_parent() {
    namespace fs = std::filesystem;
    try {
        return media::settings::get_config_dir();
    } catch (...) {
        return {};
    }
}

std::string load_system_prompt_md_if_present() {
    namespace fs = std::filesystem;
    const fs::path filename = "system-prompt.md";

    auto try_file = [&](const fs::path& dir) -> std::string {
        if (dir.empty()) return {};
        const fs::path f = dir / filename;
        std::error_code ec;
        if (!fs::is_regular_file(f, ec)) return {};
        std::ifstream in(f, std::ios::binary);
        if (!in) return {};
        std::ostringstream oss;
        oss << in.rdbuf();
        return trim_system_prompt_utf8(std::move(oss.str()));
    };

    if (std::string s = try_file(media::app::exe_parent_directory()); !s.empty()) return s;
    if (std::string s = try_file(system_prompt_config_parent());       !s.empty()) return s;
    return {};
}

// ── Date / time ───────────────────────────────────────────────────────────────

std::string local_datetime_with_offset() {
    using namespace std::chrono;
    const auto     now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);

    std::tm ltm{};
    std::tm utm{};
#if defined(_WIN32)
    localtime_s(&ltm, &t);
    gmtime_s(&utm, &t);
#else
    localtime_r(&t, &ltm);
    gmtime_r(&t, &utm);
#endif

    int local_m = ltm.tm_hour * 60 + ltm.tm_min;
    int utc_m   = utm.tm_hour * 60 + utm.tm_min;
    int day_d   = ltm.tm_yday - utm.tm_yday;
    if (day_d >  1) day_d = -1;
    if (day_d < -1) day_d =  1;
    const int off_min = local_m - utc_m + day_d * 1440;
    const char sign   = off_min >= 0 ? '+' : '-';
    const int aoff    = std::abs(off_min);

    std::ostringstream oss;
    oss << std::put_time(&ltm, "%Y-%m-%dT%H:%M:%S")
        << sign
        << std::setw(2) << std::setfill('0') << (aoff / 60) << ':'
        << std::setw(2) << std::setfill('0') << (aoff % 60);

#if defined(_WIN32)
    TIME_ZONE_INFORMATION tzi{};
    if (GetTimeZoneInformation(&tzi) != TIME_ZONE_ID_INVALID) {
        const WCHAR* wname = (ltm.tm_isdst > 0) ? tzi.DaylightName : tzi.StandardName;
        if (wname && wname[0]) {
            const int len = ::WideCharToMultiByte(CP_UTF8, 0, wname, -1,
                                                  nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                std::string tz(static_cast<std::size_t>(len - 1), '\0');
                ::WideCharToMultiByte(CP_UTF8, 0, wname, -1,
                                      tz.data(), len, nullptr, nullptr);
                oss << " (" << tz << ')';
            }
        }
    }
#endif
    return oss.str();
}

// ── RAII scopes ───────────────────────────────────────────────────────────────

PathToolBlocklistScope::PathToolBlocklistScope(const std::vector<std::string>& disabled, int max_iter) {
    if (!disabled.empty() && max_iter > 0) {
        media::llm::path::set_agent_tool_blocklist(disabled);
        active = true;
    }
}
PathToolBlocklistScope::~PathToolBlocklistScope() {
    if (active) media::llm::path::clear_agent_tool_blocklist();
}

AgentPathBaseScope::AgentPathBaseScope(const Turn& t) {
    std::string base;
    if (!t.folder_hint.empty()) base = t.folder_hint;
    if (!base.empty()) {
        media::llm::path::set_agent_path_base(base);
        active = true;
    }
}
AgentPathBaseScope::~AgentPathBaseScope() {
    if (active) media::llm::path::clear_agent_path_base();
}

GodmodeScope::GodmodeScope(bool on) {
    if (on) {
        media::llm::path::set_agent_godmode(true);
        active = true;
    }
}
GodmodeScope::~GodmodeScope() {
    if (active) media::llm::path::set_agent_godmode(false);
}

// ── Runtime environment block ─────────────────────────────────────────────────

void append_runtime_environment_context(std::ostringstream& s) {
    s << "Runtime context (built in, always true for this session):\n"
         "  - Host OS: ";
#if defined(_WIN32)
    s << "Microsoft Windows (desktop). User paths often use drive letters and backslashes; "
         "the app normalises paths for tools.\n";
#elif defined(__APPLE__)
    s << "Apple macOS (desktop). Paths are POSIX-style.\n";
#elif defined(__linux__)
    s << "Linux (desktop or server). Paths are POSIX-style.\n";
#else
    s << "Other or unknown; paths follow the host platform.\n";
#endif
    s << "  - Application: **" << pm::brand::k_app_display_u8
      << "** (technical id `" << pm::brand::k_app_id_u8 << "`)";
#if defined(_WIN32)
    s << " \u2014 includes Windows Explorer / shell integration.\n";
#else
    s << ".\n";
#endif
    s << "  - Current local date/time: **" << local_datetime_with_offset() << "**"
         " \u2014 use this when the user says 'now', 'today', 'tonight', or a clock time without a timezone.\n"
         "  - Chat interface: the user reads your replies in an **in-app chat panel** with **rich Markdown** "
         "(headings, lists, emphasis, fenced code blocks, links when allowed by the renderer). "
         "It is not a plain-text terminal; use Markdown when it clarifies structure or code.\n\n";
}

// ── LLM usage parsing ─────────────────────────────────────────────────────────

std::int64_t json_usage_int64(const nlohmann::json& usage, const char* key) {
    if (!usage.contains(key) || !usage[key].is_number()) return 0;
    const auto& v = usage[key];
    if (v.is_number_integer()) return v.get<std::int64_t>();
    return static_cast<std::int64_t>(v.get<double>());
}

void merge_llm_usage_from_provider_meta(nlohmann::json& agg,
                                        const std::string& provider_meta_json,
                                        const std::string& router)
{
    if (provider_meta_json.empty()) return;

    if (!agg.is_object()) {
        agg                          = nlohmann::json::object();
        agg["llm_rounds"]            = nlohmann::json::array();
        agg["prompt_tokens"]         = 0;
        agg["completion_tokens"]     = 0;
        agg["total_tokens"]          = 0;
    }
    if (!agg.contains("llm_rounds") || !agg["llm_rounds"].is_array())
        agg["llm_rounds"] = nlohmann::json::array();

    nlohmann::json meta = nlohmann::json::parse(provider_meta_json, nullptr, false);
    if (meta.is_discarded() || !meta.is_object()) return;

    nlohmann::json round_entry = nlohmann::json::object();
    if (!router.empty())        round_entry["provider"] = router;
    if (meta.contains("model")) round_entry["model"]    = meta["model"];
    if (meta.contains("id"))    round_entry["id"]       = meta["id"];

    std::int64_t add_prompt     = 0;
    std::int64_t add_completion = 0;
    std::int64_t add_total      = 0;
    double       add_cost       = 0;
    bool         have_usage     = false;
    bool         have_cost      = false;

    if (meta.contains("usage") && meta["usage"].is_object()) {
        const nlohmann::json& u = meta["usage"];
        round_entry["usage"]    = u;
        have_usage              = true;
        add_prompt     = json_usage_int64(u, "prompt_tokens")     + json_usage_int64(u, "input_tokens");
        add_completion = json_usage_int64(u, "completion_tokens") + json_usage_int64(u, "output_tokens");
        add_total      = json_usage_int64(u, "total_tokens");
        if (add_total <= 0 && (add_prompt > 0 || add_completion > 0))
            add_total = add_prompt + add_completion;
        if (u.contains("cost") && u["cost"].is_number()) {
            have_cost = true;
            add_cost  = u["cost"].get<double>();
        }
    }

    if (!round_entry.empty())
        agg["llm_rounds"].push_back(std::move(round_entry));

    if (!have_usage) return;

    agg["prompt_tokens"]     = json_usage_int64(agg, "prompt_tokens")     + add_prompt;
    agg["completion_tokens"] = json_usage_int64(agg, "completion_tokens") + add_completion;
    agg["total_tokens"]      = json_usage_int64(agg, "total_tokens")      + add_total;

    if (have_cost) {
        double prev = 0;
        if (agg.contains("cost") && agg["cost"].is_number())
            prev = agg["cost"].get<double>();
        agg["cost"] = prev + add_cost;
    }
}

} // namespace media::llm::agent::detail
