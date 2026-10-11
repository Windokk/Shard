#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Shard::Engine::Rendering {

    /// @brief GPU time of named scopes (a render pass, a compute stage), measured with timestamp queries.
    ///
    /// Results come back a few frames late (the GPU is still working on them) ; Latest() is the newest complete frame.
    /// Off by default : the queries cost a little, so it is only switched on while someone looks at the numbers.
    class GpuProfiler {
    public:
        struct Timing {
            std::string name;
            float ms = 0.0f;
        };

        virtual ~GpuProfiler() = default;

        virtual void SetEnabled(bool enabled) = 0;
        bool IsEnabled() const { return m_Enabled.load(std::memory_order_relaxed); }

        /// One frame is BeginFrame ... EndFrame, scopes nest
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void Begin(const char* name) = 0;
        virtual void End() = 0;

        /// The newest frame whose results arrived, and its sequence number (changes whenever a new one arrives). Safe to call
        /// from any thread : the profiler is fed by the render thread.
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

    /// Times a scope for the lifetime of the object ; does nothing without a profiler or while it is off
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
