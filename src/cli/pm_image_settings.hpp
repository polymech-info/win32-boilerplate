#ifndef PM_IMAGE_SETTINGS_HPP
#define PM_IMAGE_SETTINGS_HPP

#include "pm_image_cli_state.hpp"

#include <CLI/CLI.hpp>

void pm_image_register_settings(CLI::App& app, PmImageCliState& s);
int pm_image_cmd_settings(CLI::App& app, PmImageCliState& st);

#endif
