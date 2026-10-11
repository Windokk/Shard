#pragma once

#include "engine/renderer/rhi/gpu_profiler.hpp"

#include <array>
#include <cstdint>

namespace Shard::Engine::Rendering {

    class GLGpuProfiler : public GpuProfiler {
    public:
        ~GLGpuProfiler() override;

        void SetEnabled(bool enabled) override { m_Enabled.store(enabled, std::memory_order_relaxed); }
        void BeginFrame() override;
        void EndFrame() override;
        void Begin(const char* name) override;
        void End() override;

    private:
        struct Scope {
            std::string name;
            uint32_t queries[2] = {0, 0};   // timestamp at the start, at the end
        };

        // Frames in flight : the GPU runs a few frames behind the CPU, a frame's queries are read once they are all done
        static constexpr int kFrames = 4;
        struct Frame {
            std::vector<Scope> scopes;
            size_t used = 0;
            bool pending = false;
        };

        uint32_t AcquireQuery();
        void Collect(Frame& frame);

        std::array<Frame, kFrames> m_Frames;
        int m_Current = 0;
        bool m_InFrame = false;
        std::vector<size_t> m_Open;
        std::vector<uint32_t> m_FreeQueries;
    };
}
