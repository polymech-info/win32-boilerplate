#include "resize.hpp"

#include "cache.hpp"
#include "glob_paths.hpp"
#include "url_fetch.hpp"

#include <vips/vips.h>
#include <vips/operation.h> // vips_cache_drop_all

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <random>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

/**
    @link:https://www.libvips.org/API/current/func.threadpool_run.html
    
*/

namespace fs = std::filesystem;

namespace media {

namespace {

std::once_flag g_vips_init;

void ensure_vips() {
    std::call_once(g_vips_init, []() {
        if (vips_init("media-img"))
            std::abort();
    });
}

std::string vips_err() {
    const char *buf = vips_error_buffer();
    std::string s = buf ? buf : "vips error";
    vips_error_clear();
    return s;
}

void to_lower(std::string &s) {
    for (char &c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
}

VipsKernel parse_kernel(const std::string &k) {
    std::string x = k;
    to_lower(x);
    if (x == "nearest")
        return VIPS_KERNEL_NEAREST;
    if (x == "cubic")
        return VIPS_KERNEL_CUBIC;
    if (x == "mitchell")
        return VIPS_KERNEL_MITCHELL;
    if (x == "lanczos2")
        return VIPS_KERNEL_LANCZOS2;
    return VIPS_KERNEL_LANCZOS3;
}

VipsInteresting parse_interesting(const std::string &p) {
    std::string x = p;
    to_lower(x);
    if (x == "attention" || x == "entropy")
        return VIPS_INTERESTING_ATTENTION;
    if (x == "low" || x == "left" || x == "top")
        return VIPS_INTERESTING_LOW;
    if (x == "high" || x == "right" || x == "bottom")
        return VIPS_INTERESTING_HIGH;
    return VIPS_INTERESTING_CENTRE;
}

bool parse_rgb_background(const std::string &bg, double out[3]) {
    if (bg.empty() || bg[0] != '#' || bg.size() < 4)
        return false;
    try {
        if (bg.size() == 7) {
            out[0] = static_cast<double>(std::stoi(bg.substr(1, 2), nullptr, 16));
            out[1] = static_cast<double>(std::stoi(bg.substr(3, 2), nullptr, 16));
            out[2] = static_cast<double>(std::stoi(bg.substr(5, 2), nullptr, 16));
            return true;
        }
        if (bg.size() == 4) {
            out[0] = static_cast<double>(std::stoi(bg.substr(1, 1), nullptr, 16) * 17);
            out[1] = static_cast<double>(std::stoi(bg.substr(2, 1), nullptr, 16) * 17);
            out[2] = static_cast<double>(std::stoi(bg.substr(3, 1), nullptr, 16) * 17);
            return true;
        }
    } catch (...) {
    }
    return false;
}

std::string output_format(const std::string &path, const ResizeOptions &opt) {
    std::string fmt = opt.format;
    if (fmt.empty()) {
        const auto dot = path.rfind('.');
        if (dot != std::string::npos)
            fmt = path.substr(dot + 1);
    }
    to_lower(fmt);
    return fmt;
}

/**
 * JPEG has no alpha. vips_jpegsave on RGBA / grey+alpha can error or, on some Windows
 * stacks, fault inside libjpeg. Flatten to opt.background (default white) first.
 */
static bool flatten_for_jpeg_if_alpha(VipsImage *in, const ResizeOptions &opt, VipsImage **out_flat,
                                     std::string &err_out)
{
    *out_flat = nullptr;
    if (!vips_image_hasalpha(in))
        return true;
    double             bg[3] = {255, 255, 255};
    (void)parse_rgb_background(opt.background, bg);
    VipsArrayDouble *bg_a = vips_array_double_newv(3, bg[0], bg[1], bg[2]);
    if (vips_flatten(in, out_flat, "background", bg_a, NULL)) {
        vips_area_unref(VIPS_AREA(bg_a));
        err_out = vips_err();
        return false;
    }
    vips_area_unref(VIPS_AREA(bg_a));
    return true;
}

bool save_image(VipsImage *in, const std::string &path, const ResizeOptions &opt, std::string &err_out) {
    const std::string fmt = output_format(path, opt);
    const gboolean strip = opt.strip_metadata ? TRUE : FALSE;

    if (fmt == "jpg" || fmt == "jpeg") {
        VipsImage *flat = nullptr;
        if (!flatten_for_jpeg_if_alpha(in, opt, &flat, err_out))
            return false;
        VipsImage *work = flat ? flat : in;
        if (vips_jpegsave(work, path.c_str(), "Q", opt.quality, "optimize_coding", TRUE, "strip", strip, NULL)) {
            if (flat) g_object_unref(flat);
            goto fail;
        }
        if (flat) g_object_unref(flat);
        return true;
    }
    if (fmt == "png") {
        if (vips_pngsave(in, path.c_str(), "compression", opt.png_compression, NULL))
            goto fail;
        return true;
    }
    if (fmt == "webp") {
        if (vips_webpsave(in, path.c_str(), "Q", opt.quality, "strip", strip, NULL))
            goto fail;
        return true;
    }
    if (fmt == "tif" || fmt == "tiff") {
        if (vips_tiffsave(in, path.c_str(), NULL))
            goto fail;
        return true;
    }
    if (fmt == "avif" || fmt == "heic" || fmt == "heif") {
        if (vips_image_write_to_file(in, path.c_str(), "Q", opt.quality, NULL))
            goto fail;
        return true;
    }

    if (vips_image_write_to_file(in, path.c_str(), NULL))
        goto fail;
    return true;
fail:
    err_out = vips_err();
    return false;
}

bool apply_user_rotate(VipsImage *in, VipsImage **out, int deg, std::string &err_out) {
    deg = ((deg % 360) + 360) % 360;
    if (deg == 0) {
        *out = in;
        g_object_ref(in);
        return true;
    }
    VipsAngle a = VIPS_ANGLE_D0;
    if (deg == 90)
        a = VIPS_ANGLE_D90;
    else if (deg == 180)
        a = VIPS_ANGLE_D180;
    else if (deg == 270)
        a = VIPS_ANGLE_D270;
    else {
        err_out = "rotate must be 0, 90, 180, or 270";
        return false;
    }
    if (vips_rot(in, out, a, NULL)) {
        err_out = vips_err();
        return false;
    }
    return true;
}

fs::path unique_temp_download_path(std::string &err_out) {
    err_out.clear();
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) {
        err_out = "temp directory: " + ec.message();
        return {};
    }
    static std::mutex mtx;
    static std::mt19937_64 rng{std::random_device{}()};
    std::lock_guard<std::mutex> lock(mtx);
    std::uniform_int_distribution<uint64_t> dist;
    for (int i = 0; i < 32; ++i) {
        fs::path p = base / ("media-img-url-" + std::to_string(dist(rng)) + ".dl");
        if (!fs::exists(p, ec))
            return p;
    }
    err_out = "could not allocate temp download path";
    return {};
}

/// Random temp path in the same directory as the final output (same volume for rename on Windows).
static fs::path unique_temp_beside_output(const fs::path &out_fs, std::string &err_out) {
    err_out.clear();
    std::error_code ec;
    fs::path dir = out_fs.parent_path();
    if (dir.empty()) {
        err_out = "in-place: output has no parent directory";
        return {};
    }
    const std::string stem = out_fs.stem().string();
    const std::string ext  = out_fs.extension().string();
    static std::mutex          mtx;
    static std::mt19937_64     rng{std::random_device{}()};
    std::lock_guard<std::mutex> lock(mtx);
    std::uniform_int_distribution<uint64_t> dist;
    for (int i = 0; i < 64; ++i) {
        fs::path p = dir / (stem + ".pm-inplace-" + std::to_string(dist(rng)) + ext);
        if (!fs::exists(p, ec))
            return p;
    }
    err_out = "could not allocate unique temp name beside output";
    return {};
}

/**
 * After vips wrote to a temp file, commit over the final path without fs::copy_file, which
 * on Windows can hit ERROR_USER_MAPPED_FILE (0x4C4) if the input loader still has the
 * destination path mapped. Prefer MoveFileEx, then remove+rename, then copy as last resort.
 */
static bool commit_inplace_output(const fs::path &tmp, const fs::path &dest, std::string &err_out) {
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
        err_out = "replace in-place output: " + ec.message();
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return false;
    }
    fs::remove(tmp, ec);
    return true;
}

/** True when input and output refer to the same file (in-place overwrite). */
static bool paths_are_same_resize_target(const std::string &input_path, const std::string &output_path) {
    std::error_code ec;
    fs::path a = fs::absolute(fs::path(input_path), ec);
    fs::path b = fs::absolute(fs::path(output_path), ec);
    if (fs::exists(a, ec) && fs::exists(b, ec)) {
        if (fs::equivalent(a, b, ec))
            return true;
    }
    ec.clear();
    return fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
}

} // namespace

bool resize_batch(const std::string &input_spec, const std::string &output_spec, const ResizeOptions &opt,
                  std::string &err_out, ResizeBatchResult *out_stats,
                  const std::function<void(std::size_t, std::size_t)> &progress) {
    if (out_stats) {
        out_stats->count = 0;
        out_stats->outputs.clear();
    }
    std::string pair_err;
    auto jobs = pair_resize_paths(input_spec, output_spec, pair_err,
                                  output_spec.empty() ? &opt.format : nullptr,
                                  output_spec.empty() ? &opt.output_stem_suffix : nullptr);
    if (!pair_err.empty()) {
        err_out = pair_err;
        return false;
    }
    if (jobs.empty()) {
        err_out = "no resize jobs";
        return false;
    }
    const std::size_t n = jobs.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto &job = jobs[i];
        if (progress)
            progress(i, n);
        std::error_code ec;
        std::filesystem::create_directories(job.second.parent_path(), ec);
        std::string one_err;
        if (!resize_file(job.first, job.second.string(), opt, one_err)) {
            err_out = job.first + ": " + one_err;
            return false;
        }
        if (out_stats) {
            ++out_stats->count;
            out_stats->outputs.push_back(job.second.string());
        }
    }
    return true;
}

void apply_resize_options_from_json(const nlohmann::json &j, ResizeOptions &opt) {
    auto num = [&](const char *key, int &dest) {
        if (!j.contains(key) || j[key].is_null())
            return;
        if (j[key].is_number_integer())
            dest = j[key].get<int>();
    };
    auto num_or_bool = [&](const char *key, bool &dest) {
        if (!j.contains(key) || j[key].is_null())
            return;
        if (j[key].is_boolean())
            dest = j[key].get<bool>();
        else if (j[key].is_number_integer())
            dest = j[key].get<int>() != 0;
    };
    auto str = [&](const char *key, std::string &dest) {
        if (!j.contains(key) || !j[key].is_string())
            return;
        dest = j[key].get<std::string>();
    };

    num("max_width", opt.max_width);
    num("max_height", opt.max_height);
    str("format", opt.format);
    str("fit", opt.fit);
    str("position", opt.position);
    str("kernel", opt.kernel);
    num("quality", opt.quality);
    num("png_compression", opt.png_compression);
    num_or_bool("without_enlargement", opt.without_enlargement);
    num_or_bool("autorotate", opt.autorotate);
    num_or_bool("strip_metadata", opt.strip_metadata);
    num("rotate", opt.rotate);
    num_or_bool("flip", opt.flip);
    num_or_bool("flop", opt.flop);
    str("background", opt.background);
    num_or_bool("cache", opt.cache_enabled);
    str("cache_dir", opt.cache_dir);
    num("url_timeout_sec", opt.url_timeout_sec);
    num("url_max_redirects", opt.url_max_redirects);
}

void apply_cache_defaults_from_json(const nlohmann::json &j, ResizeOptions &opt,
                                    const CacheServerDefaults &defaults) {
    if (!j.contains("cache") || j["cache"].is_null())
        opt.cache_enabled = defaults.enabled;
    if (!j.contains("cache_dir") || !j["cache_dir"].is_string() || j["cache_dir"].get<std::string>().empty())
        opt.cache_dir = defaults.cache_dir;
}

static bool resize_file_vips(const std::string &input_path, const std::string &output_path, ResizeOptions opt,
                             std::string &err_out) {
    VipsImage *base = vips_image_new_from_file(input_path.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, NULL);
    if (!base) {
        err_out = vips_err();
        return false;
    }

    VipsImage *cur = base;
    VipsImage *next = nullptr;

    if (opt.autorotate) {
        if (vips_autorot(cur, &next, NULL)) {
            err_out = vips_err();
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        cur = next;
    }

    if (opt.rotate != 0) {
        if (!apply_user_rotate(cur, &next, opt.rotate, err_out)) {
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        cur = next;
    }

    if (opt.flip) {
        if (vips_flip(cur, &next, VIPS_DIRECTION_VERTICAL, NULL)) {
            err_out = vips_err();
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        cur = next;
    }
    if (opt.flop) {
        if (vips_flip(cur, &next, VIPS_DIRECTION_HORIZONTAL, NULL)) {
            err_out = vips_err();
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        cur = next;
    }

    const int iw = vips_image_get_width(cur);
    const int ih = vips_image_get_height(cur);
    const int W = opt.max_width;
    const int H = opt.max_height;
    const VipsKernel vk = parse_kernel(opt.kernel);
    const VipsInteresting vi = parse_interesting(opt.position);

    const bool want_resize = (W > 0 || H > 0) || opt.fit == "fill";

    if (!want_resize) {
        if (!save_image(cur, output_path, opt, err_out)) {
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        return true;
    }

    if (opt.fit == "fill") {
        if (W <= 0 || H <= 0) {
            err_out = "fill fit requires both max_width and max_height";
            g_object_unref(cur);
            return false;
        }
        const double sx = static_cast<double>(W) / static_cast<double>(iw);
        const double sy = static_cast<double>(H) / static_cast<double>(ih);
        if (vips_resize(cur, &next, sx, "vscale", sy, "kernel", vk, NULL)) {
            err_out = vips_err();
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        cur = next;
    } else if (opt.fit == "inside" || opt.fit == "contain") {
        if (W <= 0 && H <= 0) {
            err_out = "inside/contain requires max_width and/or max_height";
            g_object_unref(cur);
            return false;
        }
        double scale = 1.0;
        if (W > 0 && H > 0) {
            scale = std::min(static_cast<double>(W) / static_cast<double>(iw),
                             static_cast<double>(H) / static_cast<double>(ih));
        } else if (W > 0) {
            scale = static_cast<double>(W) / static_cast<double>(iw);
        } else {
            scale = static_cast<double>(H) / static_cast<double>(ih);
        }
        if (opt.without_enlargement)
            scale = std::min(scale, 1.0);
        if (scale <= 0.0) {
            err_out = "invalid geometry";
            g_object_unref(cur);
            return false;
        }
        if (vips_resize(cur, &next, scale, "kernel", vk, NULL)) {
            err_out = vips_err();
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        cur = next;

        if (opt.fit == "contain" && W > 0 && H > 0) {
            const int cw = vips_image_get_width(cur);
            const int ch = vips_image_get_height(cur);
            if (cw < W || ch < H) {
                double bg[3] = {255.0, 255.0, 255.0};
                if (!parse_rgb_background(opt.background, bg))
                    parse_rgb_background("#ffffff", bg);
                const int left = (W - cw) / 2;
                const int top = (H - ch) / 2;
                VipsArrayDouble *bg_a = vips_array_double_newv(3, bg[0], bg[1], bg[2]);
                const int e = vips_embed(cur, &next, left, top, W, H, "extend", VIPS_EXTEND_BACKGROUND,
                                         "background", bg_a, NULL);
                vips_area_unref(VIPS_AREA(bg_a));
                if (e) {
                    err_out = vips_err();
                    g_object_unref(cur);
                    return false;
                }
                g_object_unref(cur);
                cur = next;
            }
        }
    } else if (opt.fit == "cover") {
        if (W <= 0 || H <= 0) {
            err_out = "cover fit requires max_width and max_height";
            g_object_unref(cur);
            return false;
        }
        const double sx = static_cast<double>(W) / static_cast<double>(iw);
        const double sy = static_cast<double>(H) / static_cast<double>(ih);
        const double sc = std::max(sx, sy);
        if (vips_resize(cur, &next, sc, "kernel", vk, NULL)) {
            err_out = vips_err();
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        cur = next;
        const int rw = vips_image_get_width(cur);
        const int rh = vips_image_get_height(cur);
        const int left = std::max(0, (rw - W) / 2);
        const int top = std::max(0, (rh - H) / 2);
        if (vi == VIPS_INTERESTING_ATTENTION) {
            if (vips_smartcrop(cur, &next, W, H, "interesting", VIPS_INTERESTING_ATTENTION, NULL)) {
                err_out = vips_err();
                g_object_unref(cur);
                return false;
            }
        } else {
            if (vips_extract_area(cur, &next, left, top, W, H, NULL)) {
                err_out = vips_err();
                g_object_unref(cur);
                return false;
            }
        }
        g_object_unref(cur);
        cur = next;
    } else if (opt.fit == "outside") {
        if (W <= 0 || H <= 0) {
            err_out = "outside fit requires max_width and max_height";
            g_object_unref(cur);
            return false;
        }
        const double sx = static_cast<double>(W) / static_cast<double>(iw);
        const double sy = static_cast<double>(H) / static_cast<double>(ih);
        double sc = std::max(sx, sy);
        if (opt.without_enlargement)
            sc = std::min(sc, 1.0);
        if (vips_resize(cur, &next, sc, "kernel", vk, NULL)) {
            err_out = vips_err();
            g_object_unref(cur);
            return false;
        }
        g_object_unref(cur);
        cur = next;
    } else {
        err_out = std::string("unknown fit: ") + opt.fit;
        g_object_unref(cur);
        return false;
    }

    if (!save_image(cur, output_path, opt, err_out)) {
        g_object_unref(cur);
        return false;
    }
    g_object_unref(cur);
    return true;
}

bool resize_file(const std::string &input_path, const std::string &output_path, const ResizeOptions &opt_in,
                 std::string &err_out) {
    ResizeOptions opt = opt_in;
    to_lower(opt.fit);
    to_lower(opt.kernel);
    to_lower(opt.position);

    if (opt.cache_enabled) {
        std::string ce;
        if (try_copy_from_cache(input_path, output_path, opt, ce))
            return true;
        if (!ce.empty()) {
            err_out = ce;
            return false;
        }
    }

    ensure_vips();
    vips_error_clear();

    if (is_http_url(input_path)) {
        std::string tmp_err;
        fs::path tmp = unique_temp_download_path(tmp_err);
        if (tmp.empty()) {
            err_out = tmp_err;
            return false;
        }
        if (!fetch_url_to_file(input_path, tmp, opt.url_timeout_sec, opt.url_max_redirects, err_out)) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
        const bool ok = resize_file_vips(tmp.string(), output_path, opt, err_out);
        std::error_code ec;
        fs::remove(tmp, ec);
        if (ok && opt.cache_enabled)
            store_in_cache(input_path, output_path, opt);
        return ok;
    }

    if (paths_are_same_resize_target(input_path, output_path)) {
        std::string  tmp_err;
        const fs::path out_fs(output_path);
        // Temp beside the final file: same volume as the source so replace is a rename, not a cross-drive copy.
        fs::path tmp_out = unique_temp_beside_output(out_fs, tmp_err);
        if (tmp_out.empty()) {
            err_out = std::move(tmp_err);
            return false;
        }
        const bool ok = resize_file_vips(input_path, tmp_out.string(), opt, err_out);
        if (!ok) {
            std::error_code ec;
            fs::remove(tmp_out, ec);
            return false;
        }
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(out_fs, ec);
        if (ec)
            canon = fs::absolute(out_fs);
        if (!commit_inplace_output(tmp_out, canon, err_out)) {
            return false;
        }
        if (opt.cache_enabled)
            store_in_cache(input_path, output_path, opt);
        return true;
    }

    const bool ok = resize_file_vips(input_path, output_path, opt, err_out);
    if (ok && opt.cache_enabled)
        store_in_cache(input_path, output_path, opt);
    return ok;
}

// ── Buffer-only variant (zero-fs) ────────────────────────────────────────────

namespace {

// Save a VipsImage to a memory buffer using the encoder selected by `fmt_lower`.
// On success, `out` holds the encoded bytes and `mime_out` is set; on failure
// `err_out` is populated and `out` is left empty.
bool save_to_buffer(VipsImage *cur, const std::string &fmt_lower, const ResizeOptions &opt,
                    std::string &mime_out, std::string &out, std::string &err_out) {
    void *buf = nullptr;
    size_t len = 0;
    int rc = -1;
    const gboolean strip = opt.strip_metadata ? TRUE : FALSE;

    if (fmt_lower == "jpg" || fmt_lower == "jpeg" || fmt_lower.empty()) {
        VipsImage *flat = nullptr;
        if (!flatten_for_jpeg_if_alpha(cur, opt, &flat, err_out))
            return false;
        VipsImage *work = flat ? flat : cur;
        rc = vips_jpegsave_buffer(work, &buf, &len, "Q", opt.quality, "optimize_coding", TRUE, "strip", strip,
                                  NULL);
        if (flat) g_object_unref(flat);
        mime_out = "image/jpeg";
    } else if (fmt_lower == "png") {
        rc = vips_pngsave_buffer(cur, &buf, &len, "compression", opt.png_compression,
                                 "strip", strip, NULL);
        mime_out = "image/png";
    } else if (fmt_lower == "webp") {
        rc = vips_webpsave_buffer(cur, &buf, &len, "Q", opt.quality, "strip", strip, NULL);
        mime_out = "image/webp";
    } else {
        err_out = "resize_buffer: unsupported output format '" + fmt_lower +
                  "' (supported: jpeg, png, webp)";
        return false;
    }

    if (rc != 0 || !buf) {
        if (buf) g_free(buf);
        err_out = vips_err();
        return false;
    }
    out.assign(static_cast<const char *>(buf), len);
    g_free(buf);
    return true;
}

} // namespace

ResizeBufferResult resize_buffer(const void *in_data, std::size_t in_size, const ResizeOptions &opt_in) {
    ResizeBufferResult res;
    if (!in_data || in_size == 0) {
        res.error = "resize_buffer: empty input";
        return res;
    }

    ResizeOptions opt = opt_in;
    to_lower(opt.fit);
    to_lower(opt.kernel);
    to_lower(opt.position);

    ensure_vips();

    VipsImage *base = vips_image_new_from_buffer(in_data, in_size, "", NULL);
    if (!base) {
        res.error = vips_err();
        return res;
    }

    VipsImage *cur = base;
    VipsImage *next = nullptr;

    // ── orientation pre-processing (mirrors resize_file_vips) ───────────────
    if (opt.autorotate) {
        if (vips_autorot(cur, &next, NULL)) { res.error = vips_err(); g_object_unref(cur); return res; }
        g_object_unref(cur); cur = next;
    }
    if (opt.rotate != 0) {
        std::string e;
        if (!apply_user_rotate(cur, &next, opt.rotate, e)) { res.error = e; g_object_unref(cur); return res; }
        g_object_unref(cur); cur = next;
    }
    if (opt.flip) {
        if (vips_flip(cur, &next, VIPS_DIRECTION_VERTICAL, NULL)) { res.error = vips_err(); g_object_unref(cur); return res; }
        g_object_unref(cur); cur = next;
    }
    if (opt.flop) {
        if (vips_flip(cur, &next, VIPS_DIRECTION_HORIZONTAL, NULL)) { res.error = vips_err(); g_object_unref(cur); return res; }
        g_object_unref(cur); cur = next;
    }

    const int iw = vips_image_get_width(cur);
    const int ih = vips_image_get_height(cur);
    const int W = opt.max_width;
    const int H = opt.max_height;
    const VipsKernel vk = parse_kernel(opt.kernel);
    const VipsInteresting vi = parse_interesting(opt.position);
    const bool want_resize = (W > 0 || H > 0) || opt.fit == "fill";

    if (want_resize) {
        if (opt.fit == "fill") {
            if (W <= 0 || H <= 0) {
                res.error = "fill fit requires both max_width and max_height";
                g_object_unref(cur); return res;
            }
            const double sx = static_cast<double>(W) / iw;
            const double sy = static_cast<double>(H) / ih;
            if (vips_resize(cur, &next, sx, "vscale", sy, "kernel", vk, NULL)) {
                res.error = vips_err(); g_object_unref(cur); return res;
            }
            g_object_unref(cur); cur = next;
        } else if (opt.fit == "inside" || opt.fit == "contain" || opt.fit.empty()) {
            if (W <= 0 && H <= 0) {
                res.error = "inside/contain requires max_width and/or max_height";
                g_object_unref(cur); return res;
            }
            double scale = 1.0;
            if (W > 0 && H > 0) scale = std::min(static_cast<double>(W) / iw, static_cast<double>(H) / ih);
            else if (W > 0)    scale = static_cast<double>(W) / iw;
            else               scale = static_cast<double>(H) / ih;
            if (opt.without_enlargement) scale = std::min(scale, 1.0);
            if (scale <= 0.0) { res.error = "invalid geometry"; g_object_unref(cur); return res; }
            if (vips_resize(cur, &next, scale, "kernel", vk, NULL)) {
                res.error = vips_err(); g_object_unref(cur); return res;
            }
            g_object_unref(cur); cur = next;

            if (opt.fit == "contain" && W > 0 && H > 0) {
                const int cw = vips_image_get_width(cur);
                const int ch = vips_image_get_height(cur);
                if (cw < W || ch < H) {
                    double bg[3] = {255.0, 255.0, 255.0};
                    if (!parse_rgb_background(opt.background, bg)) parse_rgb_background("#ffffff", bg);
                    const int left = (W - cw) / 2;
                    const int top  = (H - ch) / 2;
                    VipsArrayDouble *bg_a = vips_array_double_newv(3, bg[0], bg[1], bg[2]);
                    const int e = vips_embed(cur, &next, left, top, W, H, "extend", VIPS_EXTEND_BACKGROUND,
                                             "background", bg_a, NULL);
                    vips_area_unref(VIPS_AREA(bg_a));
                    if (e) { res.error = vips_err(); g_object_unref(cur); return res; }
                    g_object_unref(cur); cur = next;
                }
            }
        } else if (opt.fit == "cover") {
            if (W <= 0 || H <= 0) {
                res.error = "cover fit requires max_width and max_height";
                g_object_unref(cur); return res;
            }
            const double sx = static_cast<double>(W) / iw;
            const double sy = static_cast<double>(H) / ih;
            if (vips_resize(cur, &next, std::max(sx, sy), "kernel", vk, NULL)) {
                res.error = vips_err(); g_object_unref(cur); return res;
            }
            g_object_unref(cur); cur = next;
            const int rw = vips_image_get_width(cur);
            const int rh = vips_image_get_height(cur);
            const int left = std::max(0, (rw - W) / 2);
            const int top  = std::max(0, (rh - H) / 2);
            if (vi == VIPS_INTERESTING_ATTENTION) {
                if (vips_smartcrop(cur, &next, W, H, "interesting", VIPS_INTERESTING_ATTENTION, NULL)) {
                    res.error = vips_err(); g_object_unref(cur); return res;
                }
            } else {
                if (vips_extract_area(cur, &next, left, top, W, H, NULL)) {
                    res.error = vips_err(); g_object_unref(cur); return res;
                }
            }
            g_object_unref(cur); cur = next;
        } else if (opt.fit == "outside") {
            if (W <= 0 || H <= 0) {
                res.error = "outside fit requires max_width and max_height";
                g_object_unref(cur); return res;
            }
            const double sx = static_cast<double>(W) / iw;
            const double sy = static_cast<double>(H) / ih;
            double sc = std::max(sx, sy);
            if (opt.without_enlargement) sc = std::min(sc, 1.0);
            if (vips_resize(cur, &next, sc, "kernel", vk, NULL)) {
                res.error = vips_err(); g_object_unref(cur); return res;
            }
            g_object_unref(cur); cur = next;
        } else {
            res.error = "unknown fit: " + opt.fit;
            g_object_unref(cur); return res;
        }
    }

    std::string fmt = opt.format;
    to_lower(fmt);
    std::string err;
    if (!save_to_buffer(cur, fmt, opt, res.mime, res.bytes, err)) {
        res.error = err;
        g_object_unref(cur);
        return res;
    }
    g_object_unref(cur);
    res.ok = true;
    return res;
}

} // namespace media
