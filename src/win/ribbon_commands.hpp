#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pm::cli {

struct RegisteredRibbonCommandInfo {
    const char* id;
    const char* label;
    bool available;
};

#if defined(_WIN32)
/// Ribbon command ids for custom-command pickers (reflects compile-time feature flags).
std::vector<RegisteredRibbonCommandInfo> registered_ribbon_commands();

/// Resolve a ribbonCommand string (case/underscore insensitive; accepts aliases) to IDC_CMD_*.
std::uint32_t ribbon_command_id_from_name(const std::string& name);
#else
inline std::vector<RegisteredRibbonCommandInfo> registered_ribbon_commands() { return {}; }
#endif

} // namespace pm::cli
