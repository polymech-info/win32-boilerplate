#pragma once

#include <string>
#include <string_view>

namespace pmui {

/**
 * Maps settings.json `appearance.display_language` (`en`|`es`|`de`|`it`|`fr`) to a Win32 LANGID and
 * calls SetThreadUILanguage so ribbon / menu / dialog resources resolve to the matching LANGUAGE blocks.
 * Call once on the UI thread before creating any windows (see launch_ui_next.cpp).
 */
void apply_display_language_from_settings();

/** Apply thread UI language from a language code (invalid codes fall back to `en`). */
void apply_display_language(std::string_view code);

/** After the user changes display language in App Settings: optionally spawn a new instance and quit this one. */
bool restart_current_process();

} // namespace pmui
