#pragma once

#include "polymech_export.h"

#include <filesystem>

namespace media::app {

/// Parent directory of the running executable (install / `dist` folder), or empty if unknown.
POLYMECH_API std::filesystem::path exe_parent_directory();

} // namespace media::app
