#ifndef PM_IMAGE_DISPATCH_HPP
#define PM_IMAGE_DISPATCH_HPP

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_dispatch_parsed(CLI::App& app, PmImageCliState& s);

#endif
