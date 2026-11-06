#pragma once

#include "engine/core/diagnostics/profiler.hpp"

namespace Shard::Editor::GUI{

    class ProfilerPanel
    {
        public:
            void Draw();

        private:
            void DrawBreakdown(const Engine::Debugging::FrameProfile& frame);
            void DrawHistory(const Engine::Debugging::Profiler& profiler);

            float refreshIntervalSeconds = 0.25f;
            float timeSinceRefresh = 0.0f;
            Engine::Debugging::FrameProfile displayedFrame{};

            int selectedCategory = -1;
    };
}
