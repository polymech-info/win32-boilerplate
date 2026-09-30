#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

/** Optional stderr-style trace for Pixlwiz / pm-pics image upload (multipart). */
using PmServiceUploadLogLine = std::function<void(std::string_view line)>;

std::string pm_getenv_trimmed(const char* key);
std::string pm_trim_trailing_slash(std::string s);

/**
 * Resolve API host base URL: non-empty `cli_server_url_override` wins, else
 * `SERVER_URL`, `VITE_SERVER_IMAGE_API_URL`, `CLIENT_URL` (trimmed, no trailing slash).
 */
std::string pm_resolve_service_server_base(const std::string& cli_server_url_override);

/**
 * POST multipart field `file` to `{base_url}/api/images` with query **`forward=vfs&original=true`**
 * (same as pm-pics `uploadUtils.uploadImage` / browser `FormData` to `/api/images?...`).
 * `forward=vfs` stores via the VFS path; `original=true` keeps original bytes where the server supports it.
 * On transport success sets `http_code_out` and `response_body_out` even when HTTP is 4xx/5xx.
 */
bool pm_service_post_image_multipart(const std::string& base_url, const std::string& bearer_token,
                                     const std::filesystem::path& file_path, long& http_code_out,
                                     std::string& response_body_out, std::string& err_out,
                                     const PmServiceUploadLogLine& log_line = {});
