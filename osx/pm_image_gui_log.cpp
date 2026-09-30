// Append-only `ui-log.log` beside the executable (matches Windows [cwd]/ui-log.log intent;
// here: directory of main bundle binary). Uses spdlog like `src/win/ui_next/ui_log_file.cpp`.
#include "pm_image_gui_log.hpp"

#include <filesystem>
#include <limits.h> // PATH_MAX (for _NSGetExecutablePath buffer)
#include <memory>
#include <string>

#include <mach-o/dyld.h>

#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace {

std::shared_ptr<spdlog::logger> g_file;

} // namespace

static std::string executable_dir_utf8() {
  char  buf[PATH_MAX];
  uint32_t n = static_cast<uint32_t>(sizeof(buf));
  if (_NSGetExecutablePath(buf, &n) != 0) {
    return {};
  }
  std::filesystem::path p(buf);
  return p.parent_path().string();
}

void pm_image_gui_log_init() {
  if (g_file) {
    return;
  }
  const std::string dir = executable_dir_utf8();
  if (dir.empty()) {
    return;
  }
  const std::string path = (std::filesystem::path(dir) / "ui-log.log").string();
  try {
    auto   sink
        = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, /* truncate */ false);
    g_file = std::make_shared<spdlog::logger>("pm-image-gui", std::move(sink));
    g_file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
    g_file->set_level(spdlog::level::info);
    g_file->flush_on(spdlog::level::info);
    g_file->info("----");
    g_file->info("log file: {}", path);
  } catch (...) {
    g_file.reset();
  }
}

void pm_image_gui_log_info(const char* utf8_line) {
  if (!g_file || utf8_line == nullptr) {
    return;
  }
  g_file->info("{}", utf8_line);
}
