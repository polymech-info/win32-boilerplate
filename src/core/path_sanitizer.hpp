#pragma once

#include <string>

namespace media::path {

/**
 * VFS subpath: relative, no null / traversal / `C:\` (matches ref `sanitizeSubpath`).
 * Empty input → empty output (ok).
 */
std::string sanitize_subpath(const std::string &input, std::string &err);

/**
 * After `sanitize_subpath` — allowlist per segment, extension-friendly on the last.
 */
std::string sanitize_write_path(const std::string &subpath, std::string &err, bool is_directory = false);

/** Single path segment: illegal chars, control, Windows reserved, trailing dots/spaces. */
std::string sanitize_filename(const std::string &input, const std::string &replacement = std::string());

/**
 * User-facing local / glob / UNC path string (not `http`/`https` — use
 * @ref `validate_url_spec_for_input_selection` later). Reuses tamper+traversal rules from
 * `sanitize_subpath` (steps 1–7) without VFS “no drive / root” restrictions.
 */
bool validate_path_spec_for_input_selection(const std::string &raw, std::string &err);

/**
 * Placeholder for future URL allowlists (schemes, hosts). Currently only structural checks
 * you may want to expand (delegate to @ref is_http_url in callers, or stricter here later).
 */
bool validate_url_spec_for_input_selection(const std::string &raw, std::string &err);

} // namespace media::path
