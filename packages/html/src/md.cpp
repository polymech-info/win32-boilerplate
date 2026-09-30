// ── Markdown → HTML rendering via md4c (CommonMark + GFM) ───────────────────
// md4c is a tiny SAX-style parser; md_html() is the convenience wrapper that
// emits an HTML fragment (no <html>/<body> wrapper) into a callback.
//
// We collect the chunks into a std::string and either return that fragment
// directly (md_to_html) or wrap it in a full document with embedded CSS
// (wrap_markdown_html) suitable for an embedded WebBrowser preview.
//
#include "html/html.h"

#include <md4c-html.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace html {

namespace {

void md_append_cb(const MD_CHAR *chunk, MD_SIZE size, void *userdata) {
    auto *out = static_cast<std::string *>(userdata);
    out->append(chunk, size);
}

unsigned build_md_flags(const MdOptions &o) {
    unsigned f = 0;
    if (o.tables)         f |= MD_FLAG_TABLES;
    if (o.strikethrough)  f |= MD_FLAG_STRIKETHROUGH;
    if (o.task_lists)     f |= MD_FLAG_TASKLISTS;
    if (o.autolinks)      f |= MD_FLAG_PERMISSIVEAUTOLINKS;
    if (o.permissive_url) f |= MD_FLAG_PERMISSIVEURLAUTOLINKS
                              | MD_FLAG_PERMISSIVEEMAILAUTOLINKS;
    if (o.fenced_code)    f |= MD_FLAG_NOINDENTEDCODEBLOCKS == 0 ? 0 : 0;
    if (o.no_html)        f |= MD_FLAG_NOHTML;
    // Always-on CommonMark niceties for sane web output.
    f |= MD_FLAG_COLLAPSEWHITESPACE;
    return f;
}

// Append `html, body { color / background }` with !important so the page
// matches Win32 `ThemePalette` when callers pass non-builtin COLORREFs.
static void append_html_escaped_attr(std::string &doc, const std::string &val) {
    for (unsigned char uc : val) {
        const char c = static_cast<char>(uc);
        switch (c) {
        case '&': doc += "&amp;";  break;
        case '"': doc += "&quot;"; break;
        case '<': doc += "&lt;";   break;
        default:  doc += c;        break;
        }
    }
}

static void append_page_color_override(std::string &doc, uint32_t bg,
                                       uint32_t fg) {
    if (bg == kMarkdownHtmlBuiltinPageColors ||
        fg == kMarkdownHtmlBuiltinPageColors)
        return;
    const auto ch = [](uint32_t c, int shift) -> unsigned {
        return (unsigned)((c >> shift) & 0xFFu);
    };
    // COLORREF 0x00bbggrr → CSS #rrggbb
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "\nhtml,body{color:#%02X%02X%02X!important;"
                  "background:#%02X%02X%02X!important;}\n",
                  ch(fg, 0), ch(fg, 8), ch(fg, 16), ch(bg, 0), ch(bg, 8),
                  ch(bg, 16));
    doc += buf;
}

} // namespace

std::string md_to_html(const std::string &markdown, const MdOptions &opts) {
    std::string out;
    out.reserve(markdown.size() + markdown.size() / 4 + 32);
    const int rc = md_html(
        markdown.data(),
        static_cast<MD_SIZE>(markdown.size()),
        md_append_cb,
        &out,
        build_md_flags(opts),
        /* renderer flags */ 0u);
    if (rc != 0) {
        // md_html only fails on allocation issues; fall back to escaping the
        // input so the caller still gets something safe to display.
        std::string safe = "<pre>";
        safe.reserve(markdown.size() + 16);
        for (char c : markdown) {
            switch (c) {
                case '<': safe += "&lt;";  break;
                case '>': safe += "&gt;";  break;
                case '&': safe += "&amp;"; break;
                default:  safe += c;       break;
            }
        }
        safe += "</pre>";
        return safe;
    }
    return out;
}

std::string wrap_markdown_html(const std::string &body,
                               const std::string &title,
                               HtmlTheme theme,
                               uint32_t page_bg_colorref,
                               uint32_t page_fg_colorref,
                               const std::string *base_href_utf8) {
    // Inline GitHub-ish CSS so the WebBrowser shows tables / code / quotes
    // without needing any external file or stylesheet.  Kept concise; tweak
    // freely.
    static const char *kCssLight = R"CSS(
:root { color-scheme: light dark; }
html, body {
  margin: 0;
  font: 14px/1.55 -apple-system, "Segoe UI", system-ui, "Helvetica Neue", Arial, sans-serif;
  color: #1f2328;
  background: #ffffff;
}
body { padding: 16px 22px; max-width: 980px; box-sizing: border-box; }
h1, h2, h3, h4, h5, h6 { line-height: 1.25; margin: 18px 0 8px; }
h1 { font-size: 1.75em; border-bottom: 1px solid #d0d7de; padding-bottom: 0.2em; }
h2 { font-size: 1.4em;  border-bottom: 1px solid #d0d7de; padding-bottom: 0.2em; }
h3 { font-size: 1.18em; }
p, ul, ol, blockquote, pre, table { margin: 0 0 12px; }
a  { color: #0969da; text-decoration: none; }
a:hover { text-decoration: underline; }
img { max-width: 100%; height: auto; }
code, pre, kbd, samp {
  font-family: "Cascadia Mono", Consolas, "Liberation Mono", monospace;
  font-size: 12.5px;
}
:not(pre) > code {
  padding: 0.18em 0.36em;
  background: rgba(175,184,193,0.2);
  border-radius: 4px;
}
pre {
  padding: 12px 14px;
  background: #f6f8fa;
  border-radius: 6px;
  overflow: auto;
}
pre code { background: transparent; padding: 0; }
blockquote {
  margin: 0 0 12px;
  padding: 0 1em;
  border-left: 4px solid #d0d7de;
  color: #57606a;
}
table {
  border-collapse: collapse;
  display: block;
  overflow-x: auto;
}
th, td {
  padding: 6px 13px;
  border: 1px solid #d0d7de;
}
tr:nth-child(2n) { background: #f6f8fa; }
hr { border: 0; border-top: 1px solid #d0d7de; margin: 18px 0; }
ul, ol { padding-left: 1.6em; }
li + li { margin-top: 0.2em; }
.task-list-item { list-style: none; }
.task-list-item input { margin: 0 0.4em 0.2em -1.4em; }
* { scrollbar-width: thin; scrollbar-color: #a8adb4 #e8eaed; }
::-webkit-scrollbar { width: 10px; height: 10px; }
::-webkit-scrollbar-track { background: #e8eaed; }
::-webkit-scrollbar-thumb { background: #a8adb4; border-radius: 5px; }
::-webkit-scrollbar-thumb:hover { background: #7d838c; }
@media (prefers-color-scheme: dark) {
  html, body { color: #e6edf3; background: #0d1117; }
  h1, h2     { border-bottom-color: #30363d; }
  a          { color: #58a6ff; }
  pre        { background: #161b22; }
  :not(pre) > code { background: rgba(110,118,129,0.4); }
  th, td     { border-color: #30363d; }
  tr:nth-child(2n) { background: #161b22; }
  blockquote { border-left-color: #30363d; color: #8b949e; }
  hr         { border-top-color: #30363d; }
}
)CSS";

    // Forced-dark variant — used when the host app is in Dark mode regardless
    // of the system `prefers-color-scheme` (the embedded WebBrowser/IE engine
    // doesn't observe the modern app dark-mode signal).
    static const char *kCssDark = R"CSS(
:root { color-scheme: dark; }
html, body {
  margin: 0;
  font: 14px/1.55 -apple-system, "Segoe UI", system-ui, "Helvetica Neue", Arial, sans-serif;
  color: #e6edf3;
  background: #0d1117;
}
body { padding: 16px 22px; max-width: 980px; box-sizing: border-box; }
h1, h2, h3, h4, h5, h6 { line-height: 1.25; margin: 18px 0 8px; }
h1 { font-size: 1.75em; border-bottom: 1px solid #30363d; padding-bottom: 0.2em; }
h2 { font-size: 1.4em;  border-bottom: 1px solid #30363d; padding-bottom: 0.2em; }
h3 { font-size: 1.18em; }
p, ul, ol, blockquote, pre, table { margin: 0 0 12px; }
a  { color: #58a6ff; text-decoration: none; }
a:hover { text-decoration: underline; }
img { max-width: 100%; height: auto; }
code, pre, kbd, samp {
  font-family: "Cascadia Mono", Consolas, "Liberation Mono", monospace;
  font-size: 12.5px;
}
:not(pre) > code {
  padding: 0.18em 0.36em;
  background: rgba(110,118,129,0.4);
  border-radius: 4px;
}
pre {
  padding: 12px 14px;
  background: #161b22;
  border-radius: 6px;
  overflow: auto;
}
pre code { background: transparent; padding: 0; }
blockquote {
  margin: 0 0 12px;
  padding: 0 1em;
  border-left: 4px solid #30363d;
  color: #8b949e;
}
table {
  border-collapse: collapse;
  display: block;
  overflow-x: auto;
}
th, td {
  padding: 6px 13px;
  border: 1px solid #30363d;
}
tr:nth-child(2n) { background: #161b22; }
hr { border: 0; border-top: 1px solid #30363d; margin: 18px 0; }
ul, ol { padding-left: 1.6em; }
li + li { margin-top: 0.2em; }
.task-list-item { list-style: none; }
.task-list-item input { margin: 0 0.4em 0.2em -1.4em; }
* { scrollbar-width: thin; scrollbar-color: #6e7681 #21262d; }
::-webkit-scrollbar { width: 10px; height: 10px; }
::-webkit-scrollbar-track { background: #21262d; }
::-webkit-scrollbar-thumb { background: #6e7681; border-radius: 5px; }
::-webkit-scrollbar-thumb:hover { background: #8b949e; }
)CSS";

    std::string doc;
    doc.reserve(body.size() + 1024);
    doc += "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    if (base_href_utf8 && !base_href_utf8->empty()) {
        doc += "<base href=\"";
        append_html_escaped_attr(doc, *base_href_utf8);
        doc += "\">";
    }
    if (!title.empty()) {
        doc += "<title>";
        for (char c : title) {
            switch (c) {
                case '<': doc += "&lt;";  break;
                case '>': doc += "&gt;";  break;
                case '&': doc += "&amp;"; break;
                default:  doc += c;       break;
            }
        }
        doc += "</title>";
    }
    doc += "<style>";
    doc += (theme == HtmlTheme::Dark) ? kCssDark : kCssLight;
    append_page_color_override(doc, page_bg_colorref, page_fg_colorref);
    doc += "</style></head><body>";
    doc += body;
    doc += "</body></html>";
    return doc;
}

} // namespace html
