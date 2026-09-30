// ThorVG — CPU raster of Tabler-style SVG (fill="currentColor") for toolbar DIBs.
// Tabler bytes: PhysFS mounts (prefer) loose tabler-icons/icons/filled, else assets.pfs,
// else dev tree; then direct file I/O from beside-exe, dev, and finally the baked path.
#include "stdafx.h"

#ifdef FEATURE_SVG_BUTTONS

#include "svg_raster.hpp"
#include "svg_paths.generated.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <physfs.h>
#include <thorvg.h>

#include "helpers/text_conv.hpp"

namespace {

std::once_flag s_tvgInit;
std::once_flag s_physfsInit;

void ensure_tvg()
{
    std::call_once(s_tvgInit, [] {
        tvg::Initializer::init(0);
    });
}

void replace_current_color(std::string& s, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    const std::string from = "currentColor";
    char            repl[20]{};
    (void)std::snprintf(repl, sizeof(repl), "#%02X%02X%02X", (unsigned)r, (unsigned)g, (unsigned)b);
    for (;;) {
        const size_t p = s.find(from);
        if (p == std::string::npos) break;
        s.replace(p, from.size(), repl);
    }
}

std::filesystem::path module_exe_dir()
{
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1u));
    if (n == 0u) return {};
    return std::filesystem::path(std::wstring(buf.data(), n)).parent_path();
}

static std::optional<std::filesystem::path> path_to_tabler_filled_source()
{
    try {
        const std::filesystem::path p(PM_TABLER_FILLED_DIR_W);
        if (std::filesystem::is_directory(p)) return p;
    } catch (...) {
    }
    return std::nullopt;
}

static std::optional<std::filesystem::path> dist_root_dir()
{
    const auto ex = module_exe_dir();
    if (ex.empty()) return std::nullopt;
    if (ex.filename() == L"win-x64")
        return ex.parent_path();
    return ex;
}

static std::optional<std::filesystem::path> path_tabler_filled_vendor()
{
    const auto root = dist_root_dir();
    if (!root) return std::nullopt;
    try {
        const std::filesystem::path p = *root / L"vendor" / L"tabler-icons" / L"icons" / L"filled";
        if (std::filesystem::is_directory(p)) return p;
    } catch (...) {
    }
    return std::nullopt;
}

static std::optional<std::filesystem::path> path_tabler_filled_beside_exe_legacy()
{
    const auto ex = module_exe_dir();
    if (ex.empty()) return std::nullopt;
    try {
        const std::filesystem::path p = ex / L"tabler-icons" / L"icons" / L"filled";
        if (std::filesystem::is_directory(p)) return p;
    } catch (...) {
    }
    return std::nullopt;
}

static std::optional<std::filesystem::path> path_assets_pfs_shared()
{
    const auto root = dist_root_dir();
    if (!root) return std::nullopt;
    try {
        const auto pfs = *root / L"shared" / L"assets.pfs";
        if (std::filesystem::is_regular_file(pfs)) return pfs;
    } catch (...) {
    }
    return std::nullopt;
}

/// Mount order: dist/vendor loose icons → dist/shared/assets.pfs → legacy beside-exe assets → dev source tree.
static std::optional<std::filesystem::path> first_physfs_mount_path()
{
    if (const auto vendor = path_tabler_filled_vendor()) return vendor;
    if (const auto shared = path_assets_pfs_shared()) return shared;
    if (const auto beside = path_tabler_filled_beside_exe_legacy()) return beside;
    const auto ex = module_exe_dir();
    if (!ex.empty()) {
        const auto pfs = ex / L"assets.pfs";
        try {
            if (std::filesystem::is_regular_file(pfs)) return pfs;
        } catch (...) {
        }
    }
    return path_to_tabler_filled_source();
}

static void physfs_shutdown_at_exit()
{
    if (PHYSFS_isInit())
        PHYSFS_deinit();
}

void ensure_physfs_and_mount()
{
    std::call_once(s_physfsInit, [] {
        if (!PHYSFS_init(nullptr)) {
            (void)OutputDebugStringA("pmui_svg: PHYSFS_init failed; SVG icons will use direct file I/O only.\n");
            return;
        }
        const auto root = first_physfs_mount_path();
        if (!root) {
            (void)OutputDebugStringA("pmui_svg: no dist/shared/assets.pfs, dist/vendor/tabler-icons, or dev icons/filled/; using direct file I/O for SVGs.\n");
            (void)PHYSFS_deinit();
            return;
        }
        const std::string mountU8 = pmui::wide_to_utf8(root->native());
        if (PHYSFS_mount(mountU8.c_str(), /*mountPoint*/ "/", /*append to path*/ 0) == 0) {
            const char* e = PHYSFS_getLastError();
            (void)OutputDebugStringA((std::string("pmui_svg: PHYSFS_mount failed for ") + mountU8
                                      + (e ? std::string(" | ") + e : std::string()) + "\n")
                                         .c_str());
            (void)PHYSFS_deinit();
            return;
        }
        (void)std::atexit(physfs_shutdown_at_exit);
    });
}

static bool physfs_read_file_to_string(const char* vpath, std::string& out)
{
    if (!vpath || vpath[0] == 0) return false;
    PHYSFS_File* f = PHYSFS_openRead(vpath);
    if (!f) return false;

    const PHYSFS_sint64 nlen = PHYSFS_fileLength(f);
    if (nlen <= 0) {
        (void)PHYSFS_close(f);
        return false;
    }
    out.assign(static_cast<size_t>(nlen), '\0');
    const PHYSFS_sint64 nread
        = PHYSFS_readBytes(f, out.data(), static_cast<PHYSFS_uint64>(static_cast<size_t>(nlen)));
    (void)PHYSFS_close(f);
    if (nread != nlen) {
        out.clear();
        return false;
    }
    return true;
}

static bool try_read_svg(const std::wstring& anyPathForBasename, std::string& out)
{
    ensure_physfs_and_mount();
    if (PHYSFS_isInit()) {
        const std::wstring         base   = std::filesystem::path(anyPathForBasename).filename().native();
        const std::string          baseU8 = pmui::wide_to_utf8(base);
        if (physfs_read_file_to_string(baseU8.c_str(), out) && !out.empty())
            return true;
    }

    const std::wstring         baseW = std::filesystem::path(anyPathForBasename).filename().native();
    {
        if (const auto beside = path_tabler_filled_vendor()) {
            try {
                const std::filesystem::path full = *beside / std::filesystem::path(baseW);
                std::ifstream                 ifs(full, std::ios::binary);
                if (ifs) {
                    out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    if (!out.empty()) return true;
                }
            } catch (...) {
            }
        }
    }
    {
        if (const auto legacy = path_tabler_filled_beside_exe_legacy()) {
            try {
                const std::filesystem::path full = *legacy / std::filesystem::path(baseW);
                std::ifstream                 ifs(full, std::ios::binary);
                if (ifs) {
                    out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    if (!out.empty()) return true;
                }
            } catch (...) {
            }
        }
    }
    {
        const auto dev = path_to_tabler_filled_source();
        if (dev) {
            try {
                const std::filesystem::path  full  = *dev / std::filesystem::path(baseW);
                std::ifstream                 ifs(full, std::ios::binary);
                if (ifs) {
                    out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    if (!out.empty()) return true;
                }
            } catch (...) {
            }
        }
    }

    try {
        std::ifstream ifs(std::filesystem::path(anyPathForBasename), std::ios::binary);
        if (!ifs) return false;
        out.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        return !out.empty();
    } catch (...) {
        return false;
    }
}

} // namespace

HBITMAP pmui_svg_rasterize_colored(const char* svgUtf8, size_t len, int dim, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (!svgUtf8 || len == 0 || dim < 4) return nullptr;
    ensure_tvg();

    std::string body(svgUtf8, len);
    replace_current_color(body, r, g, b);

    auto* pic = tvg::Picture::gen();
    if (!pic) return nullptr;
    if (pic->load(body.data(), (uint32_t)body.size(), "svg", nullptr, true) != tvg::Result::Success) {
        tvg::Paint::rel(pic);
        return nullptr;
    }
    pic->size((float)dim, (float)dim);

    auto* canvas = tvg::SwCanvas::gen();
    if (!canvas) {
        tvg::Paint::rel(pic);
        return nullptr;
    }
    const uint32_t stride = (uint32_t)dim;
    std::vector<uint32_t> buf((size_t)dim * (size_t)dim, 0);
    if (canvas->target(buf.data(), stride, (uint32_t)dim, (uint32_t)dim, tvg::ColorSpace::ABGR8888S)
        != tvg::Result::Success) {
        tvg::Paint::rel(pic);
        delete canvas;
        return nullptr;
    }
    if (canvas->add(pic) != tvg::Result::Success) {
        tvg::Paint::rel(pic);
        delete canvas;
        return nullptr;
    }
    (void)canvas->update();
    (void)canvas->draw(true);
    (void)canvas->sync();
    delete canvas;

    // ThorVG ABGR8888S: `_abgrJoin(r,g,b,a)` packs `a<<24|b<<16|g<<8|r` — on LE the low
    // byte is **red**, not blue. Windows 32bpp DIB + GDI+ `PixelFormat32bppPARGB` expect
    // premultiplied **BGRA** (B in the first byte). Mis-labelling R/B produced opaque
    // black tiles and wrong alpha when blitting.
    for (uint32_t& p : buf) {
        const std::uint8_t tr = static_cast<std::uint8_t>(p & 0xFFu);
        const std::uint8_t tg = static_cast<std::uint8_t>((p >> 8) & 0xFFu);
        const std::uint8_t tb = static_cast<std::uint8_t>((p >> 16) & 0xFFu);
        const std::uint8_t ta = static_cast<std::uint8_t>((p >> 24) & 0xFFu);
        if (ta == 0) {
            p = 0;
            continue;
        }
        const unsigned pb = (static_cast<unsigned>(tb) * ta + 127u) / 255u;
        const unsigned pg = (static_cast<unsigned>(tg) * ta + 127u) / 255u;
        const unsigned pr = (static_cast<unsigned>(tr) * ta + 127u) / 255u;
        p = static_cast<uint32_t>(pb) | (static_cast<uint32_t>(pg) << 8) | (static_cast<uint32_t>(pr) << 16)
            | (static_cast<uint32_t>(ta) << 24);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth    = dim;
    bmi.bmiHeader.biHeight   = -dim;
    bmi.bmiHeader.biPlanes   = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*  bits  = nullptr;
    HDC    sdc   = ::GetDC(nullptr);
    HBITMAP hb   = ::CreateDIBSection(sdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, sdc);
    if (!hb || !bits) {
        if (hb) ::DeleteObject(hb);
        return nullptr;
    }
    (void)memcpy_s(bits, (size_t)dim * (size_t)dim * 4u, buf.data(), (size_t)dim * (size_t)dim * 4u);
    return hb;
}

HBITMAP pmui_svg_rasterize_file_wide(const wchar_t* filePathW, int dim, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    if (!filePathW || filePathW[0] == 0) return nullptr;
    std::string data;
    if (!try_read_svg(filePathW, data)) return nullptr;
    return pmui_svg_rasterize_colored(data.data(), data.size(), dim, r, g, b);
}

#endif // FEATURE_SVG_BUTTONS
