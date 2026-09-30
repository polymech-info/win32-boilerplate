#pragma once

#include <string>

namespace html {

struct MdOptions;

/// Convert Markdown to a terminal-friendly UTF-8 string. When @p ansi_color is true, emit SGR
/// sequences for headings, emphasis, code, links, etc. When false, still applies block/inline
/// structure (headings, bullets, GFM pipe tables) without escape codes. Uses the same GFM-oriented flags as
/// @ref md_to_html via @p md_opts. On parse failure, returns @p markdown unchanged.
std::string markdown_to_terminal(const std::string& markdown, const MdOptions& md_opts, bool ansi_color,
                                 int terminal_width_cols);

} // namespace html
