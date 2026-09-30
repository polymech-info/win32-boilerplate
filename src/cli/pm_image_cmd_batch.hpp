// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_BATCH_HPP
#define PM_IMAGE_CMD_BATCH_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_batch(CLI::App& app, PmImageCliState& st);

void pm_image_register_batch(CLI::App& app, PmImageCliState& s);

#endif
