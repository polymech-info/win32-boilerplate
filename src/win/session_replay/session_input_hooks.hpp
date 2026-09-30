#pragma once

#include <nlohmann/json.hpp>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace media::win::session_replay::input {

/// Install `WH_MOUSE_LL` + `WH_KEYBOARD_LL` for the current process; events are
/// window-aware (hit-test HWND, class name) with move **threshold** + drag (tighter
/// move sampling while a button is held).
///
/// Call from the same thread that owns @p main_frame_hwnd (UI thread). Returns false
/// on failure; partial hooks are removed if the second hook fails.
bool start_input_recording(HWND main_frame_hwnd, std::string& err);

/// Uninstall hooks and fill @p out_events with the captured list (replaces any previous value).
void stop_input_recording(nlohmann::json& out_events);

/// True between successful `start_` and `stop_` (e.g. if you need a guard before calling stop twice).
bool is_input_recording();

} // namespace media::win::session_replay::input
