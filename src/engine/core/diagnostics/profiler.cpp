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

    void Profiler::PublishRenderThread()
    {
        std::lock_guard<std::mutex> lock(m_RenderMutex);
        for (size_t i = 0; i < kProfileCategoryCount; i++)
            m_RenderPublished.categoryMs[i] += m_RenderLocal.categoryMs[i];
        for (size_t i = 0; i < kRenderSubSampleCount; i++)
            m_RenderPublished.renderSubMs[i] += m_RenderLocal.renderSubMs[i];
        m_RenderPublished.subSampleCount += m_RenderLocal.subSampleCount;

        m_RenderLocal.categoryMs = {};
        m_RenderLocal.renderSubMs = {};
        m_RenderLocal.subSampleCount = 0;
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

    void Profiler::EndFrameSampling(float frameMs)
    {
        m_CurrentFrame.totalMs = frameMs;

        // What the render thread did since the last time (it is a frame or so behind) joins this frame's figures
        const bool renderThread = s_RenderThreadSampling.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lock(m_RenderMutex);
            for (size_t i = 0; i < kProfileCategoryCount; i++)
                m_CurrentFrame.categoryMs[i] += m_RenderPublished.categoryMs[i];
            for (size_t i = 0; i < kRenderSubSampleCount; i++)
                m_CurrentFrame.renderSubMs[i] += m_RenderPublished.renderSubMs[i];
            m_RenderSubSampleCount += m_RenderPublished.subSampleCount;
            m_RenderPublished.categoryMs = {};
            m_RenderPublished.renderSubMs = {};
            m_RenderPublished.subSampleCount = 0;
        }

        // Rendering's wall time contains every sub-sample's own bookkeeping : remove it.
        float& renderingMs = m_CurrentFrame.categoryMs[static_cast<size_t>(ProfileCategory::Rendering)];
        renderingMs -= m_RenderSubSampleCount * m_SampleOverheadMs;
        if (renderingMs < 0.0f)
            renderingMs = 0.0f;
        m_RenderSubSampleCount = 0;

        float tracked = 0.0f;
        for (size_t i = 0; i < kProfileCategoryCount; i++)
        {
            if (static_cast<ProfileCategory>(i) == ProfileCategory::Other)
                continue;
            // Time the render thread spent is not time this thread frame was busy
            if (renderThread && (static_cast<ProfileCategory>(i) == ProfileCategory::Rendering || static_cast<ProfileCategory>(i) == ProfileCategory::Presentation))
                continue;
            tracked += m_CurrentFrame.categoryMs[i];
        }

        float other = m_CurrentFrame.totalMs - tracked;
        m_CurrentFrame.categoryMs[static_cast<size_t>(ProfileCategory::Other)] = other > 0.0f ? other : 0.0f;

        m_LastFrame = m_CurrentFrame;
        m_History[m_HistoryCursor] = m_CurrentFrame;
        m_HistoryCursor = (m_HistoryCursor + 1) % kHistoryLength;
        if (m_HistoryCount < kHistoryLength)
            m_HistoryCount++;

        m_CurrentFrame = FrameProfile{};
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