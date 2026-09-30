// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_SERVE_HPP
#define PM_IMAGE_CMD_SERVE_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_serve(CLI::App& app, PmImageCliState& st);

void pm_image_register_serve(CLI::App& app, PmImageCliState& s);

#endif
