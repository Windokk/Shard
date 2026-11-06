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

    class RenderWorldData : public Worlds::IWorldExtension{
        public:
            std::shared_ptr<Objects::Skybox> skybox;

            std::unordered_map<int, std::shared_ptr<Objects::Components::ProbeVolume>> probeVolumes;
            std::unordered_map<int, std::shared_ptr<Objects::Components::ReflectionProbe>> reflectionProbes;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Light>> lightComps;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Model>> models;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Camera>> cameras;
            std::unordered_map<int, std::shared_ptr<Objects::Components::Billboard>> billboards;
            std::unordered_map<int, std::pair<glm::mat4, Mesh*>> meshes;

            bool SetSkybox(const std::string& pathInProject);

            void ClearSkybox();

            std::string GetSkyboxPath() const;

            std::string lightmapData;

            LightmapSettings lightmapSettings;

            DisplaySettings display;

            SSRSettings ssr;

            bool BakeLightmaps(Worlds::World& world);

            void OnLightmapsBaked(Worlds::World& world, const LightmapBakeResult& result);

            void ClearLightmaps(Worlds::World& world);

            void OnComponentAdded(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component, bool cloned) override;
            void OnComponentRemoved(Worlds::World& world, int idInWorld, const std::shared_ptr<Objects::Components::Component>& component) override;
            void OnLoad(Worlds::World& world) override;
            void DeserializeSettings(Worlds::World& world, const nlohmann::json& settings) override;
            void SerializeSettings(const Worlds::World& world, nlohmann::ordered_json& settings) override;
    };

    void RegisterRenderingModule();
}
