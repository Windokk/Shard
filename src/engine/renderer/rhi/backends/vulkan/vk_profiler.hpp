#pragma once

#include <array>
#include <string>
#include <vector>

#include "engine/renderer/rhi/gpu_profiler.hpp"

#include "vk_context.hpp"

namespace Shard::Engine::Rendering {

    /// GPU time of the scopes of a frame (the passes) : a timestamp at the start and at the end of each, in the command buffer, read back a few frames
    /// later once the GPU is done with them.
    class VKGpuProfiler final : public GpuProfiler
    {
        public:

            VKGpuProfiler();
            ~VKGpuProfiler() override;

            void SetEnabled(bool enabled) override { m_Enabled.store(enabled, std::memory_order_relaxed); }
            void BeginFrame() override;
            void EndFrame() override;
            void Begin(const char* name) override;
            void End() override;

        private:

            static constexpr int kFrames = 4;
            static constexpr uint32_t kMaxScopes = 256;

            struct Frame
            {
                VkQueryPool pool = VK_NULL_HANDLE;
                std::vector<std::string> names;
                uint32_t used = 0;
                bool pending = false;
            };

            void Collect(Frame& frame);

            std::array<Frame, kFrames> m_Frames;
            int m_Current = 0;
            bool m_InFrame = false;
            std::vector<uint32_t> m_Open;
    };

}
