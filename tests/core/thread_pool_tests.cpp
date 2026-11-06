#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "engine/core/jobs/thread_pool.hpp"

using namespace Shard::Engine::Core;

namespace {
    ThreadPoolDesc Desc(unsigned threads) {
        ThreadPoolDesc d;
        d.name = "Test";
        d.threadCount = threads;
        return d;
    }
}

TEST(ThreadPool, RunsEveryPostedTask) {
    ThreadPool pool(Desc(4));
    std::atomic<int> count{0};
    for (int i = 0; i < 1000; ++i) pool.Post([&] { count.fetch_add(1); });
    pool.WaitIdle();
    EXPECT_EQ(count.load(), 1000);
}

TEST(ThreadPool, SingleThreadPoolIsSerial) {
    ThreadPool pool(Desc(1));
    std::vector<int> order;
    std::set<std::thread::id> threads;
    for (int i = 0; i < 200; ++i)
        pool.Post([&, i] { order.push_back(i); threads.insert(std::this_thread::get_id()); });
    pool.WaitIdle();
    ASSERT_EQ(order.size(), 200u);
    for (int i = 0; i < 200; ++i) EXPECT_EQ(order[i], i);
    EXPECT_EQ(threads.size(), 1u);
    EXPECT_NE(*threads.begin(), std::this_thread::get_id());
}

TEST(ThreadPool, SubmitReturnsTheResult) {
    ThreadPool pool(Desc(2));
    auto f = pool.Submit([] { return 6 * 7; });
    EXPECT_EQ(f.get(), 42);
}

TEST(ThreadPool, SubmitForwardsExceptions) {
    ThreadPool pool(Desc(1));
    auto f = pool.Submit([]() -> int { throw std::runtime_error("boom"); });
    EXPECT_THROW(f.get(), std::runtime_error);
    EXPECT_EQ(pool.Submit([] { return 1; }).get(), 1);
}

TEST(ThreadPool, BlockingTasksDontStallOthers) {
    ThreadPool pool(Desc(4));
    std::atomic<int> arrived{0};
    std::vector<std::future<void>> fs;
    for (int i = 0; i < 4; ++i)
        fs.push_back(pool.Submit([&] {
            arrived.fetch_add(1);
            while (arrived.load() < 4) std::this_thread::yield();
        }));
    for (auto& f : fs) f.get();
    EXPECT_EQ(arrived.load(), 4);
}

TEST(ThreadPool, DestructorRunsTheQueuedTasks) {
    std::atomic<int> count{0};
    {
        ThreadPool pool(Desc(1));
        for (int i = 0; i < 100; ++i)
            pool.Post([&] { std::this_thread::sleep_for(std::chrono::microseconds(50)); count.fetch_add(1); });
    }
    EXPECT_EQ(count.load(), 100);
}

TEST(ThreadPool, TasksMayPostMoreTasks) {
    ThreadPool pool(Desc(2));
    std::atomic<int> count{0};
    for (int i = 0; i < 10; ++i)
        pool.Post([&] {
            for (int j = 0; j < 10; ++j) pool.Post([&] { count.fetch_add(1); });
        });
    // A parent is still running while it posts children, so WaitIdle only sees idle once the children are queued too.
    pool.WaitIdle();
    EXPECT_EQ(count.load(), 100);
}

TEST(ThreadPool, IsCurrentThreadInPoolOnly) {
    ThreadPool pool(Desc(2));
    ThreadPool other(Desc(1));
    EXPECT_FALSE(pool.IsCurrentThread());
    EXPECT_TRUE(pool.Submit([&] { return pool.IsCurrentThread(); }).get());
    EXPECT_FALSE(pool.Submit([&] { return other.IsCurrentThread(); }).get());
}

TEST(ThreadPool, OnThreadStartRunsPerThread) {
    std::mutex m;
    std::set<unsigned> indices;
    {
        ThreadPoolDesc d = Desc(3);
        d.onThreadStart = [&](unsigned i) { std::lock_guard<std::mutex> l(m); indices.insert(i); };
        ThreadPool pool(d);
        pool.WaitIdle();
    }
    EXPECT_EQ(indices, (std::set<unsigned>{0, 1, 2}));
}

TEST(ThreadPools, CreatesPoolsLazily) {
    std::mutex m;
    std::vector<std::string> started;
    ThreadPools pools([&](PoolKind, const std::string& name, unsigned) {
        std::lock_guard<std::mutex> l(m);
        started.push_back(name);
    });
    {
        std::lock_guard<std::mutex> l(m);
        EXPECT_TRUE(started.empty());
    }
    ThreadPool& io = pools.Get(PoolKind::IO);
    ThreadPool& audio = pools.Get(PoolKind::Audio);
    EXPECT_EQ(&io, &pools.Get(PoolKind::IO));
    EXPECT_EQ(io.ThreadCount(), ThreadPools::DefaultThreadCount(PoolKind::IO));
    EXPECT_EQ(audio.ThreadCount(), 1u);
    // WaitIdle only means "no task", so give the threads time to run their start callback.
    for (int i = 0; i < 2000; ++i) {
        { std::lock_guard<std::mutex> l(m); if (started.size() >= 5) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::lock_guard<std::mutex> l(m);
    EXPECT_EQ(std::count(started.begin(), started.end(), "IO"), 4);
    EXPECT_EQ(std::count(started.begin(), started.end(), "Audio"), 1);
    EXPECT_EQ(std::count(started.begin(), started.end(), "Render"), 0);
}
