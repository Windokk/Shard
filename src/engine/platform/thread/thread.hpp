#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace Shard::Engine::Core::Platform {

    unsigned HardwareConcurrency();

    unsigned WorkerThreadCount();

    enum class ThreadPriority { Lowest, Low, Normal, High, Highest };

    uint64_t CurrentThreadId();

    void SetCurrentThreadName(const std::string& name);
    bool SetCurrentThreadPriority(ThreadPriority priority);
    bool SetCurrentThreadAffinity(uint64_t mask);

    struct ThreadDesc {
        std::string name;
        ThreadPriority priority = ThreadPriority::Normal;
        uint64_t affinityMask = 0;
    };

    class Thread {
    public:
        Thread() = default;
        Thread(const ThreadDesc& desc, std::function<void()> function);
        Thread(Thread&&) noexcept = default;
        Thread& operator=(Thread&& other) noexcept;
        ~Thread() { Join(); }

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        bool Joinable() const { return m_Thread.joinable(); }
        void Join() { if (m_Thread.joinable()) m_Thread.join(); }

    private:
        std::thread m_Thread;
    };
}
