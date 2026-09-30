#ifndef PM_UI_CHATWEBRESOURCE_H
#define PM_UI_CHATWEBRESOURCE_H

#include <string>

namespace pmui {

/// True when the build included the apps/chat-next WebView bundle (FEATURE_CHAT_WEB=ON).
bool chat_web_available();

/// Load chat.html (UTF-8) from dist/shared, with legacy beside-exe fallback.
/// Returns an empty string when FEATURE_CHAT_WEB is OFF or the file is missing.
///
/// The HTML is **self-contained** — webpack's html-inline-script plugin
/// inlines all CSS + JS. The WebView2 host can load it via NavigateToString
/// (no need for a virtual scheme handler unless you want richer hosting).
std::string load_chat_web_html();

/// Load settings.html (UTF-8) from dist/shared, with legacy beside-exe fallback.
/// Returns an empty string when the file is missing.
std::string load_settings_web_html();

/// Load xblox.html (UTF-8) from dist/shared, with legacy beside-exe fallback.
/// Returns an empty string when the file is missing.
std::string load_xblox_web_html();

} // namespace pmui

#endif // PM_UI_CHATWEBRESOURCE_H
