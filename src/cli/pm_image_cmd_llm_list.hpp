// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_LLM_LIST_HPP
#define PM_IMAGE_CMD_LLM_LIST_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_llm_list(CLI::App& app, PmImageCliState& st);

#endif
