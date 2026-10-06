#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/world/world.hpp"
#include "engine/renderer/components/skybox.hpp"

namespace Shard::Engine::Objects::Components{
    class Light;
    class Camera;
    class Model;
    class ProbeVolume;
}

namespace Shard::Engine::Rendering{

    class Mesh;

    /// What the renderer keeps on a world : the components it draws or lights the scene with, and the skybox.
    class RenderWorldData : public Worlds::IWorldExtension{
        public:
            std::shared_ptr<Objects::Components::ProbeVolume> probeVolume;
            std::shared_ptr<Objects::Skybox> skybox;

            // These are maps for fast lookup (key: id IN WORLD, value: ptr to the comp)
            std::vector<std::shared_ptr<Objects::Components::Light>> lights;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Light>> lightComps;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Model>> models;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Camera>> cameras;
            std::unordered_map<int, std::pair<glm::mat4, Mesh*>> meshes;

            /// @brief Makes `pathInProject` (an equirectangular RGB image, e.g. an .hdr) this world's skybox,
            /// creating the skybox if the world has none. Takes effect immediately - the sky, the IBL
            /// lighting and the probes' sky term all read the skybox every frame - but note the map
            /// is loaded and IBL-convolved synchronously the first time it is used.
            /// @return false (logged) if the file can't be used as an environment map; the world's current
            /// skybox, if any, is then left as it was.
            bool SetSkybox(const std::string& pathInProject);

            /// @brief Removes the world's skybox (and its draw command). No-op if it has none.
            void ClearSkybox();

            /// @brief Path in the project of the skybox's image, or empty if the world has no skybox. This
            /// is what the world file stores.
            std::string GetSkyboxPath() const;

            void OnComponentAdded(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned) override;
            void OnComponentRemoved(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component) override;
            void OnLoad(Worlds::World& world) override;
            void DeserializeSettings(Worlds::World& world, const nlohmann::json& settings) override;
            void SerializeSettings(const Worlds::World& world, nlohmann::ordered_json& settings) override;
    };

    /// Declares to the world what the renderer adds to it : its component types, its world extension and the
    /// resources it reads from a world file.
    void RegisterRenderingModule();
}
