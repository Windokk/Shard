#if defined(_WIN32)

#include "engine/platform/crash/crash_internal.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace Shard::Engine::Core::Platform::CrashHandler::Internal {

    namespace {
        LPTOP_LEVEL_EXCEPTION_FILTER g_PreviousFilter = nullptr;
        std::terminate_handler g_PreviousTerminate = nullptr;
        void (*g_PreviousAbort)(int) = nullptr;
        bool g_Installed = false;

        const char* ExceptionName(DWORD code)
        {
            switch (code)
            {
                case EXCEPTION_ACCESS_VIOLATION:        return "Access violation";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:   return "Array bounds exceeded";
                case EXCEPTION_DATATYPE_MISALIGNMENT:   return "Datatype misalignment";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO:      return "Floating point divide by zero";
                case EXCEPTION_FLT_INVALID_OPERATION:   return "Invalid floating point operation";
                case EXCEPTION_ILLEGAL_INSTRUCTION:     return "Illegal instruction";
                case EXCEPTION_IN_PAGE_ERROR:           return "In page error";
                case EXCEPTION_INT_DIVIDE_BY_ZERO:      return "Integer divide by zero";
                case EXCEPTION_PRIV_INSTRUCTION:        return "Privileged instruction";
                case EXCEPTION_STACK_OVERFLOW:          return "Stack overflow";
                default:                                return "Unhandled exception";
            }
        }

        void DescribeAddress(std::ofstream& report, void* address)
        {
            HMODULE module = nullptr;
            char path[MAX_PATH] = {};
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCSTR>(address), &module) &&
                GetModuleFileNameA(module, path, MAX_PATH))
            {
                const char* name = path;
                for (const char* p = path; *p; ++p)
                    if (*p == '\\' || *p == '/')
                        name = p + 1;
                char line[MAX_PATH + 64];
                std::snprintf(line, sizeof(line), "%s+0x%llx", name,
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
                report << line;
            }
            else
            {
                char line[32];
                std::snprintf(line, sizeof(line), "0x%p", address);
                report << line;
            }
        }

        // The dump is written through dbghelp, loaded only when a crash happens
        std::string WriteMiniDump(const std::string& basePath, EXCEPTION_POINTERS* pointers)
        {
            HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
            if (!dbghelp)
                return {};

            using MiniDumpWriteDumpFn = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
            auto write = reinterpret_cast<MiniDumpWriteDumpFn>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));

            std::string path = basePath + ".dmp";
            bool ok = false;
            if (write)
            {
                HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file != INVALID_HANDLE_VALUE)
                {
                    MINIDUMP_EXCEPTION_INFORMATION exception{};
                    exception.ThreadId = GetCurrentThreadId();
                    exception.ExceptionPointers = pointers;
                    exception.ClientPointers = FALSE;

                    ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithDataSegs,
                               pointers ? &exception : nullptr, nullptr, nullptr) != FALSE;
                    CloseHandle(file);
                }
            }
            FreeLibrary(dbghelp);
            return ok ? path : std::string();
        }

        LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* pointers)
        {
            const DWORD code = pointers && pointers->ExceptionRecord ? pointers->ExceptionRecord->ExceptionCode : 0;
            Report(ExceptionName(code), pointers);

            if (g_PreviousFilter)
                return g_PreviousFilter(pointers);
            return EXCEPTION_EXECUTE_HANDLER;   // terminate : the OS error dialog would only hide the report
        }

        void TerminateHandler()
        {
            Report("std::terminate (uncaught C++ exception)", nullptr);
            std::abort();
        }

        void AbortHandler(int)
        {
            Report("abort()", nullptr);
            std::signal(SIGABRT, SIG_DFL);
            std::raise(SIGABRT);
        }

        BOOL WINAPI ConsoleHandler(DWORD type)
        {
            if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
            {
                CallTerminateHandler();
                return TRUE;
            }
            return FALSE;
        }
    }

    void InstallOS()
    {
        if (g_Installed)
            return;
        g_Installed = true;

        g_PreviousFilter = SetUnhandledExceptionFilter(UnhandledFilter);
        g_PreviousTerminate = std::set_terminate(TerminateHandler);
        g_PreviousAbort = std::signal(SIGABRT, AbortHandler);
    }

    void UninstallOS()
    {
        if (!g_Installed)
            return;
        g_Installed = false;

        SetUnhandledExceptionFilter(g_PreviousFilter);
        std::set_terminate(g_PreviousTerminate);
        std::signal(SIGABRT, g_PreviousAbort ? g_PreviousAbort : SIG_DFL);
    }

    void WriteOSDetails(std::ofstream& report, CrashInfo& info, const std::string& basePath, void* osContext)
    {
        auto* pointers = static_cast<EXCEPTION_POINTERS*>(osContext);

        if (pointers && pointers->ExceptionRecord)
        {
            report << "Exception code : 0x" << std::hex << pointers->ExceptionRecord->ExceptionCode << std::dec << "\n";
            report << "Exception at   : ";
            DescribeAddress(report, pointers->ExceptionRecord->ExceptionAddress);
            report << "\n";
            if (pointers->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pointers->ExceptionRecord->NumberParameters >= 2)
            {
                const ULONG_PTR kind = pointers->ExceptionRecord->ExceptionInformation[0];
                report << (kind == 0 ? "Reading" : kind == 1 ? "Writing" : "Executing") << " address 0x"
                       << std::hex << pointers->ExceptionRecord->ExceptionInformation[1] << std::dec << "\n";
            }
        }

        // Frames of the handler's own stack : the faulting frames are in the minidump
        void* frames[64];
        const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
        report << "\nStack (module+offset) :\n";
        for (USHORT i = 0; i < count; ++i)
        {
            report << "  #" << i << " ";
            DescribeAddress(report, frames[i]);
            report << "\n";
        }
        report.flush();

        info.dumpPath = WriteMiniDump(basePath, pointers);
        if (!info.dumpPath.empty())
            report << "\nMinidump : " << info.dumpPath << "\n";
    }

    void SetTerminateRequestHandlerOS(bool enabled)
    {
        SetConsoleCtrlHandler(ConsoleHandler, enabled ? TRUE : FALSE);
    }
}

#endif
