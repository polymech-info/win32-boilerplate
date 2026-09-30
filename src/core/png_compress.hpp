#pragma once
// ── PNG compression pipeline (FEATURE_PNG_COMPRESSOR) ───────────────────────
//
// Provides two optimisation modes for PNG output files:
//
//  1. Palette quantisation (libimagequant) — lossy, up to 60–70 % smaller.
//     Reduces an RGBA image to an indexed 8-bit palette (2–256 colours).
//     Quality is controlled by png_quantize_quality (60–100).
//
//  2. Ultra-DEFLATE (zopfli) — lossless, 5–20 % smaller than level-9 zlib.
//     Re-compresses the PNG's DEFLATE stream using zopfli's block splitter.
//     Much slower (~10–50× vs standard DEFLATE); use sparingly.
//
// Modes can be combined (quantise first, then zopfli-compress the result).
//
// Requires:  cmake -DFEATURE_PNG_COMPRESSOR=ON
// Zopfli:    cmake -DFEATURE_PNG_COMPRESSOR=ON -DFEATURE_PNG_ZOPFLI=ON
//
// ─────────────────────────────────────────────────────────────────────────────
#ifdef FEATURE_PNG_COMPRESSOR

#include <string>

namespace media {

struct PngCompressOptions {
    // ── Palette quantisation (libimagequant, lossy) ───────────────────────────
    bool quantize         = false;
    int  quantize_colors  = 256;    // 8–256
    int  quantize_quality = 85;     // 60–100 (maps to liq min/max quality)

    // ── Compression level ─────────────────────────────────────────────────────
    int  libpng_level     = 9;      // 1–9; only used when zopfli is OFF

    // ── Zopfli ultra-compression (lossless, optional) ─────────────────────────
    bool use_zopfli       = false;
    int  zopfli_iter      = 15;     // zopfli iteration count (speed vs size)
};

/**
 * Compress a PNG image at @p input_path and write the result to @p output_path.
 * If quantize is true, converts to a palette image via libimagequant first.
 * If use_zopfli is true, the DEFLATE stream is re-compressed with zopfli.
 *
 * @p input_path  may be any format readable by libvips (JPEG, PNG, …).
 * @p output_path must end in .png.
 *
 * Returns an empty string on success, or an error message on failure.
 */
std::string png_compress(const std::string& input_path,
                          const std::string& output_path,
                          const PngCompressOptions& opts = {});

/**
 * In-place variant: reads @p path, overwrites it with the compressed result.
 * A temporary file is used internally; @p path is only overwritten on success.
 */
std::string png_compress_inplace(const std::string& path,
                                  const PngCompressOptions& opts = {});

} // namespace media

#endif // FEATURE_PNG_COMPRESSOR
