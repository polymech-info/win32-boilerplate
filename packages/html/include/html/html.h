#pragma once

#include <string>
#include <vector>

namespace html {

/// Parsed element — tag name + text content.
struct Element {
  std::string tag;
  std::string text;
};

/// Link with href and optional attributes.
struct Link {
  std::string href;
  std::string rel;   // e.g. "canonical", "stylesheet"
  std::string text;  // anchor text (for <a> tags)
};

/// Parse an HTML string and return all elements with their text content.
std::vector<Element> parse(const std::string &html_str);

/// Extract the text content of all elements matching a CSS selector.
std::vector<std::string> select(const std::string &html_str,
                                const std::string &selector);

// ── Enricher extraction helpers ─────────────────────────────────────────────

/// Extract the <title> text.
std::string get_title(const std::string &html_str);

/// Extract a <meta name="X"> or <meta property="X"> content attribute.
std::string get_meta(const std::string &html_str, const std::string &name);

/// Extract <link rel="canonical"> href.
std::string get_canonical(const std::string &html_str);

/// Extract all <a href="..."> values (resolved links as-is from the HTML).
std::vector<Link> get_links(const std::string &html_str);

/// Extract visible body text, stripping script/style/noscript/svg/iframe.
std::string get_body_text(const std::string &html_str);

/// Extract raw JSON strings from <script type="application/ld+json">.
std::vector<std::string> get_json_ld(const std::string &html_str);

/// Extract an attribute value from the first element matching a CSS selector.
std::string get_attr(const std::string &html_str, const std::string &selector,
                     const std::string &attr_name);

/// Convert HTML content to Markdown.
std::string to_markdown(const std::string &html_str);

// ── Markdown → HTML (md4c) ──────────────────────────────────────────────────

struct MdOptions {
    bool tables           = true;   ///< GFM `| col | col |`
    bool strikethrough    = true;   ///< GFM `~~del~~`
    bool task_lists       = true;   ///< GFM `[ ] todo`
    bool autolinks        = true;   ///< GFM bare URLs
    bool fenced_code      = true;   ///< triple-backtick code blocks
    bool no_html          = false;  ///< treat raw HTML as text (safe mode)
    bool permissive_url   = true;   ///< accept URLs with non-RFC chars
};

/// Render Markdown into an HTML fragment (no `<html>` / `<head>` wrapper).
/// Defaults to GFM (tables, strikethrough, task lists, autolinks, fenced code).
std::string md_to_html(const std::string &markdown,
                       const MdOptions &opts = {});

/// Theme variant used by `wrap_markdown_html`.
enum class HtmlTheme { Light = 0, Dark = 1 };

/// Sentinel: use built-in light/dark page `background` / `color` from the
/// embedded stylesheet (`packages/html/src/md.cpp`). Any other value is a
/// Win32-style `COLORREF` (`0x00bbggrr`) merged into the final `<style>` so
/// `html, body` match an app window background (e.g. `ThemePalette::window_bg`).
constexpr uint32_t kMarkdownHtmlBuiltinPageColors = 0xFFFFFFFFu;

/// Wrap an HTML fragment into a full GitHub-flavoured document with embedded
/// CSS for typography, tables, code blocks, and quotes — suitable for an
/// embedded WebBrowser preview. Pass `HtmlTheme::Dark` for the dark palette.
/// Optional @p page_bg_colorref / @p page_fg_colorref override the outer page
/// colors so the document background matches host panels instead of GitHub `#0d1117`.
/// When @p base_href_utf8 is non-null and non-empty, a `<base href="…">` is emitted
/// in `<head>` (after the viewport meta) so relative URLs resolve against that base.
std::string wrap_markdown_html(const std::string &body,
                               const std::string &title = {},
                               HtmlTheme theme = HtmlTheme::Light,
                               uint32_t page_bg_colorref = kMarkdownHtmlBuiltinPageColors,
                               uint32_t page_fg_colorref = kMarkdownHtmlBuiltinPageColors,
                               const std::string *base_href_utf8 = nullptr);

} // namespace html

#include "html/md_terminal.h"
