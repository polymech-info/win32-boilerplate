#pragma once
// Data-only view models for CSettingsView (no Win32 / MFC).
// Kept separate from SettingsPanel.h to limit header size and coupling.

#include <string>
#include <vector>

// ── Pre-resize (in memory, libvips) — used by Meta, Find (LLM), Transform ────
struct PreResizeInMemoryOptions {
    bool enabled = false;
    int  width   = 512; ///< long edge: 256 | 512 | 768 | 1024
    /// Transform only: if true, only camera RAW / HEIC get the explicit downscale when enabled.
    /// If false, every input is downscaled (legacy behaviour). Meta/Find ignore this field in core.
    bool raw_only = true;
};

// ── TransformSettings (AI image edit — matches media::TransformOptions UI side) ─
struct TransformSettings {
    std::string provider     = "replicate";
    std::string model        = "gemini-3-pro-image-preview";
    std::string aspect_ratio;   // empty = auto
    std::string image_size;     // empty = default
    std::string prompt;
    /// Optional reference image paths (logo / brand sheet / style swatch).
    std::vector<std::string> reference_images;
    /// Same semantics as Meta “Pre-resize” / Find LLM, plus optional RAW-only for transform.
    PreResizeInMemoryOptions preresize_in_memory{};
};

// ── FindSettings (matches media::FindOptions; UI state lives here) ────────────
struct FindSettings {
    std::string prompt;             // user query (required unless reference_images supplied)
    bool        llm = false;        // semantic LLM mode

    // Name mode
    bool case_insensitive = true;
    bool match_folders    = true;
    bool recursive        = true;

    // LLM mode
    bool bypass_cache = false;
    bool generate     = true;
    bool use_md       = true;
    bool use_json     = true;
    bool use_exif     = true;
    int  max_results  = 0;          // 0 = unlimited

    // Reused meta plumbing for LLM judge / on-the-fly generation
    std::string provider     = "google";
    std::string model        = "gemini-3-pro-image-preview";
    int         resize_width = 512;
    bool        resize_first = true;

    /// Optional reference image paths — examples the LLM judge should compare
    /// candidates against (logo, brand sheet, "find more like this" image).
    std::vector<std::string> reference_images;
};

// ── MetaSettings (matches media::MetaOptions; UI state lives here) ────────────
struct MetaSettings {
    // Outputs (any combination)
    bool        out_md       = true;
    bool        out_json     = true;
    bool        update_exif  = false;

    // Pre-flight resize (in memory) — same width presets as `PreResizeInMemoryOptions` / Find LLM.
    bool        resize_first = true;
    int         resize_width = 512;

    // Provider / model / prompt
    std::string provider     = "google";
    std::string model        = "gemini-3-pro-image-preview";
    std::string prompt;          // empty = use lib default

    // Output folder (.md/.json) — empty = next to source
    std::string out_dir;
};

// ── CompressSettings ──────────────────────────────────────────────────────────
/// Which command tabs had `command_provider_overrides` entries after load (used to avoid duplicate Replicate reload on startup).
struct CommandProviderOverridesKeysPresent {
    bool transform = false;
    bool meta = false;
    bool find = false;
};

struct CompressSettings {
    int         output_preset    = 0;     // 0=next to source, 1=_compressed, 2=custom
    std::string out_dir;

    // Compressor selection
    bool        use_mozjpeg      = false; // false=PNG, true=MozJPEG (JPEG output)

    // MozJPEG options
    int         jpeg_quality     = 85;
    bool        jpeg_progressive = true;
    bool        jpeg_trellis     = false;

    // PNG options
    int         level            = 9;     // DEFLATE 1–9
    bool        quantize         = false;
    int         colors           = 256;
    int         q_quality        = 85;
    bool        zopfli           = false;
    int         zopfli_iter      = 15;

    // Common
    bool         strip_metadata   = true;
};
