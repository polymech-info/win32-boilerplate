#pragma once

#if defined(FEATURE_STT) && FEATURE_STT

#include <string>

namespace pm::stt {

// POST {base_url}/v1/audio/transcriptions  (OpenAI-compatible multipart; LiteLLM proxy).
// wav_path — path to a 16 kHz mono 16-bit PCM WAV file.
// base_url  — provider base URL, e.g. "https://llm.polymech.info"; /v1 appended when absent.
// Returns the transcript text. Throws std::runtime_error on failure.
std::string proxy_stt_transcribe(
    const std::string& wav_path,
    const std::string& base_url,
    const std::string& api_key,
    const std::string& model);

// Write a vector of 16-bit mono PCM samples (16 kHz) to a WAV file.
// Returns false if the file could not be written.
bool write_pcm_wav(const std::string& path,
                   const short*       samples,
                   std::size_t        n_samples,
                   int                sample_rate = 16000);

} // namespace pm::stt

#endif // FEATURE_STT
