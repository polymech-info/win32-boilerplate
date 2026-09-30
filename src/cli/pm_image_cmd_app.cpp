#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_app.hpp"

#if defined(_WIN32)
#include "win/app_commands.hpp"
#include "win/session_replay/session_replay.hpp"
#include "win/ui_singleton.hpp"
#include "win/ui_next/helpers/text_conv.hpp"
#endif

int pm_image_cmd_app(CLI::App& app, PmImageCliState& st) {
#if defined(_WIN32)
    if (st.app_screenshot_cmd && st.app_screenshot_cmd->parsed()) {
#if defined(PM_IMAGE_CLI_ONLY)
        std::cerr << "app takescreenshot: unavailable in the console-only binary; run "
                  << pm::brand::k_app_id_u8 << " app takescreenshot instead.\n";
        return 1;
#else
        namespace fs = std::filesystem;
        fs::path out;
        if (!st.app_screenshot_output.empty()) {
            out = fs::path(st.app_screenshot_output);
            if (out.is_relative()) {
                std::error_code ec;
                out = fs::absolute(out, ec);
                if (ec) {
                    std::cerr << "app takescreenshot: could not resolve output path: " << st.app_screenshot_output
                              << " (" << ec.message() << ")\n";
                    return 1;
                }
            }
        } else {
            out = media::win::app_cmd::default_screenshots_dir() / media::win::app_cmd::make_screenshot_filename();
        }
        if (out.has_parent_path()) {
            std::error_code ec;
            fs::create_directories(out.parent_path(), ec);
        }
        const int ww = st.app_screenshot_window_w;
        const int wh = st.app_screenshot_window_h;
        if (ww < 0 || wh < 0) {
            std::cerr << "app takescreenshot: --window-width and --window-height must be non-negative.\n";
            return 1;
        }
        if ((ww > 0) != (wh > 0)) {
            std::cerr << "app takescreenshot: --window-width and --window-height must be set together (positive "
                         "outer size in pixels).\n";
            return 1;
        }
        pmui::set_splash_disabled_for_session(!st.show_startup_splash);
        const int rc = media::win::launch_ui_next_screenshot_probe(
            out.wstring(), media::win::app_cmd::k_app_cli_take_screenshot_wait_ms, ww, wh);
        if (rc == 0)
            std::cout << out.string() << "\n";
        return rc;
#endif
    }
    if (st.app_browse_cmd && st.app_browse_cmd->parsed()) {
        const std::string payload = std::string("browse|") + st.app_browse_paths;
        if (!media::win::forward_app_command_to_primary(payload)) {
            std::cerr << "app: no running " << pm::brand::k_app_id_u8
                      << " UI to receive `browse` (start the UI first by running `" << pm::brand::k_app_id_u8 << "`).\n";
            return 1;
        }
        return 0;
    }
#if FEATURE_COMMAND_REPLAY
    if (st.app_replay_cmd && st.app_replay_cmd->parsed()) {
        namespace fs = std::filesystem;
        fs::path    resolved;
        std::string rerr;
        if (!media::win::session_replay::resolve_session_replay_cli_input(st.app_replay_path, resolved, rerr)) {
            std::cerr << "app replay: " << rerr << "\n";
            return 1;
        }
        const std::string payload =
            std::string("replay|") + pmui::wide_to_utf8(resolved.wstring());
        if (!media::win::forward_app_command_to_primary(payload)) {
            std::cerr << "app: no running " << pm::brand::k_app_id_u8
                      << " UI to receive `replay` (start the UI first, or run `" << pm::brand::k_app_id_u8
                      << " replay --path=â€¦`).\n";
            return 1;
        }
        return 0;
    }
#endif
    std::string verb;
#if FEATURE_COMMAND_UI_COMMAND_CONTROL
    if (st.app_pause_cmd && st.app_pause_cmd->parsed())       verb = "pausebatch";
    if (st.app_resume_cmd && st.app_resume_cmd->parsed())      verb = "resumebatch";
    if (st.app_cancel_cmd && st.app_cancel_cmd->parsed())      verb = "cancelbatch";
#endif
#if FEATURE_COMMAND_REPLAY
    if (st.app_recordstart_cmd && st.app_recordstart_cmd->parsed())     verb = "recordstart";
    if (st.app_recordstop_cmd && st.app_recordstop_cmd->parsed())      verb = "recordstop";
#endif
#if defined(FEATURE_SESSION_VIDEO_RECORDER) && FEATURE_SESSION_VIDEO_RECORDER
    if (st.app_videorecordstart_cmd && st.app_videorecordstart_cmd->parsed()) verb = "videorecordstart";
    if (st.app_videorecordstop_cmd && st.app_videorecordstop_cmd->parsed())  verb = "videorecordstop";
    if (st.app_videorecordpause_cmd && st.app_videorecordpause_cmd->parsed()) verb = "videorecordpause";
#endif

    if (verb.empty()) {
        std::cerr << "app: no subcommand selected.\n";
        return 1;
    }
    if (!media::win::forward_app_command_to_primary(verb)) {
        std::cerr << "app: no running " << pm::brand::k_app_id_u8 << " UI to receive `" << verb
                  << "` (start the UI first by running `" << pm::brand::k_app_id_u8 << "`).\n";
        return 1;
    }
    return 0;
#else
    std::cerr << "media-img: app commands are only available on Windows.\n";
    return 1;
#endif
}

void pm_image_register_app(CLI::App& app, PmImageCliState& s) {
    const std::string app_subcmd_desc = std::string("Send a command to the running ") + pm::brand::k_app_id_u8 + " UI (Windows only).";
    s.app_cmd_grp = app.add_subcommand("app", app_subcmd_desc.c_str());
    int app_subcmd_count = 0;

    s.app_screenshot_cmd = s.app_cmd_grp->add_subcommand(
        "takescreenshot",
        "Start the main window, wait 10s, save a PNG, then exit (same flow as `test screenshot` with a "
        "fixed wait). Default file: <cwd>/screenshots/<timestamp>.png. "
        "Use -o for a custom path (relative paths are from cwd).");
    s.app_screenshot_cmd->add_option(
        "-o,--output", s.app_screenshot_output, "PNG path (default: <cwd>/screenshots/<timestamp>.png)");
    s.app_screenshot_cmd
        ->add_option("--window-width", s.app_screenshot_window_w,
                     "Outer main window width in pixels (use with --window-height; default: from saved layout)")
        ->default_val(0);
    s.app_screenshot_cmd
        ->add_option("--window-height", s.app_screenshot_window_h,
                     "Outer main window height in pixels (use with --window-width)")
        ->default_val(0);
    ++app_subcmd_count;

    s.app_browse_cmd = s.app_cmd_grp->add_subcommand(
        "browse",
        "Open path(s) in the running UI File tree and selection (semicolon-separated; Windows).");
    s.app_browse_cmd->add_option("paths", s.app_browse_paths, "Semicolon-separated absolute paths")->required();
    ++app_subcmd_count;

#if FEATURE_COMMAND_UI_COMMAND_CONTROL
    s.app_pause_cmd = s.app_cmd_grp->add_subcommand(
        "pausebatch",
        "Tell the running UI to pause the current batch after the current file.");
    s.app_resume_cmd = s.app_cmd_grp->add_subcommand(
        "resumebatch",
        "Tell the running UI to resume a paused batch.");
    s.app_cancel_cmd = s.app_cmd_grp->add_subcommand(
        "cancelbatch",
        "Tell the running UI to cancel the current batch.");
    app_subcmd_count += 3;
#endif
#if FEATURE_COMMAND_REPLAY
    s.app_recordstart_cmd = s.app_cmd_grp->add_subcommand(
        "recordstart",
        "Begin session JSON capture (Ctrl+R in the UI; see docs/session-replay.md).");
    s.app_recordstop_cmd = s.app_cmd_grp->add_subcommand(
        "recordstop",
        "Write session JSON and stop (Ctrl+H in the UI).");
    const std::string app_replay_desc = std::string("Apply session layout in the running UI (same as `") + pm::brand::k_app_id_u8 + " replay --path=…` for a new process).";
    s.app_replay_cmd = s.app_cmd_grp->add_subcommand("replay", app_replay_desc.c_str());
    s.app_replay_cmd->add_option(
        "--path", s.app_replay_path,
        "Session .json file (if missing as given: resolved under app profile dir, then profile/sessions/)")
        ->required();
    app_subcmd_count += 3;
#endif
#if defined(FEATURE_SESSION_VIDEO_RECORDER) && FEATURE_SESSION_VIDEO_RECORDER
    s.app_videorecordstart_cmd = s.app_cmd_grp->add_subcommand(
        "videorecordstart",
        "Start main-window video capture to Videos/session-HHMMSS.mp4 (Ctrl+Alt+R), or resume if recording is paused (Windows+session-video build).");
    s.app_videorecordstop_cmd = s.app_cmd_grp->add_subcommand(
        "videorecordstop",
        "Stop main-window video capture and finalize the MP4 (Ctrl+Alt+H; Windows+session-video build).");
    s.app_videorecordpause_cmd = s.app_cmd_grp->add_subcommand(
        "videorecordpause",
        "Toggle pause/resume while session video is recording (Ctrl+Alt+P; Windows+session-video build).");
    app_subcmd_count += 3;
#endif

    if (app_subcmd_count > 0)
        s.app_cmd_grp->require_subcommand(1);
}
