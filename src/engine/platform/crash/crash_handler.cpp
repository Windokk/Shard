#include "engine/platform/crash/crash_handler.hpp"
#include "engine/platform/crash/crash_internal.hpp"

#include "engine/platform/process/process.hpp"

#include <csignal>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace Shard::Engine::Core::Platform {

    namespace CrashHandler {

        namespace Internal {
            CrashHandlerDesc g_Desc;
            std::function<void()> g_TerminateHandler;

            namespace {
                std::vector<CrashHook> g_Hooks;
                std::mutex g_Mutex;
            }

            namespace {
                // One report at a time, and none while a report is already being written (a crash in a hook)
                bool g_Reporting = false;
            }

            CrashInfo Report(const std::string& reason, void* osContext)
            {
                CrashInfo info;
                info.reason = reason;

                {
                    std::lock_guard<std::mutex> lock(g_Mutex);
                    if (g_Reporting)
                        return info;
                    g_Reporting = true;
                }

                std::error_code ec;
                std::filesystem::create_directories(g_Desc.outputDirectory, ec);

                const std::time_t now = std::time(nullptr);
                char stamp[32] = {};
                std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));

                const std::string basePath = g_Desc.outputDirectory + "/" + g_Desc.appName + "_" + stamp + "_" + std::to_string(CurrentProcessId());

                std::ofstream report(basePath + ".txt");
                if (report)
                {
                    report << g_Desc.appName << " crash report\n";
                    report << "Reason : " << reason << "\n";
                    report << "Time   : " << stamp << "\n";
                    report << "PID    : " << CurrentProcessId() << "\n";
                    report << "Exe    : " << ExecutablePath() << "\n\n";
                    info.reportPath = basePath + ".txt";
                    WriteOSDetails(report, info, basePath, osContext);
                }

                for (const CrashHook& hook : g_Hooks)
                    if (hook)
                        hook(info);

                {
                    std::lock_guard<std::mutex> lock(g_Mutex);
                    g_Reporting = false;
                }
                return info;
            }

            void CallTerminateHandler()
            {
                if (g_TerminateHandler)
                    g_TerminateHandler();
            }
        }

        void Install(const CrashHandlerDesc& desc)
        {
            Internal::g_Desc = desc;
            Internal::InstallOS();
        }

        void Uninstall()
        {
            Internal::UninstallOS();
        }

        void AddHook(CrashHook hook)
        {
            std::lock_guard<std::mutex> lock(Internal::g_Mutex);
            Internal::g_Hooks.push_back(std::move(hook));
        }

        void SetTerminateRequestHandler(std::function<void()> handler)
        {
            Internal::g_TerminateHandler = std::move(handler);
            Internal::SetTerminateRequestHandlerOS(static_cast<bool>(Internal::g_TerminateHandler));
        }

        CrashInfo WriteReport(const std::string& reason)
        {
            return Internal::Report(reason, nullptr);
        }
    }
}
