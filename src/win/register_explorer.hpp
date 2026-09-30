#pragma once

#include <string>

namespace media::win {

struct RegisterExplorerOptions {
    std::string group{"PM Media"};
    bool unregister{false};
    bool dry{false};
    bool refresh_shell{true};
    /** Internal: this process was relaunched through UAC and should perform the writes directly. */
    bool elevated_write_only{false};
    /** Empty = use path of running pm-image.exe */
    std::string media_bin;
    /** Comma-separated positive integers */
    std::string widths{"1980,1200"};

    /**
     * Default: register `"%1"` for file/Directory (non-Background) verbs — on multi-select, Windows 10/11
     * typically runs the static handler once per file (reliable; matches cascaded `subCommands` behavior).
     * If true: register ` %*` instead (experimental) so one launch may receive all selected paths. Not
     * guaranteed in nested static menus; validate on your build. For robust batching use COM
     * (`IExplorerCommand`) or a small merge launcher.
     */
    bool explorer_experimental_all_selected{false};

};

/** Windows only. Returns 0 on success. */
int register_explorer_run(const RegisterExplorerOptions &opt);

} // namespace media::win
