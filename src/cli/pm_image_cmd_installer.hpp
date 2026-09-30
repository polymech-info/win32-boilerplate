#pragma once

#include "pm_image_cmd_includes.hpp"

int pm_image_cmd_installer(CLI::App& app, PmImageCliState& st);
void pm_image_register_installer(CLI::App& app, PmImageCliState& s);
