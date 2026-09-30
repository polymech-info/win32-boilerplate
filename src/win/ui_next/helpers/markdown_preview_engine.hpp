#pragma once
// Default markdown preview engine id — mirrors `apps/viewer-next/src/bridge/engine.ts` `ENGINE`.
// Future: read App Settings; branch FileViewer markdown host (MSHTML WebOC vs embedded CoreWebView2).
#include <cstring>

namespace pmui {

inline constexpr const char k_markdown_preview_engine_default[] = "webview2";

/// Active engine id for the main-frame markdown preview (`FileViewer_Markdown.cpp`).
/// Today returns the default only; later may read settings ("webview2" | "webbrowser", …).
/// `webview2` + `FEATURE_VIEWER_WEB`: main-frame markdown uses `CViewerWebPanel` + apps/viewer-next.
inline const char* markdown_preview_engine() noexcept
{
    return k_markdown_preview_engine_default;
}

inline bool markdown_preview_engine_is_webview2() noexcept
{
    return std::strcmp(markdown_preview_engine(), "webview2") == 0;
}

} // namespace pmui
