#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <type_traits>
#include <vector>
#include <utility>

namespace Shard::Engine::Rendering {

    // Owns the graphics context. Single FIFO: SubmitFrame (counts toward frames in flight), Post (fire and forget), Invoke (run and wait).
    // All three run inline when the calling thread is already the graphics thread (render thread stopped, headless, tests).
    class RenderThread {
    public:
        static RenderThread& Get();

        bool Start(std::function<void()> release, std::function<bool()> acquire);

        void Stop(std::function<void()> release, std::function<bool()> acquire);

        bool IsRunning() const { return m_Running.load(std::memory_order_acquire); }

        void BindHomeThread();

        bool IsGraphicsThread() const;

        void Post(std::function<void()> work);
        void Invoke(std::function<void()> work);
        void SubmitFrame(std::function<void()> work);

        void Flush();

        void WaitForFrameSlot();

        void SetMaxFramesInFlight(int frames) { m_MaxFramesInFlight.store(frames < 1 ? 1 : frames, std::memory_order_relaxed); }
        int MaxFramesInFlight() const { return m_MaxFramesInFlight.load(std::memory_order_relaxed); }

        std::function<void()> onFrameFinished;

        void ReportWrongThread(const char* what);
        uint64_t WrongThreadCount() const { return m_WrongThreadCount.load(std::memory_order_relaxed); }
        std::vector<std::string> WrongThreadPlaces() const;

        struct Stats {
            uint64_t framesSubmitted = 0;
            uint64_t framesFinished = 0;
            double averageFrameMs = 0.0;
            double averageWaitMs = 0.0;
            double averageInvokeMs = 0.0;
            double busyFraction = 0.0;
        };
        Stats GetStats() const;
        void ResetStats();

        template <class F>
        auto InvokeResult(F&& f) -> std::invoke_result_t<F>
        {
            using R = std::invoke_result_t<F>;
            if (IsGraphicsThread())
                return f();
            if constexpr (std::is_void_v<R>)
            {
                Invoke(std::forward<F>(f));
            }
            else
            {
                R result{};
                Invoke([&] { result = f(); });
                return result;
            }
        }

    private:
        struct Task {
            std::function<void()> work;
            std::shared_ptr<struct Completion> completion;
            bool isFrame = false;
        };

        void Loop(std::function<bool()> acquire);
        void Enqueue(Task task);

        mutable std::mutex m_Mutex;
        std::condition_variable m_WorkReady;
        std::condition_variable m_FrameDone;
        std::deque<Task> m_Queue;
        std::thread m_Thread;
        bool m_StopRequested = false;
        std::atomic<bool> m_Running{false};
        std::atomic<int> m_MaxFramesInFlight{2};
        std::atomic<uint64_t> m_WrongThreadCount{0};
        mutable std::mutex m_WrongThreadMutex;
        std::vector<std::string> m_WrongThreadNames;
        int m_FramesInFlight = 0;
        bool m_Started = false;
        bool m_StartSucceeded = false;
        std::thread::id m_HomeThread;
        std::atomic<bool> m_HomeBound{false};

        uint64_t m_FramesSubmitted = 0;
        uint64_t m_FramesFinished = 0;
        double m_AverageFrameMs = 0.0;
        double m_AverageWaitMs = 0.0;
        double m_AverageInvokeMs = 0.0;
        double m_BusySeconds = 0.0;
        double m_InvokeMsThisFrame = 0.0;
        uint64_t m_WaitSamples = 0;
        std::chrono::steady_clock::time_point m_StatsStart = std::chrono::steady_clock::now();

        inline static thread_local bool t_IsRenderThread = false;
    };

    template <class F>
    inline auto OnGraphicsThread(F&& f) -> std::invoke_result_t<F>
    {
        return RenderThread::Get().InvokeResult(std::forward<F>(f));
    }
}

#define SHARD_FORWARD_TO_GRAPHICS_THREAD(call)                                                              \
    if (!::Shard::Engine::Rendering::RenderThread::Get().IsGraphicsThread())                                \
    {                                                                                                       \
        ::Shard::Engine::Rendering::RenderThread::Get().Invoke([&] { call; });                              \
        return;                                                                                             \
    }

#define SHARD_ASSERT_GRAPHICS_THREAD()\
    do {\
        if (!::Shard::Engine::Rendering::RenderThread::Get().IsGraphicsThread())\
            ::Shard::Engine::Rendering::RenderThread::Get().ReportWrongThread(__func__);\
    } while (false)
