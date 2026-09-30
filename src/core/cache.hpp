#pragma once

#include "resize.hpp"

#include <filesystem>
#include <string>

namespace media {

/** `<cwd>/cache/images` (absolute at call time). */
std::filesystem::path default_cache_dir();

/** Resolved cache root: `opt.cache_dir` if set, else `default_cache_dir()`. */
std::filesystem::path effective_cache_dir(const ResizeOptions &opt);

/**
 * If caching is enabled and a cache entry exists, copy it to `output_path`.
 * @return true on cache hit (copy succeeded), false on miss (continue processing).
 * On hard error, sets `err_out` and returns false.
 */
bool try_copy_from_cache(const std::string &input_path, const std::string &output_path, const ResizeOptions &opt,
                         std::string &err_out);

/** After a successful resize, copy `output_path` into the cache (best-effort; ignores failures). */
void store_in_cache(const std::string &input_path, const std::string &output_path, const ResizeOptions &opt);

} // namespace media
