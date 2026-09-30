#pragma once

struct PmImageCliState;
namespace CLI { class App; }

/// Register the unified `pm-image status` command.
void pm_image_register_status(CLI::App& app, PmImageCliState& st);

/// `pm-image status` — show Pixlwiz credits plus license/trial details when enabled.
int pm_image_cmd_status_pixlwiz(CLI::App& app, PmImageCliState& st);
