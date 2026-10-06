#pragma once

#include <functional>
#include <string>

namespace Shard::Engine::Core::Platform {

    struct CrashInfo {
        std::string reason;         // "Access violation", "SIGSEGV", ...
        std::string reportPath;     // text report written next to the dump (empty if it could not be written)
        std::string dumpPath;       // minidump (Windows only, empty elsewhere or on failure)
    };

    struct CrashHandlerDesc {
        std::string appName = "Shard";
        std::string outputDirectory = "crashes";    // created when the first crash is written
    };

    /// @brief Hooks to run when the process crashes (after the report is written) : flush the logs, save an
    /// emergency copy of the open world, ... The hook runs in the state the crash left the process in : keep it
    /// to what is most likely to still work, and don't throw.
    using CrashHook = std::function<void(const CrashInfo&)>;

    namespace CrashHandler {
        /// @brief Installs the handlers : unhandled exceptions / SEH and abort/terminate on Windows, fatal signals
        /// (SEGV, ABRT, FPE, ILL, BUS) on POSIX. Safe to call again to change the description.
        void Install(const CrashHandlerDesc& desc = {});
        void Uninstall();

        void AddHook(CrashHook hook);

        /// @brief Called when the user asks the process to stop (Ctrl+C, SIGINT / SIGTERM, console close) : lets a
        /// headless server or a tool finish what it does instead of dying. Called from a signal context on POSIX :
        /// only set a flag in there.
        void SetTerminateRequestHandler(std::function<void()> handler);

        /// @brief Writes the report as if a crash happened, and calls the hooks. For tests and for fatal errors the engine detects itself.
        CrashInfo WriteReport(const std::string& reason);
    }
}
