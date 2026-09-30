#pragma once

#include "lib/pm_service_upload.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/** One HTTP response as received (bytes as UTF-8 string; body is exact wire payload). */
struct PmServiceHttpRawStep {
    std::string label;
    long        http_status = 0;
    std::string body_raw;
};

/** Outcome of `POST /api/posts` plus `POST /api/pictures` for each uploaded image (pm-pics flow). */
struct PmServiceCreatePostPicturesResult {
    bool                            ok = false;
    std::string                     err;
    std::string                     post_id;
    std::vector<std::string>        picture_ids;
    /** Order: create post, then per file [multipart /api/images, POST /api/pictures]. */
    std::vector<PmServiceHttpRawStep> http_raw_steps;
};

/** Optional progress hook (UTF-8); may run on a worker thread — post to UI yourself. */
using PmServicePostProgressLine = std::function<void(std::string_view)>;

/**
 * Create a post then upload each image (multipart /api/images) and attach rows (POST /api/pictures).
 * Matches `publishHandlers` / `client-posts` + `client-pictures`: type `supabase-image`, positions 0..n-1.
 *
 * @param title_override  If non-empty, used as post title; otherwise first file's filename (including extension).
 * @param description     If empty, JSON `description` is null; else the given string.
 * @param settings_visibility  Sent as `settings.visibility` on POST /api/posts (`"public"`, `"listed"`, or `"private"`; default public).
 */
PmServiceCreatePostPicturesResult pm_service_create_post_with_pictures(
    const std::string& base_url, const std::string& bearer_token, const std::vector<std::filesystem::path>& image_files,
    const std::string& title_override, const std::string& description, const std::string& settings_visibility,
    const PmServiceUploadLogLine& log_line = {}, const PmServicePostProgressLine& progress = {});
