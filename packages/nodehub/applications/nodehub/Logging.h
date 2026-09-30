#ifndef LOGGING_H
#define LOGGING_H

#include <memory>
#include <string>

#include <spdlog/fmt/bundled/printf.h>

// Include spdlog for header-only mode
#ifndef DISABLE_LOGGING
#define SPDLOG_HEADER_ONLY
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

// Global spdlog logger instance
extern std::shared_ptr<spdlog::logger> g_logger;
#else
// When logging disabled, stub out
namespace spdlog {
    class logger;
    namespace level {
        enum level_enum : int;
    }
}
extern std::shared_ptr<spdlog::logger> g_logger;
#endif

// Initialize logging system
void InitLogger(const char* app_name = "app", bool console = true, bool file = true, const char* filename = "app.log");

// Shutdown logging system
void ShutdownLogger();

// Parse log level string helper (returns debug on invalid input)
spdlog::level::level_enum ParseLogLevel(const std::string& level_name, bool* out_valid = nullptr);

// Update the global logger and sinks to the provided level
void SetLoggerLevel(spdlog::level::level_enum level);

// printf-style helpers (preserve existing formatting strings)
template<typename... Args>
inline void LogTracef(const char* fmt, Args&&... args)
{
#ifndef DISABLE_LOGGING
    if (g_logger)
        g_logger->trace(fmt::sprintf(fmt, std::forward<Args>(args)...));
#else
    (void)fmt;
    ((void)std::initializer_list<int>{((void)args, 0)...});
#endif
}

template<typename... Args>
inline void LogDebugf(const char* fmt, Args&&... args)
{
#ifndef DISABLE_LOGGING
    if (g_logger)
        g_logger->debug(fmt::sprintf(fmt, std::forward<Args>(args)...));
#else
    (void)fmt;
    ((void)std::initializer_list<int>{((void)args, 0)...});
#endif
}

template<typename... Args>
inline void LogInfof(const char* fmt, Args&&... args)
{
#ifndef DISABLE_LOGGING
    if (g_logger)
        g_logger->info(fmt::sprintf(fmt, std::forward<Args>(args)...));
#else
    (void)fmt;
    ((void)std::initializer_list<int>{((void)args, 0)...});
#endif
}

template<typename... Args>
inline void LogWarnf(const char* fmt, Args&&... args)
{
#ifndef DISABLE_LOGGING
    if (g_logger)
        g_logger->warn(fmt::sprintf(fmt, std::forward<Args>(args)...));
#else
    (void)fmt;
    ((void)std::initializer_list<int>{((void)args, 0)...});
#endif
}

template<typename... Args>
inline void LogErrorf(const char* fmt, Args&&... args)
{
#ifndef DISABLE_LOGGING
    if (g_logger)
        g_logger->error(fmt::sprintf(fmt, std::forward<Args>(args)...));
#else
    (void)fmt;
    ((void)std::initializer_list<int>{((void)args, 0)...});
#endif
}

template<typename... Args>
inline void LogCriticalf(const char* fmt, Args&&... args)
{
#ifndef DISABLE_LOGGING
    if (g_logger)
        g_logger->critical(fmt::sprintf(fmt, std::forward<Args>(args)...));
#else
    (void)fmt;
    ((void)std::initializer_list<int>{((void)args, 0)...});
#endif
}

// -----------------------------------------------------------------------------
// Logging macros - can be disabled via DISABLE_LOGGING
// -----------------------------------------------------------------------------
#ifndef DISABLE_LOGGING
  // Main logging macros using spdlog
  #define LOG_TRACE(...)    if (g_logger) g_logger->trace(__VA_ARGS__)
  #define LOG_DEBUG(...)    if (g_logger) g_logger->debug(__VA_ARGS__)
  #define LOG_INFO(...)     if (g_logger) g_logger->info(__VA_ARGS__)
  #define LOG_WARN(...)     if (g_logger) g_logger->warn(__VA_ARGS__)
  #define LOG_ERROR(...)    if (g_logger) g_logger->error(__VA_ARGS__)
  #define LOG_CRITICAL(...) if (g_logger) g_logger->critical(__VA_ARGS__)
  
  // Legacy compatibility (map old names to spdlog levels)
  #define LOG_FATAL(...)    LOG_CRITICAL(__VA_ARGS__)
  #define LOG_VERBOSE(...)  LOG_TRACE(__VA_ARGS__)
#else
  // Stub implementations when logging is disabled
  #define LOG_TRACE(...)    (void)0
  #define LOG_DEBUG(...)    (void)0
  #define LOG_INFO(...)     (void)0
  #define LOG_WARN(...)     (void)0
  #define LOG_ERROR(...)    (void)0
  #define LOG_CRITICAL(...) (void)0
  #define LOG_FATAL(...)    (void)0
  #define LOG_VERBOSE(...)  (void)0
#endif

#endif // LOGGING_H
