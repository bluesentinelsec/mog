/**
 * @file log.cpp
 * @brief Process-wide spdlog integration for mog.
 */

#include "mog/log.hpp"

#include <cctype>
#include <mutex>
#include <string>

#if defined(MOG_HAS_SPDLOG) && MOG_HAS_SPDLOG
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace mog
{
namespace
{

std::string ToLower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

#if defined(MOG_HAS_SPDLOG) && MOG_HAS_SPDLOG

std::mutex g_mu;
LoggerPtr g_logger;
LogLevel g_default_level = LogLevel::Warn;

#endif

} // namespace

bool ParseLogLevel(std::string_view text, LogLevel &out)
{
    const std::string lower = ToLower(text);
    if (lower == "trace")
    {
        out = LogLevel::Trace;
        return true;
    }
    if (lower == "debug" || lower == "verbose")
    {
        out = LogLevel::Debug;
        return true;
    }
    if (lower == "info")
    {
        out = LogLevel::Info;
        return true;
    }
    if (lower == "warn" || lower == "warning")
    {
        out = LogLevel::Warn;
        return true;
    }
    if (lower == "error" || lower == "err")
    {
        out = LogLevel::Error;
        return true;
    }
    if (lower == "critical" || lower == "crit" || lower == "fatal")
    {
        out = LogLevel::Critical;
        return true;
    }
    if (lower == "off" || lower == "none" || lower == "silent")
    {
        out = LogLevel::Off;
        return true;
    }
    return false;
}

std::string_view ToString(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warn:
        return "warn";
    case LogLevel::Error:
        return "error";
    case LogLevel::Critical:
        return "critical";
    case LogLevel::Off:
        return "off";
    }
    return "unknown";
}

#if defined(MOG_HAS_SPDLOG) && MOG_HAS_SPDLOG

spdlog::level::level_enum ToSpdlogLevel(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warn:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Critical:
        return spdlog::level::critical;
    case LogLevel::Off:
        return spdlog::level::off;
    }
    return spdlog::level::info;
}

LogLevel FromSpdlogLevel(spdlog::level::level_enum level) noexcept
{
    switch (level)
    {
    case spdlog::level::trace:
        return LogLevel::Trace;
    case spdlog::level::debug:
        return LogLevel::Debug;
    case spdlog::level::info:
        return LogLevel::Info;
    case spdlog::level::warn:
        return LogLevel::Warn;
    case spdlog::level::err:
        return LogLevel::Error;
    case spdlog::level::critical:
        return LogLevel::Critical;
    case spdlog::level::off:
        return LogLevel::Off;
    default:
        return LogLevel::Info;
    }
}

LoggerPtr MakeDefaultLogger(LogLevel level)
{
    auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("mog", sink);
    logger->set_level(ToSpdlogLevel(level));
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    logger->flush_on(spdlog::level::warn);
    return logger;
}

void SetLogger(LoggerPtr logger)
{
    std::lock_guard lock(g_mu);
    g_logger = std::move(logger);
}

void UseDefaultLogger(LogLevel level)
{
    auto logger = MakeDefaultLogger(level);
    std::lock_guard lock(g_mu);
    g_default_level = level;
    g_logger = std::move(logger);
}

LoggerPtr GetLogger()
{
    std::lock_guard lock(g_mu);
    if (!g_logger)
    {
        g_logger = MakeDefaultLogger(g_default_level);
    }
    return g_logger;
}

void SetLogLevel(LogLevel level)
{
    std::lock_guard lock(g_mu);
    g_default_level = level;
    if (!g_logger)
    {
        g_logger = MakeDefaultLogger(level);
    }
    else
    {
        g_logger->set_level(ToSpdlogLevel(level));
    }
}

LogLevel GetLogLevel()
{
    auto logger = GetLogger();
    return FromSpdlogLevel(logger->level());
}

#endif // MOG_HAS_SPDLOG

} // namespace mog
