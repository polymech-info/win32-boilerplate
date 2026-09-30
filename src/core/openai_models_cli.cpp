#include "openai_models_cli.hpp"

#include "logger/logger.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/settings_store.hpp"

namespace media::openai_cli {
namespace {

static std::filesystem::path cache_path()
{
    if (const char* p = std::getenv("POLYMECH_OPENAI_MODELS_CACHE")) {
        if (p[0]) return std::filesystem::path(p);
    }
    return media::settings::get_config_dir() / "openai-models-cache.json";
}

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud)
{
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

static void log_oai(const std::string& line)       { logger::info(std::string("openai_models: ") + line); }
static void log_oai_debug(const std::string& line)  { logger::debug(std::string("openai_models: ") + line); }

static std::int64_t now_epoch_seconds()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

static std::int64_t file_mtime_epoch(const std::filesystem::path& p)
{
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return 0;
    const auto ft   = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

static std::optional<std::int64_t> json_fetched_at(const nlohmann::json& j)
{
    if (!j.is_object() || !j.contains("fetched_at")) return std::nullopt;
    const auto& f = j["fetched_at"];
    if (f.is_number_integer())  return f.get<std::int64_t>();
    if (f.is_number_unsigned()) return static_cast<std::int64_t>(f.get<std::uint64_t>());
    if (f.is_number_float())    return static_cast<std::int64_t>(f.get<double>());
    if (f.is_string()) {
        try { return static_cast<std::int64_t>(std::stoll(f.get_ref<const std::string&>())); }
        catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

static bool cache_is_fresh(const nlohmann::json& j, std::int64_t file_mtime)
{
    if (!j.is_object()) return false;
    std::int64_t ts = 0;
    if (const auto t = json_fetched_at(j)) ts = *t;
    else if (file_mtime > 0)               ts = file_mtime;
    else                                    return false;
    const std::int64_t now = now_epoch_seconds();
    std::int64_t       age = now - ts;
    if (age < 0) { if (age < -3600) return false; age = 0; }
    return age <= kOpenAIModelsCacheTtlSeconds;
}

static std::mutex     g_oai_mu;
static nlohmann::json g_oai_cache;
static bool           g_oai_ready    = false;
static bool           g_oai_load_ok  = false;
static std::int64_t   g_oai_mtime    = 0;

static std::string strip_slashes(std::string s)
{
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

static std::string models_url(const std::string& base_in)
{
    const std::string b = strip_slashes(base_in);
    if (b.empty()) return "https://api.openai.com/v1/models";
    if (b.find("/models") != std::string::npos) return b;
    if (b.size() >= 3 && b.compare(b.size() - 3, 3, "/v1") == 0) return b + "/models";
    return b + "/models";
}

static bool load_cache(nlohmann::json& j)
{
    j = nlohmann::json::object();
    std::lock_guard<std::mutex> lock(g_oai_mu);
    if (g_oai_ready) {
        j = g_oai_cache;
        return g_oai_load_ok;
    }
    g_oai_mtime   = 0;
    g_oai_cache   = nlohmann::json::object();
    g_oai_load_ok = false;
    const auto p = cache_path();
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        log_oai_debug("cache cold: missing " + p.string());
        g_oai_ready = true;
        j = g_oai_cache;
        return false;
    }
    g_oai_mtime = file_mtime_epoch(p);
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        log_oai_debug("cache cold: cannot open " + p.string());
        g_oai_ready = true;
        j = g_oai_cache;
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (raw.empty()) { g_oai_ready = true; j = g_oai_cache; return false; }
    try {
        nlohmann::json parsed = nlohmann::json::parse(raw);
        if (!parsed.is_object()) { g_oai_ready = true; j = g_oai_cache; return false; }
        g_oai_cache   = std::move(parsed);
        g_oai_load_ok = true;
        g_oai_ready   = true;
        j             = g_oai_cache;
        log_oai("cache cold: ok path=" + p.string());
    } catch (...) {
        g_oai_ready = true;
        j = g_oai_cache;
        return false;
    }
    return true;
}

static void save_cache(const nlohmann::json& data_array)
{
    nlohmann::json stored;
    stored["data"]       = data_array;
    stored["fetched_at"] = now_epoch_seconds();
    {
        std::lock_guard<std::mutex> lock(g_oai_mu);
        g_oai_cache   = stored;
        g_oai_load_ok = true;
        g_oai_ready   = true;
        g_oai_mtime   = now_epoch_seconds();
    }
    const auto p = cache_path();
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs) { log_oai_debug("cache write: failed to open " + p.string()); return; }
    ofs << stored.dump();
    if (ofs.good())
        log_oai("cache write: " + p.string() + " n=" + std::to_string(data_array.size()));
    else
        log_oai_debug("cache write: short write " + p.string());
}

} // namespace

bool list_models_openai_http(const std::string& api_key,
                              const std::string& base_url,
                              std::string&       json_out,
                              std::string&       err_out,
                              const bool         force_refresh)
{
    json_out.clear();
    err_out.clear();
    const std::string url = models_url(base_url);

    if (!force_refresh) {
        nlohmann::json cache_j;
        if (load_cache(cache_j) && cache_is_fresh(cache_j, g_oai_mtime)) {
            if (cache_j.contains("data") && cache_j["data"].is_array() && !cache_j["data"].empty()) {
                json_out = nlohmann::json{{"data", cache_j["data"]}}.dump();
                log_oai("source=disk_cache n=" + std::to_string(cache_j["data"].size()));
                return true;
            }
        }
        log_oai_debug("cache miss/stale, fetching url=" + url);
    }

    CURL* curl = curl_easy_init();
    if (!curl) { err_out = "openai models: curl init failed"; return false; }

    std::string        response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    if (!api_key.empty()) {
        const std::string auth = "Authorization: Bearer " + api_key;
        hdrs = curl_slist_append(hdrs, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "pm-image (openai model catalog)");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    const CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err_out = std::string("openai models: ") + curl_easy_strerror(cc);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        err_out = "openai models: HTTP " + std::to_string(http_code);
        if (response.size() < 1500) err_out += ": " + response;
        return false;
    }
    if (response.empty()) { err_out = "openai models: empty response"; return false; }

    try {
        nlohmann::json j = nlohmann::json::parse(response);
        // OpenAI returns {"object":"list","data":[...]}
        if (!j.is_object() || !j.contains("data") || !j["data"].is_array()) {
            err_out = "openai models: unexpected response shape (expected {\"data\":[...]})";
            if (response.size() < 400) err_out += " body=" + response;
            return false;
        }
        save_cache(j["data"]);
        json_out = nlohmann::json{{"data", j["data"]}}.dump();
        log_oai("source=http n=" + std::to_string(j["data"].size()));
    } catch (const std::exception& e) {
        err_out = std::string("openai models: JSON parse: ") + e.what();
        return false;
    }
    return true;
}

bool list_openai_models(const std::string&         api_key,
                        const std::string&         base_url,
                        std::vector<OpenAIModelRow>& out,
                        std::string&               err_out,
                        const bool                 force_refresh)
{
    out.clear();
    err_out.clear();
    std::string json;
    if (!list_models_openai_http(api_key, base_url, json, err_out, force_refresh)) return false;
    nlohmann::json root;
    try { root = nlohmann::json::parse(json); }
    catch (const std::exception& e) { err_out = std::string("openai models: ") + e.what(); return false; }
    if (!root.is_object() || !root.contains("data") || !root["data"].is_array()) {
        err_out = "openai models: no data array";
        return false;
    }
    for (const auto& el : root["data"]) {
        if (!el.is_object() || !el.contains("id") || !el["id"].is_string()) continue;
        OpenAIModelRow row;
        row.id       = el["id"].get<std::string>();
        row.owned_by = el.contains("owned_by") && el["owned_by"].is_string()
                           ? el["owned_by"].get<std::string>()
                           : std::string{};
        out.push_back(std::move(row));
    }
    std::sort(out.begin(), out.end(), [](const OpenAIModelRow& a, const OpenAIModelRow& b) {
        return a.id < b.id;
    });
    return true;
}

} // namespace media::openai_cli
