#pragma once

#include <string>

namespace pmui {

/// Defines a read-only window.APP_FEATURES object for WebView2 documents.
std::wstring webview_app_features_bootstrap_js();

/// Defines a read-only window.SYSTEM_CONTEXT object for WebView2 documents.
std::wstring webview_system_context_bootstrap_js();

} // namespace pmui
