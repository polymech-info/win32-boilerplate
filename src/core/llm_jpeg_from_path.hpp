#pragma once
// libvips: load any path the stack supports (RAW, HEIC, etc.) and encode to JPEG
// in memory. Shared by meta (optional resize) and transform (RAW/HEIC → API).
#include "polymech_export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace media {

/// Thumbnail longest edge to `max_edge_px`, then JPEG Q85. Populates image dimensions.
POLYMECH_API bool path_to_jpeg_for_llm(
    const std::string& path,
    int                max_edge_px,
    std::vector<uint8_t>& out_bytes,
    int&               orig_w,
    int&               orig_h,
    int&               out_w,
    int&               out_h,
    std::string&       err);

} // namespace media
