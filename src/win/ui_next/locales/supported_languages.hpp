#pragma once

#include <array>
#include <string_view>

namespace pmui::locales {

/** BCP-47 style lower-case tags; `en` is the default authoring / fallback language. */
inline constexpr std::array<std::string_view, 5> kSupportedLanguages = {
    "en",
    "es",
    "de",
    "it",
    "fr",
};

inline constexpr std::string_view kDefaultLanguage = "en";

[[nodiscard]] constexpr bool is_supported(std::string_view code) noexcept
{
    for (std::string_view lang : kSupportedLanguages) {
        if (lang == code)
            return true;
    }
    return false;
}

} // namespace pmui::locales
