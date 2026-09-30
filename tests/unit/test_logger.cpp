#include <catch2/catch_test_macros.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>

#include "logger/logger.h"

TEST_CASE("logger::init does not throw", "[logger]") {
  REQUIRE_NOTHROW(logger::init("test"));
}

TEST_CASE("logger functions do not throw after init", "[logger]") {
  logger::init("test");

  REQUIRE_NOTHROW(logger::info("info message"));
  REQUIRE_NOTHROW(logger::warn("warn message"));
  REQUIRE_NOTHROW(logger::error("error message"));
  REQUIRE_NOTHROW(logger::debug("debug message"));
  REQUIRE_NOTHROW(logger::trace("trace message"));
}

TEST_CASE("logger::init can be called multiple times", "[logger]") {
  REQUIRE_NOTHROW(logger::init("first"));
  REQUIRE_NOTHROW(logger::init("second"));
  REQUIRE_NOTHROW(logger::info("after re-init"));
}

TEST_CASE("logger::set_log_level normalizes and applies", "[logger]") {
    logger::init_stderr("lvl-test", "info");
    REQUIRE(spdlog::default_logger()->level() == spdlog::level::info);
    REQUIRE_NOTHROW(logger::set_log_level("OFF"));
    REQUIRE(spdlog::default_logger()->level() == spdlog::level::off);
    REQUIRE_NOTHROW(logger::set_log_level("DeBuG"));
    REQUIRE(spdlog::default_logger()->level() == spdlog::level::debug);
}

TEST_CASE("logger::init_stderr_and_file empty path matches stderr-only", "[logger]") {
    REQUIRE_NOTHROW(logger::init_stderr_and_file("stderr-file-test", "info", ""));
    REQUIRE_NOTHROW(logger::info("hello"));
}

TEST_CASE("logger::init_stderr_and_file writes append file", "[logger]") {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "pm_image_logger_unit_test.log";
    std::error_code ec;
    fs::remove(tmp, ec);
    REQUIRE_NOTHROW(logger::init_stderr_and_file("file-sink-test", "info", tmp.string()));
    REQUIRE_NOTHROW(logger::warn("unit test line"));
    logger::flush();
    REQUIRE(fs::is_regular_file(tmp));
    REQUIRE(fs::file_size(tmp) > 0);
    fs::remove(tmp, ec);
}
