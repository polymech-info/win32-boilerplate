#include "proxy_stt.hpp"

#if defined(FEATURE_STT) && FEATURE_STT

#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pm::stt {

namespace {

static size_t curl_write_vec(void* ptr, size_t sz, size_t n, void* ud) {
    auto* v = static_cast<std::vector<uint8_t>*>(ud);
    const auto* p = static_cast<const uint8_t*>(ptr);
    v->insert(v->end(), p, p + sz * n);
    return sz * n;
}

#pragma pack(push, 1)
struct WavHdr {
    char     riff[4]         = {'R','I','F','F'};
    uint32_t chunk_size      = 0;
    char     wave[4]         = {'W','A','V','E'};
    char     fmt_id[4]       = {'f','m','t',' '};
    uint32_t fmt_size        = 16;
    uint16_t audio_format    = 1;   // PCM
    uint16_t channels        = 1;
    uint32_t sample_rate     = 16000;
    uint32_t byte_rate       = 32000; // sample_rate * channels * (bits/8)
    uint16_t block_align     = 2;
    uint16_t bits_per_sample = 16;
    char     data_id[4]      = {'d','a','t','a'};
    uint32_t data_size       = 0;
};
#pragma pack(pop)
static_assert(sizeof(WavHdr) == 44, "WavHdr must be 44 bytes");

} // namespace

bool write_pcm_wav(const std::string& path,
                   const short*       samples,
                   std::size_t        n_samples,
                   int                sample_rate)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const uint32_t data_bytes = static_cast<uint32_t>(n_samples * sizeof(short));
    WavHdr hdr;
    hdr.sample_rate  = static_cast<uint32_t>(sample_rate);
    hdr.byte_rate    = static_cast<uint32_t>(sample_rate) * 2u; // mono 16-bit
    hdr.data_size    = data_bytes;
    hdr.chunk_size   = data_bytes + 36;
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(samples),
            static_cast<std::streamsize>(data_bytes));
    return f.good();
}

std::string proxy_stt_transcribe(
    const std::string& wav_path,
    const std::string& base_url,
    const std::string& api_key,
    const std::string& model)
{
    std::string eff_base = base_url.empty() ? "https://llm.polymech.info" : base_url;
    while (!eff_base.empty() && eff_base.back() == '/') eff_base.pop_back();
    if (eff_base.size() < 3 || eff_base.compare(eff_base.size() - 3, 3, "/v1") != 0)
        eff_base += "/v1";
    const std::string url = eff_base + "/audio/transcriptions";

    CURL* c = curl_easy_init();
    if (!c) throw std::runtime_error("proxy_stt: curl_easy_init failed");

    const std::string auth_hdr = "Authorization: Bearer " + api_key;
    curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, auth_hdr.c_str());

    curl_mime* mime  = curl_mime_init(c);
    curl_mimepart* p = curl_mime_addpart(mime);
    curl_mime_name(p, "file");
    curl_mime_filedata(p, wav_path.c_str());
    curl_mime_filename(p, "audio.wav");
    curl_mime_type(p, "audio/wav");
    p = curl_mime_addpart(mime);
    curl_mime_name(p, "model");
    curl_mime_data(p, model.c_str(), CURL_ZERO_TERMINATED);

    std::vector<uint8_t> response;
    response.reserve(8 * 1024);

    curl_easy_setopt(c, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(c, CURLOPT_MIMEPOST,       mime);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  curl_write_vec);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &response);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL,       1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        120L);

    const CURLcode rc = curl_easy_perform(c);
    curl_mime_free(mime);
    curl_slist_free_all(hdrs);
    if (rc != CURLE_OK) {
        curl_easy_cleanup(c);
        throw std::runtime_error(std::string("proxy_stt: curl error: ") + curl_easy_strerror(rc));
    }
    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(c);
    const std::string body(response.begin(), response.end());
    if (http_code != 200)
        throw std::runtime_error("proxy_stt: HTTP " + std::to_string(http_code) + ": " + body);

    try {
        auto j = nlohmann::json::parse(body);
        return j.value("text", "");
    } catch (...) {
        throw std::runtime_error("proxy_stt: failed to parse response: " + body);
    }
}

} // namespace pm::stt

#endif // FEATURE_STT
