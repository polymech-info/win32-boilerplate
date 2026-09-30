#include "png_compress.hpp"
#ifdef FEATURE_PNG_COMPRESSOR

#include <vips/vips.h>
#include <libimagequant.h>
#include <png.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <filesystem>

#ifdef FEATURE_PNG_ZOPFLI
#include <zopflipng_lib.h>
#endif

namespace media {
namespace {

// ── libpng write helpers ──────────────────────────────────────────────────────

struct PngWrite {
    FILE*        fp      = nullptr;
    png_structp  png_ptr = nullptr;
    png_infop    info_ptr = nullptr;

    ~PngWrite() {
        if (png_ptr) png_destroy_write_struct(&png_ptr, &info_ptr);
        if (fp) std::fclose(fp);
    }
    bool ok() const { return fp && png_ptr && info_ptr; }
};

// libpng error handler — avoids longjmp-based error handling.
static void png_error_fn(png_structp, png_const_charp msg) {
    throw std::runtime_error(std::string("libpng error: ") + msg);
}
static void png_warn_fn(png_structp, png_const_charp) {}

// ── Write indexed (palette) PNG ───────────────────────────────────────────────

static std::string write_indexed_png(
    const std::string&  output_path,
    int                 width,
    int                 height,
    const liq_palette*  palette,
    const std::vector<uint8_t>& remapped,
    int                 level)
{
    PngWrite w;
    w.fp = std::fopen(output_path.c_str(), "wb");
    if (!w.fp) return "png_compress: cannot open output: " + output_path;

    try {
        w.png_ptr  = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                              png_error_fn, png_warn_fn);
        if (!w.png_ptr) return "png_compress: png_create_write_struct failed";
        w.info_ptr = png_create_info_struct(w.png_ptr);
        if (!w.info_ptr) return "png_compress: png_create_info_struct failed";

        png_init_io(w.png_ptr, w.fp);
        png_set_compression_level(w.png_ptr, level);

        png_set_IHDR(w.png_ptr, w.info_ptr,
                     (png_uint_32)width, (png_uint_32)height,
                     8, PNG_COLOR_TYPE_PALETTE,
                     PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_BASE,
                     PNG_FILTER_TYPE_BASE);

        // Set palette and transparency (alpha in tRNS chunk).
        int n = (int)palette->count;
        std::vector<png_color> pal(n);
        std::vector<uint8_t>   trans(n);
        for (int i = 0; i < n; ++i) {
            pal[i].red   = palette->entries[i].r;
            pal[i].green = palette->entries[i].g;
            pal[i].blue  = palette->entries[i].b;
            trans[i]     = palette->entries[i].a;
        }
        png_set_PLTE(w.png_ptr, w.info_ptr, pal.data(), n);
        png_set_tRNS(w.png_ptr, w.info_ptr, trans.data(), n, nullptr);

        png_write_info(w.png_ptr, w.info_ptr);

        // Write rows.
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = remapped.data() + y * width;
            png_write_row(w.png_ptr, const_cast<png_bytep>(row));
        }
        png_write_end(w.png_ptr, w.info_ptr);
    }
    catch (const std::exception& ex) {
        return ex.what();
    }
    return {};
}

// ── Write RGBA PNG ────────────────────────────────────────────────────────────

static std::string write_rgba_png(
    const std::string& output_path,
    int width, int height,
    const uint8_t* rgba,
    int level)
{
    PngWrite w;
    w.fp = std::fopen(output_path.c_str(), "wb");
    if (!w.fp) return "png_compress: cannot open output: " + output_path;

    try {
        w.png_ptr  = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                              png_error_fn, png_warn_fn);
        if (!w.png_ptr) return "png_compress: png_create_write_struct failed";
        w.info_ptr = png_create_info_struct(w.png_ptr);
        if (!w.info_ptr) return "png_compress: png_create_info_struct failed";

        png_init_io(w.png_ptr, w.fp);
        png_set_compression_level(w.png_ptr, level);
        png_set_IHDR(w.png_ptr, w.info_ptr,
                     (png_uint_32)width, (png_uint_32)height,
                     8, PNG_COLOR_TYPE_RGBA,
                     PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_BASE,
                     PNG_FILTER_TYPE_BASE);
        png_write_info(w.png_ptr, w.info_ptr);

        for (int y = 0; y < height; ++y) {
            const uint8_t* row = rgba + y * width * 4;
            png_write_row(w.png_ptr, const_cast<png_bytep>(row));
        }
        png_write_end(w.png_ptr, w.info_ptr);
    }
    catch (const std::exception& ex) {
        return ex.what();
    }
    return {};
}

// ── Load image to RGBA via vips ───────────────────────────────────────────────

struct VipsGuard {
    VipsImage* img = nullptr;
    void*      buf = nullptr;
    ~VipsGuard() {
        if (img) g_object_unref(img);
        if (buf) g_free(buf);
    }
};

static std::string load_rgba(const std::string& path,
                               VipsGuard& g,
                               int& w, int& h)
{
    g.img = vips_image_new_from_file(path.c_str(), nullptr);
    if (!g.img) return "png_compress: vips cannot load: " + path;

    // Ensure RGBA.
    VipsImage* out = nullptr;
    if (vips_flatten(g.img, &out, nullptr) == 0) {
        g_object_unref(g.img); g.img = out;
    }
    if (vips_image_get_bands(g.img) == 3) {
        // Add alpha.
        VipsImage* alpha = nullptr;
        if (vips_bandjoin_const1(g.img, &alpha, 255, nullptr) == 0) {
            g_object_unref(g.img); g.img = alpha;
        }
    }

    w = vips_image_get_width(g.img);
    h = vips_image_get_height(g.img);
    if (w <= 0 || h <= 0) return "png_compress: empty image";

    // Get raw pixels (the buffer is valid as long as img is alive).
    g.buf = (void*)vips_image_get_data(g.img);
    if (!g.buf) return "png_compress: vips_image_get_data failed";
    return {};
}

} // namespace

// ── Public API ─────────────────────────────────────────────────────────────────

std::string png_compress(const std::string& input_path,
                          const std::string& output_path,
                          const PngCompressOptions& opts)
{
    VipsGuard g;
    int w = 0, h = 0;
    std::string err = load_rgba(input_path, g, w, h);
    if (!err.empty()) return err;

    const auto* rgba = static_cast<const uint8_t*>(g.buf);

    std::string final_path = output_path;

#ifdef FEATURE_PNG_ZOPFLI
    // If zopfli is requested, write to a temp file first, then re-compress.
    const bool use_zopfli = opts.use_zopfli;
    std::string temp_path;
    if (use_zopfli) {
        namespace fs = std::filesystem;
        temp_path  = output_path + ".zoptmp.png";
        final_path = temp_path;
    }
#endif

    if (opts.quantize) {
        // ── Path 1: palette quantisation via libimagequant ────────────────────
        liq_attr* attr = liq_attr_create();
        if (!attr) return "png_compress: liq_attr_create failed";

        liq_set_max_colors(attr, std::max(8, std::min(256, opts.quantize_colors)));
        int qmin = std::max(0, opts.quantize_quality - 15);
        int qmax = std::min(100, opts.quantize_quality);
        liq_set_quality(attr, qmin, qmax);

        liq_image* img = liq_image_create_rgba(attr, rgba, w, h, 0);
        if (!img) { liq_attr_destroy(attr); return "png_compress: liq_image_create_rgba failed"; }

        liq_result* result = nullptr;
        liq_error lerr = liq_quantize_image(attr, img, &result);
        if (lerr != LIQ_OK || !result) {
            liq_image_destroy(img);
            liq_attr_destroy(attr);
            return std::string("png_compress: quantisation failed (liq error ") +
                   std::to_string((int)lerr) + ")";
        }

        std::vector<uint8_t> remapped((size_t)w * h);
        liq_write_remapped_image(result, img, remapped.data(), remapped.size());
        const liq_palette* pal = liq_get_palette(result);

        err = write_indexed_png(final_path, w, h, pal, remapped, opts.libpng_level);

        liq_result_destroy(result);
        liq_image_destroy(img);
        liq_attr_destroy(attr);
    } else {
        // ── Path 2: lossless RGBA PNG with configurable compression level ──────
        err = write_rgba_png(final_path, w, h, rgba, opts.libpng_level);
    }

    if (!err.empty()) return err;

#ifdef FEATURE_PNG_ZOPFLI
    if (use_zopfli) {
        // Re-compress the temp PNG with zopfli then write to the real output.
        ZopfliPNGOptions zopts;
        ZopfliPNGSetDefaults(&zopts);
        zopts.num_iterations      = opts.zopfli_iter;
        zopts.num_iterations_large = std::max(1, opts.zopfli_iter / 3);

        // Read temp file.
        std::vector<unsigned char> in_buf;
        {
            FILE* f = std::fopen(temp_path.c_str(), "rb");
            if (!f) {
                std::filesystem::remove(temp_path);
                return "png_compress: cannot read zopfli temp: " + temp_path;
            }
            std::fseek(f, 0, SEEK_END);
            in_buf.resize((size_t)std::ftell(f));
            std::fseek(f, 0, SEEK_SET);
            (void)std::fread(in_buf.data(), 1, in_buf.size(), f);
            std::fclose(f);
        }
        std::filesystem::remove(temp_path);

        std::vector<unsigned char> out_buf;
        int zopfli_ok = ZopfliPNGOptimize(in_buf, zopts, false, &out_buf);
        if (zopfli_ok != 0) return "png_compress: ZopfliPNGOptimize failed";

        FILE* fout = std::fopen(output_path.c_str(), "wb");
        if (!fout) return "png_compress: cannot write zopfli output: " + output_path;
        std::fwrite(out_buf.data(), 1, out_buf.size(), fout);
        std::fclose(fout);
    }
#endif

    return {};
}

std::string png_compress_inplace(const std::string& path,
                                  const PngCompressOptions& opts)
{
    namespace fs = std::filesystem;
    const std::string tmp = path + ".pctmp.png";
    std::string err = png_compress(path, tmp, opts);
    if (!err.empty()) { fs::remove(tmp); return err; }
    // Atomic replace.
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp); return "png_compress: rename failed: " + ec.message(); }
    return {};
}

} // namespace media
#endif // FEATURE_PNG_COMPRESSOR
