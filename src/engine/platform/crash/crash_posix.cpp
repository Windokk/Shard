#if !defined(_WIN32)

#include "engine/platform/crash/crash_internal.hpp"

#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

#if defined(__GLIBC__) || defined(__APPLE__)
    #include <execinfo.h>
#endif

namespace Shard::Engine::Core::Platform::CrashHandler::Internal {

    namespace {
        constexpr int kFatalSignals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS };

        struct sigaction g_Previous[sizeof(kFatalSignals) / sizeof(int)];
        bool g_Installed = false;
        char g_AltStack[64 * 1024];

        const char* SignalName(int sig)
        {
            switch (sig)
            {
                case SIGSEGV: return "SIGSEGV (invalid memory access)";
                case SIGABRT: return "SIGABRT (abort)";
                case SIGFPE:  return "SIGFPE (arithmetic error)";
                case SIGILL:  return "SIGILL (illegal instruction)";
                case SIGBUS:  return "SIGBUS (bus error)";
                default:      return "fatal signal";
            }
        }

        // Not async-signal-safe (allocations, streams) : this is a best effort report of a process that is dying anyway.
        void FatalHandler(int sig, siginfo_t*, void*)
        {
            Report(SignalName(sig), nullptr);

            // Back to the default action so that the OS still produces its core dump / exit status
            signal(sig, SIG_DFL);
            raise(sig);
        }

        void TerminateHandler(int)
        {
            CallTerminateHandler();
        }
    }

    void InstallOS()
    {
        if (g_Installed)
            return;
        g_Installed = true;

        // The handler of a stack overflow needs a stack of its own
        stack_t alt{};
        alt.ss_sp = g_AltStack;
        alt.ss_size = sizeof(g_AltStack);
        sigaltstack(&alt, nullptr);

        struct sigaction action{};
        action.sa_sigaction = FatalHandler;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&action.sa_mask);

        for (size_t i = 0; i < sizeof(kFatalSignals) / sizeof(int); ++i)
            sigaction(kFatalSignals[i], &action, &g_Previous[i]);
    }

    void UninstallOS()
    {
        if (!g_Installed)
            return;
        g_Installed = false;

        for (size_t i = 0; i < sizeof(kFatalSignals) / sizeof(int); ++i)
            sigaction(kFatalSignals[i], &g_Previous[i], nullptr);
    }

    void WriteOSDetails(std::ofstream& report, CrashInfo&, const std::string&, void*)
    {
    #if defined(__GLIBC__) || defined(__APPLE__)
        void* frames[64];
        const int count = backtrace(frames, 64);
        char** symbols = backtrace_symbols(frames, count);
        report << "Stack :\n";
        for (int i = 0; i < count; ++i)
            report << "  #" << i << " " << (symbols ? symbols[i] : "?") << "\n";
        std::free(symbols);
    #else
        report << "No stack trace on this platform.\n";
    #endif
        report.flush();
    }

    void SetTerminateRequestHandlerOS(bool enabled)
    {
        struct sigaction action{};
        action.sa_handler = enabled ? TerminateHandler : SIG_DFL;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
    }
}

#endif
