#pragma once
// Pure filesystem helpers for the file queue: expand directories to image files,
// read Win32 HDROP lists. Image membership uses pmui::is_image_ext (lib feature flags).

#include <shellapi.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace pmui::queue_paths {

/**
 * All regular files under @p dir whose lowercased extension matches is_image_ext.
 * @param recursive  When true, walk subdirectories (recursive_directory_iterator).
 * On iterator / filesystem errors, returns what was collected so far; @p ec may be set.
 */
std::vector<std::filesystem::path> image_files_in_directory(
    const std::filesystem::path& dir,
    bool                         recursive,
    std::error_code&             ec);

/**
 * Non-recursive directory listing: regular files whose extension is an image or a known text/markdown
 * preview type (`is_image_ext` / `is_text_preview_eligible_for_path`). Sorted by filename (case-insensitive) for stable ordering.
 */
std::vector<std::filesystem::path> previewable_files_in_directory(
    const std::filesystem::path& dir,
    std::error_code&             ec);

/**
 * Glob expansion via vendored p-ranav/glob (`glob::glob` / `glob::rglob` when the pattern contains `**`).
 * Examples: `C:\pics\*.png`, `.\assets\IMG_?.jpg`, `.\repo\**\*.md`.
 * Returns previewable regular files (same predicate as @ref previewable_files_in_directory), sorted by filename.
 */
std::vector<std::filesystem::path> previewable_paths_matching_filename_wildcard(
    const std::filesystem::path& pattern_path,
    std::error_code&             ec);

/**
 * Queued path expansion (matches Add-to-queue / drag-drop rules):
 * - If @p p is a directory: image files only (see image_files_in_directory).
 * - Otherwise: a single path (any file type — same as legacy list view behaviour).
 */
std::vector<std::filesystem::path> paths_to_enqueue(
    const std::filesystem::path& p,
    bool                         recursive_for_directories,
    std::error_code&             ec);

/** Shell drag-and-drop: one path per HDROP entry, each passed through paths_to_enqueue (recursive dirs). */
std::vector<std::filesystem::path> image_paths_from_hdrop(HDROP hDrop);

} // namespace pmui::queue_paths
