#pragma once

#include <CLI/CLI.hpp>
#include <string>
#include <functional>
#include "kbot.h"

namespace polymech {

/// Attach kbot subcommands to the main app
CLI::App* setup_cmd_kbot(CLI::App& app);

/// CLI Entry points
int run_cmd_kbot_ai();
int run_cmd_kbot_run();

/// IPC / UDS Entry points (implemented in src/cmd_kbot*.cpp — not in libkbot; compile those TU into your binary).
int run_kbot_ai_ipc(const std::string& payload, const std::string& jobId, const kbot::KBotCallbacks& cb);
int run_kbot_run_ipc(const std::string& payload, const std::string& jobId, const kbot::KBotCallbacks& cb);

/// Standalone UDS/TCP server for KBot (orchestrator tests, LLM worker).
int run_cmd_kbot_uds(const std::string& pipe_path);

/// Helper to check parsed state
bool is_kbot_ai_parsed();
bool is_kbot_run_parsed();

} // namespace polymech
