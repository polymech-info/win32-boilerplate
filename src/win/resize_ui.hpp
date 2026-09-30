#pragma once

#include <string>

#include "core/resize.hpp"

namespace media::win {

/**
 * Show a native Win32 dialog: pick input image, optional output path, and resize options.
 * Seeds defaults from `initial` (e.g. CLI flags). On Cancel returns false.
 */
bool show_resize_ui(ResizeOptions &opt, std::string &input_path, std::string &output_path,
                      const ResizeOptions &initial);

} // namespace media::win
