#include "lib/pm_service_upload.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

/** Must match pm-pics `src/lib/uploadUtils.ts` (`fetch(.../api/images?forward=vfs&original=true)`). */
constexpr const char* k_pm_service_images_upload_query = "?forward=vfs&original=true";

size_t curl_write_string(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

void log_or_ignore(const PmServiceUploadLogLine& log_line, std::string_view line)
{
    if (log_line)
        log_line(line);
}

} // namespace

std::string pm_getenv_trimmed(const char* key)
{
    const char* v = std::getenv(key);
    if (!v)
        return {};
    std::string s(v);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    return s.substr(i);
}

std::string pm_trim_trailing_slash(std::string s)
{
    while (!s.empty() && (s.back() == '/' || s.back() == '\\'))
        s.pop_back();
    return s;
}

std::string pm_resolve_service_server_base(const std::string& cli_server_url_override)
{
    if (!cli_server_url_override.empty())
        return pm_trim_trailing_slash(cli_server_url_override);
    std::string u = pm_getenv_trimmed("SERVER_URL");
    if (u.empty())
        u = pm_getenv_trimmed("VITE_SERVER_IMAGE_API_URL");
    if (u.empty())
        u = pm_getenv_trimmed("CLIENT_URL");
    return pm_trim_trailing_slash(u);
}

bool pm_service_post_image_multipart(const std::string& base_url, const std::string& bearer_token,
                                     const std::filesystem::path& file_path, long& http_code_out,
                                     std::string& response_body_out, std::string& err_out,
                                     const PmServiceUploadLogLine& log_line)
{
    http_code_out     = 0;
    response_body_out.clear();
    err_out.clear();
    std::error_code fs_ec;
    if (!std::filesystem::is_regular_file(file_path, fs_ec)) {
        err_out = "not a file: " + file_path.string();
        return false;
    }
    const std::string url = base_url + "/api/images" + k_pm_service_images_upload_query;
    log_or_ignore(log_line, "POST " + url);
    log_or_ignore(log_line, "  file path: " + file_path.string());
    const auto sz = static_cast<std::uintmax_t>(std::filesystem::file_size(file_path, fs_ec));
    log_or_ignore(log_line, "  file size bytes: " + std::to_string(static_cast<unsigned long long>(sz)));

    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "curl_easy_init failed";
        return false;
    }

    curl_mime*     mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    if (curl_mime_filedata(part, file_path.string().c_str()) != CURLE_OK) {
        err_out = "curl_mime_filedata failed for " + file_path.string();
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        return false;
    }

    struct curl_slist* hdr = nullptr;
    const std::string    auth = "Authorization: Bearer " + bearer_token;
    hdr                        = curl_slist_append(hdr, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body_out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    const CURLcode cc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code_out);
    curl_slist_free_all(hdr);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err_out = std::string("curl_easy_perform: ") + curl_easy_strerror(cc);
        return false;
    }
    log_or_ignore(log_line, std::string("  HTTP status: ") + std::to_string(http_code_out));
    log_or_ignore(log_line, std::string("  response body length: ") + std::to_string(response_body_out.size()));
    if (response_body_out.size() <= 800)
        log_or_ignore(log_line, std::string("  response body: ") + response_body_out);
    else
        log_or_ignore(log_line, "  response body (first 400 chars): " + response_body_out.substr(0, 400) + "...");
    return true;
}
