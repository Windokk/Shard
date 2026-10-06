#pragma once

#include "engine/platform/filesystem/async_io.hpp"

#include <memory>

namespace Shard::Engine::Core::Platform::AsyncIO {

    /// @brief What does the actual IO. Each request completes its promise exactly once, from any thread.
    class Backend {
    public:
        virtual ~Backend() = default;

        virtual const char* Name() const = 0;
        virtual void Read(std::string path, std::promise<IOResult> promise) = 0;
        virtual void Write(std::string path, std::vector<uint8_t> data, std::promise<IOResult> promise) = 0;
        /// @brief Blocks until every request already submitted has completed.
        virtual void Drain() = 0;
    };

    /// @brief The OS backend : IOCP on Windows, io_uring on Linux (when built with liburing and supported by the kernel).
    /// nullptr when there is none, the generic thread pool backend is used then.
    std::unique_ptr<Backend> CreateNativeBackend();
}
