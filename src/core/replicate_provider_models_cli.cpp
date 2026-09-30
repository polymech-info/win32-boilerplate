#include "replicate_provider_models_cli.hpp"

#include "logger/logger.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <optional>
#include <unordered_set>

#include "app_image_provider.hpp"
#include "core/settings_store.hpp"

namespace media::replicate_cli {
namespace {

static std::filesystem::path cache_path() {
    if (const char* p = std::getenv("POLYMECH_REPLICATE_MODELS_CACHE")) {
        if (p[0]) return std::filesystem::path(p);
    }
    return media::settings::get_config_dir() / "replicate-models-cache.json";
}

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string short_body(std::string s) {
    for (char& c : s) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    if (s.size() <= 220) return s;
    return s.substr(0, 220) + "...";
}

static void log_rep(const std::string& line) { logger::trace(std::string("replicate_models: ") + line); }
static void log_rep_debug(const std::string& line) { logger::trace(std::string("replicate_models: ") + line); }

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

/// JSON may store epoch seconds as int, uint, or float; nlohmann can yield number_float.
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
    // Clock skew: treat small negative age (timestamp slightly in the future) as 0.
    const std::int64_t now = now_epoch_seconds();
    std::int64_t       age = now - ts;
    if (age < 0) {
        if (age < -3600) return false; // more than 1h "in the future" — ignore as bogus
        age = 0;
    }
    return age <= kReplicateModelsCacheTtlSeconds;
}

// Single in-process mirror of replicate-models-cache.json. After the first disk parse we keep a
// `std::shared_ptr<const json>` so `fetch_collections` / `fetch_collection_models` can read the cache
// thousands of times per session without deep-copying a multi‑MB tree on every call (startup was
// dominated by repeated `j = g_replicate_cache_mem` copies).
static std::mutex                              g_replicate_cache_mu;
static std::shared_ptr<const nlohmann::json>   g_replicate_cache_sp;
static bool                                    g_replicate_cache_mem_ready = false;
static bool                                    g_replicate_cache_load_ok   = false;
static std::int64_t                            g_replicate_cache_file_mtime = 0;

static void ensure_replicate_cache_loaded_unlocked()
{
    if (g_replicate_cache_mem_ready)
        return;

    const auto p = cache_path();
    g_replicate_cache_file_mtime = 0;
    g_replicate_cache_sp.reset();
    g_replicate_cache_load_ok = false;

    const auto mark_done = [&](bool load_ok) {
        g_replicate_cache_mem_ready = true;
        g_replicate_cache_load_ok   = load_ok;
    };

    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        log_rep_debug("cache cold read: file missing at " + p.string());
        mark_done(false);
        return;
    }
    g_replicate_cache_file_mtime = file_mtime_epoch(p);
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        log_rep_debug("cache cold read: cannot open " + p.string());
        mark_done(false);
        return;
    }
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (raw.empty()) {
        log_rep_debug("cache cold read: empty file " + p.string());
        mark_done(false);
        return;
    }
    try {
        nlohmann::json parsed = nlohmann::json::parse(raw);
        if (!parsed.is_object()) {
            log_rep_debug("cache cold read: root is not an object — " + p.string());
            mark_done(false);
            return;
        }
        g_replicate_cache_sp        = std::make_shared<const nlohmann::json>(std::move(parsed));
        g_replicate_cache_load_ok   = true;
        g_replicate_cache_mem_ready = true;
    } catch (const std::exception& e) {
        log_rep_debug(std::string("cache cold read: parse error (") + e.what() + ") — " + p.string());
        mark_done(false);
        return;
    } catch (...) {
        log_rep_debug("cache cold read: parse error (non-exception) — " + p.string());
        mark_done(false);
        return;
    }
    if (g_replicate_cache_sp) {
        const bool fresh = cache_is_fresh(*g_replicate_cache_sp, g_replicate_cache_file_mtime);
        std::ostringstream o;
        o << "cache cold read: ok path=" << p.string() << " fresh=" << (fresh ? 1 : 0)
          << " has_fetched_at=" << (g_replicate_cache_sp->contains("fetched_at") ? 1 : 0);
        log_rep(o.str());
    }
}

/// Caller may use the returned `shared_ptr` (and `const nlohmann::json&` through it) without copying
/// the full cache tree. `out_file_mtime` / `out_load_ok` optional.
static std::shared_ptr<const nlohmann::json> replicate_cache_snapshot(std::int64_t* out_file_mtime = nullptr,
    bool* out_load_ok = nullptr)
{
    std::lock_guard<std::mutex> lock(g_replicate_cache_mu);
    ensure_replicate_cache_loaded_unlocked();
    if (out_file_mtime) *out_file_mtime = g_replicate_cache_file_mtime;
    if (out_load_ok) *out_load_ok = g_replicate_cache_load_ok;
    return g_replicate_cache_sp;
}

static void save_cache(const nlohmann::json& j) {
    if (!j.is_object()) return;
    std::lock_guard<std::mutex> lock(g_replicate_cache_mu);
    g_replicate_cache_sp          = std::make_shared<const nlohmann::json>(j);
    g_replicate_cache_mem_ready   = true;
    g_replicate_cache_load_ok     = true;
    const auto p = cache_path();
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        log_rep_debug("cache write: failed to open for write — " + p.string());
        return;
    }
    const std::string raw = j.dump(2);
    ofs.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    ofs.flush();
    g_replicate_cache_file_mtime = file_mtime_epoch(p);
    log_rep_debug("cache write: " + p.string() + " bytes=" + std::to_string(raw.size()));
}

static bool http_get_json(const std::string& api_key,
                          const std::string& url,
                          nlohmann::json& out_json,
                          std::string& err_out) {
    out_json = nlohmann::json::object();
    err_out.clear();
    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "curl init failed";
        return false;
    }

    std::string resp;
    struct curl_slist* hdrs = nullptr;
    const std::string auth = "Authorization: Bearer " + api_key;
    hdrs = curl_slist_append(hdrs, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pm-image/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);

    const CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err_out = std::string("curl error: ") + curl_easy_strerror(cc);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        err_out = "http " + std::to_string(http_code);
        if (!resp.empty()) err_out += ", body: " + short_body(resp);
        return false;
    }
    try {
        out_json = nlohmann::json::parse(resp);
    } catch (const std::exception& e) {
        err_out = std::string("JSON parse error: ") + e.what();
        return false;
    }
    return true;
}

static std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// When the cache was populated with `models_by_collection` only (e.g. `fetch_collection_models` /
/// provider_model combo) and never with `collections` from `GET /v1/collections`, we still need
/// a collection list for the UI. Build a minimal list from cached keys so `fetch_collections` does
/// not always fall through to a network `http_get_json` in that case.
static nlohmann::json collection_results_from_model_keys(const nlohmann::json& models_by) {
    nlohmann::json results = nlohmann::json::array();
    if (!models_by.is_object()) return results;
    std::vector<std::string> slugs;
    slugs.reserve(models_by.size());
    for (auto it = models_by.begin(); it != models_by.end(); ++it) {
        if (!it.value().is_array()) continue;
        slugs.push_back(it.key());
    }
    std::sort(slugs.begin(), slugs.end(), [](const std::string& a, const std::string& b) {
        return to_lower(a) < to_lower(b);
    });
    for (const auto& s : slugs) {
        nlohmann::json row;
        row["slug"] = s;
        row["name"] = s;
        results.push_back(std::move(row));
    }
    return results;
}

/// Matches CLI: app settings providers + active provider (no environment).
static std::string resolve_replicate_api_key(const std::string& in) {
    if (!in.empty()) return in;
    std::string k;
    std::string bu;
    media::fill_image_provider_credentials_from_app("replicate", false, k, bu);
    return k;
}

} // namespace

static std::string url_encode_query_part(const std::string& in) {
    CURL* c = curl_easy_init();
    if (!c) return in;
    char* enc = curl_easy_escape(c, in.c_str(), static_cast<int>(in.size()));
    std::string out = enc ? enc : in;
    if (enc) curl_free(enc);
    curl_easy_cleanup(c);
    return out;
}

std::string normalize_replicate_base_url(const std::string& base_url) {
    std::string base = base_url;
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    if (base.empty()) return "https://api.replicate.com/v1";
    return base;
}

bool fetch_collections(const std::string& api_key,
                       const std::string& base_url,
                       std::vector<CollectionInfo>& out_collections,
                       std::string& err_out,
                       bool force_refresh) 
{
    out_collections.clear();
    err_out.clear();
    const std::string key = resolve_replicate_api_key(api_key);
    std::int64_t      file_ep = 0;
    bool              cache_load_ok = false;
    std::shared_ptr<const nlohmann::json> cache_sp = replicate_cache_snapshot(&file_ep, &cache_load_ok);
    static const nlohmann::json            k_empty_cache_obj = nlohmann::json::object();
    const nlohmann::json& cache_j = (cache_sp ? *cache_sp : k_empty_cache_obj);
    const bool            has_cache = cache_load_ok && static_cast<bool>(cache_sp);
    const bool            use_cache = has_cache && cache_is_fresh(cache_j, file_ep);

    std::string base = normalize_replicate_base_url(base_url);
    std::string url;
    if (base.find("/v1/collections") != std::string::npos) {
        url = base;
    } else if (base.size() >= 3 && base.substr(base.size() - 3) == "/v1") {
        url = base + "/collections";
    } else {
        url = base + "/v1/collections";
    }

    nlohmann::json   j;
    const char*      col_source = nullptr;

    if (!force_refresh && use_cache && cache_j.contains("collections") && cache_j["collections"].is_array()
        && !cache_j["collections"].empty()) {
        j = nlohmann::json::object();
        j["results"] = cache_j["collections"];
        col_source   = "disk_cache_fresh";
    } else if (!force_refresh && use_cache
               && (!cache_j.contains("collections") || !cache_j["collections"].is_array() || cache_j["collections"].empty())
               && cache_j.contains("models_by_collection") && cache_j["models_by_collection"].is_object()) {
        nlohmann::json syn = collection_results_from_model_keys(cache_j["models_by_collection"]);
        if (syn.is_array() && !syn.empty()) {
            j = nlohmann::json::object();
            j["results"] = std::move(syn);
            col_source   = "disk_synthetic_from_model_keys";
        }
    }

    if (!col_source) {
        if (!key.empty()) {
            if (!http_get_json(key, url, j, err_out)) {
                if (use_cache && cache_j.contains("collections") && cache_j["collections"].is_array()
                    && !cache_j["collections"].empty()) {
                    j = nlohmann::json::object();
                    j["results"] = cache_j["collections"];
                    err_out.clear();
                    col_source = "http_fail_fallback_cache";
                } else if (use_cache && cache_j.contains("models_by_collection")
                           && cache_j["models_by_collection"].is_object()) {
                    nlohmann::json syn = collection_results_from_model_keys(cache_j["models_by_collection"]);
                    if (syn.is_array() && !syn.empty()) {
                        j = nlohmann::json::object();
                        j["results"] = std::move(syn);
                        err_out.clear();
                        col_source = "http_fail_fallback_synthetic";
                    } else {
                        log_rep(std::string("fetch_collections: failed (no fallback) — ") + err_out);
                        return false;
                    }
                } else {
                    log_rep(std::string("fetch_collections: failed (no fallback) — ") + err_out);
                    return false;
                }
            } else {
                col_source = "http";
            }
        } else if (use_cache && cache_j.contains("collections") && cache_j["collections"].is_array()
                   && !cache_j["collections"].empty()) {
            j = nlohmann::json::object();
            j["results"] = cache_j["collections"];
            col_source   = "disk_cache_no_api_key";
        } else if (!force_refresh && use_cache
                   && (!cache_j.contains("collections") || !cache_j["collections"].is_array() || cache_j["collections"].empty())
                   && cache_j.contains("models_by_collection") && cache_j["models_by_collection"].is_object()) {
            nlohmann::json syn = collection_results_from_model_keys(cache_j["models_by_collection"]);
            if (syn.is_array() && !syn.empty()) {
                j = nlohmann::json::object();
                j["results"] = std::move(syn);
                col_source   = "disk_synthetic_from_model_keys";
            } else {
                err_out = "api key is empty";
                log_rep("fetch_collections: api key is empty and cache cannot serve (force_refresh="
                        + std::string(force_refresh ? "true" : "false")
                        + " has_cache=" + std::string(has_cache ? "true" : "false")
                        + " use_cache=" + std::string(use_cache ? "true" : "false") + ")");
                return false;
            }
        } else {
            err_out = "api key is empty";
            log_rep("fetch_collections: api key is empty and cache cannot serve (force_refresh="
                    + std::string(force_refresh ? "true" : "false")
                    + " has_cache=" + std::string(has_cache ? "true" : "false")
                    + " use_cache=" + std::string(use_cache ? "true" : "false") + ")");
            return false;
        }
    }
    if (!j.is_object() || !j.contains("results") || !j["results"].is_array()) {
        err_out = "invalid JSON shape from collections endpoint";
        log_rep(std::string("fetch_collections: bad JSON shape from source=") + (col_source ? col_source : "?"));
        return false;
    }

    for (const auto& it : j["results"]) {
        if (!it.is_object()) continue;
        CollectionInfo c;
        if (it.contains("slug") && it["slug"].is_string()) c.slug = it["slug"].get<std::string>();
        if (it.contains("name") && it["name"].is_string()) c.name = it["name"].get<std::string>();
        if (it.contains("description") && it["description"].is_string()) c.description = it["description"].get<std::string>();
        if (c.slug.empty()) continue;
        if (c.name.empty()) c.name = c.slug;
        out_collections.push_back(std::move(c));
    }
    if (out_collections.empty()) {
        err_out = "collections list is empty";
        log_rep(std::string("fetch_collections: empty list (source=") + (col_source ? col_source : "?") + ")");
        return false;
    }
    {
        std::ostringstream o;
        o << "fetch_collections: source=" << (col_source ? col_source : "?") << " count=" << out_collections.size()
          << " force_refresh=" << (force_refresh ? 1 : 0) << " use_cache=" << (use_cache ? 1 : 0)
          << " has_key=" << (!key.empty() ? 1 : 0) << " url=" << url;
        log_rep(o.str());
    }
    // Only persist when the list came from the network or was first derived (synthetic) on disk — not on
    // pure disk_cache_fresh / disk_cache_no_api_key reads, which would rewrite the file every panel open.
    const bool persist_collections = col_source
        && (std::strcmp(col_source, "http") == 0 || std::strcmp(col_source, "disk_synthetic_from_model_keys") == 0
            || std::strcmp(col_source, "http_fail_fallback_synthetic") == 0);
    if (j.contains("results") && j["results"].is_array() && !j["results"].empty() && persist_collections) {
        nlohmann::json cache_mut = cache_sp ? nlohmann::json(*cache_sp) : nlohmann::json::object();
        cache_mut["collections"] = j["results"];
        cache_mut["fetched_at"]  = now_epoch_seconds();
        save_cache(cache_mut);
    }
    return true;
}

bool fetch_collection_models(const std::string& api_key,
                               const std::string& base_url,
                               const std::string& collection_slug,
                               std::vector<ModelInfo>& out_models,
                               std::string& err_out,
                               bool force_refresh) {
    out_models.clear();
    err_out.clear();
    if (collection_slug.empty()) {
        err_out = "collection slug is empty";
        return false;
    }
    const std::string key = resolve_replicate_api_key(api_key);
    std::int64_t      file_ep = 0;
    bool              cache_load_ok = false;
    std::shared_ptr<const nlohmann::json> cache_sp = replicate_cache_snapshot(&file_ep, &cache_load_ok);
    static const nlohmann::json            k_empty_cache_obj = nlohmann::json::object();
    const nlohmann::json& cache_j = (cache_sp ? *cache_sp : k_empty_cache_obj);
    const bool            has_cache = cache_load_ok && static_cast<bool>(cache_sp);
    const bool            use_cache = has_cache && cache_is_fresh(cache_j, file_ep);

    std::string base = normalize_replicate_base_url(base_url);
    std::string url;
    if (base.find("/v1/collections/") != std::string::npos) {
        url = base;
    } else if (base.size() >= 3 && base.substr(base.size() - 3) == "/v1") {
        url = base + "/collections/" + collection_slug;
    } else {
        url = base + "/v1/collections/" + collection_slug;
    }

    nlohmann::json j;
    const char*   m_source = "?";
    if (!force_refresh && use_cache && cache_j.contains("models_by_collection") &&
        cache_j["models_by_collection"].is_object() &&
        cache_j["models_by_collection"].contains(collection_slug) &&
        cache_j["models_by_collection"][collection_slug].is_array()) {
        j = nlohmann::json::object();
        j["models"] = cache_j["models_by_collection"][collection_slug];
        m_source     = "disk_cache_fresh";
    } else if (!key.empty()) {
        if (!http_get_json(key, url, j, err_out)) {
            if (use_cache && cache_j.contains("models_by_collection") &&
                cache_j["models_by_collection"].is_object() &&
                cache_j["models_by_collection"].contains(collection_slug) &&
                cache_j["models_by_collection"][collection_slug].is_array()) {
                j = nlohmann::json::object();
                j["models"] = cache_j["models_by_collection"][collection_slug];
                err_out.clear();
                m_source = "http_fail_fallback_cache";
            } else {
                log_rep(std::string("fetch_collection_models: collection=") + collection_slug
                        + " failed (no fallback) — " + err_out);
                return false;
            }
        } else {
            m_source = "http";
        }
    } else if (use_cache && cache_j.contains("models_by_collection") &&
               cache_j["models_by_collection"].is_object() &&
               cache_j["models_by_collection"].contains(collection_slug) &&
               cache_j["models_by_collection"][collection_slug].is_array()) {
        j = nlohmann::json::object();
        j["models"] = cache_j["models_by_collection"][collection_slug];
        m_source     = "disk_cache_no_api_key";
    } else {
        err_out = "api key is empty";
        log_rep("fetch_collection_models: collection=" + collection_slug
                + " api key is empty and cache cannot serve (force_refresh="
                + std::string(force_refresh ? "true" : "false")
                + " has_cache=" + std::string(has_cache ? "true" : "false")
                + " use_cache=" + std::string(use_cache ? "true" : "false") + ")");
        return false;
    }
    if (!j.is_object() || !j.contains("models") || !j["models"].is_array()) {
        err_out = "invalid JSON shape from collection endpoint";
        log_rep("fetch_collection_models: bad JSON shape collection=" + collection_slug
                + " source=" + std::string(m_source));
        return false;
    }

    for (const auto& it : j["models"]) {
        if (!it.is_object()) continue;
        ModelInfo m;
        if (it.contains("slug") && it["slug"].is_string()) m.slug = it["slug"].get<std::string>();
        if (m.slug.empty() && it.contains("owner") && it["owner"].is_string() && it.contains("name") && it["name"].is_string()) {
            m.slug = it["owner"].get<std::string>() + "/" + it["name"].get<std::string>();
        }
        if (m.slug.empty() && it.contains("id") && it["id"].is_string()) m.slug = it["id"].get<std::string>();
        if (m.slug.empty()) continue;
        if (it.contains("description") && it["description"].is_string()) m.description = it["description"].get<std::string>();
        if (it.contains("url") && it["url"].is_string()) m.url = it["url"].get<std::string>();
        if (it.contains("visibility") && it["visibility"].is_string()) m.visibility = it["visibility"].get<std::string>();
        if (it.contains("is_official") && it["is_official"].is_boolean()) m.is_official = it["is_official"].get<bool>();
        out_models.push_back(std::move(m));
    }
    if (out_models.empty()) {
        err_out = "models list is empty for collection " + collection_slug;
        log_rep("fetch_collection_models: empty models collection=" + collection_slug + " source=" + std::string(m_source));
        return false;
    }
    {
        std::ostringstream o;
        o << "fetch_collection_models: collection=" << collection_slug << " source=" << m_source
          << " count=" << out_models.size() << " force_refresh=" << (force_refresh ? 1 : 0)
          << " use_cache=" << (use_cache ? 1 : 0) << " has_key=" << (!key.empty() ? 1 : 0) << " url=" << url;
        log_rep(o.str());
    }
    if (m_source && std::strcmp(m_source, "http") == 0 && j.contains("models") && j["models"].is_array()) {
        nlohmann::json cache_mut = cache_sp ? nlohmann::json(*cache_sp) : nlohmann::json::object();
        if (!cache_mut.contains("models_by_collection") || !cache_mut["models_by_collection"].is_object()) {
            cache_mut["models_by_collection"] = nlohmann::json::object();
        }
        cache_mut["models_by_collection"][collection_slug] = j["models"];
        cache_mut["fetched_at"]                              = now_epoch_seconds();
        save_cache(cache_mut);
    }
    return true;
}

void sort_collections_alpha(std::vector<CollectionInfo>& collections) {
    std::sort(collections.begin(), collections.end(), [](const CollectionInfo& a, const CollectionInfo& b) {
        return to_lower(a.slug) < to_lower(b.slug);
    });
}

void sort_models_alpha(std::vector<ModelInfo>& models) {
    std::sort(models.begin(), models.end(), [](const ModelInfo& a, const ModelInfo& b) {
        return to_lower(a.slug) < to_lower(b.slug);
    });
}

bool resolve_collection_for_model_cached(const std::string& model_slug, std::string& collection_slug_out) {
    collection_slug_out.clear();
    if (model_slug.empty()) return false;
    bool cache_load_ok = false;
    std::shared_ptr<const nlohmann::json> cache_sp = replicate_cache_snapshot(nullptr, &cache_load_ok);
    if (!cache_load_ok || !cache_sp) return false;
    const nlohmann::json& cache_j = *cache_sp;
    if (!cache_j.contains("models_by_collection") || !cache_j["models_by_collection"].is_object()) return false;
    const auto& by_col = cache_j["models_by_collection"];
    for (auto it = by_col.begin(); it != by_col.end(); ++it) {
        if (!it.value().is_array()) continue;
        for (const auto& m : it.value()) {
            if (!m.is_object()) continue;
            std::string slug;
            if (m.contains("slug") && m["slug"].is_string()) slug = m["slug"].get<std::string>();
            if (slug.empty() && m.contains("owner") && m["owner"].is_string() && m.contains("name") && m["name"].is_string()) {
                slug = m["owner"].get<std::string>() + "/" + m["name"].get<std::string>();
            }
            if (slug == model_slug) {
                collection_slug_out = it.key();
                return true;
            }
        }
    }
    return false;
}

bool list_models_replicate_http(
    const std::string& api_key,
    const std::string& base_url,
    int limit,
    const std::string& cursor,
    const std::string& sort_by,
    const std::string& sort_direction,
    std::string& json_out,
    std::string& err_out,
    bool force_refresh) {
    json_out.clear();
    err_out.clear();
    const std::string key = resolve_replicate_api_key(api_key);

    std::string url = base_url.empty() ? "https://api.replicate.com/v1/collections/official" : base_url;
    while (!url.empty() && (url.back() == '/' || url.back() == '\\')) url.pop_back();
    const bool is_models_endpoint    = (url.find("/v1/models") != std::string::npos);
    const bool is_official_collection = (url.find("/v1/collections/official") != std::string::npos);
    if (!is_models_endpoint && !is_official_collection) {
        if (url.size() >= 3 && url.substr(url.size() - 3) == "/v1") url += "/collections/official";
        else url += "/v1/collections/official";
    }

    if (!force_refresh && !is_models_endpoint && is_official_collection && cursor.empty()) {
        std::int64_t      file_ep = 0;
        bool              cache_load_ok = false;
        std::shared_ptr<const nlohmann::json> cache_sp = replicate_cache_snapshot(&file_ep, &cache_load_ok);
        if (cache_load_ok && cache_sp) {
            const nlohmann::json& cache_j = *cache_sp;
            if (cache_is_fresh(cache_j, file_ep) && cache_j.contains("models_by_collection") &&
                cache_j["models_by_collection"].is_object() &&
                cache_j["models_by_collection"].contains("official") &&
                cache_j["models_by_collection"]["official"].is_array()) {
                nlohmann::json synth = nlohmann::json::object();
                synth["models"] = cache_j["models_by_collection"]["official"];
                json_out          = synth.dump();
                log_rep("list_models_replicate_http: source=disk_cache_official n="
                        + std::to_string(synth["models"].size()) + " bytes_out=" + std::to_string(json_out.size()));
                return true;
            }
            log_rep_debug("list_models_replicate_http: official collection cache not used (stale or incomplete) "
                          "force_refresh=" + std::string(force_refresh ? "true" : "false"));
        }
    }

    if (key.empty()) {
        err_out = "provider models list: API key required (Replicate key in app settings, or --api-key)";
        log_rep("list_models_replicate_http: abort — no API key after cache miss url=" + url);
        return false;
    }

    std::vector<std::string> qs;
    if (is_models_endpoint) {
        if (limit > 0) qs.push_back("limit=" + std::to_string(limit));
        if (!cursor.empty()) qs.push_back("cursor=" + url_encode_query_part(cursor));
        if (!sort_by.empty()) qs.push_back("sort_by=" + url_encode_query_part(sort_by));
        if (!sort_direction.empty()) qs.push_back("sort_direction=" + url_encode_query_part(sort_direction));
    }
    if (!qs.empty()) {
        url += "?";
        for (size_t i = 0; i < qs.size(); ++i) {
            if (i) url += "&";
            url += qs[i];
        }
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "provider models list: curl init failed";
        return false;
    }

    std::string response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    const std::string auth = "Authorization: Bearer " + key;
    hdrs = curl_slist_append(hdrs, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    const CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err_out = std::string("provider models list: HTTP error: ") + curl_easy_strerror(cc);
        log_rep("list_models_replicate_http: curl failed url=" + url + " — " + err_out);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        err_out = "provider models list: Replicate HTTP " + std::to_string(http_code);
        if (response.size() < 1500) err_out += ": " + response;
        log_rep("list_models_replicate_http: http error url=" + url + " — " + err_out);
        return false;
    }
    if (response.empty()) {
        err_out = "provider models list: empty response body from Replicate";
        log_rep("list_models_replicate_http: empty body url=" + url);
        return false;
    }
    json_out = std::move(response);
    log_rep("list_models_replicate_http: source=http url=" + url + " bytes=" + std::to_string(json_out.size())
            + " is_models_ep=" + std::string(is_models_endpoint ? "1" : "0"));
    return true;
}

namespace {

const nlohmann::json* openapi_navigate_ref(const nlohmann::json& openapi_root, const std::string& ref) {
    if (ref.size() < 2 || ref[0] != '#' || ref[1] != '/')
        return nullptr;
    const nlohmann::json* cur = &openapi_root;
    std::size_t pos = 2;
    while (pos < ref.size()) {
        const std::size_t slash = ref.find('/', pos);
        const std::string seg =
            (slash == std::string::npos) ? ref.substr(pos) : ref.substr(pos, slash - pos);
        if (seg.empty())
            return nullptr;
        if (!cur->is_object() || !cur->contains(seg))
            return nullptr;
        cur = &(*cur)[seg];
        if (slash == std::string::npos)
            break;
        pos = slash + 1;
    }
    return cur;
}

void merge_schema_fragments(nlohmann::json& acc, const nlohmann::json& frag) {
    if (!frag.is_object())
        return;
    for (auto it = frag.begin(); it != frag.end(); ++it) {
        const auto& k = it.key();
        if (k == "x-order" || k == "title" || k == "allOf")
            continue;
        acc[k] = it.value();
    }
}

nlohmann::json flatten_one_input_property(const nlohmann::json& openapi_root, const nlohmann::json& prop_def) {
    nlohmann::json acc = nlohmann::json::object();
    merge_schema_fragments(acc, prop_def);
    if (prop_def.contains("allOf") && prop_def["allOf"].is_array()) {
        for (const auto& part : prop_def["allOf"]) {
            if (!part.is_object())
                continue;
            if (part.contains("$ref") && part["$ref"].is_string()) {
                const auto* t = openapi_navigate_ref(openapi_root, part["$ref"].get_ref<const std::string&>());
                if (t)
                    merge_schema_fragments(acc, *t);
            } else {
                merge_schema_fragments(acc, part);
            }
        }
    }
    return acc;
}

nlohmann::json flatten_openapi_input_properties(const nlohmann::json& openapi_root) {
    nlohmann::json out = nlohmann::json::object();
    if (!openapi_root.is_object() || !openapi_root.contains("components") || !openapi_root["components"].is_object())
        return out;
    const auto& comp = openapi_root["components"];
    if (!comp.contains("schemas") || !comp["schemas"].is_object())
        return out;
    const auto& schemas = comp["schemas"];
    if (!schemas.contains("Input") || !schemas["Input"].is_object())
        return out;
    const auto& input = schemas["Input"];
    if (!input.contains("properties") || !input["properties"].is_object())
        return out;
    for (auto it = input["properties"].begin(); it != input["properties"].end(); ++it)
        out[it.key()] = flatten_one_input_property(openapi_root, it.value());
    return out;
}

bool cached_model_slug_matches_ci(const std::string& want, const nlohmann::json& m) {
    std::string slug;
    if (m.contains("slug") && m["slug"].is_string())
        slug = m["slug"].get<std::string>();
    if (slug.empty() && m.contains("owner") && m["owner"].is_string() && m.contains("name") && m["name"].is_string())
        slug = m["owner"].get<std::string>() + "/" + m["name"].get<std::string>();
    if (slug.empty())
        return false;
    return to_lower(slug) == to_lower(want);
}

bool find_model_in_cache_by_slug(const std::string& model_slug, nlohmann::json& model_out) {
    bool cache_load_ok = false;
    std::shared_ptr<const nlohmann::json> cache_sp = replicate_cache_snapshot(nullptr, &cache_load_ok);
    if (!cache_load_ok || !cache_sp) return false;
    const nlohmann::json& cache_j = *cache_sp;
    if (!cache_j.contains("models_by_collection") || !cache_j["models_by_collection"].is_object())
        return false;
    for (auto it = cache_j["models_by_collection"].begin(); it != cache_j["models_by_collection"].end(); ++it) {
        if (!it.value().is_array())
            continue;
        for (const auto& m : it.value()) {
            if (!m.is_object())
                continue;
            if (cached_model_slug_matches_ci(model_slug, m)) {
                model_out = m;
                return true;
            }
        }
    }
    return false;
}

bool schema_prop_is_array_of_strings(const nlohmann::json& flat_props, const char* key) {
    if (!flat_props.contains(key) || !flat_props[key].is_object())
        return false;
    const auto& o = flat_props[key];
    if (o.value("type", std::string{}) == "array")
        return true;
    if (o.contains("items") && o["items"].is_object()) {
        const auto& it = o["items"];
        if (it.value("type", std::string{}) == "string")
            return true;
        if (it.contains("anyOf") && it["anyOf"].is_array()) {
            for (const auto& a : it["anyOf"]) {
                if (a.is_object() && a.value("type", std::string{}) == "string")
                    return true;
            }
        }
    }
    return false;
}

void infer_video_field_plan_from_flat(const nlohmann::json& flat, ReplicateVideoInputFieldPlan& p) {
    p.first_still_key.clear();
    p.second_still_key.clear();
    p.reference_array_key.clear();
    p.use_image_input_array = false;
    if (!flat.is_object() || flat.empty())
        return;

    std::unordered_set<std::string> keys;
    for (auto it = flat.begin(); it != flat.end(); ++it)
        keys.insert(it.key());

    if (keys.count("image_input") && schema_prop_is_array_of_strings(flat, "image_input"))
        p.use_image_input_array = true;

    if (!p.use_image_input_array) {
        static const char* first_prio[] = {"image", "first_frame_image", "init_image", "start_image", "input_image"};
        for (const char* k : first_prio) {
            if (keys.count(k)) {
                p.first_still_key = k;
                break;
            }
        }

        static const char* second_prio[] = {"last_frame", "last_frame_image", "end_image", "end_frame_image", "last_image"};
        for (const char* k : second_prio) {
            if (keys.count(k)) {
                p.second_still_key = k;
                break;
            }
        }
    }

    static const char* ref_prio[] = {"reference_images", "reference_image_urls", "style_reference_images"};
    for (const char* k : ref_prio) {
        if (keys.count(k) && schema_prop_is_array_of_strings(flat, k)) {
            p.reference_array_key = k;
            break;
        }
    }
}

static void extract_openapi_input_required(const nlohmann::json& openapi, nlohmann::json& req_out) {
    req_out = nlohmann::json::array();
    if (!openapi.contains("components") || !openapi["components"].is_object())
        return;
    const auto& comp = openapi["components"];
    if (!comp.contains("schemas") || !comp["schemas"].is_object())
        return;
    const auto& schemas = comp["schemas"];
    if (!schemas.contains("Input") || !schemas["Input"].is_object())
        return;
    const auto& input = schemas["Input"];
    if (!input.contains("required") || !input["required"].is_array())
        return;
    for (const auto& r : input["required"])
        if (r.is_string())
            req_out.push_back(r);
}

static nlohmann::json flat_field_to_param_schema(const nlohmann::json& flat) {
    nlohmann::json out = nlohmann::json::object();
    if (!flat.is_object()) {
        out["type"] = "string";
        return out;
    }
    if (flat.contains("anyOf") && flat["anyOf"].is_array()) {
        for (const auto& branch : flat["anyOf"]) {
            if (!branch.is_object())
                continue;
            nlohmann::json sub = flat_field_to_param_schema(branch);
            if (sub.contains("type"))
                return sub;
        }
    }
    std::string t;
    if (flat.contains("type")) {
        const auto& tf = flat["type"];
        if (tf.is_string())
            t = tf.get<std::string>();
        else if (tf.is_array()) {
            for (const auto& el : tf) {
                if (el.is_string() && el.get<std::string>() != "null") {
                    t = el.get<std::string>();
                    break;
                }
            }
        }
    }
    if (t == "array") {
        out["type"] = "array";
        if (flat.contains("items") && flat["items"].is_object())
            out["items"] = flat_field_to_param_schema(flat["items"]);
        else
            out["items"] = {{"type", "string"}};
    } else if (t == "object") {
        out["type"] = "object";
        if (flat.contains("properties") && flat["properties"].is_object()) {
            nlohmann::json props = nlohmann::json::object();
            for (auto it = flat["properties"].begin(); it != flat["properties"].end(); ++it)
                props[it.key()] = flat_field_to_param_schema(it.value());
            out["properties"] = std::move(props);
        }
        if (flat.contains("additionalProperties"))
            out["additionalProperties"] = flat["additionalProperties"];
    } else if (t == "integer" || t == "number" || t == "string" || t == "boolean") {
        out["type"] = t;
    } else if (!t.empty()) {
        out["type"] = t;
    } else if (flat.contains("enum")) {
        out["type"] = "string";
    } else {
        out["type"] = "string";
    }
    if (flat.contains("enum"))
        out["enum"] = flat["enum"];
    if (flat.contains("format"))
        out["format"] = flat["format"];
    if (flat.contains("description"))
        out["description"] = flat["description"];
    if (flat.contains("default"))
        out["default"] = flat["default"];
    if (flat.contains("minimum"))
        out["minimum"] = flat["minimum"];
    if (flat.contains("maximum"))
        out["maximum"] = flat["maximum"];
    if (flat.contains("minItems"))
        out["minItems"] = flat["minItems"];
    if (flat.contains("maxItems"))
        out["maxItems"] = flat["maxItems"];
    return out;
}

static nlohmann::json openapi_flat_to_parameters_impl(const nlohmann::json& flat, const nlohmann::json& required_in) {
    nlohmann::json props = nlohmann::json::object();
    if (flat.is_object()) {
        for (auto it = flat.begin(); it != flat.end(); ++it)
            props[it.key()] = flat_field_to_param_schema(it.value());
    }
    nlohmann::json req = nlohmann::json::array();
    if (required_in.is_array()) {
        for (const auto& r : required_in) {
            if (!r.is_string())
                continue;
            const std::string key = r.get<std::string>();
            if (flat.is_object() && flat.contains(key))
                req.push_back(key);
        }
    }
    return nlohmann::json{
        {"type", "object"},
        {"properties", std::move(props)},
        {"required", std::move(req)},
        {"additionalProperties", false},
    };
}

} // namespace

bool lookup_replicate_video_openapi_input(const std::string& model_slug,
                                          nlohmann::json& flattened_input_properties_out,
                                          ReplicateVideoInputFieldPlan& plan_out,
                                          std::string& err_out) {
    flattened_input_properties_out = nlohmann::json::object();
    plan_out                         = ReplicateVideoInputFieldPlan{};
    err_out.clear();
    if (model_slug.empty()) {
        err_out = "model slug is empty";
        return false;
    }
    nlohmann::json model;
    if (!find_model_in_cache_by_slug(model_slug, model)) {
        err_out = "model not found in replicate-models-cache.json (open Chat → Image provider and refresh models)";
        return false;
    }
    if (!model.contains("latest_version") || !model["latest_version"].is_object()) {
        err_out = "cached model has no latest_version";
        return false;
    }
    const auto& lv = model["latest_version"];
    if (!lv.contains("openapi_schema") || !lv["openapi_schema"].is_object()) {
        err_out = "latest_version has no openapi_schema";
        return false;
    }
    const auto& openapi = lv["openapi_schema"];
    flattened_input_properties_out = flatten_openapi_input_properties(openapi);
    if (!flattened_input_properties_out.is_object() || flattened_input_properties_out.empty()) {
        err_out = "OpenAPI components.schemas.Input.properties missing or empty";
        return false;
    }
    infer_video_field_plan_from_flat(flattened_input_properties_out, plan_out);
    plan_out.openapi_found = true;
    if (!plan_out.valid()) {
        err_out = "could not infer first-frame or image_input[] mapping from OpenAPI Input";
        plan_out.openapi_found = false;
        return false;
    }
    if (!plan_out.first_still_key.empty() && plan_out.first_still_key == plan_out.reference_array_key)
        plan_out.reference_array_key.clear();
    return true;
}

bool lookup_replicate_openapi_input_flat(const std::string& model_slug,
                                         nlohmann::json& flattened_input_properties_out,
                                         nlohmann::json& input_required_array_out,
                                         std::string& err_out) {
    flattened_input_properties_out = nlohmann::json::object();
    input_required_array_out       = nlohmann::json::array();
    err_out.clear();
    if (model_slug.empty()) {
        err_out = "model slug is empty";
        return false;
    }
    nlohmann::json model;
    if (!find_model_in_cache_by_slug(model_slug, model)) {
        err_out = "model not found in replicate-models-cache.json (open Chat → Image provider and refresh models)";
        return false;
    }
    if (!model.contains("latest_version") || !model["latest_version"].is_object()) {
        err_out = "cached model has no latest_version";
        return false;
    }
    const auto& lv = model["latest_version"];
    if (!lv.contains("openapi_schema") || !lv["openapi_schema"].is_object()) {
        err_out = "latest_version has no openapi_schema";
        return false;
    }
    const auto& openapi = lv["openapi_schema"];
    flattened_input_properties_out = flatten_openapi_input_properties(openapi);
    if (!flattened_input_properties_out.is_object() || flattened_input_properties_out.empty()) {
        err_out = "OpenAPI components.schemas.Input.properties missing or empty";
        return false;
    }
    extract_openapi_input_required(openapi, input_required_array_out);
    return true;
}

nlohmann::json openapi_flat_to_create_tool_parameters(const nlohmann::json& flat_input_properties,
                                                      const nlohmann::json& input_required_array) {
    return openapi_flat_to_parameters_impl(flat_input_properties, input_required_array);
}

} // namespace media::replicate_cli
