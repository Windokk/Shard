#pragma once

#include <string>

namespace Shard::Engine::Worlds{
    class World;
}

namespace Shard::Editor::GUI{

    class WorldSettingsPanel
    {
        public:
            void Draw();

        private:
            void DrawGeneralCategory();
            void DrawRenderingCategory();
            void DrawSkyboxSection(Engine::Worlds::World* world);

            void ApplySkybox(Engine::Worlds::World* world, const std::string& pathInProject);

            std::string m_SkyboxInput;
            bool m_SkyboxInputActive = false;
            std::string m_SkyboxError;
            Engine::Worlds::World* m_LastWorld = nullptr;
    };
}
