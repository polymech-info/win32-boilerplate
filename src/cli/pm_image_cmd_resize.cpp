#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_resize.hpp"

int pm_image_cmd_resize(CLI::App& app, PmImageCliState& st) {
    (void)app;
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    bool resize_ui_mode = false;
#endif
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
        if (st.resize_open_chat && !st.resize_ui_next) {
            std::cerr << "resize: --chat requires --ui-next\n";
            return 1;
        }
        struct UiSingletonOwner {
            bool mutex = false;
            bool bridge = false;
            ~UiSingletonOwner() {
                if (bridge)
                    media::win::destroy_ui_singleton_bridge();
                if (mutex)
                    media::win::release_ui_singleton_mutex();
            }
        } ui_singleton;
#endif
        media::ResizeOptions opt;
        std::string in_path_e;
        std::string out_path_e;

#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
        if (st.resize_ui_next) {
            std::vector<std::string> initial;
            if (!st.src_list.empty())
                initial = st.src_list;
            else if (!st.in_path.empty())
                initial.push_back(st.in_path);
            const std::string forward_seed = st.src_list.empty() ? st.in_path : media_cli::join_src_semicolons(st.src_list);
            if (!media::win::try_acquire_ui_singleton_mutex()) {
                if (!forward_seed.empty()) {
                    const std::string enc = std::string("browse|") + forward_seed;
                    if (!media::win::forward_app_command_to_primary(enc)) {
                        std::cerr << pm::brand::k_app_id_u8
                                  << ": could not forward paths to the running UI (start "
                                  << pm::brand::k_app_id_u8 << " first).\n";
                        return 1;
                    }
                }
                if (st.resize_open_chat) {
                    const std::string cmd =
                        forward_seed.empty() ? std::string("chat") : (std::string("chat|") + forward_seed);
                    if (!media::win::forward_app_command_to_primary(cmd)) {
                        std::cerr << "media-img: could not reach the running " << pm::brand::k_app_id_u8
                                  << " UI for --chat.\n";
                        return 1;
                    }
                }
                return 0;
            }
            media::win::release_ui_singleton_mutex();
            if (st.ui_reset)
                media::settings::set_ui_reset_session(true);
            pmui::set_splash_disabled_for_session(!st.show_startup_splash);
            return media::win::launch_ui_next(initial, st.resize_open_chat);
        }

        if (st.resize_ui) {
            if (!st.src_list.empty() && !st.in_path.empty()) {
                std::cerr << "resize: use either positional input or --src, not both\n";
                return 1;
            }
            {
                const std::string forward_seed = st.src_list.empty() ? st.in_path : media_cli::join_src_semicolons(st.src_list);
                if (!media::win::try_acquire_ui_singleton_mutex()) {
                    if (!forward_seed.empty()) {
                        if (!media::win::forward_resize_ui_paths_to_primary(forward_seed))
                            std::cerr << "media-img: could not reach the running resize UI (try again in a moment)\n";
                    } else {
                        std::cerr << "media-img: resize UI is already running\n";
                    }
                    return 0;
                }
                ui_singleton.mutex = true;
                if (!media::win::create_ui_singleton_bridge()) {
                    std::cerr << "media-img: could not create UI bridge window\n";
                    return 1;
                }
                ui_singleton.bridge = true;
            }
            resize_ui_mode = true;
            media::ResizeOptions initial{};
            initial.max_width = st.max_w;
            initial.max_height = st.max_h;
            initial.format = st.format;
            initial.fit = st.fit;
            initial.position = st.position;
            initial.kernel = st.kernel;
            initial.background = st.background;
            initial.quality = st.quality;
            initial.png_compression = st.png_compression;
            initial.rotate = st.rotate;
            initial.flip = st.flip;
            initial.flop = st.flop;
            initial.autorotate = !st.no_autorotate;
            initial.strip_metadata = !st.no_strip;
            initial.without_enlargement = !st.allow_enlargement;
            initial.cache_enabled = !st.resize_no_cache;
            initial.cache_dir = st.resize_cache_dir;
            initial.url_timeout_sec = st.url_timeout_sec;
            initial.url_max_redirects = st.url_max_redirects;

            std::string ui_in = st.src_list.empty() ? st.in_path : media_cli::join_src_semicolons(st.src_list);
            std::string ui_out = st.dst_flag.empty() ? st.out_path : st.dst_flag;
            if (!media::win::show_resize_ui(opt, ui_in, ui_out, initial))
                return 0;
            in_path_e = std::move(ui_in);
            out_path_e = std::move(ui_out);
        } else
#endif
        {
            const bool use_src_dst = !st.src_list.empty() || !st.dst_flag.empty();
            if (use_src_dst) {
                if (st.src_list.empty() || st.dst_flag.empty()) {
                    std::cerr << "resize: --src (one or more) and --dst must be used together\n";
                    return 1;
                }
                media::InputSelection rsel;
                rsel.add_from_cli_src_list(st.src_list);
                std::string v_err;
                if (!rsel.validate(v_err)) {
                    std::cerr << "resize: " << v_err << "\n";
                    return 1;
                }
                const std::vector<std::string> resolved = rsel.resolve(v_err, true);
                if (!v_err.empty()) {
                    std::cerr << v_err << "\n";
                    return 1;
                }
                std::string spec_err;
                in_path_e = media::InputSelection::to_expand_spec(resolved, spec_err);
                if (in_path_e.empty() && !spec_err.empty()) {
                    std::cerr << spec_err << "\n";
                    return 1;
                }
                out_path_e = st.dst_flag;
            } else if (st.in_path.empty()) {
                std::cerr << "resize: provide input (and output, or omit output to write under the current directory)\n";
                return 1;
            } else if (st.out_path.empty()) {
                std::string derr;
                out_path_e = media::default_output_path_for_resize(st.in_path, st.format, derr, {});
                if (out_path_e.empty()) {
                    std::cerr << derr << "\n";
                    return 1;
                }
                in_path_e = st.in_path;
            } else {
                in_path_e = st.in_path;
                out_path_e = st.out_path;
            }

            opt.max_width = st.max_w;
            opt.max_height = st.max_h;
            opt.format = st.format;
            opt.fit = st.fit;
            opt.position = st.position;
            opt.kernel = st.kernel;
            opt.background = st.background;
            opt.quality = st.quality;
            opt.png_compression = st.png_compression;
            opt.rotate = st.rotate;
            opt.flip = st.flip;
            opt.flop = st.flop;
            opt.autorotate = !st.no_autorotate;
            opt.strip_metadata = !st.no_strip;
            opt.without_enlargement = !st.allow_enlargement;
            opt.cache_enabled = !st.resize_no_cache;
            opt.cache_dir = st.resize_cache_dir;
            opt.url_timeout_sec = st.url_timeout_sec;
            opt.url_max_redirects = st.url_max_redirects;
        }

#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
        if (resize_ui_mode) {
            std::string xerr;
            const std::string expanded_in = media::expand_resize_ui_inputs(in_path_e, xerr);
            if (expanded_in.empty()) {
                std::cerr << xerr << "\n";
                return 1;
            }
            in_path_e = expanded_in;
        }
#endif

        if (out_path_e.empty()) {
            std::string derr;
            out_path_e = media::default_output_path_for_resize(in_path_e, opt.format, derr, opt.output_stem_suffix);
            if (out_path_e.empty() && !derr.empty()) {
                std::cerr << derr << "\n";
                return 1;
            }
        }

        std::string err;
        media::ResizeBatchResult batch;
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
        bool batch_ok;
        {
            std::string preview_err;
            auto jobs_preview =
                media::pair_resize_paths(in_path_e, out_path_e, preview_err,
                                         out_path_e.empty() ? &opt.format : nullptr,
                                         out_path_e.empty() ? &opt.output_stem_suffix : nullptr);
            if (!preview_err.empty()) {
                std::cerr << preview_err << "\n";
                return 1;
            }
            // Match meta/compress/transform: --no-gui forces console (no list-style job window).
            const bool use_job_window =
                !st.g_no_gui && (resize_ui_mode || jobs_preview.size() > 1 || st.resize_job_ui);
            if (use_job_window)
                batch_ok = media::win::run_resize_batch_with_job_ui(
                    in_path_e, out_path_e, opt, err, &batch, resize_ui_mode || st.resize_job_ui);
            else
                batch_ok = media::resize_batch(in_path_e, out_path_e, opt, err, &batch);
        }
#else
        const bool batch_ok = media::resize_batch(in_path_e, out_path_e, opt, err, &batch);
#endif
        if (!batch_ok) {
            std::cerr << err << "\n";
            return 1;
        }
        if (batch.count == 1)
            std::cout << out_path_e << "\n";
        else if (batch.count > 1)
            std::cout << batch.count << " file(s) written\n";
        return 0;}

void pm_image_register_resize(CLI::App& app, PmImageCliState& s) {
    s.resize_cmd = app.add_subcommand("resize", "Resize / transform an image (libvips, Sharp-like options)");
    s.resize_cmd->add_option("input", s.in_path, "Input path, glob (*, ?, **), or http(s):// URL")->required(false);
    s.resize_cmd->add_option(
        "output", s.out_path,
        "Output file/dir, or omit when there is exactly one input → write under cwd (sanitized name)");
    s.resize_cmd->add_option("--src", s.src_list, "Input (repeat for multiple); use with --dst; Explorer passes several files")
        ->expected(-1);
    s.resize_cmd->add_option("--dst", s.dst_flag, "Same as positional output; directory if multiple inputs");
    s.resize_cmd->add_option("--max-width", s.max_w, "Target / max width (0 = no limit)");
    s.resize_cmd->add_option("--max-height", s.max_h, "Target / max height (0 = no limit)");
    s.resize_cmd->add_option("--format", s.format, "Output format (default: from extension)");
    s.resize_cmd
        ->add_option("--fit", s.fit,
                     "inside|cover|contain|fill|outside — see Sharp resize.fit")
        ->default_val("inside");
    s.resize_cmd->add_option("--position", s.position, "For cover: centre|attention|entropy|…")->default_val("centre");
    s.resize_cmd->add_option("--kernel", s.kernel, "nearest|cubic|mitchell|lanczos2|lanczos3")->default_val("lanczos3");
    s.resize_cmd->add_option("-q,--quality", s.quality, "JPEG/WebP/AVIF quality 1–100")->default_val(85);
    s.resize_cmd->add_option("--png-compression", s.png_compression, "PNG DEFLATE 0–9")->default_val(6);
    s.resize_cmd->add_option("--background", s.background, "Letterbox colour #rrggbb (contain)");
    s.resize_cmd->add_option("--rotate", s.rotate, "Rotate 0|90|180|270 after EXIF autorotate")->default_val(0);
    s.resize_cmd->add_flag("--flip", s.flip, "Vertical flip");
    s.resize_cmd->add_flag("--flop", s.flop, "Horizontal flop");
    s.resize_cmd->add_flag("--no-autorotate", s.no_autorotate, "Disable EXIF orientation");
    s.resize_cmd->add_flag("--no-strip", s.no_strip, "Keep metadata on output");
    s.resize_cmd->add_flag("--allow-enlargement", s.allow_enlargement, "Allow upscaling (inside/contain/outside)");
    s.resize_cmd->add_flag("--no-cache", s.resize_no_cache, "Disable output cache (default: cache on)");
    s.resize_cmd->add_option("--cache-dir", s.resize_cache_dir, "Cache root (default: <cwd>/cache/images)");
    s.resize_cmd->add_option("--url-timeout", s.url_timeout_sec, "HTTP(S) fetch timeout (seconds, 0 = libcurl default)")
        ->default_val(5);
    s.resize_cmd
        ->add_option("--url-max-redirects", s.url_max_redirects, "Max redirects when fetching URL inputs")
        ->default_val(20);

#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    s.resize_cmd->add_flag("--ui", s.resize_ui,
                         "Windows: native dialog for paths/options; optional --src/--dst seed the dialog");
    s.resize_cmd->add_flag("--ui-next", s.resize_ui_next,
                         "Windows: new Win32++ ribbon UI with drag-drop queue and settings panel");
    s.resize_cmd->add_flag("--chat", s.resize_open_chat,
                         "With --ui-next: open Chat and use --src (or positional input) as agent context");
    s.resize_cmd->add_flag("--job-ui", s.resize_job_ui,
                         "List-style job window (pause / cancel) — Windows");
#endif
}