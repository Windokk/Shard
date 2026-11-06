#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Shard::Engine::Core::Platform {

    enum class IpcStatus { Ok, TimedOut, Closed, Error };

    class NamedPipe {
    public:
        static std::unique_ptr<NamedPipe> CreateServer(const std::string& name);

        static std::unique_ptr<NamedPipe> Connect(const std::string& name, int timeoutMs = 0);

        ~NamedPipe();
        NamedPipe(const NamedPipe&) = delete;
        NamedPipe& operator=(const NamedPipe&) = delete;

        bool WaitForClient(int timeoutMs = -1);

        bool IsConnected() const;
        void Disconnect();

        IpcStatus Read(void* buffer, size_t size, size_t* read, int timeoutMs = -1);

        IpcStatus Write(const void* data, size_t size, int timeoutMs = -1);

    private:
        NamedPipe() = default;
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class SharedMemory {
    public:
        static std::unique_ptr<SharedMemory> Create(const std::string& name, size_t size);
        static std::unique_ptr<SharedMemory> Open(const std::string& name);

        ~SharedMemory();
        SharedMemory(const SharedMemory&) = delete;
        SharedMemory& operator=(const SharedMemory&) = delete;

        void* Data() const;
        size_t Size() const;

    private:
        SharedMemory() = default;
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
