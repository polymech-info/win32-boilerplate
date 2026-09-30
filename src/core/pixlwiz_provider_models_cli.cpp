#include "pixlwiz_provider_models_cli.hpp"

#include "logger/logger.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/settings_store.hpp"

namespace media::pixlwiz_cli {
namespace {

static std::filesystem::path cache_path() {
    if (const char* p = std::getenv("POLYMECH_PIXLWIZ_MODELS_CACHE")) {
        if (p[0]) return std::filesystem::path(p);
    }
    return media::settings::get_config_dir() / "pixlwiz-models-cache.json";
}

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

static void log_pw(const std::string& line) { logger::info(std::string("pixlwiz_models: ") + line); }
static void log_pw_debug(const std::string& line) { logger::debug(std::string("pixlwiz_models: ") + line); }

static std::int64_t now_epoch_seconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

static std::int64_t file_mtime_epoch(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return 0;
    const auto ft = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

static std::optional<std::int64_t> json_fetched_at_epoch(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("fetched_at")) return std::nullopt;
    const auto& f = j["fetched_at"];
    if (f.is_number_integer()) return f.get<std::int64_t>();
    if (f.is_number_unsigned()) return static_cast<std::int64_t>(f.get<std::uint64_t>());
    if (f.is_number_float()) return static_cast<std::int64_t>(f.get<double>());
    if (f.is_string()) {
        try {
            return static_cast<std::int64_t>(std::stoll(f.get_ref<const std::string&>()));
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

static bool cache_is_fresh(const nlohmann::json& j, std::int64_t file_mtime_epoch_sec) {
    if (!j.is_object()) return false;
    std::int64_t ts = 0;
    if (const std::optional<std::int64_t> t = json_fetched_at_epoch(j)) {
        ts = *t;
    } else if (file_mtime_epoch_sec > 0) {
        ts = file_mtime_epoch_sec;
    } else {
        return false;
    }
    const std::int64_t now = now_epoch_seconds();
    std::int64_t       age = now - ts;
    if (age < 0) {
        if (age < -3600) return false;
        age = 0;
    }
    return age <= kPixlWizModelsCacheTtlSeconds;
}

static std::mutex     g_pw_cache_mu;
static nlohmann::json g_pw_cache_mem;
static bool           g_pw_mem_ready  = false;
static bool           g_pw_load_ok    = false;
static std::int64_t   g_pw_file_mtime = 0;

static std::string strip_trailing_slashes(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

static std::string models_endpoint_url(const std::string& base_in) {
    const std::string b = strip_trailing_slashes(base_in);
    std::string url;
    if (b.empty()) {
        url = "https://llm.polymech.info/models";
    } else if (b.find("/models") != std::string::npos) {
        url = b;
    } else {
        // bare host or host+path — append /models
        url = b + "/models";
    }
    // Append query parameters for extended model metadata
    url += "?return_wildcard_routes=true&include_model_access_groups=false&only_model_access_groups=false&include_metadata=true";
    return url;
}

static bool load_cache(nlohmann::json& j) {
    j = nlohmann::json::object();
    std::lock_guard<std::mutex> lock(g_pw_cache_mu);
    if (g_pw_mem_ready) {
        j = g_pw_cache_mem;
        return g_pw_load_ok;
    }
    g_pw_file_mtime = 0;
    g_pw_cache_mem  = nlohmann::json::object();
    g_pw_load_ok    = false;
    const auto p = cache_path();
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        log_pw_debug("cache cold read: file missing at " + p.string());
        g_pw_mem_ready = true;
        j              = g_pw_cache_mem;
        return false;
    }
    g_pw_file_mtime = file_mtime_epoch(p);
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        log_pw_debug("cache cold read: cannot open " + p.string());
        g_pw_mem_ready = true;
        j              = g_pw_cache_mem;
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (raw.empty()) {
        g_pw_mem_ready = true;
        j              = g_pw_cache_mem;
        return false;
    }
    try {
        nlohmann::json parsed = nlohmann::json::parse(raw);
        if (!parsed.is_object()) {
            g_pw_mem_ready = true;
            j              = g_pw_cache_mem;
            return false;
        }
        g_pw_cache_mem     = std::move(parsed);
        g_pw_load_ok       = true;
        g_pw_mem_ready     = true;
        j                  = g_pw_cache_mem;
    } catch (const std::exception& e) {
        log_pw_debug(std::string("cache cold read: parse error (") + e.what() + ") — " + p.string());
        g_pw_mem_ready = true;
        j              = g_pw_cache_mem;
        return false;
    } catch (...) {
        g_pw_mem_ready = true;
        j              = g_pw_cache_mem;
        return false;
    }
    {
        const bool        fresh     = cache_is_fresh(g_pw_cache_mem, g_pw_file_mtime);
        std::ostringstream o;
        o << "cache cold read: ok path=" << p.string() << " fresh=" << (fresh ? 1 : 0);
        log_pw(o.str());
    }
    return true;
}

static void save_cache(const nlohmann::json& j) {
    if (!j.is_object()) return;
    std::lock_guard<std::mutex> lock(g_pw_cache_mu);
    g_pw_cache_mem  = j;
    g_pw_mem_ready  = true;
    g_pw_load_ok    = true;
    const auto p = cache_path();
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        log_pw_debug("cache write: failed to open for write — " + p.string());
        return;
    }
    const std::string raw = j.dump(2);
    ofs.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    ofs.flush();
    g_pw_file_mtime = file_mtime_epoch(p);
    log_pw_debug("cache write: " + p.string() + " bytes=" + std::to_string(raw.size()));
}

/// Normalize `{"data": [...]}` or `{"models": [...]}` or bare array → `{"data": [...]}`.
static nlohmann::json normalize_pixlwiz_models_body(nlohmann::json j) {
    if (j.is_array()) return nlohmann::json{{"data", std::move(j)}};
    if (!j.is_object()) return nlohmann::json::object();
    if (j.contains("data") && j["data"].is_array()) {
        nlohmann::json out;
        out["data"] = std::move(j["data"]);
        return out;
    }
    if (j.contains("models") && j["models"].is_array()) {
        nlohmann::json out;
        out["data"] = std::move(j["models"]);
        return out;
    }
    return j;
}

} // namespace

bool list_models_pixlwiz_http(const std::string& api_key,
                               const std::string& base_url,
                               std::string&       json_out,
                               std::string&       err_out,
                               bool               force_refresh) {
    json_out.clear();
    err_out.clear();

    const std::string url = models_endpoint_url(base_url);

    if (!force_refresh) {
        nlohmann::json cache_j;
        if (load_cache(cache_j)) {
            const bool fresh = cache_is_fresh(cache_j, g_pw_file_mtime);
            if (fresh && cache_j.contains("data") && cache_j["data"].is_array()) {
                nlohmann::json out;
                out["data"] = cache_j["data"];
                json_out    = out.dump(2);
                log_pw("list_models: source=disk_cache url=" + url + " n=" + std::to_string(cache_j["data"].size()));
                return true;
            }
        }
    }

    if (api_key.empty()) {
        err_out = "PixlWiz: API key required (set in App Provider Settings or pass --api-key)";
        log_pw("list_models: abort — no API key url=" + url);

        // Return stale cache rather than a hard error when we have something.
        nlohmann::json cache_j;
        load_cache(cache_j);
        if (cache_j.is_object() && cache_j.contains("data") && cache_j["data"].is_array() && !cache_j["data"].empty()) {
            nlohmann::json out;
            out["data"] = cache_j["data"];
            json_out    = out.dump(2);
            err_out.clear();
            log_pw("list_models: source=stale_cache_no_key url=" + url);
            return true;
        }
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "PixlWiz: curl init failed";
        return false;
    }

    std::string response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    const std::string auth = "Authorization: Bearer " + api_key;
    hdrs = curl_slist_append(hdrs, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pm-image/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    const CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err_out = std::string("PixlWiz: HTTP error: ") + curl_easy_strerror(cc);
        log_pw("list_models: curl failed url=" + url + " — " + err_out);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        err_out = "PixlWiz: HTTP " + std::to_string(http_code);
        if (response.size() < 1500) err_out += ": " + response;
        log_pw("list_models: http error url=" + url + " — " + err_out);
        return false;
    }
    if (response.empty()) {
        err_out = "PixlWiz: empty response body";
        log_pw("list_models: empty body url=" + url);
        return false;
    }

    try {
        nlohmann::json parsed  = nlohmann::json::parse(response);
        nlohmann::json normed  = normalize_pixlwiz_models_body(std::move(parsed));
        if (normed.is_object() && normed.contains("data") && normed["data"].is_array()) {
            nlohmann::json to_cache = normed;
            to_cache["fetched_at"]  = now_epoch_seconds();
            save_cache(to_cache);
        }
        json_out = normed.dump(2);
    } catch (const std::exception& e) {
        err_out = std::string("PixlWiz: JSON parse error: ") + e.what();
        return false;
    }

    log_pw("list_models: source=http url=" + url + " bytes=" + std::to_string(json_out.size()));
    return true;
}

bool list_pixlwiz_catalog_models(const std::string&             api_key,
                                  const std::string&             base_url,
                                  std::vector<PixlWizModelRow>&  out,
                                  std::string&                   err_out,
                                  bool                           force_refresh) {
    out.clear();
    err_out.clear();
    std::string raw;
    if (!list_models_pixlwiz_http(api_key, base_url, raw, err_out, force_refresh))
        return false;

    try {
        const nlohmann::json root = nlohmann::json::parse(raw);
        const nlohmann::json* data = nullptr;
        if (root.is_object() && root.contains("data") && root["data"].is_array()) data = &root["data"];
        if (!data || data->empty()) {
            err_out = "PixlWiz: model list is empty";
            return false;
        }
        for (const auto& el : *data) {
            if (!el.is_object()) continue;
            PixlWizModelRow row;
            if (el.contains("id") && el["id"].is_string()) row.id = el["id"].get<std::string>();
            if (row.id.empty()) continue;
            if (el.contains("name") && el["name"].is_string()) row.name = el["name"].get<std::string>();
            if (row.name.empty()) row.name = row.id;
            if (el.contains("description") && el["description"].is_string()) row.description = el["description"].get<std::string>();
            if (el.contains("url") && el["url"].is_string()) row.open_url = el["url"].get<std::string>();
            out.push_back(std::move(row));
        }
        std::sort(out.begin(), out.end(), [](const PixlWizModelRow& a, const PixlWizModelRow& b) {
            return a.name < b.name;
        });
    } catch (const std::exception& e) {
        err_out = std::string("PixlWiz: parse error: ") + e.what();
        return false;
    }

    if (out.empty()) {
        err_out = "PixlWiz: no valid models found in response";
        return false;
    }
    return true;
}

} // namespace media::pixlwiz_cli
