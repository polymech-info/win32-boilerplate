#include "meta.hpp"
#include "core/settings_portable.hpp"
#include "core/settings_runtime.hpp"
#include "llm_jpeg_from_path.hpp"
#include "replicate_http_log.hpp"
#include "url_fetch.hpp"

#include "logger/logger.h"

#include <vips/vips.h>
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
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace media {

namespace {

// ── vips bootstrap (mirrors resize.cpp) ─────────────────────────────────────
std::once_flag g_vips_init;
void ensure_vips() {
    std::call_once(g_vips_init, []() {
        if (vips_init("media-img"))
            std::abort();
    });
}
std::string vips_err() {
    const char* buf = vips_error_buffer();
    std::string s = buf ? buf : "vips error";
    vips_error_clear();
    return s;
}

// ── base64 (duplicate of transform.cpp helper to avoid header coupling) ─────
const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(b64_table[(n >> 18) & 0x3F]);
        out.push_back(b64_table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? b64_table[n & 0x3F] : '=');
    }
    return out;
}

std::string mime_from_ext(const std::string& ext) {
    std::string e = ext;
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
    if (e == ".png")  return "image/png";
    if (e == ".webp") return "image/webp";
    if (e == ".gif")  return "image/gif";
    if (e == ".tif" || e == ".tiff") return "image/tiff";
    if (e == ".avif") return "image/avif";
    if (e == ".heic" || e == ".heif") return "image/heic";
    return "image/jpeg";
}

size_t curl_write_string(char* ptr, size_t size, size_t nmemb, void* ud) {
    auto* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string resolve_replicate_predict_url(const std::string& base_url) {
    std::string base = base_url;
    if (base.empty()) base = "https://api.replicate.com/v1";
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    if (base.size() >= 3 && base.substr(base.size() - 3) == "/v1") return base + "/predictions";
    return base + "/v1/predictions";
}

// Replicate's Gemini often returns a *single* JSON (or ```json``` block) split across many array
// strings. Joining those with newlines can break string literals, so for all-string arrays we
// concatenate with no separator (same idea as the Gemini API path, which appends .text parts).
std::string extract_text_from_replicate_output(const json& output) {
    if (output.is_string()) return output.get<std::string>();
    if (output.is_array()) {
        bool all_strings = !output.empty();
        for (const auto& item : output) {
            if (!item.is_string()) {
                all_strings = false;
                break;
            }
        }
        if (all_strings) {
            std::string cat;
            for (const auto& item : output) {
                cat += item.get<std::string>();
            }
            return cat;
        }
        std::string joined;
        for (const auto& item : output) {
            if (item.is_string()) {
                if (!joined.empty()) joined += "\n";
                joined += item.get<std::string>();
            } else if (item.is_object() || item.is_array()) {
                if (!joined.empty()) joined += "\n";
                joined += item.dump();
            }
        }
        return joined;
    }
    if (output.is_object()) return output.dump();
    return {};
}

static std::string strip_markdown_code_fence(std::string t) {
    auto trim2 = [](std::string& s) {
        const auto a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) {
            s.clear();
            return;
        }
        const auto b = s.find_last_not_of(" \t\r\n");
        s = s.substr(a, b - a + 1);
    };
    trim2(t);
    if (t.size() >= 3 && t.compare(0, 3, "```") == 0) {
        const size_t nl = t.find('\n');
        if (nl == std::string::npos) return t;
        t = t.substr(nl + 1);
        const size_t close = t.rfind("```");
        if (close != std::string::npos) t.resize(close);
        trim2(t);
    }
    return t;
}

static void normalize_confidence_in_payload(json& o) {
    if (!o.is_object() || !o.contains("confidence")) return;
    auto& c = o["confidence"];
    if (!c.is_number()) return;
    double v = c.get<double>();
    if (v > 1.0 && v <= 10.0) c = v / 10.0;
    else if (v > 10.0 && v <= 100.0) c = v / 100.0;
    else if (v > 1.0) c = 1.0;
    else if (v < 0.0) c = 0.0;
}

bool parse_or_wrap_meta_payload(const std::string& model_text,
                                const std::string& input_path,
                                json& payload_out) {
    std::string txt = model_text;
    const auto first = txt.find_first_not_of(" \t\r\n");
    const auto last  = txt.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) txt.clear();
    else txt = txt.substr(first, last - first + 1);

    const std::string unfenced = strip_markdown_code_fence(txt);
    std::string       brace_sub;
    {
        const std::string& base = unfenced.empty() ? txt : unfenced;
        const size_t       l    = base.find('{');
        const size_t       r    = base.rfind('}');
        if (l != std::string::npos && r != std::string::npos && r > l) brace_sub = base.substr(l, r - l + 1);
    }

    auto try_parse_object = [&](const std::string& s) -> bool {
        if (s.empty()) return false;
        try {
            json j = json::parse(s);
            if (j.is_object()) {
                payload_out = std::move(j);
                normalize_confidence_in_payload(payload_out);
                return true;
            }
        } catch (...) {}
        return false;
    };
    if (try_parse_object(txt) || try_parse_object(unfenced) || try_parse_object(brace_sub)) return true;

    // Fallback for models that return plain text instead of strict JSON.
    payload_out = json::object();
    payload_out["title"] = fs::path(input_path).stem().string();
    payload_out["alt"]   = txt.size() > 125 ? txt.substr(0, 125) : txt;
    payload_out["description"] = txt.empty() ? "No textual metadata returned by model." : txt;
    payload_out["scene"] = "unknown";
    payload_out["estimated_location"] = "unknown";
    payload_out["characters"] = json::array();
    payload_out["objects"]  = json::array();
    payload_out["tags"]     = json::array();
    payload_out["confidence"] = 0.0;
    return true;
}

/// macOS `PolymechReplicateModelBrowser` (and any copy/paste) can produce `g/google/...` by
/// prepending the first character of a slug. Reject that mistaken prefix so `version` matches
/// Replicate's `owner/model` slug and we don't get HTTP 404.
static void strip_replicate_model_display_prefix(std::string& m) {
    if (m.size() < 3 || m[1] != '/') return;
    const std::string rest = m.substr(2);
    if (rest.empty()) return;
    if (static_cast<char>(std::tolower(static_cast<unsigned char>(m[0])))
        == static_cast<char>(std::tolower(static_cast<unsigned char>(rest.front())))) {
        m = rest;
    }
}

// Meta shares one optional `base_url` field with the Gemini path; a leftover `generativelanguage` URL
// makes `resolve_replicate_predict_url` point at the wrong host → HTTP 4xx.
static std::string base_url_for_replicate_from_opts(const std::string& in) {
    if (in.empty()) return in;
    std::string l = in;
    for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l.find("replicate.com") != std::string::npos) return in;
    if (l.rfind("http://localhost", 0) == 0 || l.rfind("https://localhost", 0) == 0) return in;
    if (l.rfind("http://127.0.0.1", 0) == 0 || l.rfind("https://127.0.0.1", 0) == 0) return in;
    logger::info(
        "Replicate[meta] base_url is not a Replicate API base; using default. Ignored value was: " + in);
    return {};
}

// Replicate model input keys differ: `google/gemini-2.5-flash` (vision + text) uses `images` (url or data: URI).
// Image generation / edit models (e.g. gemini-2.5-flash-image, nano-banana) use `image_input`.
// Sending the wrong key drops the image → text-only, hallucinated captions.
static bool replicate_meta_model_uses_images_key(const std::string& model) {
    if (model.find("gemini-2.5-flash-image") != std::string::npos) return false;
    if (model == "google/gemini-2.5-flash") return true;
    return false;
}

// Extract JSON from markdown code blocks (e.g., ```json {...} ```)
static std::string extract_json_from_markdown(const std::string& text) {
    // Look for ```json or ``` followed by JSON
    size_t start = text.find("```json");
    if (start != std::string::npos) {
        start += 7; // Skip past ```json
    } else {
        start = text.find("```");
        if (start != std::string::npos) {
            start += 3; // Skip past ```
        }
    }
    if (start == std::string::npos) return text; // No markdown fences, return as-is

    // Find the closing ```
    size_t end = text.find("```", start);
    if (end == std::string::npos) end = text.size();

    // Extract and trim
    std::string extracted = text.substr(start, end - start);
    // Trim leading/trailing whitespace
    size_t first = extracted.find_first_not_of(" \t\r\n");
    size_t last = extracted.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return extracted;
    return extracted.substr(first, last - first + 1);
}

// Pixlwiz uses OpenAI-compatible chat completions with vision support
static bool call_pixlwiz_meta(const MetaOptions& opts,
                              const std::vector<uint8_t>& image_bytes,
                              const std::string& mime,
                              const std::string& full_prompt,
                              MetaProgressFn progress,
                              json& payload_out,
                              std::string& err,
                              const std::string& log_file = {})
{
    std::string model = opts.model;
    if (model.empty()) model = "gpt-4o-mini";

    // Build data URL for the image
    std::string data_url = std::string("data:") + (mime.empty() ? "image/png" : mime)
                           + ";base64," + base64_encode(image_bytes.data(), image_bytes.size());

    // OpenAI vision format: messages with image_url content
    json req = {
        {"model", model},
        {"messages", json::array({
            {{
                {"role", "user"},
                {"content", json::array({
                    {{{"type", "text"}, {"text", full_prompt}}},
                    {{{"type", "image_url"}, {"image_url", {{"url", data_url}}}}}
                })}
            }}
        })},
        {"max_tokens", 4096}
    };

    std::string base = opts.base_url;
    if (base.empty()) base = "https://llm.polymech.info/v1";
    while (!base.empty() && (base.back() == '/' || base.back() == '\\')) base.pop_back();
    const std::string url = base + "/chat/completions";

    std::string body = req.dump();

    if (progress) progress("POST " + model + " (" + std::to_string(image_bytes.size() / 1024) + " KB image)");

    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) { err = "curl_easy_init failed"; return false; }

    std::string response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    std::string auth = "Authorization: Bearer " + opts.api_key;
    hdrs = curl_slist_append(hdrs, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    const auto t0 = std::chrono::steady_clock::now();
    CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    {
        std::string line = std::string("Pixlwiz[meta]: POST ") + url + " model=" + opts.model + " HTTP "
                           + std::to_string(http_code) + " in " + std::to_string(ms) + " ms resp_bytes="
                           + std::to_string(response.size());
        if (!log_file.empty()) line += " file=" + log_file;
        logger::info(line);
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err = std::string("HTTP error: ") + curl_easy_strerror(cc);
        return false;
    }
    if (http_code != 200) {
        err = "API HTTP " + std::to_string(http_code);
        try {
            auto j = json::parse(response);
            if (j.contains("error") && j["error"].is_object() && j["error"].contains("message"))
                err += ": " + j["error"]["message"].get<std::string>();
        } catch (...) {
            if (response.size() < 800) err += ": " + response;
        }
        return false;
    }

    if (progress) progress("Parsing response");

    json resp;
    try { resp = json::parse(response); }
    catch (const std::exception& e) {
        err = std::string("response JSON parse error: ") + e.what();
        return false;
    }

    // Extract text from choices[0].message.content
    std::string text;
    try {
        if (resp.contains("choices") && resp["choices"].is_array() && !resp["choices"].empty()) {
            auto& first = resp["choices"][0];
            if (first.contains("message") && first["message"].is_object()) {
                auto& msg = first["message"];
                if (msg.contains("content") && msg["content"].is_string()) {
                    text = msg["content"].get<std::string>();
                }
            }
        }
    } catch (...) {
        err = "API response missing choices[0].message.content";
        return false;
    }
    if (text.empty()) {
        err = "Empty content in API response";
        return false;
    }

    // Try to extract JSON from markdown code block if present
    std::string json_text = extract_json_from_markdown(text);
    if (json_text.empty()) json_text = text;

    json parsed;
    try { parsed = json::parse(json_text); }
    catch (const std::exception& e) {
        err = std::string("JSON extraction failed: ") + e.what();
        return false;
    }

    payload_out = std::move(parsed);
    return true;
}

bool call_replicate_meta(const MetaOptions& opts,
                         const std::vector<uint8_t>& image_bytes,
                         const std::string& mime,
                         const std::string& full_prompt,
                         MetaProgressFn progress,
                         json& payload_out,
                         std::string& err,
                         const std::string& input_path = {})
{
    const std::string model_in = opts.model;
    std::string       model     = model_in;
    strip_replicate_model_display_prefix(model);
    if (model != model_in) {
        logger::info(std::string("Replicate[meta] normalized model slug from '") + model_in + "' to '"
                      + model + "' (UI duplicate prefix strip; must be owner/model for `version` field)");
    }
    if (model.empty() || model.find("gemini-") == 0) model = "google/gemini-2.5-flash";
    const std::string     base_effective  = base_url_for_replicate_from_opts(opts.base_url);
    const std::string     base_ignored   = (base_effective == opts.base_url) ? std::string{} : opts.base_url;
    const std::string     url        = resolve_replicate_predict_url(base_effective);
    const std::string data_url = std::string("data:") + (mime.empty() ? "image/png" : mime)
                               + ";base64," + base64_encode(image_bytes.data(), image_bytes.size());
    json              input  = {{"prompt", full_prompt}};
    if (replicate_meta_model_uses_images_key(model)) {
        input["images"] = json::array({data_url});
    } else {
        input["image_input"] = json::array({data_url});
    }
    json req = {{"version", model}, {"input", input}};
    {
        const std::string p = input_path.empty() ? "meta" : input_path;
        const std::string req_dump = replicate_log::dump_json_for_log(req, 0);
        logger::info(std::string("Replicate[meta] context=") + p);
        logger::info(std::string("Replicate[meta] base_url_in=") + (opts.base_url.empty() ? std::string{"<empty>"} : opts.base_url)
                      + (base_ignored.empty() ? "" : (std::string{" effective_ignored="} + base_ignored + " (non-Replicate host)"))
                      + " resolved_POST_url=" + url);
        logger::info(std::string("Replicate[meta] model_in=") + (model_in.empty() ? std::string{"<empty>"} : model_in) + " version_effective=" + model);
        logger::info(std::string("Replicate[meta] POST ") + url);
        logger::info("Replicate[meta] request_headers: Content-Type=application/json, Prefer=wait=60, "
                      "Authorization=Bearer<redacted>");
        logger::info(std::string("Replicate[meta] request_json=") + req_dump);
    }

    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) { err = "curl_easy_init failed"; return false; }
    std::string response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, "Prefer: wait=60");
    const std::string auth = "Authorization: Bearer " + opts.api_key;
    hdrs = curl_slist_append(hdrs, auth.c_str());
    const std::string body = req.dump();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    const CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    if (cc != CURLE_OK) {
        logger::info(std::string("Replicate[meta] initial_request curl_error: ") + std::string(curl_easy_strerror(cc))
                     + " url=" + url);
        err = std::string("Replicate HTTP error: ") + curl_easy_strerror(cc) + "\nReplicate[meta] debug: POST " + url
            + " version=" + model;
        return false;
    }
    {
        const std::string raw = response.size() > 10000
                                    ? (response.substr(0, 10000) + "...(truncated)")
                                    : response;
        try {
            json rj = json::parse(response);
            logger::info(std::string("Replicate[meta] initial_response HTTP ") + std::to_string(http_code) + " json="
                          + replicate_log::dump_json_for_log(std::move(rj), 12000));
        } catch (const std::exception& e) {
            logger::info(std::string("Replicate[meta] initial_response HTTP ") + std::to_string(http_code)
                         + " (parse failed) raw=" + raw + " err=" + e.what());
        }
    }
    if (http_code < 200 || http_code >= 300) {
        err = "Replicate HTTP " + std::to_string(http_code);
        if (response.empty()) {
            err += " (empty response body)";
        } else if (response.size() < 4000) {
            err += ": " + response;
        } else {
            err += ": " + response.substr(0, 4000) + "...(see log)";
        }
        {
            err += std::string("\nReplicate[meta] debug: POST ") + url + " version=" + model;
            if (!base_ignored.empty()) {
                err += " (options.base_url was a non-Replicate host; it was ignored - check Meta settings 'API base URL' if 404 continues)";
            }
        }
        {
            std::string body_log = response;
            try {
                body_log = replicate_log::dump_json_for_log(json::parse(response), 0);
            } catch (...) {
                if (body_log.size() > 24000) body_log = body_log.substr(0, 24000) + "...(truncated)";
            }
            logger::info(std::string("Replicate[meta] create_prediction_failed http=") + std::to_string(http_code) + " url=" + url + " model=" + model
                         + " body=" + body_log);
        }
        return false;
    }

    json resp;
    try { resp = json::parse(response); }
    catch (const std::exception& e) { err = std::string("Replicate JSON parse error: ") + e.what(); return false; }

    std::string status = resp.value("status", "");
    std::string poll_url;
    try {
        if (resp.contains("urls") && resp["urls"].is_object() &&
            resp["urls"].contains("get") && resp["urls"]["get"].is_string()) {
            poll_url = resp["urls"]["get"].get<std::string>();
        }
    } catch (...) {}

    if (!poll_url.empty()) {
        logger::info(std::string("Replicate[meta] poll_url GET ") + poll_url);
    }
    if ((status == "starting" || status == "processing") && !poll_url.empty()) {
        for (int i = 0; i < 30 && (status == "starting" || status == "processing"); ++i) {
            if (progress) progress("Replicate prediction " + status + "... polling");
            CURL* poll = curl_easy_init();
            if (!poll) break;
            std::string poll_resp;
            struct curl_slist* poll_hdrs = nullptr;
            poll_hdrs = curl_slist_append(poll_hdrs, auth.c_str());
            curl_easy_setopt(poll, CURLOPT_URL, poll_url.c_str());
            curl_easy_setopt(poll, CURLOPT_HTTPHEADER, poll_hdrs);
            curl_easy_setopt(poll, CURLOPT_WRITEFUNCTION, curl_write_string);
            curl_easy_setopt(poll, CURLOPT_WRITEDATA, &poll_resp);
            curl_easy_setopt(poll, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(poll, CURLOPT_CONNECTTIMEOUT, 10L);
            CURLcode pc = curl_easy_perform(poll);
            curl_slist_free_all(poll_hdrs);
            curl_easy_cleanup(poll);
            if (pc != CURLE_OK) {
                logger::info(std::string("Replicate[meta] poll n=") + std::to_string(i) + " GET failed: "
                             + std::string(curl_easy_strerror(pc)));
                break;
            }
            try {
                resp = json::parse(poll_resp);
                status = resp.value("status", status);
            } catch (const std::exception& e) {
                logger::info(std::string("Replicate[meta] poll n=") + std::to_string(i) + " json_parse_error: "
                              + e.what());
                break;
            } catch (...) {
                break;
            }
            {
                const std::string poll_dump = replicate_log::dump_json_for_log(json(resp), 8000);
                logger::info(std::string("Replicate[meta] poll n=") + std::to_string(i) + " status=" + status + " body="
                             + poll_dump);
            }
            if (status == "succeeded" || status == "failed" || status == "canceled") break;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    if (status != "succeeded") {
        const std::string rep_err = resp.value("error", std::string{});
        err = "Replicate prediction status=" + (status.empty() ? "unknown" : status)
            + (rep_err.empty() ? "" : (": " + rep_err));
        return false;
    }

    std::string model_text;
    if (resp.contains("output")) model_text = extract_text_from_replicate_output(resp["output"]);
    if (model_text.empty() && resp.contains("logs") && resp["logs"].is_string()) {
        model_text = resp["logs"].get<std::string>();
    }
    if (model_text.empty()) {
        err = "Replicate response did not include textual output";
        return false;
    }
    if (!parse_or_wrap_meta_payload(model_text, input_path, payload_out)) {
        err = "Failed to parse Replicate payload";
        return false;
    }
    logger::info("Replicate[meta]: file=" + input_path + " model=" + model);
    return true;
}

// ── EXIF read via libvips header fields ─────────────────────────────────────
// libvips exposes EXIF as fields like "exif-ifd0-Make", "exif-GPS-GPSLatitude",
// each value rendered as "value (unit, type)" string.  We strip the trailing
// parenthetical and group by the IFD/GPS bucket so JSON consumers get a clean
// map.
json exif_from_vips_image(VipsImage* img) {
    json out = json::object();
    if (!img) return out;
    out["width"]  = vips_image_get_width(img);
    out["height"] = vips_image_get_height(img);
    out["bands"]  = vips_image_get_bands(img);

    gchar** fields = vips_image_get_fields(img);
    if (fields) {
        for (gchar** p = fields; *p; ++p) {
            const std::string key(*p);
            if (key.rfind("exif-", 0) != 0) continue;
            char* val = nullptr;
            if (vips_image_get_as_string(img, key.c_str(), &val) != 0 || !val) continue;
            std::string v(val);
            g_free(val);
            auto paren = v.rfind(" (");
            if (paren != std::string::npos && v.back() == ')')
                v.erase(paren);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\n' || v.back() == '\r'))
                v.pop_back();
            out[key] = v;
        }
        g_strfreev(fields);
    }
    return out;
}

json read_exif_from_path(const std::string& path) {
    VipsImage* img = vips_image_new_from_file(path.c_str(),
                                              "access", VIPS_ACCESS_SEQUENTIAL, NULL);
    if (!img) { vips_error_clear(); return json::object(); }
    json out = exif_from_vips_image(img);
    g_object_unref(img);
    return out;
}

json read_exif_from_buffer(const void* data, std::size_t size) {
    if (!data || size == 0) return json::object();
    VipsImage* img = vips_image_new_from_buffer(data, size, "", NULL);
    if (!img) { vips_error_clear(); return json::object(); }
    json out = exif_from_vips_image(img);
    g_object_unref(img);
    return out;
}

// ── In-memory resize → JPEG buffer (used as Gemini input) ───────────────────
bool resize_to_jpeg_buffer(const std::string& path,
                           int target_width,
                           std::vector<uint8_t>& out_bytes,
                           int& orig_w, int& orig_h,
                           int& out_w, int& out_h,
                           std::string& err)
{
    return path_to_jpeg_for_llm(path, target_width, out_bytes,
                                orig_w, orig_h, out_w, out_h, err);
}

// Same idea, but the source is already in-memory bytes — no fs hit.
bool resize_to_jpeg_buffer_from_buffer(const void* in_data, std::size_t in_size,
                                        int target_width,
                                        std::vector<uint8_t>& out_bytes,
                                        int& orig_w, int& orig_h,
                                        int& out_w, int& out_h,
                                        std::string& err)
{
    VipsImage* in = vips_image_new_from_buffer(in_data, in_size, "", NULL);
    if (!in) { err = vips_err(); return false; }
    orig_w = vips_image_get_width(in);
    orig_h = vips_image_get_height(in);

    VipsImage* thumb = nullptr;
    if (vips_thumbnail_buffer(const_cast<void*>(in_data), in_size, &thumb, target_width, NULL)) {
        g_object_unref(in);
        err = vips_err();
        return false;
    }
    out_w = vips_image_get_width(thumb);
    out_h = vips_image_get_height(thumb);

    void* buf = nullptr;
    size_t len = 0;
    if (vips_jpegsave_buffer(thumb, &buf, &len, "Q", 85, "strip", TRUE, NULL)) {
        g_object_unref(in); g_object_unref(thumb);
        err = vips_err();
        return false;
    }
    out_bytes.assign(static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + len);
    g_free(buf);
    g_object_unref(in);
    g_object_unref(thumb);
    return true;
}

bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) { err = "cannot open: " + path; return false; }
    out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

// ── Markdown rendering ─────────────────────────────────────────────────────
std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, b - a + 1);
}

// ── Defensive JSON accessors ────────────────────────────────────────────────
// Models occasionally return values with the wrong type (e.g. alt as an array,
// confidence as a string, tags as a single string, etc.).  Going through these
// helpers means we never call `.get<T>()` on a value of the wrong type and so
// never throw out of render_markdown / serialisation.
std::string js_str(const json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return {};
    const auto& v = j[key];
    if (v.is_string())   return v.get<std::string>();
    if (v.is_number())   return v.dump();
    if (v.is_boolean())  return v.get<bool>() ? "true" : "false";
    return v.dump();   // arrays / objects / etc. → readable inline JSON
}
double js_num(const json& j, const char* key, double def) {
    if (!j.contains(key)) return def;
    const auto& v = j[key];
    if (v.is_number())   return v.get<double>();
    if (v.is_string()) {
        try { return std::stod(v.get<std::string>()); } catch (...) {}
    }
    return def;
}

std::string render_markdown(const std::string& input_path,
                            const json& payload,
                            const json& exif)
{
    std::ostringstream md;
    std::string title = js_str(payload, "title");
    if (title.empty()) title = fs::path(input_path).filename().string();
    md << "# " << title << "\n\n";

    md << "*Source*: `" << input_path << "`\n\n";

    if (auto alt = js_str(payload, "alt"); !alt.empty())
        md << "**Alt text**: " << alt << "\n\n";

    if (auto desc = js_str(payload, "description"); !desc.empty())
        md << "## Description\n\n" << trim(desc) << "\n\n";

    auto list_section = [&](const char* key, const char* heading) {
        if (!payload.contains(key) || !payload[key].is_array() || payload[key].empty()) return;
        md << "## " << heading << "\n\n";
        for (auto& item : payload[key]) {
            if (item.is_string()) {
                md << "- " << item.get<std::string>() << "\n";
            } else if (item.is_object()) {
                std::string type  = js_str(item, "type");
                std::string notes = js_str(item, "notes");
                int count = 0;
                if (item.contains("count") && item["count"].is_number())
                    count = item["count"].get<int>();
                md << "- " << type;
                if (count > 0) md << " (\xC3\x97" << count << ")";
                if (!notes.empty()) md << " \xE2\x80\x94 " << notes;
                md << "\n";
            } else if (!item.is_null()) {
                md << "- " << item.dump() << "\n";
            }
        }
        md << "\n";
    };
    list_section("characters", "People & characters");
    list_section("objects",    "Objects");
    list_section("tags",       "Tags");

    if (auto scene = js_str(payload, "scene"); !scene.empty())
        md << "**Scene**: " << scene << "\n\n";
    if (auto loc = js_str(payload, "estimated_location"); !loc.empty())
        md << "**Estimated location**: " << loc << "\n\n";
    if (payload.contains("confidence") && !payload["confidence"].is_null())
        md << "*Confidence*: " << js_num(payload, "confidence", 0.0) << "\n\n";

    if (!exif.empty()) {
        md << "## EXIF\n\n";
        const char* hi[] = {"exif-ifd0-Make", "exif-ifd0-Model",
                            "exif-ifd2-DateTimeOriginal", "exif-ifd2-FNumber",
                            "exif-ifd2-ExposureTime", "exif-ifd2-ISOSpeedRatings",
                            "exif-ifd2-FocalLength",
                            "exif-GPS-GPSLatitude", "exif-GPS-GPSLongitude"};
        for (auto* k : hi) {
            const std::string val = js_str(exif, k);
            if (!val.empty()) md << "- **" << k << "**: " << val << "\n";
        }
        md << "\n";
    }
    return md.str();
}

// ── EXIF write (in-place rewrite via vips) ──────────────────────────────────
bool write_exif_description(const std::string& path, const std::string& desc, std::string& err) {
    VipsImage* img = vips_image_new_from_file(path.c_str(), NULL);
    if (!img) { err = vips_err(); return false; }
    vips_image_set_string(img, "exif-ifd0-ImageDescription", desc.c_str());
    // Atomic-ish: write to <path>.tmp.<ext>, then replace.
    fs::path src(path);
    fs::path tmp = src.parent_path() / (src.stem().string() + ".meta-tmp" + src.extension().string());
    if (vips_image_write_to_file(img, tmp.string().c_str(), NULL)) {
        g_object_unref(img);
        err = vips_err();
        return false;
    }
    g_object_unref(img);
    std::error_code ec;
    fs::remove(src, ec);
    fs::rename(tmp, src, ec);
    if (ec) { err = "rename failed: " + ec.message(); return false; }
    return true;
}

// ── Gemini call ─────────────────────────────────────────────────────────────
bool call_gemini_meta(const MetaOptions& opts,
                      const std::vector<uint8_t>& image_bytes,
                      const std::string& mime,
                      const std::string& full_prompt,
                      MetaProgressFn progress,
                      json& payload_out,
                      std::string& err,
                      const std::string& log_file = {})
{
    std::string b64 = base64_encode(image_bytes.data(), image_bytes.size());

    json req = {
        {"contents", json::array({
            {{"parts", json::array({
                {{"text", full_prompt}},
                {{"inlineData", {{"mimeType", mime}, {"data", b64}}}}
            })}}
        })},
        {"generationConfig", {
            {"responseMimeType", "application/json"}
        }}
    };

    const std::string url = resolve_gemini_generate_url(opts.base_url, opts.model);
    std::string body = req.dump();

    if (progress) progress("POST " + opts.model + " (" + std::to_string(image_bytes.size() / 1024) + " KB image)");

    ensure_curl_global();
    CURL* curl = curl_easy_init();
    if (!curl) { err = "curl_easy_init failed"; return false; }

    std::string response;
    struct curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    std::string auth = "x-goog-api-key: " + opts.api_key;
    hdrs = curl_slist_append(hdrs, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    const auto t0 = std::chrono::steady_clock::now();
    CURLcode cc = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    {
        std::string line = std::string("Gemini[meta]: POST ") + url + " model=" + opts.model + " HTTP "
                           + std::to_string(http_code) + " in " + std::to_string(ms) + " ms resp_bytes="
                           + std::to_string(response.size());
        if (!log_file.empty()) line += " file=" + log_file;
        logger::info(line);
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (cc != CURLE_OK) {
        err = std::string("HTTP error: ") + curl_easy_strerror(cc);
        return false;
    }
    if (http_code != 200) {
        err = "API HTTP " + std::to_string(http_code);
        try {
            auto j = json::parse(response);
            if (j.contains("error") && j["error"].contains("message"))
                err += ": " + j["error"]["message"].get<std::string>();
        } catch (...) {
            if (response.size() < 800) err += ": " + response;
        }
        return false;
    }

    if (progress) progress("Parsing response");

    json resp;
    try { resp = json::parse(response); }
    catch (const std::exception& e) {
        err = std::string("response JSON parse error: ") + e.what();
        return false;
    }

    // Extract text from candidates[0].content.parts[*].text
    std::string text;
    try {
        auto& parts = resp["candidates"][0]["content"]["parts"];
        for (auto& p : parts) {
            if (p.contains("text")) text += p["text"].get<std::string>();
        }
    } catch (...) {
        err = "API response missing candidates[0].content.parts";
        return false;
    }
    if (text.empty()) { err = "empty model response"; return false; }

    try { payload_out = json::parse(text); }
    catch (const std::exception& e) {
        err = std::string("model returned non-JSON: ") + e.what() + "\n--- raw ---\n" + text;
        return false;
    }
    return true;
}

} // namespace

// ── public API ──────────────────────────────────────────────────────────────

std::string default_meta_prompt() {
    return
        "You are an expert image cataloguer. You will be given one image (the only subject). "
        "Describe ONLY what is actually visible in THAT image. Do not substitute a different scene, "
        "stock photo, or generic example. If you are unsure, say so in the description and lower confidence.\n"
        "Respond with a single JSON object (no explanations, no markdown code fences, no ```) using exactly these keys:\n"
        "  title              — short title (<=80 chars)\n"
        "  alt                — SEO alt text (<=125 chars, plain factual)\n"
        "  description        — 2–4 sentence neutral description of the actual pixels\n"
        "  scene              — one short phrase (e.g. \"indoor room, white backdrop\")\n"
        "  estimated_location — best guess or \"unknown\"\n"
        "  characters         — array of {type, count, notes} for people / animals / characters (empty array if none)\n"
        "  objects            — array of {type, count} for the main visible objects\n"
        "  tags               — array of 5–12 lowercase keywords\n"
        "  confidence         — number from 0 to 1 only (your certainty about the description matching this image)\n"
        "Be concise. Never include any text before or after the JSON object.";
}

MetaResult meta_extract_impl(const std::string& input_path,
                             const MetaOptions& opts_in,
                             MetaProgressFn progress);

MetaResult meta_extract(const std::string& input_path,
                        const MetaOptions& opts_in,
                        MetaProgressFn progress)
{
    // Wrap the whole pipeline so a single bad model response (or an out-of-shape
    // EXIF / JSON value) can never escape as an exception and abort the worker
    // thread that called us.
    try {
        return meta_extract_impl(input_path, opts_in, progress);
    } catch (const std::exception& e) {
        MetaResult res;
        res.error = std::string("meta exception: ") + e.what();
        return res;
    } catch (...) {
        MetaResult res;
        res.error = "meta exception: unknown";
        return res;
    }
}

MetaResult meta_extract_impl(const std::string& input_path,
                             const MetaOptions& opts_in,
                             MetaProgressFn progress)
{
    MetaResult res;
    MetaOptions opts = opts_in;
    media::runtime_settings::merge_provider_credentials(
        opts.provider, opts.dry_run, opts.api_key, opts.base_url);
    if (opts.prompt.empty()) {
        opts.prompt = default_meta_prompt();
        // Augment with the user's contextual question (if any), so Gemini's
        // `description` field directly answers what the chat user asked.
        if (!opts.user_query.empty()) {
            opts.prompt += "\n\nThe user is specifically asking: \""
                         + opts.user_query
                         + "\". In your `description` field, lead with a clear answer "
                           "to that question (one or two sentences), then give the "
                           "rest of the structured catalog as usual.";
        }
    }

    if (!fs::exists(input_path)) {
        res.error = "input not found: " + input_path;
        return res;
    }
    // Note: callers may legitimately request "no disk outputs" (REST / IPC use
    // the in-memory json_text directly).  CLI guards this case in main.cpp.

    ensure_vips();

    // ── 1. Optional in-memory resize ────────────────────────────────────────
    std::vector<uint8_t> image_bytes;
    std::string mime;
    if (opts.resize_first && opts.resize_width > 0) {
        if (progress) progress("Resizing in memory to " + std::to_string(opts.resize_width) + "px");
        std::string err;
        if (!resize_to_jpeg_buffer(input_path, opts.resize_width, image_bytes,
                                   res.orig_w, res.orig_h, res.resized_w, res.resized_h, err)) {
            res.error = "resize failed: " + err;
            return res;
        }
        mime = "image/jpeg";
    } else {
        std::string err;
        if (!read_file_bytes(input_path, image_bytes, err)) { res.error = err; return res; }
        mime = mime_from_ext(fs::path(input_path).extension().string());
        // try to read original dimensions
        VipsImage* img = vips_image_new_from_file(input_path.c_str(),
                                                  "access", VIPS_ACCESS_SEQUENTIAL, NULL);
        if (img) {
            res.orig_w = vips_image_get_width(img);
            res.orig_h = vips_image_get_height(img);
            g_object_unref(img);
        } else {
            vips_error_clear();
        }
    }
    res.bytes_sent = image_bytes.size();

    // ── 2. EXIF basics ──────────────────────────────────────────────────────
    if (progress) progress("Reading EXIF");
    json exif = read_exif_from_path(input_path);

    // ── 3. Dry-run early exit ───────────────────────────────────────────────
    if (opts.dry_run) {
        json preview = {
            {"input",        input_path},
            {"provider",     opts.provider},
            {"model",        opts.model},
            {"resize_first", opts.resize_first},
            {"resize_width", opts.resize_width},
            {"orig_w",       res.orig_w},
            {"orig_h",       res.orig_h},
            {"resized_w",    res.resized_w},
            {"resized_h",    res.resized_h},
            {"bytes_to_send",res.bytes_sent},
            {"mime",         mime},
            {"out_md",       opts.out_md},
            {"out_json",     opts.out_json},
            {"update_exif",  opts.update_exif},
            {"out_dir",      opts.out_dir},
            {"prompt",       opts.prompt},
            {"exif_keys",    static_cast<int>(exif.size())}
        };
        res.json_text = preview.dump(2);
        res.markdown  = "# (dry-run)\n\n```json\n" + preview.dump(2) + "\n```\n";
        res.ok = true;
        if (progress) progress("Dry-run OK - no API call, no files written");
        return res;
    }

    // ── 4. Provider check ───────────────────────────────────────────────────
    if (opts.provider != "google" && opts.provider != "replicate" && opts.provider != "pixlwiz") {
        res.error = "unsupported provider: " + opts.provider;
        return res;
    }
    if (opts.api_key.empty()) {
        if (opts.provider == "replicate")
            res.error = "API key required (configure Replicate in app provider settings, or pass --api-key)";
        else if (opts.provider == "pixlwiz")
            res.error = "API key required (configure Pixlwiz in app provider settings, or pass --api-key)";
        else
            res.error = "API key required (configure Google/Gemini in app provider settings, or pass --api-key)";
        return res;
    }

    // ── 5. Call model ───────────────────────────────────────────────────────
    json payload;
    {
        std::string err;
        bool ok = false;
        if (opts.provider == "replicate")
            ok = call_replicate_meta(opts, image_bytes, mime, opts.prompt, progress, payload, err, input_path);
        else if (opts.provider == "pixlwiz")
            ok = call_pixlwiz_meta(opts, image_bytes, mime, opts.prompt, progress, payload, err, input_path);
        else
            ok = call_gemini_meta(opts, image_bytes, mime, opts.prompt, progress, payload, err, input_path);
        if (!ok) {
            res.error = err;
            return res;
        }
    }

    // ── 6. Build outputs ────────────────────────────────────────────────────
    json full = payload;
    full["source"]    = input_path;
    full["model"]     = opts.model;
    full["provider"]  = opts.provider;
    if (!exif.empty()) full["exif"] = exif;

    res.json_text = full.dump(2);
    res.markdown  = render_markdown(input_path, payload, exif);

    fs::path src(input_path);
    fs::path dir = opts.out_dir.empty() ? src.parent_path() : fs::path(opts.out_dir);
    if (!opts.out_dir.empty()) {
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    std::string stem = src.stem().string();

    if (opts.out_md) {
        fs::path p = dir / (stem + ".md");
        std::ofstream ofs(p, std::ios::binary);
        if (!ofs) { res.error = "cannot write " + p.string(); return res; }
        ofs.write(res.markdown.data(), static_cast<std::streamsize>(res.markdown.size()));
        res.md_path = p.string();
        if (progress) progress("Wrote " + res.md_path);
    }
    if (opts.out_json) {
        fs::path p = dir / (stem + ".json");
        std::ofstream ofs(p, std::ios::binary);
        if (!ofs) { res.error = "cannot write " + p.string(); return res; }
        ofs.write(res.json_text.data(), static_cast<std::streamsize>(res.json_text.size()));
        res.json_path = p.string();
        if (progress) progress("Wrote " + res.json_path);
    }
    if (opts.update_exif) {
        std::string desc = payload.value("description", std::string{});
        if (desc.empty()) desc = payload.value("alt", std::string{});
        if (desc.empty()) {
            res.error = "EXIF update requested but model returned no description/alt";
            return res;
        }
        std::string err;
        if (!write_exif_description(input_path, desc, err)) {
            res.error = "EXIF write failed: " + err;
            return res;
        }
        res.exif_updated = true;
        if (progress) progress("EXIF ImageDescription updated");
    }

    res.ok = true;
    return res;
}

// ── Buffer-only variant (zero-fs) ────────────────────────────────────────────

MetaResult meta_extract_buffer_impl(const void* in_data, std::size_t in_size,
                                     const std::string& in_mime,
                                     const MetaOptions& opts_in,
                                     MetaProgressFn progress);

MetaResult meta_extract_buffer(const void* in_data, std::size_t in_size,
                                const std::string& in_mime,
                                const MetaOptions& opts_in,
                                MetaProgressFn progress) {
    try {
        return meta_extract_buffer_impl(in_data, in_size, in_mime, opts_in, progress);
    } catch (const std::exception& e) {
        MetaResult res; res.error = std::string("meta exception: ") + e.what(); return res;
    } catch (...) {
        MetaResult res; res.error = "meta exception: unknown"; return res;
    }
}

MetaResult meta_extract_buffer_impl(const void* in_data, std::size_t in_size,
                                     const std::string& in_mime,
                                     const MetaOptions& opts_in,
                                     MetaProgressFn progress)
{
    MetaResult res;
    if (!in_data || in_size == 0) { res.error = "empty input bytes"; return res; }

    MetaOptions opts = opts_in;
    media::runtime_settings::merge_provider_credentials(
        opts.provider, opts.dry_run, opts.api_key, opts.base_url);
    if (opts.prompt.empty()) {
        opts.prompt = default_meta_prompt();
        if (!opts.user_query.empty()) {
            opts.prompt += "\n\nThe user is specifically asking: \""
                         + opts.user_query
                         + "\". In your `description` field, lead with a clear answer "
                           "to that question (one or two sentences), then give the "
                           "rest of the structured catalog as usual.";
        }
    }

    // REST contract: never write to disk regardless of caller toggles.
    opts.out_md = false; opts.out_json = false; opts.update_exif = false;

    ensure_vips();

    // Optional in-memory resize (LLM doesn't need the full sensor data).
    std::vector<uint8_t> image_bytes;
    std::string mime;
    if (opts.resize_first && opts.resize_width > 0) {
        if (progress) progress("Resizing in memory to " + std::to_string(opts.resize_width) + "px");
        std::string err;
        if (!resize_to_jpeg_buffer_from_buffer(in_data, in_size, opts.resize_width,
                                                image_bytes,
                                                res.orig_w, res.orig_h,
                                                res.resized_w, res.resized_h, err)) {
            res.error = "resize failed: " + err;
            return res;
        }
        mime = "image/jpeg";
    } else {
        image_bytes.assign(static_cast<const uint8_t*>(in_data),
                           static_cast<const uint8_t*>(in_data) + in_size);
        mime = in_mime.empty() ? "image/jpeg" : in_mime;
        // Try original dimensions for diagnostics.
        VipsImage* img = vips_image_new_from_buffer(in_data, in_size, "", NULL);
        if (img) {
            res.orig_w = vips_image_get_width(img);
            res.orig_h = vips_image_get_height(img);
            g_object_unref(img);
        } else { vips_error_clear(); }
    }
    res.bytes_sent = image_bytes.size();

    if (progress) progress("Reading EXIF");
    json exif = read_exif_from_buffer(in_data, in_size);

    if (opts.dry_run) {
        json preview = {
            {"input",        "<buffer>"},
            {"provider",     opts.provider},
            {"model",        opts.model},
            {"resize_first", opts.resize_first},
            {"resize_width", opts.resize_width},
            {"orig_w",       res.orig_w},
            {"orig_h",       res.orig_h},
            {"resized_w",    res.resized_w},
            {"resized_h",    res.resized_h},
            {"bytes_to_send",res.bytes_sent},
            {"mime",         mime},
            {"prompt",       opts.prompt},
            {"exif_keys",    static_cast<int>(exif.size())}
        };
        res.json_text = preview.dump(2);
        res.markdown  = "# (dry-run)\n\n```json\n" + preview.dump(2) + "\n```\n";
        res.ok = true;
        return res;
    }

    if (opts.provider != "google" && opts.provider != "replicate" && opts.provider != "pixlwiz") {
        res.error = "unsupported provider";
        return res;
    }
    if (opts.api_key.empty()) {
        res.error = "API key required";
        return res;
    }

    json payload;
    {
        std::string err;
        bool ok = false;
        if (opts.provider == "replicate")
            ok = call_replicate_meta(opts, image_bytes, mime, opts.prompt, progress, payload, err, "<buffer>");
        else if (opts.provider == "pixlwiz")
            ok = call_pixlwiz_meta(opts, image_bytes, mime, opts.prompt, progress, payload, err, "<buffer>");
        else
            ok = call_gemini_meta(opts, image_bytes, mime, opts.prompt, progress, payload, err, "<buffer>");
        if (!ok) {
            res.error = err;
            return res;
        }
    }

    json full = payload;
    full["source"]    = "<buffer>";
    full["model"]     = opts.model;
    full["provider"]  = opts.provider;
    if (!exif.empty()) full["exif"] = exif;

    res.json_text = full.dump(2);
    res.markdown  = render_markdown("<buffer>", payload, exif);
    res.ok = true;
    return res;
}

// ── JSON option mapping ──────────────────────────────────────────────────────

void apply_meta_options_from_json(const nlohmann::json& j, MetaOptions& opts) {
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

    str    ("provider",     opts.provider);
    str    ("model",        opts.model);
    str    ("api_key",      opts.api_key);
    str    ("base_url",     opts.base_url);
    str    ("prompt",       opts.prompt);
    str    ("user_query",   opts.user_query);   // optional contextual question
    boolean("resize_first", opts.resize_first);
    num    ("resize_width", opts.resize_width);
    boolean("out_md",       opts.out_md);
    boolean("out_json",     opts.out_json);
    boolean("update_exif",  opts.update_exif);
    str    ("out_dir",      opts.out_dir);
    boolean("dry_run",      opts.dry_run);

    if (!j.contains("provider") || !j["provider"].is_string())
        opts.provider.clear();
    if (!j.contains("model") || !j["model"].is_string())
        opts.model.clear();
}

} // namespace media
