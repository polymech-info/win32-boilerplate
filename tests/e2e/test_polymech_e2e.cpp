#include <catch2/catch_test_macros.hpp>
#include <toml++/toml.hpp>

#include "logger/logger.h"
#include "polymech/polymech.h"
#include "postgres/postgres.h"
#include "json/json.h"

// ── E2E: fetch pages from live Supabase ─────────────────────────────────

TEST_CASE("E2E: fetch all pages", "[e2e]") {
  logger::init("e2e-polymech");

  // Load config
  auto cfg = toml::parse_file("config/postgres.toml");
  postgres::Config pg_cfg;
  pg_cfg.supabase_url = cfg["supabase"]["url"].value_or(std::string(""));
  pg_cfg.supabase_key =
      cfg["supabase"]["publishable_key"].value_or(std::string(""));

  REQUIRE(!pg_cfg.supabase_url.empty());
  REQUIRE(!pg_cfg.supabase_key.empty());

  postgres::init(pg_cfg);

  auto result = polymech::fetch_pages();

  // Should return valid JSON
  REQUIRE(json::is_valid(result));

  // Should be an array (even if empty)
  REQUIRE(result.front() == '[');
  REQUIRE(result.back() == ']');
}
