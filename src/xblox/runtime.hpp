#pragma once

#include "core/command_variables.hpp"
#include "types.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace media::xblox {

struct ExecutionEvent {
    std::string path;
    std::string kind;
    std::string status;
    std::string message;
    int exit_code = 0;
    nlohmann::json data = nlohmann::json::object();
    std::string block_id;
    std::string type;
    std::vector<std::string> stdout_lines;
    std::vector<std::string> stderr_lines;
    int error_code = 0;
};

struct CommandInvocation {
    std::string path;
    std::string id;
    std::string label;
    std::string action;
    nlohmann::json payload = nlohmann::json::object();
    nlohmann::json command = nlohmann::json::object();
    std::vector<std::string> extra_args;
};

using EventSink = std::function<void(const ExecutionEvent&)>;
using CommandHandler = std::function<int(const CommandInvocation&, std::string* message)>;
using ConsoleCommandHandler = std::function<int(const std::string& line, bool new_shell, bool close_on_exit, std::string* message)>;
using CancelPredicate = std::function<bool()>;

struct ExecutionOptions {
    nlohmann::json custom_commands = nlohmann::json::object();
    std::filesystem::path cli_executable_path;
    std::filesystem::path default_cwd;
    media::commands::VariableContext command_context;
    std::vector<std::string> extra_args;
    bool execute_external_commands = true;
    bool wait_blocks = true;
    bool collect_events = true;
    int max_loop_iterations = 1;
    int default_timeout_ms = 30000;

    CommandHandler app_command_handler;
    CommandHandler ribbon_command_handler;
    CommandHandler open_url_handler;
    CommandHandler open_path_handler;
    ConsoleCommandHandler console_command_handler;
    CancelPredicate cancel_requested;
    EventSink event_sink;
};

struct BlockRunFlags {
    bool enabled = true;
    bool abort_on_error = true;
    bool background = false;
    bool cancellable = true;
    bool consume_events = true;
    int timeout_ms = 0;
};

struct ExecutionResult {
    bool ok = true;
    int exit_code = 0;
    size_t event_count = 0;
    size_t error_count = 0;
    nlohmann::json last_block_result = nullptr;
    std::vector<ExecutionEvent> events;
};

} // namespace media::xblox
