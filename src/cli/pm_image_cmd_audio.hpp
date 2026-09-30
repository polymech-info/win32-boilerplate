#pragma once

#if defined(FEATURE_STT) && FEATURE_STT

#include "pm_image_cli_state.hpp"
#include <CLI/CLI.hpp>

int pm_image_cmd_audio(CLI::App& app, PmImageCliState& st);

void pm_image_register_audio(CLI::App& app, PmImageCliState& s);

#endif // FEATURE_STT
