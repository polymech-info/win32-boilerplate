#pragma once
//
// Central extension predicates and preview limits for pm-image (dialogs, queue scan,
// Explorer, FileViewer, ViewerWebPanel, chat context). Replaces the former split headers
// `helpers/image_exts.hpp`, `helpers/text_exts.hpp`, `helpers/viewer_3d_exts.hpp`,
// and `helpers/viewer_documents_exts.hpp`.
//

#include "features.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace pmui {

// ── Text / source / config ───────────────────────────────────────────────────
// Known text extensions (no exhaustive list; covers our docs + common code).

inline bool is_text_ext(std::wstring_view ext)
{
    return ext == L".txt"  || ext == L".md"   || ext == L".markdown" ||
           ext == L".log"  || ext == L".csv"  || ext == L".tsv"      ||
           ext == L".json" || ext == L".jsonl"|| ext == L".ndjson"   || ext == L".xblox" ||
           ext == L".xml"  || ext == L".html" || ext == L".htm"      ||
           ext == L".css"  || ext == L".scss" || ext == L".less"     ||
           ext == L".js"   || ext == L".jsx"  || ext == L".mjs"      || ext == L".cjs"  ||
           ext == L".ts"   || ext == L".tsx"  ||
           ext == L".cpp"  || ext == L".cxx"  || ext == L".cc"       || ext == L".c"    ||
           ext == L".h"    || ext == L".hpp"  || ext == L".hxx"      || ext == L".inl"  ||
           ext == L".py"   || ext == L".rb"   || ext == L".rs"       || ext == L".go"   ||
           ext == L".java" || ext == L".kt"   || ext == L".swift"    || ext == L".cs"   ||
           ext == L".sh"   || ext == L".bash" || ext == L".zsh"      ||
           ext == L".ps1"  || ext == L".cmd"  || ext == L".bat"      ||
           ext == L".yaml" || ext == L".yml"  || ext == L".toml"     || ext == L".ini"  ||
           ext == L".conf" || ext == L".cfg"  || ext == L".env"      ||
           ext == L".sql"  || ext == L".dot"  || ext == L".gv"       ||
           ext == L".svg"  || ext == L".rss"  || ext == L".atom"     ||
           ext == L".diff" || ext == L".patch"|| ext == L".gitignore" ||
           ext == L".gitattributes" || ext == L".editorconfig"       ||
           ext == L".cmake"|| ext == L".dockerfile";
}

/// Cheap binary probe: true if @p len bytes contain a NUL (unsafe for EDIT preview).
inline bool looks_like_binary(const std::uint8_t* data, std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i)
        if (data[i] == 0) return true;
    return false;
}

/// Regular file whose first bytes do not look binary (NUL sniff, up to 4 KiB).
inline bool regular_file_sniffs_as_plaintext(const std::wstring& path_w)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path_w);
    if (!fs::is_regular_file(p, ec) || ec)
        return false;
    const auto fname = p.filename().wstring();
    if (fname.empty() || fname == L"." || fname == L"..")
        return false;

    const std::uintmax_t sz = fs::file_size(p, ec);
    if (ec)
        return false;
    if (sz == 0)
        return true;

    std::ifstream ifs(p, std::ios::binary);
    if (!ifs)
        return false;

    constexpr std::size_t kCap = 4096;
    const std::size_t n = static_cast<std::size_t>((std::min)(sz, static_cast<std::uintmax_t>(kCap)));
    std::vector<std::uint8_t> buf(n);
    if (!ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n)))
        return false;
    return !looks_like_binary(buf.data(), buf.size());
}

/// Extensionless regular file: first bytes must not look binary.
inline bool extensionless_regular_file_sniffs_as_plaintext(const std::wstring& path_w)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path_w);
    if (!fs::is_regular_file(p, ec) || ec)
        return false;
    if (!p.extension().empty())
        return false;
    return regular_file_sniffs_as_plaintext(path_w);
}

// ── Standard web/screen images + camera RAW ───────────────────────────────────

inline bool is_standard_image_ext(std::wstring_view ext)
{
    return ext == L".jpg"  || ext == L".jpeg" ||
           ext == L".png"  || ext == L".webp" ||
           ext == L".tif"  || ext == L".tiff" ||
           ext == L".bmp"  || ext == L".gif"  ||
           ext == L".avif" || ext == L".heic";
}

inline bool is_browser_image_ext(std::wstring_view ext)
{
    return ext == L".svg"  || ext == L".jpg"  || ext == L".jpeg" ||
           ext == L".png"  || ext == L".gif"  || ext == L".webp" ||
           ext == L".bmp"  || ext == L".avif" || ext == L".ico";
}

inline bool is_raw_ext(std::wstring_view ext)
{
    return ext == L".arw"  ||   // Sony
           ext == L".cr2"  ||   // Canon (older)
           ext == L".cr3"  ||   // Canon (newer)
           ext == L".nef"  ||   // Nikon
           ext == L".nrw"  ||   // Nikon (compact)
           ext == L".dng"  ||   // Adobe DNG / Leica / Hasselblad
           ext == L".orf"  ||   // Olympus / OM System
           ext == L".rw2"  ||   // Panasonic / Leica
           ext == L".raf"  ||   // Fujifilm
           ext == L".pef"  ||   // Pentax
           ext == L".srw"  ||   // Samsung
           ext == L".x3f"  ||   // Sigma Foveon
           ext == L".3fr"  ||   // Hasselblad
           ext == L".mef"  ||   // Mamiya
           ext == L".mrw";      // Minolta / Konica-Minolta
}

inline bool is_image_ext(std::wstring_view ext)
{
    if (is_standard_image_ext(ext)) return true;
#ifdef FEATURE_RAW_PREVIEW
    if (is_raw_ext(ext)) return true;
#endif
    return false;
}

inline const wchar_t* image_dialog_filter()
{
#ifdef FEATURE_RAW_PREVIEW
    return
        L"Images (*.jpg;*.jpeg;*.png;*.webp;*.tif;*.tiff;*.bmp;*.gif;*.avif;*.heic"
        L";*.arw;*.cr2;*.cr3;*.nef;*.nrw;*.dng;*.orf;*.rw2;*.raf;*.pef)\0"
        L"*.jpg;*.jpeg;*.png;*.webp;*.tif;*.tiff;*.bmp;*.gif;*.avif;*.heic"
        L";*.arw;*.cr2;*.cr3;*.nef;*.nrw;*.dng;*.orf;*.rw2;*.raf;*.pef\0"
        L"RAW Files (*.arw;*.cr2;*.cr3;*.nef;*.nrw;*.dng;*.orf;*.rw2;*.raf;*.pef)\0"
        L"*.arw;*.cr2;*.cr3;*.nef;*.nrw;*.dng;*.orf;*.rw2;*.raf;*.pef\0"
        L"All Files (*.*)\0*.*\0\0";
#else
    return
        L"Images (*.jpg;*.jpeg;*.png;*.webp;*.tif;*.tiff;*.bmp;*.gif;*.avif;*.heic)\0"
        L"*.jpg;*.jpeg;*.png;*.webp;*.tif;*.tiff;*.bmp;*.gif;*.avif;*.heic\0"
        L"All Files (*.*)\0*.*\0\0";
#endif
}

// ── 3D mesh / CAD / OpenSCAD source (WebView2 viewer-next) ───────────────────
// Keep in sync with `ThreeDViewer.tsx` loaders.

inline bool is_viewer_3d_ext(std::wstring_view ext)
{
    return ext == L".stl" || ext == L".obj" || ext == L".gltf" || ext == L".glb" || ext == L".ply"
        || ext == L".step" || ext == L".stp" || ext == L".dxf" || ext == L".scad";
}

inline std::uintmax_t viewer_3d_max_bytes_for_ext(std::wstring_view ext)
{
    if (ext == L".stl")
        return 8ull * 1024 * 1024;
    return 3ull * 1024 * 1024;
}

// ── HTML (viewer-next iframe, base URL = parent folder) ─────────────────────

inline bool is_html_ext(std::wstring_view ext)
{
    return ext == L".html" || ext == L".htm";
}

// ── PDF / spreadsheet / hosted text fetch limits (viewer-next) ──────────────

inline bool is_viewer_pdf_ext(std::wstring_view ext)
{
    return ext == L".pdf";
}

inline bool is_viewer_spreadsheet_ext(std::wstring_view ext)
{
    return ext == L".csv" || ext == L".xls" || ext == L".xlsx";
}

/// Video preview in WebView2 + viewer-next (`FEATURE_VIEWER_WEB`). Keep in sync with `VideoViewerPane.tsx` extensions.
inline bool is_video_ext(std::wstring_view ext)
{
    return ext == L".mp4"  || ext == L".m4v"  || ext == L".webm" || ext == L".mov"  ||
           ext == L".mkv"  || ext == L".avi"  || ext == L".wmv"  || ext == L".ogv"  ||
           ext == L".ogg"  || ext == L".mpg"  || ext == L".mpeg" || ext == L".ts"   ||
           ext == L".m2ts" || ext == L".mts"  || ext == L".3gp"  || ext == L".3g2";
}

/// Extension routed to image / video / PDF / sheet / 3D / HTML preview (not text sniff fallback).
inline bool is_specialized_preview_ext(std::wstring_view ext)
{
    return is_image_ext(ext) || is_browser_image_ext(ext) || is_video_ext(ext)
        || is_viewer_pdf_ext(ext) || is_viewer_spreadsheet_ext(ext) || is_viewer_3d_ext(ext)
        || is_html_ext(ext);
}

/// Known text extension, or unknown extension + plaintext sniff (after specialized preview types).
inline bool is_text_preview_eligible_for_path(const std::wstring& path_w)
{
    namespace fs = std::filesystem;
    std::wstring ext = fs::path(path_w).extension().wstring();
    for (auto& c : ext)
        c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
    if (is_text_ext(ext))
        return true;
    if (is_specialized_preview_ext(ext))
        return false;
    return regular_file_sniffs_as_plaintext(path_w);
}

/// ISO BMFF (`ftyp` at byte offset 4): MP4 / M4V / MOV family, including extensionless files the Shell still types as video.
inline bool sniff_iso_bmff_ftyp_at_file_start(const std::wstring& path_w)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path_w);
    if (!fs::is_regular_file(p, ec) || ec)
        return false;
    const auto sz = fs::file_size(p, ec);
    if (ec || sz < 12)
        return false;
    std::ifstream in(p, std::ios::binary);
    unsigned char b[12];
    if (!in.read(reinterpret_cast<char*>(b), 12))
        return false;
    return std::memcmp(b + 4, "ftyp", 4) == 0;
}

/// Explorer + centre preview: known video extension, or extensionless file that still begins like MP4/MOV.
inline bool is_video_preview_eligible_for_path(const std::wstring& path_w)
{
    namespace fs = std::filesystem;
    std::wstring ext = fs::path(path_w).extension().wstring();
    for (auto& c : ext)
        c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
    if (is_video_ext(ext))
        return true;
    if (!ext.empty())
        return false;
    return sniff_iso_bmff_ftyp_at_file_start(path_w);
}

/// Max preview bytes using full path (handles extensionless BMFF video vs extensionless text sniff).
inline std::uintmax_t viewer_document_max_bytes_for_path_w(const std::wstring& path_w)
{
    namespace fs = std::filesystem;
    std::wstring ext = fs::path(path_w).extension().wstring();
    for (auto& c : ext)
        c = static_cast<wchar_t>(std::towlower(static_cast<std::wint_t>(c)));
    if (ext == L".pdf")
        return 48ull * 1024 * 1024;
    if (is_video_ext(ext) || (ext.empty() && sniff_iso_bmff_ftyp_at_file_start(path_w)))
        return 512ull * 1024 * 1024;
    if (is_text_preview_eligible_for_path(path_w))
        return 5ull * 1024 * 1024;
    return 32ull * 1024 * 1024;
}

inline std::uintmax_t viewer_document_max_bytes_for_ext(std::wstring_view ext)
{
    if (ext == L".pdf")
        return 48ull * 1024 * 1024;
    if (is_video_ext(ext))
        return 512ull * 1024 * 1024;
    if (is_text_ext(ext) || ext.empty())
        return 5ull * 1024 * 1024;
    if (is_specialized_preview_ext(ext))
        return 32ull * 1024 * 1024;
    // Unknown extension: assume text sniff fallback limit until LoadText re-checks the path.
    return 5ull * 1024 * 1024;
}

} // namespace pmui
