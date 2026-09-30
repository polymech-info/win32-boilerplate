#pragma once

#if defined(FEATURE_STT) && FEATURE_STT

#include <cstdint>
#include <string>
#include <vector>

namespace pm::tts {

// POST {base_url}/v1/audio/speech  (OpenAI-compatible; LiteLLM proxy).
// base_url  — provider base URL, e.g. "https://llm.polymech.info";
//             /v1 is appended automatically when absent.
// voice_id  — ElevenLabs voice UUID; falls back to Sarah (EXAVITQu4vr4xnSDxMaL) when empty.
// fmt       — OpenAI format name: "mp3" | "opus" | "aac" | "flac" (default: "mp3").
// Returns raw audio bytes on success; throws std::runtime_error on failure.
std::vector<uint8_t> proxy_tts_synthesize(
    const std::string& text,
    const std::string& base_url,
    const std::string& api_key,
    const std::string& model,
    const std::string& voice_id = {},
    const std::string& fmt      = "mp3");

} // namespace pm::tts

#endif // FEATURE_STT
