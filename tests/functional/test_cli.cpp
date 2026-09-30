#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

#include <toml++/toml.hpp>

#include "html/html.h"
#include "logger/logger.h"
#include "postgres/postgres.h"

// ── Functional: full pipeline tests ─────────────────────────────────────────

TEST_CASE("Full pipeline: parse HTML and select", "[functional]") {
  const std::string input =
      "<html><body>"
      "<h1>Title</h1>"
      "<ul><li class=\"item\">A</li><li class=\"item\">B</li></ul>"
      "</body></html>";

  // Parse should find elements
  auto elements = html::parse(input);
  REQUIRE(!elements.empty());

  // Select by class should find 2 items
  auto items = html::select(input, ".item");
  REQUIRE(items.size() == 2);
  CHECK(items[0] == "A");
  CHECK(items[1] == "B");
}

TEST_CASE("Full pipeline: TOML config round-trip", "[functional]") {
  // Write a temp TOML file
  const std::string toml_content = "[server]\n"
                                   "host = \"localhost\"\n"
                                   "port = 8080\n"
                                   "\n"
                                   "[database]\n"
                                   "name = \"test_db\"\n";

  std::string tmp_path = "test_config_tmp.toml";
  {
    std::ofstream out(tmp_path);
    REQUIRE(out.is_open());
    out << toml_content;
  }

  // Parse it
  auto tbl = toml::parse_file(tmp_path);

  CHECK(tbl["server"]["host"].value_or("") == std::string("localhost"));
  CHECK(tbl["server"]["port"].value_or(0) == 8080);
  CHECK(tbl["database"]["name"].value_or("") == std::string("test_db"));

  // Serialize back
  std::ostringstream ss;
  ss << tbl;
  auto serialized = ss.str();
  CHECK(serialized.find("localhost") != std::string::npos);

  // Cleanup
  std::remove(tmp_path.c_str());
}

TEST_CASE("Full pipeline: logger + postgres integration", "[functional]") {
  REQUIRE_NOTHROW(logger::init("functional-test"));

  // Init with a dummy config (no real connection)
  postgres::Config cfg;
  cfg.supabase_url = "https://example.supabase.co";
  cfg.supabase_key = "test-key";
  REQUIRE_NOTHROW(postgres::init(cfg));

  REQUIRE_NOTHROW(logger::info("Functional test: postgres init ok"));
}
