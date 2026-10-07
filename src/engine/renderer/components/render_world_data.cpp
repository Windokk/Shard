#include "render_world_data.hpp"

#include <algorithm>

#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"

namespace Shard::Engine::Rendering{

    using namespace Objects::Components;

    void RenderWorldData::OnComponentAdded(Worlds::World&, int idInWorld, const std::shared_ptr<Component>& component, bool cloned)
    {
        if(auto light = std::dynamic_pointer_cast<Light>(component)){
            light->SetLightIndex((int)lights.size());
            lights.push_back(light);
            lightComps.emplace(idInWorld, light);
        }
        else if(auto model = std::dynamic_pointer_cast<Model>(component)){
            models.emplace(idInWorld, model);

            if(cloned)
                model->Update();
        }
        else if(auto volume = std::dynamic_pointer_cast<ProbeVolume>(component)){
            if(!cloned){
                probeVolume = volume;
                volume->Activate();
            }
        }
        else if(auto camera = std::dynamic_pointer_cast<Camera>(component)){
            cameras.emplace(idInWorld, camera);
            camera->AddToCameraManager();
        }
    }

    void RenderWorldData::OnComponentRemoved(Worlds::World&, int idInWorld, const std::shared_ptr<Component>& component)
    {
        if(component->IsInstanceOf<Camera>()){
            cameras.erase(idInWorld);
        }
        else if(component->IsInstanceOf<Light>()){
            int index = lightComps.at(idInWorld)->GetLightIndex();
            if (index >= 0 && index < (int)lights.size())
            {
                std::rotate(lights.begin() + index, lights.begin() + index + 1, lights.end());
                lights.pop_back();

                // Everything past the removed slot shifted down by one - resync each light's
                // cached index so a later removal doesn't rotate() with a stale (now out-of-range) index.
                for (size_t i = index; i < lights.size(); ++i)
                    lights[i]->ReindexTo((int)i);
            }
            else
            {
                DEBUG_ERROR("World::RemoveComponent(Light) index OUT OF RANGE - light stays stuck in lights vector!");
            }
            lightComps.erase(idInWorld);
        }
        else if(component->IsInstanceOf<Model>()){
            meshes.erase(idInWorld);
        }
    }

    void RenderWorldData::OnLoad(Worlds::World&)
    {
        // The renderer's pass draw lists were cleared on the previous world's unload
        // (ClearPassesContent), so the skybox's draw command has to be re-submitted every load -
        // otherwise a world with a skybox but no actors renders as a black viewport.
        if(skybox)
            skybox->CreateDrawCommands();

        for(auto& [id,cam] : cameras){
            Core::GetEngine().GetCameraManager()->AddCamera(cam->parent->GetID(), cam);
        }

        for(int i = 0; i < lights.size(); i++){
            lights[i]->SetLightIndex(i);
        }

        for(auto& [id,model] : models){
            model->Update();
        }

        if(probeVolume)
            probeVolume->Activate();
    }

    void RenderWorldData::DeserializeSettings(Worlds::World&, const nlohmann::json& settings)
    {
        if(settings.contains("skybox")){
            auto& skyboxFile = settings["skybox"];
            if(skyboxFile.is_string()){
                if(!SetSkybox(skyboxFile.get<std::string>()))
                    DEBUG_ERROR("Couldn't deserialize skybox : shader or cubemap missing");
            }
        }
    }

    void RenderWorldData::SerializeSettings(const Worlds::World&, nlohmann::ordered_json& settings)
    {
        const std::string skyboxFile = GetSkyboxPath();
        if(!skyboxFile.empty()){
            settings["skybox"] = skyboxFile;
        }
    }

    bool RenderWorldData::SetSkybox(const std::string &pathInProject)
    {
        Core::Resources::ResourcesManager* resources = Core::GetEngine().GetResourcesManager();

        // Loading (and IBL-convolving) the map is the part that can fail - bad path, not an image, not
        // 3-channel - so it goes first : a failed attempt must leave the current skybox untouched.
        std::shared_ptr<EnvironmentMap> envMap = resources->Get<EnvironmentMap>(Core::Resources::AssetKind::EnvMap, pathInProject);
        if(!envMap)
            return false;

        // Already have a skybox : keep its material and pipeline, just point it at the new map. Its draw
        // command is updated in place (see Skybox::CreateDrawCommands).
        if(skybox){
            skybox->SetEnvironmentMap(envMap);
            return true;
        }

        std::shared_ptr<Shader> shader = resources->Get<Shader>(Core::Resources::AssetKind::Shader, "shaders/skybox/skybox");
        if(!shader)
            return false;

        PipelineSpecifications specs;
        specs.depthCompare = DepthCompareOp::LessOrEqual;
        specs.depthWrite = false;
        specs.shader = shader;
        specs.topology = PrimitiveTopology::Triangles;
        specs.debugName = "SkyboxPipeline";

        std::shared_ptr<Pipeline> skyboxPipeline = Core::GetEngine().GetRenderContext()->GetOrAddPipeline(specs);

        std::shared_ptr<Material> skyboxMat = Material::Create(shader, skyboxPipeline, false, Opacity::Opaque);

        this->skybox = Core::Object::Create<Objects::Skybox>(envMap, skyboxMat);
        return true;
    }

    void RenderWorldData::ClearSkybox()
    {
        if(!skybox)
            return;

        skybox->RemoveDrawCommands();
        skybox = nullptr;
    }

    std::string RenderWorldData::GetSkyboxPath() const
    {
        if(!skybox || !skybox->GetEnvMap())
            return "";

        std::shared_ptr<Filesystem::AssetInfos> infos = Core::GetEngine().GetAssetIDManager()->GetAssetFromID(skybox->GetEnvMap()->GetAssetID());
        return infos ? infos->baseInfos.nameInProject : "";
    }

    void RegisterRenderingModule()
    {
        using Core::Resources::AssetKind;
        using Core::Resources::ResourceKey;

        auto& registry = GetComponentRegistry();

        registry.RegisterBuiltinComponent<Model>("model",
            [](const nlohmann::json& component, std::vector<ResourceKey>& out){
                // The fields name their assets ("meshID", "materialsID") ; levels saved before the serialization went
                // through the reflection used "mesh" and "materials" (an object by slot)
                for (const char* key : { "meshID", "mesh" }) {
                    if (component.contains(key) && component[key].is_string() && !component[key].get<std::string>().empty()) {
                        out.push_back({AssetKind::Mesh, component[key].get<std::string>()});
                        break;
                    }
                }

                if (component.contains("materialsID") && component["materialsID"].is_array()) {
                    for (const auto& material : component["materialsID"]) {
                        if (material.is_string() && !material.get<std::string>().empty())
                            out.push_back({AssetKind::Material, material.get<std::string>()});
                    }
                }
                else if (component.contains("materials") && component["materials"].is_object()) {
                    for (auto it = component["materials"].begin(); it != component["materials"].end(); ++it) {
                        if (it.value().is_string())
                            out.push_back({AssetKind::Material, it.value().get<std::string>()});
                    }
                }
            });
        registry.RegisterBuiltinComponent<Light>("light");
        registry.RegisterBuiltinComponent<Camera>("camera");
        registry.RegisterBuiltinComponent<ProbeVolume>("probeVolume",
            [](const nlohmann::json& component, std::vector<ResourceKey>& out){
                if (component.contains("bakedData") && component["bakedData"].is_string() && !component["bakedData"].get<std::string>().empty())
                    out.push_back({AssetKind::ProbeBake, component["bakedData"].get<std::string>()});
            });

        Worlds::RegisterWorldExtension<RenderWorldData>();

        Worlds::RegisterWorldSettingsAssetRefs([](const nlohmann::json& settings, std::vector<ResourceKey>& out){
            if (settings.contains("skybox") && settings["skybox"].is_string())
                out.push_back({AssetKind::EnvMap, settings["skybox"].get<std::string>()});
        });
    }
}
