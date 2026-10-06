#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    // --- Environment ---

    std::optional<std::string> GetEnv(const std::string& name);
    bool SetEnv(const std::string& name, const std::string& value);

    // --- Current process ---

    uint32_t CurrentProcessId();

    /// @brief Absolute path of the running executable (UTF-8, forward slashes).
    std::string ExecutablePath();
    std::string ExecutableDirectory();

    std::string WorkingDirectory();
    bool SetWorkingDirectory(const std::string& path);

    // --- Other processes ---

    /// @brief A process started by this one (the game from the editor, a cooker, ...).
    class Process {
    public:
        /// @brief Starts `executable` with `arguments` (no shell involved : the arguments are passed as they are).
        /// nullptr if the process could not be started.
        static std::unique_ptr<Process> Launch(const std::string& executable,
                                               const std::vector<std::string>& arguments = {},
                                               const std::string& workingDirectory = "");

        ~Process();
        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

        uint32_t Id() const;
        bool IsRunning();
        /// @brief Waits for the process to exit and returns its exit code. timeoutMs < 0 waits forever; nullopt on timeout.
        std::optional<int> Wait(int timeoutMs = -1);
        void Kill();

    private:
        Process() = default;
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // --- Desktop integration ---

    /// @brief Shows `path` in the file manager (Explorer, xdg-open) : selects the item when `select` is true on
    /// the platforms that can, opens the folder otherwise.
    void RevealInFileManager(const std::string& path, bool select);

    /// @brief Opens a file or URL with the application the OS associates with it.
    bool OpenWithDefaultApplication(const std::string& pathOrUrl);
}
