#include "pm_image_register_cli.hpp"

#include "constants.hpp"
#include "core/settings_runtime.hpp"
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <sstream>

#include "pm_image_cmd_resize.hpp"
#include "pm_image_cmd_compress.hpp"
#include "pm_image_cmd_transform.hpp"
#include "pm_image_cmd_create.hpp"
#include "pm_image_cmd_meta.hpp"
#include "pm_image_cmd_find.hpp"
#include "pm_image_cmd_search.hpp"
#include "pm_image_cmd_dup.hpp"
#if defined(FEATURE_SERVE) && FEATURE_SERVE
#include "pm_image_cmd_serve.hpp"
#endif
#if defined(FEATURE_IPC) && FEATURE_IPC
#include "pm_image_cmd_ipc.hpp"
#endif
#include "pm_image_cmd_provider_models.hpp"
#include "pm_image_cmd_llm_agent.hpp"
#include "pm_image_cmd_info.hpp"
#include "pm_image_cmd_register_explorer.hpp"
#include "pm_image_cmd_register_startmenu.hpp"
#include "pm_image_cmd_installer.hpp"
#include "pm_image_cmd_app.hpp"
#include "pm_image_cmd_service.hpp"
#include "pm_image_cmd_batch.hpp"
#include "pm_image_cmd_login.hpp"
#include "pm_image_cmd_status.hpp"
#include "pm_image_settings.hpp"
#include "pm_image_cmd_daemon.hpp"

#if defined(FEATURE_XBLOX) && FEATURE_XBLOX && defined(FEATURE_COMMAND_XBLOX) && FEATURE_COMMAND_XBLOX
#include "pm_image_cmd_xblox.hpp"
#endif

#if defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT
#include "pm_image_cmd_assistant.hpp"
#endif

#if defined(FEATURE_STT) && FEATURE_STT && defined(FEATURE_COMMAND_AUDIO) && FEATURE_COMMAND_AUDIO
#include "pm_image_cmd_audio.hpp"
#endif

#if defined(FEATURE_VIDEO) && FEATURE_VIDEO && defined(FEATURE_COMMAND_VIDEO) && FEATURE_COMMAND_VIDEO
#include "pm_image_cmd_video.hpp"
#endif

namespace pm::cli {

namespace {

std::string trim_ascii(std::string s)
{
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
    const auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t b = 0;
    while (b < s.size() && is_ws(static_cast<unsigned char>(s[b])))
        ++b;
    std::size_t e = s.size();
    while (e > b && is_ws(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

std::string json_string(const nlohmann::json& o, const char* key)
{
    return o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string{};
}

std::string action_label(const nlohmann::json& o)
{
    if (const std::string v = json_string(o, "appCommand"); !v.empty())
        return "app:" + v;
    if (const std::string v = json_string(o, "cliCommand"); !v.empty())
        return "cli:" + v;
    if (const std::string v = json_string(o, "ribbonCommand"); !v.empty())
        return "ribbon:" + v;
    if (const std::string v = json_string(o, "url"); !v.empty())
        return "url";
    if (const std::string v = json_string(o, "path"); !v.empty())
        return "path";
    if (o.contains("externalCommand") && o["externalCommand"].is_object()) {
        const auto& ext = o["externalCommand"];
        if (!json_string(ext, "command").empty())
            return "external";
        if (json_string(ext, "mode") == "shell" && !json_string(ext, "shellLine").empty())
            return "external:shell";
    }
    return "metadata";
}

void collect_visible_custom_items(const nlohmann::json& items,
                                  const std::string& group,
                                  std::vector<CustomCommandInfo>& out)
{
    if (!items.is_array())
        return;
    for (const auto& item : items) {
        if (!item.is_object())
            continue;
        if (item.value("enabled", true) == false || item.value("visible", true) == false)
            continue;
        const std::string type = item.value("type", std::string{"button"});
        if (type != "separator") {
            const std::string id = json_string(item, "id");
            const std::string label = json_string(item, "label");
            if (!id.empty() || !label.empty()) {
                out.push_back(CustomCommandInfo{
                    group,
                    id,
                    label.empty() ? id : label,
                    type,
                    action_label(item),
                });
            }
        }
        collect_visible_custom_items(item.value("items", nlohmann::json::array()), group, out);
    }
}

} // namespace

// ── Runtime command filter (stub for user/admin settings) ─────────────────────
// TODO: Load from settings.json or environment to allow runtime disabling
// of commands that were enabled at compile time. This enables use cases like:
// - Admin policies restricting certain operations
// - Trial/demo mode with limited commands
// - Feature flags via remote config
bool is_command_enabled_at_runtime(Cmd c) {
    // Stub: always enabled for now
    // Future: check s.settings_.command_policy or similar
    (void)c;  // Unused for now
    return true;
}

std::vector<RegisteredCommandInfo> registered_cli_commands()
{
    return {
        {Cmd::Resize, "resize", "Resize", cmd_available_at_compile_time(Cmd::Resize)},
        {Cmd::Compress, "compress", "Compress", cmd_available_at_compile_time(Cmd::Compress)},
        {Cmd::Transform, "transform", "Transform", cmd_available_at_compile_time(Cmd::Transform)},
        {Cmd::Create, "create", "Create", cmd_available_at_compile_time(Cmd::Create)},
        {Cmd::Meta, "meta", "Meta", cmd_available_at_compile_time(Cmd::Meta)},
        {Cmd::Find, "find", "Find", cmd_available_at_compile_time(Cmd::Find)},
        {Cmd::Search, "search", "Search", cmd_available_at_compile_time(Cmd::Search)},
        {Cmd::Duplicates, "duplicates", "Duplicates", cmd_available_at_compile_time(Cmd::Duplicates)},
        {Cmd::Serve, "serve", "Serve", cmd_available_at_compile_time(Cmd::Serve)},
        {Cmd::Ipc, "ipc", "IPC", cmd_available_at_compile_time(Cmd::Ipc)},
        {Cmd::Settings, "settings", "Settings", cmd_available_at_compile_time(Cmd::Settings)},
        {Cmd::ProviderModels, "provider", "Provider models", cmd_available_at_compile_time(Cmd::ProviderModels)},
        {Cmd::Llm, "llm", "LLM", cmd_available_at_compile_time(Cmd::Llm)},
        {Cmd::RegisterExplorer, "register-explorer", "Register Explorer", cmd_available_at_compile_time(Cmd::RegisterExplorer)},
        {Cmd::RegisterStartMenu, "register-startmenu", "Register Start Menu", cmd_available_at_compile_time(Cmd::RegisterStartMenu)},
        {Cmd::Installer, "installer", "Installer helper", cmd_available_at_compile_time(Cmd::Installer)},
        {Cmd::App, "app", "App Commands", cmd_available_at_compile_time(Cmd::App)},
        {Cmd::Service, "service", "Service", cmd_available_at_compile_time(Cmd::Service)},
        {Cmd::Batch, "batch", "Batch", cmd_available_at_compile_time(Cmd::Batch)},
        {Cmd::Login, "login", "Login", cmd_available_at_compile_time(Cmd::Login)},
        {Cmd::Audio, "audio", "Audio Capture / TTS / STT", cmd_available_at_compile_time(Cmd::Audio)},
        {Cmd::Video, "video", "Video Capture", cmd_available_at_compile_time(Cmd::Video)},
#if FEATURE_XBLOX
        {Cmd::Xblox, "xblox", "XBlox", cmd_available_at_compile_time(Cmd::Xblox)},
#endif
        {Cmd::Test, "test", "Test", cmd_available_at_compile_time(Cmd::Test)},
        {Cmd::License, "license", "License", cmd_available_at_compile_time(Cmd::License)},
        {Cmd::Status, "status", "Status", cmd_available_at_compile_time(Cmd::Status)},
        {Cmd::Commands, "commands", "List registered commands", cmd_available_at_compile_time(Cmd::Commands)},
        {Cmd::Daemon, "daemon", "Global shortcut daemon", cmd_available_at_compile_time(Cmd::Daemon)},
        {Cmd::Assistant, "assistant", "AI assistant / UIA spy + STT dictation", cmd_available_at_compile_time(Cmd::Assistant)},
        {Cmd::Info, "info", "Generated agent skill references", cmd_available_at_compile_time(Cmd::Info)},
    };
}

std::vector<RegisteredAppCommandInfo> registered_app_commands()
{
    std::vector<RegisteredAppCommandInfo> out;
#if !defined(_WIN32)
    return out;
#else
    auto push = [&](const char* id, const char* label) {
        out.push_back({id, label, true});
    };
#if defined(FEATURE_CHAT_WEB) && FEATURE_CHAT_WEB
    push("chat", "Chat");
#endif
    push("takescreenshot", "Take screenshot");
    push("browse", "Browse");
#if FEATURE_COMMAND_UI_COMMAND_CONTROL
    push("pausebatch", "Pause batch");
    push("resumebatch", "Resume batch");
    push("cancelbatch", "Cancel batch");
#endif
#if FEATURE_COMMAND_REPLAY
    push("recordstart", "Start session record");
    push("recordstop", "Stop session record");
    push("replay", "Session replay");
#endif
#if defined(FEATURE_SESSION_VIDEO_RECORDER) && FEATURE_SESSION_VIDEO_RECORDER
    push("videorecordstart", "Start session video");
    push("videorecordstop", "Stop session video");
    push("videorecordpause", "Toggle session video pause");
#endif
    return out;
#endif
}

std::vector<CustomCommandInfo> visible_custom_commands()
{
    std::vector<CustomCommandInfo> out;
#if FEATURE_CUSTOM_COMMANDS
    std::string raw, err;
    if (!media::runtime_settings::load_command_json_utf8(raw, err) || raw.empty())
        return out;
    try {
        const auto doc = nlohmann::json::parse(trim_ascii(std::move(raw)));
        const auto groups = doc.value("ribbon", nlohmann::json::object()).value("groups", nlohmann::json::array());
        if (!groups.is_array())
            return out;
        for (const auto& group : groups) {
            if (!group.is_object())
                continue;
            const std::string group_label = json_string(group, "label").empty()
                ? json_string(group, "id")
                : json_string(group, "label");
            collect_visible_custom_items(group.value("items", nlohmann::json::array()), group_label, out);
        }
    } catch (...) {
    }
#endif
    return out;
}

std::string registered_cli_commands_help_text()
{
    std::ostringstream oss;
    oss << "\nRegistered commands:\n";
    for (const auto& c : registered_cli_commands()) {
        oss << "  " << (c.id ? c.id : "");
        if (c.label && c.label[0] != 0)
            oss << " - " << c.label;
        if (!c.available)
            oss << " (disabled in this build)";
        oss << "\n";
    }
    oss << "\nCustom Commands:\n";
    const auto custom = visible_custom_commands();
    if (custom.empty()) {
        oss << "  (none enabled/visible in commands.json)\n";
    } else {
        std::string last_group;
        for (const auto& c : custom) {
            if (c.group != last_group) {
                last_group = c.group;
                if (!last_group.empty())
                    oss << "  [" << last_group << "]\n";
            }
            oss << "  " << (c.id.empty() ? c.label : c.id);
            if (!c.label.empty() && c.label != c.id)
                oss << " - " << c.label;
            if (!c.action.empty())
                oss << " (" << c.action << ")";
            oss << "\n";
        }
    }
    return oss.str();
}

// Combined check: compile-time AND runtime
template<Cmd C>
constexpr bool cmd_enabled_full(CmdFlags flags) noexcept {
    // Compile-time check first (zero runtime cost if false)
    if constexpr (!cmd_available_at_compile_time(C)) {
        return false;
    }
    // Runtime bitmask check
    return cmd_enabled(flags, C);
}

} // namespace pm::cli

void pm_image_register_cli(CLI::App& app, PmImageCliState& s, pm::cli::CmdFlags cmds) {
    using pm::cli::Cmd;
    using pm::cli::cmd_enabled;
    using pm::cli::cmd_enabled_full;
    using pm::cli::is_command_enabled_at_runtime;

    // Root-level extras are tolerated for no-subcommand UI launches. Parsed subcommands
    // still validate their own arguments unless they explicitly allow extras.
    app.allow_extras();

    // ── Global options (always registered) ────────────────────────────────────
    app.add_option(
            "--log-level", s.log_level,
            "Global stderr log level for this process (spdlog): trace, debug, info, warn, error, critical, off. "
            "Applies to all subcommands. Default: info.")
        ->default_val("info")
        ->check(CLI::IsMember(
            {"trace", "debug", "info", "warn", "warning", "error", "err", "critical", "off", "none"},
            CLI::ignore_case));
    app.add_flag(
        "--no-gui", s.g_no_gui,
        "Windows: do not open the list-style job window for batch (use console; for scripts, tests, CI). "
        "Place before the subcommand (e.g. `pm-image --no-gui meta a.jpg b.jpg`).");
    app.add_option(
        "--config-dir", s.config_dir,
        "Profile/config root for this process. Affects settings.json, model caches, MCP config, skills, "
        "OAuth tokens, WebView state, and other app profile files.");
    app.add_option(
            "--cwd", s.cwd,
            "Working directory for this process. Affects log file locations (pm-image.log, agent-*.json), "
            "project-local path lookup and agent folder context. Default: current directory. "
            "With --src paths, --cwd defaults to the parent of the first file/folder.")
        ->default_val(".");
    app.add_option(
        "--commands", s.commands_path,
        "Read custom commands from this commands.json file for this process only. "
        "Relative paths are resolved from cwd. Useful for tests and portable command sets.");
#if defined(FEATURE_MCP_SERVER) && FEATURE_MCP_SERVER
    app.add_flag(
        "--mcp,--no-mcp", s.mcp_enabled,
        "Enable the embedded MCP HTTP listener for this process (default: on). "
        "Bind host defaults to 0.0.0.0 on Windows and 127.0.0.1 elsewhere; set --mcp-bind to override. "
        "Env PM_IMAGE_MCP=0|false|off|no or =1|true|on|yes overrides this flag for CI.");
    app.add_option(
            "--mcp-bind", s.mcp_bind,
            "Interface/address for embedded MCP HTTP. Default: 0.0.0.0 on Windows (all interfaces), "
            "127.0.0.1 on other OS (loopback). Use 127.0.0.1 on Windows for loopback-only.");
    app.add_option(
            "--mcp-port", s.mcp_port,
            "First TCP port to try for embedded MCP on --mcp-bind (default 4444; next free port if busy)")
        ->default_val(4444)
        ->check(CLI::Range(1, 65535));
#endif
#if defined(_WIN32)
    app.add_flag(
        "--console", s.win_console,
        "Windows: attach or allocate a console for stdout/stderr (default: off — GUI launches from Explorer, "
        "Open with, shell verbs, etc. do not open a console window).");
    app.add_flag(
        "--pause-on-exit", s.shell_pause_on_exit,
        "Windows: wait for Enter before exiting (used by newShellWindow when closeOnExit is false).");
    app.add_option(
            "--src", s.ui_chat_src_list,
            "With --ui-chat, or with --ui-preset and no subcommand: input path(s), repeat; `;` in a quoted value for several")
        ->expected(-1);
    app.add_option(
            "--ui-preset", s.ui_preset,
            "No subcommand: workbench `main` (full tools), `chat` (chat-first), or `viewer` (fast image preview + menu/status, optional Explorer). "
            "Pair with top-level --src to seed; overrides `ui.workbench` for this run.")
        ->check(CLI::IsMember({"main", "chat", "viewer"}));
    app.add_option(
            "--mic", s.ui_mic,
            "With --ui-preset=chat: pass a chat launch hint. `start` auto-starts STT when the composer is ready.")
        ->check(CLI::IsMember({"start", "off"}, CLI::ignore_case));
    app.add_option(
            "--prompt", s.ui_prompt,
            "With --ui-preset=chat: pre-fill the composer with this text on launch.");
    app.add_option(
            "--app", s.ui_app,
            "With --ui-preset=viewer: force a specific viewer app. "
            "`agent-flow` opens agent.json files in the node flow visualizer. "
            "If omitted, the viewer selects based on file extension.");
    app.add_flag(
        "--ui-reset", s.ui_reset,
        "Windows: this launch ignores workbench window layout, dock JSON, and chrome from settings.json (built-in "
        "defaults). `ui.workbench` is ignored unless combined with --ui-preset. Does not modify the file.");
    app.add_option(
        "--layout", s.layout_override_path,
        "Windows: no subcommand only. Apply an exported layout JSON for this process without importing/persisting it. "
        "Uses the file's workbench unless --ui-preset overrides it.");
    app.add_flag(
        "--splash,--no-splash", s.show_startup_splash,
        "Windows: startup splash when opening the native UI (default: off). Use --splash to show; --no-splash is explicit off.");
    app.add_option(
        "--settings", s.settings_read_path,
        "Windows: read app settings from this file for this process only (PME1 or UTF-8 JSON). "
        "Relative paths are resolved from cwd. Does not import into the profile — use `settings import` to persist.");
#endif

    // ── Per-command registration (gated by compile-time + runtime flags) ──────
    // Compile-time check via if constexpr: disabled commands generate no code

    if constexpr (FEATURE_COMMAND_RESIZE) {
        if (cmd_enabled_full<Cmd::Resize>(cmds) && is_command_enabled_at_runtime(Cmd::Resize))
            pm_image_register_resize(app, s);
    }
    if constexpr (FEATURE_COMMAND_COMPRESS) {
        if (cmd_enabled_full<Cmd::Compress>(cmds) && is_command_enabled_at_runtime(Cmd::Compress))
            pm_image_register_compress(app, s);
    }
    if constexpr (FEATURE_COMMAND_TRANSFORM) {
        if (cmd_enabled_full<Cmd::Transform>(cmds) && is_command_enabled_at_runtime(Cmd::Transform))
            pm_image_register_transform(app, s);
    }
    if constexpr (FEATURE_COMMAND_CREATE) {
        if (cmd_enabled_full<Cmd::Create>(cmds) && is_command_enabled_at_runtime(Cmd::Create))
            pm_image_register_create(app, s);
    }
    if constexpr (FEATURE_COMMAND_META) {
        if (cmd_enabled_full<Cmd::Meta>(cmds) && is_command_enabled_at_runtime(Cmd::Meta))
            pm_image_register_meta(app, s);
    }
    if constexpr (FEATURE_COMMAND_FIND) {
        if (cmd_enabled_full<Cmd::Find>(cmds) && is_command_enabled_at_runtime(Cmd::Find))
            pm_image_register_find(app, s);
    }
    if constexpr (FEATURE_COMMAND_SEARCH) {
        if (cmd_enabled_full<Cmd::Search>(cmds) && is_command_enabled_at_runtime(Cmd::Search))
            pm_image_register_search(app, s);
    }
    if constexpr (FEATURE_COMMAND_DUPLICATES) {
        if (cmd_enabled_full<Cmd::Duplicates>(cmds) && is_command_enabled_at_runtime(Cmd::Duplicates))
            pm_image_register_dup(app, s);
    }
#if defined(FEATURE_SERVE) && FEATURE_SERVE
    if constexpr (FEATURE_COMMAND_SERVE) {
        if (cmd_enabled_full<Cmd::Serve>(cmds) && is_command_enabled_at_runtime(Cmd::Serve))
            pm_image_register_serve(app, s);
    }
#endif
#if defined(FEATURE_IPC) && FEATURE_IPC
    if constexpr (FEATURE_COMMAND_IPC) {
        if (cmd_enabled_full<Cmd::Ipc>(cmds) && is_command_enabled_at_runtime(Cmd::Ipc))
            pm_image_register_ipc(app, s);
    }
#endif
    if constexpr (FEATURE_COMMAND_PROVIDER_MODELS) {
        if (cmd_enabled_full<Cmd::ProviderModels>(cmds) && is_command_enabled_at_runtime(Cmd::ProviderModels))
            pm_image_register_provider_models(app, s);
    }
    if constexpr (FEATURE_COMMAND_LLM) {
        if (cmd_enabled_full<Cmd::Llm>(cmds) && is_command_enabled_at_runtime(Cmd::Llm))
            pm_image_register_llm(app, s);
    }
    if constexpr (FEATURE_REGISTER_EXPLORER) {
        if (cmd_enabled_full<Cmd::RegisterExplorer>(cmds) && is_command_enabled_at_runtime(Cmd::RegisterExplorer))
            pm_image_register_explorer(app, s);
        if (cmd_enabled_full<Cmd::RegisterStartMenu>(cmds) && is_command_enabled_at_runtime(Cmd::RegisterStartMenu))
            pm_image_register_startmenu(app, s);
        if (cmd_enabled_full<Cmd::Installer>(cmds) && is_command_enabled_at_runtime(Cmd::Installer))
            pm_image_register_installer(app, s);
    }
    if constexpr (FEATURE_COMMAND_APP) {
        if (cmd_enabled_full<Cmd::App>(cmds) && is_command_enabled_at_runtime(Cmd::App))
            pm_image_register_app(app, s);
    }
    if constexpr (FEATURE_COMMAND_SERVICE) {
        if (cmd_enabled_full<Cmd::Service>(cmds) && is_command_enabled_at_runtime(Cmd::Service))
            pm_image_register_service(app, s);
    }
#if defined(FEATURE_XBLOX) && FEATURE_XBLOX && defined(FEATURE_COMMAND_XBLOX) && FEATURE_COMMAND_XBLOX
    {
        if (cmd_enabled_full<Cmd::Xblox>(cmds) && is_command_enabled_at_runtime(Cmd::Xblox))
            pm_image_register_xblox(app, s);
    }
#endif
    if constexpr (FEATURE_DAEMON) {
        if (cmd_enabled_full<Cmd::Daemon>(cmds) && is_command_enabled_at_runtime(Cmd::Daemon))
            pm_image_register_daemon(app, s);
    }
#if defined(FEATURE_ASSISTANT) && FEATURE_ASSISTANT
    if constexpr (FEATURE_ASSISTANT) {
        if (cmd_enabled_full<Cmd::Assistant>(cmds) && is_command_enabled_at_runtime(Cmd::Assistant))
            pm_image_register_assistant(app, s);
    }
#endif
    if constexpr (FEATURE_COMMAND_BATCH) {
        if (cmd_enabled_full<Cmd::Batch>(cmds) && is_command_enabled_at_runtime(Cmd::Batch))
            pm_image_register_batch(app, s);
    }

#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
    if (cmd_enabled_full<Cmd::Login>(cmds) && is_command_enabled_at_runtime(Cmd::Login))
        pm_image_register_login(app, s);
#endif

    if constexpr (FEATURE_COMMAND_STATUS) {
        if (cmd_enabled_full<Cmd::Status>(cmds) && is_command_enabled_at_runtime(Cmd::Status))
            pm_image_register_status(app, s);
    }

#if defined(FEATURE_STT) && FEATURE_STT && defined(FEATURE_COMMAND_AUDIO) && FEATURE_COMMAND_AUDIO
    if (cmd_enabled_full<Cmd::Audio>(cmds) && is_command_enabled_at_runtime(Cmd::Audio))
        pm_image_register_audio(app, s);
#endif

#if defined(FEATURE_VIDEO) && FEATURE_VIDEO && defined(FEATURE_COMMAND_VIDEO) && FEATURE_COMMAND_VIDEO
    if (cmd_enabled_full<Cmd::Video>(cmds) && is_command_enabled_at_runtime(Cmd::Video))
        pm_image_register_video(app, s);
#endif

    if constexpr (FEATURE_COMMAND_SETTINGS) {
        if (cmd_enabled_full<Cmd::Settings>(cmds) && is_command_enabled_at_runtime(Cmd::Settings))
            pm_image_register_settings(app, s);
    }

#if defined(_WIN32)
    // ── Test / Replay (small; Win32 dev harness) ──────────────────────────────
    if constexpr (FEATURE_COMMAND_TEST) {
        if (cmd_enabled_full<Cmd::Test>(cmds) && is_command_enabled_at_runtime(Cmd::Test)) {
            s.test_cmd = app.add_subcommand(
                "test",
                "Developer harness: UI probes (see also `app` for live-instance commands).");
            s.test_cmd->require_subcommand(1);
            s.test_screenshot_cmd = s.test_cmd->add_subcommand(
                "screenshot",
                "Start the main window, wait, capture it to a PNG, then exit.");
            s.test_screenshot_cmd->add_option("-o,--output", s.test_screenshot_out, "PNG output path")->required();
            s.test_screenshot_cmd
                ->add_option("--wait-ms", s.test_screenshot_wait_ms,
                             "Milliseconds to wait after UI init before capture (default: 2000)")
                ->default_val(2000);
            const std::string replay_subcmd_desc = std::string("Start the UI and apply snapshot.window_layout from a session JSON. "
                "For a running instance use: ") + pm::brand::k_app_id_u8 + " app replay --path=… (see docs/session-replay.md).";
            s.replay_cmd = app.add_subcommand("replay", replay_subcmd_desc.c_str());
            s.replay_cmd->add_option(
                "--path", s.replay_session_path,
                "Session .json file (if missing as given: resolved under app profile dir, then profile/sessions/)")
                ->required();
        }
    }
#endif

#if defined(_WIN32) && defined(FEATURE_LICENSE_FILE)
    if constexpr (FEATURE_COMMAND_LICENSE) {
        if (cmd_enabled_full<Cmd::License>(cmds) && is_command_enabled_at_runtime(Cmd::License)) {
            s.license_cmd = app.add_subcommand(
                "license",
                "Offline activation (license.dat or license.json; see docs/license-server.md)");
            s.license_cmd->require_subcommand(1);
            s.lic_fingerprint_cmd =
                s.license_cmd->add_subcommand("fingerprint",
                                            "Print this machine's fingerprint (hex) for the issuer portal");
            const std::string lic_import_subcmd_desc = std::string("Copy license.dat (issuer) or license.json into %APPDATA%\\")
                                                        + pm::brand::k_config_subpath_u8 + "\\";
            s.lic_import_cmd = s.license_cmd->add_subcommand("import", lic_import_subcmd_desc.c_str());
            s.lic_import_cmd->add_option("path", s.lic_import_path)->required(true);
            s.lic_verify_cmd = s.license_cmd->add_subcommand(
                "verify", "Check that the installed license verifies on this machine (exit 0/1)");
        }
    }
#endif

#if defined(_WIN32) && FEATURE_TRIAL_CHECK
    if constexpr (FEATURE_COMMAND_STATUS) {
        s.godmod_cmd = app.add_subcommand("godmod", "");
        s.godmod_cmd->silent();
        s.purgetrial_cmd = app.add_subcommand("purgetrial", "");
        s.purgetrial_cmd->silent();
    }
#endif

    if constexpr (FEATURE_COMMAND_COMMANDS) {
        if (cmd_enabled_full<Cmd::Commands>(cmds) && is_command_enabled_at_runtime(Cmd::Commands)) {
            s.commands_cmd = app.add_subcommand(
                "commands",
                "List registered pm-image CLI commands for UI/custom-command pickers and scripts.");
            s.commands_cmd->add_flag("--json", s.commands_json, "Print command metadata as JSON.");
            s.commands_cmd->footer(pm::cli::registered_cli_commands_help_text());
        }
    }

    if constexpr (FEATURE_COMMAND_INFO) {
        if (cmd_enabled_full<Cmd::Info>(cmds) && is_command_enabled_at_runtime(Cmd::Info))
            pm_image_register_info(app, s);
    }

#if FEATURE_CUSTOM_COMMANDS
    for (const auto& c : pm::cli::visible_custom_commands()) {
        if (c.id.empty())
            continue;
        const std::string desc = (c.label.empty() ? std::string{"Custom command"} : c.label)
            + ". Appends extra args to the configured command; use `--` before args that should not be consumed by the wrapper.";
        auto* custom_cmd = app.add_subcommand(c.id, desc);
        custom_cmd->allow_extras();
        custom_cmd->prefix_command();
        s.custom_command_cmds.push_back(custom_cmd);
    }
#endif
}
