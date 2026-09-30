#include "compress.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <random>
#include <string>

#include <vips/vips.h>
#include <vips/operation.h> // vips_cache_drop_all

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef FEATURE_PNG_COMPRESSOR
#include "png_compress.hpp"
#endif

namespace media {
namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string ext_lower(const std::string& path)
{
    auto e = fs::path(path).extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c){ return std::tolower(c); });
    return e;
}

static Compressor resolve_compressor(const std::string& output_path,
                                     Compressor requested)
{
    if (requested != Compressor::Auto) return requested;
    const auto ext = ext_lower(output_path);
    if (ext == ".png") return Compressor::PNG;
    return Compressor::MozJPEG;   // .jpg / .jpeg / anything else
}

// Capture the latest vips error into a std::string and clear the buffer.
static std::string vips_err(const std::string& prefix)
{
    std::string msg = prefix;
    const char* ve  = ::vips_error_buffer();
    if (ve && ve[0]) { msg += ": "; msg += ve; }
    ::vips_error_clear();
    return msg;
}

/// Random temp in the same directory as the final output (same as resize in-place).
static fs::path unique_temp_beside_output(const fs::path& out_fs, std::string& err_out)
{
    err_out.clear();
    std::error_code ec;
    fs::path      dir = out_fs.parent_path();
    if (dir.empty()) {
        err_out = "in-place: output has no parent directory";
        return {};
    }
    const std::string stem = out_fs.stem().string();
    const std::string ext  = out_fs.extension().string();
    static std::mutex           mtx;
    static std::mt19937_64      rng{std::random_device{}()};
    std::lock_guard<std::mutex> lock(mtx);
    std::uniform_int_distribution<uint64_t> dist;
    for (int i = 0; i < 64; ++i) {
        fs::path p = dir / (stem + ".pm-compress-" + std::to_string(dist(rng)) + ext);
        if (!fs::exists(p, ec))
            return p;
    }
    err_out = "could not allocate unique temp name beside output";
    return {};
}

// Same as resize::commit_inplace_output: vips + Windows MoveFileEx/replace.
static bool commit_inplace_output(const fs::path& tmp, const fs::path& dest, std::string& err_out)
{
    err_out.clear();
    vips_cache_drop_all();
#ifdef _WIN32
    if (::MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING))
        return true;
#endif
    std::error_code ec;
    for (int attempt = 0; attempt < 10; ++attempt) {
        vips_cache_drop_all();
        ec.clear();
        fs::remove(dest, ec);
        if (!ec || !fs::exists(dest, ec))
            break;
#ifdef _WIN32
        ::Sleep(20);
#endif
    }
    ec.clear();
    fs::rename(tmp, dest, ec);
    if (!ec)
        return true;
    vips_cache_drop_all();
    ec.clear();
    fs::copy_file(tmp, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err_out     = "replace in-place output: " + ec.message();
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return false;
    }
    fs::remove(tmp, ec);
    return true;
}

/** True when input and output are the same file (e.g. .JPG → .jpg on Windows). */
static bool paths_are_same_file(const std::string& input_path, const std::string& output_path)
{
    std::error_code ec;
    fs::path        a = fs::absolute(fs::path(input_path), ec);
    fs::path        b = fs::absolute(fs::path(output_path), ec);
    if (fs::exists(a, ec) && fs::exists(b, ec)) {
        if (fs::equivalent(a, b, ec))
            return true;
    }
    ec.clear();
    return fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
}

// ── MozJPEG path ─────────────────────────────────────────────────────────────
// libvips on Windows (vips-dev-w64-all) bundles mozjpeg as its JPEG encoder.
// vips_jpegsave exposes the mozjpeg-specific knobs directly.

static std::string compress_mozjpeg(const std::string& input,
                                     const std::string& output,
                                     const CompressOptions& opts)
{
    VipsImage* img = ::vips_image_new_from_file(input.c_str(), nullptr);
    if (!img)
        return vips_err("compress: cannot load '" + input + "'");

    int rc = ::vips_jpegsave(img, output.c_str(),
        "Q",                   opts.jpeg_quality,
        "strip",               (int)opts.strip_metadata,
        "optimize-coding",     1,      // always on with mozjpeg (Huffman opt)
        "interlace",           (int)opts.jpeg_progressive,
        "trellis-quant",       (int)opts.jpeg_trellis_quant,
        "overshoot-deringing", (int)opts.jpeg_overshoot_deringing,
        "optimize-scans",      (int)opts.jpeg_optimize_scans,
        nullptr);

    ::g_object_unref(img);
    if (rc != 0)
        return vips_err("compress: jpegsave failed → '" + output + "'");
    return {};
}

// ── PNG path ──────────────────────────────────────────────────────────────────

static std::string compress_png(const std::string& input,
                                  const std::string& output,
                                  const CompressOptions& opts)
{
#ifdef FEATURE_PNG_COMPRESSOR
    // Full pipeline: libimagequant + libpng [+ zopfli]
    PngCompressOptions pc{};
    pc.quantize         = opts.png_quantize;
    pc.quantize_colors  = opts.png_quantize_colors;
    pc.quantize_quality = opts.png_quantize_quality;
    pc.libpng_level     = opts.png_level;
    pc.use_zopfli       = opts.png_zopfli;
    pc.zopfli_iter      = opts.png_zopfli_iter;
    return png_compress(input, output, pc);
#else
    // Fallback: vips pngsave at the requested compression level
    VipsImage* img = ::vips_image_new_from_file(input.c_str(), nullptr);
    if (!img)
        return vips_err("compress: cannot load '" + input + "'");

    int rc = ::vips_pngsave(img, output.c_str(),
        "compression", opts.png_level,
        "strip",       (int)opts.strip_metadata,
        nullptr);

    ::g_object_unref(img);
    if (rc != 0)
        return vips_err("compress: pngsave failed → '" + output + "'");
    return {};
#endif
}

// ── Public API ────────────────────────────────────────────────────────────────

std::string compress_file(const std::string& input,
                           const std::string& output,
                           const CompressOptions& opts)
{
    const Compressor comp = resolve_compressor(output, opts.compressor);
    // Windows: "file.JPG" and "file.jpg" are the same path; vips cannot read
    // the source and jpegsave to that path at once. Match resize_file: temp + replace.
    if (paths_are_same_file(input, output)) {
        std::string  tmp_err;
        const fs::path out_fs(output);
        fs::path       tmp_out = unique_temp_beside_output(out_fs, tmp_err);
        if (tmp_out.empty())
            return tmp_err.empty() ? std::string("compress: could not allocate temp path")
                                   : std::move(tmp_err);
        std::string err;
        if (comp == Compressor::MozJPEG)
            err = compress_mozjpeg(input, tmp_out.string(), opts);
        else
            err = compress_png(input, tmp_out.string(), opts);
        if (!err.empty()) {
            std::error_code ec;
            fs::remove(tmp_out, ec);
            return err;
        }
        std::error_code ec2;
        fs::path        canon = fs::weakly_canonical(out_fs, ec2);
        if (ec2)
            canon = fs::absolute(out_fs);
        std::string commit_err;
        if (!commit_inplace_output(tmp_out, canon, commit_err)) {
            return commit_err.empty() ? std::string("compress: commit in-place failed")
                                      : std::move(commit_err);
        }
        return {};
    }
    if (comp == Compressor::MozJPEG)
        return compress_mozjpeg(input, output, opts);
    return compress_png(input, output, opts);
}

// ── Buffer-only variant (zero-fs) ─────────────────────────────────────────────

CompressBufferResult compress_buffer(const void* in_data, std::size_t in_size,
                                      const CompressOptions& opts)
{
    CompressBufferResult res;
    if (!in_data || in_size == 0) {
        res.error = "compress_buffer: empty input";
        return res;
    }

    VipsImage* img = ::vips_image_new_from_buffer(in_data, in_size, "", nullptr);
    if (!img) { res.error = vips_err("compress_buffer: cannot decode input"); return res; }

    // Auto: when the user didn't pick a compressor, default to MozJPEG (the
    // size-win choice) — we have no path/extension to infer from.
    Compressor comp = opts.compressor;
    if (comp == Compressor::Auto) comp = Compressor::MozJPEG;

    void*  out_buf = nullptr;
    size_t out_len = 0;
    int    rc      = -1;

    if (comp == Compressor::MozJPEG) {
        rc = ::vips_jpegsave_buffer(img, &out_buf, &out_len,
            "Q",                   opts.jpeg_quality,
            "strip",               (int)opts.strip_metadata,
            "optimize-coding",     1,
            "interlace",           (int)opts.jpeg_progressive,
            "trellis-quant",       (int)opts.jpeg_trellis_quant,
            "overshoot-deringing", (int)opts.jpeg_overshoot_deringing,
            "optimize-scans",      (int)opts.jpeg_optimize_scans,
            nullptr);
        res.mime = "image/jpeg";
    } else {
        // PNG: libimagequant / zopfli pipeline is path-only; in buffer mode we
        // use plain vips_pngsave_buffer at the requested DEFLATE level.
        rc = ::vips_pngsave_buffer(img, &out_buf, &out_len,
            "compression", opts.png_level,
            "strip",       (int)opts.strip_metadata,
            nullptr);
        res.mime = "image/png";
    }

    ::g_object_unref(img);
    if (rc != 0 || !out_buf) {
        if (out_buf) ::g_free(out_buf);
        res.error = vips_err(comp == Compressor::MozJPEG
                                 ? "compress_buffer: jpegsave failed"
                                 : "compress_buffer: pngsave failed");
        return res;
    }

    res.bytes.assign(static_cast<const char*>(out_buf), out_len);
    ::g_free(out_buf);
    res.ok = true;
    return res;
}

std::string compress_default_output(const std::string& input,
                                     const std::string& out_dir,
                                     const CompressOptions& opts)
{
    fs::path p(input);
    const Compressor comp = resolve_compressor(input, opts.compressor);
    const std::string new_ext  = (comp == Compressor::MozJPEG) ? ".jpg" : ".png";
    const std::string orig_ext = ext_lower(input);

    std::string stem = p.stem().string();
    if (!opts.output_suffix.empty()) {
        stem += opts.output_suffix;
    } else if (orig_ext == new_ext) {
        // Same format — add suffix so we don't clobber the source.
        stem += "_compressed";
    }

    const std::string filename = stem + new_ext;
    if (!out_dir.empty())
        return (fs::path(out_dir) / filename).string();
    return (p.parent_path() / filename).string();
}

// ── JSON option mapping ──────────────────────────────────────────────────────

void apply_compress_options_from_json(const nlohmann::json& j, CompressOptions& opts) {
    auto str = [&](const char* key, std::string& dst) {
        if (j.contains(key) && j[key].is_string()) dst = j[key].get<std::string>();
    };
    auto num = [&](const char* key, int& dst) {
        if (j.contains(key) && j[key].is_number_integer()) dst = j[key].get<int>();
    };
    auto boolean = [&](const char* key, bool& dst) {
        if (!j.contains(key) || j[key].is_null()) return;
        if (j[key].is_boolean()) dst = j[key].get<bool>();
        else if (j[key].is_number_integer()) dst = j[key].get<int>() != 0;
    };

    if (j.contains("compressor") && j["compressor"].is_string()) {
        std::string c = j["compressor"].get<std::string>();
        std::transform(c.begin(), c.end(), c.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        if (c == "mozjpeg" || c == "jpeg" || c == "jpg") opts.compressor = Compressor::MozJPEG;
        else if (c == "png")                              opts.compressor = Compressor::PNG;
        else                                              opts.compressor = Compressor::Auto;
    }

    boolean("strip_metadata",        opts.strip_metadata);
    str    ("output_suffix",         opts.output_suffix);

    // MozJPEG (also accept short names without prefix)
    if (j.contains("quality")    && j["quality"].is_number_integer())  opts.jpeg_quality      = j["quality"].get<int>();
    num    ("jpeg_quality",         opts.jpeg_quality);
    if (j.contains("progressive")) {
        boolean("progressive", opts.jpeg_progressive);
    }
    boolean("jpeg_progressive",     opts.jpeg_progressive);
    boolean("optimize_scans",       opts.jpeg_optimize_scans);
    boolean("jpeg_optimize_scans",  opts.jpeg_optimize_scans);
    boolean("trellis",              opts.jpeg_trellis_quant);
    boolean("trellis_quant",        opts.jpeg_trellis_quant);
    boolean("jpeg_trellis_quant",   opts.jpeg_trellis_quant);
    boolean("overshoot_deringing",  opts.jpeg_overshoot_deringing);

    // PNG
    num    ("level",                opts.png_level);
    num    ("png_level",            opts.png_level);
    boolean("quantize",             opts.png_quantize);
    boolean("png_quantize",         opts.png_quantize);
    num    ("colors",               opts.png_quantize_colors);
    num    ("png_quantize_colors",  opts.png_quantize_colors);
    num    ("quant_quality",        opts.png_quantize_quality);
    num    ("png_quantize_quality", opts.png_quantize_quality);
    boolean("zopfli",               opts.png_zopfli);
    boolean("png_zopfli",           opts.png_zopfli);
    num    ("zopfli_iter",          opts.png_zopfli_iter);
    num    ("png_zopfli_iter",      opts.png_zopfli_iter);
}

} // namespace media
