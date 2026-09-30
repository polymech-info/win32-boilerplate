#pragma once

#include "polymech_export.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace media {

/**
 * Sharp-like processing options (libvips backend).
 * @see https://sharp.pixelplumbing.com/api-resize
 * @see https://www.libvips.org/API/current/class.Image.html
 */
struct ResizeOptions {
    int max_width = 0;   // 0 = unconstrained (use with max_height)
    int max_height = 0;  // 0 = unconstrained

    /** Output container: png, jpg, jpeg, webp, tif, tiff, avif, heic, … — empty = infer from output path */
    std::string format;

    /**
     * How the image should fit the target box (max_width × max_height when both set).
     * inside — default; not larger than box (Sharp “inside” / contain bounds).
     * cover — fill box, crop overflow (centre or `position`).
     * contain — fit inside box; letterbox to exact WxH using `background` when both dimensions set.
     * fill — stretch to exact WxH (ignores aspect).
     * outside — at least as large as the box on both axes (no crop); may exceed WxH.
     */
    std::string fit = "inside";

    /** For `cover`: centre, attention, entropy, low, high — maps to libvips “interesting”. */
    std::string position = "centre";

    /** nearest, cubic, mitchell, lanczos2, lanczos3 (default, closest to Sharp). */
    std::string kernel = "lanczos3";

    int quality = 85;        /**< JPEG / WebP / AVIF-style quality 1–100 */
    int png_compression = 6; /**< PNG DEFLATE 0–9 */

#ifdef FEATURE_PNG_COMPRESSOR
    // ── Post-resize PNG optimisation (FEATURE_PNG_COMPRESSOR) ────────────────
    /** Run libimagequant palette quantisation on PNG outputs (lossy, smaller). */
    bool png_quantize         = false;
    int  png_quantize_colors  = 256;    /**< Palette size 8–256 */
    int  png_quantize_quality = 85;     /**< Quantisation quality 60–100 */
    /** Ultra-compress PNG DEFLATE with zopfli (lossless, ~10–50× slower). */
    bool png_zopfli           = false;
    int  png_zopfli_iter      = 15;     /**< zopfli iteration count (speed ↔ size) */
#endif

    bool without_enlargement = true;
    bool autorotate = true;       /**< Apply EXIF orientation (like Sharp default). */
    bool strip_metadata = true;  /**< Strip EXIF etc. on save where supported */

    /** Degrees: 0, 90, 180, 270 — applied after autorotate. */
    int rotate = 0;
    bool flip = false;  /**< Vertical flip */
    bool flop = false;  /**< Horizontal flip */

    /** Letterbox / embed: `#rrggbb` or `#rgb` */
    std::string background = "#ffffff";

    /** When true (default), reuse prior outputs under `cache_dir` keyed by input + mtime + options. */
    bool cache_enabled = true;
    /** Empty: `<cwd>/cache/images`; otherwise absolute or relative path resolved at use time. */
    std::string cache_dir;

    /** HTTP(S) download: total + connect timeout (seconds). Default 5. */
    int url_timeout_sec = 5;
    /** Max redirects when fetching URL inputs. Default 20. */
    int url_max_redirects = 20;

    /**
     * When the resize dialog (or future callers) omit an explicit output path, append this to the stem
     * before the extension (e.g. "_resized"). Used only by default_output_path_for_resize; empty = unchanged name.
     */
    std::string output_stem_suffix;
};

/** Defaults for `serve` / `ipc` when JSON omits `cache` / `cache_dir`. */
struct CacheServerDefaults {
    bool enabled = true;
    std::string cache_dir;
};

POLYMECH_API bool resize_file(const std::string& input_path, const std::string& output_path, const ResizeOptions& opt,
                              std::string& err_out);

/**
 * Buffer-only variant — never touches the filesystem. Designed for the REST
 * /v1/llm tool surface and any other zero-fs caller that already has the image
 * bytes in memory.
 *
 * @param in_data   pointer to the source image bytes (any libvips-loadable format)
 * @param in_size   size in bytes
 * @param opts      same ResizeOptions as the file variant; cache_enabled and
 *                  url_* fields are ignored (no fs / no network in this path).
 *
 * Notes:
 *  - When opts.format is empty the encoder defaults to JPEG (we have no path to
 *    infer from).
 *  - Supported encoders: jpeg/jpg, png, webp. Other formats return an error
 *    string in result.error.
 */
struct ResizeBufferResult {
    bool        ok = false;
    std::string error;
    std::string mime;        // "image/jpeg", "image/png", "image/webp"
    std::string bytes;       // raw output bytes
};
POLYMECH_API ResizeBufferResult resize_buffer(const void* in_data, std::size_t in_size,
                                              const ResizeOptions& opts);

/** Result of `resize_batch` when `out_stats` is set (REST / IPC). */
struct ResizeBatchResult {
    int count = 0;
    std::vector<std::string> outputs;
};

/**
 * One or more inputs (glob `*` `?` `**` or a single file) paired to output file(s) or a directory.
 * Stops on first failure; `err_out` names the failing input when possible.
 * @param progress If set, invoked before each file with (0-based index, total count).
 */
POLYMECH_API bool resize_batch(const std::string& input_spec, const std::string& output_spec, const ResizeOptions& opt,
                               std::string& err_out, ResizeBatchResult* out_stats = nullptr,
                               const std::function<void(std::size_t, std::size_t)>& progress = {});

/** Merge JSON keys into `opt` (REST / IPC). Unknown keys ignored. */
POLYMECH_API void apply_resize_options_from_json(const nlohmann::json& j, ResizeOptions& opt);

/** Apply JSON `cache` / `cache_dir`, then fill from `defaults` when those keys are absent. */
POLYMECH_API void apply_cache_defaults_from_json(const nlohmann::json& j, ResizeOptions& opt, const CacheServerDefaults& defaults);

} // namespace media
