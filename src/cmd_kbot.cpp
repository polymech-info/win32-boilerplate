#include "cmd_kbot.h"
#include "logger/logger.h"
#include <CLI/CLI.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <toml++/toml.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace polymech {

// Helper to reliably extract API keys for any router from postgres.toml [services] section
static std::string get_api_key_for_router(const toml::table& cfg, const std::string& router) {
    if (router == "ollama") return "ollama";
    std::string key = router.empty() ? "OPENROUTER" : router;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::toupper(c); });
    
    if (key == "OPENAI") {
        auto val = cfg["services"]["OPENAI_KEY"].value_or(std::string(""));
        if (!val.empty()) return val;
    }
    
    return cfg["services"][key].value_or(std::string(""));
}

// Global states for CLI mode
static kbot::KBotOptions g_kbot_opts;
static kbot::KBotRunOptions g_run_opts;

static CLI::App* ai_cmd = nullptr;
static CLI::App* run_cmd = nullptr;

CLI::App* setup_cmd_kbot(CLI::App& app) {
    auto* kbot_cmd = app.add_subcommand("kbot", "KBot AI & Task runner");
    
    // ── AI Subcommand ──
    ai_cmd = kbot_cmd->add_subcommand("ai", "Run KBot AI tasks");
    ai_cmd->add_option("-p,--path", g_kbot_opts.path, "Target directory")->default_val(".");
    ai_cmd->add_option("--prompt", g_kbot_opts.prompt, "The prompt. Supports file paths and vars.");
    ai_cmd->add_option("-c,--config", g_kbot_opts.config_path, "Config file for API Keys")->default_val("config/postgres.toml");
    /* Same destination as TS `onCompletion`: write LLM text here (--dst wins if both appear in IPC JSON). */
    ai_cmd->add_option(
        "--dst,--output",
        g_kbot_opts.dst,
        "Write completion result to this path (${MODEL}, ${ROUTER} expanded). Same as --output.");
    ai_cmd->add_option("--append", g_kbot_opts.append, "How to handle output if --dst exists: concat|merge|replace")->default_val("concat");
    ai_cmd->add_option("--wrap", g_kbot_opts.wrap, "Specify how to wrap output: meta|none")->default_val("none");
    ai_cmd->add_option("--each", g_kbot_opts.each, "Iterate over items (GLOB, JSON, array)");
    ai_cmd->add_option("--disable", g_kbot_opts.disable, "Disable tools categories");
    ai_cmd->add_option("--disableTools", g_kbot_opts.disable_tools, "Specific tools to disable");
    ai_cmd->add_option("--tools", g_kbot_opts.tools, "Tools to use");
    ai_cmd->add_option("--include", g_kbot_opts.include_globs, "Glob patterns or paths to include");
    ai_cmd->add_option("--exclude", g_kbot_opts.exclude_globs, "Glob patterns or paths to exclude");
    ai_cmd->add_option("--globExtension", g_kbot_opts.glob_extension, "Glob extension behavior (e.g. match-cpp)");
    ai_cmd->add_option("--api_key", g_kbot_opts.api_key, "Explicit API key to use");
    ai_cmd->add_option("--model", g_kbot_opts.model, "AI model to use");
    ai_cmd->add_option("--router", g_kbot_opts.router, "Router to use (openai, openrouter, deepseek)")->default_val("openrouter");
    ai_cmd->add_option("--mode", g_kbot_opts.mode, "Chat completion mode")->default_val("tools");
    ai_cmd->add_option(
        "--response-format",
        g_kbot_opts.response_format_json,
        R"(Chat completions response_format JSON, e.g. {"type":"json_object"} for structured output)");
    ai_cmd->add_flag("--dry", g_kbot_opts.dry_run, "Dry run");

    // ── Run Subcommand ──
    run_cmd = kbot_cmd->add_subcommand("run", "Run a .vscode/launch.json configuration");
    run_cmd->add_option("-c,--config", g_run_opts.config, "Config name")->default_val("default");
    run_cmd->add_flag("--dry", g_run_opts.dry, "Dry run");
    run_cmd->add_flag("--list", g_run_opts.list, "List available configs");
    run_cmd->add_option("--projectPath", g_run_opts.project_path, "Project path")->default_val(".");
    run_cmd->add_option("--logFilePath", g_run_opts.log_file_path, "Log file path");

    return kbot_cmd;
}

bool is_kbot_ai_parsed() { return ai_cmd != nullptr && ai_cmd->parsed(); }
bool is_kbot_run_parsed() { return run_cmd != nullptr && run_cmd->parsed(); }

int run_cmd_kbot_ai() {
    // Fallback logic if API key isn't explicitly provided on CLI
    if (g_kbot_opts.api_key.empty() && !g_kbot_opts.config_path.empty()) {
        try {
            auto cfg = toml::parse_file(g_kbot_opts.config_path);
            g_kbot_opts.api_key = get_api_key_for_router(cfg, g_kbot_opts.router);
            logger::debug("Loaded API Key from fallback config: " + g_kbot_opts.config_path);
        } catch (const std::exception& e) {
            logger::warn("Failed to load generic fallback kbot config: " + std::string(e.what()));
        }
    }

    return kbot::run_kbot_ai_pipeline(g_kbot_opts, kbot::KBotCallbacks{});
}

int run_cmd_kbot_run() {
    return kbot::run_kbot_run_pipeline(g_run_opts, kbot::KBotCallbacks{});
}

int run_kbot_ai_ipc(const std::string& payload, const std::string& jobId, const kbot::KBotCallbacks& cb) {
    kbot::KBotOptions opts;
    opts.job_id = jobId;
    opts.config_path = "config/postgres.toml"; // Fixed path for IPC worker

    // Optional: Parse JSON from payload to overwrite opts variables using rapidjson
    rapidjson::Document doc;
    doc.Parse(payload.c_str());
    if (!doc.HasParseError() && doc.IsObject()) {
        if (doc.HasMember("prompt") && doc["prompt"].IsString()) opts.prompt = doc["prompt"].GetString();
        if (doc.HasMember("path") && doc["path"].IsString()) opts.path = doc["path"].GetString();
        if (doc.HasMember("include") && doc["include"].IsArray()) {
            for (const auto& v : doc["include"].GetArray()) {
                if (v.IsString()) opts.include_globs.push_back(v.GetString());
            }
        }
        if (doc.HasMember("exclude") && doc["exclude"].IsArray()) {
            for (const auto& v : doc["exclude"].GetArray()) {
                if (v.IsString()) opts.exclude_globs.push_back(v.GetString());
            }
        }
        if (doc.HasMember("dry_run") && doc["dry_run"].IsBool()) opts.dry_run = doc["dry_run"].GetBool();
        if (doc.HasMember("api_key") && doc["api_key"].IsString()) opts.api_key = doc["api_key"].GetString();
        if (doc.HasMember("router") && doc["router"].IsString()) opts.router = doc["router"].GetString();
        if (doc.HasMember("model") && doc["model"].IsString()) opts.model = doc["model"].GetString();
        if (doc.HasMember("base_url") && doc["base_url"].IsString()) opts.base_url = doc["base_url"].GetString();
        else if (doc.HasMember("baseURL") && doc["baseURL"].IsString()) opts.base_url = doc["baseURL"].GetString();
        /* Single numeric path: RapidJSON may store JSON integers as int, uint64, or double — GetDouble() is consistent. */
        if (doc.HasMember("llm_timeout_ms") && doc["llm_timeout_ms"].IsNumber() && !doc["llm_timeout_ms"].IsNull()) {
            const rapidjson::Value& v = doc["llm_timeout_ms"];
            const double d = v.GetDouble();
            if (d > 0.0 && std::isfinite(d)) {
                const long long ms = static_cast<long long>(std::llround(d));
                const int cap = std::numeric_limits<int>::max();
                if (ms > 0 && ms <= static_cast<long long>(cap)) {
                    opts.llm_timeout_ms = static_cast<int>(ms);
                }
            }
        }
        if (doc.HasMember("response_format")) {
            const rapidjson::Value& rf = doc["response_format"];
            if (rf.IsObject()) {
                rapidjson::StringBuffer sb;
                rapidjson::Writer<rapidjson::StringBuffer> w(sb);
                rf.Accept(w);
                opts.response_format_json.assign(sb.GetString(), sb.GetSize());
            } else if (rf.IsString()) {
                opts.response_format_json = rf.GetString();
            }
        }
        if (doc.HasMember("dst") && doc["dst"].IsString())
            opts.dst = doc["dst"].GetString();
        if (doc.HasMember("output") && doc["output"].IsString()) {
            if (opts.dst.empty())
                opts.dst = doc["output"].GetString();
        }
    }

    if (opts.api_key.empty()) {
        try {
            auto cfg = toml::parse_file(opts.config_path);
            opts.api_key = get_api_key_for_router(cfg, opts.router);
        } catch (...) {}
    }

    logger::info("Receiving AI task over IPC... job: " + jobId);
    if (opts.llm_timeout_ms > 0) {
        logger::info("kbot-ai IPC: llm_timeout_ms=" + std::to_string(opts.llm_timeout_ms));
    }
    return kbot::run_kbot_ai_pipeline(opts, cb);
}

int run_kbot_run_ipc(const std::string& payload, const std::string& jobId, const kbot::KBotCallbacks& cb) {
    kbot::KBotRunOptions opts;
    opts.job_id = jobId;

    rapidjson::Document doc;
    doc.Parse(payload.c_str());
    if (!doc.HasParseError() && doc.IsObject()) {
        if (doc.HasMember("config") && doc["config"].IsString()) opts.config = doc["config"].GetString();
        if (doc.HasMember("dry") && doc["dry"].IsBool()) opts.dry = doc["dry"].GetBool();
    }

    logger::info("Receiving run task over IPC... job: " + jobId);
    return kbot::run_kbot_run_pipeline(opts, cb);
}

} // namespace polymech
