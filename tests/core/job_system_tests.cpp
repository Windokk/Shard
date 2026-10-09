#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <vector>

#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/parallel_for.hpp"
#include "engine/core/jobs/task_graph.hpp"
#include "engine/core/jobs/work_stealing_deque.hpp"

using namespace Shard::Engine::Core;

namespace {
    JobSystemDesc Workers(unsigned n, size_t queue = 4096) {
        JobSystemDesc d;
        d.workerCount = n;
        d.queueCapacity = queue;
        return d;
    }
}

TEST(WorkStealingDeque, OwnerIsLifoThievesAreFifo) {
    WorkStealingDeque<int*> dq(8);
    int v[4] = {0, 1, 2, 3};
    for (int& x : v) ASSERT_TRUE(dq.Push(&x));
    EXPECT_EQ(dq.Steal(), &v[0]);   // oldest
    EXPECT_EQ(dq.Pop(), &v[3]);     // newest
    EXPECT_EQ(dq.Pop(), &v[2]);
    EXPECT_EQ(dq.Steal(), &v[1]);
    EXPECT_EQ(dq.Pop(), nullptr);
    EXPECT_EQ(dq.Steal(), nullptr);
}

TEST(WorkStealingDeque, PushFailsWhenFull) {
    WorkStealingDeque<int*> dq(4);
    int v[5];
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(dq.Push(&v[i]));
    EXPECT_FALSE(dq.Push(&v[4]));
    EXPECT_NE(dq.Pop(), nullptr);
    EXPECT_TRUE(dq.Push(&v[4]));
}

TEST(WorkStealingDeque, EveryItemTakenExactlyOnceUnderContention) {
    constexpr int kItems = 200000;
    WorkStealingDeque<int*> dq(1024);
    std::vector<int> items(kItems);
    std::iota(items.begin(), items.end(), 0);
    std::vector<std::atomic<int>> taken(kItems);
    std::atomic<int> total{0};
    std::atomic<bool> done{false};

    std::vector<std::thread> thieves;
    for (int t = 0; t < 3; ++t)
        thieves.emplace_back([&] {
            while (!done.load() || total.load() < kItems) {
                if (int* p = dq.Steal()) { taken[*p].fetch_add(1); total.fetch_add(1); }
                if (done.load() && total.load() >= kItems) break;
            }
        });

    for (int i = 0; i < kItems; ++i) {            // owner: push, and pop some back itself
        while (!dq.Push(&items[i])) {
            if (int* p = dq.Pop()) { taken[*p].fetch_add(1); total.fetch_add(1); }
        }
        if (i % 3 == 0)
            if (int* p = dq.Pop()) { taken[*p].fetch_add(1); total.fetch_add(1); }
    }
    while (int* p = dq.Pop()) { taken[*p].fetch_add(1); total.fetch_add(1); }
    done = true;
    for (auto& t : thieves) t.join();

    EXPECT_EQ(total.load(), kItems);
    for (int i = 0; i < kItems; ++i) ASSERT_EQ(taken[i].load(), 1) << "item " << i;
}

TEST(JobSystem, RunsAllSubmittedJobs) {
    JobSystem js(Workers(3));
    std::atomic<int> counter{0};
    JobGroup g;
    for (int i = 0; i < 10000; ++i) js.Submit(g, [&] { counter.fetch_add(1); });
    js.Wait(g);
    EXPECT_EQ(counter.load(), 10000);
    EXPECT_TRUE(g.IsDone());
}

TEST(JobSystem, WorksWithZeroWorkers) {
    JobSystem js(Workers(0));
    int sum = 0;                                  // single thread: no atomics needed
    JobGroup g;
    for (int i = 1; i <= 100; ++i) js.Submit(g, [&sum, i] { sum += i; });
    js.Wait(g);
    EXPECT_EQ(sum, 5050);
}

TEST(JobSystem, NestedSubmitAndWaitDoNotDeadlock) {
    JobSystem js(Workers(2));
    std::atomic<int> leaves{0};
    JobGroup outer;
    for (int i = 0; i < 50; ++i)
        js.Submit(outer, [&] {
            JobGroup inner;                       // a job waiting on its own children, on a worker thread
            for (int j = 0; j < 50; ++j) js.Submit(inner, [&] { leaves.fetch_add(1); });
            js.Wait(inner);
        });
    js.Wait(outer);
    EXPECT_EQ(leaves.load(), 2500);
}

TEST(JobSystem, JobsMaySubmitIntoTheirOwnGroup) {
    JobSystem js(Workers(3));
    std::atomic<int> count{0};
    JobGroup g;
    std::function<void(int)> spawn = [&](int depth) {
        count.fetch_add(1);
        if (depth == 0) return;
        js.Submit(g, [&, depth] { spawn(depth - 1); });
        js.Submit(g, [&, depth] { spawn(depth - 1); });
    };
    js.Submit(g, [&] { spawn(10); });
    js.Wait(g);
    EXPECT_EQ(count.load(), (1 << 11) - 1);
}

TEST(JobSystem, SmallRingFallsBackToInlineExecution) {
    JobSystem js(Workers(1, 8));                  // 8 slots, 5000 jobs: the back-pressure path must keep it correct
    std::atomic<int> counter{0};
    JobGroup g;
    for (int i = 0; i < 5000; ++i) js.Submit(g, [&] { counter.fetch_add(1); });
    js.Wait(g);
    EXPECT_EQ(counter.load(), 5000);
}

TEST(JobSystem, LargeCapturesAreBoxed) {
    JobSystem js(Workers(2));
    std::atomic<long> sum{0};
    JobGroup g;
    for (int i = 0; i < 100; ++i) {
        std::array<long, 64> big{};               // 512 bytes: does not fit the inline storage
        big.fill(i);
        js.Submit(g, [big, &sum] { sum.fetch_add(std::accumulate(big.begin(), big.end(), 0L)); });
    }
    js.Wait(g);
    EXPECT_EQ(sum.load(), 64L * (99 * 100 / 2));
}

TEST(JobSystem, ForeignThreadsCanSubmit) {
    JobSystem js(Workers(2));
    std::atomic<int> counter{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            JobGroup g;
            for (int i = 0; i < 500; ++i) js.Submit(g, [&] { counter.fetch_add(1); });
            js.Wait(g);
        });
    for (auto& t : threads) t.join();
    EXPECT_EQ(counter.load(), 2000);
}

TEST(JobSystem, WorkIsSpreadAcrossThreads) {
    JobSystem js(Workers(3));
    std::mutex m;
    std::set<std::thread::id> ids;
    JobGroup g;
    for (int i = 0; i < 64; ++i)
        js.Submit(g, [&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            std::lock_guard<std::mutex> l(m);
            ids.insert(std::this_thread::get_id());
        });
    js.Wait(g);
    EXPECT_GT(ids.size(), 1u);
}

TEST(JobSystem, OnWorkerStartCalledForEveryBackgroundThread) {
    std::atomic<int> started{0};
    std::mutex m;
    std::set<unsigned> indices;
    {
        JobSystemDesc d = Workers(3);
        d.onWorkerStart = [&](unsigned i) { started.fetch_add(1); std::lock_guard<std::mutex> l(m); indices.insert(i); };
        JobSystem js(d);
        js.Run([] {});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_EQ(started.load(), 3);
    EXPECT_EQ(indices, (std::set<unsigned>{1, 2, 3}));
}

TEST(JobSystem, IdleWorkersWakeUpForLateWork) {
    JobSystem js(Workers(2));
    for (int round = 0; round < 20; ++round) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));   // let them go to sleep
        std::atomic<int> c{0};
        JobGroup g;
        for (int i = 0; i < 8; ++i) js.Submit(g, [&] { c.fetch_add(1); });
        js.Wait(g);
        ASSERT_EQ(c.load(), 8);
    }
}

TEST(ParallelFor, VisitsEveryIndexExactlyOnce) {
    JobSystem js(Workers(3));
    for (size_t n : {0u, 1u, 2u, 7u, 100u, 12345u}) {
        for (size_t grain : {0u, 1u, 3u, 64u, 100000u}) {
            std::vector<std::atomic<int>> hits(n);
            ParallelFor(js, 0, n, grain, [&](size_t i) { hits[i].fetch_add(1); });
            for (size_t i = 0; i < n; ++i) ASSERT_EQ(hits[i].load(), 1) << "n=" << n << " grain=" << grain << " i=" << i;
        }
    }
}

TEST(ParallelFor, RespectsBeginOffset) {
    JobSystem js(Workers(2));
    std::vector<int> data(1000, 0);
    ParallelFor(js, 100, 900, 16, [&](size_t i) { data[i] = 1; });
    for (size_t i = 0; i < data.size(); ++i) EXPECT_EQ(data[i], (i >= 100 && i < 900) ? 1 : 0);
}

TEST(ParallelFor, RangeChunksAreDisjointAndBounded) {
    JobSystem js(Workers(3));
    std::mutex m;
    std::vector<std::pair<size_t, size_t>> chunks;
    ParallelForRange(js, 0, 1000, 37, [&](size_t b, size_t e) {
        std::lock_guard<std::mutex> l(m);
        chunks.emplace_back(b, e);
    });
    std::sort(chunks.begin(), chunks.end());
    size_t expect = 0;
    for (auto [b, e] : chunks) {
        EXPECT_EQ(b, expect);
        EXPECT_LE(e - b, 37u);
        EXPECT_GT(e, b);
        expect = e;
    }
    EXPECT_EQ(expect, 1000u);
}

TEST(ParallelFor, ComputesSameSumAsSerial) {
    JobSystem js(Workers(3));
    std::vector<uint64_t> v(100000);
    ParallelFor(js, 0, v.size(), [&](size_t i) { v[i] = i * i; });
    uint64_t expected = 0;
    for (size_t i = 0; i < v.size(); ++i) expected += i * i;
    EXPECT_EQ(std::accumulate(v.begin(), v.end(), uint64_t{0}), expected);
}

TEST(ParallelFor, CanBeNested) {
    JobSystem js(Workers(2));
    std::atomic<int> count{0};
    ParallelFor(js, 0, 20, 1, [&](size_t) {
        ParallelFor(js, 0, 20, 1, [&](size_t) { count.fetch_add(1); });
    });
    EXPECT_EQ(count.load(), 400);
}

TEST(TaskGraph, RespectsDependencies) {
    JobSystem js(Workers(3));
    for (int rep = 0; rep < 200; ++rep) {
        std::mutex m;
        std::vector<char> order;
        auto rec = [&](char c) { return [&, c] { std::lock_guard<std::mutex> l(m); order.push_back(c); }; };

        //   A -> B -> D
        //   A -> C -> D        (B and C are free to run in parallel)
        TaskGraph g;
        auto a = g.Add("A", rec('A')), b = g.Add("B", rec('B')), c = g.Add("C", rec('C')), d = g.Add("D", rec('D'));
        g.DependsOn(b, a); g.DependsOn(c, a); g.DependsOn(d, b); g.DependsOn(d, c);
        ASSERT_TRUE(g.Run(js));

        ASSERT_EQ(order.size(), 4u);
        EXPECT_EQ(order.front(), 'A');
        EXPECT_EQ(order.back(), 'D');
    }
}

TEST(TaskGraph, RunsEveryTaskOnceAndIsReusable) {
    JobSystem js(Workers(3));
    constexpr int kTasks = 200;
    std::vector<std::atomic<int>> runs(kTasks);
    TaskGraph g;
    std::vector<TaskGraph::TaskId> ids;
    for (int i = 0; i < kTasks; ++i) ids.push_back(g.Add("t" + std::to_string(i), [&runs, i] { runs[i].fetch_add(1); }));
    for (int i = 1; i < kTasks; ++i) g.DependsOn(ids[i], ids[(i - 1) / 2]);     // a binary tree
    for (int frame = 0; frame < 5; ++frame) ASSERT_TRUE(g.Run(js));
    for (int i = 0; i < kTasks; ++i) EXPECT_EQ(runs[i].load(), 5);
}

TEST(TaskGraph, PrerequisiteWritesAreVisibleToDependents) {
    JobSystem js(Workers(3));
    for (int rep = 0; rep < 200; ++rep) {
        int x = 0, y = 0, z = 0;                  // plain ints: a race here would be flagged by TSan / wrong values
        TaskGraph g;
        auto a = g.Add("a", [&] { x = 1; });
        auto b = g.Add("b", [&] { y = x + 1; });
        auto c = g.Add("c", [&] { z = y + 1; });
        g.DependsOn(b, a); g.DependsOn(c, b);
        g.Run(js);
        ASSERT_EQ(z, 3);
    }
}

TEST(TaskGraph, DetectsCyclesAndRunsNothing) {
    JobSystem js(Workers(1));
    int ran = 0;
    TaskGraph g;
    auto a = g.Add("a", [&] { ++ran; }), b = g.Add("b", [&] { ++ran; }), c = g.Add("c", [&] { ++ran; });
    g.DependsOn(b, a); g.DependsOn(c, b); g.DependsOn(a, c);
    std::string err;
    EXPECT_FALSE(g.Validate(&err));
    EXPECT_NE(err.find("'a'"), std::string::npos);
    EXPECT_FALSE(g.Run(js));
    EXPECT_EQ(ran, 0);
}

TEST(TaskGraph, RejectsBadEdgesAndIgnoresDuplicates) {
    TaskGraph g;
    auto a = g.Add("a", [] {}), b = g.Add("b", [] {});
    EXPECT_FALSE(g.DependsOn(a, a));
    EXPECT_FALSE(g.DependsOn(a, 99));
    EXPECT_TRUE(g.DependsOn(b, a));
    EXPECT_TRUE(g.DependsOn(b, a));               // duplicate must not count twice, or b would never become ready
    JobSystem js(Workers(1));
    EXPECT_TRUE(g.Run(js));
}

TEST(TaskGraph, EmptyGraphRuns) {
    JobSystem js(Workers(1));
    TaskGraph g;
    EXPECT_TRUE(g.Run(js));
}
