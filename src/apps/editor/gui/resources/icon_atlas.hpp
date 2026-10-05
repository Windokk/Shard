
#pragma once

#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/assets/vfs/filesystem.hpp"

#include <imgui/imgui.h>

namespace Shard::Editor::GUI {

    struct AtlasRegion
    {
        ImVec2 uv0;
        ImVec2 uv1;
    };

    class IconAtlas
    {
        public:
            bool Build(const std::vector<std::string>& iconPaths, int atlasSize = 1024);

            std::shared_ptr<Engine::Rendering::Texture2D> GetTexture() { return texture; }
            const AtlasRegion& GetRegion(const Engine::Filesystem::Type type) const;

        private:
            std::shared_ptr<Engine::Rendering::Texture2D> texture;
            std::unordered_map<Engine::Filesystem::Type, AtlasRegion> regions;
            int width = 0;
            int height = 0;
    };
}