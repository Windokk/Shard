#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace Shard::Engine::Core::Platform {

    /// @brief Number of hardware threads (always >= 1). The one place the engine asks, so that every system sizes its
    /// worker budget from the same value.
    unsigned HardwareConcurrency();

    /// @brief HardwareConcurrency() - 1 (the calling thread works too), at least 1.
    unsigned WorkerThreadCount();

    enum class ThreadPriority { Lowest, Low, Normal, High, Highest };

    /// @brief Identifier of the calling thread, the one a debugger shows.
    uint64_t CurrentThreadId();

    /// @brief Name shown by debuggers and profilers for the calling thread.
    void SetCurrentThreadName(const std::string& name);
    bool SetCurrentThreadPriority(ThreadPriority priority);
    /// @brief Restricts the calling thread to the CPUs set in the mask (bit i = logical CPU i, first 64 CPUs).
    bool SetCurrentThreadAffinity(uint64_t mask);

    struct ThreadDesc {
        std::string name;
        ThreadPriority priority = ThreadPriority::Normal;
        uint64_t affinityMask = 0;      // 0 = no restriction
    };

    /// @brief A thread that is named / prioritised / pinned from the inside, before it runs the user function.
    /// Joins in the destructor.
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
