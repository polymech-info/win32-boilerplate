#include "llm_fs_guard.hpp"

#include "core/glob_paths.hpp"
#include "llm/llm_fs_write_blocklist.h"
#include "llm/sensitive_paths.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include <filesystem>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#if defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace media::llm {

namespace fs = std::filesystem;

namespace {

void tolower_inplace(std::string& s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

static bool str_starts_with_sv(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool str_ends_with_sv(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool path_matches_dir_prefix(std::string_view path, std::string_view dir_prefix) {
    if (dir_prefix.empty()) return path.empty();
    if (dir_prefix.back() != '/') return str_starts_with_sv(path, dir_prefix);
    if (str_starts_with_sv(path, dir_prefix)) return true;
    const std::string_view dir = dir_prefix.substr(0, dir_prefix.size() - 1);
    return path == dir;
}

static std::string norm_path_for_sensitive(const fs::path& abs) {
    std::string s = abs.generic_string();
    for (char& c : s)
        if (c == '\\') c = '/';
#if defined(_WIN32)
    tolower_inplace(s);
#endif
    return s;
}

static std::string norm_sensitive_pattern_row(const char* row) {
    std::string s(row);
    for (char& c : s)
        if (c == '\\') c = '/';
#if defined(_WIN32)
    tolower_inplace(s);
#endif
    return s;
}

#if defined(_WIN32)
static void retarget_windows_pattern_drive(std::string& pat, const std::string& norm_path) {
    if (norm_path.size() < 2 || norm_path[1] != ':' || pat.size() < 2 || pat[1] != ':') return;
    if (std::tolower(static_cast<unsigned char>(pat[0])) != 'c') return;
    pat[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(norm_path[0])));
}
#endif

static bool path_matches_star_pattern(std::string_view path, std::string_view pat) {
    const std::size_t star = pat.find('*');
    if (star == std::string_view::npos) {
        if (pat.empty()) return path.empty();
        if (pat.back() == '/') return str_starts_with_sv(path, pat);
        return path == pat;
    }
    const std::string_view left = pat.substr(0, star);
    if (!str_starts_with_sv(path, left)) return false;
    path.remove_prefix(left.size());
    const std::string_view patrest = pat.substr(star + 1);
    if (path.empty()) return false;
    const std::size_t slash = path.find('/');
    if (slash == std::string_view::npos) return path_matches_star_pattern(path, patrest);
    const std::string_view tail = path.substr(slash);
    return path_matches_star_pattern(tail, patrest);
}

static bool path_blocked_by_prefix_list(const fs::path& abs) {
    const std::string norm = norm_path_for_sensitive(abs);
    for (const char* const* pp = SENSITIVE_PATH_PREFIXES; *pp; ++pp) {
        std::string pat = norm_sensitive_pattern_row(*pp);
#if defined(_WIN32)
        retarget_windows_pattern_drive(pat, norm);
#endif
        if (pat.find("**") != std::string::npos) {
            if (media::path_matches_path_glob(norm, pat)) return true;
            continue;
        }
        if (pat.find('*') != std::string::npos) {
            if (path_matches_star_pattern(norm, pat)) return true;
        } else if (!pat.empty() && pat.back() == '/') {
            if (path_matches_dir_prefix(norm, pat)) return true;
        } else {
            if (norm == pat) return true;
        }
    }
    return false;
}

static bool filename_is_dotfile(const fs::path& p) {
    const std::string fn = p.filename().string();
    return !fn.empty() && fn[0] == '.';
}

#if defined(_WIN32)
static bool win_is_hidden(const fs::path& p) {
    const std::wstring w = p.wstring();
    const DWORD a = GetFileAttributesW(w.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return false;
    return (a & FILE_ATTRIBUTE_HIDDEN) != 0;
}
#endif

#if defined(__APPLE__)
static bool apple_is_uf_hidden(const fs::path& p) {
    struct stat st {};
    if (stat(p.string().c_str(), &st) != 0) return false;
    return (st.st_flags & UF_HIDDEN) != 0;
}
#endif

static bool file_has_platform_hidden(const fs::path& p) {
#if defined(_WIN32)
    if (win_is_hidden(p)) return true;
#elif defined(__APPLE__)
    if (apple_is_uf_hidden(p)) return true;
#endif
    (void)p;
    return false;
}

static bool write_ext_is_allowed_media(std::string_view ext) {
    static constexpr const char* allowed[] = {
        // raster / vector images
        ".jpg",  ".jpeg", ".jpe",  ".jfif", ".png",  ".apng", ".webp", ".gif",  ".bmp",
        ".tif",  ".tiff", ".ico",  ".heic", ".heif", ".avif", ".jxl",  ".svg",
        ".psd",  ".exr",  ".hdr",  ".tga",  ".pcx",  ".wbmp",
        // RAW stills
        ".arw",  ".cr2",  ".cr3",  ".nef",  ".nrw",  ".dng",  ".orf",  ".rw2",
        ".raf",  ".pef",  ".srw",  ".x3f",  ".3fr",  ".mef",  ".mrw",  ".raw",
        // video
        ".mp4",  ".m4v",  ".mkv",  ".webm", ".mov",  ".avi",  ".wmv",  ".flv",
        ".mpg",  ".mpeg", ".m2ts", ".mts",  ".ts",   ".ogv",  ".3gp",  ".3g2",
        // audio
        ".mp3",  ".wav",  ".flac", ".ogg",  ".opus", ".m4a",  ".aac",  ".wma",
        ".aiff", ".aif",  ".alac",
    };
    for (const char* a : allowed) {
        if (ext == a) return true;
    }
    return false;
}

static bool write_tarball_suffix_blocked(const fs::path& p, std::string_view ext) {
    if (ext != ".gz" && ext != ".bz2" && ext != ".xz" && ext != ".zst") return false;
    std::string stem = p.stem().string();
    tolower_inplace(stem);
    return stem.size() >= 4 && stem.compare(stem.size() - 4, 4, ".tar") == 0;
}

static bool write_abs_prefix_blocked_for_write(const std::string& norm) {
    for (const char* const* pp = LLM_WRITE_BLOCKED_ABSOLUTE_PREFIXES; *pp; ++pp) {
        std::string pat = norm_sensitive_pattern_row(*pp);
#if defined(_WIN32)
        retarget_windows_pattern_drive(pat, norm);
#endif
        if (path_matches_dir_prefix(norm, pat)) return true;
    }
    return false;
}

static bool write_extension_in_blocklist(std::string_view ext) {
    for (const char* const* pp = LLM_WRITE_BLOCKED_EXTENSIONS; *pp; ++pp) {
        if (ext == *pp) return true;
    }
    return false;
}

} // namespace

std::string llm_fs_guard_write_deny_reason(const fs::path& path) {
    fs::path p = path;
    std::error_code ec;
    if (fs::exists(p, ec)) {
        const fs::path c = fs::weakly_canonical(p, ec);
        if (!ec && !c.empty()) p = c;
    } else {
        p = p.lexically_normal();
    }

    const std::string norm = norm_path_for_sensitive(p);
    if (write_abs_prefix_blocked_for_write(norm)) return "refusing write to system or protected location";

    std::string ext = p.extension().string();
    tolower_inplace(ext);
    if (write_ext_is_allowed_media(ext)) return {};
    if (write_tarball_suffix_blocked(p, ext)) return "refusing disallowed archive type for write";
    if (write_extension_in_blocklist(ext)) return "refusing disallowed file type for write";
    return {};
}

bool llm_is_sensitive_path(const fs::path& path) {
    fs::path p = path;
    std::error_code ec;
    if (fs::exists(p, ec)) {
        const fs::path c = fs::weakly_canonical(p, ec);
        if (!ec && !c.empty()) p = c;
    } else {
        p = p.lexically_normal();
    }
    return path_blocked_by_prefix_list(p);
}

std::string llm_fs_guard_deny_reason(const fs::path& path) {
    fs::path p = path;
    std::error_code ec;
    if (fs::exists(p, ec)) {
        const fs::path c = fs::weakly_canonical(p, ec);
        if (!ec && !c.empty()) p = c;
    } else {
        p = p.lexically_normal();
    }

    if (filename_is_dotfile(p)) return "refusing dot-named path";
    if (path_blocked_by_prefix_list(p)) return "refusing sensitive path or filename";
    if (fs::exists(p, ec) && file_has_platform_hidden(p)) return "refusing hidden file";
    return {};
}

} // namespace media::llm
