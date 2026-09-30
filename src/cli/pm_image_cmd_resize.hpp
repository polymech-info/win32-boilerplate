#ifndef PM_IMAGE_CMD_RESIZE_HPP
#define PM_IMAGE_CMD_RESIZE_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_resize(CLI::App& app, PmImageCliState& st);

void pm_image_register_resize(CLI::App& app, PmImageCliState& s);

#endif
