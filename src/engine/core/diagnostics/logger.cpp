#include "logger.hpp"

#include <iomanip>
#include <ctime>

#include "engine/world/engine.hpp"

namespace Shard::Engine::Debugging{
    
    void Logger::AddSink(LogSink sink)
    {
        sinks.push_back(std::move(sink));
    }

    void Logger::EnableTimestamp()
    {
        useTimestamp = true;
    }

    void Logger::EnableFileLogging(const std::string &filepath)
    {
        logFile.open(filepath, std::ios::out | std::ios::app);
    }

    void Logger::SetMinimumLevel(Level level) {
        currentMinLevel = level;
    }

    std::string Logger::LevelToString(Level level) {
        switch (level) {
            case Level::Log:    return "LOG";
            case Level::Info:   return "INFO";
            case Level::Warning:return "WARNING";
            case Level::Error:  return "ERROR";
            case Level::Fatal:  return "FATAL";
        }
        return "UNKNOWN";
    }

    std::string Logger::GetTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);

        std::tm tm{};
    #ifdef _WIN32
        localtime_s(&tm, &now_c);      // Windows
    #else
        localtime_r(&now_c, &tm);      // POSIX
    #endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S]");
        return oss.str();
    }
}