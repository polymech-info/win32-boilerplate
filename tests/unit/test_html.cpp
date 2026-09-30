#include <catch2/catch_test_macros.hpp>
#include <string>
#include <thread>
#include <vector>

#include "html/html.h"
#include "html/html2md.h"

// ═══════════════════════════════════════════════════════
// html::parse / html::select  (existing)
// ═══════════════════════════════════════════════════════

TEST_CASE("html::parse returns elements from valid HTML", "[html]") {
  auto elements =
      html::parse("<html><body><h1>Title</h1><p>Body</p></body></html>");

  REQUIRE(elements.size() >= 2);

  bool found_h1 = false;
  bool found_p = false;
  for (const auto &el : elements) {
    if (el.tag == "h1" && el.text == "Title")
      found_h1 = true;
    if (el.tag == "p" && el.text == "Body")
      found_p = true;
  }
  CHECK(found_h1);
  CHECK(found_p);
}

TEST_CASE("html::parse returns empty for empty input", "[html]") {
  auto elements = html::parse("");
  REQUIRE(elements.empty());
}

TEST_CASE("html::parse handles nested elements", "[html]") {
  auto elements = html::parse("<div><span>Nested</span></div>");

  bool found_span = false;
  for (const auto &el : elements) {
    if (el.tag == "span" && el.text == "Nested") {
      found_span = true;
    }
  }
  CHECK(found_span);
}

TEST_CASE("html::select finds elements by CSS selector", "[html][select]") {
  auto matches = html::select("<ul><li>A</li><li>B</li><li>C</li></ul>", "li");

  REQUIRE(matches.size() == 3);
  CHECK(matches[0] == "A");
  CHECK(matches[1] == "B");
  CHECK(matches[2] == "C");
}

TEST_CASE("html::select returns empty for no matches", "[html][select]") {
  auto matches = html::select("<p>Hello</p>", "h1");
  REQUIRE(matches.empty());
}

TEST_CASE("html::select works with class selector", "[html][select]") {
  auto matches = html::select(
      R"(<div><span class="a">X</span><span class="b">Y</span></div>)", ".a");

  REQUIRE(matches.size() == 1);
  CHECK(matches[0] == "X");
}

// ═══════════════════════════════════════════════════════
// html2md  — conversion & large-chunk robustness
// ═══════════════════════════════════════════════════════

TEST_CASE("html2md basic conversion", "[html2md]") {
  std::string md = html2md::Convert("<h1>Hello</h1><p>World</p>");
  CHECK(md.find("Hello") != std::string::npos);
  CHECK(md.find("World") != std::string::npos);
}

TEST_CASE("html2md empty input", "[html2md]") {
  std::string md = html2md::Convert("");
  CHECK(md.empty());
}

TEST_CASE("html2md whitespace-only input", "[html2md]") {
  std::string md = html2md::Convert("   \n\t  ");
  // Should return empty or whitespace — must not crash
  CHECK(md.size() < 20);
}

// ---------- large payload stress tests ----------

static std::string make_paragraphs(size_t count) {
  std::string html;
  html.reserve(count * 40);
  for (size_t i = 0; i < count; ++i) {
    html += "<p>Paragraph number ";
    html += std::to_string(i);
    html += " with some filler text.</p>\n";
  }
  return html;
}

static std::string make_large_html(size_t target_bytes) {
  // Build a chunk of roughly target_bytes by repeating a row
  const std::string row = "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor.</p>\n";
  std::string html;
  html.reserve(target_bytes + 256);
  html += "<html><body>";
  while (html.size() < target_bytes) {
    html += row;
  }
  html += "</body></html>";
  return html;
}

TEST_CASE("html2md handles 64KB HTML", "[html2md][large]") {
  auto html = make_large_html(64 * 1024);
  REQUIRE(html.size() >= 64 * 1024);
  std::string md = html2md::Convert(html);
  CHECK(!md.empty());
  CHECK(md.find("Lorem ipsum") != std::string::npos);
}

TEST_CASE("html2md handles 512KB HTML", "[html2md][large]") {
  auto html = make_large_html(512 * 1024);
  std::string md = html2md::Convert(html);
  CHECK(!md.empty());
}

TEST_CASE("html2md handles 1MB HTML", "[html2md][large]") {
  auto html = make_large_html(1024 * 1024);
  std::string md = html2md::Convert(html);
  CHECK(!md.empty());
}

TEST_CASE("html2md 10K paragraphs", "[html2md][large]") {
  auto html = make_paragraphs(10000);
  std::string md = html2md::Convert(html);
  CHECK(!md.empty());
  CHECK(md.find("Paragraph number 9999") != std::string::npos);
}

// ---------- deeply nested HTML ----------

TEST_CASE("html2md deeply nested divs (500 levels)", "[html2md][large]") {
  const int depth = 500;
  std::string html;
  for (int i = 0; i < depth; ++i) html += "<div>";
  html += "deep content";
  for (int i = 0; i < depth; ++i) html += "</div>";

  std::string md = html2md::Convert(html);
  CHECK(md.find("deep content") != std::string::npos);
}

// ---------- wide table ----------

TEST_CASE("html2md wide table (200 columns)", "[html2md][large]") {
  std::string html = "<table><tr>";
  for (int i = 0; i < 200; ++i) {
    html += "<td>C" + std::to_string(i) + "</td>";
  }
  html += "</tr></table>";

  std::string md = html2md::Convert(html);
  CHECK(!md.empty());
  CHECK(md.find("C0") != std::string::npos);
  CHECK(md.find("C199") != std::string::npos);
}

// ---------- concurrent conversion ----------

TEST_CASE("html2md concurrent conversions are thread-safe", "[html2md][threads]") {
  const int num_threads = 8;
  const std::string html = make_large_html(32 * 1024); // 32KB each
  std::vector<std::string> results(num_threads);
  std::vector<std::thread> threads;

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([&results, &html, i]() {
      results[i] = html2md::Convert(html);
    });
  }

  for (auto &t : threads) t.join();

  for (int i = 0; i < num_threads; ++i) {
    CHECK(!results[i].empty());
    CHECK(results[i].find("Lorem ipsum") != std::string::npos);
  }
}

// ═══════════════════════════════════════════════════════
// html2md  — malformed / faulty HTML robustness
// ═══════════════════════════════════════════════════════

TEST_CASE("html2md unclosed tags", "[html2md][faulty]") {
  std::string md = html2md::Convert("<p>Hello <b>bold <i>italic");
  CHECK(md.find("Hello") != std::string::npos);
  CHECK(md.find("bold") != std::string::npos);
}

TEST_CASE("html2md mismatched/overlapping tags", "[html2md][faulty]") {
  std::string md = html2md::Convert("<b>bold <i>both</b> italic</i>");
  CHECK(md.find("bold") != std::string::npos);
}

TEST_CASE("html2md broken attributes", "[html2md][faulty]") {
  std::string md = html2md::Convert(R"(<a href="http://example.com class="bad>Link</a>)");
  // must not crash — output may vary
  (void)md;
}

TEST_CASE("html2md bare text (no tags)", "[html2md][faulty]") {
  std::string md = html2md::Convert("Just plain text, no HTML at all.");
  CHECK(md.find("Just plain text") != std::string::npos);
}

TEST_CASE("html2md random binary noise", "[html2md][faulty]") {
  // Full 0-255 byte range — previously crashed on MSVC debug builds due to
  // signed char passed to isspace() without unsigned cast. Fixed in html2md.cpp.
  std::string noise(4096, '\0');
  for (size_t i = 0; i < noise.size(); ++i) {
    noise[i] = static_cast<char>((i * 131 + 17) % 256);
  }
  std::string md = html2md::Convert(noise);
  // No assertion on content — just survival
  (void)md;
}

TEST_CASE("html2md truncated document", "[html2md][faulty]") {
  std::string html = "<html><body><table><tr><td>Cell1</td><td>Cell2";
  // abruptly ends mid-table
  std::string md = html2md::Convert(html);
  CHECK(md.find("Cell1") != std::string::npos);
}

TEST_CASE("html2md script and style tags", "[html2md][faulty]") {
  std::string html = R"(
    <p>Before</p>
    <script>alert('xss');</script>
    <style>.foo { color: red; }</style>
    <p>After</p>
  )";
  std::string md = html2md::Convert(html);
  CHECK(md.find("Before") != std::string::npos);
  CHECK(md.find("After") != std::string::npos);
  // script/style content should be stripped
  CHECK(md.find("alert") == std::string::npos);
}

TEST_CASE("html2md null bytes in input", "[html2md][faulty]") {
  std::string html = "<p>Hello";
  html += '\0';
  html += "World</p>";
  // html2md may stop at null or handle it — must not crash
  std::string md = html2md::Convert(html);
  (void)md;
}

// ═══════════════════════════════════════════════════════
// html2md  — web scraper real-world edge cases
// ═══════════════════════════════════════════════════════

TEST_CASE("html2md UTF-8 multibyte (CJK, Arabic, emoji)", "[html2md][scraper]") {
  std::string html =
    "<h1>日本語テスト</h1>"
    "<p>مرحبا بالعالم</p>"
    "<p>Ñoño señor über straße</p>"
    "<p>Emoji: 🚀🔥💀👻 and 中文混合English</p>";
  std::string md = html2md::Convert(html);
  CHECK(md.find("Emoji") != std::string::npos);
}

TEST_CASE("html2md BOM prefix", "[html2md][scraper]") {
  // UTF-8 BOM (EF BB BF) prepended — common from Windows-origin pages
  std::string html = "\xEF\xBB\xBF<html><body><p>Content after BOM</p></body></html>";
  std::string md = html2md::Convert(html);
  CHECK(md.find("Content after BOM") != std::string::npos);
}

TEST_CASE("html2md entity soup", "[html2md][scraper]") {
  std::string html =
    "<p>Price: &euro;10 &amp; &lt;20&gt; items</p>"
    "<p>&nbsp;&nbsp;&nbsp;indented &mdash; dashes &ndash; more</p>"
    "<p>Bad entity: &notreal; and &#999999; and &#xZZZZ;</p>";
  std::string md = html2md::Convert(html);
  CHECK(md.find("Price") != std::string::npos);
}

TEST_CASE("html2md CDATA and comments", "[html2md][scraper]") {
  std::string html =
    "<p>Before</p>"
    "<!-- <script>alert('xss')</script> -->"
    "<![CDATA[This is raw <data> & stuff]]>"
    "<!-- multi\nline\ncomment -->"
    "<p>After</p>";
  std::string md = html2md::Convert(html);
  CHECK(md.find("Before") != std::string::npos);
  CHECK(md.find("After") != std::string::npos);
}

TEST_CASE("html2md deeply nested inline tags", "[html2md][scraper]") {
  // Real pages sometimes have insanely nested spans from WYSIWYG editors
  std::string html = "<p>";
  for (int i = 0; i < 100; ++i) html += "<span><b><i><em><strong>";
  html += "deep text";
  for (int i = 0; i < 100; ++i) html += "</strong></em></i></b></span>";
  html += "</p>";
  std::string md = html2md::Convert(html);
  // 100 layers of bold/italic produce tons of ** and * markers —
  // just verify no crash and non-empty output
  CHECK(!md.empty());
}

TEST_CASE("html2md huge single line (no newlines)", "[html2md][scraper]") {
  // Minified HTML — one giant line, 200KB
  std::string html;
  html.reserve(200 * 1024);
  html += "<html><body>";
  for (int i = 0; i < 5000; ++i) {
    html += "<div><span class=\"c" + std::to_string(i) + "\">item" +
            std::to_string(i) + "</span></div>";
  }
  html += "</body></html>";
  std::string md = html2md::Convert(html);
  CHECK(md.find("item0") != std::string::npos);
  CHECK(md.find("item4999") != std::string::npos);
}

TEST_CASE("html2md data URI in img src", "[html2md][scraper]") {
  std::string html =
    "<p>Before image</p>"
    "<img src=\"data:image/png;base64,iVBORw0KGgoAAAANSU"
    "hEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwAD"
    "hgGAWjR9awAAAABJRU5ErkJggg==\" alt=\"pixel\">"
    "<p>After image</p>";
  std::string md = html2md::Convert(html);
  CHECK(md.find("Before image") != std::string::npos);
  CHECK(md.find("After image") != std::string::npos);
}

TEST_CASE("html2md mixed Latin-1 and UTF-8 bytes", "[html2md][scraper]") {
  // Latin-1 encoded chars (0x80-0xFF) that are NOT valid UTF-8
  // Common when scraping pages with wrong charset declaration
  std::string html = "<p>caf\xe9 na\xefve r\xe9sum\xe9</p>";  // café naïve résumé in Latin-1
  std::string md = html2md::Convert(html);
  CHECK(md.find("caf") != std::string::npos);
}

TEST_CASE("html2md HTML with HTTP headers prepended", "[html2md][scraper]") {
  // Sometimes raw HTTP responses leak into scraper output
  std::string html =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Content-Length: 42\r\n"
    "\r\n"
    "<html><body><p>Real content</p></body></html>";
  std::string md = html2md::Convert(html);
  CHECK(md.find("Real content") != std::string::npos);
}

TEST_CASE("html2md Google Maps / Places markup soup", "[html2md][scraper]") {
  // Simplified version of real Google Places HTML with data attributes,
  // inline styles, aria labels, and deeply nested structure
  std::string html = R"(
    <div class="section-result" data-result-index="0" jsaction="pane.resultSection.click">
      <div class="section-result-title">
        <span><span>Müller's Büro & Café</span></span>
      </div>
      <div class="section-result-details">
        <span class="section-result-location">Königstraße 42, München</span>
        <span class="section-result-rating">
          <span aria-label="4.5 stars">★★★★☆</span>
          <span>(1,234)</span>
        </span>
      </div>
      <div style="display:none" aria-hidden="true">
        <script type="application/ld+json">{"@type":"LocalBusiness","name":"test"}</script>
      </div>
    </div>
  )";
  std::string md = html2md::Convert(html);
  CHECK(md.find("Café") != std::string::npos);
  CHECK(md.find("München") != std::string::npos);
}

// ═══════════════════════════════════════════════════════
// html2md  — output amplification & pathological input
// ═══════════════════════════════════════════════════════

TEST_CASE("html2md nested blockquotes (output amplification)", "[html2md][amplification]") {
  // Each <blockquote> nesting adds a ">" prefix per line in markdown.
  // 50 deep = each line gets 50 ">" prefixes — tests that output doesn't
  // explode exponentially.
  std::string html;
  for (int i = 0; i < 50; ++i) html += "<blockquote>";
  html += "<p>deep quote</p>";
  for (int i = 0; i < 50; ++i) html += "</blockquote>";
  auto md = html2md::Convert(html);
  // Output size should be reasonable — not exponential.
  // 50 levels * "> " prefix = ~100 chars + text < 1 KB
  CHECK(md.size() < 4096);
  CHECK(!md.empty());
}

TEST_CASE("html2md very long attribute value", "[html2md][amplification]") {
  // 1 MB href — tests ExtractAttributeFromTagLeftOf won't choke
  std::string long_url(1024 * 1024, 'A');
  std::string html = "<a href=\"" + long_url + "\">Click</a>";
  auto md = html2md::Convert(html);
  // Must survive without crash
  CHECK(!md.empty());
}

TEST_CASE("html2md 10K unclosed p tags", "[html2md][amplification]") {
  // Each unclosed <p> generates "\n\n" — tests that md_ doesn't
  // grow beyond reasonable bounds
  std::string html;
  html.reserve(50000);
  for (int i = 0; i < 10000; ++i) html += "<p>text";
  auto md = html2md::Convert(html);
  CHECK(!md.empty());
  // Should contain the text, output gets big but not catastrophic
  CHECK(md.find("text") != std::string::npos);
}

TEST_CASE("html2md output-to-input ratio check", "[html2md][amplification]") {
  // Verify that for normal, representative HTML, output is smaller
  // than input (html2md strips tags, so markdown should be leaner)
  std::string html;
  html.reserve(100 * 1024);
  html += "<html><body>";
  for (int i = 0; i < 1000; ++i) {
    html += "<div class=\"wrapper\"><p class=\"content\">Paragraph " +
            std::to_string(i) + " with some text.</p></div>\n";
  }
  html += "</body></html>";
  auto md = html2md::Convert(html);
  // Markdown should be smaller than HTML (we stripped all the divs/classes)
  CHECK(md.size() < html.size());
  CHECK(md.size() > 0);
}

TEST_CASE("html2md pathological repeated angle brackets", "[html2md][amplification]") {
  // Incomplete tags: lots of "<" without closing ">" — stresses tag parser
  std::string html(8192, '<');
  auto md = html2md::Convert(html);
  // Must not infinite-loop — just survive
  (void)md;
}

// ═══════════════════════════════════════════════════════
// html::md_to_html  — Markdown → HTML (md4c, GFM)
// ═══════════════════════════════════════════════════════

TEST_CASE("md_to_html: heading and paragraph", "[html][md]") {
  auto h = html::md_to_html("# Hello\n\nWorld");
  CHECK(h.find("<h1>Hello</h1>") != std::string::npos);
  CHECK(h.find("<p>World</p>")   != std::string::npos);
}

TEST_CASE("md_to_html: GFM table", "[html][md][gfm]") {
  auto h = html::md_to_html(
      "| Col A | Col B |\n"
      "|-------|-------|\n"
      "| a1    | b1    |\n"
      "| a2    | b2    |\n");
  CHECK(h.find("<table>") != std::string::npos);
  CHECK(h.find("<thead>") != std::string::npos);
  CHECK(h.find("<th>Col A</th>") != std::string::npos);
  CHECK(h.find("<td>a2</td>")     != std::string::npos);
}

TEST_CASE("md_to_html: fenced code block keeps language hint", "[html][md][gfm]") {
  auto h = html::md_to_html(
      "Snippet:\n"
      "\n"
      "```cpp\n"
      "int square(int x) { return x*x; }\n"
      "```\n");
  CHECK(h.find("<pre><code") != std::string::npos);
  CHECK(h.find("language-cpp") != std::string::npos);
  CHECK(h.find("int square") != std::string::npos);
}

TEST_CASE("md_to_html: inline formatting", "[html][md]") {
  auto h = html::md_to_html(
      "Use **bold** and *italic* and `code` and [link](https://example.com).\n");
  CHECK(h.find("<strong>bold</strong>")    != std::string::npos);
  CHECK(h.find("<em>italic</em>")          != std::string::npos);
  CHECK(h.find("<code>code</code>")        != std::string::npos);
  CHECK(h.find("href=\"https://example.com\"") != std::string::npos);
}

TEST_CASE("md_to_html: GFM strikethrough + task list", "[html][md][gfm]") {
  auto h = html::md_to_html(
      "~~done~~\n\n"
      "- [x] one\n"
      "- [ ] two\n");
  CHECK(h.find("<del>done</del>") != std::string::npos);
  CHECK(h.find("type=\"checkbox\"") != std::string::npos);
}

TEST_CASE("md_to_html: blockquote", "[html][md]") {
  auto h = html::md_to_html("> quoted\n");
  CHECK(h.find("<blockquote>") != std::string::npos);
  CHECK(h.find("quoted")        != std::string::npos);
}

TEST_CASE("md_to_html: empty input is benign", "[html][md]") {
  auto h = html::md_to_html("");
  CHECK((h.empty() || h == "\n"));
}

TEST_CASE("md_to_html: no_html option escapes raw HTML", "[html][md][safety]") {
  html::MdOptions o;
  o.no_html = true;
  auto h = html::md_to_html("<script>alert(1)</script> hi", o);
  CHECK(h.find("&lt;script&gt;") != std::string::npos);
  CHECK(h.find("<script>")        == std::string::npos);
}

TEST_CASE("md_to_html: large input survives", "[html][md][large]") {
  std::string big;
  big.reserve(64 * 1024);
  for (int i = 0; i < 4000; ++i) {
    big += "## Heading " + std::to_string(i) + "\n\nParagraph body line.\n\n";
  }
  auto h = html::md_to_html(big);
  CHECK(!h.empty());
  CHECK(h.find("Heading 3999") != std::string::npos);
}

TEST_CASE("wrap_markdown_html: full document scaffold", "[html][md]") {
  auto frag = html::md_to_html("# T\n\nB\n");
  auto full = html::wrap_markdown_html(frag, "Title");
  CHECK(full.find("<!DOCTYPE html>")    != std::string::npos);
  CHECK(full.find("<title>Title</title>") != std::string::npos);
  CHECK(full.find("<style>")             != std::string::npos);
  CHECK(full.find("<h1>T</h1>")          != std::string::npos);
}
