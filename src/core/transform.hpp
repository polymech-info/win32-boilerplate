#pragma once

#include "polymech_export.h"

#include <string>
#include <vector>
#include <functional>

#include <nlohmann/json.hpp>

namespace media {

struct BatchControl;

/// Options for **all** `transform.cpp` generative backends: Gemini/Replicate **image** (`transform_image`, `create_image`,
/// `transform_buffer`, …) and **Replicate video** (`replicate_video_*`). Same type so one settings merge / JSON parse path
/// can supply `provider`, `model`, keys, and raster-read hints (`resize_*`) used when loading stills for video `input`.
struct TransformOptions {
    /// Set explicitly, via JSON (`apply_transform_options_from_json`), or from app/CLI defaults (e.g. chat image settings).
    std::string provider;
    std::string model;
    std::string api_key;
    /// Google Gemini API root (e.g. `https://generativelanguage.googleapis.com/v1beta`).
    /// Empty uses the public default. Set from app Provider settings on Windows.
    std::string base_url;
    std::string prompt;
    std::string aspect_ratio;   // e.g. "1:1","16:9","4:3","3:4","9:16","21:9",""=auto
    std::string image_size;     // "512","1K","2K","4K",""=default(1K)

    /// When set with `resize_width` > 0, decode via libvips and scale the longest
    /// edge before the API (any format vips understands, including large rasters).
    bool resize_first = false;
    int  resize_width = 0;
    /// When `resize_first` is set: if true, only camera RAW / HEIC use the explicit
    /// long-edge downscale; other rasters are read as before (no forced pre-resize).
    bool preresize_raw_only = false;

    /// Optional reference images (paths) sent alongside the input image — e.g. a
    /// brand logo, design sheet, or style swatch the model should respect when
    /// generating the transformed result. Order is preserved; each is added as
    /// an additional `inlineData` part in the multimodal Gemini request.
    std::vector<std::string> reference_images;

    /// Replicate image-to-video only: JSON key for the first still (data URL), e.g. `image`, `init_image`,
    /// `first_frame_image`. Empty = use OpenAPI **Input** keyframe plan from `replicate-models-cache.json` for `model`.
    std::string replicate_first_frame_key;
    /// When two frames are supplied, JSON key for the second still (e.g. `last_frame`, `last_frame_image`). Empty = from OpenAPI plan or omit.
    std::string replicate_second_frame_key;
};

struct TransformResult {
    bool        ok = false;
    std::string error;
    std::string output_path;          // written file
    std::string ai_text;              // optional text part from model
    std::vector<uint8_t> image_data;  // raw bytes (PNG/JPEG) before writing
    /// When `provider == "replicate"`, Replicate's prediction `urls.web` (dashboard / share), if returned.
    std::string replicate_prediction_web_url;
};

using TransformProgressFn = std::function<void(const std::string& status)>;

/// Edit a single image using a generative AI model.
/// Reads `input_path`, sends image + prompt to the API, writes result to `output_path`.
/// If `output_path` is empty, derives it from input + prompt.
POLYMECH_API TransformResult transform_image(
    const std::string& input_path,
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress = nullptr,
    BatchControl* batch = nullptr,
    std::function<void()> on_before_batch_pause = nullptr
);

/// Generate an image from a text prompt (and optional reference images). No input raster;
/// use this instead of `transform_image` when there is no source file to edit.
/// If `output_path` is empty, writes `create_<slug>.png` in the process current directory.
POLYMECH_API TransformResult create_image(
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress = nullptr,
    BatchControl* batch = nullptr,
    std::function<void()> on_before_batch_pause = nullptr
);

/// Replicate **video** from ordered still frames (mime + raw bytes per frame).
/// `provider` must be `replicate`. Keyframe JSON keys come from the local OpenAPI **Input** cache for `options.model`,
/// or from `replicate_first_frame_key` / `replicate_second_frame_key` when set (no slug heuristics).
/// Optional @p reference_mime_bytes are sent only when the model's OpenAPI **Input** defines a matching array field
/// (see local model cache); there is no slug-based `reference_images` fallback.
/// If `output_path` is empty, writes `video_<slug>.mp4` under the process cwd (or see `default_replicate_video_output_path`).
POLYMECH_API TransformResult replicate_video_frames(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& frames_mime_bytes,
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress = nullptr,
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& reference_mime_bytes = {});

/// Replicate video with caller-built `input` JSON (OpenAPI **Input** keys). String values that point at
/// existing local files are read and replaced with `data:...;base64,...` before POST. Remote `http(s)` and
/// existing `data:` strings are left unchanged.
POLYMECH_API TransformResult replicate_video_replicate_input(
    nlohmann::json input,
    const std::string& output_path,
    const TransformOptions& opts,
    TransformProgressFn progress = nullptr);

/// Default `video_<prompt_slug>.mp4`. If @p preferred_parent_dir is non-empty, uses that folder; otherwise the process cwd.
POLYMECH_API std::string default_replicate_video_output_path(const std::string& prompt,
                                                             const std::string& preferred_parent_dir = {});

/// Path the Replicate video pipeline will write to **before** POST: @p explicit_output_path if non-empty, else the same
/// default as @ref replicate_video_replicate_input (first local file parent in @p input + prompt slug). The final file may
/// still differ when a `.png` placeholder is replaced with `.mp4` / `.webm` / `.mov` after the response MIME is known.
POLYMECH_API std::string replicate_video_intended_output_path(const std::string& explicit_output_path,
                                                                const nlohmann::json& input,
                                                                const std::string& prompt);

/// Read bytes + MIME from disk for Replicate/Gemini image inputs (optional vips resize / RAW decode).
POLYMECH_API bool read_raster_for_llm_transform_path(
    const std::string& abs_path,
    const TransformOptions& opts,
    TransformProgressFn progress,
    std::vector<uint8_t>& out_bytes,
    std::string& out_mime,
    std::string& err);

/// Build a default output path from input path and prompt text.
POLYMECH_API std::string default_transform_output(const std::string& input_path, const std::string& prompt);

/// Default path for `create_image` when `output_path` is empty: `<cwd>/create_<slug>.png`.
POLYMECH_API std::string default_create_image_output(const std::string& prompt);

/// Buffer-only variant for the REST endpoint — never touches the host fs.
struct TransformBufferResult {
    bool        ok = false;
    std::string error;
    std::string mime;        // always "image/png" for current Gemini models
    std::string bytes;       // raw output bytes
    std::string ai_text;     // optional text part from the model
};
/// Reference images may also be passed as in-memory blobs; otherwise the
/// `opts.reference_images` paths are read from disk (as in the file variant).
struct ReferenceBuffer {
    std::string mime;        // e.g. "image/png"
    std::string bytes;
};
POLYMECH_API TransformBufferResult transform_buffer(
    const void* in_data, std::size_t in_size,
    const std::string& in_mime,
    const TransformOptions& opts,
    const std::vector<ReferenceBuffer>& ref_buffers = {});

/// Zero-fs **create**: prompt (+ optional in-memory reference images) only; no input image bytes.
POLYMECH_API TransformBufferResult create_buffer(
    const TransformOptions& opts,
    const std::vector<ReferenceBuffer>& ref_buffers = {});

/// Merge JSON keys into @p opts (REST / IPC). Unknown keys are ignored.
/// Recognized: provider, model, api_key, prompt, aspect_ratio, image_size,
/// resize_first, resize_width, preresize_raw_only, reference_images / references / reference.
/// If @p j omits `provider` or `model` (or they are not strings), those fields are cleared so
/// struct defaults are not treated as JSON-specified values.
POLYMECH_API void apply_transform_options_from_json(const nlohmann::json& j, TransformOptions& opts);

/// Build `…/v1beta/models/{model}:generateContent` (strip trailing `/` on @p base_url; if empty, use the public API root).
POLYMECH_API std::string resolve_gemini_generate_url(const std::string& base_url, const std::string& model);

} // namespace media
