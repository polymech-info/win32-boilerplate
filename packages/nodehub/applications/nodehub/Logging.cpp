#include "Logging.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <algorithm>
#include <cctype>
#include <vector>

// Global logger instance
std::shared_ptr<spdlog::logger> g_logger = nullptr;

void InitLogger(const char* app_name, bool console, bool file, const char* filename)
{
    try
    {
        // Create sinks vector
        std::vector<spdlog::sink_ptr> sinks;
        
        // Add console sink if requested (with color support)
        if (console)
        {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::trace);
            console_sink->set_pattern("[%H:%M:%S:%e] [%^%l%$] [%n] %v");
            sinks.push_back(console_sink);
        }
        
        // Add file sink if requested
        if (file && filename)
        {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true);
            file_sink->set_level(spdlog::level::trace);
            file_sink->set_pattern("[%Y-%m-%d %H:%M:%S:%e] [%l] [%n] %v");
            sinks.push_back(file_sink);
        }
        
        // Create logger with multiple sinks
        g_logger = std::make_shared<spdlog::logger>(app_name, sinks.begin(), sinks.end());
        
        // Register logger with spdlog
        spdlog::register_logger(g_logger);
        
        // Set as default logger
        spdlog::set_default_logger(g_logger);

        // Flush on every log (can be adjusted for performance)
        g_logger->flush_on(spdlog::level::trace);
        
        // Default to debug verbosity unless configured otherwise
        SetLoggerLevel(spdlog::level::debug);
    }
    catch (const spdlog::spdlog_ex& ex)
    {
        // fprintf(stderr, "Log initialization failed: %s\n", ex.what());
    }
}

void ShutdownLogger()
{
    if (g_logger)
    {
        g_logger->flush();
        g_logger.reset();
    }
    
    spdlog::shutdown();
}

spdlog::level::level_enum ParseLogLevel(const std::string& level_name, bool* out_valid)
{
    if (out_valid)
        *out_valid = true;

    if (level_name.empty())
        return spdlog::level::debug;

    auto first = std::find_if_not(level_name.begin(), level_name.end(), [](unsigned char c) {
        return std::isspace(c);
    });
    auto last = std::find_if_not(level_name.rbegin(), level_name.rend(), [](unsigned char c) {
        return std::isspace(c);
    }).base();

    if (first == level_name.end() || first >= last)
        return spdlog::level::debug;

    std::string normalized;
    normalized.resize(static_cast<size_t>(last - first));
    std::transform(first, last, normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (normalized == "trace" || normalized == "t")
        return spdlog::level::trace;
    if (normalized == "debug" || normalized == "d")
        return spdlog::level::debug;
    if (normalized == "info" || normalized == "information" || normalized == "i")
        return spdlog::level::info;
    if (normalized == "warn" || normalized == "warning" || normalized == "w")
        return spdlog::level::warn;
    if (normalized == "error" || normalized == "err" || normalized == "e")
        return spdlog::level::err;
    if (normalized == "critical" || normalized == "fatal" || normalized == "c" || normalized == "f")
        return spdlog::level::critical;
    if (normalized == "off" || normalized == "none" || normalized == "disable" || normalized == "disabled")
        return spdlog::level::off;

    if (out_valid)
        *out_valid = false;

    return spdlog::level::debug;
}

void SetLoggerLevel(spdlog::level::level_enum level)
{
    spdlog::set_level(level);

    if (g_logger)
    {
        g_logger->set_level(level);

        for (const auto& sink : g_logger->sinks())
        {
            if (sink)
            {
                sink->set_level(level);
            }
        }
    }
}
