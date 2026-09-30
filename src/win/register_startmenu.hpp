#pragma once

#include <string>

namespace media::win {

struct RegisterStartMenuOptions {
    std::string folder{"PolyMech"};
    bool unregister{false};
    bool dry{false};
    /** Empty = path of the running pm-image.exe. */
    std::string media_bin;
    /** Empty = inferred from media_bin; use when zip layout differs from dist/installer layout. */
    std::string install_root;
};

/** Windows only. Registers/removes current-user Start Menu shortcuts. Returns 0 on success. */
int register_startmenu_run(const RegisterStartMenuOptions& opt);

} // namespace media::win
