#include "render_thread.hpp"

#include <algorithm>
#include <cstdio>

namespace Shard::Engine::Rendering {

    struct Completion {
        std::mutex mutex;
        std::condition_variable condition;
        bool done = false;
        std::exception_ptr error;
    };

    namespace {
        using Clock = std::chrono::steady_clock;

        double MillisecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        void Smooth(double& average, double sample, bool first)
        {
            average = first ? sample : average + (sample - average) * 0.1;
        }
    }

    RenderThread& RenderThread::Get()
    {
        static RenderThread instance;
        return instance;
    }

    void RenderThread::BindHomeThread()
    {
        m_HomeThread = std::this_thread::get_id();
        m_HomeBound.store(true, std::memory_order_release);
    }

    bool RenderThread::IsGraphicsThread() const
    {
        if (t_IsRenderThread)
            return true;
        if (IsRunning())
            return false;
        // Not running : the graphics API belongs to the thread that set it up. Before anyone said which, any thread will do
        // (tests, tools).
        return !m_HomeBound.load(std::memory_order_acquire) || m_HomeThread == std::this_thread::get_id();
    }

    bool RenderThread::Start(std::function<void()> release, std::function<bool()> acquire)
    {
        if (IsRunning())
            return true;

        BindHomeThread();
        if (release)
            release();      // the calling thread lets go of the context : the new one takes it

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_StopRequested = false;
            m_Started = false;
            m_StartSucceeded = false;
            m_FramesInFlight = 0;
        }

        m_Thread = std::thread([this, acquire]() { Loop(acquire); });

        bool ok = false;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_FrameDone.wait(lock, [this] { return m_Started; });
            ok = m_StartSucceeded;
        }

        if (!ok)
        {
            m_Thread.join();
            if (acquire)
                acquire();  // the context goes back where it was
            return false;
        }

        ResetStats();
        return true;
    }

    void RenderThread::Stop(std::function<void()> release, std::function<bool()> acquire)
    {
        if (!IsRunning())
            return;

        // Everything submitted runs first, then the thread lets go of the context on its way out
        if (release)
            Invoke(std::move(release));
        else
            Flush();

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_StopRequested = true;
        }
        m_WorkReady.notify_all();
        m_Thread.join();
        m_Running.store(false, std::memory_order_release);

        if (acquire)
            acquire();

        // Whatever another thread queued while the thread was on its way out runs here, now that this thread owns the
        // context again (a task left behind would leave its caller waiting forever)
        std::deque<Task> leftovers;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            leftovers.swap(m_Queue);
            m_FramesInFlight = 0;
        }
        for (Task& task : leftovers)
        {
            try
            {
                if (task.work)
                    task.work();
            }
            catch (...)
            {
                if (task.completion)
                    task.completion->error = std::current_exception();
            }
            if (task.completion)
            {
                std::lock_guard<std::mutex> lock(task.completion->mutex);
                task.completion->done = true;
                task.completion->condition.notify_all();
            }
        }
    }

    void RenderThread::Loop(std::function<bool()> acquire)
    {
        t_IsRenderThread = true;

        const bool ok = !acquire || acquire();
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Started = true;
            m_StartSucceeded = ok;
            if (ok)
                m_Running.store(true, std::memory_order_release);
        }
        m_FrameDone.notify_all();

        if (!ok)
        {
            t_IsRenderThread = false;
            return;
        }

        for (;;)
        {
            Task task;
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_WorkReady.wait(lock, [this] { return !m_Queue.empty() || m_StopRequested; });
                if (m_Queue.empty())
                    break;
                task = std::move(m_Queue.front());
                m_Queue.pop_front();
            }

            const auto start = Clock::now();
            try
            {
                if (task.work)
                    task.work();
            }
            catch (...)
            {
                if (task.completion)
                    task.completion->error = std::current_exception();
                else
                    std::fprintf(stderr, "[RenderThread] a posted task threw an exception\n");
            }
            const double elapsedMs = MillisecondsSince(start);

            if (task.isFrame && onFrameFinished)
            {
                try { onFrameFinished(); }
                catch (...) {}
            }

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_BusySeconds += elapsedMs * 0.001;
                if (task.isFrame)
                {
                    m_FramesInFlight--;
                    Smooth(m_AverageFrameMs, elapsedMs, m_FramesFinished == 0);
                    m_FramesFinished++;
                }
            }
            if (task.isFrame)
                m_FrameDone.notify_all();

            if (task.completion)
            {
                std::lock_guard<std::mutex> lock(task.completion->mutex);
                task.completion->done = true;
                task.completion->condition.notify_all();
            }
        }

        t_IsRenderThread = false;
    }

    void RenderThread::Enqueue(Task task)
    {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (task.isFrame)
            {
                m_FramesInFlight++;
                m_FramesSubmitted++;
            }
            m_Queue.push_back(std::move(task));
        }
        m_WorkReady.notify_one();
    }

    void RenderThread::Post(std::function<void()> work)
    {
        if (IsGraphicsThread() || !IsRunning())
        {
            work();
            return;
        }
        Task task;
        task.work = std::move(work);
        Enqueue(std::move(task));
    }

    void RenderThread::Invoke(std::function<void()> work)
    {
        if (IsGraphicsThread() || !IsRunning())
        {
            work();
            return;
        }

        const auto start = Clock::now();

        Task task;
        task.work = std::move(work);
        task.completion = std::make_shared<Completion>();
        std::shared_ptr<Completion> completion = task.completion;
        Enqueue(std::move(task));

        {
            std::unique_lock<std::mutex> lock(completion->mutex);
            completion->condition.wait(lock, [&] { return completion->done; });
        }

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_InvokeMsThisFrame += MillisecondsSince(start);
        }

        if (completion->error)
            std::rethrow_exception(completion->error);
    }

    void RenderThread::SubmitFrame(std::function<void()> work)
    {
        if (IsGraphicsThread() || !IsRunning())
        {
            work();
            return;
        }
        Task task;
        task.work = std::move(work);
        task.isFrame = true;
        Enqueue(std::move(task));
    }

    void RenderThread::Flush()
    {
        Invoke([] {});
    }

    void RenderThread::WaitForFrameSlot()
    {
        if (!IsRunning())
            return;

        const auto start = Clock::now();
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_FrameDone.wait(lock, [this] { return m_FramesInFlight < MaxFramesInFlight() || !IsRunning(); });

            // One sample per frame : what the simulation waited, and what it spent parked in Invoke() since the last one
            const bool first = m_WaitSamples == 0;
            Smooth(m_AverageWaitMs, MillisecondsSince(start), first);
            Smooth(m_AverageInvokeMs, m_InvokeMsThisFrame, first);
            m_InvokeMsThisFrame = 0.0;
            m_WaitSamples++;
        }
    }

    void RenderThread::ReportWrongThread(const char* what)
    {
        const uint64_t count = m_WrongThreadCount.fetch_add(1, std::memory_order_relaxed) + 1;
        std::lock_guard<std::mutex> lock(m_WrongThreadMutex);
        if (m_WrongThreadNames.size() < 16 && std::find(m_WrongThreadNames.begin(), m_WrongThreadNames.end(), what) == m_WrongThreadNames.end())
        {
            m_WrongThreadNames.push_back(what);
            std::fprintf(stderr, "[RenderThread] %s used the graphics API from a thread that does not own it (call %llu)\n", what, (unsigned long long)count);
        }
    }

    std::vector<std::string> RenderThread::WrongThreadPlaces() const
    {
        std::lock_guard<std::mutex> lock(m_WrongThreadMutex);
        return m_WrongThreadNames;
    }

    RenderThread::Stats RenderThread::GetStats() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        Stats stats;
        stats.framesSubmitted = m_FramesSubmitted;
        stats.framesFinished = m_FramesFinished;
        stats.averageFrameMs = m_AverageFrameMs;
        stats.averageWaitMs = m_AverageWaitMs;
        stats.averageInvokeMs = m_AverageInvokeMs;
        const double elapsed = std::chrono::duration<double>(Clock::now() - m_StatsStart).count();
        stats.busyFraction = elapsed > 0.0 ? m_BusySeconds / elapsed : 0.0;
        return stats;
    }

    void RenderThread::ResetStats()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FramesSubmitted = 0;
        m_FramesFinished = 0;
        m_AverageFrameMs = m_AverageWaitMs = m_AverageInvokeMs = 0.0;
        m_InvokeMsThisFrame = 0.0;
        m_WaitSamples = 0;
        m_BusySeconds = 0.0;
        m_StatsStart = Clock::now();
    }
}
