#ifndef MEDIA_IMG_VERSION
#define MEDIA_IMG_VERSION "0.1.0"
#endif

#include "pm_image_run.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <laserpants/dotenv/dotenv.h>
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include "logger/logger.h"
#include "constants.hpp"
#include <cstdio>
#include "core/cli_cancel.hpp"
#include "cli_tty.hpp"
#include "core/settings_runtime.hpp"
#include "core/settings_store.hpp"
#include "pm_image_register_cli.hpp"
#include "pm_image_dispatch.hpp"
#include "pm_image_mcp_embed.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <cstring>
#if !defined(PM_IMAGE_CLI_ONLY)
#include "win/ui_next/ui_log_file.hpp"
#include "win/ui_next/helpers/splash_window.hpp"
#endif
#endif

namespace {

bool has_arg(int argc, char** argv, const char* needle)
{
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && std::strcmp(argv[i], needle) == 0)
            return true;
    }
    return false;
}

CLI::App* find_subcommand_path(CLI::App& app, int argc, char** argv)
{
    CLI::App* cur = &app;
    CLI::App* matched = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        const std::string token = argv[i];
        if (token == "--help" || token == "-h" || token == "--json")
            continue;
        if (!token.empty() && token[0] == '-') {
            if (matched)
                break;
            continue;
        }
        CLI::App* next = cur->get_subcommand_no_throw(token);
        if (!next) {
            if (!matched)
                continue;
            break;
        }
        cur = next;
        matched = cur;
    }
    return matched;
}

nlohmann::json option_to_json(const CLI::Option* opt)
{
    nlohmann::json names = nlohmann::json::array();
    for (const auto& n : opt->get_snames())
        names.push_back("-" + n);
    for (const auto& n : opt->get_lnames())
        names.push_back("--" + n);

    return nlohmann::json{
        {"name", opt->get_name(opt->get_positional(), true)},
        {"names", std::move(names)},
        {"positional", opt->get_positional()},
        {"required", opt->get_required()},
        {"group", opt->get_group()},
        {"description", opt->get_description()},
        {"typeName", opt->get_type_name()},
        {"default", opt->get_default_str()},
        {"expectedMin", opt->get_expected_min()},
        {"expectedMax", opt->get_expected_max()},
        {"allowExtraArgs", opt->get_allow_extra_args()},
    };
}

bool maybe_print_command_help_json(CLI::App& app, int argc, char** argv)
{
    if (argc < 3 || !argv[1])
        return false;
    const bool wants_help = has_arg(argc, argv, "--help") || has_arg(argc, argv, "-h");
    const bool wants_json = has_arg(argc, argv, "--json");
    if (!wants_help || !wants_json)
        return false;

    CLI::App* sub = find_subcommand_path(app, argc, argv);
    if (!sub)
        return false;

    nlohmann::json options = nlohmann::json::array();
    for (const auto* opt : sub->get_options())
        options.push_back(option_to_json(opt));

    nlohmann::json subcommands = nlohmann::json::array();
    for (const auto* child : sub->get_subcommands({})) {
        if (!child || child->get_name().empty())
            continue;
        subcommands.push_back({
            {"name", child->get_name()},
            {"description", child->get_description()},
        });
    }

    std::cout << nlohmann::json{
        {"name", sub->get_name()},
        {"description", sub->get_description()},
        {"allowExtras", sub->get_allow_extras()},
        {"options", std::move(options)},
        {"subcommands", std::move(subcommands)},
    }.dump(2) << "\n";
    return true;
}

std::string early_option_value(int argc, char** argv, const char* name)
{
    const std::string eq_prefix = std::string(name) + "=";
    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        const std::string token = argv[i];
        if (token == name && i + 1 < argc && argv[i + 1])
            return argv[i + 1];
        if (token.rfind(eq_prefix, 0) == 0)
            return token.substr(eq_prefix.size());
    }
    return {};
}

} // namespace

#if defined(_WIN32)
namespace {
bool g_pm_image_win_console = false;

bool win_skip_alloc_console(int argc, char** argv)
{
    bool skip = false;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i])
            continue;
        if (std::strcmp(argv[i], "--ui-reset") == 0)
            skip = true;
        else if (std::strcmp(argv[i], "--splash") == 0) {
#if !defined(PM_IMAGE_CLI_ONLY)
            pmui::set_splash_disabled_for_session(false);
#endif
            skip = true;
        } else if (std::strcmp(argv[i], "--no-splash") == 0) {
#if !defined(PM_IMAGE_CLI_ONLY)
            pmui::set_splash_disabled_for_session(true);
#endif
            skip = true;
        }
    }
    return skip;
}

/** GUI subsystem (`WIN32_EXECUTABLE`): stdio is often disconnected until we attach to the parent's console.
 *  Call **before** CLI11 parse so `--help` / parse errors reach cmd.exe or Windows Terminal.
 *  Does **not** `AllocConsole` unless @p allow_alloc_console (Explorer launches: attach fails, no extra window).
 *
 *  `CreateProcessW(..., CREATE_NEW_CONSOLE, ...)` gives a console HWND before CRT stdio is wired; the old
 *  `GetConsoleWindow() != nullptr` early-return left stdout/stderr invalid so `login` stderr looked like
 *  "nothing happened". */
void pm_image_prepare_windows_stdio(bool allow_alloc_console)
{
    const HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD        ft   = FILE_TYPE_UNKNOWN;
    if (hout && hout != INVALID_HANDLE_VALUE)
        ft = GetFileType(hout);
    const bool piped_to_parent = (ft == FILE_TYPE_PIPE) || (ft == FILE_TYPE_DISK);
    if (piped_to_parent)
        return;

    const HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD        eft = FILE_TYPE_UNKNOWN;
    if (herr && herr != INVALID_HANDLE_VALUE)
        eft = GetFileType(herr);
    const bool out_on_console = hout && hout != INVALID_HANDLE_VALUE && ft == FILE_TYPE_CHAR;
    const bool err_on_console = herr && herr != INVALID_HANDLE_VALUE && eft == FILE_TYPE_CHAR;
    if (out_on_console || err_on_console)
        return;

    if (GetConsoleWindow() == nullptr) {
        if (!::AttachConsole(ATTACH_PARENT_PROCESS)) {
            if (!allow_alloc_console)
                return;
            (void)::AllocConsole();
        }
    }

    FILE* fp = nullptr;
    (void)freopen_s(&fp, "CONOUT$", "w", stdout);
    (void)freopen_s(&fp, "CONOUT$", "w", stderr);
    (void)freopen_s(&fp, "CONIN$", "r", stdin);
}

/** When stdout or stderr is a Win32 console (not a pipe/file), set input/output code pages to UTF-8 so
 *  UTF-8 from spdlog, CLI, and model text displays correctly (avoids mojibake like "ΓÇö" for U+2014 EM DASH). */
void pm_image_try_set_windows_console_utf8()
{
    const HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD        ft_out = FILE_TYPE_UNKNOWN;
    DWORD        ft_err = FILE_TYPE_UNKNOWN;
    if (hout && hout != INVALID_HANDLE_VALUE)
        ft_out = GetFileType(hout);
    if (herr && herr != INVALID_HANDLE_VALUE)
        ft_err = GetFileType(herr);
    const bool on_console = (ft_out == FILE_TYPE_CHAR) || (ft_err == FILE_TYPE_CHAR);
    if (!on_console)
        return;
    (void)::SetConsoleOutputCP(CP_UTF8);
    (void)::SetConsoleCP(CP_UTF8);
}
} // namespace

bool pm_image_win_console_requested()
{
    return g_pm_image_win_console;
}

/** `dotenv::init(".env")` only reads cwd; Explorer / shortcuts often leave cwd ≠ install dir. */
void pm_image_dotenv_load_alternate_locations()
{
    namespace fs = std::filesystem;
    auto try_file = [](const fs::path& p) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec))
            return;
        dotenv::init(dotenv::Preserve, pm::path_u8_str(p).c_str());
    };
    wchar_t mod[MAX_PATH + 1]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, mod, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        const fs::path exe(mod);
        try_file(exe.parent_path() / L".env");
    }
    try {
        try_file(media::settings::get_config_dir() / L".env");
    } catch (...) {
    }
}
#endif

int pm_image_run(int argc, char** argv) {
#if defined(_WIN32)
#if !defined(PM_IMAGE_CLI_ONLY)
    pmui::ui_log_file_set_process_t0();
#endif
    std::ios::sync_with_stdio(true);
    pm_image_prepare_windows_stdio(false);
    pm_image_try_set_windows_console_utf8();
#endif
    if (const std::string config_dir = early_option_value(argc, argv, "--config-dir"); !config_dir.empty()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path dir = fs::absolute(fs::path(config_dir), ec);
        if (ec)
            dir = fs::path(config_dir);
        fs::create_directories(dir, ec);
        if (ec) {
            std::cerr << pm::brand::k_app_id_u8 << ": --config-dir: failed to create directory: "
                      << dir.string() << ": " << ec.message() << "\n";
            return 1;
        }
        media::settings::set_config_dir_override(dir);
    }

    dotenv::init(dotenv::Preserve);
#if defined(_WIN32)
    pm_image_dotenv_load_alternate_locations();
#endif
    media::cli::install_cli_interrupt_handlers();

#if defined(__APPLE__)
    logger::macos_bootstrap_file_logging_utf8(pm::brand::k_app_id_u8, "info");
#else
    logger::init_stderr(pm::brand::k_app_id_u8);
#endif

#if FEATURE_SERVE && FEATURE_IPC
    const char* cli_description = "media-img - resize (CLI), serve (REST), ipc (JSON lines)";
#elif FEATURE_SERVE
    const char* cli_description = "media-img - resize (CLI), serve (REST)";
#elif FEATURE_IPC
    const char* cli_description = "media-img - resize (CLI), ipc (JSON lines)";
#else
    const char* cli_description = "media-img - image tools CLI";
#endif
    CLI::App app{cli_description, "media-img"};
    app.set_version_flag("-v,--version", std::string(MEDIA_IMG_VERSION));
    app.require_subcommand(0, 1);

    if (const std::string commands_path = early_option_value(argc, argv, "--commands"); !commands_path.empty())
        media::runtime_settings::set_command_json_path_override(commands_path);

    PmImageCliState s;
    pm_image_register_cli(app, s);
    if (maybe_print_command_help_json(app, argc, argv))
        return 0;

#if defined(_WIN32)
    (void)win_skip_alloc_console(argc, argv);
#endif
    CLI11_PARSE(app, argc, argv);
    logger::set_log_level(s.log_level);
    media::mcp_embed::apply_after_cli_parse(s);
    media::mcp_embed::start_embedded_mcp_if_enabled();
#if defined(_WIN32)
#if !defined(PM_IMAGE_CLI_ONLY)
    pmui::ui_log_file_mark_after_cli_parse();
#endif
    g_pm_image_win_console = s.win_console;
    if (s.win_console) {
        pm_image_prepare_windows_stdio(true);
        pm_image_try_set_windows_console_utf8();
        logger::init_stderr(pm::brand::k_app_id_u8, s.log_level);
    }
    if (!s.settings_read_path.empty()) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path abs_in = fs::absolute(fs::path(s.settings_read_path));
        if (!fs::exists(abs_in, ec) || !fs::is_regular_file(abs_in, ec)) {
            std::cerr << pm::brand::k_app_id_u8 << ": --settings: not a file: " << abs_in.string() << "\n";
            return 1;
        }
        std::error_code canon_ec;
        const fs::path use_path = fs::weakly_canonical(abs_in, canon_ec);
        media::settings::set_settings_read_path_override(canon_ec ? abs_in : use_path);
    }
#endif
    if (!s.commands_path.empty())
        media::runtime_settings::set_command_json_path_override(s.commands_path);

    const int r = pm_image_dispatch_parsed(app, s);
    // Draining stderr (spdlog) and stdio before exit avoids some hosts (PowerShell / Windows Terminal)
    // leaving the console in a state where the user must press Enter before the next prompt.
    logger::flush();
    (void)std::cout.flush();
    (void)std::cerr.flush();
    (void)fflush(stdout);
    (void)fflush(stderr);
#if defined(_WIN32)
    if (s.shell_pause_on_exit && cli_tty::stdout_is_tty()) {
        std::cerr << "\nPress Enter to close...\n";
        (void)std::cin.get();
    }
#endif
    return r;
}
