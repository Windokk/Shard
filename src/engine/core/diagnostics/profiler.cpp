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

    void Profiler::BeginSample(ProfileCategory category)
    {
        m_SampleStart[static_cast<size_t>(category)] = ProfileClock::now();
    }

    void Profiler::EndSample(ProfileCategory category)
    {
        auto elapsed = ProfileClock::now() - m_SampleStart[static_cast<size_t>(category)];
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();
        ms -= m_SampleOverheadMs;
        m_CurrentFrame.categoryMs[static_cast<size_t>(category)] += ms > 0.0f ? ms : 0.0f;
    }

    void Profiler::BeginRenderSubSample(RenderSubSample sample)
    {
        m_RenderSubSampleStart[static_cast<size_t>(sample)] = ProfileClock::now();
    }

    void Profiler::EndRenderSubSample(RenderSubSample sample)
    {
        auto elapsed = ProfileClock::now() - m_RenderSubSampleStart[static_cast<size_t>(sample)];
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();
        ms -= m_SampleOverheadMs;
        m_CurrentFrame.renderSubMs[static_cast<size_t>(sample)] += ms > 0.0f ? ms : 0.0f;
        m_RenderSubSampleCount++;
    }

    void Profiler::EndFrameSampling(float frameMs)
    {
        m_CurrentFrame.totalMs = frameMs;

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