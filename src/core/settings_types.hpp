#pragma once

#include "constants.hpp"
#include <map>
#include <string>

namespace media::settings_types {

struct ProviderEntry {
    std::string api_key;
    std::string base_url;
    std::string default_model;
};

using ProviderMap = std::map<std::string, ProviderEntry>;

/// Canonical `settings.json["chat"]` shape. Runtime provider/model selection must come from
/// explicit chat fields or command/tool overrides; provider rows only supply credentials/base URLs.
struct ChatProviderSettings {
    std::string router;
    std::string base_url;
    std::string api_key;
    std::string model;
    int timeout_ms = 180'000;
    int max_iterations = 8;
    std::string image_provider;
    std::string image_model;
    std::string image_recognition_provider;
    std::string image_recognition_model;
    std::string video_provider;
    std::string video_model;
    // Audio — Speech-to-Text and Text-to-Speech provider/model/voice selection.
    // stt_provider / stt_model  : "pixlwiz" + alias  or  "elevenlabs" + scribe model id.
    // tts_provider / tts_model  : "pixlwiz" + alias  or  "elevenlabs" + eleven_* model id.
    // tts_voice_id              : ElevenLabs voice UUID (only used when tts_provider="elevenlabs").
    std::string stt_provider;
    std::string stt_model;
    std::string tts_provider;
    std::string tts_model;
    std::string tts_voice_id;
    /// LLM HTTP endpoint mode → /chat/completions ("completion") or /responses ("responses").
    /// Default is the global knob pm::llm::k_default_api_mode in constants.hpp.
    std::string api_mode = pm::llm::k_default_api_mode;
};

} // namespace media::settings_types
