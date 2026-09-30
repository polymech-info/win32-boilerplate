#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#if defined(_WIN32) && defined(FEATURE_SESSION_VIDEO_RECORDER)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace media::win::recorder {

#if defined(_WIN32) && defined(FEATURE_SESSION_VIDEO_RECORDER)
/// H.264 MP4 of the given top-level window via Windows.Graphics.Capture + Media Foundation.
/// @p main_window is the `HWND` of the frame. Call from the UI thread.
/// @p out_mp4 should end in `.mp4` (e.g. `%USERPROFILE%\\Videos\\session-143055.mp4` from the UI default).
/// @return true on success (call `stop()` to finalize the file).
bool start(HWND main_window, const std::filesystem::path& out_mp4, std::string& err_utf8);

/// Stop capture and finalize the MP4 (safe to call if not recording).
void stop() noexcept;

bool is_recording() noexcept;

/// While recording: toggle pause (skip frames / freeze timeline). No-op if not recording.
void toggle_pause() noexcept;

bool is_paused() noexcept;
#else
inline bool start(void*, const std::filesystem::path&, std::string& err)
{
    err = "session video recorder not available on this build.";
    return false;
}
inline void stop() {}
inline bool is_recording() { return false; }
inline void toggle_pause() noexcept {}
inline bool is_paused() noexcept { return false; }
#endif

} // namespace media::win::recorder
