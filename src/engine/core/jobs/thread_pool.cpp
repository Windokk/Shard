#include "engine/core/jobs/thread_pool.hpp"

#include <algorithm>

namespace Shard::Engine::Core {

    namespace {
        thread_local const ThreadPool* tl_pool = nullptr;      // the pool the calling thread belongs to
    }

    ThreadPool::ThreadPool(ThreadPoolDesc desc) : m_Desc(std::move(desc)) {
        const unsigned count = std::max(1u, m_Desc.threadCount);
        m_Threads.reserve(count);
        for (unsigned i = 0; i < count; ++i)
            m_Threads.emplace_back([this, i] { ThreadMain(i); });
    }

    ThreadPool::~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stopping = true;
        }
        m_Wake.notify_all();
        for (std::thread& t : m_Threads) t.join();             // the threads leave only once the queue is empty
    }

    void ThreadPool::Enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (!m_Stopping) {
                m_Queue.push_back(std::move(task));
                m_Wake.notify_one();
                return;
            }
        }
        task();                                                // being destroyed : do not lose the task, run it here
    }

    void ThreadPool::WaitIdle() {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Idle.wait(lock, [this] { return m_Queue.empty() && m_Running == 0; });
    }

    bool ThreadPool::IsCurrentThread() const { return tl_pool == this; }

    void ThreadPool::ThreadMain(unsigned index) {
        tl_pool = this;
        if (m_Desc.onThreadStart) m_Desc.onThreadStart(index);

        std::unique_lock<std::mutex> lock(m_Mutex);
        for (;;) {
            m_Wake.wait(lock, [this] { return m_Stopping || !m_Queue.empty(); });
            if (m_Queue.empty()) break;                        // stopping and drained

            std::function<void()> task = std::move(m_Queue.front());
            m_Queue.pop_front();
            ++m_Running;
            lock.unlock();
            task();                                            // outside the lock : other threads keep taking tasks
            task = nullptr;                                    // destroy the closure before reporting idle
            lock.lock();
            if (--m_Running == 0 && m_Queue.empty()) m_Idle.notify_all();
        }
        tl_pool = nullptr;
    }

    ThreadPools::ThreadPools(ThreadStartFn onThreadStart) : m_OnThreadStart(std::move(onThreadStart)) {}

    ThreadPools::~ThreadPools() {
        // Reverse order : the audio / render work may hand things to the IO threads, not the other way round
        for (size_t i = kKinds; i-- > 0;) m_Pools[i].reset();
    }

    ThreadPool& ThreadPools::Get(PoolKind kind) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::unique_ptr<ThreadPool>& pool = m_Pools[static_cast<size_t>(kind)];
        if (!pool) {
            ThreadPoolDesc desc;
            desc.name = Name(kind);
            desc.threadCount = DefaultThreadCount(kind);
            if (m_OnThreadStart)
                desc.onThreadStart = [fn = m_OnThreadStart, kind, name = desc.name](unsigned index) { fn(kind, name, index); };
            pool = std::make_unique<ThreadPool>(std::move(desc));
        }
        return *pool;
    }

    const char* ThreadPools::Name(PoolKind kind) {
        switch (kind) {
            case PoolKind::IO:     return "IO";
            case PoolKind::Render: return "Render";
            case PoolKind::Audio:  return "Audio";
        }
        return "Pool";
    }

    unsigned ThreadPools::DefaultThreadCount(PoolKind kind) {
        switch (kind) {
            case PoolKind::IO:     return 4;    // they mostly wait on a disk : more than the cores is fine
            case PoolKind::Render: return 1;    // serial : one thread, tasks in order
            case PoolKind::Audio:  return 1;
        }
        return 1;
    }
}
