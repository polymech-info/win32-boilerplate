#pragma once

#if defined(FEATURE_STT) && FEATURE_STT

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pm::tts {

struct ElevenLabsTTSConfig {
    std::string api_key;
    std::string voice_id      = "tLK6fPv15M0oKv4V3ACR";
    std::string model_id      = "eleven_v3";
    std::string output_format = "mp3_44100_128";
};

// Synchronous: fetches all audio bytes then returns them.
// Throws std::runtime_error on network or API error.
std::vector<uint8_t> elevenlabs_tts_synthesize(const std::string& text,
                                                const ElevenLabsTTSConfig& cfg);

// Pick a sensible ElevenLabs output_format from a file extension.
//   .wav  → "pcm_44100"    (raw s16le — write WAV header yourself)
//   .mp3  → "mp3_44100_128"
//   .opus → "opus_48000_32"
//   else  → "mp3_44100_128"
std::string tts_format_for_ext(const std::string& ext_lower);

// True when format string starts with "pcm_" (raw PCM, needs WAV wrapper).
bool tts_format_is_pcm(const std::string& fmt);

// Parse sample-rate from pcm format string e.g. "pcm_44100" → 44100.
int tts_pcm_sample_rate(const std::string& fmt);

} // namespace pm::tts

#endif // FEATURE_STT
