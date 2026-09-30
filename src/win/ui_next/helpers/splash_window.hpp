#pragma once

// Startup splash (dist/branding/splash-1.png + logo.png) — docs/splash.md, CMake FEATURE_SPLASH (default ON).

namespace pmui {

void set_splash_disabled_for_session(bool disabled) noexcept;
bool splash_disabled_for_session() noexcept;

#if defined(FEATURE_SPLASH)
void splash_show();
void splash_hide();
void splash_fade_out_and_hide(unsigned durationMs = 300);
void splash_reassert_topmost();
bool splash_is_visible();
#else
inline void  splash_show() {}
inline void  splash_hide() {}
inline void  splash_fade_out_and_hide(unsigned) {}
inline void  splash_reassert_topmost() {}
inline bool splash_is_visible() { return false; }
#endif

// Chat workbench + WebView2: hold startup splash until JS posts `kind: "ready"`, with a
// 10s watchdog on the main frame (`WM_TIMER` on `kTimerChatWebComposerLoadTimeout`). On timeout we
// log, clear the watch, and fade the splash — the process keeps running (no ExitProcess).
// When not waiting, `CMainFrame::OnDeferredPostLayoutInit` may dismiss the splash as before.
void splash_register_chat_workbench_composer_wait(HWND mainFrame);
void splash_on_chat_composer_ready();
void splash_on_chat_workbench_composer_load_timeout();
/// True after `splash_register…` until ready or 10s timeout (timeout fades splash here); OnDeferred skips fade while set.
bool splash_main_frame_defers_splash_dismissal() noexcept;
/// If true, `splash_on_chat_composer_ready` already faded; OnDeferred `consume` clears, skipping the 240ms pass.
bool splash_consume_deferred_splash_dismissal_by_chat_ready() noexcept;

// Must match the id handled in `CMainFrame::WndProc` (WM_TIMER).
inline constexpr UINT_PTR kTimerChatWebComposerLoadTimeout = 5000;

} // namespace pmui
