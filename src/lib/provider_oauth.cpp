#include "lib/provider_oauth.hpp"

#include "core/settings_store.hpp"
#if defined(_WIN32)
#include <Windows.h>
#include <shellapi.h>
#endif

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace media::provider_oauth {
namespace {

struct ProviderStatus {
    bool        in_progress = false;
    std::string info;
    std::string error;
};

std::mutex                                      g_mu;
std::unordered_map<std::string, ProviderStatus> g_status;

std::string ascii_lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::filesystem::path token_store_path(std::string& err)
{
    err.clear();
    return media::settings::get_config_dir() / "oauth-tokens.json";
}

void set_status(const std::string& provider, bool in_progress, const std::string& info, const std::string& error)
{
    std::lock_guard<std::mutex> lk(g_mu);
    auto& st = g_status[provider];
    st.in_progress = in_progress;
    st.info = info;
    st.error = error;
}

bool read_store_json(nlohmann::json& out, std::string& err)
{
    out = nlohmann::json::object();
    const std::filesystem::path p = token_store_path(err);
    if (!err.empty())
        return false;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec))
        return true;
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) {
        err = "open oauth token store failed";
        return false;
    }
    try {
        ifs >> out;
        if (!out.is_object())
            out = nlohmann::json::object();
        return true;
    } catch (const std::exception& e) {
        err = std::string("parse oauth token store: ") + e.what();
        return false;
    }
}

bool write_store_json(const nlohmann::json& doc, std::string& err)
{
    const std::filesystem::path p = token_store_path(err);
    if (!err.empty())
        return false;
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        err = "write oauth token store failed";
        return false;
    }
    ofs << doc.dump(2);
    return true;
}

bool open_browser_url(const std::string& url, std::string& err)
{
#if defined(_WIN32)
    const std::wstring wurl(url.begin(), url.end());
    const HINSTANCE h = ::ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    const auto code = reinterpret_cast<INT_PTR>(h);
    if (code <= 32) {
        err = "ShellExecuteW failed";
        return false;
    }
    return true;
#else
    (void)url;
    err = "open browser not implemented on this platform";
    return false;
#endif
}

size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* ud)
{
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

bool http_post_json(const std::string& url, const std::string& json_body, long& status_out, std::string& body_out, std::string& err)
{
    status_out = 0;
    body_out.clear();
    err.clear();
    CURL* c = curl_easy_init();
    if (!c) {
        err = "curl_easy_init failed";
        return false;
    }
    struct curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Accept: application/json");
    hdr = curl_slist_append(hdr, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body_out);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    const CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status_out);
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        err = std::string("curl: ") + curl_easy_strerror(rc);
        return false;
    }
    return true;
}

void run_github_device_flow()
{
    const std::string provider = "github";
    set_status(provider, true, "Starting device login...", "");

    constexpr const char* kClientId = "Iv1.b507a08c87ecfe98";
    constexpr const char* kDeviceUrl = "https://github.com/login/device/code";
    constexpr const char* kTokenUrl = "https://github.com/login/oauth/access_token";

    nlohmann::json req{{"client_id", kClientId}, {"scope", "read:user"}};
    long status = 0;
    std::string body, err;
    if (!http_post_json(kDeviceUrl, req.dump(), status, body, err)) {
        set_status(provider, false, "", "GitHub device code request failed: " + err);
        return;
    }
    nlohmann::json dj;
    try {
        dj = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        set_status(provider, false, "", std::string("GitHub device response parse failed: ") + e.what());
        return;
    }
    const std::string user_code = dj.value("user_code", std::string{});
    const std::string verify_uri = dj.value("verification_uri", std::string{"https://github.com/login/device"});
    const std::string device_code = dj.value("device_code", std::string{});
    const int interval = std::max(1, dj.value("interval", 5));
    const int expires_in = std::max(60, dj.value("expires_in", 900));
    if (user_code.empty() || device_code.empty()) {
        set_status(provider, false, "", "GitHub did not return device code");
        return;
    }
    {
        std::string open_err;
        if (!open_browser_url(verify_uri, open_err))
            set_status(provider, true, "Open browser manually: " + verify_uri + " code: " + user_code, "");
        else
            set_status(provider, true, "Browser opened. Enter code: " + user_code, "");
    }

    const int max_attempts = std::max(1, expires_in / interval);
    for (int i = 0; i < max_attempts; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        nlohmann::json tok_req{
            {"client_id", kClientId},
            {"device_code", device_code},
            {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
        };
        long tstatus = 0;
        std::string tbody, terr;
        if (!http_post_json(kTokenUrl, tok_req.dump(), tstatus, tbody, terr))
            continue;
        nlohmann::json tj;
        try {
            tj = nlohmann::json::parse(tbody);
        } catch (...) {
            continue;
        }
        const std::string e = tj.value("error", std::string{});
        if (e == "authorization_pending" || e == "slow_down")
            continue;
        if (e == "expired_token") {
            set_status(provider, false, "", "GitHub device code expired");
            return;
        }
        if (e == "access_denied") {
            set_status(provider, false, "", "GitHub access denied");
            return;
        }
        if (!e.empty()) {
            set_status(provider, false, "", "GitHub error: " + e);
            return;
        }
        const std::string token = tj.value("access_token", std::string{});
        if (!token.empty()) {
            nlohmann::json doc;
            std::string io_err;
            if (!read_store_json(doc, io_err)) {
                set_status(provider, false, "", io_err);
                return;
            }
            auto& row = doc["github"];
            if (!row.is_object())
                row = nlohmann::json::object();
            row["access_token"] = token;
            if (!write_store_json(doc, io_err)) {
                set_status(provider, false, "", io_err);
                return;
            }
            set_status(provider, false, "GitHub authenticated", "");
            return;
        }
    }
    set_status(provider, false, "", "Timed out waiting for GitHub authorization");
}

} // namespace

std::string normalize_provider_id(std::string provider_id)
{
    provider_id = ascii_lower(std::move(provider_id));
    if (provider_id == "openai_codex")
        return "openai";
    if (provider_id == "github_copilot")
        return "github";
    return provider_id;
}

bool is_supported_provider(const std::string& provider_id)
{
    const std::string p = normalize_provider_id(provider_id);
    return p == "openrouter" || p == "openai" || p == "github";
}

bool read_access_token(const std::string& provider_id, std::string& token_out, std::string& err)
{
    token_out.clear();
    const std::string p = normalize_provider_id(provider_id);
    if (!is_supported_provider(p)) {
        err = "unsupported provider";
        return false;
    }
    nlohmann::json doc;
    if (!read_store_json(doc, err))
        return false;
    if (!doc.contains(p) || !doc[p].is_object() || !doc[p].contains("access_token") || !doc[p]["access_token"].is_string()) {
        err = "token not found";
        return false;
    }
    token_out = doc[p]["access_token"].get<std::string>();
    if (token_out.empty()) {
        err = "token is empty";
        return false;
    }
    return true;
}

bool has_access_token(const std::string& provider_id, bool& has_token_out, std::string& err)
{
    has_token_out = false;
    std::string token;
    std::string terr;
    const bool ok = read_access_token(provider_id, token, terr);
    if (ok) {
        has_token_out = true;
        err.clear();
        return true;
    }
    if (terr == "token not found" || terr == "token is empty") {
        err.clear();
        return true;
    }
    err = terr;
    return false;
}

bool clear_access_token(const std::string& provider_id, std::string& err)
{
    const std::string p = normalize_provider_id(provider_id);
    if (!is_supported_provider(p)) {
        err = "unsupported provider";
        return false;
    }
    nlohmann::json doc;
    if (!read_store_json(doc, err))
        return false;
    if (doc.contains(p) && doc[p].is_object())
        doc[p].erase("access_token");
    return write_store_json(doc, err);
}

bool start_login_async(const std::string& provider_id, std::string& message_out, std::string& err)
{
    message_out.clear();
    err.clear();
    const std::string p = normalize_provider_id(provider_id);
    if (!is_supported_provider(p)) {
        err = "unsupported provider";
        return false;
    }

    if (p == "openrouter") {
        const std::string url = "https://openrouter.ai/settings/keys";
        if (!open_browser_url(url, err))
            return false;
        message_out = "Opened OpenRouter keys page in your browser.";
        set_status(p, false, message_out, "");
        return true;
    }
    if (p == "openai") {
        const std::string url = "https://platform.openai.com/api-keys";
        if (!open_browser_url(url, err))
            return false;
        message_out = "Opened OpenAI API keys page in your browser.";
        set_status(p, false, message_out, "");
        return true;
    }

    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_status[p].in_progress) {
            message_out = "Login already in progress.";
            return true;
        }
        g_status[p].in_progress = true;
        g_status[p].info.clear();
        g_status[p].error.clear();
    }
    std::thread([]() { run_github_device_flow(); }).detach();
    message_out = "GitHub device login started.";
    return true;
}

bool login_in_progress(const std::string& provider_id)
{
    const std::string p = normalize_provider_id(provider_id);
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_status.find(p);
    return it != g_status.end() && it->second.in_progress;
}

void read_last_login_message(const std::string& provider_id, std::string& info_out, std::string& error_out)
{
    const std::string p = normalize_provider_id(provider_id);
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_status.find(p);
    if (it == g_status.end()) {
        info_out.clear();
        error_out.clear();
        return;
    }
    info_out = it->second.info;
    error_out = it->second.error;
}

} // namespace media::provider_oauth
