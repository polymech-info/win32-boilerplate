#pragma once
//
// Pure helpers for LLM-facing glob / directory walks: dev-folder name rules
// (aligned with pm-pics exclude-default.ts) and the same extension blocklist
// used by file_read for listing-style tools.
//
#include "polymech_export.h"

#include <filesystem>
#include <string_view>

namespace media::llm {

/// True if a single path component (directory or file name) should be skipped
/// when scanning under a project tree — mirrors DEFAULT_EXCLUDES + "any dot prefix".
POLYMECH_API bool glob_exclude_default_path_component(std::string_view name_utf8);

/// True if any filename segment of `path` matches glob_exclude_default_path_component.
/// Uses generic path stringification (caller should pass a normalised absolute path).
POLYMECH_API bool glob_path_has_excluded_component(const std::filesystem::path& path);

/// Same extension denylist as file_read (images, archives, binaries, media) — lowercase ext with dot.
POLYMECH_API bool glob_file_extension_blocked_like_file_read(std::string_view ext_lower);

} // namespace media::llm
