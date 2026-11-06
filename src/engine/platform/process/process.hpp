#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    std::optional<std::string> GetEnv(const std::string& name);
    bool SetEnv(const std::string& name, const std::string& value);

    uint32_t CurrentProcessId();

    std::string ExecutablePath();
    std::string ExecutableDirectory();

    std::string WorkingDirectory();
    bool SetWorkingDirectory(const std::string& path);

    class Process {
    public:
        static std::unique_ptr<Process> Launch(const std::string& executable,
                                               const std::vector<std::string>& arguments = {},
                                               const std::string& workingDirectory = "");

        ~Process();
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

        uint32_t Id() const;
        bool IsRunning();
        std::optional<int> Wait(int timeoutMs = -1);
        void Kill();

    private:
        Process() = default;
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    void RevealInFileManager(const std::string& path, bool select);

    bool OpenWithDefaultApplication(const std::string& pathOrUrl);
}
