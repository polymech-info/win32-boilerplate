// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_CREATE_HPP
#define PM_IMAGE_CMD_CREATE_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_create(CLI::App& app, PmImageCliState& st);

void pm_image_register_create(CLI::App& app, PmImageCliState& s);

#endif
