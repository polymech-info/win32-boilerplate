#include "llm_jpeg_from_path.hpp"

#include <vips/vips.h>

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace media {
namespace {

std::once_flag g_vips_for_llm;

void ensure_vips_llm() {
    std::call_once(g_vips_for_llm, []() {
        if (vips_init("llm_jpeg")) std::abort();
    });
}

std::string vips_err() {
    const char* b = vips_error_buffer();
    std::string s = b ? b : "vips error";
    vips_error_clear();
    return s;
}

} // namespace

bool path_to_jpeg_for_llm(const std::string& path, int max_edge_px,
                          std::vector<uint8_t>& out_bytes,
                          int& orig_w, int& orig_h, int& out_w, int& out_h,
                          std::string& err) {
    ensure_vips_llm();
    if (max_edge_px <= 0) {
        err = "path_to_jpeg_for_llm: max_edge_px must be positive";
        return false;
    }
    VipsImage* in = vips_image_new_from_file(
        path.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, NULL);
    if (!in) {
        err = vips_err();
        return false;
    }
    orig_w = vips_image_get_width(in);
    orig_h = vips_image_get_height(in);

    VipsImage* thumb = nullptr;
    if (vips_thumbnail(path.c_str(), &thumb, max_edge_px, NULL)) {
        g_object_unref(in);
        err = vips_err();
        return false;
    }
    out_w = vips_image_get_width(thumb);
    out_h = vips_image_get_height(thumb);

    void*  buf = nullptr;
    size_t len = 0;
    if (vips_jpegsave_buffer(thumb, &buf, &len, "Q", 85, "strip", TRUE, NULL)) {
        g_object_unref(in);
        g_object_unref(thumb);
        err = vips_err();
        return false;
    }
    out_bytes.assign(static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf) + len);
    g_free(buf);
    g_object_unref(in);
    g_object_unref(thumb);
    return true;
}

} // namespace media
