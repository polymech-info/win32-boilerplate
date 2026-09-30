#pragma once

#if defined(FEATURE_VIDEO) && FEATURE_VIDEO

#include <CLI/CLI.hpp>

struct PmImageCliState;

// Dispatch handler for the "video" subcommand.
// Reads st.video_*_cmd to determine which sub-verb (info / image / video) was parsed.
int pm_image_cmd_video(CLI::App& app, PmImageCliState& st);

void pm_image_register_video(CLI::App& app, PmImageCliState& s);

#endif // FEATURE_VIDEO
