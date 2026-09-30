// Optional `dev.json` for first-run PM-Image shell layout (no Windows `settings_store` on macOS GUI).
#include "pm_dev_settings.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <limits.h>
#include <mach-o/dyld.h>

static std::string const& exe_dir_utf8() {
  static std::string s;
  static bool        init = false;
  if (!init) {
    char        buf[PATH_MAX];
    uint32_t     n = static_cast<uint32_t>(sizeof(buf));
    if (_NSGetExecutablePath(buf, &n) == 0) {
      std::string  p(buf);
      auto         pos = p.find_last_of('/');
      if (pos != std::string::npos) {
        s = p.substr(0, pos);
      }
    }
    init = true;
  }
  return s;
}

static bool read_file(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::in | std::ios::ate | std::ios::binary);
  if (!f) {
    return false;
  }
  const auto sz = f.tellg();
  f.seekg(0);
  if (sz <= 0) {
    return false;
  }
  out.assign(static_cast<size_t>(sz), '\0');
  f.read(out.data(), sz);
  return f.good() || f.eof();
}

bool pm_dev_load_gui_defaults(struct PmDevGuiDefaults& out, std::string& err) {
  err   = "";
  out.loaded  = false;
  std::string  raw;
  if (const char* env = std::getenv("PM_IMAGE_DEV_JSON")) {
    if (read_file(env, raw)) {
    } else {
      err  = std::string("PM_IMAGE_DEV_JSON not readable: ") + env;
    }
  }
  if (raw.empty()) {
    const std::string&         d  = exe_dir_utf8();
    const std::vector<std::string> candidates
        = { d + "/../../../../dist/dev.json", d + "/../../../dist/dev.json", d + "/../../dist/dev.json",
            d + "/../dev.json", d + "/dev.json" };
    for (const auto& c : candidates) {
      if (read_file(c, raw)) {
        break;
      }
    }
  }
  if (raw.empty()) {
    return false; // not an error — use struct defaults
  }
  try {
    const nlohmann::json j  = nlohmann::json::parse(raw, nullptr, false, true);
    if (j.is_discarded() || !j.is_object()) {
      err  = "dev.json parse";
      return false;
    }
    if (j.contains("win32_dock") && j["win32_dock"].is_object() && j["win32_dock"].contains("children")
        && j["win32_dock"]["children"].is_array()) {
      for (const auto& ch : j["win32_dock"]["children"]) {
        if (!ch.is_object()) {
          continue;
        }
        const int id  = ch.value("dockID", 0);
        if (id == 7) {
          out.explorer_width_pt = ch.value("dockSize", 240);
        } else if (id == 1) {
          out.log_height_pt  = ch.value("dockSize", 200);
        }
      }
    }
    out.loaded  = true;
  } catch (const std::exception& e) {
    err  = e.what();
    return false;
  }
  if (out.explorer_width_pt < 100) {
    out.explorer_width_pt  = 240;
  }
  if (out.log_height_pt < 64) {
    out.log_height_pt  = 120;
  }
  if (out.log_height_pt > 2000) {
    out.log_height_pt  = 200;
  }
  return out.loaded;
}
