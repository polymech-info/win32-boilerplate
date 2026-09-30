#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_print_help.hpp"

int pm_image_cmd_print_help(CLI::App& app, PmImageCliState& st) {
    std::cout << app.help() << "\n";
    return 0;
}
