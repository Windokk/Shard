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

    /// Which family of editor drawings a shape belongs to : each family has its own pass, so the viewport can show
    /// or hide it without touching the components it is drawn for.
    enum class DebugLayer { Volumes, Probes, Physics };

    /// @brief What the editor draws over the scene to show what the components are : the colliders of the physics
    /// bodies, the box of the volumes, the probes of the GI volumes.
    /// @note It lives in the editor, not in the engine : the components only expose their data (PhysicsBody's shapes,
    /// Volume's half extent...), and every frame this reads it and keeps one wireframe draw command per thing to show.
    /// A game has none of it.
    class EditorDebugDraw{
        public:
            EditorDebugDraw();
            ~EditorDebugDraw();

            /// Registers the passes and builds the material the shapes are drawn with. Needs the renderer and its
            /// viewport framebuffer. No-op once done.
            void Init();

            bool IsInitialized() const { return m_Material != nullptr; }

            /// Names of the passes of each layer, for the passes that have to run after (or before) them
            static const char* PassOf(DebugLayer layer);

            /// Shows or hides a family of drawings. Hiding keeps the shapes and their commands, it only skips the pass.
            void SetLayerVisible(DebugLayer layer, bool visible);

            /// Walks the loaded worlds and brings the drawings in line with their components : new ones are created,
            /// the ones whose component changed are rebuilt or moved, the ones whose component is gone are removed.
            void Update();

            /// Removes every drawing
            void Clear();

        private:

            enum class ShapeKind { Sphere, Box, Capsule, Cylinder, ProbeGrid };

            /// Everything the mesh of a shape depends on : a drawing is rebuilt when it changes
            struct ShapeSpec{
                ShapeKind kind = ShapeKind::Box;
                float radius = 0.0f;           // sphere, capsule, cylinder
                float halfHeight = 0.0f;       // capsule, cylinder
                glm::vec3 halfExtent = glm::vec3(0.0f); // box, probe grid
                glm::ivec3 probeCounts = glm::ivec3(0); // probe grid

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
                bool submitted = false;
                bool seen = false;
            };

            void VisitActor(Engine::Objects::Actor& actor);

            /// Makes sure `slot` of `ownerID` (a component id in its world) shows `spec` at `model` in `layer`
            void Show(DebugLayer layer, uint32_t ownerID, uint32_t slot, uint32_t objectID, const ShapeSpec& spec, const glm::mat4& model);

            void Remove(Entry& entry);

            std::unique_ptr<DebugShape> BuildShape(const ShapeSpec& spec) const;

            Engine::Rendering::Renderer* m_Renderer = nullptr;
            std::shared_ptr<Engine::Rendering::Material> m_Material;
            std::shared_ptr<Engine::Rendering::RenderPass> m_Passes[3];
            std::unordered_map<uint64_t, Entry> m_Entries;
    };
}
