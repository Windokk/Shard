#pragma once

#include "engine/platform/crash/crash_handler.hpp"

#include <fstream>

namespace Shard::Engine::Core::Platform::CrashHandler::Internal {

    extern CrashHandlerDesc g_Desc;
    extern std::function<void()> g_TerminateHandler;

    CrashInfo Report(const std::string& reason, void* osContext);

    void CallTerminateHandler();

    void InstallOS();
    void UninstallOS();
    void WriteOSDetails(std::ofstream& report, CrashInfo& info, const std::string& basePath, void* osContext);
    void SetTerminateRequestHandlerOS(bool enabled);
}
