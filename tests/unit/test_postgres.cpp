#include <catch2/catch_test_macros.hpp>

#include "postgres/postgres.h"

// Unit tests use a no-op init — no network required
TEST_CASE("postgres::ping throws without init", "[postgres]") {
  // If called without init, should throw
  CHECK_THROWS(postgres::ping());
}
