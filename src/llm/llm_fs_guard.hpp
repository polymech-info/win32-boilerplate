#pragma once
//
// Shared filesystem policy for all LLM-driven tools (buffer + path catalog).
// Deny lists live in sensitive_paths.h (secrets, DB dumps, browser stores, …);
// see llm_fs_guard.cpp for dotfiles and platform hidden-file checks as well.
//
#include "polymech_export.h"

#include <filesystem>
#include <string>

namespace media::llm {

/// Returns a non-empty human-readable reason if LLM tools must not touch this path
/// (read, write, list root, or emit outputs here). Empty string means allowed.
/// Explorer → dock preview (`CFileViewer::OpenFile`) uses the same check so the WebView
/// host does not map or read paths the tools are denied.
/// When the path exists, it is normalized with weakly_canonical first.
POLYMECH_API std::string llm_fs_guard_deny_reason(const std::filesystem::path& path);

/// Lightweight sensitive-path test: checks only the SENSITIVE_PATH_PREFIXES list
/// (secrets, DB dumps, credential stores, browser profiles, …).
/// Does NOT check for dot-files or platform-hidden flags — those are handled by the
/// caller's own include_hidden option.  Used by search_files / find_images at the
/// lib layer so every call site gets the filter for free.
POLYMECH_API bool llm_is_sensitive_path(const std::filesystem::path& path);

/// Write targets only (`write_file`, image tool output paths). Blocks disallowed
/// extensions (scripts, installers, PDF/Office, archives, …) and system prefixes;
/// image / video / audio extensions from the allowlist are permitted.
POLYMECH_API std::string llm_fs_guard_write_deny_reason(const std::filesystem::path& path);

} // namespace media::llm
