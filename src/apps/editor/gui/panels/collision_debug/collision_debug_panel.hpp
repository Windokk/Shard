#pragma once

#include <cstdint>

namespace Shard::Editor::GUI{

    }

namespace Shard::Editor::Core { class EditorMainWindow; }

namespace Shard::Editor::GUI{

    class CollisionDebugPanel
    {
        public:
            void SetParentWindow(Core::EditorMainWindow* window) { parent = window; }
            void Draw(bool* open);

        private:
            void DrawExplain();
            void DrawRecorder();
            void DrawLive();

            Core::EditorMainWindow* parent = nullptr;
            int step = -1;
            bool follow = true;
            bool paused = false;
            bool swapped = false;
    };
}
