#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

#include <toml++/toml.hpp>

#include "logger/logger.h"
#include "postgres/postgres.h"
#include "json/json.h"


// ── E2E: Supabase connect via config/postgres.toml ──────────────────────────

TEST_CASE("E2E: connect to Supabase and ping", "[e2e][postgres]") {
  logger::init("e2e-test");

  // Read config — path relative to CWD (project root)
  auto cfg = toml::parse_file("config/postgres.toml");
  postgres::Config pg_cfg;
  pg_cfg.supabase_url = cfg["supabase"]["url"].value_or(std::string(""));
  pg_cfg.supabase_key =
      cfg["supabase"]["publishable_key"].value_or(std::string(""));

  REQUIRE(!pg_cfg.supabase_url.empty());
  REQUIRE(!pg_cfg.supabase_key.empty());

  postgres::init(pg_cfg);

  auto status = postgres::ping();
  logger::info("E2E ping result: " + status);
  CHECK(status == "ok");
}

TEST_CASE("E2E: query profiles table", "[e2e][postgres]") {
  logger::init("e2e-test");

  auto cfg = toml::parse_file("config/postgres.toml");
  postgres::Config pg_cfg;
  pg_cfg.supabase_url = cfg["supabase"]["url"].value_or(std::string(""));
  pg_cfg.supabase_key =
      cfg["supabase"]["publishable_key"].value_or(std::string(""));

  postgres::init(pg_cfg);

  auto result = postgres::query("profiles", "id,username", "", 3);
  logger::info("E2E query result: " + result);

  // Should be valid JSON array
  CHECK(json::is_valid(result));
}
