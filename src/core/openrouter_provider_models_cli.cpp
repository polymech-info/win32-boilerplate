#include "openrouter_provider_models_cli.hpp"

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

namespace media::openrouter_cli {
namespace {

static std::filesystem::path cache_path() {
    if (const char* p = std::getenv("POLYMECH_OPENROUTER_MODELS_CACHE")) {
        if (p[0]) return std::filesystem::path(p);
    }
    return media::settings::get_config_dir() / "openrouter-models-cache.json";
}

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

static void log_or(const std::string& line) { logger::info(std::string("openrouter_models: ") + line); }
static void log_or_debug(const std::string& line) { logger::debug(std::string("openrouter_models: ") + line); }

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
    return age <= kOpenRouterModelsCacheTtlSeconds;
}

static std::mutex     g_or_cache_mu;
static nlohmann::json g_or_cache_mem;
static bool           g_or_mem_ready  = false;
static bool           g_or_load_ok    = false;
static std::int64_t   g_or_file_mtime = 0;

static std::string strip_trailing_slashes(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

static std::string models_endpoint_url(const std::string& base_in) {
    const std::string b = strip_trailing_slashes(base_in);
    if (b.empty()) return "https://openrouter.ai/api/v1/models";
    if (b.find("/models") != std::string::npos) return b;
    if (b.size() >= 3 && b.compare(b.size() - 3, 3, "/v1") == 0) return b + "/models";
    if (b.size() >= 4 && b.compare(b.size() - 4, 4, "/api") == 0) return b + "/v1/models";
    return b + "/models";
}

static bool load_cache(nlohmann::json& j) {
    j = nlohmann::json::object();
    std::lock_guard<std::mutex> lock(g_or_cache_mu);
    if (g_or_mem_ready) {
        j = g_or_cache_mem;
        return g_or_load_ok;
    }
    g_or_file_mtime = 0;
    g_or_cache_mem  = nlohmann::json::object();
    g_or_load_ok    = false;
    const auto p = cache_path();
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        log_or_debug("cache cold read: file missing at " + p.string());
        g_or_mem_ready = true;
        j              = g_or_cache_mem;
        return false;
    }
    g_or_file_mtime = file_mtime_epoch(p);
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        log_or_debug("cache cold read: cannot open " + p.string());
        g_or_mem_ready = true;
        j              = g_or_cache_mem;
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (raw.empty()) {
        g_or_mem_ready = true;
        j              = g_or_cache_mem;
        return false;
    }
    try {
        nlohmann::json parsed = nlohmann::json::parse(raw);
        if (!parsed.is_object()) {
            g_or_mem_ready = true;
            j              = g_or_cache_mem;
            return false;
        }
        g_or_cache_mem     = std::move(parsed);
        g_or_load_ok       = true;
        g_or_mem_ready     = true;
        j                  = g_or_cache_mem;
    } catch (const std::exception& e) {
        log_or_debug(std::string("cache cold read: parse error (") + e.what() + ") — " + p.string());
        g_or_mem_ready = true;
        j              = g_or_cache_mem;
        return false;
    } catch (...) {
        g_or_mem_ready = true;
        j              = g_or_cache_mem;
        return false;
    }
    {
        const bool        fresh     = cache_is_fresh(g_or_cache_mem, g_or_file_mtime);
        std::ostringstream o;
        o << "cache cold read: ok path=" << p.string() << " fresh=" << (fresh ? 1 : 0);
        log_or(o.str());
    }
    return true;
}

/// OpenRouter may return OpenAI-style @c data or a top-level @c models array (see ref cache JSON); normalize to @c data.
static nlohmann::json normalize_openrouter_models_body(nlohmann::json j) {
    if (j.is_array()) return nlohmann::json{{"data", std::move(j)}};
    if (!j.is_object()) return nlohmann::json::object();
    if (j.contains("data") && j["data"].is_array()) {
        nlohmann::json out    = nlohmann::json::object();
        out["data"]           = std::move(j["data"]);
        return out;
    }
    if (j.contains("models") && j["models"].is_array()) {
        nlohmann::json out;
        out["data"] = std::move(j["models"]);
        return out;
    }
    return nlohmann::json::object();
}

static void save_cache_merged(const nlohmann::json& api_object) {
    const nlohmann::json norm = normalize_openrouter_models_body(api_object.is_object() ? api_object
                                                                                        : nlohmann::json::object());
    if (!norm.contains("data") || !norm["data"].is_array()) return;
    nlohmann::json stored = norm;
    stored["fetched_at"]  = now_epoch_seconds();
    {
        std::lock_guard<std::mutex> lock(g_or_cache_mu);
        g_or_cache_mem   = stored;
        g_or_load_ok     = true;
        g_or_mem_ready   = true;
        g_or_file_mtime  = now_epoch_seconds();
    }
    const auto     p   = cache_path();
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        log_or_debug("cache write: failed to open for write — " + p.string());
        return;
    }
    ofs << stored.dump();
    if (!ofs.good()) log_or_debug("cache write: short write — " + p.string());
    else
        log_or("cache write: " + p.string() + " models=" + std::to_string(stored["data"].size()));
}

static std::string json_payload_for_stdout(const nlohmann::json& cache_or_disk) {
    nlohmann::json out = normalize_openrouter_models_body(cache_or_disk);
    if (out.contains("data") && out["data"].is_array()) {
        nlohmann::json payload{{"data", out["data"]}};
        return payload.dump();
    }
    nlohmann::json fallback = cache_or_disk;
    if (fallback.is_object()) fallback.erase("fetched_at");
    return fallback.dump();
}

} // namespace

bool list_models_openrouter_http(
    const std::string& api_key,
    const std::string& base_url,
    std::string&       json_out,
    std::string&       err_out,
    const bool         force_refresh) {
    json_out.clear();
    err_out.clear();
    const std::string url = models_endpoint_url(base_url);

    if (!force_refresh) {
        nlohmann::json cache_j;
        if (load_cache(cache_j) && cache_is_fresh(cache_j, g_or_file_mtime)) {
            const nlohmann::json norm = normalize_openrouter_models_body(cache_j);
            if (norm.contains("data") && norm["data"].is_array() && !norm["data"].empty()) {
                json_out = json_payload_for_stdout(cache_j);
                log_or("list_models_openrouter_http: source=disk_cache n="
                       + std::to_string(norm["data"].size()) + " bytes_out=" + std::to_string(json_out.size()));
                return true;
            }
        }
        log_or_debug("list_models_openrouter_http: cache miss or stale, fetching url=" + url);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "provider models list: curl init failed";
        return false;
    }

    std::string         response;
    struct curl_slist* hdrs = nullptr;
    hdrs                    = curl_slist_append(hdrs, "Content-Type: application/json");
    if (!api_key.empty()) {
        const std::string auth = "Authorization: Bearer " + api_key;
        hdrs                     = curl_slist_append(hdrs, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pm-image (openrouter model catalog)");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    const CURLcode cc = curl_easy_perform(curl);
    long           http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err_out = std::string("provider models list: HTTP error: ") + curl_easy_strerror(cc);
        log_or("list_models_openrouter_http: curl failed url=" + url + " — " + err_out);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        err_out = "provider models list: OpenRouter HTTP " + std::to_string(http_code);
        if (response.size() < 1500) err_out += ": " + response;
        log_or("list_models_openrouter_http: http error url=" + url + " — " + err_out);
        return false;
    }
    if (response.empty()) {
        err_out = "provider models list: empty response body from OpenRouter";
        log_or("list_models_openrouter_http: empty body url=" + url);
        return false;
    }
    try {
        nlohmann::json     j     = nlohmann::json::parse(response);
        nlohmann::json     norm = normalize_openrouter_models_body(std::move(j));
        if (!norm.contains("data") || !norm["data"].is_array()) {
            err_out = "provider models list: OpenRouter response has no model list (expected \"data\" or "
                      "\"models\" array, or a JSON array of models)";
            if (response.size() < 400) err_out += " body=" + response;
            return false;
        }
        save_cache_merged(norm);
        json_out = nlohmann::json{{"data", norm["data"]}}.dump();
        log_or("list_models_openrouter_http: source=http url=" + url + " bytes=" + std::to_string(json_out.size()));
    } catch (const std::exception& e) {
        err_out = std::string("provider models list: failed to parse OpenRouter JSON: ") + e.what();
        return false;
    }
    return true;
}

static std::string or_to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string or_join_str_array(const nlohmann::json& arr) {
    if (!arr.is_array()) return "";
    std::string out;
    for (const auto& v : arr) {
        if (!v.is_string()) continue;
        if (!out.empty()) out += ", ";
        out += v.get<std::string>();
    }
    return out;
}

static bool or_has_supported(const nlohmann::json& m, const char* p) {
    if (!m.contains("supported_parameters") || !m["supported_parameters"].is_array()) return false;
    for (const auto& x : m["supported_parameters"]) {
        if (x.is_string() && x.get<std::string>() == p) return true;
    }
    return false;
}

static std::optional<std::int64_t> or_context_tokens(const nlohmann::json& m) {
    if (m.contains("context_length") && m["context_length"].is_number_integer())
        return m["context_length"].get<std::int64_t>();
    if (m.contains("top_provider") && m["top_provider"].is_object() && m["top_provider"].contains("context_length")
        && m["top_provider"]["context_length"].is_number_integer())
        return m["top_provider"]["context_length"].get<std::int64_t>();
    return std::nullopt;
}

static std::string or_format_detail_head(const nlohmann::json& m) {
    std::ostringstream o;
    if (const auto ctx = or_context_tokens(m)) o << "Context: " << *ctx << " tokens\n";
    if (m.contains("architecture") && m["architecture"].is_object()) {
        const auto& a   = m["architecture"];
        const std::string in  = a.contains("input_modalities") ? or_join_str_array(a["input_modalities"]) : std::string{};
        const std::string out = a.contains("output_modalities") ? or_join_str_array(a["output_modalities"]) : std::string{};
        if (a.contains("modality") && a["modality"].is_string()) o << "Modality: " << a["modality"].get<std::string>() << "\n";
        o << "In: " << (in.empty() ? "-" : in) << "  |  Out: " << (out.empty() ? "-" : out) << "\n";
    }
    const bool tools = or_has_supported(m, "tools");
    const bool tch   = or_has_supported(m, "tool_choice");
    if (tools || tch) {
        o << "Tools: ";
        if (tools) o << "function/tools";
        if (tools && tch) o << ", ";
        if (tch) o << "tool_choice";
        o << " (supported on OpenRouter)\n";
    } else {
        o << "Tools: not listed\n";
    }
    return o.str();
}

static std::string or_make_open_url(const std::string& id) {
    if (id.empty()) return "https://openrouter.ai";
    return "https://openrouter.ai/" + id;
}

static void or_sort_catalog_rows(std::vector<OpenRouterCatalogModelRow>& models) {
    std::sort(models.begin(), models.end(), [](const OpenRouterCatalogModelRow& a, const OpenRouterCatalogModelRow& b) {
        return or_to_lower(a.name) < or_to_lower(b.name);
    });
}

bool list_openrouter_catalog_models(const std::string& api_key,
                                    const std::string& base_url,
                                    std::vector<OpenRouterCatalogModelRow>& out,
                                    std::string& err_out,
                                    const bool force_refresh) {
    out.clear();
    err_out.clear();
    std::string json;
    if (!list_models_openrouter_http(api_key, base_url, json, err_out, force_refresh)) return false;
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        err_out = std::string("OpenRouter: ") + e.what();
        return false;
    }
    nlohmann::json* data = nullptr;
    if (root.is_object() && root["data"].is_array()) data = &root["data"];
    if (!data) {
        err_out = "OpenRouter: model list is not a JSON object with a data array.";
        return false;
    }
    for (const auto& el : *data) {
        if (!el.is_object()) continue;
        if (!el.contains("id") || !el["id"].is_string()) continue;
        OpenRouterCatalogModelRow mi;
        mi.id   = el["id"].get<std::string>();
        mi.name = el.contains("name") && el["name"].is_string() ? el["name"].get<std::string>() : mi.id;
        mi.description =
            el.contains("description") && el["description"].is_string() ? el["description"].get<std::string>() : "";
        mi.open_url    = or_make_open_url(mi.id);
        mi.detail_head = or_format_detail_head(el);
        out.push_back(std::move(mi));
    }
    or_sort_catalog_rows(out);
    return true;
}

} // namespace media::openrouter_cli
