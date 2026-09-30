#ifndef PM_IMAGE_CLI_STATE_HPP
#define PM_IMAGE_CLI_STATE_HPP

#include "constants.hpp"
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

// All CLI11-bound option storage and subcommand / option handles for `pm-image`.
struct PmImageCliState {
    /// Global stderr spdlog level for this process (default info). Place before subcommand.
    std::string log_level = "info";
    bool        g_no_gui = false;
    /// Profile/config root for this process (settings, caches, MCP, skills, web state).
    std::string config_dir;
    /// Working directory for this process (affects log files, agent context, path resolution).
    std::string cwd;
    /// In-process MCP HTTP (loopback) for internal LLM tooling — default **on**; see docs/llm/mcp.md. Use `--no-mcp` or `PM_IMAGE_MCP=0` for CI.
    bool        mcp_enabled = false;
    int         mcp_port    = 4444;
#if defined(_WIN32)
    /// Embedded MCP bind address (default all interfaces on Windows; override with @c --mcp-bind).
    std::string mcp_bind = "0.0.0.0";
#else
    std::string mcp_bind = "127.0.0.1";
#endif
    std::string in_path;
    std::string out_path;
    std::vector<std::string> src_list;
    std::string dst_flag;
    int         max_w     = 0;
    int         max_h     = 0;
    std::string format;
    std::string fit         = "inside";
    std::string position    = "centre";
    std::string kernel      = "lanczos3";
    std::string background  = "#ffffff";
    int         quality         = 85;
    int         png_compression = 6;
    int         rotate = 0;
    bool        flip  = false;
    bool        flop  = false;
    bool        no_autorotate   = false;
    bool        no_strip        = false;
    bool        allow_enlargement = false;
    bool        resize_no_cache = false;
    std::string resize_cache_dir;
    int         url_timeout_sec   = 5;
    int         url_max_redirects = 20;
#if defined(_WIN32)
    /// Allocate a console for stdout/stderr (default off for Explorer / Open with / IExecute GUI launches).
    bool        win_console = false;
    /// Windows: keep a newShellWindow console open after the command finishes (launcher sets this).
    bool        shell_pause_on_exit = false;
    bool resize_ui      = false;
    bool resize_ui_next = false;
    bool resize_open_chat = false;
    bool resize_job_ui  = false;
    /// Top-level `pm-image --ui-chat` (no subcommand): stand-alone `CChatWebView` host.
    bool                     ui_chat = false;
    std::vector<std::string> ui_chat_src_list;
    /// No subcommand: @c --ui-preset=main|chat|viewer — one-shot workbench (overrides @c ui.workbench for this process).
    std::string ui_preset;
    /// With --ui-preset=viewer: force a specific viewer app (e.g., `agent-flow`).
    std::string ui_app;
    /// Windows UI launch: ignore persisted workbench window/dock/chrome (and file @c ui.workbench unless @c --ui-preset); use defaults.
    bool ui_reset = false;
    /// Windows UI launch: apply an exported layout document for this process only; does not write settings.json.
    std::string layout_override_path;
    /// Windows: show startup GDI+ splash for this launch (`--splash`; default off — same as `--no-splash`).
    bool show_startup_splash = false;
    /// Windows: top-level @c --settings — read app settings from this file for this process only (not @c settings import).
    std::string settings_read_path;
    /// Windows chat UI launch hint consumed by chat-next host args: `--mic=start` starts STT after the composer is ready.
    std::string ui_mic;
    /// With --ui-preset=chat: pre-fill the composer text area with this string on launch.
    std::string ui_prompt;
#endif

#if defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT
    // ── auto spy ────────────────────────────────────────────────────────────
    CLI::App* assistant_cmd     = nullptr;  ///< `auto` subcommand group
    CLI::App* assistant_spy_cmd = nullptr;  ///< `auto spy`
    CLI::App* assistant_app_inspect_cmd = nullptr; ///< `assistant app-inspect`
    CLI::App* assistant_app_inspect_dump_cmd = nullptr;
    CLI::App* assistant_app_inspect_screenshot_cmd = nullptr;
    CLI::App* assistant_app_use_cmd = nullptr; ///< `assistant app-use`
    CLI::App* assistant_app_use_open_cmd = nullptr;
    CLI::App* assistant_app_use_mouse_move_cmd = nullptr;
    CLI::App* assistant_app_use_click_cmd = nullptr;
    CLI::App* assistant_app_use_type_cmd = nullptr;
    CLI::App* assistant_app_use_hotkey_cmd = nullptr;
    CLI::App* assistant_app_use_cursor_cmd = nullptr;
    CLI::App* assistant_app_use_batch_cmd = nullptr;
    CLI::App* assistant_app_use_key_press_cmd = nullptr;
    int       asst_interval_ms      = 500;  ///< --interval-ms
    bool      asst_no_value         = false;///< --no-value
    bool      asst_no_selection     = false;///< --no-selection
    bool      asst_no_text          = false;///< --no-text
    bool      asst_log_all          = false;///< --all: log every tick
    bool      asst_verbose          = false;///< --verbose: print full text buffer
    bool      asst_continue_on_error = false;
    int       asst_text_max_chars   = 4096; ///< --text-max-chars
    bool      asst_json             = false;///< --json for one-shot computer-use commands
    bool      asst_md               = false;///< --md compact markdown for app-inspect dump
    bool      asst_probe_cells      = false;///< --probe-cells for virtualized spreadsheet cells
    bool      asst_foreground       = false;///< inspect/use foreground window only
    bool      asst_virtual          = false;///< click via target HWND messages, not cursor movement
    bool      asst_no_activate      = false;///< --no-activate: skip bringing target window to foreground before screenshot
    int       asst_pid              = 0;
    int       asst_hwnd             = 0;
    int       asst_element          = -1;
    int       asst_limit            = 500;
    int       asst_wait_ms          = 1000;
    int       asst_x                = 0;
    int       asst_y                = 0;
    int       asst_w                = 0;
    int       asst_h                = 0;
    int       asst_count            = 1;
    int       asst_api_width        = 0;
    int       asst_api_height       = 0;
    int       asst_quality          = 85;
    std::string asst_title;
    std::string asst_process;
    std::string asst_controls;
    std::string asst_exe;
    std::string asst_args;
    std::string asst_cwd;
    std::string asst_text;
    std::string asst_keys;
    std::string asst_key;             ///< single key name for app-use key-press / key-down / key-up
    int         asst_hold_ms   = 50;  ///< app-use key-press --hold-ms (note sustain)
    std::string asst_button;
    std::string asst_batch_file;
    std::string asst_output;
    std::string asst_rect;
    // ── auto spy --stt ──────────────────────────────────────────────────────
#  if defined(FEATURE_STT) && FEATURE_STT
    bool        asst_ui             = false;  ///< --ui: show assistant toolbar window
    bool        asst_stt            = false;  ///< --stt: live STT + write-back to focused element
    bool        asst_stt_live      = true;   ///< --stt-live / --no-stt-live: stream audio continuously (on) vs buffer-then-send (off)
    std::string asst_stt_provider;            ///< --stt-provider (e.g. "elevenlabs"; default: chat settings)
    std::string asst_stt_api_key;             ///< --stt-api-key
    int         asst_stt_silence_ms = 1200;   ///< --stt-silence-ms: VAD auto-commit threshold
#  endif
#endif

    CLI::App* settings_cmd = nullptr;
    CLI::App* daemon_cmd = nullptr;
    CLI::App* daemon_run_cmd = nullptr;
    CLI::App* daemon_tray_cmd = nullptr;
    CLI::App* daemon_register_cmd = nullptr;
    CLI::App* daemon_unregister_cmd = nullptr;
    CLI::App* daemon_stop_cmd = nullptr;
    CLI::App* daemon_path_cmd = nullptr;
    std::string daemon_config_path;
    bool daemon_elevated_write_only = false;
    bool daemon_tray = false;
    CLI::App* info_cmd = nullptr;
    CLI::App* info_commands_cmd = nullptr;
    CLI::App* info_xblox_cmd = nullptr;
    std::string info_dst;
    bool info_stdout = false;
    CLI::App* commands_cmd = nullptr;
    std::vector<CLI::App*> custom_command_cmds;
    std::string commands_path;
    bool commands_json = false;
#if defined(FEATURE_XBLOX) && FEATURE_XBLOX && defined(FEATURE_COMMAND_XBLOX) && FEATURE_COMMAND_XBLOX
    CLI::App* xblox_cmd = nullptr;
    CLI::App* xblox_run_cmd = nullptr;
    CLI::App* xblox_info_cmd = nullptr;
    std::string xblox_src;
    std::string xblox_commands_path;
    std::vector<std::string> xblox_extra_args;
    bool xblox_json = false;
    bool xblox_info_json = false;
    bool xblox_dry_run = false;
    bool xblox_no_wait = false;
    int xblox_max_loop_iterations = 1;
#endif
    CLI::App* settings_import_cmd = nullptr;
    CLI::App* settings_export_cmd = nullptr;
    CLI::App* settings_path_cmd = nullptr;
    std::string settings_import_path;
    std::string settings_export_path = "settings.json";
    /// Windows only: @c settings export --encrypted (PME1). Rejected on other OS.
    bool settings_export_encrypted = false;
    /// @c settings import/export --archive profile ZIP (skips web* cache dirs).
    bool settings_archive = false;

    std::string              cmp_input;
    std::string              cmp_output;
    std::vector<std::string> cmp_src_list;
    std::string              cmp_dst_flag;
    std::string              cmp_compressor;
    int         cmp_quality        = 85;
    bool        cmp_no_progressive = false;
    bool        cmp_optimize_scans = false;
    bool        cmp_trellis        = false;
    int         cmp_png_level   = 9;
    bool        cmp_quantize    = false;
    int         cmp_colors         = 256;
    int         cmp_quant_quality  = 85;
    bool        cmp_zopfli         = false;
    int         cmp_zopfli_iter    = 15;
    bool        cmp_no_strip = false;
    std::string cmp_suffix;
#if defined(_WIN32)
    bool cmp_job_ui = false;
#endif

    std::string              tf_input;
    std::string              tf_output;
    std::string              tf_prompt;
    std::string              tf_provider;
    std::string              tf_model;
    std::string              tf_api_key;
    std::string              tf_aspect;
    std::string              tf_size;
    std::vector<std::string> tf_refs;
    std::string              tf_preset_id;
    std::vector<std::string> tf_src_list;
    bool                     tf_json = false;
#if defined(_WIN32)
    bool tf_job_ui = false;
#endif

    std::string              cr_output;
    std::string              cr_prompt;
    std::string              cr_provider;
    std::string              cr_model;
    std::string              cr_api_key;
    std::string              cr_aspect;
    std::string              cr_size;
    std::vector<std::string> cr_refs;
    bool                     cr_json = false;

    std::vector<std::string> mt_inputs;
    std::string              mt_out_dir;
    std::string              mt_provider;
    std::string              mt_model;
    std::string              mt_api_key;
    std::string              mt_prompt;
    bool        mt_no_resize   = false;
    int         mt_resize_w    = 512;
    bool        mt_no_md       = false;
    bool        mt_no_json     = false;
    bool        mt_update_exif = false;
    bool        mt_dry_run     = false;
#if defined(_WIN32)
    bool mt_job_ui = false;
#endif

    // ── search ──────────────────────────────────────────────────────────────
    std::vector<std::string> srch_inputs;
    std::string              srch_query;
    /// "any" (default) | "image" — which files are candidates.
    std::string              srch_type             = "any";
    /// "own" (default) | "os" — walker / index source.
    std::string              srch_indexer          = "own";
    bool                     srch_grep             = false;
    bool                     srch_names_only       = false;
    bool                     srch_regex            = false;
    bool                     srch_case_sensitive   = false;
    bool                     srch_whole_word       = false;
    bool                     srch_no_recursive     = false;
    bool                     srch_include_hidden   = false;
    bool                     srch_follow_symlinks  = false;
    bool                     srch_no_skip_binary   = false;
    bool                     srch_dry_run          = false;
    bool                     srch_print_json       = false;
    int                      srch_max_results      = 0;
    int                      srch_context          = 0;   ///< Symmetric -C context.
    int                      srch_context_before   = 0;   ///< -B: context lines before match.
    int                      srch_context_after    = 0;   ///< -A: context lines after match.
    bool                     srch_multiline        = false; ///< --multiline: ^ / $ per line.
    /// --output-mode: "content" (default) | "files_with_matches" | "count".
    std::string              srch_output_mode      = "content";
    /// --type: rg-style type shorthand (js, cpp, py, …) expanded to include globs.
    std::string              srch_type_filter;
    int                      srch_head_limit       = 0;   ///< --head-limit: cap output N; 0 = unlimited.
    int                      srch_offset           = 0;   ///< --offset: skip first N entries.
    int64_t                  srch_max_file_size    = 0; ///< 0 = use default (256 MB).
    int                      srch_max_per_file     = 0; ///< 0 = use default (1000).
    /// --include: filename glob pattern(s) — only scan matching files (e.g. "*.cpp").
    std::vector<std::string> srch_include_globs;
    /// --exclude: skip files matching these glob patterns.
    std::vector<std::string> srch_exclude_globs;
    /// --exclude-dir: directory names to prune (default list used when empty).
    std::vector<std::string> srch_exclude_dirs;

    std::vector<std::string> fd_inputs;
    std::string              fd_prompt;
    bool        fd_llm            = false;
    bool        fd_case_sensitive = false;
    bool        fd_no_folders     = false;
    bool        fd_no_recursive   = false;
    bool        fd_bypass_cache   = false;
    bool        fd_no_generate    = false;
    bool        fd_no_md          = false;
    bool        fd_no_json        = false;
    bool        fd_no_exif        = false;
    int         fd_max_results    = 0;
    bool        fd_dry_run        = false;
    bool        fd_print_json     = false;
    std::string fd_provider;
    std::string fd_model;
    std::string fd_api_key;
    std::string fd_judge_prompt;
    std::string fd_meta_prompt;
    int         fd_resize_w  = 512;
    bool        fd_no_resize = false;
    bool        fd_local_text = false;
    std::vector<std::string> fd_refs;

    std::vector<std::string>   dup_inputs;
    std::string                dup_by = "fingerprint";
    bool                       dup_no_recursive = false;
    int                        dup_min_group    = 2;
    int                        dup_max_hamming  = 0;
    bool                       dup_fp_same_size = false;
    bool                       dup_no_md        = false;
    bool                       dup_no_json      = false;
    bool                       dup_no_exif      = false;
    std::string                dup_meta_prompt;
    bool                       dup_meta_json_llm      = false;
    bool                       dup_meta_json_implicit = false;
    int                        dup_meta_json_min_sim  = 7;
    std::string                dup_meta_json_cmp_prompt;
    std::string                dup_llm_router;
    std::string                dup_llm_model;
    std::string                dup_llm_api_key;
    std::string                dup_llm_base_url;
    int                        dup_llm_timeout_ms = 0;
    bool                       dup_print_json  = false;
    std::string                dup_report_md;
    std::string                dup_report_json;
    std::string                dup_load_session;
    std::string                dup_save_session;

    std::string host             = "127.0.0.1";
    int         port             = 8080;
    bool        serve_no_cache   = false;
    std::string serve_cache_dir;
    std::string ipc_host = "127.0.0.1";
    int         ipc_port = 9333;
    std::string ipc_unix;
    bool        ipc_no_cache = false;
    std::string ipc_cache_dir;

    std::string pm_provider        = "replicate";
    std::string pm_api_key;
    std::string pm_base_url;
    int         pm_limit = 0;
    std::string pm_cursor;
    std::string pm_sort_by;
    std::string pm_sort_direction;

    std::string              llm_call_name;
    std::string              llm_call_args_path;
    std::string              llm_call_image_file;
    std::string              ag_prompt;
    std::vector<std::string> ag_paths;
    std::string              ag_router;
    std::string              ag_model;
    std::string              ag_api_key;
    std::string              ag_base_url;
    /// LLM API type: "completion" (default, /chat/completions) or "responses" (/responses).
    std::string              ag_api_mode = pm::llm::k_default_api_mode;
    /// LLM streaming mode: auto (default), on, off.
    std::string              ag_streaming = "auto";
    int                      ag_timeout_ms = 0;
    int                      ag_max_iter   = 0;
    bool                     ag_json       = false;
    bool                     ag_dry         = false;
    bool                     ag_no_tools    = false;
    bool                     ag_godmode     = false;
    /// `llm agent`: when true, hydrate/store session memory across turns.
    bool                     ag_multi_turn  = true;
    /// `llm agent`: true when user explicitly passes --multi-turn or --single-turn.
    bool                     ag_multi_turn_explicit = false;
    /// `llm agent`: optional explicit session id for cross-process memory.
    std::string              ag_session_id;
    std::string              ag_disable_tools;
    std::string              ag_log_path;
    /// `llm agent`: after the first turn completes, start the global
    /// AgentScheduler in this process so `schedule_every` / `schedule_in`
    /// / `schedule_at` tasks created by the agent actually fire. Process
    /// blocks until SIGINT (Ctrl+C), --scheduler-timeout seconds elapse, or
    /// --scheduler-exit-when-idle is set and no enabled tasks remain.
    bool                     ag_scheduler           = false;
    /// `llm agent --scheduler`: auto-exit after this many seconds (0 = run
    /// until SIGINT or idle). Useful for tests and bounded play sessions.
    int                      ag_scheduler_timeout_s = 0;
    /// `llm agent --scheduler`: exit cleanly once every scheduled task is
    /// disabled (one-shots done, every-tasks hit max_runs or got cancelled).
    bool                     ag_scheduler_exit_when_idle = false;
    /// `llm agent`: auto | plain | render — terminal markdown on stdout (human mode only).
    std::string              ag_markdown = "auto";
    /// `llm agent`: auto | never | always — ANSI colors when markdown rendering is active.
    std::string              ag_color = "auto";
    bool                     llm_info_json = false;
    /// `llm info`: skip live MCP handshake / tools/list (fast; avoids broken local MCP servers).
    bool                     llm_info_no_mcp_probe = false;
    /// `llm info`: auto | plain | render — terminal markdown on stdout (human mode only).
    std::string              llm_info_markdown = "render";
    /// `llm info`: auto | never | always — ANSI colors when markdown rendering is active.
    std::string              llm_info_color = "auto";

    std::string reg_group     = "PM Media";
    bool        reg_unregister  = false;
    bool        reg_dry         = false;
    bool        reg_no_refresh  = false;
    bool        reg_elevated_write_only = false;
    std::string reg_media_bin;
    std::string reg_widths = "1980,1200";

    std::string startmenu_folder = "PolyMech";
    bool        startmenu_unregister = false;
    bool        startmenu_dry = false;
    std::string startmenu_media_bin;
    std::string startmenu_install_root;

    bool        installer_uninstall = false;
    bool        installer_dry = false;
    bool        installer_no_seed = false;
    bool        installer_no_explorer = false;
    bool        installer_no_startmenu = false;
    std::string installer_root;

    std::string app_chat_paths;
    std::string app_browse_paths;
    std::string app_replay_path;
    /// `app takescreenshot --output` (optional; relative paths → absolute from cwd).
    std::string app_screenshot_output;
    /// `app takescreenshot` outer main-window size in pixels; both 0 = use layout/saved placement.
    int         app_screenshot_window_w = 0;
    int         app_screenshot_window_h = 0;
#if defined(_WIN32)
    std::string test_screenshot_out;
    int         test_screenshot_wait_ms = 2000;
    std::string replay_session_path;
#endif
    std::string batch_discard_id;
    std::string batch_resume_id;
#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE)
    std::string lic_import_path;
#endif
    bool status_json = false;

#if defined(FEATURE_STT) && FEATURE_STT
    // ── audio info ──────────────────────────────────────────────────────────
    bool        audio_info_json          = false;

    // ── audio record ────────────────────────────────────────────────────────
    /// Destination WAV path; relative paths are resolved from cwd.
    std::string audio_record_dst;
    /// Capture device name (case-insensitive substring, from `audio info`); empty = system default.
    std::string audio_record_input;
    /// Stop after this many ms (0 = wait for Ctrl+C).
    int         audio_record_duration_ms = 0;
    /// Enable live/batch STT alongside recording (--stt). Without this flag, record WAV only.
    bool        audio_stt = false;
    /// STT provider for live transcription alongside recording (e.g. "elevenlabs").
    std::string audio_stt_provider;
    /// API key for the selected STT provider (also used for TTS playback).
    std::string audio_stt_api_key;
    /// TTS voice ID for real-time STT→TTS→speaker loop (empty = disabled).
    std::string audio_record_tts_voice_id;
    /// TTS model ID used when audio_record_tts_voice_id is set.
    std::string audio_record_tts_model_id = "eleven_v3";
    /// Auto-commit STT after this many ms of silence (0 = disabled; default 1500 ms).
    int         audio_record_silence_ms   = 1500;
    /// Optional path to write the full STT transcript as a UTF-8 text file (empty = skip).
    std::string audio_record_text_out;
    bool        audio_record_status_json = false;

    // ── audio play ──────────────────────────────────────────────────────────
    /// Path to audio file to play (MP3, WAV, FLAC). Relative paths resolved from cwd.
    std::string audio_play_path;
    /// Wait for playback to finish (blocking). Default false = fire-and-forget.
    bool        audio_play_wait = false;

    // ── audio tts ───────────────────────────────────────────────────────────
    std::string audio_tts_text;
    std::string audio_tts_dst;
    std::string audio_tts_voice_id = "tLK6fPv15M0oKv4V3ACR"; // "George"
    std::string audio_tts_model_id = "eleven_v3";
    std::string audio_tts_format;    // empty = auto-detect from --dst extension
    std::string audio_tts_provider;
    std::string audio_tts_api_key;
    bool        audio_tts_no_play = false;  // --no-play: skip speaker output

    // ── llm agent --mic (voice conversation loop) ───────────────────────────
    /// Use microphone as prompt source (continuous STT → LLM → TTS loop).
    bool        ag_mic              = false;
    /// STT provider for mic mode (default "elevenlabs").
    std::string ag_mic_stt_provider = "elevenlabs";
    /// ElevenLabs API key for STT (and TTS when --stt-voice-id is set).
    /// Falls back to ELEVENLABS_API_KEY env var.
    std::string ag_mic_stt_api_key;
    /// Input device name substring (empty = system default).
    std::string ag_mic_input;
    /// VAD silence duration for auto-commit (ms; 0 = disabled; default 1500).
    int         ag_mic_silence_ms   = 1500;
    /// ElevenLabs voice ID for speaking the LLM response aloud (empty = text-only).
    std::string ag_mic_voice_id;
    /// TTS model used when ag_mic_voice_id is set (default eleven_v3).
    std::string ag_mic_model_id     = "eleven_v3";
    /// Disable TTS playback in mic mode even if chat settings have TTS enabled.
    bool        ag_mic_no_tts       = false;

    CLI::App* audio_cmd        = nullptr;
    CLI::App* audio_info_cmd   = nullptr;
    CLI::App* audio_record_cmd = nullptr;
    CLI::App* audio_record_stop_cmd   = nullptr;
    CLI::App* audio_record_status_cmd = nullptr;
    CLI::App* audio_play_cmd   = nullptr;
    CLI::App* audio_tts_cmd    = nullptr;
#endif // FEATURE_STT

#if defined(FEATURE_VIDEO) && FEATURE_VIDEO
    // ── video info ──────────────────────────────────────────────────────────
    bool        video_info_json           = false;
    bool        video_info_modes          = false;
    /// Device name filter for `video info --modes` (empty = all devices).
    std::string video_info_input;

    // ── video image (still capture) ─────────────────────────────────────────
    /// Destination image path (.jpg / .png / .bmp); resolved from cwd.
    std::string video_image_dst;
    /// Capture device name (case-insensitive substring from `video info`); empty = first/default.
    std::string video_image_input;
    /// Preferred capture resolution (0 = device default).
    int         video_image_width         = 0;
    int         video_image_height        = 0;
    /// Mode index from `video info --modes` (-1 = use width/height instead).
    int         video_image_mode_idx      = -1;

    // ── video video (recording) ─────────────────────────────────────────────
    /// Destination AVI path; resolved from cwd.
    std::string video_video_dst;
    /// Capture device name for recording; empty = first/default.
    std::string video_video_input;
    /// Preferred capture resolution (0 = device default).
    int         video_video_width         = 0;
    int         video_video_height        = 0;
    /// Mode index from `video info --modes` (-1 = use width/height/fps instead).
    int         video_video_mode_idx      = -1;
    /// Frame rate for capture and AVI header (default 30).
    int         video_video_fps           = 30;
    /// Stop recording after this many ms (0 = wait for Ctrl+C).
    int         video_video_duration_ms   = 0;
    /// JPEG quality for MJPEG AVI frames (1-100, default 85).
    int         video_video_quality       = 85;
    bool        video_record_status_json  = false;

    CLI::App* video_cmd              = nullptr;
    CLI::App* video_info_cmd         = nullptr;
    CLI::App* video_image_cmd        = nullptr;
    CLI::App* video_record_cmd       = nullptr;
    CLI::App* video_record_stop_cmd  = nullptr;
    CLI::App* video_record_status_cmd = nullptr;
#endif // FEATURE_VIDEO

    CLI::App* search_cmd    = nullptr;
    CLI::App* resize_cmd    = nullptr;
    CLI::App* compress_cmd  = nullptr;
    CLI::App* transform_cmd = nullptr;
    CLI::App* create_cmd    = nullptr;
    CLI::App* meta_cmd      = nullptr;
    CLI::App* find_cmd      = nullptr;
    CLI::App* dup_cmd       = nullptr;
    CLI::App* serve_cmd     = nullptr;
    CLI::App* ipc_cmd       = nullptr;
    CLI::App* provider_cmd  = nullptr;
    CLI::App* provider_models_cmd     = nullptr;
    CLI::App* provider_models_list_cmd = nullptr;
    CLI::App* llm_cmd     = nullptr;
    CLI::App* llm_list_cmd = nullptr;
    bool llm_tools_list_path = false;
    CLI::App* llm_call_cmd = nullptr;
    CLI::App* llm_agent_cmd = nullptr;
    CLI::App* llm_info_cmd  = nullptr;
    CLI::App* llm_info_providers_cmd = nullptr;
    CLI::App* llm_info_models_cmd    = nullptr;
    std::string llm_info_models_provider;
    bool        llm_info_models_no_cache = false;
    CLI::App* llm_info_tools_cmd = nullptr;
    CLI::App* llm_info_skills_cmd = nullptr;
    CLI::App* reg_cmd   = nullptr;
    CLI::App* startmenu_cmd = nullptr;
    CLI::App* installer_cmd = nullptr;
    CLI::App* app_cmd_grp = nullptr;
    CLI::App* app_screenshot_cmd = nullptr;
    CLI::App* app_pause_cmd    = nullptr;
    CLI::App* app_resume_cmd   = nullptr;
    CLI::App* app_cancel_cmd   = nullptr;
    CLI::App* app_browse_cmd   = nullptr;
    CLI::App* app_recordstart_cmd = nullptr;
    CLI::App* app_recordstop_cmd  = nullptr;
    CLI::App* app_videorecordstart_cmd = nullptr;
    CLI::App* app_videorecordstop_cmd  = nullptr;
    CLI::App* app_videorecordpause_cmd = nullptr;
    CLI::App* app_replay_cmd   = nullptr;
#if defined(_WIN32)
    CLI::App* test_cmd           = nullptr;
    CLI::App* test_screenshot_cmd = nullptr;
    CLI::App* replay_cmd         = nullptr;
#endif
    CLI::App* login_cmd        = nullptr;
    CLI::App* logout_cmd       = nullptr;
    CLI::App* status_cmd       = nullptr;
    CLI::App* pixlwiz_status_cmd  = nullptr;
    bool      pixlwiz_status_json = false;
    bool      pixlwiz_status_log  = false;
    int       pixlwiz_status_log_days = 7;
    CLI::Option* login_decode_opt = nullptr;
    bool        login_probe        = false;
    bool        login_no_browser   = false;
    std::string login_decode_jwt_value;
    std::string login_issuer;
    std::string login_client_id;
    /// First TCP port to try for OAuth loopback (default 8844); must exist in ZITADEL redirect URIs for this client.
    int login_oauth_port = 8844;

    CLI::App* service_cmd        = nullptr;
    CLI::App* service_upload_cmd = nullptr;
    std::vector<std::string> service_upload_files;
    CLI::App* service_posts_cmd        = nullptr;
    CLI::App* service_posts_create_cmd = nullptr;
    std::vector<std::string> service_posts_create_files;
    std::string              service_post_title;
    std::string              service_post_description;
    /// POST /api/posts `settings.visibility`: `public`, `listed`, or `private`.
    std::string              service_post_visibility = "public";
    std::string              service_server_url;
    /// Include exact HTTP response bodies on stdout (for tests / contract capture).
    bool service_dump_raw_http = false;
#if defined(_WIN32)
    /// Windows: Explorer-style list window + post dialog (same as shell verb `Share to Pixlwiz…`).
    bool service_posts_create_job_ui = false;
#endif

    CLI::App* batch_cmd        = nullptr;
    CLI::App* batch_list_cmd   = nullptr;
    CLI::App* batch_discard_cmd = nullptr;
    CLI::App* batch_resume_cmd  = nullptr;
#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE)
    CLI::App* license_cmd         = nullptr;
    CLI::App* lic_fingerprint_cmd = nullptr;
    CLI::App* lic_import_cmd      = nullptr;
    CLI::App* lic_verify_cmd     = nullptr;
#endif
#if defined(_WIN32) && defined(FEATURE_TRIAL_CHECK)
    CLI::App* godmod_cmd    = nullptr;
    CLI::App* purgetrial_cmd = nullptr;
#endif

    CLI::Option* tf_provider_opt = nullptr;
    CLI::Option* tf_model_opt    = nullptr;
    CLI::Option* cr_provider_opt = nullptr;
    CLI::Option* cr_model_opt    = nullptr;
    CLI::Option* mt_provider_opt = nullptr;
    CLI::Option* mt_model_opt    = nullptr;
    CLI::Option* fd_provider_opt = nullptr;
    CLI::Option* fd_model_opt    = nullptr;
};

#endif
