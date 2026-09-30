#pragma once

#include <string>

/** First-run split positions from `dev.json` (parallels `window` + `win32_dock` on Windows). */
struct PmDevGuiDefaults {
  int explorer_width_pt  = 240; /**< `dockID` 7 (file tree) */
  int log_height_pt      = 200; /**< `dockID` 1 (log strip) on Windows; bottom row */
  bool   loaded          = false;
};

/** Searches: `PM_IMAGE_DEV_JSON`, `…/dist/dev.json` (relative to executable). */
bool pm_dev_load_gui_defaults(struct PmDevGuiDefaults& out, std::string& err);
