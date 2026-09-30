#pragma once
// Swappable `<base href>` for embedded markdown preview (relative images / links).
#include <string>
#include <Windows.h>

namespace pmui {

/// UTF-8 `file:///…/` URL for the parent directory of @p markdown_file_path.
/// Empty on failure (no parent, conversion error, extended `\\?\` paths).
std::string default_markdown_base_href_utf8(LPCWSTR markdown_file_path);

using MarkdownBaseHrefUtf8Fn = std::string(LPCWSTR markdown_file_path);

/// Current resolver for markdown preview; never used as a call target when null.
/// Starts as `default_markdown_base_href_utf8`. Set to nullptr to omit `<base href>`.
MarkdownBaseHrefUtf8Fn* markdown_base_href_resolver() noexcept;

/// @p fn nullptr disables `<base href>`; any other function replaces the default.
/// Restore the built-in behaviour with `set_markdown_base_href_resolver(
///     &default_markdown_base_href_utf8)`.
void set_markdown_base_href_resolver(MarkdownBaseHrefUtf8Fn* fn) noexcept;

} // namespace pmui
