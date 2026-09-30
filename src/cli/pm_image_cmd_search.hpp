// Per-subcommand handler for `search`.
#ifndef PM_IMAGE_CMD_SEARCH_HPP
#define PM_IMAGE_CMD_SEARCH_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_search(CLI::App& app, PmImageCliState& st);

void pm_image_register_search(CLI::App& app, PmImageCliState& s);

#endif
