// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_LLM_CALL_HPP
#define PM_IMAGE_CMD_LLM_CALL_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_llm_call(CLI::App& app, PmImageCliState& st);

#endif
