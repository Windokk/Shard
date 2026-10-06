#if defined(_WIN32)

#include "engine/platform/process/process.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>

namespace Shard::Engine::Core::Platform {

    namespace {
        std::wstring ToWide(const std::string& utf8)
        {
            if (utf8.empty())
                return {};
            const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), length);
            return wide;
        }

        std::string ToUtf8(const std::wstring& wide)
        {
            if (wide.empty())
                return {};
            const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), length, nullptr, nullptr);
            return utf8;
        }

        // Quoting rules of CommandLineToArgvW / the MSVC runtime
        std::wstring QuoteArgument(const std::wstring& arg)
        {
            if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
                return arg;

            std::wstring quoted = L"\"";
            for (size_t i = 0;; ++i)
            {
                size_t backslashes = 0;
                while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }

                if (i == arg.size())
                {
                    quoted.append(backslashes * 2, L'\\');
                    break;
                }
                if (arg[i] == L'"')
                {
                    quoted.append(backslashes * 2 + 1, L'\\');
                    quoted.push_back(L'"');
                }
                else
                {
                    quoted.append(backslashes, L'\\');
                    quoted.push_back(arg[i]);
                }
            }
            quoted.push_back(L'"');
            return quoted;
        }
    }

    std::optional<std::string> GetEnv(const std::string& name)
    {
        const std::wstring wideName = ToWide(name);
        const DWORD length = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
        if (length == 0)
            return std::nullopt;

        std::wstring value(length, L'\0');
        const DWORD written = GetEnvironmentVariableW(wideName.c_str(), value.data(), length);
        value.resize(written);
        return ToUtf8(value);
    }

    bool SetEnv(const std::string& name, const std::string& value)
    {
        return SetEnvironmentVariableW(ToWide(name).c_str(), ToWide(value).c_str()) != 0;
    }

    uint32_t CurrentProcessId()
    {
        return GetCurrentProcessId();
    }

    std::string ExecutablePath()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return {};
            if (length < buffer.size())
            {
                buffer.resize(length);
                break;
            }
            buffer.resize(buffer.size() * 2);
        }
        std::string path = ToUtf8(buffer);
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    std::string ExecutableDirectory()
    {
        const std::string path = ExecutablePath();
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }

    std::string WorkingDirectory()
    {
        const DWORD length = GetCurrentDirectoryW(0, nullptr);
        if (length == 0)
            return {};
        std::wstring buffer(length, L'\0');
        const DWORD written = GetCurrentDirectoryW(length, buffer.data());
        buffer.resize(written);
        std::string path = ToUtf8(buffer);
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    }

    bool SetWorkingDirectory(const std::string& path)
    {
        return SetCurrentDirectoryW(ToWide(path).c_str()) != 0;
    }

    struct Process::Impl {
        HANDLE process = nullptr;
        DWORD id = 0;
    };

    std::unique_ptr<Process> Process::Launch(const std::string& executable, const std::vector<std::string>& arguments,
                                             const std::string& workingDirectory)
    {
        std::wstring commandLine = QuoteArgument(ToWide(executable));
        for (const std::string& argument : arguments)
            commandLine += L" " + QuoteArgument(ToWide(argument));

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};

        const std::wstring directory = ToWide(workingDirectory);
        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            directory.empty() ? nullptr : directory.c_str(), &startup, &info))
            return nullptr;

        CloseHandle(info.hThread);

        std::unique_ptr<Process> process(new Process());
        process->m_Impl = std::make_unique<Impl>();
        process->m_Impl->process = info.hProcess;
        process->m_Impl->id = info.dwProcessId;
        return process;
    }

    Process::~Process()
    {
        if (m_Impl && m_Impl->process)
            CloseHandle(m_Impl->process);
    }

    uint32_t Process::Id() const
    {
        return m_Impl->id;
    }

    bool Process::IsRunning()
    {
        return WaitForSingleObject(m_Impl->process, 0) == WAIT_TIMEOUT;
    }

    std::optional<int> Process::Wait(int timeoutMs)
    {
        const DWORD result = WaitForSingleObject(m_Impl->process, timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));
        if (result != WAIT_OBJECT_0)
            return std::nullopt;

        DWORD code = 0;
        GetExitCodeProcess(m_Impl->process, &code);
        return static_cast<int>(code);
    }

    void Process::Kill()
    {
        TerminateProcess(m_Impl->process, 1);
    }

    void RevealInFileManager(const std::string& path, bool select)
    {
        std::wstring native = ToWide(path);
        std::replace(native.begin(), native.end(), L'/', L'\\');

        const std::wstring args = select ? L"/select,\"" + native + L"\"" : L"\"" + native + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

    bool OpenWithDefaultApplication(const std::string& pathOrUrl)
    {
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", ToWide(pathOrUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }
}

#endif
