#include "agent_memory.hpp"

#include "constants.hpp"
#include "core/settings_store.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace fs = std::filesystem;

namespace media::llm::agent {

// ── Internal store ───────────────────────────────────────────────────────────

namespace {

constexpr size_t k_max_events = 50;

struct TaskStore {
    std::mutex                             mutex;
    std::unordered_map<std::string, ScheduledTask> tasks;    // real scheduler tasks (in-process only)
    std::unordered_map<std::string, ScheduledTask> sessions; // chat-session memory (persisted to disk)
    int                                    next_seq = 1;
};

TaskStore& store() {
    static TaskStore s;
    return s;
}

thread_local std::string tl_current_task_id;

std::string gen_id(const std::string& title) {
    auto& s = store();   // lock held by caller
    std::string safe;
    for (unsigned char c : title) {
        if (std::isalnum(c))
            safe += static_cast<char>(std::tolower(c));
        else if (!safe.empty() && safe.back() != '-')
            safe += '-';
        if (safe.size() >= 20) break;
    }
    while (!safe.empty() && safe.back() == '-') safe.pop_back();
    if (safe.empty()) safe = "task";
    return safe + "-" + std::to_string(s.next_seq++);
}

// ── Session-disk plumbing ───────────────────────────────────────────────────

std::mutex                  g_session_dir_mutex;
std::optional<fs::path>     g_session_dir_override;

fs::path resolve_session_store_dir_unlocked() {
    if (g_session_dir_override)
        return *g_session_dir_override;
    if (const char* env = std::getenv("PM_IMAGE_SESSION_DIR"); env && *env)
        return fs::path(env);
    try {
        return media::settings::get_config_dir() / "sessions";
    } catch (...) {
        // Last-ditch: relative to CWD (no roaming profile available).
        return fs::path("sessions");
    }
}

bool ensure_session_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return !ec;
}

fs::path session_file_path_for(const std::string& sanitized_id) {
    std::lock_guard<std::mutex> lk(g_session_dir_mutex);
    return resolve_session_store_dir_unlocked() / (sanitized_id + ".json");
}

bool atomic_write_json(const fs::path& path, const nlohmann::json& j) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return false;

    fs::path tmp = path;
    tmp += ".tmp";
    try {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o.is_open()) return false;
        o << j.dump(2);
        o.flush();
        if (!o) return false;
    } catch (...) { return false; }

    fs::rename(tmp, path, ec);
    if (ec) {
        // Windows rename-over-existing can fail with EACCES on some FS — fall
        // back to remove + rename.
        std::error_code rm_ec;
        fs::remove(path, rm_ec);
        fs::rename(tmp, path, ec);
        if (ec) return false;
    }
    return true;
}

bool read_json_file(const fs::path& path, nlohmann::json& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    try { in >> out; } catch (...) { return false; }
    return true;
}

nlohmann::json serialize_session_task(const ScheduledTask& t) {
    nlohmann::json j;
    j["id"]           = t.id;
    j["title"]        = t.title;
    j["created_at"]   = t.created_at;
    j["updated_at"]   = t.updated_at;
    j["memory_state"] = t.memory_state;
    j["events"]       = t.events;
    return j;
}

void deserialize_session_task(const nlohmann::json& j, ScheduledTask& t) {
    if (j.contains("id") && j["id"].is_string())             t.id = j["id"].get<std::string>();
    if (j.contains("title") && j["title"].is_string())       t.title = j["title"].get<std::string>();
    if (j.contains("created_at") && j["created_at"].is_string()) t.created_at = j["created_at"].get<std::string>();
    if (j.contains("updated_at") && j["updated_at"].is_string()) t.updated_at = j["updated_at"].get<std::string>();
    if (j.contains("memory_state") && j["memory_state"].is_object())
        t.memory_state = j["memory_state"];
    if (j.contains("events") && j["events"].is_array())
        t.events = j["events"].get<std::vector<nlohmann::json>>();
}

/// Persist a session entry to disk. Called while TaskStore mutex is held;
/// disk I/O failure is silently swallowed (best-effort).
void persist_session_locked(const ScheduledTask& t) {
    if (t.id.empty()) return;
    const std::string fid = sanitize_session_id(t.id);
    if (fid.empty()) return;
    const fs::path path = session_file_path_for(fid);
    (void) ensure_session_dir(path.parent_path());
    (void) atomic_write_json(path, serialize_session_task(t));
}

/// Try to materialize a session from disk into the in-memory cache.
/// Returns the inserted task pointer (or existing one if already cached).
ScheduledTask* try_load_session_into_cache_locked(TaskStore& s, const std::string& id) {
    auto it = s.sessions.find(id);
    if (it != s.sessions.end()) return &it->second;

    const std::string fid = sanitize_session_id(id);
    if (fid.empty()) return nullptr;
    const fs::path path = session_file_path_for(fid);
    std::error_code ec;
    if (!fs::exists(path, ec)) return nullptr;

    nlohmann::json j;
    if (!read_json_file(path, j)) return nullptr;

    ScheduledTask t;
    t.id    = id;
    t.title = "chat-session";
    deserialize_session_task(j, t);
    // Don't trust the file's id field over the caller's id.
    t.id = id;

    auto [ins, _ok] = s.sessions.emplace(id, std::move(t));
    return &ins->second;
}

bool debug_dumps_enabled() {
    if (const char* env = std::getenv("PM_IMAGE_DEBUG_DUMPS"); env && *env) {
        return std::string(env) != "0";
    }
    return false;
}

} // namespace

// ── Time helpers ─────────────────────────────────────────────────────────────

std::string now_iso8601() {
    auto      now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm   tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

static std::string tp_to_iso8601(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm   tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::optional<std::chrono::system_clock::time_point> parse_iso8601(const std::string& s) {
    // Accept "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|…]" — timezone part ignored (treated as UTC).
    if (s.size() < 19) return std::nullopt;
    std::tm tm{};
    std::istringstream ss(s.substr(0, 19));
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) return std::nullopt;
    tm.tm_isdst = 0;
#if defined(_WIN32)
    std::time_t t = _mkgmtime(&tm);
#else
    std::time_t t = timegm(&tm);
#endif
    if (t == static_cast<std::time_t>(-1)) return std::nullopt;
    return std::chrono::system_clock::from_time_t(t);
}

// ── Serialization ────────────────────────────────────────────────────────────

nlohmann::json task_to_json(const ScheduledTask& t) {
    nlohmann::json j;
    j["id"]                   = t.id;
    j["title"]                = t.title;
    j["prompt"]               = t.prompt;
    j["enabled"]              = t.enabled;
    j["created_at"]           = t.created_at;
    j["updated_at"]           = t.updated_at;
    j["run_count"]            = t.run_count;
    j["consecutive_failures"] = t.consecutive_failures;
    if (!t.last_error.empty())   j["last_error"]   = t.last_error;
    if (!t.folder_hint.empty())  j["folder_hint"]  = t.folder_hint;
    j["memory_state"]         = t.memory_state;

    nlohmann::json sched;
    switch (t.schedule.kind) {
        case ScheduleKind::At:    sched["kind"] = "at";    break;
        case ScheduleKind::In:    sched["kind"] = "in";    break;
        case ScheduleKind::Every: sched["kind"] = "every"; break;
    }
    if (t.schedule.interval) sched["interval_seconds"] = static_cast<int>(t.schedule.interval->count());
    if (t.schedule.delay)    sched["delay_seconds"]    = static_cast<int>(t.schedule.delay->count());
    if (t.schedule.max_runs) sched["max_runs"]         = *t.schedule.max_runs;
    if (t.schedule.run_at)   sched["run_at"]           = tp_to_iso8601(*t.schedule.run_at);
    if (t.schedule.start_at) sched["start_at"]         = tp_to_iso8601(*t.schedule.start_at);
    j["schedule"]             = std::move(sched);

    if (t.next_run_at) j["next_run_at"] = tp_to_iso8601(*t.next_run_at);
    if (t.last_run_at) j["last_run_at"] = tp_to_iso8601(*t.last_run_at);

    if (!t.events.empty()) {
        j["events"] = t.events;
    }
    return j;
}

nlohmann::json task_to_summary_json(const ScheduledTask& t) {
    nlohmann::json j;
    j["id"]      = t.id;
    j["title"]   = t.title;
    j["enabled"] = t.enabled;
    j["run_count"] = t.run_count;
    switch (t.schedule.kind) {
        case ScheduleKind::At:    j["schedule_kind"] = "at";    break;
        case ScheduleKind::In:    j["schedule_kind"] = "in";    break;
        case ScheduleKind::Every: j["schedule_kind"] = "every"; break;
    }
    if (t.schedule.interval) j["interval_seconds"] = static_cast<int>(t.schedule.interval->count());
    if (t.next_run_at)       j["next_run_at"]      = tp_to_iso8601(*t.next_run_at);
    if (t.last_run_at)       j["last_run_at"]      = tp_to_iso8601(*t.last_run_at);
    if (!t.last_error.empty() && t.consecutive_failures > 0) j["last_error"] = t.last_error;
    return j;
}

// ── Optional CWD debug dump ─────────────────────────────────────────────────
// Off by default — historically these files leaked into every CLI invocation's
// working directory. Re-enable via PM_IMAGE_DEBUG_DUMPS=1 for local debugging.
// Session memory now persists via per-session files under session_store_dir().

static void dump_debug_files_locked(const TaskStore& s) {
    if (!debug_dumps_enabled()) return;

    nlohmann::json sched_arr = nlohmann::json::array();
    for (const auto& kv : s.tasks)
        sched_arr.push_back(task_to_json(kv.second));
    try { std::ofstream("scheduler.json") << sched_arr.dump(2); } catch (...) {}

    nlohmann::json mem_arr = nlohmann::json::array();
    for (const auto& kv : s.sessions) {
        const ScheduledTask& t = kv.second;
        nlohmann::json mj;
        mj["id"]           = t.id;
        mj["memory_state"] = t.memory_state;
        mj["events"]       = t.events;
        mem_arr.push_back(std::move(mj));
    }
    try { std::ofstream("memory.json") << mem_arr.dump(2); } catch (...) {}
}

// ── Compute initial next_run_at from spec ────────────────────────────────────

static std::optional<std::chrono::system_clock::time_point>
compute_initial_next_run(const ScheduleSpec& spec) {
    const auto now = std::chrono::system_clock::now();
    switch (spec.kind) {
        case ScheduleKind::At:
            return spec.run_at;
        case ScheduleKind::In:
            if (spec.delay) return now + *spec.delay;
            return std::nullopt;
        case ScheduleKind::Every:
            if (spec.start_at) return *spec.start_at;
            if (spec.interval) return now + *spec.interval;
            return std::nullopt;
    }
    return std::nullopt;
}

// ── Store API ────────────────────────────────────────────────────────────────

std::string task_store_create(ScheduledTask task) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);

    if (task.id.empty())
        task.id = gen_id(task.title);

    const std::string now = now_iso8601();
    task.created_at = now;
    task.updated_at = now;

    if (!task.next_run_at)
        task.next_run_at = compute_initial_next_run(task.schedule);

    std::string id = task.id;
    s.tasks[id] = std::move(task);
    dump_debug_files_locked(s);
    return id;
}

std::optional<ScheduledTask> task_store_get(const std::string& id) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);
    // Scheduler tasks first, then in-memory sessions, then disk-backed sessions.
    auto it = s.tasks.find(id);
    if (it != s.tasks.end()) return it->second;
    auto sit = s.sessions.find(id);
    if (sit != s.sessions.end()) return sit->second;
    if (auto* loaded = try_load_session_into_cache_locked(s, id))
        return *loaded;
    return std::nullopt;
}

std::vector<ScheduledTask> task_store_list() {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);
    std::vector<ScheduledTask> out;
    out.reserve(s.tasks.size());
    for (const auto& kv : s.tasks)
        out.push_back(kv.second);
    return out;
}

bool task_store_update(const ScheduledTask& task) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);
    auto it = s.tasks.find(task.id);
    if (it == s.tasks.end()) return false;
    it->second             = task;
    it->second.updated_at  = now_iso8601();
    dump_debug_files_locked(s);
    return true;
}

bool task_store_cancel(const std::string& id) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);
    auto it = s.tasks.find(id);
    if (it == s.tasks.end()) return false;
    it->second.enabled    = false;
    it->second.updated_at = now_iso8601();
    dump_debug_files_locked(s);
    return true;
}

void task_store_clear_all() {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);
    s.tasks.clear();
    dump_debug_files_locked(s);
}

bool task_store_write_memory(const std::string& task_id, const nlohmann::json& state) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);
    auto it = s.tasks.find(task_id);
    if (it != s.tasks.end()) {
        it->second.memory_state = state;
        it->second.updated_at   = now_iso8601();
        dump_debug_files_locked(s);
        return true;
    }
    // Session — fall through to disk if not cached.
    ScheduledTask* st = nullptr;
    auto sit = s.sessions.find(task_id);
    if (sit != s.sessions.end()) {
        st = &sit->second;
    } else {
        st = try_load_session_into_cache_locked(s, task_id);
    }
    if (!st) return false;
    st->memory_state = state;
    st->updated_at   = now_iso8601();
    persist_session_locked(*st);
    dump_debug_files_locked(s);
    return true;
}

void task_store_append_event(const std::string& task_id, const nlohmann::json& event) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);

    auto it = s.tasks.find(task_id);
    if (it != s.tasks.end()) {
        auto& evs = it->second.events;
        evs.push_back(event);
        if (evs.size() > k_max_events)
            evs.erase(evs.begin(), evs.begin() + static_cast<std::ptrdiff_t>(evs.size() - k_max_events));
        it->second.updated_at = now_iso8601();
        dump_debug_files_locked(s);
        return;
    }

    ScheduledTask* sess = nullptr;
    auto sit = s.sessions.find(task_id);
    if (sit != s.sessions.end()) {
        sess = &sit->second;
    } else {
        sess = try_load_session_into_cache_locked(s, task_id);
    }
    if (!sess) return;
    auto& evs = sess->events;
    evs.push_back(event);
    if (evs.size() > k_max_events)
        evs.erase(evs.begin(), evs.begin() + static_cast<std::ptrdiff_t>(evs.size() - k_max_events));
    sess->updated_at = now_iso8601();
    persist_session_locked(*sess);
    dump_debug_files_locked(s);
}

// ── Per-turn task context ────────────────────────────────────────────────────

void        set_current_task_id(const std::string& id) { tl_current_task_id = id; }
void        clear_current_task_id()                     { tl_current_task_id.clear(); }
std::string get_current_task_id()                       { return tl_current_task_id; }

// ── Session memory ────────────────────────────────────────────────────────────
// Sessions live in TaskStore::sessions — completely separate from the scheduler's
// TaskStore::tasks map. They are never iterated by the scheduler and never appear
// in scheduler.json.

// Generated session ids include a millisecond timestamp so that concurrent
// process launches (the chat panel + a CLI invocation, two CLI invocations,
// or the same app started twice quickly) don't collide on the same disk file.
static std::string generate_unique_session_id(int seq) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ostringstream oss;
    oss << "session-" << std::hex << ms << std::dec << '-' << seq;
    return oss.str();
}

std::string session_create() {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);

    ScheduledTask st;
    st.id         = generate_unique_session_id(s.next_seq++);
    st.title      = "chat-session";
    st.created_at = now_iso8601();
    st.updated_at = st.created_at;

    const std::string id = st.id;
    auto [it, _ok] = s.sessions.emplace(id, std::move(st));
    persist_session_locked(it->second);
    return id;
}

std::string session_get_or_create(const std::string& preferred_id) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);

    if (!preferred_id.empty()) {
        // Already cached in memory?
        auto it = s.sessions.find(preferred_id);
        if (it != s.sessions.end())
            return it->first;
        // Pull from disk into the cache if a file exists.
        if (auto* loaded = try_load_session_into_cache_locked(s, preferred_id))
            return loaded->id;

        // Brand new session — create + persist.
        ScheduledTask st;
        st.id         = preferred_id;
        st.title      = "chat-session";
        st.created_at = now_iso8601();
        st.updated_at = st.created_at;
        auto [ins, _ok] = s.sessions.emplace(preferred_id, std::move(st));
        persist_session_locked(ins->second);
        return preferred_id;
    }

    ScheduledTask st;
    st.id         = generate_unique_session_id(s.next_seq++);
    st.title      = "chat-session";
    st.created_at = now_iso8601();
    st.updated_at = st.created_at;
    const std::string id = st.id;
    auto [ins, _ok] = s.sessions.emplace(id, std::move(st));
    persist_session_locked(ins->second);
    return id;
}

void session_load(const std::string& session_id,
                  nlohmann::json& out_memory_state,
                  nlohmann::json& out_recent_events,
                  size_t max_events)
{
    out_memory_state  = nlohmann::json::object();
    out_recent_events = nlohmann::json::array();

    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);

    auto it = s.sessions.find(session_id);
    ScheduledTask* src = (it != s.sessions.end()) ? &it->second
                                                  : try_load_session_into_cache_locked(s, session_id);
    if (!src) return;

    out_memory_state = src->memory_state;

    // Return the last max_events entries (oldest-first so the LLM reads in order).
    const auto& evs = src->events;
    const size_t start = (evs.size() > max_events) ? evs.size() - max_events : 0;
    for (size_t i = start; i < evs.size(); ++i)
        out_recent_events.push_back(evs[i]);
}

void session_append_turn(const std::string& session_id, const nlohmann::json& entry) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);

    auto it = s.sessions.find(session_id);
    ScheduledTask* sess = (it != s.sessions.end()) ? &it->second
                                                   : try_load_session_into_cache_locked(s, session_id);
    if (!sess) return;

    auto& evs = sess->events;
    evs.push_back(entry);

    // Evict oldest entries beyond the session ring-buffer limit.
    if (evs.size() > pm::llm::k_session_memory_max_turns)
        evs.erase(evs.begin(),
                  evs.begin() + static_cast<std::ptrdiff_t>(
                      evs.size() - pm::llm::k_session_memory_max_turns));

    sess->updated_at = now_iso8601();
    persist_session_locked(*sess);
    dump_debug_files_locked(s);
}

// ── Session-disk public API ─────────────────────────────────────────────────

std::filesystem::path session_store_dir() {
    std::lock_guard<std::mutex> lk(g_session_dir_mutex);
    const fs::path dir = resolve_session_store_dir_unlocked();
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

void set_session_store_dir(const std::filesystem::path& dir) {
    std::lock_guard<std::mutex> lk(g_session_dir_mutex);
    if (dir.empty()) g_session_dir_override.reset();
    else             g_session_dir_override = dir;
}

std::string sanitize_session_id(const std::string& id) {
    std::string out;
    out.reserve(id.size());
    for (unsigned char c : id) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    // Reject path-traversal — collapse leading dots and any ".." segments.
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    // Replace any literal ".." with "__".
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        if (out[i] == '.' && out[i + 1] == '.') {
            out[i]     = '_';
            out[i + 1] = '_';
        }
    }
    if (out.empty()) out = "session";
    if (out.size() > 80) out.resize(80);
    return out;
}

std::vector<std::string> session_list_persisted() {
    std::vector<std::string> out;
    const fs::path dir = session_store_dir();
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const fs::path& p = entry.path();
        if (p.extension() == ".json")
            out.push_back(p.stem().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool session_delete(const std::string& session_id) {
    auto& s = store();
    std::unique_lock<std::mutex> lk(s.mutex);

    bool dropped = false;
    auto it = s.sessions.find(session_id);
    if (it != s.sessions.end()) {
        s.sessions.erase(it);
        dropped = true;
    }

    const std::string fid = sanitize_session_id(session_id);
    if (!fid.empty()) {
        const fs::path path = session_file_path_for(fid);
        std::error_code ec;
        if (fs::remove(path, ec) || dropped)
            return true;
    }
    return dropped;
}

} // namespace media::llm::agent
