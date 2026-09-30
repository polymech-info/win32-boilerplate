#pragma once

#include "win/settings_store.hpp"

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace media::win::session_replay {

constexpr int kSessionFileVersion = 2;

std::filesystem::path default_sessions_dir();

/**
 * Resolves a CLI `--path` for session replay: uses @p utf8 as given first, then
 * `get_config_dir() / utf8` when relative, then `get_config_dir() / "sessions" / <filename>`.
 * On failure, sets @p err and leaves @p out_abs empty. On success, @p out_abs is absolute.
 */
bool resolve_session_replay_cli_input(const std::string& utf8, std::filesystem::path& out_abs, std::string& err);

/// `session-HHMMSS.json` in local time (avoids name clashes within a minute).
std::wstring make_session_filename();

nlohmann::json window_layout_to_json(const media::settings::WindowLayout& wl);
bool         json_to_window_layout(const nlohmann::json& j, media::settings::WindowLayout& out, std::string& err);

/// @param input_events If non-null and an array, written to `root["events"]`; otherwise an empty array.
/// @param snapshot_docks If non-null and an object, merged into `snapshot` as `docks` (Win32++ dock sizes / visibility).
nlohmann::json build_session_json(const media::settings::WindowLayout& wl, HWND main_hwnd,
                                  const nlohmann::json* input_events = nullptr,
                                  const nlohmann::json* snapshot_docks  = nullptr);

bool read_session_file(const std::filesystem::path& p, nlohmann::json& out, std::string& err);
bool write_session_file(const std::filesystem::path& p, const nlohmann::json& j, std::string& err);

} // namespace media::win::session_replay
