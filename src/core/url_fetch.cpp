#include "url_fetch.hpp"

#include <fstream>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

namespace media {

#if defined(MEDIA_ANDROID_NO_CURL) && (defined(__ANDROID__) || defined(ANDROID))

// Android NDK: no libcurl; local file paths in resize work. HTTP(S) inputs fail at fetch.
static std::once_flag g_curl_init;

void ensure_curl_global() {
    (void)g_curl_init;
}

bool is_http_url(const std::string& s) {
    return s.size() > 8 && (s.compare(0, 7, "http://") == 0 || s.compare(0, 8, "https://") == 0);
}

void url_template_variables(const std::string& url, std::string& src_dir, std::string& src_name, std::string& src_ext) {
    src_dir.clear();
    src_name.clear();
    src_ext.clear();
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos)
        return;
    const size_t path_start = url.find('/', scheme + 3);
    if (path_start == std::string::npos) {
        src_dir = url;
        src_name = "image";
        return;
    }
    std::string path_only = url.substr(path_start);
    while (path_only.size() > 1 && path_only.back() == '/')
        path_only.pop_back();
    const size_t last = path_only.find_last_of('/');
    const std::string filename =
        (last == std::string::npos) ? path_only.substr(1) : path_only.substr(last + 1);
    fs::path fn(filename.empty() ? "image" : filename);
    src_name = fn.stem().string();
    src_ext  = fn.extension().string();
    if (last == std::string::npos)
        src_dir = url.substr(0, path_start);
    else
        src_dir = url.substr(0, path_start + last);
}

std::string url_suggested_filename(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos)
        return "image";
    size_t path_start = url.find('/', scheme + 3);
    if (path_start == std::string::npos)
        return "image";
    std::string path_only = url.substr(path_start);
    while (path_only.size() > 1 && path_only.back() == '/')
        path_only.pop_back();
    const size_t last = path_only.find_last_of('/');
    std::string filename = (last == std::string::npos) ? path_only.substr(1) : path_only.substr(last + 1);
    if (filename.empty())
        return "image";
    return filename;
}

bool fetch_url_to_file(const std::string&, const fs::path&, int, int, std::string& err_out) {
    err_out = "HTTP fetch is not available in Android NDK build (MEDIA_ANDROID_NO_CURL). Use a local file path.";
    return false;
}

#else

#include <curl/curl.h>

static std::once_flag g_curl_init;

void ensure_curl_global() {
    std::call_once(g_curl_init, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

namespace {

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* os = static_cast<std::ostream*>(userdata);
    const size_t n = size * nmemb;
    os->write(ptr, static_cast<std::streamsize>(n));
    return os->good() ? n : 0;
}

} // namespace

bool is_http_url(const std::string& s) {
    return s.size() > 8 && (s.compare(0, 7, "http://") == 0 || s.compare(0, 8, "https://") == 0);
}

void url_template_variables(const std::string& url, std::string& src_dir, std::string& src_name, std::string& src_ext) {
    src_dir.clear();
    src_name.clear();
    src_ext.clear();
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos)
        return;
    const size_t path_start = url.find('/', scheme + 3);
    if (path_start == std::string::npos) {
        src_dir = url;
        src_name = "image";
        return;
    }
    std::string path_only = url.substr(path_start);
    while (path_only.size() > 1 && path_only.back() == '/')
        path_only.pop_back();
    const size_t last = path_only.find_last_of('/');
    const std::string filename =
        (last == std::string::npos) ? path_only.substr(1) : path_only.substr(last + 1);
    fs::path fn(filename.empty() ? "image" : filename);
    src_name = fn.stem().string();
    src_ext  = fn.extension().string();
    if (last == std::string::npos)
        src_dir = url.substr(0, path_start);
    else
        src_dir = url.substr(0, path_start + last);
}

std::string url_suggested_filename(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos)
        return "image";
    size_t path_start = url.find('/', scheme + 3);
    if (path_start == std::string::npos)
        return "image";
    std::string path_only = url.substr(path_start);
    while (path_only.size() > 1 && path_only.back() == '/')
        path_only.pop_back();
    const size_t last = path_only.find_last_of('/');
    std::string filename = (last == std::string::npos) ? path_only.substr(1) : path_only.substr(last + 1);
    if (filename.empty())
        return "image";
    return filename;
}

bool fetch_url_to_file(const std::string& url, const fs::path& dest, int timeout_sec, int max_redirects,
                       std::string& err_out) {
    err_out.clear();
    ensure_curl_global();

    CURL* curl = curl_easy_init();
    if (!curl) {
        err_out = "curl_easy_init failed";
        return false;
    }

    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        err_out = "cannot open temp file for download: " + dest.generic_string();
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(max_redirects > 0 ? max_redirects : 20));
    curl_easy_setopt(curl, CURLOPT_POSTREDIR, 3L);
    if (timeout_sec > 0) {
        const long t = static_cast<long>(timeout_sec);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, t);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, t);
    } else {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 0L);
    }
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "media-img/0.1 (+https://github.com/libvips/libvips)");

    const CURLcode res = curl_easy_perform(curl);
    long           http_code = 0;
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);
    out.close();

    std::error_code ec;
    if (res != CURLE_OK) {
        fs::remove(dest, ec);
        err_out = std::string("curl: ") + curl_easy_strerror(res);
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        fs::remove(dest, ec);
        err_out = "HTTP status " + std::to_string(http_code);
        return false;
    }
    return true;
}

#endif

} // namespace media
