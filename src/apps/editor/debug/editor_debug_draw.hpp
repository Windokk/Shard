#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <glm/glm.hpp>

#include "engine/core/color.hpp"

namespace Shard::Engine::Objects{
    class Actor;
}

namespace Shard::Engine::Rendering{
    class Material;
    class Renderer;
    struct RenderPass;
}

namespace Shard::Editor::Debug{

    class DebugShape;

    enum class DebugLayer { Volumes, Probes, Physics };

    class EditorDebugDraw{
        public:
            EditorDebugDraw();
            ~EditorDebugDraw();

            void Init();

            bool IsInitialized() const { return m_Material != nullptr; }

            static const char* PassOf(DebugLayer layer);

            void SetLayerVisible(DebugLayer layer, bool visible);

            void Update();

            void Clear();

        private:

            enum class ShapeKind { Sphere, Box, Capsule, Cylinder, ProbeGrid, VolumeSphere, VolumeCylinder, VolumeCone, VolumePyramid };

            struct ShapeSpec{
                ShapeKind kind = ShapeKind::Box;
                float radius = 0.0f;
                float halfHeight = 0.0f;
                glm::vec3 halfExtent = glm::vec3(0.0f);
                glm::ivec3 probeCounts = glm::ivec3(0);

                bool operator==(const ShapeSpec& other) const
                {
                    return kind == other.kind && radius == other.radius && halfHeight == other.halfHeight
                        && halfExtent == other.halfExtent && probeCounts == other.probeCounts;
                }
                bool operator!=(const ShapeSpec& other) const { return !(*this == other); }
            };

            struct Entry{
                ShapeSpec spec;
                std::unique_ptr<DebugShape> shape;
                DebugLayer layer = DebugLayer::Physics;
                uint32_t modelID = 0;
                glm::mat4 model = glm::mat4(1.0f);
                uint64_t commandID = 0;
                uint64_t epoch = 0;
                bool submitted = false;
                bool seen = false;
            };

            void VisitActor(Engine::Objects::Actor& actor);

            void Show(DebugLayer layer, uint32_t ownerID, uint32_t slot, uint32_t objectID, const ShapeSpec& spec, const glm::mat4& model);

            void Remove(Entry& entry);

            std::unique_ptr<DebugShape> BuildShape(const ShapeSpec& spec) const;

            Engine::Rendering::Renderer* m_Renderer = nullptr;
            std::shared_ptr<Engine::Rendering::Material> m_Material;
            std::shared_ptr<Engine::Rendering::RenderPass> m_Passes[3];
            std::unordered_map<uint64_t, Entry> m_Entries;
    };
}
