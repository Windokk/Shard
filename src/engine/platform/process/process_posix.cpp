#if !defined(_WIN32)

#include "engine/platform/process/process.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

#if defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

extern char** environ;

namespace Shard::Engine::Core::Platform {

    std::optional<std::string> GetEnv(const std::string& name)
    {
        const char* value = std::getenv(name.c_str());
        if (!value)
            return std::nullopt;
        return std::string(value);
    }

    bool SetEnv(const std::string& name, const std::string& value)
    {
        return setenv(name.c_str(), value.c_str(), 1) == 0;
    }

    uint32_t CurrentProcessId()
    {
        return static_cast<uint32_t>(getpid());
    }

    std::string ExecutablePath()
    {
    #if defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            return {};
        return std::filesystem::weakly_canonical(buffer.c_str()).string();
    #else
        std::string buffer(4096, '\0');
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length <= 0)
            return {};
        buffer.resize(static_cast<size_t>(length));
        return buffer;
    #endif
    }

    std::string ExecutableDirectory()
    {
        const std::string path = ExecutablePath();
        const size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }

    std::string WorkingDirectory()
    {
        std::string buffer(4096, '\0');
        if (!getcwd(buffer.data(), buffer.size()))
            return {};
        buffer.resize(std::strlen(buffer.c_str()));
        return buffer;
    }

    bool SetWorkingDirectory(const std::string& path)
    {
        return chdir(path.c_str()) == 0;
    }

    struct Process::Impl {
        pid_t pid = -1;
        bool exited = false;
        int exitCode = 0;

        // Collects the exit status if the child is done. Returns true once it has exited.
        bool Poll(bool block)
        {
            if (exited)
                return true;
            int status = 0;
            const pid_t result = waitpid(pid, &status, block ? 0 : WNOHANG);
            if (result == pid)
            {
                exited = true;
                exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
            }
            return exited;
        }
    };

    std::unique_ptr<Process> Process::Launch(const std::string& executable, const std::vector<std::string>& arguments,
                                             const std::string& workingDirectory)
    {
        // Everything the child needs is prepared before the fork (no allocation between fork and exec)
        std::vector<std::string> storage;
        storage.push_back(executable);
        storage.insert(storage.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (std::string& s : storage)
            argv.push_back(s.data());
        argv.push_back(nullptr);

        // exec failure is reported through a close-on-exec pipe
        int errorPipe[2];
        if (pipe(errorPipe) != 0)
            return nullptr;
        fcntl(errorPipe[1], F_SETFD, FD_CLOEXEC);

        const pid_t pid = fork();
        if (pid < 0)
        {
            close(errorPipe[0]);
            close(errorPipe[1]);
            return nullptr;
        }

        if (pid == 0)
        {
            close(errorPipe[0]);
            if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
                _exit(127);
            execvp(argv[0], argv.data());
            const int error = errno;
            ssize_t ignored = write(errorPipe[1], &error, sizeof(error));
            (void)ignored;
            _exit(127);
        }

        close(errorPipe[1]);
        int childError = 0;
        const ssize_t read_ = read(errorPipe[0], &childError, sizeof(childError));
        close(errorPipe[0]);
        if (read_ > 0)
        {
            waitpid(pid, nullptr, 0);
            return nullptr;
        }

        std::unique_ptr<Process> process(new Process());
        process->m_Impl = std::make_unique<Impl>();
        process->m_Impl->pid = pid;
        return process;
    }

    Process::~Process()
    {
        // A child left running becomes a zombie once it exits if nobody waits for it : reap it if it is already done
        if (m_Impl)
            m_Impl->Poll(false);
    }

    uint32_t Process::Id() const
    {
        return static_cast<uint32_t>(m_Impl->pid);
    }

    bool Process::IsRunning()
    {
        return !m_Impl->Poll(false);
    }

    std::optional<int> Process::Wait(int timeoutMs)
    {
        if (timeoutMs < 0)
        {
            m_Impl->Poll(true);
            return m_Impl->exitCode;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!m_Impl->Poll(false))
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return std::nullopt;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return m_Impl->exitCode;
    }

    void Process::Kill()
    {
        if (!m_Impl->exited)
            kill(m_Impl->pid, SIGKILL);
    }

    void RevealInFileManager(const std::string& path, bool select)
    {
        // No portable "select this file" : open the folder that contains it
        const std::filesystem::path p(path);
        const std::string directory = (select ? p.parent_path() : p).string();
        OpenWithDefaultApplication(directory);
    }

    bool OpenWithDefaultApplication(const std::string& pathOrUrl)
    {
    #if defined(__APPLE__)
        const char* opener = "open";
    #else
        const char* opener = "xdg-open";
    #endif
        // Detached : opener runs in the background, its output is not wanted
        const pid_t pid = fork();
        if (pid < 0)
            return false;
        if (pid == 0)
        {
            if (!freopen("/dev/null", "w", stdout) || !freopen("/dev/null", "w", stderr))
                _exit(127);
            execlp(opener, opener, pathOrUrl.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        // the opener returns right after handing the file over to the desktop
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
}

#endif
