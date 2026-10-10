#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Shard::Engine::Core {

    struct ThreadPoolDesc {
        std::string name = "Pool";
        /// Dedicated threads. 1 makes the pool a SERIAL executor : tasks run one at a time, in the order they were
        /// posted, always on the same thread (what a render or audio thread needs).
        unsigned threadCount = 1;
        /// Called first thing on every thread of the pool, with its index (0..threadCount-1). The core layer cannot
        /// depend on the platform layer, so the engine plugs thread naming / priority / affinity in here.
        std::function<void(unsigned index)> onThreadStart;
    };

    /// @brief A fixed set of dedicated threads fed by one FIFO queue.
    ///
    /// The counterpart of the JobSystem for work that must NOT run on its workers :
    ///  * work that BLOCKS (disk reads, waiting on a device) : a job that sleeps on a worker takes a core away from
    ///    the work-stealing pool, here it only costs a thread that was going to wait anyway;
    ///  * work that must stay on ONE thread, in order (a render thread, an audio thread) : threadCount == 1.
    ///
    /// Destruction runs every task still queued, then joins.
    class ThreadPool {
    public:
        explicit ThreadPool(ThreadPoolDesc desc = {});
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        /// @brief Queues f() and returns at once. f must not throw (an exception would end the program, there is
        /// nobody to hand it to : use Submit if f can throw).
        template <typename F>
        void Post(F&& f) { Enqueue(std::function<void()>(std::forward<F>(f))); }

        /// @brief Queues f() and returns a future of its result. An exception thrown by f is rethrown by get().
        /// Unlike std::async's, the future does NOT block when it is destroyed : whatever f references must outlive it.
        template <typename F>
        auto Submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>> {
            using R = std::invoke_result_t<std::decay_t<F>>;
            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));   // std::function needs a copyable callable
            std::future<R> result = task->get_future();
            Enqueue([task] { (*task)(); });
            return result;
        }

        /// @brief Blocks until the queue is empty and no task is running. Do not call it from one of the pool's own
        /// tasks (it would wait for itself).
        void WaitIdle();

        /// @brief True when the calling thread is one of this pool's threads.
        bool IsCurrentThread() const;

        const std::string& Name() const { return m_Desc.name; }
        unsigned ThreadCount() const { return static_cast<unsigned>(m_Threads.size()); }

    private:
        void Enqueue(std::function<void()> task);
        void ThreadMain(unsigned index);

        ThreadPoolDesc m_Desc;
        std::vector<std::thread> m_Threads;

        std::mutex m_Mutex;
        std::condition_variable m_Wake;       // a task was queued, or the pool is stopping
        std::condition_variable m_Idle;       // the pool became idle
        std::deque<std::function<void()>> m_Queue;
        unsigned m_Running = 0;               // tasks being executed right now
        bool m_Stopping = false;
    };

    /// The engine's dedicated pools (the work-stealing one is the JobSystem).
    enum class PoolKind : uint8_t {
        IO,         ///< blocking work : file reads, decoding, anything that waits
        Render,     ///< one thread, in order : work that must happen on the render side
        Audio       ///< one thread, in order : audio work
    };

    /// @brief Owns the ThreadPool of each PoolKind. A pool is created the first time it is asked for, so the ones
    /// nothing uses cost no thread.
    class ThreadPools {
    public:
        /// @param onThreadStart called on every thread of every pool : (kind, name of the pool, index in the pool).
        using ThreadStartFn = std::function<void(PoolKind, const std::string&, unsigned)>;

        explicit ThreadPools(ThreadStartFn onThreadStart = {});
        ~ThreadPools();

        ThreadPools(const ThreadPools&) = delete;
        ThreadPools& operator=(const ThreadPools&) = delete;

        ThreadPool& Get(PoolKind kind);

        static const char* Name(PoolKind kind);
        static unsigned DefaultThreadCount(PoolKind kind);

    private:
        static constexpr size_t kKinds = 3;

        ThreadStartFn m_OnThreadStart;
        std::mutex m_Mutex;
        std::unique_ptr<ThreadPool> m_Pools[kKinds];
    };
}
