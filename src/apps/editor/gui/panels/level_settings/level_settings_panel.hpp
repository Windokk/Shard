#pragma once

#include <string>

namespace Shard::Engine::Levels{
    class Level;
}

namespace Shard::Editor::GUI{

    class LevelSettingsPanel
    {
        public:
            void Draw();

        private:
            void DrawGeneralCategory();
            void DrawRenderingCategory();
            void DrawSkyboxSection(Engine::Levels::Level* level);

            void ApplySkybox(Engine::Levels::Level* level, const std::string& pathInProject);

            std::string m_SkyboxInput;
            bool m_SkyboxInputActive = false;
            std::string m_SkyboxError;
            Engine::Levels::Level* m_LastLevel = nullptr;
    };
}
