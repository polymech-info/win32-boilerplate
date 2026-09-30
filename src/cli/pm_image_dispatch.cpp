#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_handlers.hpp"
#include "pm_image_register_cli.hpp"
#include "core/command_variables.hpp"
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
#include "win/session_replay/session_replay.hpp"
#include "win/ui_next/log_sink.h"
#endif
#if defined(FEATURE_PIXLWIZ_AUTH) && FEATURE_PIXLWIZ_AUTH
#include "core/pixlwiz_user_budget.hpp"
#include "core/settings_runtime.hpp"
#include "lib/pm_zitadel_oauth.hpp"
#endif

#include <cctype>
#include <string_view>

namespace fs = std::filesystem;

namespace {

std::string json_string(const nlohmann::json& o, const char* key)
{
    return o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string{};
}

bool json_bool(const nlohmann::json& o, const char* key, bool fallback = false)
{
    return o.contains(key) && o[key].is_boolean() ? o[key].get<bool>() : fallback;
}

std::vector<std::string> json_string_array(const nlohmann::json& o, const char* key)
{
    std::vector<std::string> out;
    if (!o.contains(key) || !o[key].is_array())
        return out;
    for (const auto& item : o[key]) {
        if (item.is_string())
            out.push_back(item.get<std::string>());
    }
    return out;
}

std::vector<std::string> split_cli_command_path(const std::string& cli)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_quote = false;
    char quote = '\0';

    for (char ch : cli) {
        if ((ch == '"' || ch == '\'') && (!in_quote || quote == ch)) {
            in_quote = !in_quote;
            quote = in_quote ? ch : '\0';
            continue;
        }
        if (!in_quote && std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

bool find_visible_custom_command_item(const nlohmann::json& items, const std::string& id, nlohmann::json& out)
{
    if (!items.is_array())
        return false;
    for (const auto& item : items) {
        if (!item.is_object())
            continue;
        if (item.value("enabled", true) == false || item.value("visible", true) == false)
            continue;
        if (json_string(item, "id") == id) {
            out = item;
            return true;
        }
        if (find_visible_custom_command_item(item.value("items", nlohmann::json::array()), id, out))
            return true;
    }
    return false;
}

bool load_visible_custom_command_item(const std::string& id, nlohmann::json& out, std::string& err)
{
    std::string raw;
    if (!media::runtime_settings::load_command_json_utf8(raw, err))
        return false;
    if (raw.empty()) {
        err = "commands.json is empty or missing";
        return false;
    }
    try {
        const auto doc = nlohmann::json::parse(raw);
        const auto groups = doc.value("ribbon", nlohmann::json::object()).value("groups", nlohmann::json::array());
        if (!groups.is_array()) {
            err = "commands.json: ribbon.groups is missing";
            return false;
        }
        for (const auto& group : groups) {
            if (!group.is_object())
                continue;
            if (find_visible_custom_command_item(group.value("items", nlohmann::json::array()), id, out))
                return true;
        }
    } catch (const std::exception& e) {
        err = std::string("commands.json parse failed: ") + e.what();
        return false;
    }
    err = "custom command not found or not enabled/visible: " + id;
    return false;
}

std::string first_source_from_args(const std::vector<std::string>& extra_args)
{
    for (size_t i = 0; i + 1 < extra_args.size(); ++i) {
        if (extra_args[i] == "--")
            break;
        if (extra_args[i] == "--src" || extra_args[i] == "-s" || extra_args[i] == "--input" || extra_args[i] == "-i")
            return extra_args[i + 1];
    }
    return {};
}

std::vector<std::string> passthrough_custom_args(const std::vector<std::string>& extra_args)
{
    std::vector<std::string> out;
    bool passthrough_only = false;
    for (size_t i = 0; i < extra_args.size(); ++i) {
        if (!passthrough_only && extra_args[i] == "--") {
            passthrough_only = true;
            continue;
        }
        if (!passthrough_only
            && (extra_args[i] == "--src" || extra_args[i] == "-s" || extra_args[i] == "--input" || extra_args[i] == "-i")
            && i + 1 < extra_args.size()) {
            ++i;
            continue;
        }
        out.push_back(extra_args[i]);
    }
    return out;
}

std::string cwd_from_source_arg(const std::string& source)
{
    if (source.empty())
        return {};
    std::error_code ec;
    fs::path path = fs::absolute(fs::u8path(source), ec);
    if (ec)
        path = fs::u8path(source);
    if (fs::is_directory(path, ec) && !ec)
        return path.lexically_normal().string();
    fs::path parent = path.parent_path();
    if (parent.empty())
        return {};
    return parent.lexically_normal().string();
}

std::string first_source_from_custom_invocation(const nlohmann::json& item, const std::vector<std::string>& extra_args)
{
    const std::string runtime_source = first_source_from_args(extra_args);
    if (!runtime_source.empty())
        return runtime_source;
    if (item.contains("source") && item["source"].is_object()) {
        const auto& source = item["source"];
        const auto files = json_string_array(source, "files");
        if (!files.empty())
            return files.front();
    }
    return {};
}

std::vector<std::string> sources_from_custom_invocation(const nlohmann::json& item, const std::vector<std::string>& extra_args)
{
    std::vector<std::string> out;
    for (size_t i = 0; i + 1 < extra_args.size(); ++i) {
        if (extra_args[i] == "--")
            break;
        if (extra_args[i] == "--src" || extra_args[i] == "-s" || extra_args[i] == "--input" || extra_args[i] == "-i")
            out.push_back(extra_args[i + 1]);
    }
    if (item.contains("source") && item["source"].is_object()) {
        const auto& source = item["source"];
        const auto files = json_string_array(source, "files");
        out.insert(out.end(), files.begin(), files.end());
        const auto folders = json_string_array(source, "folders");
        out.insert(out.end(), folders.begin(), folders.end());
    }
    return out;
}

bool run_option_enabled(const nlohmann::json& item, const char* key, bool fallback = false)
{
    if (!item.contains("runOptions") || !item["runOptions"].is_object())
        return fallback;
    return json_bool(item["runOptions"], key, fallback);
}

#if defined(_WIN32)
std::wstring utf8_to_wide_local(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return std::wstring(s.begin(), s.end());
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::wstring quote_process_arg(std::wstring_view arg)
{
    if (arg.empty())
        return L"\"\"";
    bool need_quotes = false;
    for (wchar_t ch : arg) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'"') {
            need_quotes = true;
            break;
        }
    }
    if (!need_quotes)
        return std::wstring(arg);
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(ch);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring current_executable_path()
{
    std::vector<wchar_t> buf(32768, L'\0');
    const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size() - 1u));
    if (n == 0u)
        return {};
    return std::wstring(buf.data(), n);
}

int run_custom_new_shell_windows(const std::string& custom_id,
                                 const std::wstring& command,
                                 const std::string& cwd,
                                 bool close_on_exit,
                                 bool use_call_operator)
{
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass ";
    if (!close_on_exit)
        cmd += L"-NoExit ";
    cmd += L"-Command ";
    cmd += quote_process_arg(use_call_operator ? (L"& " + command) : command);

    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring wcwd = utf8_to_wide_local(cwd);
    const wchar_t* cwdp = wcwd.empty() ? nullptr : wcwd.c_str();
    if (!::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                          CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT, nullptr, cwdp, &si, &pi)) {
        std::cerr << custom_id << ": CreateProcessW new shell failed: " << static_cast<unsigned long>(::GetLastError()) << "\n";
        return 1;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return 0;
}

int run_custom_process_windows(const std::string& custom_id, std::wstring cmd, const std::string& cwd = {})
{
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring wcwd = utf8_to_wide_local(cwd);
    const wchar_t* cwdp = wcwd.empty() ? nullptr : wcwd.c_str();
    if (!::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                          CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP, nullptr, cwdp, &si, &pi)) {
        std::cerr << custom_id << ": CreateProcessW failed: " << static_cast<unsigned long>(::GetLastError()) << "\n";
        return 1;
    }
    ::CloseHandle(pi.hThread);

    bool cancel_sent = false;
    for (;;) {
        const DWORD wait = ::WaitForSingleObject(pi.hProcess, 100);
        if (wait == WAIT_OBJECT_0)
            break;
        if (wait == WAIT_FAILED) {
            std::cerr << custom_id << ": WaitForSingleObject failed: " << static_cast<unsigned long>(::GetLastError()) << "\n";
            ::TerminateProcess(pi.hProcess, 1);
            break;
        }
        if (media::cli::cancel_requested()) {
            if (!cancel_sent) {
                cancel_sent = true;
                (void)::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pi.dwProcessId);
            } else if (::WaitForSingleObject(pi.hProcess, 5000) != WAIT_OBJECT_0) {
                ::TerminateProcess(pi.hProcess, 130);
                (void)::WaitForSingleObject(pi.hProcess, 3000);
                break;
            }
        }
    }

    DWORD code = 1;
    (void)::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hProcess);
    if (media::cli::cancel_requested() && code == STILL_ACTIVE)
        return 130;
    return static_cast<int>(code);
}
#else
std::string shell_quote_posix(const std::string& arg)
{
    if (arg.empty())
        return "''";
    std::string out = "'";
    for (char ch : arg) {
        if (ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}
#endif

int spawn_custom_external_command(const std::string& custom_id, const nlohmann::json& item, const std::vector<std::string>& extra_args)
{
    if (!item.contains("externalCommand") || !item["externalCommand"].is_object())
        return 2;
    const auto& ext = item["externalCommand"];
    const std::string mode = json_string(ext, "mode");
    std::string cwd = json_string(ext, "cwd");
    if (cwd.empty())
        cwd = json_string(item, "cwd");
    const bool new_shell_window = run_option_enabled(item, "newShellWindow");
    const bool close_on_exit = run_option_enabled(item, "closeOnExit");
    const auto passthrough_args = passthrough_custom_args(extra_args);
    if (mode == "shell") {
        const std::string line = json_string(ext, "shellLine");
        if (line.empty()) {
            std::cerr << custom_id << ": externalCommand.shellLine is empty\n";
            return 2;
        }
        if (!passthrough_args.empty())
            std::cerr << custom_id << ": warning: extra args are ignored for shell-mode external commands\n";
#if defined(_WIN32)
        if (new_shell_window)
            return run_custom_new_shell_windows(custom_id, utf8_to_wide_local(line), cwd, close_on_exit, false);
        std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ";
        cmd += quote_process_arg(utf8_to_wide_local(line));
        return run_custom_process_windows(custom_id, cmd, cwd);
#else
        std::string cmd;
        if (!cwd.empty())
            cmd += "cd " + shell_quote_posix(cwd) + " && ";
        cmd += "/bin/sh -lc " + shell_quote_posix(line);
        return std::system(cmd.c_str());
#endif
    }

    const std::string command = json_string(ext, "command");
    if (command.empty()) {
        std::cerr << custom_id << ": externalCommand.command is empty\n";
        return 2;
    }
    std::vector<std::string> args = json_string_array(ext, "args");
    args.insert(args.end(), passthrough_args.begin(), passthrough_args.end());
#if defined(_WIN32)
    std::wstring cmd = quote_process_arg(utf8_to_wide_local(command));
    for (const auto& arg : args) {
        cmd.push_back(L' ');
        cmd += quote_process_arg(utf8_to_wide_local(arg));
    }
    if (new_shell_window)
        return run_custom_new_shell_windows(custom_id, cmd, cwd, close_on_exit, true);
    return run_custom_process_windows(custom_id, cmd, cwd);
#else
    std::string cmd;
    if (!cwd.empty())
        cmd += "cd " + shell_quote_posix(cwd) + " && ";
    cmd += shell_quote_posix(command);
    for (const auto& arg : args)
        cmd += " " + shell_quote_posix(arg);
    return std::system(cmd.c_str());
#endif
}

int spawn_custom_cli_command(const std::string& custom_id, const nlohmann::json& item, const std::vector<std::string>& extra_args, const PmImageCliState& st)
{
    const std::string cli = json_string(item, "cliCommand");
    if (cli.empty()) {
        std::cerr << custom_id << ": only cliCommand custom commands are executable from the CLI right now\n";
        return 2;
    }
    std::vector<std::string> args;
    for (const auto& arg : json_string_array(item, "globalArgs"))
        args.push_back(arg);
    const std::string configured_cwd = json_string(item, "cwd");
    if (!configured_cwd.empty() && configured_cwd != ".") {
        args.push_back("--cwd");
        args.push_back(configured_cwd);
    }
    const std::string configured_log_level = json_string(item, "logLevel");
    const std::string log_level = configured_log_level.empty() ? st.log_level : configured_log_level;
    if (!log_level.empty() && log_level != "info") {
        args.push_back("--log-level");
        args.push_back(log_level);
    }
    if (st.g_no_gui)
        args.push_back("--no-gui");
    const auto cli_parts = split_cli_command_path(cli);
    if (cli_parts.empty()) {
        std::cerr << custom_id << ": cliCommand is empty\n";
        return 2;
    }
    args.insert(args.end(), cli_parts.begin(), cli_parts.end());
    for (const auto& arg : json_string_array(item, "args"))
        args.push_back(arg);
    const auto passthrough_args = passthrough_custom_args(extra_args);
    args.insert(args.end(), passthrough_args.begin(), passthrough_args.end());

#if defined(_WIN32)
    const std::wstring exe = current_executable_path();
    if (exe.empty()) {
        std::cerr << custom_id << ": could not resolve current executable path\n";
        return 1;
    }
    std::wstring cmd = quote_process_arg(exe);
    for (const auto& arg : args) {
        cmd.push_back(L' ');
        cmd += quote_process_arg(utf8_to_wide_local(arg));
    }
    if (run_option_enabled(item, "newShellWindow"))
        return run_custom_new_shell_windows(custom_id, cmd, configured_cwd,
                                            !run_option_enabled(item, "closeOnExit"), true);
    return run_custom_process_windows(custom_id, cmd);
#else
    std::ostringstream cmd;
    cmd << "pm-image-cli";
    for (const auto& arg : args)
        cmd << " " << shell_quote_posix(arg);
    return std::system(cmd.str().c_str());
#endif
}

} // namespace

int pm_image_dispatch_parsed(CLI::App& app, PmImageCliState& st) {
    if (st.cwd.empty() || st.cwd == ".") {
        for (auto* custom_cmd : st.custom_command_cmds) {
            if (!custom_cmd || !custom_cmd->parsed())
                continue;
            const std::string inferred_cwd = cwd_from_source_arg(first_source_from_args(custom_cmd->remaining()));
            if (!inferred_cwd.empty()) {
                st.cwd = inferred_cwd;
                break;
            }
        }
    }

    // Apply --cwd early so all subsequent path operations use the correct working directory
    if (!st.cwd.empty() && st.cwd != ".") {
        std::error_code ec;
        const fs::path new_cwd = fs::absolute(st.cwd, ec);
        if (ec || !fs::exists(new_cwd) || !fs::is_directory(new_cwd)) {
            std::cerr << pm::brand::k_app_id_u8 << ": --cwd is not a valid directory: " << st.cwd << "\n";
            return 1;
        }
        fs::current_path(new_cwd, ec);
        if (ec) {
            std::cerr << pm::brand::k_app_id_u8 << ": --cwd failed to change directory: " << st.cwd << "\n";
            return 1;
        }
    }
#if FEATURE_CUSTOM_COMMANDS
    for (auto* custom_cmd : st.custom_command_cmds) {
        if (!custom_cmd || !custom_cmd->parsed())
            continue;
        const std::string id = custom_cmd->get_name();
        nlohmann::json item;
        std::string err;
        if (!load_visible_custom_command_item(id, item, err)) {
            std::cerr << id << ": " << err << "\n";
            return 1;
        }
        std::error_code ec_cwd;
        const std::string cwd = fs::current_path(ec_cwd).string();
        media::commands::resolve_custom_command_item_variables(item, media::commands::VariableContext{
            ec_cwd ? std::string{} : cwd,
            first_source_from_custom_invocation(item, custom_cmd->remaining()),
            sources_from_custom_invocation(item, custom_cmd->remaining()),
        }, &err);
        if (!err.empty())
            std::cerr << id << ": variable resolve warning: " << err << "\n";
        if (!json_string(item, "cliCommand").empty())
            return spawn_custom_cli_command(id, item, custom_cmd->remaining(), st);
        if (item.contains("externalCommand") && item["externalCommand"].is_object())
            return spawn_custom_external_command(id, item, custom_cmd->remaining());
        std::cerr << id << ": this custom command type is not executable from the CLI yet\n";
        return 2;
    }
#endif
#if FEATURE_COMMAND_COMMANDS
    if (st.commands_cmd && st.commands_cmd->parsed()) {
        const auto commands = pm::cli::registered_cli_commands();
        const auto custom_commands = pm::cli::visible_custom_commands();
        if (st.commands_json) {
            nlohmann::json rows = nlohmann::json::array();
            for (const auto& c : commands) {
                rows.push_back({
                    {"id", c.id ? c.id : ""},
                    {"label", c.label ? c.label : ""},
                    {"available", c.available},
                });
            }
            nlohmann::json custom_rows = nlohmann::json::array();
            for (const auto& c : custom_commands) {
                custom_rows.push_back({
                    {"group", c.group},
                    {"id", c.id},
                    {"label", c.label},
                    {"type", c.type},
                    {"action", c.action},
                    {"enabled", true},
                    {"visible", true},
                });
            }
            std::cout << nlohmann::json{
                {"registeredCommands", std::move(rows)},
                {"customCommands", std::move(custom_rows)},
            }.dump(2) << "\n";
        } else {
            std::cout << "Registered commands\n";
            for (const auto& c : commands) {
                std::cout << "  " << (c.id ? c.id : "");
                if (c.label && c.label[0] != 0)
                    std::cout << " - " << c.label;
                if (!c.available)
                    std::cout << " (disabled in this build)";
                std::cout << "\n";
            }
            std::cout << "\nCustom Commands\n";
            if (custom_commands.empty()) {
                std::cout << "  (none enabled/visible in commands.json)\n";
            } else {
                std::string last_group;
                for (const auto& c : custom_commands) {
                    if (c.group != last_group) {
                        last_group = c.group;
                        if (!last_group.empty())
                            std::cout << "  [" << last_group << "]\n";
                    }
                    std::cout << "  " << (c.id.empty() ? c.label : c.id);
                    if (!c.label.empty() && c.label != c.id)
                        std::cout << " - " << c.label;
                    if (!c.action.empty())
                        std::cout << " (" << c.action << ")";
                    std::cout << "\n";
                }
            }
        }
        return 0;
    }
#endif
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    // Set log directory to current path (--cwd or original cwd)
    pmui::set_pm_image_log_file_directory(fs::current_path());
#endif
#if defined(_WIN32) && FEATURE_LICENSE_FILE
    if (st.lic_fingerprint_cmd && st.lic_fingerprint_cmd->parsed()) {
        std::cout << media::win::machine_fingerprint_hex() << "\n";
        return 0;
    }
    if (st.lic_import_cmd && st.lic_import_cmd->parsed()) {
        std::string lie;
        if (!media::win::license_import_from_file(st.lic_import_path, lie)) {
            std::cerr << "license import: " << lie << "\n";
            return 1;
        }
        std::cout << "license import: wrote "
                  << media::win::license_active_storage_path().string() << "\n";
        return 0;
    }
    if (st.lic_verify_cmd && st.lic_verify_cmd->parsed()) {
        std::string ve;
        if (!media::win::license_is_valid(ve)) {
            std::cerr << "license verify: " << ve << "\n";
            return 1;
        }
        std::cout << "license verify: ok (" << media::win::license_active_storage_path().string()
                  << ")\n";
        return 0;
    }
#endif

#if defined(_WIN32) && FEATURE_TRIAL_CHECK
    bool skip_trial_for_license = false;
#if FEATURE_LICENSE_FILE
    {
        std::string le;
        if (media::win::license_is_valid(le))
            skip_trial_for_license = true;
    }
#endif
    if (st.status_cmd && st.status_cmd->parsed()) {
        return pm_image_cmd_status_pixlwiz(app, st);
    }
    if (st.godmod_cmd && st.godmod_cmd->parsed()) {
        std::string err;
        if (!media::win::trial_set_godmode(true, err)) {
            std::cerr << "godmod: " << err << "\n";
            return 1;
        }
        std::cerr << "godmod: trial bypass enabled for this user profile.\n";
        return 0;
    }
    if (st.purgetrial_cmd && st.purgetrial_cmd->parsed()) {
        std::string err;
        if (!media::win::trial_purge_all(err)) {
            std::cerr << "purgetrial: " << err << "\n";
            return 1;
        }
        std::cerr << "purgetrial: trial tracking data removed.\n";
        return 0;
    }
    if (!media::win::trial_is_godmode() && !skip_trial_for_license) {
        std::string trial_msg;
        if (!media::win::trial_enforce_or_exit(trial_msg)) {
            std::cerr << trial_msg << "\n";
            // IExecute / some hosts use CREATE_NO_WINDOW and pipe stdio — no console, no error text.
            if (::GetConsoleWindow() == nullptr) {
                (void)::MessageBoxW(
                    nullptr, pmui::utf8_to_wide(trial_msg).c_str(), pm::brand::k_app_id_w, MB_OK | MB_ICONERROR);
            }
            return 1;
        }
    }
#endif

#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
    if (st.test_screenshot_cmd && st.test_screenshot_cmd->parsed()) {
        if (st.ui_reset)
            media::settings::set_ui_reset_session(true);
        pmui::set_splash_disabled_for_session(!st.show_startup_splash);
        return media::win::launch_ui_next_screenshot_probe(pmui::utf8_to_wide(st.test_screenshot_out),
                                                            st.test_screenshot_wait_ms);
    }
    if (st.replay_cmd && st.replay_cmd->parsed()) {
        namespace fs = std::filesystem;
        fs::path        resolved;
        std::string     rerr;
        if (!media::win::session_replay::resolve_session_replay_cli_input(st.replay_session_path, resolved, rerr)) {
            std::cerr << "replay: " << rerr << "\n";
            return 1;
        }
        if (st.ui_reset)
            media::settings::set_ui_reset_session(true);
        pmui::set_splash_disabled_for_session(!st.show_startup_splash);
        return media::win::launch_ui_next_session_replay(resolved.wstring());
    }
    if (!st.ui_chat && !st.ui_chat_src_list.empty() && st.ui_preset.empty()) {
        std::cerr << pm::brand::k_app_id_u8
                  << ": top-level --src is only used with --ui-chat or with --ui-preset (e.g. `"
                  << pm::brand::k_app_id_u8 << " --ui-preset=main|chat|viewer --src <path>`). For resize, use the `resize` subcommand's --src.\n";
        return 1;
    }
    std::wstring layout_override_path;
    std::string  layout_override_workbench;
    if (!st.layout_override_path.empty()) {
        if (!app.get_subcommands().empty()) {
            std::cerr << pm::brand::k_app_id_u8 << ": --layout is only valid when launching the UI without a subcommand\n";
            return 1;
        }
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path abs_in = fs::absolute(fs::path(st.layout_override_path));
        if (!fs::exists(abs_in, ec) || !fs::is_regular_file(abs_in, ec)) {
            std::cerr << pm::brand::k_app_id_u8 << ": --layout: not a file: " << abs_in.string() << "\n";
            return 1;
        }
        std::error_code canon_ec;
        const fs::path use_path = fs::weakly_canonical(abs_in, canon_ec);
        const fs::path final_path = canon_ec ? abs_in : use_path;
        try {
            std::ifstream in(final_path, std::ios::binary);
            nlohmann::json doc = nlohmann::json::parse(in);
            if (doc.contains("workbench") && doc["workbench"].is_string()) {
                layout_override_workbench = doc["workbench"].get<std::string>();
                if (layout_override_workbench != "main" && layout_override_workbench != "chat"
                    && layout_override_workbench != "viewer") {
                    std::cerr << pm::brand::k_app_id_u8 << ": --layout: unsupported workbench '"
                              << layout_override_workbench << "'\n";
                    return 1;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << pm::brand::k_app_id_u8 << ": --layout: JSON parse failed: " << e.what() << "\n";
            return 1;
        }
        layout_override_path = final_path.wstring();
        if (!st.ui_preset.empty() && !layout_override_workbench.empty() && st.ui_preset != layout_override_workbench) {
            std::cerr << pm::brand::k_app_id_u8 << ": --layout: file was exported for workbench '"
                      << layout_override_workbench << "'; applying to --ui-preset '" << st.ui_preset
                      << "' for this run only\n";
        }
    }
#endif
    // No subcommand -> launch the Win32++ ribbon UI (Windows only).
    // On non-Windows, fall through and CLI11 will print help / error normally.
    if (app.get_subcommands().empty()) {
#if defined(_WIN32) && !defined(PM_IMAGE_CLI_ONLY)
        if (st.ui_reset || !layout_override_path.empty())
            media::settings::set_ui_reset_session(true);
        pmui::set_splash_disabled_for_session(!st.show_startup_splash);
        if (!st.ui_preset.empty())
            media::settings::set_ui_workbench_id_cli_override(st.ui_preset.c_str());
        else if (!layout_override_workbench.empty())
            media::settings::set_ui_workbench_id_cli_override(layout_override_workbench.c_str());
        if (!st.ui_app.empty())
            media::settings::set_ui_viewer_app_cli_override(st.ui_app.c_str());
        // Top-level --src (with --ui-preset, not --ui-chat) seeds the main UI; chat preset also focuses Chat when paths are present.
        const bool open_chat = (st.ui_preset == "chat" && !st.ui_chat_src_list.empty());
        return media::win::launch_ui_next(st.ui_chat_src_list, open_chat,
            "pm_image_run: no subcommand (e.g. double-click), default --ui-next", layout_override_path);
#else
        std::cerr << app.help() << std::endl;
        return 1;
#endif
    }
    if (st.resize_cmd && st.resize_cmd->parsed())
        return pm_image_cmd_resize(app, st);
    return pm_image_dispatch_remaining_commands(app, st);
}