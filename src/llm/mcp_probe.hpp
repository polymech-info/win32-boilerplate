#pragma once

#include "polymech_export.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#ifndef FEATURE_MCP_CLIENT
#define FEATURE_MCP_CLIENT 1
#endif

namespace media::llm::mcp {

#if FEATURE_MCP_CLIENT
/**
 * Inspect profile `mcp.json`: path, parse status, and per-server MCP handshake (`initialize`) +
 * `tools/list` (names + descriptions). Used by `pm-image llm info` for diagnostics.
 * @param profile_config_dir Same root as portable `settings.json` / agent MCP (empty → default profile dir).
 */
POLYMECH_API nlohmann::json probe_mcp_config(const std::filesystem::path& profile_config_dir);
#else
inline nlohmann::json probe_mcp_config(const std::filesystem::path&)
{
    return nlohmann::json{
        {"skipped", true},
        {"disabled", true},
        {"note", "MCP client support is disabled in this build (FEATURE_MCP_CLIENT=OFF)."},
        {"servers", nlohmann::json::array()}};
}
#endif

} // namespace media::llm::mcp
