#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Shard::Engine::Rendering {

    class GpuProfiler {
    public:
        struct Timing {
            std::string name;
            float ms = 0.0f;
        };

        virtual ~GpuProfiler() = default;

        virtual void SetEnabled(bool enabled) = 0;
        bool IsEnabled() const { return m_Enabled.load(std::memory_order_relaxed); }

        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void Begin(const char* name) = 0;
        virtual void End() = 0;

        std::vector<Timing> Latest() const
        {
            std::lock_guard<std::mutex> lock(m_LatestMutex);
            return m_Latest;
        }
        uint64_t LatestSequence() const { return m_LatestSequence.load(std::memory_order_acquire); }

    protected:
        void PublishLatest(std::vector<Timing>&& timings)
        {
            {
                std::lock_guard<std::mutex> lock(m_LatestMutex);
                m_Latest = std::move(timings);
            }
            m_LatestSequence.fetch_add(1, std::memory_order_release);
        }

        std::atomic<bool> m_Enabled{false};

    private:
        mutable std::mutex m_LatestMutex;
        std::vector<Timing> m_Latest;
        std::atomic<uint64_t> m_LatestSequence{0};
    };

    class GpuScope {
    public:
        GpuScope(GpuProfiler* profiler, const char* name) : m_Profiler(profiler && profiler->IsEnabled() ? profiler : nullptr)
        {
            if (m_Profiler)
                m_Profiler->Begin(name);
        }
        ~GpuScope()
        {
            if (m_Profiler)
                m_Profiler->End();
        }
        GpuScope(const GpuScope&) = delete;
        GpuScope& operator=(const GpuScope&) = delete;

    private:
        GpuProfiler* m_Profiler;
    };
}
