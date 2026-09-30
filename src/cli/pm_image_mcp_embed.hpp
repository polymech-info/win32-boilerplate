#pragma once

struct PmImageCliState;

#ifndef FEATURE_MCP_SERVER
#define FEATURE_MCP_SERVER 1
#endif

namespace media::mcp_embed {

/// First candidate for the in-process MCP HTTP listener (see docs/llm/mcp.md).
inline constexpr int k_default_mcp_port = 4444;

#if FEATURE_MCP_SERVER
/// Called once after CLI11 parse. Records @c --mcp / @c --no-mcp, @c --mcp-bind, and @c --mcp-port.
void apply_after_cli_parse(const PmImageCliState& s);

/// Starts a background cpp-httplib server (once per process) when MCP is enabled: same JSON
/// routes as REST @c /v1/llm/tools/* plus path-mode @c /v1/llm/path-tools/* (see @c serve.cpp).
void start_embedded_mcp_if_enabled();

bool enabled() noexcept;
/// Port from `--mcp-port` (after CLI validation).
int configured_port() noexcept;
/// Bound TCP port after @ref start_embedded_mcp_if_enabled(); 0 if disabled or bind failed.
int effective_listen_port() noexcept;
#else
inline void apply_after_cli_parse(const PmImageCliState&) {}
inline void start_embedded_mcp_if_enabled() {}
inline bool enabled() noexcept { return false; }
inline int configured_port() noexcept { return 0; }
inline int effective_listen_port() noexcept { return 0; }
#endif

} // namespace media::mcp_embed
