#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_register_startmenu.hpp"

#if defined(_WIN32)
#include "win/register_startmenu.hpp"
#endif

int pm_image_cmd_register_startmenu(CLI::App& app, PmImageCliState& st) {
    (void)app;
#if defined(_WIN32)
    media::win::RegisterStartMenuOptions o;
    o.folder = st.startmenu_folder;
    o.unregister = st.startmenu_unregister;
    o.dry = st.startmenu_dry;
    o.media_bin = st.startmenu_media_bin;
    o.install_root = st.startmenu_install_root;
    return media::win::register_startmenu_run(o);
#else
    std::cerr << "media-img: register-startmenu is only available on Windows.\n";
    return 1;
#endif
}

void pm_image_register_startmenu(CLI::App& app, PmImageCliState& s) {
    s.startmenu_cmd = app.add_subcommand(
        "register-startmenu",
        "Register current-user Start Menu shortcuts for a zip/unpacked install.");
    s.startmenu_cmd->add_option("--folder", s.startmenu_folder)->default_val("PolyMech");
    s.startmenu_cmd->add_flag("--unregister", s.startmenu_unregister);
    s.startmenu_cmd->add_flag("--dry", s.startmenu_dry);
    const std::string media_bin_help = std::string("Path to ") + pm::brand::k_exe_basename_u8 + " (default: this executable)";
    s.startmenu_cmd->add_option("--media-bin", s.startmenu_media_bin, media_bin_help.c_str());
    s.startmenu_cmd->add_option(
        "--install-root",
        s.startmenu_install_root,
        "Install/zip root for docs and Uninstall.exe lookup (default: parent of win-x64, or exe directory).");
}
