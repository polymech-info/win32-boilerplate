#ifndef PM_IMAGE_REGISTER_CLI_HPP
#define PM_IMAGE_REGISTER_CLI_HPP

#include "pm_image_cli_state.hpp"
#include "constants.hpp"
#include <CLI/CLI.hpp>
#include <string>
#include <vector>

namespace pm::cli {

struct RegisteredCommandInfo {
    Cmd cmd;
    const char* id;
    const char* label;
    bool available;
};

struct CustomCommandInfo {
    std::string group;
    std::string id;
    std::string label;
    std::string type;
    std::string action;
};

struct RegisteredAppCommandInfo {
    const char* id;
    const char* label;
    bool available;
};

std::vector<RegisteredCommandInfo> registered_cli_commands();
std::vector<RegisteredAppCommandInfo> registered_app_commands();
std::vector<CustomCommandInfo> visible_custom_commands();
std::string registered_cli_commands_help_text();

} // namespace pm::cli

void pm_image_register_cli(CLI::App& app, PmImageCliState& s,
                           pm::cli::CmdFlags cmds = pm::cli::k_cmds_all);

#endif
