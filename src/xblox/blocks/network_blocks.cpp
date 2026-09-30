#include "blocks/network_blocks.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace media::xblox::blocks {
namespace {

std::string json_string(const nlohmann::json& o, const char* key)
{
    return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string{};
}

std::string upper_ascii(std::string s)
{
    for (char& ch : s)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

std::string lower_ascii(std::string s)
{
    for (char& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

long json_long(const nlohmann::json& o, const char* key, long fallback)
{
    if (!o.is_object() || !o.contains(key))
        return fallback;
    const auto& value = o[key];
    if (value.is_number_integer())
        return value.get<long>();
    if (value.is_number_unsigned())
        return static_cast<long>(value.get<unsigned long>());
    return fallback;
}

size_t write_body(char* ptr, size_t size, size_t nmemb, void* user_data)
{
    auto* out = static_cast<std::string*>(user_data);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t write_header(char* ptr, size_t size, size_t nmemb, void* user_data)
{
    auto* headers = static_cast<std::vector<std::string>*>(user_data);
    std::string line(ptr, ptr + size * nmemb);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    if (!line.empty())
        headers->push_back(std::move(line));
    return size * nmemb;
}

int progress_cancel(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
    auto* runtime = static_cast<BlockRuntime*>(clientp);
    return runtime && runtime->cancel_requested && runtime->cancel_requested(runtime->user_data) ? 1 : 0;
}

curl_slist* append_headers(const nlohmann::json& block, curl_slist* list)
{
    if (!block.is_object() || !block.contains("headers"))
        return list;
    const auto& headers = block["headers"];
    if (headers.is_object()) {
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            const std::string value = it.value().is_string() ? it.value().get<std::string>() : it.value().dump();
            list = curl_slist_append(list, (it.key() + ": " + value).c_str());
        }
    } else if (headers.is_array()) {
        for (const auto& item : headers) {
            if (item.is_string())
                list = curl_slist_append(list, item.get<std::string>().c_str());
        }
    }
    return list;
}

bool should_retry_code(long status_code)
{
    return status_code == 408 || status_code == 425 || status_code == 429 || status_code >= 500;
}

std::vector<std::string> split_filter_path(std::string filter)
{
    std::vector<std::string> parts;
    if (!filter.empty() && filter.front() == '.')
        filter.erase(filter.begin());
    std::string current;
    for (char ch : filter) {
        if (ch == '.') {
            if (!current.empty()) {
                parts.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        parts.push_back(std::move(current));
    return parts;
}

const nlohmann::json* select_path(const nlohmann::json& value, const std::string& filter)
{
    if (filter.empty() || filter == ".")
        return &value;
    const auto parts = split_filter_path(filter);
    const nlohmann::json* current = &value;
    for (const auto& part : parts) {
        if (!current)
            return nullptr;
        if (current->is_object()) {
            if (!current->contains(part))
                return nullptr;
            current = &(*current)[part];
            continue;
        }
        if (current->is_array()) {
            try {
                size_t consumed = 0;
                const size_t index = static_cast<size_t>(std::stoull(part, &consumed));
                if (consumed != part.size() || index >= current->size())
                    return nullptr;
                current = &(*current)[index];
                continue;
            } catch (...) {
                return nullptr;
            }
        }
        return nullptr;
    }
    return current;
}

std::string json_filter(const nlohmann::json& block)
{
    std::string filter = json_string(block, "parse");
    if (filter.empty())
        filter = json_string(block, "filter");
    if (filter.empty())
        filter = json_string(block, "query");
    return filter.empty() ? "." : filter;
}

bool network_request_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string url = json_string(block, "url");
    if (url.empty()) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "network", "error", "network request is missing url", 2});
        return true;
    }

    const std::string method = upper_ascii(json_string(block, "method").empty() ? "GET" : json_string(block, "method"));
    const long timeout_ms = std::max<long>(1, json_long(block, "timeoutMs", runtime.options.default_timeout_ms));
    const long connect_timeout_ms = std::max<long>(1, json_long(block, "connectTimeoutMs", std::min<long>(timeout_ms, 10000)));
    const long max_redirects = std::max<long>(0, json_long(block, "maxRedirects", 5));
    const int retries = static_cast<int>(std::max<long>(0, json_long(block, "retries", 2)));
    const int retry_delay_ms = static_cast<int>(std::max<long>(0, json_long(block, "retryDelayMs", 250)));
    const bool follow_redirects = block.value("followRedirects", true);
    const bool verify_peer = block.value("verifyPeer", true);
    const bool verify_host = block.value("verifyHost", true);
    const std::string decode = lower_ascii(json_string(block, "decode").empty() ? "raw" : json_string(block, "decode"));
    if (decode != "raw" && decode != "json") {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "network", "error", "unsupported network decode: " + decode, 2});
        return true;
    }

    std::string raw;
    std::vector<std::string> response_headers;
    std::string error;
    long status_code = 0;
    char curl_error[CURL_ERROR_SIZE] = {};
    CURLcode last_code = CURLE_OK;

    for (int attempt = 0; attempt <= retries; ++attempt) {
        raw.clear();
        response_headers.clear();
        status_code = 0;
        curl_error[0] = '\0';

        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "curl init failed";
            break;
        }

        curl_slist* headers = nullptr;
        headers = append_headers(block, headers);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_redirects ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, max_redirects);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, verify_peer ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verify_host ? 2L : 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cancel);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &runtime);
        if (headers)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        std::string body;
        if (block.contains("body")) {
            body = block["body"].is_string() ? block["body"].get<std::string>() : block["body"].dump();
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }

        last_code = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

        char* effective_url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
        const std::string final_url = effective_url ? effective_url : url;

        if (headers)
            curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (last_code == CURLE_OK && status_code < 400) {
            nlohmann::json result = {
                {"ok", true},
                {"type", "network"},
                {"url", url},
                {"effectiveUrl", final_url},
                {"method", method},
                {"statusCode", status_code},
                {"raw", raw},
                {"headers", response_headers},
                {"attempts", attempt + 1},
                {"decode", decode},
            };
            if (decode == "json") {
                nlohmann::json decoded;
                try {
                    decoded = nlohmann::json::parse(raw);
                } catch (const std::exception& e) {
                    result["ok"] = false;
                    result["error"] = std::string("json decode failed: ") + e.what();
                    runtime.set_context_value(runtime.user_data, "PREVIOUS", result);
                    runtime.emit(runtime.user_data, ExecutionEvent{
                        path,
                        "network",
                        "error",
                        result["error"].get<std::string>(),
                        2,
                        result,
                        json_string(block, "id"),
                        "network",
                        {},
                        {result["error"].get<std::string>()},
                        2,
                    });
                    return true;
                }
                const std::string filter = json_filter(block);
                const nlohmann::json* selected = select_path(decoded, filter);
                if (!selected) {
                    result["ok"] = false;
                    result["json"] = std::move(decoded);
                    result["parse"] = filter;
                    result["error"] = "json parse filter returned no value: " + filter;
                    runtime.set_context_value(runtime.user_data, "PREVIOUS", result);
                    runtime.emit(runtime.user_data, ExecutionEvent{
                        path,
                        "network",
                        "error",
                        result["error"].get<std::string>(),
                        2,
                        result,
                        json_string(block, "id"),
                        "network",
                        {},
                        {result["error"].get<std::string>()},
                        2,
                    });
                    return true;
                }
                result["parse"] = filter;
                result["result"] = *selected;
                result["json"] = std::move(decoded);
            }
            runtime.set_context_value(runtime.user_data, "PREVIOUS", result);
            runtime.emit(runtime.user_data, ExecutionEvent{
                path,
                "network",
                "ok",
                "network request complete",
                0,
                result,
                json_string(block, "id"),
                "network",
            });
            return true;
        }

        error = curl_error[0] != '\0' ? curl_error : curl_easy_strerror(last_code);
        if (last_code == CURLE_ABORTED_BY_CALLBACK)
            error = "network request cancelled";
        if (attempt < retries && last_code != CURLE_ABORTED_BY_CALLBACK && (last_code != CURLE_OK || should_retry_code(status_code))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
            continue;
        }
        break;
    }

    const int exit_code = last_code == CURLE_OPERATION_TIMEDOUT ? 124 : (last_code == CURLE_ABORTED_BY_CALLBACK ? 130 : 1);
    nlohmann::json result = {
        {"ok", false},
        {"type", "network"},
        {"url", url},
        {"method", method},
        {"statusCode", status_code},
        {"raw", raw},
        {"headers", response_headers},
        {"error", error},
        {"curlCode", static_cast<int>(last_code)},
        {"decode", decode},
    };
    runtime.set_context_value(runtime.user_data, "PREVIOUS", result);
    runtime.emit(runtime.user_data, ExecutionEvent{
        path,
        "network",
        "error",
        error.empty() ? "network request failed" : error,
        exit_code,
        result,
        json_string(block, "id"),
        "network",
        {},
        {error},
        exit_code,
    });
    return true;
}

} // namespace

BlockHandler network_block_handler(const std::string& kind)
{
    if (kind == "network" || kind == "httpRequest" || kind == "fetch")
        return network_request_block;
    return nullptr;
}

void register_network_blocks(BlockRegistry& registry)
{
    const nlohmann::json params = nlohmann::json::array({
        {{"name", "url"}, {"type", "string"}, {"required", true}},
        {{"name", "method"}, {"type", "string"}, {"default", "GET"}},
        {{"name", "decode"}, {"type", "string"}, {"default", "raw"}, {"enum", nlohmann::json::array({"raw", "json"})}},
        {{"name", "parse"}, {"type", "string"}, {"default", "."}},
        {{"name", "timeoutMs"}, {"type", "integer"}, {"default", 30000}},
        {{"name", "connectTimeoutMs"}, {"type", "integer"}, {"default", 10000}},
        {{"name", "followRedirects"}, {"type", "boolean"}, {"default", true}},
        {{"name", "retries"}, {"type", "integer"}, {"default", 2}},
        {{"name", "storeAs"}, {"type", "string"}},
    });
    registry["network"] = block_descriptor(
        "network", network_request_block, "Network Request", "Network", "HTTP request via libcurl.",
        {{"kind", "network"}, {"url", "https://"}, {"method", "GET"}, {"decode", "raw"}, {"timeoutMs", 30000}, {"followRedirects", true}, {"storeAs", "response"}},
        params, nlohmann::json::object(), 0xffffffffu, false);
    registry["httpRequest"] = block_descriptor(
        "httpRequest", network_request_block, "HTTP Request", "Network", "HTTP request via libcurl.",
        {{"kind", "httpRequest"}, {"url", "https://"}, {"method", "GET"}, {"decode", "raw"}, {"timeoutMs", 30000}, {"followRedirects", true}, {"storeAs", "response"}},
        params, nlohmann::json::object(), 0xffffffffu, false);
    registry["fetch"] = block_descriptor(
        "fetch", network_request_block, "Fetch", "Network", "Fetch a URL and store the raw response in PREVIOUS.",
        {{"kind", "fetch"}, {"url", "https://"}, {"decode", "raw"}, {"timeoutMs", 30000}, {"followRedirects", true}, {"storeAs", "response"}},
        params);
}

} // namespace media::xblox::blocks
