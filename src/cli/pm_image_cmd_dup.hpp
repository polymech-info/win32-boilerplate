// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_DUP_HPP
#define PM_IMAGE_CMD_DUP_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_dup(CLI::App& app, PmImageCliState& st);

void pm_image_register_dup(CLI::App& app, PmImageCliState& s);

#endif
