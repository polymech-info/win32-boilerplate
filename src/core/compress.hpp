#pragma once
// ── Image Compression Pipeline ───────────────────────────────────────────────
//
// compress_file() provides two independent engines:
//
//  MozJPEG  — re-encodes any input as JPEG using libvips' bundled mozjpeg.
//             Options: quality, progressive, trellis-quant, optimize-scans,
//             overshoot-deringing.  Smaller files than standard libjpeg-turbo
//             at the same perceptual quality.
//
//  PNG      — optimised PNG encode, with three layers:
//             1. vips pngsave at the requested DEFLATE level (always available)
//             2. libimagequant palette quantisation (FEATURE_PNG_COMPRESSOR)
//             3. zopfli ultra-DEFLATE (FEATURE_PNG_ZOPFLI)
//
// Compressor is inferred from the output extension when set to Auto.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>

#include <nlohmann/json.hpp>

namespace media {

enum class Compressor { Auto, MozJPEG, PNG };

struct CompressOptions {
    Compressor compressor = Compressor::Auto;

    // ── Common ────────────────────────────────────────────────────────────────
    bool        strip_metadata             = true;
    std::string output_suffix;      // appended to stem if no explicit output given
                                    // default ("") → "_compressed" when overwriting

    // ── MozJPEG ───────────────────────────────────────────────────────────────
    int  jpeg_quality              = 85;   // 1–100
    bool jpeg_progressive          = true; // interlaced / progressive JPEG
    bool jpeg_optimize_scans       = false;// split DCT coefficient spectrum
    bool jpeg_trellis_quant        = false;// trellis quantisation
    bool jpeg_overshoot_deringing  = true; // reduce ringing artifacts

    // ── PNG ───────────────────────────────────────────────────────────────────
    int  png_level           = 9;    // DEFLATE 1–9
    bool png_quantize        = false;// libimagequant palette reduction (lossy)
    int  png_quantize_colors = 256;  // 8–256
    int  png_quantize_quality= 85;   // 60–100
    bool png_zopfli          = false;// ultra-compress with zopfli
    int  png_zopfli_iter     = 15;   // zopfli iteration count
};

/**
 * Compress @p input and write the result to @p output.
 * The compressor is chosen by CompressOptions::compressor; if Auto it is
 * inferred from the output extension (.jpg → MozJPEG, .png → PNG, else MozJPEG).
 *
 * @return Empty string on success, error message on failure.
 */
std::string compress_file(const std::string& input,
                           const std::string& output,
                           const CompressOptions& opts = {});

/**
 * Generate an output path for a single file given an optional output directory.
 * Chooses the correct extension for the compressor; adds opts.output_suffix
 * (or "_compressed" when the format is unchanged) to avoid clobbering the source.
 */
std::string compress_default_output(const std::string& input,
                                     const std::string& out_dir,
                                     const CompressOptions& opts);

/**
 * Buffer-only variant — never touches the filesystem. Designed for the REST
 * /v1/compress endpoint where the request body holds the bytes and the
 * response body must be the deliverable.
 *
 * @param in_data   pointer to the source image bytes (any libvips-loadable format)
 * @param in_size   size in bytes
 * @param opts      same CompressOptions as the file variant
 * @return result.bytes / result.mime on success, or result.error on failure
 *
 * Notes:
 *  - When opts.compressor == Auto, the compressor is inferred from the *input*
 *    pixel buffer's format (libvips probes the bytes); falls back to MozJPEG.
 *  - With FEATURE_PNG_COMPRESSOR enabled, the libimagequant/zopfli pipeline is
 *    skipped in buffer mode (it's a path-only API); plain vips_pngsave_buffer
 *    is used. The CLI / file API still gets the full pipeline.
 */
struct CompressBufferResult {
    bool        ok = false;
    std::string error;
    std::string mime;        // "image/jpeg" or "image/png"
    std::string bytes;       // raw output bytes
};
CompressBufferResult compress_buffer(const void* in_data, std::size_t in_size,
                                      const CompressOptions& opts);

/**
 * Merge JSON keys into @p opts (REST / IPC). Unknown keys are ignored.
 * Recognized: compressor (mozjpeg|png|auto), strip_metadata, output_suffix,
 * jpeg_quality / -quality, jpeg_progressive / progressive,
 * jpeg_optimize_scans, jpeg_trellis_quant / trellis,
 * png_level / level, png_quantize / quantize, png_quantize_colors / colors,
 * png_quantize_quality / quant_quality, png_zopfli / zopfli,
 * png_zopfli_iter / zopfli_iter.
 */
void apply_compress_options_from_json(const nlohmann::json& j, CompressOptions& opts);

} // namespace media
