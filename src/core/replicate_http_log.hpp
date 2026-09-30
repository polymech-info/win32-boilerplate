// Helpers for Replicate HTTP logging: redact data:...;base64,... payloads, never log bearer token.

#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

namespace media {
namespace replicate_log {

// Recursively replace data URL strings (images / binary) with length hints — keeps prompts and URLs.
inline void redact_data_urls_in_json(nlohmann::json& j) {
    if (j.is_string()) {
        std::string s0 = j.get<std::string>();
        if (s0.rfind("data:", 0) == 0) {
            const auto comma = s0.find(',');
            if (comma != std::string::npos) {
                const std::size_t b64_n = s0.size() - (comma + 1);
                j = std::string("<data:URL redacted, base64_chars=") + std::to_string(b64_n)
                    + " approx_image_bytes~=" + std::to_string(b64_n * 3 / 4) + ">";
            } else {
                j = std::string("<data:URL redacted, len=") + std::to_string(s0.size()) + ">";
            }
        }
    } else if (j.is_object()) {
        for (auto& it : j.items()) {
            redact_data_urls_in_json(it.value());
        }
    } else if (j.is_array()) {
        for (auto& el : j) {
            redact_data_urls_in_json(el);
        }
    }
}

inline std::string dump_json_for_log(nlohmann::json j, std::size_t max_chars = 0) {
    redact_data_urls_in_json(j);
    std::string s = j.dump();
    if (max_chars > 0 && s.size() > max_chars) {
        s.resize(max_chars);
        s += "...(truncated)";
    }
    return s;
}

} // namespace replicate_log
} // namespace media
