#pragma once

#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    struct IOResult {
        bool ok = false;
        std::string error;
        std::vector<uint8_t> data;      // ReadFile : the content
    };

    /// @brief File IO off the calling thread. The backend is the OS one when there is one (IOCP on Windows,
    /// io_uring on Linux), a small pool of IO threads doing blocking reads and writes otherwise.
    namespace AsyncIO {
        /// @brief Reads the whole file.
        std::future<IOResult> ReadFile(const std::string& path);

        /// @brief Writes (creates or replaces) the file. The data is moved in : it must not change while the write is pending.
        std::future<IOResult> WriteFile(const std::string& path, std::vector<uint8_t> data);

        /// @brief Name of the backend in use : "IOCP", "io_uring" or "thread pool".
        const char* BackendName();

        /// @brief Waits for the pending requests and stops the backend. The next request starts it again.
        void Shutdown();

        /// @brief Forces the generic backend (or goes back to the OS one) for the following requests : for tests and
        /// to rule the OS backend out when chasing a bug.
        void UseThreadPoolBackend();
        void UseNativeBackend();
    }
}
