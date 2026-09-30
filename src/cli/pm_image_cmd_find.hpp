// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_FIND_HPP
#define PM_IMAGE_CMD_FIND_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_find(CLI::App& app, PmImageCliState& st);

void pm_image_register_find(CLI::App& app, PmImageCliState& s);

#endif
