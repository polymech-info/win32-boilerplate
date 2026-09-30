#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <windows.h>

class CMainFrame;

namespace pm::win32_dock {

static constexpr int kJsonVersion = 1;

/// @param workbench_slot  `settings["workbench"][slot]`; legacy top-level `win32_dock` is read only when @p workbench_slot is `main`.
bool load_win32_dock_doc(nlohmann::json& out, std::string& err, const char* workbench_slot = "main");
bool save_win32_dock_doc(const nlohmann::json& doc, std::string& err, const char* workbench_slot = "main");
/// Remove `win32_dock` from settings.json (e.g. after "reset layout" / bad state).
/// @param workbench_slot  Clear nested `win32_dock` for that slot; also clears legacy top key when @p workbench_slot is `main`.
void erase_from_settings(const char* workbench_slot = "main");

/// Dock JSON is persisted under the active workbench slot, e.g. @c settings.json["workbench"]["main"]["win32_dock"].
/// When the slot is @c "main", legacy top-level @c win32_dock is also loaded for migration.
/// When @p doc has `v`+`children`+`ancestor_style`, recreates the dock tree (no registry).
/// On failure, calls @p f.CloseAllDockers(). (`CMainFrame` is a @ref friend for `NewDockerFromID`.)
bool apply_dock_tree_from_json(CMainFrame& f, const nlohmann::json& doc, std::string& err);

/// Tab order + active page for each tab group (same data as `LoadDockContainers` applies from JSON).
bool apply_containers_from_json(CMainFrame& f, const nlohmann::json& container_array, std::string& err);

/// Current layout → `win32_dock` document (version @ref kJsonVersion).
nlohmann::json serialize_dock_to_json(CMainFrame& f);

} // namespace pm::win32_dock
