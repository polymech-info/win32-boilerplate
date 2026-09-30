#pragma once

// Assistant toolbar — Win32 lib layer.
// Consumed by pm-image-cli.exe (debug/foreground) and pm-image.exe (production).
//
// Daemon integration: daemon.json action type "assistant-bar" spawns
//   pm-image-cli.exe assistant spy --ui
// which calls AssistantBar::run() on a dedicated thread.

#if defined(_WIN32) && defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <functional>

namespace media::assistant {

// ── Configuration (initial state) ─────────────────────────────────────────────
struct AssistantBarConfig {
    bool spy_on = true;    ///< spy loop toggle initially active
    bool stt_on = false;   ///< microphone (STT) initially active
    bool tts_on = false;   ///< speaker   (TTS) initially active
};

// ── Button callbacks (called on the bar's own message-loop thread) ─────────────
struct AssistantBarCallbacks {
    std::function<void(bool on)> on_spy_toggle;  ///< spy toggle clicked
    std::function<void(bool on)> on_stt_toggle;  ///< mic button clicked
    std::function<void(bool on)> on_tts_toggle;  ///< speaker button clicked

    /// Returns the HWND of the last active user application.
    /// Called on BTN_SHOT click — must be fast (called on the UI thread).
    /// The bar is WS_EX_NOACTIVATE so GetForegroundWindow() works here.
    std::function<HWND()>        get_target_hwnd;

    std::function<void()>        on_chat_open;   ///< chat/open button clicked
    std::function<void()>        on_close;        ///< bar closed by user
};

// ── Toolbar window ────────────────────────────────────────────────────────────
// Borderless WS_POPUP pill, 34 × 192 px (computed from layout constants).
// Buttons (top → bottom): spy toggle, mic/STT, speaker/TTS,
//   screenshot→clipboard, open-in-chat, drag-dots grip, close.
//
// Snap: after every drag-release the bar checks all monitor edges within
//   24 px and snaps to the nearest one. When snapped it auto-hides after 2 s
//   of no hover (animated slide, leaves a 4 px peek strip).
//
// Threading: run() blocks on the Win32 message loop.
//   Call it from a dedicated thread; all other methods post thread-safe messages.
class AssistantBar {
public:
    AssistantBar();
    ~AssistantBar();

    AssistantBar(const AssistantBar&)            = delete;
    AssistantBar& operator=(const AssistantBar&) = delete;

    // Blocking — creates the window, runs the message pump, returns on close.
    int run(const AssistantBarConfig&   cfg       = {},
            const AssistantBarCallbacks& callbacks = {});

    // Thread-safe — posts WM_CLOSE to the bar window.
    void request_close();

    // Thread-safe button state updates (also repaints the relevant button).
    void set_spy_state(bool on);
    void set_stt_state(bool on);
    void set_tts_state(bool on);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace media::assistant

#endif // _WIN32 && FEATURE_ASSISTANT
