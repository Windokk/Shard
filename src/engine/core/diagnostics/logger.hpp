#pragma once

#include <string>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <functional>
#include <mutex>

namespace Shard::Engine::Debugging{
    
    enum class Level {
        Log = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
        Fatal = 4
    };

    class Logger {
    public:

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        // Provide access to the singleton instance (only used for initilization)
        static Logger& GetInstance() {
            static Logger instance;
            return instance;
        }

        using LogSink = std::function<void(const Level&, const std::string&)>;

        void AddSink(LogSink sink);

        template<typename... Args>
        void Log(Level level, const char* file, int line, Args&&... args) {
            if (static_cast<int>(level) < static_cast<int>(currentMinLevel))
                return;

            std::ostringstream ss;
            ss << "[" << LevelToString(level) << "] ";
            if (useTimestamp)
                ss << GetTimestamp() << " ";
            ss << "(" << file << ":" << line << ") ";
            (ss << ... << args);

            std::string output = ss.str();

            // Log() is reachable from background asset-decode worker threads (e.g. ReadFile/stbi/ufbx
            // failure paths during async level loading) as well as the main thread, so the whole body
            // (sinks, std::cout, logFile) needs to be serialized.
            std::lock_guard<std::mutex> lock(logMutex);

            for(auto& sink : sinks){
                sink(level, output);
            }
            std::cout << output << std::endl;
            if (logFile.is_open())
                logFile << output << std::endl;

            if (level == Level::Fatal) {
                std::cout << "Fatal error! Press Enter to exit..." << std::endl;
                std::cin.get();
                std::terminate();
            }
        }

        void EnableTimestamp();
        void EnableFileLogging(const std::string& filepath);
        void SetMinimumLevel(Level level);

    private:
        std::string LevelToString(Level level);
        std::string GetTimestamp();
        Level currentMinLevel;
        std::ofstream logFile;

        bool useTimestamp;

        std::vector<LogSink> sinks;

        std::mutex logMutex;

        Logger() = default;
    };

    #if defined(BUILD_ENGINE)

        // Used by the EXE/engine
        inline Logger* gSharedLoggerPtr = nullptr;

        inline void SetLogger(Logger* ptr) {
            gSharedLoggerPtr = ptr;
        }

        inline Logger& GetLogger() {
            return gSharedLoggerPtr ? *gSharedLoggerPtr : Logger::GetInstance();
        }

    #elif defined(BUILD_GAME)

        // Used by the Game Module DLL
        inline Logger* gSharedLoggerPtr = nullptr;

        inline void SetLogger(Logger* ptr) {
            gSharedLoggerPtr = ptr;
        }

        inline Logger& GetLogger() {
            return gSharedLoggerPtr ? *gSharedLoggerPtr : Logger::GetInstance();
        }
    #endif

}

#define DEBUG_LOG(...)       Shard::Engine::Debugging::GetLogger().Log(Shard::Engine::Debugging::Level::Log, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define DEBUG_INFO(...)      Shard::Engine::Debugging::GetLogger().Log(Shard::Engine::Debugging::Level::Info, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define DEBUG_WARNING(...)   Shard::Engine::Debugging::GetLogger().Log(Shard::Engine::Debugging::Level::Warning, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define DEBUG_ERROR(...)     Shard::Engine::Debugging::GetLogger().Log(Shard::Engine::Debugging::Level::Error, __FILE_NAME__, __LINE__, __VA_ARGS__)
#define DEBUG_FATAL(...)     Shard::Engine::Debugging::GetLogger().Log(Shard::Engine::Debugging::Level::Fatal, __FILE_NAME__, __LINE__, __VA_ARGS__)