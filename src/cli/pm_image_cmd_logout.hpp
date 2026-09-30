#pragma once

#include <CLI/CLI.hpp>

struct PmImageCliState;

int pm_image_cmd_logout(CLI::App& app, PmImageCliState& st);
