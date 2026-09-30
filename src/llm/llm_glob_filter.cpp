#include "llm_glob_filter.hpp"

#include <cctype>
#include <string>

namespace media::llm {

namespace {

void ascii_tolower_inplace(std::string& s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

/** Lowercase; mirrors pm-pics `exclude-default.ts` non-dot entries (dot-prefix handled separately). */
constexpr const char* k_exclude_names_lower[] = {
    "__macosx",
    "__pycache__",
    "bower_components",
    "cmake_install.cmake",
    "cmakecache.txt",
    "cmakefiles",
    "cmakescripts",
    "compile_commands.json",
    "coverage",
    "debug",
    "desktop.ini",
    "ehthumbs.db",
    "ipch",
    "node_modules",
    "npm-debug.log",
    "out",
    "release",
    "target",
    "testresults",
    "thumbs.db",
    "venv",
    "vfs-settings.json",
    "x64",
    "x86",
    "yarn-debug.log",
    "yarn-error.log",
};

bool exclude_set_contains_lower(std::string_view key_lower) {
    for (const char* e : k_exclude_names_lower) {
        if (key_lower == e) return true;
    }
    return false;
}

constexpr const char* k_blocked_exts[] = {
    ".png",  ".apng", ".jpg",  ".jpeg", ".jpe",  ".jfif", ".gif",  ".webp", ".bmp",
    ".tif",  ".tiff", ".ico",  ".heic", ".heif", ".avif", ".jxl",  ".psd",
    ".exr",  ".hdr",  ".raw",  ".cr2",  ".nef",  ".orf",  ".sr2",  ".dng",
    ".exe",  ".dll",  ".sys",  ".msi",  ".com",  ".cab",  ".so",   ".dylib",
    ".bundle", ".lib", ".a",   ".o",    ".obj",  ".pdb",  ".ilk",  ".exp",
    ".zip",  ".7z",   ".rar",  ".tar",  ".gz",   ".bz2",  ".xz",   ".zst",
    ".apk",  ".ipa",  ".wasm",
    ".woff", ".woff2", ".ttf", ".otf", ".eot",
    ".mp3",  ".mp4",  ".m4a",  ".webm", ".mov", ".avi",  ".mkv",  ".flac",
    ".pdf",
};

} // namespace

bool glob_exclude_default_path_component(std::string_view name_utf8) {
    if (name_utf8.empty()) return false;
    if (name_utf8[0] == '.') return true;

    std::string key(name_utf8);
    ascii_tolower_inplace(key);
    return exclude_set_contains_lower(std::string_view(key));
}

bool glob_path_has_excluded_component(const std::filesystem::path& path) {
    for (const auto& part : path) {
        const std::string seg = part.string();
        if (seg.empty() || seg == "." || seg == "..") continue;
        if (glob_exclude_default_path_component(seg)) return true;
    }
    return false;
}

bool glob_file_extension_blocked_like_file_read(std::string_view ext_lower) {
    for (const char* b : k_blocked_exts) {
        if (ext_lower == b) return true;
    }
    return false;
}

} // namespace media::llm
