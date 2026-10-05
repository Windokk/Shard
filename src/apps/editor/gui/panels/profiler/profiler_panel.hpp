#pragma once

#include "engine/debugging/profiler.hpp"

namespace Shard::Editor::GUI{

    // Editor panel visualizing per-subsystem frame timings (physics, rendering, audio, ...)
    // collected by Engine::Debugging::Profiler, so the heaviest subsystem each frame is obvious.
    class ProfilerPanel
    {
        public:
            void Draw();

        private:
            void DrawBreakdown(const Engine::Debugging::FrameProfile& frame);
            void DrawHistory(const Engine::Debugging::Profiler& profiler);

            // The panel refreshes the numbers/bar it displays every refreshIntervalSeconds
            // instead of every frame, since per-frame values are too jittery to read at a glance.
            // The history graph is unaffected : it keeps recording every frame regardless.
            float refreshIntervalSeconds = 0.25f;
            float timeSinceRefresh = 0.0f;
            Engine::Debugging::FrameProfile displayedFrame{};

            // Category clicked in the breakdown list, whose sub-counters (if any) are expanded
            // below it. -1 means no category is selected.
            int selectedCategory = -1;
    };
}
