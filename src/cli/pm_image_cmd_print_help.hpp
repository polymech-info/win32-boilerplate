// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_PRINT_HELP_HPP
#define PM_IMAGE_CMD_PRINT_HELP_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_print_help(CLI::App& app, PmImageCliState& st);

#endif
