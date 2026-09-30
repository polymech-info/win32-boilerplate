#if defined(FEATURE_STT) && FEATURE_STT

#include "elevenlabs_tts.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pm::tts {

// ── helpers ───────────────────────────────────────────────────────────────────

std::string tts_format_for_ext(const std::string& ext) {
    if (ext == ".wav")  return "pcm_44100";
    if (ext == ".mp3")  return "mp3_44100_128";
    if (ext == ".opus") return "opus_48000_32";
    return "mp3_44100_128";
}

bool tts_format_is_pcm(const std::string& fmt) {
    return fmt.size() >= 4 && fmt.substr(0, 4) == "pcm_";
}

int tts_pcm_sample_rate(const std::string& fmt) {
    // "pcm_44100" → 44100
    const auto pos = fmt.find('_');
    if (pos == std::string::npos) return 44100;
    try { return std::stoi(fmt.substr(pos + 1)); } catch (...) { return 44100; }
}

// ── libcurl write callback ─────────────────────────────────────────────────

static size_t curl_write_bytes(void* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t bytes = size * nmemb;
    auto* buf = static_cast<std::vector<uint8_t>*>(userdata);
    const auto* p = static_cast<const uint8_t*>(ptr);
    buf->insert(buf->end(), p, p + bytes);
    return bytes;
}

// ── synthesize ────────────────────────────────────────────────────────────────

std::vector<uint8_t> elevenlabs_tts_synthesize(const std::string& text,
                                                const ElevenLabsTTSConfig& cfg)
{
    CURL* c = curl_easy_init();
    if (!c) throw std::runtime_error("elevenlabs_tts: curl_easy_init failed");

    // Build URL: /v1/text-to-speech/{voice_id}?output_format={fmt}
    const std::string url =
        "https://api.elevenlabs.io/v1/text-to-speech/" + cfg.voice_id
        + "?output_format=" + cfg.output_format;

    const std::string auth_hdr  = "xi-api-key: " + cfg.api_key;
    const std::string ct_hdr    = "Content-Type: application/json";

    curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_hdr.c_str());
    hdrs = curl_slist_append(hdrs, ct_hdr.c_str());

    nlohmann::json body;
    body["text"]     = text;
    body["model_id"] = cfg.model_id;
    const std::string body_str = body.dump();

    std::vector<uint8_t> response;
    // reserve ~1 MB to avoid repeated re-allocs for typical TTS audio
    response.reserve(1024 * 1024);

    curl_easy_setopt(c, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS,     body_str.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE,  static_cast<long>(body_str.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  curl_write_bytes);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL,       1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        60L);

    const CURLcode rc = curl_easy_perform(c);
    curl_slist_free_all(hdrs);

    if (rc != CURLE_OK) {
        curl_easy_cleanup(c);
        throw std::runtime_error(
            std::string("elevenlabs_tts: curl error: ") + curl_easy_strerror(rc));
    }

    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);

    if (http_code != 200) {
        // Response body will be JSON error from the API.
        std::string msg(response.begin(), response.end());
        throw std::runtime_error(
            "elevenlabs_tts: HTTP " + std::to_string(http_code) + ": " + msg);
    }

    return response;
}

} // namespace pm::tts

#endif // FEATURE_STT
