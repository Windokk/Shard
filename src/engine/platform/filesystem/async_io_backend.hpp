#pragma once

#include "engine/platform/filesystem/async_io.hpp"

#include <memory>

namespace Shard::Engine::Core::Platform::AsyncIO {

    class Backend {
    public:
        virtual ~Backend() = default;

        virtual const char* Name() const = 0;
        virtual void Read(std::string path, std::promise<IOResult> promise) = 0;
        virtual void Write(std::string path, std::vector<uint8_t> data, std::promise<IOResult> promise) = 0;
        virtual void Drain() = 0;
    };

    std::unique_ptr<Backend> CreateNativeBackend();
}
