#include "transform.hpp"
#include "core/settings_portable.hpp"
#include "core/settings_runtime.hpp"
#include "core/cli_cancel.hpp"
#include "batch_queue.hpp"
#include "llm_jpeg_from_path.hpp"
#include "replicate_http_log.hpp"
#include "replicate_provider_models_cli.hpp"
#include "url_fetch.hpp"

#include "logger/logger.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #include "constants.hpp"
    #include <Windows.h>
    #include <shlobj.h>
    #pragma comment(lib, "Ole32.lib")
    #pragma comment(lib, "Shell32.lib")
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace media {

#if defined(_WIN32)
namespace {
std::mutex g_transform_iexecute_log_mutex;
void append_transform_iexecute_correlation_log_utf8(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_transform_iexecute_log_mutex);
    try {
        PWSTR path_tmp = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path_tmp)))
            return;
        fs::path dir(path_tmp);
        CoTaskMemFree(path_tmp);
        dir /= pm::brand::k_config_subpath_w;
        std::error_code ec;
        fs::create_directories(dir, ec);
        const fs::path logf = dir / L"pm-image-iexecute.log";
        std::ofstream      out(logf, std::ios::app | std::ios::binary);
        if (!out)
            return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        char ts[48]{};
        sprintf_s(ts, "%04u-%02u-%02u %02u:%02u:%02u  ", (unsigned)st.wYear, (unsigned)st.wMonth,
                  (unsigned)st.wDay, (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        out << ts << "[pm-image] " << line << '\n';
        out.flush();
    } catch (...) {
    }
}
} // namespace
#endif

std::string resolve_gemini_generate_url(const std::string& base_url, const std::string& model) {
    std::string base = base_url;
    if (base.empty()) base = "https://generativelanguage.googleapis.com/v1beta";
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    return base + "/models/" + model + ":generateContent";
}

static std::string resolve_replicate_predict_url(const std::string& base_url, const std::string& model) {
    (void)model;
    std::string base = base_url;
    if (base.empty()) base = "https://api.replicate.com/v1";
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    return base + "/predictions";
}

// Pause / cancel only between I/O and API (not during libvips or curl) — see BatchControl.
static bool tr_stopped(BatchControl* batch, const std::function<void()>& on_before) {
    if (media::cli::cancel_requested()) {
        if (batch) batch->request_cancel();
        return true;
    }
    if (!batch) return false;
    if (batch->paused.load() && on_before) on_before();
    batch->check_pause();
    if (media::cli::cancel_requested()) batch->request_cancel();
    return batch->cancel.load();
}

// Reference images: log which paths the worker will load (or that none were given).
static void log_ref_paths_requested(const char* mode_tag, const std::string& job_ctx,
                                    const std::vector<std::string>& paths) {
    std::string line = "Gemini[";
    line += mode_tag;
    line += "]: ";
    line += job_ctx;
    line += " - reference_images: ";
    if (paths.empty()) {
        line += "0 (none requested)";
    } else {
        int n = 0;
        for (const auto& p : paths) {
            if (!p.empty()) ++n;
        }
        line += std::to_string(n);
        line += " path(s)";
        for (const auto& p : paths) {
            if (p.empty()) continue;
            line += " | ";
            line += p;
        }
    }
    logger::info(line);
}

// After load: path + size + mime (disk reference images).
static void log_ref_disk_loaded(const char* mode_tag, const std::string& ref_path,
                                const std::string& mime, std::size_t nbytes) {
    std::string line = "Gemini[";
    line += mode_tag;
    line += "]: reference image loaded: ";
    line += ref_path;
    line += " | ";
    line += std::to_string(nbytes);
    line += " B | ";
    line += mime;
    logger::info(line);
}

// Zero-fs: no paths; log in-memory ref buffers count and per-buffer size/mime.
static void log_ref_memory_buffers(const char* mode_tag, const std::string& job_ctx,
                                  const std::vector<ReferenceBuffer>& bufs) {
    if (bufs.empty()) {
        logger::info(std::string("Gemini[") + mode_tag + "]: " + job_ctx
                     + " - in-memory reference buffers: 0 (none)");
        return;
    }
    std::size_t total = 0;
    std::size_t n = 0;
    for (const auto& b : bufs) {
        if (!b.bytes.empty()) {
            ++n;
            total += b.bytes.size();
        }
    }
    {
        std::string line = "Gemini[";
        line += mode_tag;
        line += "]: ";
        line += job_ctx;
        line += " - in-memory reference buffer(s): ";
        line += std::to_string(n);
        if (n > 0) {
            line += " (total payload ";
            line += std::to_string(total);
            line += " B)";
        }
        logger::info(line);
    }
    for (std::size_t i = 0; i < bufs.size(); ++i) {
        if (bufs[i].bytes.empty()) continue;
        std::string line = "Gemini[";
        line += mode_tag;
        line += "]:   ref[";
        line += std::to_string(i);
        line += "] ";
        line += std::to_string(bufs[i].bytes.size());
        line += " B | ";
        line += (bufs[i].mime.empty() ? "image/png" : bufs[i].mime);
        logger::info(line);
    }
}

// Camera / container types that are not valid as raw bytes for Gemini — decode via vips
// to JPEG (same path as an optional in-memory resize).
static bool ext_needs_implicit_jpeg_decode(const std::string& ext) {
    std::string e = ext;
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    static const char* k[] = {
        ".3fr", ".arw",  ".cr2",  ".cr3",  ".dng", ".erf", ".kdc",  ".dcr", ".mef", ".mos",
        ".mrw", ".nef",  ".nrw",  ".orf",  ".pef", ".ptx", ".raf",  ".raw", ".rwl", ".rw2",
        ".sr2", ".srf",  ".srw",  ".x3f",  ".crw", ".ia",  ".cs1",  ".bay", ".fff", ".iiq",
        ".heic", ".heif", ".hif"
    };
    for (const char* s : k) if (e == s) return true;
    return false;
}

// ── base64 encode/decode ────────────────────────────────────────────

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)data[i + 2];
        out.push_back(b64_table[(n >> 18) & 0x3F]);
        out.push_back(b64_table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? b64_table[n & 0x3F] : '=');
    }
    return out;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<uint8_t> base64_decode(const std::string& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size() * 3 / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = b64_decode_char(c);
        if (v < 0) continue;
        buf = (buf << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(buf >> bits));
        }
    }
    return out;
}

// ── MIME type from extension ────────────────────────────────────────

static std::string mime_from_ext(const std::string& ext) {
    std::string e = ext;
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
    if (e == ".png")  return "image/png";
    if (e == ".webp") return "image/webp";
    if (e == ".gif")  return "image/gif";
    if (e == ".bmp")  return "image/bmp";
    if (e == ".tif" || e == ".tiff") return "image/tiff";
    if (e == ".avif") return "image/avif";
    if (e == ".heic") return "image/heic";
    return "image/jpeg";
}

// Load one file for Gemini: optional vips resize, implicit JPEG for RAW/HEIC, else raw file bytes.
static bool load_image_for_transform(
    const std::string& path,
    const std::string& display_label,
    const TransformOptions& opts,
    TransformProgressFn progress,
    std::vector<uint8_t>& out_bytes,
    std::string& mime,
    std::string& err)
{
    constexpr int kImplicitLongEdge = 2048;
    std::string ext = fs::path(path).extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);

    const bool want_explicit
        = opts.resize_first && opts.resize_width > 0
          && (!opts.preresize_raw_only || ext_needs_implicit_jpeg_decode(ext));
    if (want_explicit) {
        if (progress) {
            progress("Resizing " + display_label + " in memory to "
                     + std::to_string(opts.resize_width) + "px");
        }
        int ow = 0, oh = 0, tw = 0, th = 0;
        if (!path_to_jpeg_for_llm(path, opts.resize_width, out_bytes, ow, oh, tw, th, err))
            return false;
        mime = "image/jpeg";
        return true;
    }
    if (ext_needs_implicit_jpeg_decode(ext)) {
        if (progress) progress("Decoding " + display_label + " to JPEG (libvips)…");
        int ow = 0, oh = 0, tw = 0, th = 0;
        if (!path_to_jpeg_for_llm(path, kImplicitLongEdge, out_bytes, ow, oh, tw, th, err))
            return false;
        mime = "image/jpeg";
        return true;
    }
    if (progress) progress("Reading " + path);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        err = "Cannot open: " + path;
        return false;
    }
    out_bytes.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (out_bytes.empty()) {
        err = "File is empty: " + path;
        return false;
    }
    mime = mime_from_ext(ext);
    return true;
}

bool read_raster_for_llm_transform_path(
    const std::string& abs_path,
    const TransformOptions& opts,
    TransformProgressFn progress,
    std::vector<uint8_t>& out_bytes,
    std::string& out_mime,
    std::string& err)
{
    return load_image_for_transform(abs_path, "image", opts, progress, out_bytes, out_mime, err);
}

// ── curl helpers ────────────────────────────────────────────────────

static size_t string_write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

// ── Google Gemini generateContent — internal core (bytes in / bytes out) ───
// Used by both call_gemini (file variant) and transform_buffer (zero-fs).

struct GeminiCoreResult {
    bool                 ok = false;
    std::string          error;
    std::string          ai_text;
    std::vector<uint8_t> image_data;   // PNG bytes
};

struct ReplicateCoreResult {
    bool                 ok = false;
    std::string          error;
    std::string          ai_text;
    std::vector<uint8_t> image_data;
    std::string          mime = "image/png";
    std::string          prediction_web_url;
};

static std::string prompt_slug_for_filename(const std::string& prompt) {
    std::string slug;
    slug.reserve(prompt.size());
    for (char c : prompt) {
        if (std::isalnum((unsigned char)c))
            slug.push_back((char)std::tolower((unsigned char)c));
        else if (c == ' ' || c == '-' || c == '_')
            slug.push_back('_');
    }
    std::string clean;
    for (char c : slug) {
        if (c == '_' && !clean.empty() && clean.back() == '_') continue;
        clean.push_back(c);
    }
    while (!clean.empty() && clean.back() == '_') clean.pop_back();
    if (clean.size() > 40) clean.resize(40);
    while (!clean.empty() && clean.back() == '_') clean.pop_back();
    return clean;
}

static std::string default_create_path_from_prompt(const std::string& prompt) {
    std::string clean = prompt_slug_for_filename(prompt);
    if (clean.empty()) clean = "out";
    return (fs::current_path() / ("create_" + clean + ".png")).string();
}

static std::string infer_mime_from_url(const std::string& url) {
    std::string u = url;
    std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (u.find(".mp4") != std::string::npos) return "video/mp4";
    if (u.find(".webm") != std::string::npos) return "video/webm";
    if (u.find(".mov") != std::string::npos) return "video/quicktime";
    if (u.find(".gif") != std::string::npos) return "image/gif";
    if (u.find(".jpg") != std::string::npos || u.find(".jpeg") != std::string::npos) return "image/jpeg";
    if (u.find(".webp") != std::string::npos) return "image/webp";
    if (u.find(".png") != std::string::npos) return "image/png";
    return "image/png";
}

// Replicate `output` is often a URL string, but some models return an array where the *first* string
// is not a fetchable URL (e.g. a local-style path, label, or log). Do not use out[0] blindly.
// Also accept `data:image/...;base64,...` without an HTTP download.
static bool looks_like_http_url(const std::string& s) {
    return s.size() > 8 && (s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0);
}

static bool parse_data_url_to_image(const std::string& s, std::vector<uint8_t>& out_data, std::string& out_mime) {
    if (s.size() < 12 || s.rfind("data:", 0) != 0) return false;
    const auto comma = s.find(',');
    if (comma == std::string::npos) return false;
    const std::string meta = s.substr(5, comma - 5);
    if (meta.find(";base64") == std::string::npos) return false;
    const auto semi = meta.find(';');
    if (semi == std::string::npos) {
        if (!meta.empty()) out_mime = meta;
    } else {
        out_mime = meta.substr(0, semi);
    }
    if (out_mime.empty()) out_mime = "image/png";
    out_data = base64_decode(s.substr(comma + 1));
    return !out_data.empty();
}

// Returns true and sets out_url, or true and fills out_data from a data URL, or false.
static bool extract_replicate_transform_output(
    const json& out,
    std::string& out_url,
    std::vector<uint8_t>& out_data,
    std::string& out_mime)
{
    out_url.clear();
    out_data.clear();
    out_mime = "image/png";

    if (out.is_string()) {
        const std::string s = out.get<std::string>();
        if (looks_like_http_url(s)) {
            out_url = s;
            return true;
        }
        if (parse_data_url_to_image(s, out_data, out_mime)) return true;
        return false;
    }
    if (out.is_array()) {
        for (const auto& el : out) {
            if (el.is_string() && looks_like_http_url(el.get<std::string>())) {
                out_url = el.get<std::string>();
                return true;
            }
        }
        for (const auto& el : out) {
            if (el.is_string() && parse_data_url_to_image(el.get<std::string>(), out_data, out_mime)) return true;
        }
        for (const auto& el : out) {
            if (!el.is_object()) continue;
            for (const char* key : {"url", "uri", "image", "src"}) {
                if (el.contains(key) && el[key].is_string()) {
                    const std::string s = el[key].get<std::string>();
                    if (looks_like_http_url(s)) {
                        out_url = s;
                        return true;
                    }
                    if (parse_data_url_to_image(s, out_data, out_mime)) return true;
                }
            }
        }
    }
    if (out.is_object()) {
        for (const char* key : {"url", "uri", "image", "src"}) {
            if (out.contains(key) && out[key].is_string()) {
                const std::string s = out[key].get<std::string>();
                if (looks_like_http_url(s)) {
                    out_url = s;
                    return true;
                }
                if (parse_data_url_to_image(s, out_data, out_mime)) return true;
            }
        }
    }
    return false;
}

static void stash_replicate_prediction_web_url(const json& r, std::string& stash) {
    try {
        if (r.contains("urls") && r["urls"].is_object() && r["urls"].contains("web") && r["urls"]["web"].is_string()) {
            const std::string w = r["urls"]["web"].get<std::string>();
            if (w.rfind("https://", 0) == 0) stash = w;
        }
    } catch (...) {}
}

static ReplicateCoreResult replicate_post_poll_download(
    json req_body,
    const TransformOptions& opts,
    TransformProgressFn progress,
    const std::string& log_ctx,
    const std::string& model)
{
    ReplicateCoreResult res;
    std::string         prediction_web_stash;
    const std::string url = resolve_replicate_predict_url(opts.base_url, model);
    {
        const std::string req_dump = replicate_log::dump_json_for_log(req_body, 0);
        logger::info(std::string("Replicate[") + log_ctx + "] POST " + url);
        logger::info(std::string("Replicate[") + log_ctx
                     + "] request_headers: Content-Type=application/json, Prefer=wait=60, "
                       "Authorization=Bearer<redacted>");
        logger::info(std::string("Replicate[") + log_ctx + "] request_json=" + req_dump);
    }
    const std::string body_str = req_body.dump();

    if (progress) progress("Sending request to Replicate model " + model + "...");
    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) {
        res.error = "curl_easy_init failed";
        return res;
    }

    std::string response_str;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Prefer: wait=60");
    std::string auth_header = "Authorization: Bearer " + opts.api_key;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_str.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        logger::info(std::string("Replicate[") + log_ctx + "] initial_request curl_error: "
                     + std::string(curl_easy_strerror(cc)) + " url=" + url);
        res.error = std::string("Replicate request failed: ") + curl_easy_strerror(cc);
        return res;
    }
    {
        const std::string raw = response_str.size() > 10000
                                    ? (response_str.substr(0, 10000) + "...(truncated)")
                                    : response_str;
        try {
            json rj = json::parse(response_str);
            logger::info(std::string("Replicate[") + log_ctx
                         + "] initial_response HTTP " + std::to_string(http_code) + " json="
                         + replicate_log::dump_json_for_log(std::move(rj), 12000));
        } catch (const std::exception& e) {
            logger::info(std::string("Replicate[") + log_ctx
                         + "] initial_response HTTP " + std::to_string(http_code) + " (parse failed) raw="
                         + raw + " err=" + e.what());
        }
    }
    if (http_code < 200 || http_code >= 300) {
        res.error = "Replicate API returned HTTP " + std::to_string(http_code);
        if (response_str.size() < 1200) res.error += ": " + response_str;
        try {
            stash_replicate_prediction_web_url(json::parse(response_str), prediction_web_stash);
        } catch (...) {}
        res.prediction_web_url = prediction_web_stash;
        return res;
    }

    json resp;
    try {
        resp = json::parse(response_str);
    } catch (const std::exception& e) {
        res.error = std::string("Replicate JSON parse error: ") + e.what();
        return res;
    }
    stash_replicate_prediction_web_url(resp, prediction_web_stash);
    std::string status = resp.value("status", "");
    std::string poll_url;
    try {
        if (resp.contains("urls") && resp["urls"].is_object() && resp["urls"].contains("get") && resp["urls"]["get"].is_string()) {
            poll_url = resp["urls"]["get"].get<std::string>();
        }
    } catch (...) {}
    if (!poll_url.empty()) {
        logger::info(std::string("Replicate[") + log_ctx + "] poll_url GET " + poll_url);
    }
    if ((status == "starting" || status == "processing") && !poll_url.empty()) {
        // Image jobs often finish within ~1 min; **video** (i2v / gen) routinely needs several minutes.
        // Old cap (30 × 2s ≈ 60s) returned "failed" while Replicate was still `processing` — outputs
        // appeared later on replicate.com but local download never ran.
        const int max_polls = (log_ctx.find("video") != std::string::npos) ? 360 : 90;
        for (int i = 0; i < max_polls && (status == "starting" || status == "processing"); ++i) {
            if (progress) progress("Replicate prediction " + status + "... polling");
            CURL* poll = curl_easy_init();
            if (!poll) break;
            std::string poll_resp;
            struct curl_slist* poll_hdr = nullptr;
            poll_hdr = curl_slist_append(poll_hdr, auth_header.c_str());
            curl_easy_setopt(poll, CURLOPT_URL, poll_url.c_str());
            curl_easy_setopt(poll, CURLOPT_HTTPHEADER, poll_hdr);
            curl_easy_setopt(poll, CURLOPT_WRITEFUNCTION, string_write_cb);
            curl_easy_setopt(poll, CURLOPT_WRITEDATA, &poll_resp);
            curl_easy_setopt(poll, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(poll, CURLOPT_CONNECTTIMEOUT, 10L);
            CURLcode pc = curl_easy_perform(poll);
            curl_slist_free_all(poll_hdr);
            curl_easy_cleanup(poll);
            if (pc != CURLE_OK) {
                logger::info(std::string("Replicate[") + log_ctx + "] poll n=" + std::to_string(i)
                             + " GET failed: " + std::string(curl_easy_strerror(pc)));
                break;
            }
            try {
                resp = json::parse(poll_resp);
                stash_replicate_prediction_web_url(resp, prediction_web_stash);
                status = resp.value("status", status);
            } catch (const std::exception& e) {
                logger::info(std::string("Replicate[") + log_ctx + "] poll n=" + std::to_string(i)
                             + " json_parse_error: " + e.what());
                break;
            } catch (...) {
                break;
            }
            {
                const std::string poll_dump
                    = replicate_log::dump_json_for_log(json(resp), 8000);
                logger::info(std::string("Replicate[") + log_ctx + "] poll n=" + std::to_string(i) + " status=" + status
                             + " body=" + poll_dump);
            }
            if (status == "succeeded" || status == "failed" || status == "canceled") break;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        if (status == "starting" || status == "processing") {
            logger::info(std::string("Replicate[") + log_ctx + "] poll stopped (max_polls=" + std::to_string(max_polls)
                         + ") status=" + status + " — same GET " + poll_url + " (predictions API); job may still finish.");
        }
    }
    if (status != "succeeded") {
        const std::string err = resp.value("error", std::string{});
        res.error = "Replicate prediction status=" + (status.empty() ? "unknown" : status)
                  + (err.empty() ? "" : (": " + err));
        if ((status == "starting" || status == "processing") && !prediction_web_stash.empty()) {
            res.error += " (timed out waiting; prediction may still complete — open: " + prediction_web_stash + ")";
        }
        if (response_str.size() < 1200) res.error += " raw=" + response_str;
        res.prediction_web_url = prediction_web_stash;
        return res;
    }

    if (!resp.contains("output")) {
        res.error = "Replicate response missing output";
        res.prediction_web_url = prediction_web_stash;
        return res;
    }
    std::string          out_url;
    std::vector<uint8_t> out_inline;
    std::string          out_mime = "image/png";
    if (!extract_replicate_transform_output(resp["output"], out_url, out_inline, out_mime)) {
        std::string extra;
        try {
            const std::string d = resp["output"].dump();
            extra = d.size() > 1800 ? (d.substr(0, 1800) + "...") : d;
        } catch (...) {
            extra = "(could not dump output)";
        }
        res.error = "Replicate output did not include an https URL or data:image base64. output=" + extra;
        res.prediction_web_url = prediction_web_stash;
        return res;
    }
    if (!out_inline.empty()) {
        res.image_data = std::move(out_inline);
        res.mime       = out_mime;
        logger::info(std::string("Replicate[") + log_ctx
                     + "] output: from prediction (inline / data:); bytes=" + std::to_string(res.image_data.size())
                     + " (no raw payload in log)");
    } else {
        logger::info(std::string("Replicate[") + log_ctx + "] output_download GET " + out_url
                     + " headers: Authorization=Bearer<redacted>");
        if (progress) progress("Downloading Replicate output...");
        CURL* curl_get = curl_easy_init();
        if (!curl_get) {
            res.error = "curl_easy_init failed (output fetch)";
            res.prediction_web_url = prediction_web_stash;
            return res;
        }
        std::string out_bytes;
        struct curl_slist* out_headers = nullptr;
        out_headers = curl_slist_append(out_headers, auth_header.c_str());
        curl_easy_setopt(curl_get, CURLOPT_URL, out_url.c_str());
        curl_easy_setopt(curl_get, CURLOPT_HTTPHEADER, out_headers);
        curl_easy_setopt(curl_get, CURLOPT_WRITEFUNCTION, string_write_cb);
        curl_easy_setopt(curl_get, CURLOPT_WRITEDATA, &out_bytes);
        curl_easy_setopt(curl_get, CURLOPT_TIMEOUT, 120L);
        curl_easy_setopt(curl_get, CURLOPT_CONNECTTIMEOUT, 15L);
        CURLcode cc2 = curl_easy_perform(curl_get);
        long http_code2 = 0;
        curl_easy_getinfo(curl_get, CURLINFO_RESPONSE_CODE, &http_code2);
        curl_slist_free_all(out_headers);
        curl_easy_cleanup(curl_get);
        if (cc2 != CURLE_OK) {
            res.error = std::string("Replicate output download failed: ") + curl_easy_strerror(cc2) + " (" + out_url + ")";
            res.prediction_web_url = prediction_web_stash;
            return res;
        }
        if (http_code2 < 200 || http_code2 >= 300) {
            res.error = "Replicate output URL returned HTTP " + std::to_string(http_code2) + " (" + out_url + ")";
            res.prediction_web_url = prediction_web_stash;
            return res;
        }
        res.image_data.assign(out_bytes.begin(), out_bytes.end());
        res.mime = infer_mime_from_url(out_url);
        logger::info(std::string("Replicate[") + log_ctx + "] output_download done HTTP " + std::to_string(http_code2)
                     + " bytes=" + std::to_string(res.image_data.size()) + " (no raw body in log)");
    }
    res.ai_text = resp.value("logs", std::string{});
    if (res.image_data.empty()) {
        res.error = "Replicate output is empty";
        res.prediction_web_url = prediction_web_stash;
        return res;
    }
    res.prediction_web_url = prediction_web_stash;
    if (!res.prediction_web_url.empty())
        logger::info(std::string("Replicate[") + log_ctx + "] prediction_web=" + res.prediction_web_url);
    logger::info(std::string("Replicate[") + log_ctx + "] succeeded model=" + model
                 + " output_bytes=" + std::to_string(res.image_data.size()));
    res.ok = true;
    return res;
}

static ReplicateCoreResult call_replicate_core(
    const std::vector<uint8_t>& img_bytes,
    const std::string& mime,
    const std::vector<std::pair<std::string,std::vector<uint8_t>>>& refs,
    const TransformOptions& opts,
    TransformProgressFn progress,
    const std::string& log_file = {})
{
    const std::string log_ctx = log_file.empty() ? std::string("transform") : log_file;
    std::string       model   = opts.model;
    if (model.empty()) model = "google/nano-banana-pro";
    // Replicate's `google/gemini-2.5-flash` returns text (no image URL) for image_input+prompt; use an image model.
    if (model == "google/gemini-2.5-flash") {
        logger::info(std::string("Replicate[") + log_ctx
                     + "]: model google/gemini-2.5-flash is text-only for image transform on this API; "
                       "using google/nano-banana-pro for image output");
        model = "google/nano-banana-pro";
    }

    auto to_data_url = [](const std::vector<uint8_t>& bytes, const std::string& m) {
        const std::string b64 = base64_encode(bytes.data(), bytes.size());
        return std::string("data:") + (m.empty() ? "image/png" : m) + ";base64," + b64;
    };

    json image_input = json::array();
    if (!img_bytes.empty()) image_input.push_back(to_data_url(img_bytes, mime));
    for (const auto& [rmime, rbytes] : refs) {
        if (rbytes.empty()) continue;
        image_input.push_back(to_data_url(rbytes, rmime));
    }

    json input = {
        {"prompt", opts.prompt},
        {"output_format", "png"},
        {"image_input", image_input}
    };
    if (!opts.aspect_ratio.empty()) input["aspect_ratio"] = opts.aspect_ratio;
    json req_body = {{"version", model}, {"input", input}};
    return replicate_post_poll_download(std::move(req_body), opts, progress, log_ctx, model);
}

/** Replicate image-to-video / multi-frame video: keyframe keys from OpenAPI cache or explicit `TransformOptions` keys. */
static ReplicateCoreResult call_replicate_video_frames_core(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& frames,
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& reference_frames,
    const TransformOptions& opts,
    TransformProgressFn progress,
    const std::string& log_ctx)
{
    ReplicateCoreResult res;
    if (frames.empty() || frames[0].second.empty()) {
        res.error = "replicate_video: at least one non-empty frame image is required";
        return res;
    }
    std::string model = opts.model;
    if (model.empty()) {
        res.error = "replicate_video: options.model (Replicate slug) is required";
        return res;
    }

    auto to_data_url = [](const std::vector<uint8_t>& bytes, const std::string& m) {
        const std::string b64 = base64_encode(bytes.data(), bytes.size());
        return std::string("data:") + (m.empty() ? "image/png" : m) + ";base64," + b64;
    };

    json input;
    input["prompt"] = opts.prompt;

    media::replicate_cli::ReplicateVideoInputFieldPlan openapi_plan;
    nlohmann::json                             openapi_flat;
    std::string                                openapi_err;
    bool openapi_ok = false;
    if (opts.replicate_first_frame_key.empty())
        openapi_ok = media::replicate_cli::lookup_replicate_video_openapi_input(model, openapi_flat, openapi_plan, openapi_err);

    /* Optional explicit JSON keys (tools / advanced callers). */
    if (!opts.replicate_first_frame_key.empty()) {
        input[opts.replicate_first_frame_key] = to_data_url(frames[0].second, frames[0].first);
        if (frames.size() > 1 && !frames[1].second.empty() && !opts.replicate_second_frame_key.empty())
            input[opts.replicate_second_frame_key] = to_data_url(frames[1].second, frames[1].first);
    } else if (openapi_ok && openapi_plan.valid()) {
        /* Drive stills + optional ref arrays from local `replicate-models-cache.json` OpenAPI Input. */
        if (openapi_plan.use_image_input_array) {
            json image_input = json::array();
            for (const auto& fr : frames) {
                if (fr.second.empty()) continue;
                image_input.push_back(to_data_url(fr.second, fr.first));
            }
            input["image_input"] = std::move(image_input);
        } else {
            input[openapi_plan.first_still_key] = to_data_url(frames[0].second, frames[0].first);
            if (frames.size() > 1 && !frames[1].second.empty() && !openapi_plan.second_still_key.empty())
                input[openapi_plan.second_still_key] = to_data_url(frames[1].second, frames[1].first);
        }
        if (!openapi_plan.reference_array_key.empty() && !reference_frames.empty()) {
            json ref_arr = json::array();
            for (const auto& rf : reference_frames) {
                if (rf.second.empty()) continue;
                ref_arr.push_back(to_data_url(rf.second, rf.first));
            }
            if (!ref_arr.empty())
                input[openapi_plan.reference_array_key] = std::move(ref_arr);
        }
    } else {
        res.error = "replicate_video: need options.replicate_first_frame_key (and optional replicate_second_frame_key), "
                    "or a valid OpenAPI **Input** keyframe plan in replicate-models-cache.json for model `"
                    + model + "`"
                    + (openapi_err.empty() ? std::string{} : (": " + openapi_err));
        return res;
    }
    if (!opts.aspect_ratio.empty()) input["aspect_ratio"] = opts.aspect_ratio;

    json req_body = {{"version", model}, {"input", input}};
    return replicate_post_poll_download(std::move(req_body), opts, progress, log_ctx, model);
}

static bool ends_with_ci_path(const std::string& s, const char* suffix) {
    size_t sl = 0;
    while (suffix[sl])
        ++sl;
    if (s.size() < sl)
        return false;
    for (size_t i = 0; i < sl; ++i) {
        const unsigned char a = static_cast<unsigned char>(s[s.size() - sl + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

/// CLI / agent may pass `photo.jpg` with no path separators; still read + embed as data: for Replicate.
static bool bare_media_filename_no_separators_path(const std::string& s) {
    if (s.empty())
        return false;
    if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos)
        return false;
    if (s.size() >= 2 && s[1] == ':')
        return false;
    const size_t dot = s.rfind('.');
    if (dot == std::string::npos || dot == 0)
        return false;
    static const char* suf[] = {".jpg",  ".jpeg", ".png",  ".webp", ".gif", ".bmp", ".tif", ".tiff",
                                ".heic", ".avif", ".jxl",  ".mp4",  ".webm", ".mov", ".m4v"};
    for (const char* e : suf) {
        if (ends_with_ci_path(s, e))
            return true;
    }
    return false;
}

static bool string_might_be_local_file_path(const std::string& s) {
    if (s.empty() || s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0 || s.rfind("data:", 0) == 0)
        return false;
    if (s.size() >= 2 && s[1] == ':')
        return true;
    if (!s.empty() && (s[0] == '/' || s[0] == '\\'))
        return true;
    if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos)
        return true;
    return bare_media_filename_no_separators_path(s);
}

static std::string first_existing_file_parent_from_input_json(const json& j) {
    std::string best;
    std::function<void(const json&)> walk = [&](const json& node) {
        if (!best.empty())
            return;
        if (node.is_string()) {
            const std::string s = node.get<std::string>();
            if (!string_might_be_local_file_path(s))
                return;
            std::error_code ec;
            const fs::path p(s);
            if (fs::is_regular_file(p, ec))
                best = p.parent_path().string();
        } else if (node.is_array()) {
            for (const auto& el : node)
                walk(el);
        } else if (node.is_object()) {
            for (const auto& kv : node.items())
                walk(kv.value());
        }
    };
    walk(j);
    return best;
}

std::string replicate_video_intended_output_path(const std::string& explicit_output_path,
                                                 const json&        input,
                                                 const std::string& prompt)
{
    if (!explicit_output_path.empty())
        return explicit_output_path;
    return default_replicate_video_output_path(prompt, first_existing_file_parent_from_input_json(input));
}

static void replicate_resolve_local_file_strings_in_input(
    json& j, const TransformOptions& opt, TransformProgressFn progress, std::string& err_out) {
    if (j.is_string()) {
        std::string s = j.get<std::string>();
        if (!string_might_be_local_file_path(s))
            return;
        std::error_code ec;
        const fs::path p(s);
        if (!fs::is_regular_file(p, ec))
            return;
        std::vector<uint8_t> bytes;
        std::string          mime;
        if (!read_raster_for_llm_transform_path(s, opt, progress, bytes, mime, err_out))
            return;
        if (bytes.empty()) {
            err_out = "empty raster file: " + s;
            return;
        }
        j       = std::string("data:") + mime + ";base64," + base64_encode(bytes.data(), bytes.size());
        err_out = {};
    } else if (j.is_array()) {
        for (auto& el : j) {
            replicate_resolve_local_file_strings_in_input(el, opt, progress, err_out);
            if (!err_out.empty())
                return;
        }
    } else if (j.is_object()) {
        for (auto& kv : j.items()) {
            replicate_resolve_local_file_strings_in_input(kv.value(), opt, progress, err_out);
            if (!err_out.empty())
                return;
        }
    }
}

static ReplicateCoreResult call_replicate_video_raw_input(
    json input,
    const TransformOptions& opts,
    TransformProgressFn progress,
    const std::string& log_ctx)
{
    ReplicateCoreResult res;
    std::string         model = opts.model;
    if (model.empty()) {
        res.error = "replicate_video_replicate_input: model (Replicate slug) is required";
        return res;
    }
    json req_body = {{"version", model}, {"input", std::move(input)}};
    return replicate_post_poll_download(std::move(req_body), opts, progress, log_ctx, model);
}

// `img_bytes` empty = text-to-image (or text + reference images only); no main raster.
static GeminiCoreResult call_gemini_core(
    const std::vector<uint8_t>& img_bytes,
    const std::string& mime,
    const std::vector<std::pair<std::string,std::vector<uint8_t>>>& refs,  // mime + bytes
    const TransformOptions& opts,
    TransformProgressFn progress,
    const std::string& log_file = {})
{
    GeminiCoreResult res;

    // Build the multimodal `parts` array.  Order matters for Gemini: prompt
    // text first, then the *target* image to edit (if any), then any reference
    // images (logo / brand sheet / style swatch).
    json parts = json::array();
    parts.push_back({{"text", opts.prompt}});
    if (!img_bytes.empty()) {
        const std::string b64 = base64_encode(img_bytes.data(), img_bytes.size());
        parts.push_back({{"inlineData", {{"mimeType", mime}, {"data", b64}}}});
    }

    std::size_t ref_total_kb = 0;
    for (const auto& [rmime, rbytes] : refs) {
        if (rbytes.empty()) continue;
        const std::string rb64 = base64_encode(rbytes.data(), rbytes.size());
        parts.push_back({{"inlineData", {{"mimeType", rmime}, {"data", rb64}}}});
        ref_total_kb += rbytes.size() / 1024;
    }

    json gen_config = {
        {"responseModalities", json::array({"TEXT", "IMAGE"})}
    };

    json image_config;
    if (!opts.aspect_ratio.empty())
        image_config["aspectRatio"] = opts.aspect_ratio;
    if (!opts.image_size.empty())
        image_config["imageSize"] = opts.image_size;
    if (!image_config.empty())
        gen_config["imageConfig"] = image_config;

    json req_body = {
        {"contents", json::array({
            {{"parts", parts}}
        })},
        {"generationConfig", gen_config}
    };

    const std::string url = resolve_gemini_generate_url(opts.base_url, opts.model);

    std::string body_str = req_body.dump();

    if (progress) {
        std::string note;
        if (img_bytes.empty()) {
            note = "Creating image with " + opts.model;
            if (!refs.empty()) {
                note += " (+" + std::to_string(refs.size()) + " ref, "
                        + std::to_string(ref_total_kb) + " KB)";
            }
            note += "...";
        } else {
            note = "Sending to " + opts.model + " ("
                 + std::to_string(img_bytes.size() / 1024) + " KB image";
            if (!refs.empty()) {
                note += " + " + std::to_string(refs.size())
                      + " ref " + std::to_string(ref_total_kb) + " KB";
            }
            note += ")...";
        }
        progress(note);
    }

    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) {
        res.error = "curl_easy_init failed";
        return res;
    }

    std::string response_str;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth_header = "x-goog-api-key: " + opts.api_key;
    headers = curl_slist_append(headers, auth_header.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_str.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    const auto t0 = std::chrono::steady_clock::now();
    CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    {
        const char* tag = img_bytes.empty() ? "Gemini[create]: " : "Gemini[transform]: ";
        std::string line = std::string(tag) + std::string("POST ") + url + " model=" + opts.model + " HTTP "
                           + std::to_string(http_code) + " in " + std::to_string(ms) + " ms resp_bytes="
                           + std::to_string(response_str.size());
        if (!log_file.empty()) line += " file=" + log_file;
        logger::info(line);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        res.error = std::string("HTTP request failed: ") + curl_easy_strerror(cc);
        return res;
    }

    if (http_code != 200) {
        res.error = "API returned HTTP " + std::to_string(http_code);
        // Try to extract error message from response JSON
        try {
            auto j = json::parse(response_str);
            if (j.contains("error") && j["error"].contains("message"))
                res.error += ": " + j["error"]["message"].get<std::string>();
        } catch (...) {
            if (response_str.size() < 500) res.error += ": " + response_str;
        }
        return res;
    }

    // Parse response
    if (progress) progress("Parsing response...");
    json resp;
    try {
        resp = json::parse(response_str);
    } catch (const std::exception& e) {
        res.error = std::string("JSON parse error: ") + e.what();
        return res;
    }

    // Extract image and text from candidates[0].content.parts[]
    bool found_image = false;
    try {
        auto& parts = resp["candidates"][0]["content"]["parts"];
        for (auto& part : parts) {
            if (part.contains("inlineData")) {
                auto& id = part["inlineData"];
                std::string resp_mime = id.value("mimeType", "image/png");
                std::string resp_b64  = id["data"].get<std::string>();
                res.image_data = base64_decode(resp_b64);
                found_image = true;
            }
            if (part.contains("text")) {
                if (!res.ai_text.empty()) res.ai_text += "\n";
                res.ai_text += part["text"].get<std::string>();
            }
        }
    } catch (const std::exception& e) {
        res.error = std::string("Response parsing error: ") + e.what();
        // Include raw response excerpt for debugging
        if (response_str.size() < 2000) res.error += "\nRaw: " + response_str;
        return res;
    }

    // Accept text-only responses (e.g. SVG code, HTML, JSON) — Gemini returns
    // text when the prompt asks for textual output rather than image generation.
    // Only fail when the response is completely empty.
    if (!found_image && res.ai_text.empty()) {
        res.error = "No image or text in API response";
        return res;
    }

    res.ok = true;
    return res;
}

// ── File-based wrapper around the buffer core ──────────────────────────────

static TransformResult call_gemini(
    const std::string& input_path,
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress,
    BatchControl* batch,
    const std::function<void()>& on_before)
{
    TransformResult res;

    if (tr_stopped(batch, on_before)) {
        res.error = "transform: cancelled";
        return res;
    }

    // 1. Read target image (libvips for RAW/HEIC, or byte read for common rasters)
    std::vector<uint8_t> img_bytes;
    std::string          mime;
    std::string          lerr;
    if (!load_image_for_transform(input_path, "image", opts, progress, img_bytes, mime, lerr)) {
        res.error = lerr;
        return res;
    }
    if (tr_stopped(batch, on_before)) {
        res.error = "transform: cancelled";
        return res;
    }

    // 2. Read reference images from disk (file variant only).
    log_ref_paths_requested("transform", input_path, opts.reference_images);
    std::vector<std::pair<std::string,std::vector<uint8_t>>> refs;
    refs.reserve(opts.reference_images.size());
    for (const auto& ref_path : opts.reference_images) {
        if (ref_path.empty()) continue;
        if (tr_stopped(batch, on_before)) {
            res.error = "transform: cancelled";
            return res;
        }
        std::vector<uint8_t> rb;
        std::string          rm;
        const std::string    rlabel = fs::path(ref_path).filename().string();
        if (!load_image_for_transform(ref_path, "reference " + rlabel, opts, progress, rb, rm, lerr)) {
            res.error = lerr;
            return res;
        }
        if (rb.empty()) {
            logger::info(std::string("Gemini[transform]: reference skipped (empty read): ") + ref_path);
            continue;
        }
        refs.emplace_back(std::move(rm), std::move(rb));
        log_ref_disk_loaded("transform", ref_path, refs.back().first, refs.back().second.size());
    }
    if (tr_stopped(batch, on_before)) {
        res.error = "transform: cancelled";
        return res;
    }

    // 3. Call the core.
    if (!refs.empty()) {
        logger::info("Gemini[transform]: " + input_path + " - sending " + std::to_string(refs.size())
                     + " reference part(s) to model (plus main image + prompt)");
    }

    GeminiCoreResult gr = call_gemini_core(img_bytes, mime, refs, opts, progress, input_path);
    if (!gr.ok) { res.error = gr.error; return res; }
    res.ai_text    = gr.ai_text;
    res.image_data = gr.image_data;

    // 4. Write the output (file variant only).
    std::string out = output_path;
    if (out.empty()) out = default_transform_output(input_path, opts.prompt);
    fs::path out_dir = fs::path(out).parent_path();
    if (!out_dir.empty()) {
        std::error_code ec; fs::create_directories(out_dir, ec);
    }
    if (progress) progress("Writing " + out);
    std::ofstream ofs(out, std::ios::binary);
    if (!ofs) { res.error = "Cannot write output: " + out; return res; }
    ofs.write(reinterpret_cast<const char*>(res.image_data.data()),
              static_cast<std::streamsize>(res.image_data.size()));
    ofs.close();

    res.ok = true;
    res.output_path = out;
    return res;
}

// Text-to-image (and optional reference images only); no main input file.
static TransformResult call_gemini_create(
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress,
    BatchControl* batch,
    const std::function<void()>& on_before,
    const std::string& log_label)
{
    TransformResult res;

    if (tr_stopped(batch, on_before)) {
        res.error = "create_image: cancelled";
        return res;
    }

    log_ref_paths_requested("create", log_label, opts.reference_images);
    std::vector<std::pair<std::string,std::vector<uint8_t>>> refs;
    refs.reserve(opts.reference_images.size());
    std::string lerr;
    for (const auto& ref_path : opts.reference_images) {
        if (ref_path.empty()) continue;
        if (tr_stopped(batch, on_before)) {
            res.error = "create_image: cancelled";
            return res;
        }
        std::vector<uint8_t> rb;
        std::string          rm;
        const std::string    rlabel = fs::path(ref_path).filename().string();
        if (!load_image_for_transform(ref_path, "reference " + rlabel, opts, progress, rb, rm, lerr)) {
            res.error = lerr;
            return res;
        }
        if (rb.empty()) {
            logger::info(std::string("Gemini[create]: reference skipped (empty read): ") + ref_path);
            continue;
        }
        refs.emplace_back(std::move(rm), std::move(rb));
        log_ref_disk_loaded("create", ref_path, refs.back().first, refs.back().second.size());
    }
    if (tr_stopped(batch, on_before)) {
        res.error = "create_image: cancelled";
        return res;
    }

    if (!refs.empty()) {
        logger::info("Gemini[create]: " + log_label + " - sending " + std::to_string(refs.size())
                     + " reference part(s) to model (text-to-image + refs)");
    } else if (!opts.reference_images.empty()) {
        int n = 0;
        for (const auto& p : opts.reference_images)
            if (!p.empty()) ++n;
        if (n > 0)
            logger::info("Gemini[create]: " + log_label
                         + " - warning: reference paths were given but 0 valid images loaded; "
                         "proceeding with prompt only");
    }

    const std::vector<uint8_t> no_main;
    GeminiCoreResult gr = call_gemini_core(no_main, "image/png", refs, opts, progress, log_label);
    if (!gr.ok) { res.error = gr.error; return res; }
    res.ai_text    = gr.ai_text;
    res.image_data = gr.image_data;
    if (gr.image_data.empty()) {
        res.error = "create_image: no image in model response";
        return res;
    }

    std::string out = output_path;
    if (out.empty()) out = default_create_path_from_prompt(opts.prompt);
    fs::path out_dir = fs::path(out).parent_path();
    if (!out_dir.empty()) {
        std::error_code ec; fs::create_directories(out_dir, ec);
    }
    if (progress) progress("Writing " + out);
    std::ofstream ofs(out, std::ios::binary);
    if (!ofs) { res.error = "Cannot write output: " + out; return res; }
    ofs.write(reinterpret_cast<const char*>(res.image_data.data()),
              static_cast<std::streamsize>(res.image_data.size()));
    ofs.close();

    res.ok = true;
    res.output_path = out;
    return res;
}

// Pixlwiz uses an OpenAI-compatible images/generations endpoint.
struct PixlwizCoreResult {
    bool ok = false;
    std::string error;
    std::vector<uint8_t> image_data;
    std::string mime = "image/png";
    std::string ai_text; // Pixlwiz may return a caption/revised_prompt
};

static PixlwizCoreResult call_pixlwiz_core(
    const std::vector<uint8_t>& img_bytes,
    const std::string& mime,
    const std::vector<std::pair<std::string,std::vector<uint8_t>>>& refs,
    const TransformOptions& opts,
    TransformProgressFn progress,
    const std::string& log_file = {})
{
    PixlwizCoreResult res;
    const std::string log_ctx = log_file.empty() ? std::string("transform") : log_file;
    std::string model = opts.model;
    if (model.empty()) model = "image-generation-fast";

    // Build request body for OpenAI images/generations (DALL-E style)
    json req_body = {
        {"model", model},
        {"prompt", opts.prompt},
        {"n", 1},
        {"size", opts.image_size.empty() ? std::string("1024x1024") : opts.image_size},
        {"response_format", "b64_json"}
    };
    if (!opts.aspect_ratio.empty()) {
        // Map aspect ratio to size if image_size not explicitly set
        if (opts.image_size.empty()) {
            if (opts.aspect_ratio == "1:1") req_body["size"] = "1024x1024";
            else if (opts.aspect_ratio == "16:9") req_body["size"] = "1792x1024";
            else if (opts.aspect_ratio == "9:16") req_body["size"] = "1024x1792";
            else if (opts.aspect_ratio == "4:3") req_body["size"] = "1024x768";
            else if (opts.aspect_ratio == "3:4") req_body["size"] = "768x1024";
        }
    }

    // Pixlwiz supports image editing via image parameter (if img_bytes provided)
    if (!img_bytes.empty()) {
        auto to_data_url = [](const std::vector<uint8_t>& bytes, const std::string& m) {
            const std::string b64 = base64_encode(bytes.data(), bytes.size());
            return std::string("data:") + (m.empty() ? "image/png" : m) + ";base64," + b64;
        };
        req_body["image"] = to_data_url(img_bytes, mime);
    }
    // Reference images as array of data URLs if provided
    if (!refs.empty()) {
        auto to_data_url = [](const std::vector<uint8_t>& bytes, const std::string& m) {
            const std::string b64 = base64_encode(bytes.data(), bytes.size());
            return std::string("data:") + (m.empty() ? "image/png" : m) + ";base64," + b64;
        };
        json ref_arr = json::array();
        for (const auto& [rmime, rbytes] : refs) {
            if (rbytes.empty()) continue;
            ref_arr.push_back(to_data_url(rbytes, rmime));
        }
        if (!ref_arr.empty()) {
            req_body["reference_images"] = std::move(ref_arr);
        }
    }

    std::string base = opts.base_url;
    if (base.empty()) base = "https://llm.polymech.info/v1";
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    const std::string url = base + "/images/generations";

    if (progress) {
        std::string note = img_bytes.empty() ? "Creating image with Pixlwiz" : "Transforming image with Pixlwiz";
        note += " (" + model + ")...";
        progress(note);
    }

    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) {
        res.error = "curl_easy_init failed";
        return res;
    }

    std::string response_str;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth_header = "Authorization: Bearer " + opts.api_key;
    headers = curl_slist_append(headers, auth_header.c_str());

    std::string body_str = req_body.dump();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_str.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    const auto t0 = std::chrono::steady_clock::now();
    CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    {
        const char* tag = img_bytes.empty() ? "Pixlwiz[create]: " : "Pixlwiz[transform]: ";
        std::string line = std::string(tag) + std::string("POST ") + url + " model=" + opts.model + " HTTP "
                           + std::to_string(http_code) + " in " + std::to_string(ms) + " ms resp_bytes="
                           + std::to_string(response_str.size());
        if (!log_file.empty()) line += " file=" + log_file;
        logger::info(line);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        res.error = std::string("HTTP request failed: ") + curl_easy_strerror(cc);
        return res;
    }

    if (http_code != 200) {
        res.error = "API returned HTTP " + std::to_string(http_code);
        try {
            auto j = json::parse(response_str);
            if (j.contains("error") && j["error"].is_object() && j["error"].contains("message"))
                res.error += ": " + j["error"]["message"].get<std::string>();
        } catch (...) {
            if (response_str.size() < 500) res.error += ": " + response_str;
        }
        return res;
    }

    // Parse response - OpenAI images/generations format: {"data": [{"b64_json": "...", "revised_prompt": "..."}]}
    if (progress) progress("Parsing response...");
    json resp;
    try {
        resp = json::parse(response_str);
    } catch (const std::exception& e) {
        res.error = std::string("JSON parse error: ") + e.what();
        return res;
    }

    try {
        if (resp.contains("data") && resp["data"].is_array() && !resp["data"].empty()) {
            auto& first = resp["data"][0];
            if (first.contains("b64_json") && first["b64_json"].is_string()) {
                std::string b64 = first["b64_json"].get<std::string>();
                res.image_data = base64_decode(b64);
                res.ok = true;
            }
            if (first.contains("revised_prompt") && first["revised_prompt"].is_string()) {
                res.ai_text = first["revised_prompt"].get<std::string>();
            }
        }
        if (!res.ok) {
            res.error = "No image data in response";
        }
    } catch (const std::exception& e) {
        res.error = std::string("Failed to extract image: ") + e.what();
    }
    return res;
}

// ── public API ──────────────────────────────────────────────────────

std::string default_transform_output(const std::string& input_path, const std::string& prompt) {
    fs::path p(input_path);
    std::string stem = p.stem().string();
    std::string ext  = p.extension().string();
    if (ext.empty()) ext = ".png";
    const std::string clean = prompt_slug_for_filename(prompt);
    return (p.parent_path() / (stem + "_" + clean + ext)).string();
}

std::string default_create_image_output(const std::string& prompt) {
    return default_create_path_from_prompt(prompt);
}

TransformResult transform_image(
    const std::string& input_path,
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress,
    BatchControl* batch,
    std::function<void()> on_before_batch_pause)
{
    TransformOptions o = opts;
    media::runtime_settings::merge_provider_credentials(
        o.provider, false, o.api_key, o.base_url);
#if defined(_WIN32)
    {
        std::ostringstream os;
        os << "transform_image: after merge_image_provider_credentials provider=\"" << o.provider << "\" model=\""
           << o.model << "\" api_key=" << (o.api_key.empty() ? "empty" : "set") << " base_url="
           << (o.base_url.empty() ? "empty" : "set")
#if defined(_WIN32)
           << " (Win: same `providers` store as path tools + chat; then call_gemini if provider==google)";
#else
           << " (portable settings merge; then call_gemini if provider==google)";
#endif
        append_transform_iexecute_correlation_log_utf8(os.str());
    }
#endif
    if (o.prompt.empty()) {
        return {false, "prompt is required"};
    }
    if (o.api_key.empty()) {
        return {false, "API key is required (configure the provider in app settings, or pass --api-key)"};
    }

    if (o.provider == "google") {
        return call_gemini(input_path, output_path, o, progress, batch,
                          on_before_batch_pause);
    }
    if (o.provider == "replicate") {
        std::vector<uint8_t> img_bytes;
        std::string          mime;
        std::string          lerr;
        if (!load_image_for_transform(input_path, "image", o, progress, img_bytes, mime, lerr)) {
            return {false, lerr};
        }
        std::vector<std::pair<std::string,std::vector<uint8_t>>> refs;
        refs.reserve(o.reference_images.size());
        for (const auto& ref_path : o.reference_images) {
            if (ref_path.empty()) continue;
            std::vector<uint8_t> rb;
            std::string          rm;
            if (!load_image_for_transform(ref_path, "reference", o, progress, rb, rm, lerr)) {
                return {false, lerr};
            }
            if (!rb.empty()) refs.emplace_back(std::move(rm), std::move(rb));
        }
        auto rr = call_replicate_core(img_bytes, mime, refs, o, progress, input_path);
        if (!rr.ok) {
            TransformResult fail;
            fail.ok                          = false;
            fail.error                       = rr.error;
            fail.replicate_prediction_web_url = rr.prediction_web_url;
            return fail;
        }
        TransformResult out;
        out.ok = true;
        out.ai_text = rr.ai_text;
        out.image_data = std::move(rr.image_data);
        out.replicate_prediction_web_url = rr.prediction_web_url;
        out.output_path = output_path.empty() ? default_transform_output(input_path, o.prompt) : output_path;
        fs::path out_dir = fs::path(out.output_path).parent_path();
        if (!out_dir.empty()) {
            std::error_code ec; fs::create_directories(out_dir, ec);
        }
        std::ofstream ofs(out.output_path, std::ios::binary);
        if (!ofs) return {false, "Cannot write output: " + out.output_path};
        ofs.write(reinterpret_cast<const char*>(out.image_data.data()),
                  static_cast<std::streamsize>(out.image_data.size()));
        ofs.close();
        return out;
    }
    if (o.provider == "pixlwiz") {
        std::vector<uint8_t> img_bytes;
        std::string          mime;
        std::string          lerr;
        if (!load_image_for_transform(input_path, "image", o, progress, img_bytes, mime, lerr)) {
            return {false, lerr};
        }
        std::vector<std::pair<std::string,std::vector<uint8_t>>> refs;
        refs.reserve(o.reference_images.size());
        for (const auto& ref_path : o.reference_images) {
            if (ref_path.empty()) continue;
            std::vector<uint8_t> rb;
            std::string          rm;
            if (!load_image_for_transform(ref_path, "reference", o, progress, rb, rm, lerr)) {
                return {false, lerr};
            }
            if (!rb.empty()) refs.emplace_back(std::move(rm), std::move(rb));
        }
        auto pr = call_pixlwiz_core(img_bytes, mime, refs, o, progress, input_path);
        if (!pr.ok) {
            return {false, pr.error};
        }
        TransformResult out;
        out.ok = true;
        out.ai_text = pr.ai_text;
        out.image_data = std::move(pr.image_data);
        out.output_path = output_path.empty() ? default_transform_output(input_path, o.prompt) : output_path;
        fs::path out_dir = fs::path(out.output_path).parent_path();
        if (!out_dir.empty()) {
            std::error_code ec; fs::create_directories(out_dir, ec);
        }
        std::ofstream ofs(out.output_path, std::ios::binary);
        if (!ofs) return {false, "Cannot write output: " + out.output_path};
        ofs.write(reinterpret_cast<const char*>(out.image_data.data()),
                  static_cast<std::streamsize>(out.image_data.size()));
        ofs.close();
        return out;
    }

    return {false, "Unsupported provider: " + o.provider};
}

TransformResult create_image(
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress,
    BatchControl* batch,
    std::function<void()> on_before_batch_pause)
{
    TransformOptions o = opts;
    media::runtime_settings::merge_provider_credentials(
        o.provider, false, o.api_key, o.base_url);
    if (o.prompt.empty()) {
        return {false, "prompt is required"};
    }
    if (o.api_key.empty()) {
        return {false, "API key is required (configure the provider in app settings, or pass --api-key)"};
    }
    if (o.provider == "google") {
        return call_gemini_create(output_path, o, progress, batch, on_before_batch_pause,
                                  "<create_image>");
    }
    if (o.provider == "replicate") {
        std::vector<std::pair<std::string,std::vector<uint8_t>>> refs;
        std::string lerr;
        for (const auto& ref_path : o.reference_images) {
            if (ref_path.empty()) continue;
            std::vector<uint8_t> rb;
            std::string          rm;
            if (!load_image_for_transform(ref_path, "reference", o, progress, rb, rm, lerr)) {
                return {false, lerr};
            }
            if (!rb.empty()) refs.emplace_back(std::move(rm), std::move(rb));
        }
        const std::vector<uint8_t> no_main;
        auto rr = call_replicate_core(no_main, "image/png", refs, o, progress, "<create_image>");
        if (!rr.ok) {
            TransformResult fail;
            fail.ok                          = false;
            fail.error                       = rr.error;
            fail.replicate_prediction_web_url = rr.prediction_web_url;
            return fail;
        }
        TransformResult out;
        out.ok = true;
        out.ai_text = rr.ai_text;
        out.image_data = std::move(rr.image_data);
        out.replicate_prediction_web_url = rr.prediction_web_url;
        out.output_path = output_path.empty() ? default_create_path_from_prompt(o.prompt) : output_path;
        fs::path out_dir = fs::path(out.output_path).parent_path();
        if (!out_dir.empty()) {
            std::error_code ec; fs::create_directories(out_dir, ec);
        }
        std::ofstream ofs(out.output_path, std::ios::binary);
        if (!ofs) return {false, "Cannot write output: " + out.output_path};
        ofs.write(reinterpret_cast<const char*>(out.image_data.data()),
                  static_cast<std::streamsize>(out.image_data.size()));
        ofs.close();
        return out;
    }
    if (o.provider == "pixlwiz") {
        std::vector<std::pair<std::string,std::vector<uint8_t>>> refs;
        std::string lerr;
        for (const auto& ref_path : o.reference_images) {
            if (ref_path.empty()) continue;
            std::vector<uint8_t> rb;
            std::string          rm;
            if (!load_image_for_transform(ref_path, "reference", o, progress, rb, rm, lerr)) {
                return {false, lerr};
            }
            if (!rb.empty()) refs.emplace_back(std::move(rm), std::move(rb));
        }
        const std::vector<uint8_t> no_main;
        auto pr = call_pixlwiz_core(no_main, "image/png", refs, o, progress, "<create_image>");
        if (!pr.ok) {
            return {false, pr.error};
        }
        TransformResult out;
        out.ok = true;
        out.ai_text = pr.ai_text;
        out.image_data = std::move(pr.image_data);
        out.output_path = output_path.empty() ? default_create_path_from_prompt(o.prompt) : output_path;
        fs::path out_dir = fs::path(out.output_path).parent_path();
        if (!out_dir.empty()) {
            std::error_code ec; fs::create_directories(out_dir, ec);
        }
        std::ofstream ofs(out.output_path, std::ios::binary);
        if (!ofs) return {false, "Cannot write output: " + out.output_path};
        ofs.write(reinterpret_cast<const char*>(out.image_data.data()),
                  static_cast<std::streamsize>(out.image_data.size()));
        ofs.close();
        return out;
    }
    return {false, "Unsupported provider: " + o.provider};
}

std::string default_replicate_video_output_path(const std::string& prompt, const std::string& preferred_parent_dir) {
    std::string clean = prompt_slug_for_filename(prompt);
    if (clean.empty()) clean = "out";
    fs::path base = preferred_parent_dir.empty() ? fs::current_path() : fs::path(preferred_parent_dir);
    return (base / ("video_" + clean + ".mp4")).string();
}

TransformResult replicate_video_frames(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& frames_mime_bytes,
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress,
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& reference_mime_bytes)
{
    TransformOptions o = opts;
    media::runtime_settings::merge_provider_credentials(
        o.provider, false, o.api_key, o.base_url);
    if (o.provider != "replicate")
        return {false, "replicate_video_frames: provider must be replicate"};
    if (o.prompt.empty())
        return {false, "prompt is required"};
    if (o.api_key.empty())
        return {false, "API key is required (configure Replicate in app settings, or pass options.api_key)"};
    if (o.model.empty())
        return {false, "model is required (Replicate slug, e.g. minimax/video-01-live)"};

    auto rr = call_replicate_video_frames_core(frames_mime_bytes, reference_mime_bytes, o, progress, "<replicate_video>");
    if (!rr.ok) {
        TransformResult fail;
        fail.ok                          = false;
        fail.error                       = rr.error;
        fail.replicate_prediction_web_url = rr.prediction_web_url;
        return fail;
    }

    std::string out_path = output_path;
    if (out_path.empty())
        out_path = default_replicate_video_output_path(o.prompt);
    /* If caller used a .png default but we got video bytes, swap extension from MIME. */
    if (rr.mime.find("video/") == 0) {
        std::string ext = ".mp4";
        if (rr.mime.find("webm") != std::string::npos) ext = ".webm";
        else if (rr.mime.find("quicktime") != std::string::npos) ext = ".mov";
        if (out_path.size() >= 4) {
            std::string tail = out_path.substr(out_path.size() - 4);
            for (auto& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tail == ".png") {
                out_path.resize(out_path.size() - 4);
                out_path += ext;
            }
        }
    }

    fs::path out_dir = fs::path(out_path).parent_path();
    if (!out_dir.empty()) {
        std::error_code ec;
        fs::create_directories(out_dir, ec);
    }
    std::ofstream ofs(out_path, std::ios::binary);
    if (!ofs)
        return {false, "Cannot write output: " + out_path};
    ofs.write(reinterpret_cast<const char*>(rr.image_data.data()),
              static_cast<std::streamsize>(rr.image_data.size()));
    ofs.close();
    TransformResult tr;
    tr.ok                            = true;
    tr.ai_text                       = rr.ai_text;
    tr.image_data                    = std::move(rr.image_data);
    tr.output_path                   = std::move(out_path);
    tr.replicate_prediction_web_url = rr.prediction_web_url;
    return tr;
}

TransformResult replicate_video_replicate_input(
    json input,
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress)
{
    TransformOptions o = opts;
    media::runtime_settings::merge_provider_credentials(
        o.provider, false, o.api_key, o.base_url);
    if (o.provider != "replicate")
        return {false, "replicate_video_replicate_input: provider must be replicate"};
    if (o.api_key.empty())
        return {false, "API key is required (configure Replicate in app settings, or pass options.api_key)"};
    if (o.model.empty())
        return {false, "model is required (Replicate slug, e.g. google/veo-3.1-fast)"};

    if (input.contains("prompt") && input["prompt"].is_string())
        o.prompt = input["prompt"].get<std::string>();

    const std::string video_parent = first_existing_file_parent_from_input_json(input);
    std::string       res_err;
    replicate_resolve_local_file_strings_in_input(input, o, progress, res_err);
    if (!res_err.empty())
        return {false, res_err};

    auto rr = call_replicate_video_raw_input(std::move(input), o, progress, "<replicate_video_input>");
    if (!rr.ok) {
        TransformResult fail;
        fail.ok                          = false;
        fail.error                       = rr.error;
        fail.replicate_prediction_web_url = rr.prediction_web_url;
        return fail;
    }

    std::string out_path = output_path;
    if (out_path.empty())
        out_path = default_replicate_video_output_path(o.prompt, video_parent);
    if (rr.mime.find("video/") == 0) {
        std::string ext = ".mp4";
        if (rr.mime.find("webm") != std::string::npos)
            ext = ".webm";
        else if (rr.mime.find("quicktime") != std::string::npos)
            ext = ".mov";
        if (out_path.size() >= 4) {
            std::string tail = out_path.substr(out_path.size() - 4);
            for (auto& c : tail)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (tail == ".png") {
                out_path.resize(out_path.size() - 4);
                out_path += ext;
            }
        }
    }

    fs::path out_dir = fs::path(out_path).parent_path();
    if (!out_dir.empty()) {
        std::error_code ec;
        fs::create_directories(out_dir, ec);
    }
    std::ofstream ofs(out_path, std::ios::binary);
    if (!ofs)
        return {false, "Cannot write output: " + out_path};
    ofs.write(reinterpret_cast<const char*>(rr.image_data.data()),
              static_cast<std::streamsize>(rr.image_data.size()));
    ofs.close();
    TransformResult tr;
    tr.ok                            = true;
    tr.ai_text                       = rr.ai_text;
    tr.image_data                    = std::move(rr.image_data);
    tr.output_path                   = std::move(out_path);
    tr.replicate_prediction_web_url = rr.prediction_web_url;
    return tr;
}

// ── Buffer-only variant (zero-fs) ────────────────────────────────────────────

TransformBufferResult transform_buffer(
    const void* in_data, std::size_t in_size,
    const std::string& in_mime,
    const TransformOptions& opts,
    const std::vector<ReferenceBuffer>& ref_buffers)
{
    TransformBufferResult res;
    TransformOptions o = opts;
    media::runtime_settings::merge_provider_credentials(
        o.provider, false, o.api_key, o.base_url);
    if (!in_data || in_size == 0) { res.error = "transform_buffer: empty input"; return res; }
    if (o.prompt.empty()) { res.error = "prompt is required"; return res; }
    if (o.api_key.empty()) { res.error = "API key is required"; return res; }
    if (o.provider != "google" && o.provider != "replicate" && o.provider != "pixlwiz") {
        res.error = "Unsupported provider: " + o.provider;
        return res;
    }

    std::vector<uint8_t> img_bytes(static_cast<const uint8_t*>(in_data),
                                    static_cast<const uint8_t*>(in_data) + in_size);

    // Reference images come from the caller's in-memory buffers (no fs).
    log_ref_memory_buffers("transform", "<transform_buffer>", ref_buffers);
    std::vector<std::pair<std::string,std::vector<uint8_t>>> refs;
    refs.reserve(ref_buffers.size());
    for (const auto& rb : ref_buffers) {
        if (rb.bytes.empty()) continue;
        refs.emplace_back(
            rb.mime.empty() ? "image/png" : rb.mime,
            std::vector<uint8_t>(rb.bytes.begin(), rb.bytes.end()));
    }

    if (o.provider == "google") {
        GeminiCoreResult gr = call_gemini_core(img_bytes,
                                                in_mime.empty() ? "image/png" : in_mime,
                                                refs, o, /*progress*/ nullptr, "<buffer>");
        if (!gr.ok) { res.error = gr.error; return res; }
        res.bytes.assign(reinterpret_cast<const char*>(gr.image_data.data()),
                         gr.image_data.size());
        res.mime    = "image/png";
        res.ai_text = gr.ai_text;
    } else if (o.provider == "replicate") {
        auto rr = call_replicate_core(img_bytes, in_mime.empty() ? "image/png" : in_mime, refs, o, nullptr, "<buffer>");
        if (!rr.ok) { res.error = rr.error; return res; }
        res.bytes.assign(reinterpret_cast<const char*>(rr.image_data.data()), rr.image_data.size());
        res.mime    = rr.mime.empty() ? "image/png" : rr.mime;
        res.ai_text = rr.ai_text;
    } else { // pixlwiz
        auto pr = call_pixlwiz_core(img_bytes, in_mime.empty() ? "image/png" : in_mime, refs, o, nullptr, "<buffer>");
        if (!pr.ok) { res.error = pr.error; return res; }
        res.bytes.assign(reinterpret_cast<const char*>(pr.image_data.data()), pr.image_data.size());
        res.mime    = "image/png";
        res.ai_text = pr.ai_text;
    }
    res.ok = true;
    return res;
}

TransformBufferResult create_buffer(
    const TransformOptions& opts,
    const std::vector<ReferenceBuffer>& ref_buffers)
{
    TransformBufferResult res;
    TransformOptions o = opts;
    media::runtime_settings::merge_provider_credentials(
        o.provider, false, o.api_key, o.base_url);
    if (o.prompt.empty()) { res.error = "prompt is required"; return res; }
    if (o.api_key.empty()) { res.error = "API key is required"; return res; }
    if (o.provider != "google" && o.provider != "replicate" && o.provider != "pixlwiz") {
        res.error = "Unsupported provider: " + o.provider;
        return res;
    }

    log_ref_memory_buffers("create", "<create_buffer>", ref_buffers);
    std::vector<std::pair<std::string,std::vector<uint8_t>>> refs;
    refs.reserve(ref_buffers.size());
    for (const auto& rb : ref_buffers) {
        if (rb.bytes.empty()) continue;
        refs.emplace_back(
            rb.mime.empty() ? "image/png" : rb.mime,
            std::vector<uint8_t>(rb.bytes.begin(), rb.bytes.end()));
    }

    if (o.provider == "google") {
        const std::vector<uint8_t> no_main;
        GeminiCoreResult gr = call_gemini_core(no_main, "image/png", refs, o, /*progress*/ nullptr, "<create_buffer>");
        if (!gr.ok) { res.error = gr.error; return res; }
        if (gr.image_data.empty()) {
            res.error = "create_buffer: no image in model response";
            return res;
        }
        res.bytes.assign(reinterpret_cast<const char*>(gr.image_data.data()),
                         gr.image_data.size());
        res.mime    = "image/png";
        res.ai_text = gr.ai_text;
    } else if (o.provider == "replicate") {
        const std::vector<uint8_t> no_main;
        auto rr = call_replicate_core(no_main, "image/png", refs, o, nullptr, "<create_buffer>");
        if (!rr.ok) { res.error = rr.error; return res; }
        if (rr.image_data.empty()) { res.error = "create_buffer: no image in model response"; return res; }
        res.bytes.assign(reinterpret_cast<const char*>(rr.image_data.data()), rr.image_data.size());
        res.mime    = rr.mime.empty() ? "image/png" : rr.mime;
        res.ai_text = rr.ai_text;
    } else { // pixlwiz
        const std::vector<uint8_t> no_main;
        auto pr = call_pixlwiz_core(no_main, "image/png", refs, o, nullptr, "<create_buffer>");
        if (!pr.ok) { res.error = pr.error; return res; }
        if (pr.image_data.empty()) { res.error = "create_buffer: no image in model response"; return res; }
        res.bytes.assign(reinterpret_cast<const char*>(pr.image_data.data()), pr.image_data.size());
        res.mime    = "image/png";
        res.ai_text = pr.ai_text;
    }
    res.ok = true;
    return res;
}

// ── JSON option mapping ──────────────────────────────────────────────────────

void apply_transform_options_from_json(const nlohmann::json& j, TransformOptions& opts) {
    auto str = [&](const char* key, std::string& dst) {
        if (j.contains(key) && j[key].is_string()) dst = j[key].get<std::string>();
    };
    auto num = [&](const char* key, int& dst) {
        if (j.contains(key) && j[key].is_number_integer()) dst = j[key].get<int>();
    };
    auto boolean = [&](const char* key, bool& dst) {
        if (!j.contains(key) || j[key].is_null()) return;
        if (j[key].is_boolean()) dst = j[key].get<bool>();
        else if (j[key].is_number_integer()) dst = j[key].get<int>() != 0;
    };
    str("provider",     opts.provider);
    str("model",        opts.model);
    str("api_key",      opts.api_key);
    str("base_url",     opts.base_url);
    str("prompt",       opts.prompt);
    str("aspect_ratio", opts.aspect_ratio);
    str("image_size",   opts.image_size);
    boolean("resize_first", opts.resize_first);
    num    ("resize_width", opts.resize_width);
    boolean("preresize_raw_only", opts.preresize_raw_only);

    // reference_images: array of host paths; or a single string → one ref.
    auto load_refs = [&](const char* key) {
        if (!j.contains(key)) return;
        const auto& v = j[key];
        if (v.is_string()) {
            opts.reference_images.push_back(v.get<std::string>());
        } else if (v.is_array()) {
            for (const auto& item : v) {
                if (item.is_string()) opts.reference_images.push_back(item.get<std::string>());
            }
        }
    };
    load_refs("reference_images");
    load_refs("references");        // friendly alias
    load_refs("reference");         // singular alias
    str("replicate_first_frame_key", opts.replicate_first_frame_key);
    str("replicate_second_frame_key", opts.replicate_second_frame_key);

    // JSON merge only sets keys that are present as strings; otherwise clear so callers can fill
    // from settings/CLI (and stale values are not kept across a second merge with a smaller @p j).
    if (!j.contains("provider") || !j["provider"].is_string())
        opts.provider.clear();
    if (!j.contains("model") || !j["model"].is_string())
        opts.model.clear();
}

} // namespace media
