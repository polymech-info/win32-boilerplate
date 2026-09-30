#include "blocks/shell_blocks.hpp"

#include "llm/tools/run/RunTool.hpp"
#include "logger/logger.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace media::xblox::blocks {
namespace {

std::string json_string(const nlohmann::json& o, const char* key)
{
    return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string{};
}

int json_int(const nlohmann::json& o, const char* key, int fallback)
{
    if (!o.is_object() || !o.contains(key) || !o[key].is_number_integer())
        return fallback;
    return o[key].get<int>();
}

bool json_bool(const nlohmann::json& o, const char* key, bool fallback)
{
    if (!o.is_object() || !o.contains(key) || !o[key].is_boolean())
        return fallback;
    return o[key].get<bool>();
}

std::vector<std::string> json_string_array(const nlohmann::json& o, const char* key)
{
    std::vector<std::string> out;
    if (!o.is_object() || !o.contains(key) || !o[key].is_array())
        return out;
    for (const auto& item : o[key]) {
        if (item.is_string())
            out.push_back(item.get<std::string>());
    }
    return out;
}

std::string shell_quote_arg(const std::string& arg)
{
#if defined(_WIN32)
    std::string out = "\"";
    for (char ch : arg) {
        if (ch == '"' || ch == '\\')
            out.push_back('\\');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
#else
    std::string out = "'";
    for (char ch : arg) {
        if (ch == '\'')
            out += "'\\''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
#endif
}

std::string powershell_literal_arg(const std::string& arg)
{
    std::string out = "'";
    for (char ch : arg) {
        if (ch == '\'')
            out += "''";
        else
            out.push_back(ch);
    }
    out.push_back('\'');
    return out;
}

std::string build_argv_command(const nlohmann::json& block)
{
    std::string command = json_string(block, "command");
    if (command.empty())
        command = json_string(block, "exe");
    if (command.empty())
        return {};
    std::string line = shell_quote_arg(command);
    for (const auto& arg : json_string_array(block, "args")) {
        line.push_back(' ');
        line += shell_quote_arg(arg);
    }
    return line;
}

std::string wrap_cwd_command(const std::string& command, const std::string& cwd)
{
    if (cwd.empty())
        return command;
#if defined(_WIN32)
    return "Push-Location -LiteralPath " + powershell_literal_arg(cwd) + "; try { " + command + " } finally { Pop-Location }";
#else
    return "cd " + shell_quote_arg(cwd) + " && " + command;
#endif
}

std::string normalize_mode(std::string mode)
{
    for (char& ch : mode)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return mode.empty() ? "shell" : mode;
}

std::string normalize_log_level(std::string level, const char* fallback)
{
    for (char& ch : level)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (level.empty())
        return fallback;
    if (level == "warning")
        return "warn";
    if (level == "err" || level == "critical")
        return "error";
    if (level == "off" || level == "none")
        return "off";
    if (level == "trace" || level == "debug" || level == "info" || level == "warn" || level == "error")
        return level;
    return fallback;
}

void log_at_level(const std::string& level, const std::string& message)
{
    if (level == "off" || message.empty())
        return;
    const std::string line = "[xblox:shell] " + message;
    if (level == "error")
        logger::error(line);
    else if (level == "warn")
        logger::warn(line);
    else if (level == "info")
        logger::info(line);
    else if (level == "debug")
        logger::debug(line);
    else
        logger::trace(line);
}

std::vector<std::string> split_output_lines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream stream(text);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    if (!text.empty() && (text.back() == '\n' || text.back() == '\r') && !lines.empty() && lines.back().empty())
        lines.pop_back();
    return lines;
}

void log_output_lines(const std::string& stream, const std::string& level, const std::vector<std::string>& lines)
{
    for (const auto& line : lines)
        log_at_level(level, stream + ": " + line);
}

bool shell_block(const nlohmann::json& block, const std::string& path, BlockRuntime& runtime)
{
    const std::string mode = normalize_mode(json_string(block, "mode"));
    const std::string shell = json_string(block, "shell").empty() ? "auto" : json_string(block, "shell");
    const bool background = block.value("background", false);
    const int timeout_ms = std::max(1, json_int(block, "timeoutMs", runtime.options.default_timeout_ms));
    const bool log_streams = json_bool(block, "log", block.contains("stdout") || block.contains("stderr"));
    const std::string stdout_level = normalize_log_level(json_string(block, "stdout"), "info");
    const std::string stderr_level = normalize_log_level(json_string(block, "stderr"), "error");
    std::string cwd = json_string(block, "cwd");
    if (cwd.empty() && !runtime.options.default_cwd.empty())
        cwd = runtime.options.default_cwd.string();

    std::string command;
    if (mode == "argv") {
        command = build_argv_command(block);
    } else {
        command = json_string(block, "command");
        if (command.empty())
            command = json_string(block, "line");
    }

    if (command.empty()) {
        runtime.emit(runtime.user_data, ExecutionEvent{path, "shell", "error", "shell command is empty", 2});
        return true;
    }
    if (background && runtime.options.console_command_handler) {
        std::string message;
        const int code = runtime.options.console_command_handler(
            wrap_cwd_command(command, cwd),
            true,
            block.value("closeOnExit", false),
            &message);
        const bool ok = code == 0;
        nlohmann::json result = {
            {"ok", ok},
            {"type", "shell"},
            {"mode", mode},
            {"shell", shell},
            {"command", command},
            {"cwd", cwd},
            {"background", true},
            {"terminal", true},
            {"result", message},
            {"exitCode", code},
        };
        runtime.emit(runtime.user_data, ExecutionEvent{
            path,
            "shell",
            ok ? "ok" : "error",
            message.empty() ? (ok ? "shell command sent to terminal" : "shell terminal dispatch failed") : message,
            code,
            std::move(result),
            json_string(block, "id"),
            "shell",
            {},
            {},
            ok ? 0 : code,
        });
        return true;
    }

    std::mutex chunk_mutex;
    nlohmann::json chunks = nlohmann::json::array();
    media::llm::run::set_output_chunk_hook([&](const std::string& stream, const std::string& chunk) {
        std::lock_guard<std::mutex> lk(chunk_mutex);
        chunks.push_back({{"stream", stream}, {"chunk", chunk}});
    });

    nlohmann::json args = {
        {"command", command},
        {"shell", shell},
        {"timeout_ms", timeout_ms},
    };
    const auto run = media::llm::run::execute(args, cwd);
    media::llm::run::clear_output_chunk_hook();

    const nlohmann::json envelope = run.envelope.is_object() ? run.envelope : nlohmann::json::object();
    const std::string stdout_text = envelope.value("stdout", std::string{});
    const std::string stderr_text = envelope.value("stderr", std::string{});
    const int exit_code = envelope.value("exit_code", run.ok ? 0 : 1);
    const bool timed_out = envelope.value("timed_out", false);
    const bool cancelled = envelope.value("cancelled", false);
    const bool ok = run.ok && exit_code == 0;
    const std::vector<std::string> stdout_lines = split_output_lines(stdout_text);
    const std::vector<std::string> stderr_lines = split_output_lines(stderr_text);
    if (log_streams) {
        log_output_lines("stdout", stdout_level, stdout_lines);
        log_output_lines("stderr", stderr_level, stderr_lines);
    }

    nlohmann::json result = {
        {"ok", ok},
        {"type", "shell"},
        {"mode", mode},
        {"shell", shell},
        {"command", command},
        {"cwd", cwd},
        {"background", background},
        {"log", log_streams},
        {"stdoutLevel", stdout_level},
        {"stderrLevel", stderr_level},
        {"exitCode", exit_code},
        {"result", stdout_text},
        {"stdout", stdout_text},
        {"stderr", stderr_text},
        {"stdoutLines", stdout_lines},
        {"stderrLines", stderr_lines},
        {"timedOut", timed_out},
        {"cancelled", cancelled},
        {"chunks", chunks},
    };
    if (!run.error.empty())
        result["error"] = run.error;

    runtime.set_context_value(runtime.user_data, "PREVIOUS", stdout_text);
    runtime.emit(runtime.user_data, ExecutionEvent{
        path,
        "shell",
        ok ? "ok" : "error",
        ok ? "shell command complete" : (run.error.empty() ? "shell command failed" : run.error),
        exit_code,
        std::move(result),
        json_string(block, "id"),
        "shell",
        stdout_lines,
        stderr_lines,
        ok ? 0 : (exit_code == 0 ? 1 : exit_code),
    });
    return true;
}

} // namespace

BlockHandler shell_block_handler(const std::string& kind)
{
    if (kind == "shell" || kind == "Shell")
        return shell_block;
    return nullptr;
}

void register_shell_blocks(BlockRegistry& registry)
{
    const nlohmann::json params = nlohmann::json::array({
        {{"name", "mode"}, {"type", "string"}, {"default", "shell"}},
        {{"name", "shell"}, {"type", "string"}, {"default", "auto"}},
        {{"name", "command"}, {"type", "string"}, {"required", true}},
        {{"name", "cwd"}, {"type", "string"}},
        {{"name", "args"}, {"type", "string[]"}},
        {{"name", "timeoutMs"}, {"type", "integer"}, {"default", 30000}},
        {{"name", "background"}, {"type", "boolean"}, {"default", false}},
        {{"name", "log"}, {"type", "boolean"}, {"default", false}},
        {{"name", "stdout"}, {"type", "logger::level"}, {"default", "info"}},
        {{"name", "stderr"}, {"type", "logger::level"}, {"default", "error"}},
        {{"name", "storeAs"}, {"type", "string"}},
    });
    registry["shell"] = block_descriptor(
        "shell", shell_block, "Shell", "Shell", "Run a shell command through the native RunTool.",
        {{"kind", "shell"}, {"mode", "shell"}, {"shell", "auto"}, {"command", "echo hello"}, {"timeoutMs", 30000}, {"log", false}, {"stdout", "info"}, {"stderr", "error"}, {"storeAs", "stdout"}},
        params);
    registry["Shell"] = block_descriptor(
        "Shell", shell_block, "Shell", "Shell", "Run a shell command through the native RunTool.",
        {{"kind", "Shell"}, {"mode", "shell"}, {"shell", "auto"}, {"command", "echo hello"}, {"timeoutMs", 30000}, {"log", false}, {"stdout", "info"}, {"stderr", "error"}, {"storeAs", "stdout"}},
        params, nlohmann::json::object(), 0xffffffffu, false);
}

} // namespace media::xblox::blocks
