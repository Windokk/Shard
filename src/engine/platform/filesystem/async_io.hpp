#pragma once

#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace Shard::Engine::Core::Platform {

    struct IOResult {
        bool ok = false;
        std::string error;
        std::vector<uint8_t> data;
    };

    namespace AsyncIO {
        std::future<IOResult> ReadFile(const std::string& path);

        std::future<IOResult> WriteFile(const std::string& path, std::vector<uint8_t> data);

        const char* BackendName();

        void Shutdown();

        void UseThreadPoolBackend();
        void UseNativeBackend();
    }
}
