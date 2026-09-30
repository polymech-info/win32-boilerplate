#include "pm_image_cmd_includes.hpp"
#include "pm_image_cmd_xblox.hpp"
#include "core/cli_cancel.hpp"
#include "pm_image_register_cli.hpp"
#include "xblox/blocks/builtin_blocks.hpp"
#include "xblox/utils/conv.hpp"
#include "xblox_commands.hpp"

namespace {

bool read_json_file(const std::filesystem::path& path, nlohmann::json& out, std::string& err)
{
    try {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            err = "cannot open " + path.string();
            return false;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        out = nlohmann::json::parse(ss.str());
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

bool load_custom_commands_for_xblox(PmImageCliState& st, nlohmann::json& out, std::string& err)
{
    if (!st.xblox_commands_path.empty())
        media::runtime_settings::set_command_json_path_override(st.xblox_commands_path);

    std::string raw;
    if (!media::runtime_settings::load_command_json_utf8(raw, err))
        return false;
    if (raw.empty()) {
        out = nlohmann::json::object();
        err.clear();
        return true;
    }
    try {
        out = nlohmann::json::parse(raw);
        return true;
    } catch (const std::exception& e) {
        err = std::string("commands.json parse failed: ") + e.what();
        return false;
    }
}

int noop_handler(const media::xblox::CommandInvocation& invocation, std::string* message)
{
    if (message)
        *message = invocation.action + " command noop";
    return 0;
}

nlohmann::json block_definition_to_json(const media::xblox::blocks::BlockDescriptor& block)
{
    return {
        {"kind", block.kind},
        {"label", block.label},
        {"description", block.description},
        {"group", block.group},
        {"block", block.default_block},
        {"params", block.params},
        {"flags", block.flags},
        {"platformMask", block.platform_mask},
    };
}

nlohmann::json xblox_info_payload(PmImageCliState& st)
{
    nlohmann::json blocks = nlohmann::json::array();
    nlohmann::json groups = nlohmann::json::object();
    for (const auto& block : media::xblox::blocks::registered_block_definitions()) {
        const auto block_json = block_definition_to_json(block);
        blocks.push_back(block_json);
        groups[block.group.empty() ? "Other" : block.group].push_back(block_json);
    }

    nlohmann::json registered = nlohmann::json::array();
    for (const auto& c : pm::cli::registered_cli_commands()) {
        registered.push_back({
            {"id", c.id ? c.id : ""},
            {"label", c.label ? c.label : ""},
            {"available", c.available},
        });
    }

    nlohmann::json custom = nlohmann::json::array();
    for (const auto& c : pm::cli::visible_custom_commands()) {
        custom.push_back({
            {"group", c.group},
            {"id", c.id},
            {"label", c.label},
            {"type", c.type},
            {"action", c.action},
        });
    }

    nlohmann::json document = nlohmann::json::object();
    std::string err;
    if (load_custom_commands_for_xblox(st, document, err)) {
        err.clear();
    }

    return {
        {"version", 1},
        {"description", "XBlox block-tree command flow metadata for LLM composition."},
        {"documentShape", {
            {"version", 1},
            {"context", nlohmann::json::object()},
            {"roots", nlohmann::json::array()},
        }},
        {"blocks", std::move(blocks)},
        {"groups", std::move(groups)},
        {"commands", {
            {"commandsPath", media::settings::get_command_json_path().string()},
            {"document", std::move(document)},
            {"documentError", err},
            {"registeredCommands", std::move(registered)},
            {"customCommands", std::move(custom)},
        }},
    };
}

int pm_image_cmd_xblox_info(PmImageCliState& st)
{
    const auto payload = xblox_info_payload(st);
    if (st.xblox_info_json) {
        std::cout << payload.dump(2) << "\n";
        return 0;
    }

    const auto& blocks = payload["blocks"];
    const auto& commands = payload["commands"];
    std::cout << "XBlox blocks: " << (blocks.is_array() ? blocks.size() : 0) << "\n";
    for (const auto& [group, items] : payload["groups"].items())
        std::cout << "  " << group << ": " << (items.is_array() ? items.size() : 0) << "\n";
    std::cout << "Registered CLI commands: " << commands.value("registeredCommands", nlohmann::json::array()).size() << "\n";
    std::cout << "Custom commands: " << commands.value("customCommands", nlohmann::json::array()).size() << "\n";
    std::cout << "Use `xblox info --json` for block params/defaults and command payloads.\n";
    return 0;
}

} // namespace

int pm_image_cmd_xblox(CLI::App&, PmImageCliState& st)
{
    namespace fs = std::filesystem;
    if (st.xblox_info_cmd && st.xblox_info_cmd->parsed())
        return pm_image_cmd_xblox_info(st);
    if (!st.xblox_run_cmd || !st.xblox_run_cmd->parsed())
        return 0;
    media::cli::install_cli_interrupt_handlers();

    std::error_code ec;
    const fs::path src = fs::absolute(fs::path(st.xblox_src), ec);
    if (ec || src.empty()) {
        std::cerr << "xblox run: invalid --src: " << st.xblox_src << "\n";
        return 1;
    }

    nlohmann::json blocks_file;
    std::string err;
    if (!read_json_file(src, blocks_file, err)) {
        std::cerr << "xblox run: " << err << "\n";
        return 1;
    }

    nlohmann::json custom_commands;
    if (!load_custom_commands_for_xblox(st, custom_commands, err)) {
        std::cerr << "xblox run: " << err << "\n";
        return 1;
    }

    std::error_code cwd_ec;
    const fs::path default_cwd = st.cwd.empty()
        ? fs::current_path(cwd_ec)
        : fs::path(st.cwd);

    media::xblox::ExecutionOptions options;
    options.custom_commands = std::move(custom_commands);
    if (!cwd_ec || !st.cwd.empty())
        options.default_cwd = default_cwd;
    options.execute_external_commands = !st.xblox_dry_run;
    options.wait_blocks = !st.xblox_no_wait;
    options.collect_events = st.xblox_json;
    options.max_loop_iterations = st.xblox_max_loop_iterations;
    options.extra_args = st.xblox_extra_args;
    options.app_command_handler = noop_handler;
    options.ribbon_command_handler = noop_handler;
    options.open_url_handler = noop_handler;
    options.open_path_handler = noop_handler;
    options.cancel_requested = [] { return media::cli::cancel_requested(); };

    auto result = media::xblox::run_blocks_file(blocks_file, options);

    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : result.events)
        events.push_back(media::xblox::conv::event_to_json(event));

    const nlohmann::json report = {
        {"ok", result.ok},
        {"exitCode", result.exit_code},
        {"src", src.string()},
        {"eventCount", result.event_count},
        {"events", std::move(events)},
    };

    if (st.xblox_json) {
        std::cout << report.dump(2) << "\n";
    }
    return result.ok ? 0 : (result.exit_code == 0 ? 1 : result.exit_code);
}

void pm_image_register_xblox(CLI::App& app, PmImageCliState& s)
{
    s.xblox_cmd = app.add_subcommand(
        "xblox",
        "Run XBlox block-tree command flows.");
    s.xblox_cmd->require_subcommand(1);
    s.xblox_cmd->add_option(
            "--log-level",
            s.log_level,
            "Alias for the global --log-level option when using `xblox --log-level ... run`.")
        ->check(CLI::IsMember(
            {"trace", "debug", "info", "warn", "warning", "error", "err", "critical", "off", "none"},
            CLI::ignore_case));

    s.xblox_info_cmd = s.xblox_cmd->add_subcommand(
        "info",
        "Print XBlox block/command metadata for builders and LLM composition.");
    s.xblox_info_cmd->add_flag(
        "--json",
        s.xblox_info_json,
        "Print full JSON metadata: block groups, descriptions, params, defaults, and supported commands.");
    s.xblox_info_cmd->add_option(
        "--commands",
        s.xblox_commands_path,
        "Optional commands.json override for custom command metadata.");

    s.xblox_run_cmd = s.xblox_cmd->add_subcommand(
        "run",
        "Run a blocks-file JSON document emitted by the XBlox web app.");
    s.xblox_run_cmd->add_option(
            "--src",
            s.xblox_src,
            "Path to a blocks-file JSON document: { version: 1, context?: {}, roots: [...] }.")
        ->required(true);
    s.xblox_run_cmd->add_option(
        "--commands",
        s.xblox_commands_path,
        "Optional commands.json override for resolving host.runCustomCommand({ id }).");
    s.xblox_run_cmd->add_flag(
        "--json",
        s.xblox_json,
        "Print a JSON execution report.");
    s.xblox_run_cmd->add_flag(
        "--dry-run",
        s.xblox_dry_run,
        "Stage CLI/external commands but do not spawn child processes.");
    s.xblox_run_cmd->add_flag(
        "--no-wait",
        s.xblox_no_wait,
        "Skip sleeping for wait blocks.");
    s.xblox_run_cmd
        ->add_option(
            "--max-loop-iterations",
            s.xblox_max_loop_iterations,
            "Temporary loop cap while expression/context mapping is stubbed.")
        ->default_val(1)
        ->check(CLI::Range(0, 100));
    s.xblox_run_cmd->add_option(
        "--arg",
        s.xblox_extra_args,
        "Extra argument appended to cliCommand/external argv command invocations; repeatable.");
}
