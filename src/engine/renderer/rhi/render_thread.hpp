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

    /// @brief The thread that owns the graphics context : every call into the graphics API runs on it, so the simulation
    /// (the main thread) is free to start the next frame while the previous one is still being drawn.
    ///
    /// Work reaches it in one FIFO, in the order it was submitted :
    ///  - SubmitFrame(f)  a frame (or a part of it). Counts toward the frames in flight : WaitForFrameSlot() is what keeps
    ///                    the simulation from running further than that ahead of what is drawn (the frame pacing).
    ///  - Post(f)         fire and forget (releasing a GPU object, presenting).
    ///  - Invoke(f)       run `f` on the render thread and wait for it. The caller is parked meanwhile, so `f` may read what
    ///                    the caller's thread owns (the UI, the world) : this is how code that has to touch both sides, or that
    ///                    needs the graphics API right now (creating a texture), goes through.
    /// All three run `f` right away, on the calling thread, when it already is the graphics thread - which is the main thread
    /// as long as the render thread is not started (headless runs, tests, `render.thread 0`).
    class RenderThread {
    public:
        static RenderThread& Get();

        /// @brief Starts the thread. `release` runs on the calling thread and lets go of the graphics context, `acquire` then
        /// makes it current on the new one. The context comes back to the calling thread on Stop().
        /// @return false (and the context stays with the caller) if the new thread could not take the context
        bool Start(std::function<void()> release, std::function<bool()> acquire);

        /// Drains the queue, hands the context back (`acquire`/`release` of Start, swapped) and joins the thread
        void Stop(std::function<void()> release, std::function<bool()> acquire);

        bool IsRunning() const { return m_Running.load(std::memory_order_acquire); }

        /// Says which thread the graphics API belongs to while the render thread is not running (the one that created the
        /// context). Start() does it for its caller.
        void BindHomeThread();

        /// True on the thread the graphics API belongs to : the render thread while it runs, the home thread otherwise
        bool IsGraphicsThread() const;

        void Post(std::function<void()> work);
        void Invoke(std::function<void()> work);
        void SubmitFrame(std::function<void()> work);

        /// Everything submitted so far has run when this returns
        void Flush();

        /// @brief Blocks while `MaxFramesInFlight()` frames are submitted and not finished. Call it before building the next frame.
        void WaitForFrameSlot();

        void SetMaxFramesInFlight(int frames) { m_MaxFramesInFlight.store(frames < 1 ? 1 : frames, std::memory_order_relaxed); }
        int MaxFramesInFlight() const { return m_MaxFramesInFlight.load(std::memory_order_relaxed); }

        /// Called on the render thread after every frame (the profiler hands its render side to the main thread this way)
        std::function<void()> onFrameFinished;

        /// Records a call that needed the graphics thread and was made from another one (a bug : the graphics API would run
        /// without its context). The first few places are remembered by name, the rest only counted.
        void ReportWrongThread(const char* what);
        uint64_t WrongThreadCount() const { return m_WrongThreadCount.load(std::memory_order_relaxed); }
        std::vector<std::string> WrongThreadPlaces() const;

        /// What the thread did, for the console and the benchmark
        struct Stats {
            uint64_t framesSubmitted = 0;
            uint64_t framesFinished = 0;
            double averageFrameMs = 0.0;        // render thread time per frame task, smoothed
            double averageWaitMs = 0.0;         // time the simulation waited for a frame slot per frame, smoothed
            double averageInvokeMs = 0.0;       // time spent parked in Invoke() per frame, smoothed (render thread busy or not)
            double busyFraction = 0.0;          // how much of the time since the last reset the thread had work
        };
        Stats GetStats() const;
        void ResetStats();

        /// Runs `f` on the graphics thread and returns what it returns
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

        // Stats (guarded by m_Mutex)
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

    /// Runs `f` on the graphics thread and waits (see RenderThread::Invoke)
    template <class F>
    inline auto OnGraphicsThread(F&& f) -> std::invoke_result_t<F>
    {
        return RenderThread::Get().InvokeResult(std::forward<F>(f));
    }
}

/// At the top of a void function that has to run on the graphics thread : anywhere else the call is forwarded to it (and the
/// caller waits), so the function body can use the graphics API without caring who called it.
#define SHARD_FORWARD_TO_GRAPHICS_THREAD(call)                                                              \
    if (!::Shard::Engine::Rendering::RenderThread::Get().IsGraphicsThread())                                \
    {                                                                                                       \
        ::Shard::Engine::Rendering::RenderThread::Get().Invoke([&] { call; });                              \
        return;                                                                                             \
    }

/// At the top of a function that uses the graphics API and does not forward itself : reports (see RenderThread::ReportWrongThread)
/// when it is called from another thread than the graphics one. A thread local read and a compare when all is well.
#define SHARD_ASSERT_GRAPHICS_THREAD()\
    do {\
        if (!::Shard::Engine::Rendering::RenderThread::Get().IsGraphicsThread())\
            ::Shard::Engine::Rendering::RenderThread::Get().ReportWrongThread(__func__);\
    } while (false)
