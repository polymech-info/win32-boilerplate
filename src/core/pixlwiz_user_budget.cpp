#include "pixlwiz_user_budget.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>

namespace media::pixlwiz_cli {
namespace {

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
    static_cast<std::string*>(ud)->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string strip_trailing_slashes(std::string s) {
    while (!s.empty() && (s.back() == '/' || s.back() == '\\'))
        s.pop_back();
    return s;
}

/// Build the /v2/user/info URL from a bare host root or an existing /models / /v1 URL.
static std::string user_info_url(const std::string& base_in) {
    std::string b = strip_trailing_slashes(base_in);
    if (b.empty())
        b = "https://llm.polymech.info";

    // Drop /v1 or /models suffixes so we always POST against the bare host root.
    for (const char* suf : {"/v1", "/models"}) {
        const std::size_t slen = std::strlen(suf);
        if (b.size() >= slen && b.substr(b.size() - slen) == suf)
            b = b.substr(0, b.size() - slen);
    }

    return b + "/v2/user/info";
}

static std::string host_root(const std::string& base_in) {
    std::string b = strip_trailing_slashes(base_in);
    if (b.empty()) b = "https://llm.polymech.info";
    for (const char* suf : {"/v1", "/models"}) {
        const std::size_t slen = std::strlen(suf);
        if (b.size() >= slen && b.substr(b.size() - slen) == suf)
            b = b.substr(0, b.size() - slen);
    }
    return b;
}

/// Simple blocking curl GET; fills body_out on HTTP 200, otherwise sets err.
static bool curl_get_bearer(const std::string& url,
                             const std::string& access_token,
                             std::string& body_out,
                             std::string& err) {
    CURL* curl = curl_easy_init();
    if (!curl) { err = "curl_easy_init failed"; return false; }

    body_out.clear();
    struct curl_slist* hdr = nullptr;
    const std::string  auth_hdr = "Authorization: Bearer " + access_token;
    hdr = curl_slist_append(hdr, auth_hdr.c_str());
    hdr = curl_slist_append(hdr, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     hdr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body_out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    const CURLcode rc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) { err = std::string("curl: ") + curl_easy_strerror(rc); return false; }
    if (http_code != 200) { err = "HTTP " + std::to_string(http_code) + ": " + body_out; return false; }
    return true;
}

/// Format epoch seconds as "YYYY-MM-DD" string (UTC).
static std::string epoch_to_date_str(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

} // namespace

bool pixlwiz_get_budget_info(const std::string& access_token,
                              const std::string& base_url,
                              PixlWizBudgetInfo& out,
                              std::string&       err) {
    err.clear();
    if (access_token.empty()) {
        err = "no access token — run `pm-image login` first";
        return false;
    }

    std::string body;
    if (!curl_get_bearer(user_info_url(base_url), access_token, body, err))
        return false;

    try {
        const nlohmann::json j = nlohmann::json::parse(body);
        out.user_id         = j.value("user_id",          std::string{});
        out.user_email      = j.value("user_email",        std::string{});
        out.spend           = j.value("spend",             0.0);
        out.budget_duration = j.value("budget_duration",   std::string{});
        out.budget_reset_at = j.value("budget_reset_at",   std::string{});

        if (j.contains("max_budget") && !j["max_budget"].is_null())
            out.max_budget = j["max_budget"].get<double>();
        else
            out.max_budget = -1.0;

        out.models.clear();
        if (j.contains("models") && j["models"].is_array()) {
            for (const auto& m : j["models"])
                if (m.is_string()) out.models.push_back(m.get<std::string>());
        }
    } catch (const std::exception& e) {
        err = std::string("JSON parse: ") + e.what();
        return false;
    }
    return true;
}

bool pixlwiz_get_spend_logs(const std::string&               access_token,
                             const std::string&               base_url,
                             int                              days_back,
                             int                              page_size,
                             std::vector<PixlWizSpendLogEntry>& out,
                             std::string&                     err) {
    err.clear();
    out.clear();
    if (access_token.empty()) {
        err = "no access token — run `pm-image login` first";
        return false;
    }
    if (days_back < 1)  days_back = 7;
    if (page_size < 1)  page_size = 25;
    if (page_size > 100) page_size = 100;

    const auto now_tp  = std::chrono::system_clock::now();
    const auto now_t   = std::chrono::system_clock::to_time_t(now_tp);
    const auto start_t = now_t - static_cast<std::time_t>(days_back) * 86400;

    const std::string start_str = epoch_to_date_str(start_t);
    const std::string end_str   = epoch_to_date_str(now_t);

    // URL-encode spaces as %20 for safety in query strings.
    const std::string url = host_root(base_url)
        + "/spend/logs/v2"
        + "?start_date=" + start_str
        + "&end_date="   + end_str
        + "&page=1&page_size=" + std::to_string(page_size)
        + "&sort_by=startTime&sort_order=desc";

    std::string body;
    if (!curl_get_bearer(url, access_token, body, err))
        return false;

    try {
        const nlohmann::json j = nlohmann::json::parse(body);
        if (!j.contains("data") || !j["data"].is_array()) {
            err = "unexpected response: missing 'data' array";
            return false;
        }
        for (const auto& row : j["data"]) {
            PixlWizSpendLogEntry e;
            e.request_id         = row.value("request_id",          std::string{});
            e.start_time         = row.value("startTime",           std::string{});
            e.model              = row.value("model",               std::string{});
            e.spend              = row.value("spend",               0.0);
            e.total_tokens       = row.value("total_tokens",        0);
            e.prompt_tokens      = row.value("prompt_tokens",       0);
            e.completion_tokens  = row.value("completion_tokens",   0);
            out.push_back(std::move(e));
        }
    } catch (const std::exception& e) {
        err = std::string("JSON parse: ") + e.what();
        return false;
    }
    return true;
}

} // namespace media::pixlwiz_cli
