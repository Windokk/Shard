#include "profiler.hpp"

#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Debugging{
   
    Profiler::Profiler(){

        CalibrateOverhead();

    }

    void Profiler::AddStatsProvider(StatsProvider provider)
    {
        m_StatsProviders.push_back(std::move(provider));
    }

    MinimalStatistics Profiler::GetStats()
    {
        MinimalStatistics ret{};

        for (const StatsProvider& provider : m_StatsProviders)
            provider(ret);

        return ret;
    }

    void Profiler::Shutdown()
    {
    }

    void Profiler::CalibrateOverhead()
    {
        constexpr int kIterations = 20000;

        auto start = ProfileClock::now();
        for (int i = 0; i < kIterations; i++)
        {
            BeginRenderSubSample(RenderSubSample::CameraUpdate);
            EndRenderSubSample(RenderSubSample::CameraUpdate);
        }
        auto total = ProfileClock::now() - start;

        // Raw (uncorrected) cost of a pair : the empty scopes above measure nothing but themselves.
        m_SampleOverheadMs = std::chrono::duration<float, std::milli>(total).count() / kIterations;

        m_CurrentFrame = FrameProfile{};
        m_RenderSubSampleCount = 0;
    }

    void Profiler::MarkRenderThread()
    {
        t_RenderSide = true;
        s_RenderThreadSampling.store(true, std::memory_order_release);
    }

    void Profiler::UnmarkRenderThread()
    {
        t_RenderSide = false;
        s_RenderThreadSampling.store(false, std::memory_order_release);
    }

    void Profiler::BeginRenderFrame(uint64_t frame)
    {
        // Only the render thread has a side of its own : on the thread that draws inline there is nothing to separate
        if (!t_RenderSide)
            return;

        std::lock_guard<std::mutex> lock(m_RenderMutex);

        // What was sampled so far (the frame, its UI, its swap) is the previous frame's : it is complete now
        if (m_RenderFrameValid)
            m_RenderDone[m_RenderFrame] = m_RenderLocal;

        m_RenderLocal.categoryMs = {};
        m_RenderLocal.renderSubMs = {};
        m_RenderLocal.subSampleCount = 0;
        m_RenderLocal.swapEnd = {};
        m_RenderLocal.frameStart = ProfileClock::now();

        m_RenderFrame = frame;
        m_RenderFrameValid = true;
        m_RenderStarted = frame;
        m_RenderStartedValid = true;
    }

    void Profiler::BeginSample(ProfileCategory category)
    {
        (t_RenderSide ? m_RenderLocal.sampleStart : m_SampleStart)[static_cast<size_t>(category)] = ProfileClock::now();
    }

    void Profiler::EndSample(ProfileCategory category)
    {
        const size_t index = static_cast<size_t>(category);
        auto elapsed = ProfileClock::now() - (t_RenderSide ? m_RenderLocal.sampleStart : m_SampleStart)[index];
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();
        ms -= m_SampleOverheadMs;
        (t_RenderSide ? m_RenderLocal.categoryMs : m_CurrentFrame.categoryMs)[index] += ms > 0.0f ? ms : 0.0f;
        if (t_RenderSide && category == ProfileCategory::Presentation)
            m_RenderLocal.swapEnd = ProfileClock::now();
    }

    void Profiler::BeginRenderSubSample(RenderSubSample sample)
    {
        (t_RenderSide ? m_RenderLocal.subSampleStart : m_RenderSubSampleStart)[static_cast<size_t>(sample)] = ProfileClock::now();
    }

    void Profiler::EndRenderSubSample(RenderSubSample sample)
    {
        const size_t index = static_cast<size_t>(sample);
        auto elapsed = ProfileClock::now() - (t_RenderSide ? m_RenderLocal.subSampleStart : m_RenderSubSampleStart)[index];
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();
        ms -= m_SampleOverheadMs;
        if (t_RenderSide)
        {
            m_RenderLocal.renderSubMs[index] += ms > 0.0f ? ms : 0.0f;
            m_RenderLocal.subSampleCount++;
        }
        else
        {
            m_CurrentFrame.renderSubMs[index] += ms > 0.0f ? ms : 0.0f;
            m_RenderSubSampleCount++;
        }
    }

    void Profiler::EndFrameSampling(float frameMs, uint64_t frame)
    {
        m_CurrentFrame.totalMs = frameMs;

        PendingFrame pending;
        pending.frame = frame;
        pending.profile = m_CurrentFrame;
        pending.subSampleCount = m_RenderSubSampleCount;
        pending.start = ProfileClock::now() - std::chrono::duration_cast<ProfileClock::duration>(std::chrono::duration<float, std::milli>(frameMs));
        m_Pending.push_back(pending);

        m_CurrentFrame = FrameProfile{};
        m_RenderSubSampleCount = 0;

        // A frame is closed once the render thread has begun a later one : its own part of the frame is complete by then.
        // Without a render thread there is nothing to wait for. The cap keeps a stalled render thread from holding frames back.
        constexpr size_t kMaxPending = 8;
        while (!m_Pending.empty())
        {
            PendingFrame& front = m_Pending.front();

            RenderSide render;
            bool hasRender = false;
            bool ready = false;
            {
                std::lock_guard<std::mutex> lock(m_RenderMutex);
                ready = !s_RenderThreadSampling.load(std::memory_order_acquire)
                     || (m_RenderStartedValid && m_RenderStarted > front.frame)
                     || m_Pending.size() > kMaxPending;
                if (!ready)
                    break;

                auto it = m_RenderDone.find(front.frame);
                if (it != m_RenderDone.end())
                {
                    render = it->second;
                    hasRender = true;
                }
                // Nothing older than this frame can be claimed any more
                m_RenderDone.erase(m_RenderDone.begin(), m_RenderDone.upper_bound(front.frame));
            }

            FinalizeFrame(front, hasRender ? &render : nullptr);
            m_Pending.pop_front();
        }
    }

    void Profiler::FinalizeFrame(PendingFrame& pending, const RenderSide* render)
    {
        FrameProfile frame = pending.profile;
        uint32_t subSampleCount = pending.subSampleCount;

        // `otherThreadMs` is the part of each category that was sampled by the render thread : the rest was sampled by
        // the simulation thread (e.g. its own Present() wait), which is time that thread did account for.
        std::array<float, kProfileCategoryCount> otherThreadMs{};
        if (render)
        {
            otherThreadMs = render->categoryMs;
            for (size_t i = 0; i < kProfileCategoryCount; i++)
                frame.categoryMs[i] += render->categoryMs[i];
            for (size_t i = 0; i < kRenderSubSampleCount; i++)
                frame.renderSubMs[i] += render->renderSubMs[i];
            subSampleCount += render->subSampleCount;

            if (render->swapEnd != ProfileClock::time_point{})
                frame.renderMs = std::chrono::duration<float, std::milli>(render->swapEnd - render->frameStart).count();
            else
            {
                for (size_t i = 0; i < kProfileCategoryCount; i++)
                    frame.renderMs += render->categoryMs[i];
            }
        }

        frame.frameMs = frame.totalMs + frame.renderMs;

        // Rendering's wall time contains every sub-sample's own bookkeeping : remove it.
        float& renderingMs = frame.categoryMs[static_cast<size_t>(ProfileCategory::Rendering)];
        renderingMs -= subSampleCount * m_SampleOverheadMs;
        if (renderingMs < 0.0f)
            renderingMs = 0.0f;

        float tracked = 0.0f;
        for (size_t i = 0; i < kProfileCategoryCount; i++)
        {
            if (static_cast<ProfileCategory>(i) == ProfileCategory::Other)
                continue;
            // Time the render thread spent is not time this thread's frame was busy
            tracked += frame.categoryMs[i] - otherThreadMs[i];
        }

        float other = frame.totalMs - tracked;
        frame.categoryMs[static_cast<size_t>(ProfileCategory::Other)] = other > 0.0f ? other : 0.0f;

        m_LastFrame = frame;
        m_History[m_HistoryCursor] = frame;
        m_HistoryCursor = (m_HistoryCursor + 1) % kHistoryLength;
        if (m_HistoryCount < kHistoryLength)
            m_HistoryCount++;
    }

    ScopedProfileSample::ScopedProfileSample(ProfileCategory category)
        : category(category)
    {
        if (Profiler* profiler = Profiler::Active())
            profiler->BeginSample(category);
    }

    ScopedProfileSample::~ScopedProfileSample()
    {
        if (Profiler* profiler = Profiler::Active())
            profiler->EndSample(category);
    }

    ScopedRenderSubSample::ScopedRenderSubSample(RenderSubSample sample)
        : sample(sample)
    {
        if (Profiler* profiler = Profiler::Active())
            profiler->BeginRenderSubSample(sample);
    }

    ScopedRenderSubSample::~ScopedRenderSubSample()
    {
        if (Profiler* profiler = Profiler::Active())
            profiler->EndRenderSubSample(sample);
    }
}