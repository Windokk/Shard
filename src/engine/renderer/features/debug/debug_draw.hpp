#pragma once

#include <memory>
#include <unordered_map>

#include "engine/core/debug_draw.hpp"

namespace Shard::Engine::Filesystem{
    class AssetIDManager;
}

namespace Shard::Engine::Rendering{

    class IRenderContext;
    class DebugShape;

    /// The renderer's side of Core::IDebugDraw : a shape is a wireframe mesh, drawn with the debug material
    /// in the pass of its layer.
    class DebugDraw : public Core::IDebugDraw{
        public:
            DebugDraw(IRenderContext& renderer, Filesystem::AssetIDManager& assetIDs);
            ~DebugDraw() override;

            Core::DebugShapeHandle CreateShape(const Core::DebugShapeDesc& desc) override;
            void DestroyShape(Core::DebugShapeHandle shape) override;
            void Draw(Core::DebugDrawLayer layer, const std::vector<Core::DebugDrawItem>& items) override;

        private:

            struct Entry{
                std::unique_ptr<DebugShape> shape;
                // Where it was last drawn, to take it back
                bool drawn = false;
                uint32_t ownerID = 0;
                int submeshID = 0;
                Core::DebugDrawLayer layer = Core::DebugDrawLayer::Physics;
            };

            static const char* PassOf(Core::DebugDrawLayer layer);

            IRenderContext* m_Renderer;
            Filesystem::AssetIDManager* m_AssetIDs;
            std::unordered_map<Core::DebugShapeHandle, Entry> m_Shapes;
            Core::DebugShapeHandle m_NextHandle = 1;
    };
}
