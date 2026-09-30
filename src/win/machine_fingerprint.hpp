#pragma once

#include <string>

#if defined(_WIN32)

namespace media::win {

/// SHA256(MachineGuid UTF-8 + "|" + volume serial hex) as 64-char lowercase hex (same as trial inner `machine_hash`).
std::string machine_fingerprint_hex();

} // namespace media::win

#endif
