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
        unsigned threadCount = 1;
        std::function<void(unsigned index)> onThreadStart;
    };

    // For work that blocks (disk, devices) or must stay on one thread in order (render, audio). Destruction runs queued tasks, then joins.
    class ThreadPool {
    public:
        explicit ThreadPool(ThreadPoolDesc desc = {});
        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        template <typename F>
        void Post(F&& f) { Enqueue(std::function<void()>(std::forward<F>(f))); }

        // The future does NOT block on destruction (unlike std::async), so whatever f references must outlive it
        template <typename F>
        auto Submit(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>> {
            using R = std::invoke_result_t<std::decay_t<F>>;
            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
            std::future<R> result = task->get_future();
            Enqueue([task] { (*task)(); });
            return result;
        }

        // Do not call from the pool's own tasks, it would wait for itself
        void WaitIdle();

        bool IsCurrentThread() const;

        const std::string& Name() const { return m_Desc.name; }
        unsigned ThreadCount() const { return static_cast<unsigned>(m_Threads.size()); }

    private:
        void Enqueue(std::function<void()> task);
        void ThreadMain(unsigned index);

        ThreadPoolDesc m_Desc;
        std::vector<std::thread> m_Threads;

        std::mutex m_Mutex;
        std::condition_variable m_Wake;
        std::condition_variable m_Idle;
        std::deque<std::function<void()>> m_Queue;
        unsigned m_Running = 0;
        bool m_Stopping = false;
    };

    enum class PoolKind : uint8_t {
        IO,
        Render,
        Audio
    };

    class ThreadPools {
    public:
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
