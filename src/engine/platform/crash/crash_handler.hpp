#pragma once

#include <functional>
#include <string>

namespace Shard::Engine::Core::Platform {

    struct CrashInfo {
        std::string reason;
        std::string reportPath;
        std::string dumpPath;
    };

    struct CrashHandlerDesc {
        std::string appName = "Shard";
        std::string outputDirectory = "crashes";
    };

    // Hooks run in whatever state the crash left. Keep them minimal and do not throw.
    using CrashHook = std::function<void(const CrashInfo&)>;

    namespace CrashHandler {
        void Install(const CrashHandlerDesc& desc = {});
        void Uninstall();

        void AddHook(CrashHook hook);

        // POSIX signal context: only set a flag in the handler.
        void SetTerminateRequestHandler(std::function<void()> handler);

        CrashInfo WriteReport(const std::string& reason);
    }
}
