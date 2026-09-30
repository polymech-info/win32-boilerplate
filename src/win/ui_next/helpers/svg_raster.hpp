#pragma once

#include <wxx_cstring.h>

#include <cstdint>
#include <windows.h>

/// Rasterize a UTF-8 SVG (Tabler `currentColor` filled) to a 32-bpp DIB for toolbar imagelists.
/// @param svgUtf8   UTF-8 bytes (NUL not required; size is @p len)
/// @param dim       Square output size in px (DPI already applied by caller)
/// @param r,g,b     Fill color in place of `currentColor` (0–255)
/// @return          Bitmap handle, or `nullptr` on failure (caller `DeleteObject` on success)
HBITMAP pmui_svg_rasterize_colored(const char* svgUtf8, size_t len, int dim, std::uint8_t r, std::uint8_t g, std::uint8_t b);

/// Load file bytes (PhysFS: `assets.pfs` or dev `icons/filled/`, then direct paths) and rasterize.
HBITMAP pmui_svg_rasterize_file_wide(const wchar_t* filePathW, int dim, std::uint8_t r, std::uint8_t g, std::uint8_t b);
