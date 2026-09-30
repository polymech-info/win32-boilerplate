#pragma once

#include "core/resize.hpp"

#include <string>

namespace media::http {

/** Blocking HTTP server (cpp-httplib). Returns 0 on clean shutdown. */
int run_server(const std::string &host, int port, const CacheServerDefaults &cache_defaults);

} // namespace media::http
