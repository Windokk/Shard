#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/world/world.hpp"
#include "engine/renderer/components/skybox.hpp"
#include "engine/renderer/features/lighting/lightmap_data.hpp"
#include "engine/renderer/rhi/display_transform.hpp"
#include "engine/renderer/rhi/ssr_settings.hpp"

namespace Shard::Engine::Objects::Components{
    class Light;
    class Camera;
    class Billboard;
    class Model;
    class ProbeVolume;
    class ReflectionProbe;
}

namespace Shard::Engine::Rendering{

    class Mesh;
    struct LightmapBakeResult;

    /// What the renderer keeps on a world : the components it draws or lights the scene with, and the skybox.
    class RenderWorldData : public Worlds::IWorldExtension{
        public:
            std::shared_ptr<Objects::Skybox> skybox;

            // These are maps for fast lookup (key: id IN WORLD, value: ptr to the comp)
            std::unordered_map<int, std::shared_ptr<Objects::Components::ProbeVolume>> probeVolumes;
            std::unordered_map<int, std::shared_ptr<Objects::Components::ReflectionProbe>> reflectionProbes;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Light>> lightComps;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Model>> models;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Camera>> cameras;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Billboard>> billboards;
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

            // ---- Baked lighting (see LightmapManager) ----

            /// Path in the project (e.g. "lightmaps/sponza.lightmap") of the baked lightmap pages the world's static models
            /// read, empty if the world was never baked. The world file stores it ; where each model's lightmap is in those
            /// pages is stored in the model.
            std::string lightmapData;

            /// The settings of the world's bake, stored with the world so a re-bake starts from the last one
            LightmapSettings lightmapSettings;

            // ---- Display ----

            /// How the light of the scene becomes the picture : the tonemapper and the exposure (see display_transform.hpp)
            DisplaySettings display;

            // ---- Screen-space reflections ----

            /// How the world's screen-space reflections are traced and mixed into the specular light (see SSRManager)
            SSRSettings ssr;

            /// @brief Bakes the lightmaps of this world's static models (asynchronously, see LightmapManager::BeginBake), into
            /// the project's "lightmaps" folder (or over the file the world already has). Needs the world to be loaded.
            /// @return false (logged) if the bake could not start
            bool BakeLightmaps(Worlds::World& world);

            /// @brief Called (on the main thread) when a bake has written its file : registers the file as an asset, stores where
            /// every static model's lightmap is, and flags the world as having unsaved changes - the world file has to be saved
            /// for the references to persist.
            void OnLightmapsBaked(Worlds::World& world, const LightmapBakeResult& result);

            /// @brief Deletes the baked lightmaps (file, asset entry, every model's placement) : the world is lit by its probes again
            void ClearLightmaps(Worlds::World& world);

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
