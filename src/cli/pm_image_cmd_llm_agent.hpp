// Per-subcommand handler (split from pm_image_cmd_others).
#ifndef PM_IMAGE_CMD_LLM_AGENT_HPP
#define PM_IMAGE_CMD_LLM_AGENT_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_llm_agent(CLI::App& app, PmImageCliState& st);

void pm_image_register_llm(CLI::App& app, PmImageCliState& s);

#endif
