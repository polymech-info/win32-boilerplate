#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_register_explorer.hpp"

#if defined(_WIN32)
#include "win/register_explorer.hpp"
#endif

int pm_image_cmd_register_explorer(CLI::App& app, PmImageCliState& st) {
#if defined(_WIN32)
    media::win::RegisterExplorerOptions o;
    o.group = st.reg_group;
    o.unregister = st.reg_unregister;
    o.dry = st.reg_dry;
    o.refresh_shell = !st.reg_no_refresh;
    o.elevated_write_only = st.reg_elevated_write_only;
    o.media_bin = st.reg_media_bin;
    o.widths = st.reg_widths;
    return media::win::register_explorer_run(o);
#else
    std::cerr << "media-img: register-explorer is only available on Windows.\n";
    return 1;
#endif
}

void pm_image_register_explorer(CLI::App& app, PmImageCliState& s) {
    const std::string reg_explorer_subcmd_desc =
        "Register Windows Explorer menus: resize / convert / meta + Workbench + Viewer + Chat + Presets";
    s.reg_cmd = app.add_subcommand("register-explorer", reg_explorer_subcmd_desc.c_str());
    s.reg_cmd->add_option("--group", s.reg_group)->default_val("PM Media");
    s.reg_cmd->add_flag("--unregister", s.reg_unregister);
    s.reg_cmd->add_flag("--dry", s.reg_dry);
    s.reg_cmd->add_flag("--no-refresh-shell", s.reg_no_refresh);
    s.reg_cmd
        ->add_flag("--elevated-write-only", s.reg_elevated_write_only,
                   "Internal: elevated register-explorer writes only.")
        ->group("");
    const std::string reg_media_bin_help = std::string("Path to ") + pm::brand::k_exe_basename_u8 + " (default: this executable)";
    s.reg_cmd->add_option("--media-bin", s.reg_media_bin, reg_media_bin_help.c_str());
    s.reg_cmd->add_option("--widths", s.reg_widths)->default_val("1980,1200");
}
