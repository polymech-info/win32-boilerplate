// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_REGISTER_EXPLORER_HPP
#define PM_IMAGE_CMD_REGISTER_EXPLORER_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_register_explorer(CLI::App& app, PmImageCliState& st);

void pm_image_register_explorer(CLI::App& app, PmImageCliState& s);

#endif
