#pragma once

#include "engine/platform/crash/crash_handler.hpp"

#include <fstream>

// Shared between the common part (crash_handler.cpp) and the OS parts (crash_win32.cpp / crash_posix.cpp)
namespace Shard::Engine::Core::Platform::CrashHandler::Internal {

    extern CrashHandlerDesc g_Desc;
    extern std::function<void()> g_TerminateHandler;

    /// @brief Writes the report + minidump and runs the hooks. osContext is what the OS gave to the handler.
    CrashInfo Report(const std::string& reason, void* osContext);

    void CallTerminateHandler();

    // Implemented by the OS
    void InstallOS();
    void UninstallOS();
    /// @brief Appends the OS specific parts to the report (stack trace) and writes the minidump if the OS has them.
    void WriteOSDetails(std::ofstream& report, CrashInfo& info, const std::string& basePath, void* osContext);
    void SetTerminateRequestHandlerOS(bool enabled);
}
