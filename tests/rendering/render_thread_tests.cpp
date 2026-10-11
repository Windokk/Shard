#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "engine/renderer/rhi/render_thread.hpp"

using namespace Shard::Engine::Rendering;
using namespace std::chrono_literals;

namespace {
    // The graphics context is stood in for by counters : what matters is who calls release / acquire, and when
    struct FakeContext {
        std::atomic<int> released{0};
        std::atomic<int> acquired{0};
        std::thread::id acquiredOn;
        std::thread::id releasedOn;
    };

    // Starts the thread for one test and always stops it
    struct ScopedRenderThread {
        FakeContext context;
        RenderThread& thread = RenderThread::Get();

        explicit ScopedRenderThread(int framesInFlight = 2)
        {
            thread.SetMaxFramesInFlight(framesInFlight);
            const bool ok = thread.Start(
                [this] { context.released++; context.releasedOn = std::this_thread::get_id(); },
                [this] { context.acquired++; context.acquiredOn = std::this_thread::get_id(); return true; });
            EXPECT_TRUE(ok);
        }

        ~ScopedRenderThread()
        {
            thread.Stop(
                [this] { context.released++; },
                [this] { context.acquired++; return true; });
        }
    };

    bool WaitFor(const std::atomic<bool>& flag, std::chrono::milliseconds timeout)
    {
        const auto end = std::chrono::steady_clock::now() + timeout;
        while (!flag && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(1ms);
        return flag;
    }
}

TEST(RenderThread, RunsEverythingInlineWhileNotStarted) {
    RenderThread& rt = RenderThread::Get();
    ASSERT_FALSE(rt.IsRunning());
    EXPECT_TRUE(rt.IsGraphicsThread());

    const auto me = std::this_thread::get_id();
    std::thread::id postedOn, invokedOn, frameOn;
    rt.Post([&] { postedOn = std::this_thread::get_id(); });
    rt.Invoke([&] { invokedOn = std::this_thread::get_id(); });
    rt.SubmitFrame([&] { frameOn = std::this_thread::get_id(); });
    EXPECT_EQ(postedOn, me);
    EXPECT_EQ(invokedOn, me);
    EXPECT_EQ(frameOn, me);
    rt.WaitForFrameSlot();      // nothing to wait for
    rt.Flush();
}

TEST(RenderThread, StartHandsTheContextToTheNewThread) {
    ScopedRenderThread scoped;
    RenderThread& rt = scoped.thread;

    EXPECT_TRUE(rt.IsRunning());
    EXPECT_EQ(scoped.context.released, 1);
    EXPECT_EQ(scoped.context.acquired, 1);
    EXPECT_EQ(scoped.context.releasedOn, std::this_thread::get_id());
    EXPECT_NE(scoped.context.acquiredOn, std::this_thread::get_id());
    EXPECT_FALSE(rt.IsGraphicsThread());        // the caller is no longer the graphics thread

    bool onGraphics = false;
    rt.Invoke([&] { onGraphics = rt.IsGraphicsThread(); });
    EXPECT_TRUE(onGraphics);
}

TEST(RenderThread, StopHandsTheContextBack) {
    FakeContext context;
    RenderThread& rt = RenderThread::Get();
    ASSERT_TRUE(rt.Start([&] { context.released++; }, [&] { context.acquired++; return true; }));
    rt.Stop([&] { context.released++; context.releasedOn = std::this_thread::get_id(); },
            [&] { context.acquired++; context.acquiredOn = std::this_thread::get_id(); return true; });

    EXPECT_FALSE(rt.IsRunning());
    EXPECT_EQ(context.released, 2);
    EXPECT_EQ(context.acquired, 2);
    EXPECT_NE(context.releasedOn, std::this_thread::get_id());      // given up by the render thread ...
    EXPECT_EQ(context.acquiredOn, std::this_thread::get_id());      // ... taken back by the caller
    EXPECT_TRUE(rt.IsGraphicsThread());
}

TEST(RenderThread, FailedStartKeepsTheContext) {
    RenderThread& rt = RenderThread::Get();
    int acquires = 0;
    const bool ok = rt.Start([] {}, [&] { acquires++; return acquires > 1; });     // the new thread fails, the caller's retry works
    EXPECT_FALSE(ok);
    EXPECT_FALSE(rt.IsRunning());
    EXPECT_EQ(acquires, 2);
    EXPECT_TRUE(rt.IsGraphicsThread());
}

TEST(RenderThread, WorkRunsInSubmissionOrderOnTheRenderThread) {
    ScopedRenderThread scoped;
    RenderThread& rt = scoped.thread;

    std::vector<int> order;
    std::mutex orderMutex;
    std::vector<std::thread::id> threads;
    for (int i = 0; i < 200; i++)
    {
        auto work = [&, i] {
            std::lock_guard<std::mutex> lock(orderMutex);
            order.push_back(i);
            threads.push_back(std::this_thread::get_id());
        };
        if (i % 3 == 0)
            rt.Post(work);
        else if (i % 3 == 1)
            rt.SubmitFrame(work);
        else
            rt.Invoke(work);
    }
    rt.Flush();

    ASSERT_EQ(order.size(), 200u);
    for (int i = 0; i < 200; i++)
        EXPECT_EQ(order[i], i);
    for (const auto& id : threads)
        EXPECT_EQ(id, scoped.context.acquiredOn);
}

TEST(RenderThread, InvokeWaitsAndReturnsAValue) {
    ScopedRenderThread scoped;
    RenderThread& rt = scoped.thread;

    std::atomic<int> state{0};
    const int result = rt.InvokeResult([&] { std::this_thread::sleep_for(20ms); state = 1; return 42; });
    EXPECT_EQ(result, 42);
    EXPECT_EQ(state, 1);        // it had finished when Invoke returned

    EXPECT_EQ(OnGraphicsThread([] { return std::string("hello"); }), "hello");
}

TEST(RenderThread, InvokeRethrowsOnTheCaller) {
    ScopedRenderThread scoped;
    RenderThread& rt = scoped.thread;
    EXPECT_THROW(rt.Invoke([] { throw std::runtime_error("boom"); }), std::runtime_error);

    // The thread is still alive and serving
    int ran = 0;
    rt.Invoke([&] { ran = 1; });
    EXPECT_EQ(ran, 1);
}

TEST(RenderThread, PostedTaskThatThrowsDoesNotKillTheThread) {
    ScopedRenderThread scoped;
    RenderThread& rt = scoped.thread;
    rt.Post([] { throw std::runtime_error("ignored"); });
    int ran = 0;
    rt.Invoke([&] { ran = 1; });
    EXPECT_EQ(ran, 1);
}

TEST(RenderThread, NestedCallsFromTheRenderThreadRunInline) {
    ScopedRenderThread scoped;
    RenderThread& rt = scoped.thread;

    std::vector<int> order;
    rt.Invoke([&] {
        order.push_back(1);
        rt.Invoke([&] { order.push_back(2); });         // would deadlock if it were queued behind itself
        rt.Post([&] { order.push_back(3); });
        order.push_back(4);
    });
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 4}));
}

TEST(RenderThread, FramePacingBoundsTheFramesInFlight) {
    ScopedRenderThread scoped(1);       // one frame in flight : the simulation may only be one frame ahead
    RenderThread& rt = scoped.thread;

    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    rt.SubmitFrame([&] { started = true; while (!release) std::this_thread::sleep_for(1ms); });
    ASSERT_TRUE(WaitFor(started, 2000ms));

    std::atomic<bool> passed{false};
    std::thread simulation([&] { rt.WaitForFrameSlot(); passed = true; });

    std::this_thread::sleep_for(60ms);
    EXPECT_FALSE(passed) << "the simulation ran ahead of the limit";

    release = true;
    simulation.join();
    EXPECT_TRUE(passed);
}

TEST(RenderThread, TwoFramesInFlightLetTheSimulationRunOneFrameAhead) {
    ScopedRenderThread scoped(2);
    RenderThread& rt = scoped.thread;

    std::atomic<bool> release{false};
    std::atomic<bool> started{false};
    rt.SubmitFrame([&] { started = true; while (!release) std::this_thread::sleep_for(1ms); });
    ASSERT_TRUE(WaitFor(started, 2000ms));

    rt.WaitForFrameSlot();                  // one in flight, two allowed : no wait
    rt.SubmitFrame([] {});                  // the second one queues behind the first

    std::atomic<bool> passed{false};
    std::thread simulation([&] { rt.WaitForFrameSlot(); passed = true; });
    std::this_thread::sleep_for(60ms);
    EXPECT_FALSE(passed);                   // the third must wait

    release = true;
    simulation.join();
    EXPECT_TRUE(passed);
}

TEST(RenderThread, CountsFramesAndFiresTheFrameCallback) {
    ScopedRenderThread scoped(4);
    RenderThread& rt = scoped.thread;

    std::atomic<int> callbacks{0};
    rt.onFrameFinished = [&] { callbacks++; };
    rt.ResetStats();

    for (int i = 0; i < 10; i++)
    {
        rt.WaitForFrameSlot();
        rt.SubmitFrame([] { std::this_thread::sleep_for(2ms); });
    }
    rt.Flush();

    const RenderThread::Stats stats = rt.GetStats();
    EXPECT_EQ(stats.framesSubmitted, 10u);
    EXPECT_EQ(stats.framesFinished, 10u);
    EXPECT_EQ(callbacks, 10);
    EXPECT_GT(stats.averageFrameMs, 1.0);
    EXPECT_LT(stats.averageFrameMs, 50.0);
    rt.onFrameFinished = nullptr;
}

TEST(RenderThread, OverlapsTheSimulationWithTheFrame) {
    // The point of the thread : with a 10 ms "simulation" and a 10 ms "frame", frames take about 10 ms, not 20
    ScopedRenderThread scoped(2);
    RenderThread& rt = scoped.thread;

    // Measured against the same work done one after the other on this machine : sleeps are as long as the machine's timer
    // makes them (15 ms steps on a loaded Windows runner), so an absolute bound is not a fair one. Best of a few runs, as one
    // run can be disturbed by whatever else the machine does.
    const int frames = 20;
    const auto elapsed = [](std::chrono::steady_clock::time_point since) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
    };

    double bestRatio = 1e9;
    double lastSerial = 0.0, lastOverlapped = 0.0;
    for (int attempt = 0; attempt < 3 && bestRatio >= 0.85; attempt++)
    {
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; i++)
        {
            std::this_thread::sleep_for(10ms);
            std::this_thread::sleep_for(10ms);
        }
        lastSerial = elapsed(start);

        start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; i++)
        {
            rt.WaitForFrameSlot();
            std::this_thread::sleep_for(10ms);                                     // simulation of the next frame
            rt.SubmitFrame([] { std::this_thread::sleep_for(10ms); });             // drawing of this one
        }
        rt.Flush();
        lastOverlapped = elapsed(start);

        bestRatio = std::min(bestRatio, lastOverlapped / lastSerial);
    }

    EXPECT_LT(bestRatio, 0.85) << "no overlap between simulation and render (last run : " << lastOverlapped << " ms overlapped, " << lastSerial << " ms serial)";
}

TEST(RenderThread, ManyThreadsCanPostAtOnce) {
    ScopedRenderThread scoped;
    RenderThread& rt = scoped.thread;

    std::atomic<int> count{0};
    std::vector<std::thread> producers;
    for (int t = 0; t < 8; t++)
        producers.emplace_back([&] {
            for (int i = 0; i < 500; i++)
                rt.Post([&] { count++; });
        });
    for (auto& p : producers)
        p.join();
    rt.Flush();
    EXPECT_EQ(count, 8 * 500);
}

TEST(RenderThread, StopDrainsWhatWasPosted) {
    RenderThread& rt = RenderThread::Get();
    ASSERT_TRUE(rt.Start([] {}, [] { return true; }));
    std::atomic<int> count{0};
    for (int i = 0; i < 100; i++)
        rt.Post([&] { std::this_thread::sleep_for(100us); count++; });
    rt.Stop([] {}, [] { return true; });
    EXPECT_EQ(count, 100);
}

TEST(RenderThread, CanBeStartedAgainAfterAStop) {
    for (int round = 0; round < 3; round++)
    {
        ScopedRenderThread scoped;
        int ran = 0;
        scoped.thread.Invoke([&] { ran = round + 1; });
        EXPECT_EQ(ran, round + 1);
    }
}
