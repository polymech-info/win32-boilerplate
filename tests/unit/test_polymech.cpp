#include "polymech/polymech.h"
#include "postgres/postgres.h"
#include <catch2/catch_test_macros.hpp>


// Unit test — no network required
TEST_CASE("polymech::fetch_pages throws without init", "[polymech]") {
  // postgres::init has not been called, so fetch_pages should throw
  REQUIRE_THROWS(polymech::fetch_pages());
}
