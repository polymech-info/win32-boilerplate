#pragma once

#include "core/resize.hpp"

#include <string>

namespace media::ipc {

/** TCP JSON line server for resize jobs (cross-platform). Returns 0 on error exit 0. */
int run_tcp_server(const std::string &host, int port, const CacheServerDefaults &cache_defaults);

#if !defined(_WIN32)
/** Unix domain socket (same JSON protocol). */
int run_unix_server(const std::string &path, const CacheServerDefaults &cache_defaults);
#endif

} // namespace media::ipc
