#include "proxy_tts.hpp"

#if defined(FEATURE_STT) && FEATURE_STT

#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace pm::tts {

namespace {

static size_t curl_write_vec(void* ptr, size_t sz, size_t n, void* ud) {
    auto* v = static_cast<std::vector<uint8_t>*>(ud);
    const auto* p = static_cast<const uint8_t*>(ptr);
    v->insert(v->end(), p, p + sz * n);
    return sz * n;
}

// Sarah — the proxy's configured default voice UUID.
static constexpr const char* k_default_voice = "EXAVITQu4vr4xnSDxMaL";

} // namespace

std::vector<uint8_t> proxy_tts_synthesize(
    const std::string& text,
    const std::string& base_url,
    const std::string& api_key,
    const std::string& model,
    const std::string& voice_id,
    const std::string& fmt)
{
    // Normalise base_url: strip trailing slashes, append /v1 if absent.
    std::string eff_base = base_url.empty() ? "https://llm.polymech.info" : base_url;
    while (!eff_base.empty() && eff_base.back() == '/') eff_base.pop_back();
    if (eff_base.size() < 3 || eff_base.compare(eff_base.size() - 3, 3, "/v1") != 0)
        eff_base += "/v1";
    const std::string url = eff_base + "/audio/speech";

    CURL* c = curl_easy_init();
    if (!c) throw std::runtime_error("proxy_tts: curl_easy_init failed");

    const std::string auth_hdr = "Authorization: Bearer " + api_key;
    const std::string ct_hdr   = "Content-Type: application/json";
    curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_hdr.c_str());
    hdrs = curl_slist_append(hdrs, ct_hdr.c_str());

    nlohmann::json body;
    body["model"]           = model;
    body["input"]           = text;
    body["response_format"] = fmt.empty() ? "mp3" : fmt;
    // voice is required by the OpenAI /audio/speech schema.
    body["voice"] = voice_id.empty() ? k_default_voice : voice_id;
    const std::string body_str = body.dump();

    std::vector<uint8_t> response;
    response.reserve(512 * 1024);

    curl_easy_setopt(c, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,     body_str.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body_str.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  curl_write_vec);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL,       1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        60L);

    const CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdrs);
    if (rc != CURLE_OK) {
        curl_easy_cleanup(c);
        throw std::runtime_error(std::string("proxy_tts: curl error: ") + curl_easy_strerror(rc));
    }
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    if (http_code != 200) {
        const std::string msg(response.begin(), response.end());
        throw std::runtime_error("proxy_tts: HTTP " + std::to_string(http_code) + ": " + msg);
    }
    return response;
}

} // namespace pm::tts

#endif // FEATURE_STT
