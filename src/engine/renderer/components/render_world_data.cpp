#include "render_world_data.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>

#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/components/registry/component_registry.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "engine/renderer/components/billboard_component.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/components/render_extractor.hpp"
#include "engine/renderer/features/lighting/lightmap_manager.hpp"
#include "engine/assets/asset_database_serializer.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"

namespace Shard::Engine::Rendering{

    using namespace Objects::Components;

    void RenderWorldData::OnComponentAdded(Worlds::World&, int idInWorld, const std::shared_ptr<Component>& component, bool)
    {
        // What the renderer draws or lights with is read from these maps by the Extract stage (see RenderExtractor)
        if(auto light = std::dynamic_pointer_cast<Light>(component)){
            lightComps.emplace(idInWorld, light);
        }
        else if(auto model = std::dynamic_pointer_cast<Model>(component)){
            models.emplace(idInWorld, model);
        }
        else if(auto volume = std::dynamic_pointer_cast<ProbeVolume>(component)){
            probeVolumes.emplace(idInWorld, volume);
        }
        else if(auto camera = std::dynamic_pointer_cast<Camera>(component)){
            cameras.emplace(idInWorld, camera);
            camera->AddToCameraManager();
        }
        else if(auto billboard = std::dynamic_pointer_cast<Billboard>(component)){
            billboards.emplace(idInWorld, billboard);
        }
    }

    void RenderWorldData::OnComponentRemoved(Worlds::World&, int idInWorld, const std::shared_ptr<Component>& component)
    {
        if(component->IsInstanceOf<Camera>()){
            cameras.erase(idInWorld);
        }
        else if(component->IsInstanceOf<Light>()){
            lightComps.erase(idInWorld);
        }
        else if(component->IsInstanceOf<Model>()){
            models.erase(idInWorld);
            meshes.erase(idInWorld);
        }
        else if(component->IsInstanceOf<ProbeVolume>()){
            probeVolumes.erase(idInWorld);
        }
        else if(component->IsInstanceOf<Billboard>()){
            billboards.erase(idInWorld);
        }
    }

    void RenderWorldData::OnLoad(Worlds::World&)
    {
        if(skybox)
            skybox->CreateDrawCommands();

        for(auto& [id,cam] : cameras){
            Core::GetEngine().GetCameraManager()->AddCamera(cam->parent->GetID(), cam);
        }

    }

    void RenderWorldData::DeserializeSettings(Worlds::World&, const nlohmann::json& settings)
    {
        lightmapData.clear();
        lightmapSettings = LightmapSettings{};
        display = DisplaySettings{};

        if(settings.contains("display") && settings["display"].is_object()){
            const auto& displayJson = settings["display"];

            if(displayJson.contains("tonemapper") && displayJson["tonemapper"].is_string()){
                Tonemapper parsed;
                if(ParseTonemapper(displayJson["tonemapper"].get<std::string>(), parsed))
                    display.tonemapper = parsed;
                else
                    DEBUG_WARNING("Unknown tonemapper \"" + displayJson["tonemapper"].get<std::string>() + "\" : using " + TonemapperName(display.tonemapper));
            }

            if(displayJson.contains("exposure") && displayJson["exposure"].is_number())
                display.exposure = displayJson["exposure"].get<float>();
        }

        if(settings.contains("lightmaps") && settings["lightmaps"].is_object()){
            const auto& lightmaps = settings["lightmaps"];

            auto readFloat = [&](const char* key, float& out){ if(lightmaps.contains(key) && lightmaps[key].is_number()) out = lightmaps[key].get<float>(); };
            auto readUInt = [&](const char* key, uint32_t& out){ if(lightmaps.contains(key) && lightmaps[key].is_number_integer()) out = lightmaps[key].get<uint32_t>(); };

            readFloat("texelsPerMeter", lightmapSettings.texelsPerMeter);
            readUInt("minResolution", lightmapSettings.minResolution);
            readUInt("maxResolution", lightmapSettings.maxResolution);
            readUInt("maxPageSize", lightmapSettings.maxPageSize);
            readUInt("samples", lightmapSettings.samples);
            readUInt("maxBounces", lightmapSettings.maxBounces);
            readFloat("fireflyClamp", lightmapSettings.fireflyClamp);
            readFloat("indirectIntensity", lightmapSettings.indirectIntensity);
            if(lightmaps.contains("denoise") && lightmaps["denoise"].is_boolean())
                lightmapSettings.denoise = lightmaps["denoise"].get<bool>();

            if(lightmaps.contains("data") && lightmaps["data"].is_string())
                lightmapData = lightmaps["data"].get<std::string>();
        }

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

        // The default look is not written : a world only carries what was changed
        if(display != DisplaySettings{}){
            settings["display"]["tonemapper"] = TonemapperName(display.tonemapper);
            settings["display"]["exposure"] = display.exposure;
        }

        // Only a world that was baked, or whose bake settings were touched, carries them
        if(!lightmapData.empty() || !(lightmapSettings == LightmapSettings{})){
            auto& lightmaps = settings["lightmaps"];
            lightmaps["texelsPerMeter"] = lightmapSettings.texelsPerMeter;
            lightmaps["minResolution"] = lightmapSettings.minResolution;
            lightmaps["maxResolution"] = lightmapSettings.maxResolution;
            lightmaps["maxPageSize"] = lightmapSettings.maxPageSize;
            lightmaps["samples"] = lightmapSettings.samples;
            lightmaps["maxBounces"] = lightmapSettings.maxBounces;
            lightmaps["denoise"] = lightmapSettings.denoise;
            lightmaps["fireflyClamp"] = lightmapSettings.fireflyClamp;
            lightmaps["indirectIntensity"] = lightmapSettings.indirectIntensity;
            if(!lightmapData.empty())
                lightmaps["data"] = lightmapData;
        }
    }

    namespace {
        // Keeps a world name usable as part of a file name on every platform.
        std::string SanitizeForFileName(const std::string& name)
        {
            std::string out;
            out.reserve(name.size());
            for (unsigned char c : name)
                out.push_back((std::isalnum(c) || c == '-' || c == '_') ? (char)c : '_');
            return out.empty() ? std::string("unnamed") : out;
        }
    }

    bool RenderWorldData::BakeLightmaps(Worlds::World& world)
    {
        if(!world.IsLoaded()){
            DEBUG_WARNING("Lightmaps : only a loaded world can be baked.");
            return false;
        }

        Core::IEngineContext* engine = &Core::GetEngine();
        const Filesystem::Path resRoot = engine->GetFileManager()->GetProjectResRoot();

        // Re-bake : over the world's own file. First bake : a new file in the project's "lightmaps" folder.
        std::string relative = lightmapData;
        if(relative.empty())
            relative = "lightmaps/" + SanitizeForFileName(world.GetName()) + kLightmapExtension;

        IRenderContext* renderer = engine->GetRenderContext();
        return renderer->GetLightmapManager()->BeginBake(renderer->GetSnapshot(), lightmapSettings, resRoot / relative);
    }

    void RenderWorldData::OnLightmapsBaked(Worlds::World& world, const LightmapBakeResult& result)
    {
        if(!result.success)
            return;

        Core::IEngineContext* engine = &Core::GetEngine();
        Filesystem::FileManager* fileManager = engine->GetFileManager();

        const std::string nameInProject = fileManager->GetFileInfos(result.file).nameInProject;
        if(nameInProject.empty()){
            DEBUG_ERROR("Lightmaps : baked to '" + result.file.full + "', which is outside the project's resources - the world cannot reference it.");
            return;
        }

        // The asset database is otherwise only read at project load, so without this the file has no ID and the next world
        // load couldn't resolve its path
        fileManager->RegisterAsset(result.file);

        // Where every model's lightmap is : the models of the bake that got one are given it, the others (static, but with
        // nothing to light) and those that left the bake lose the one they had
        std::unordered_map<uint64_t, const LightmapPlacement*> placements;
        for(const LightmapPlacement& placement : result.placements)
            placements[placement.key] = &placement;

        Worlds::WorldManager* worlds = engine->GetWorldManager();
        for(auto& [id, model] : models){
            if(!model)
                continue;

            auto it = placements.find(RenderKeyOf(*worlds, &world, id));
            if(it != placements.end() && model->staticLighting){
                model->lightmapPage = int(it->second->page);
                model->lightmapScaleOffset = it->second->ScaleOffset(result.pageSize);
            }
            else{
                model->ClearLightmap();
            }
        }

        // A previous bake of this world may still be cached (CPU side) from an earlier load
        engine->GetResourcesManager()->Unload(Core::Resources::AssetKind::Lightmap, nameInProject);

        lightmapData = nameInProject;
        lightmapSettings = result.settings;

        // The pages just baked are the ones the renderer uses : handed over as they are, no reading the file back
        engine->GetRenderContext()->GetLightmapManager()->ProvideData(nameInProject, result.data);

        // Written straight away rather than at project shutdown, so a crash between here and then can't leave a saved world
        // pointing at a file the database has never heard of
        if(auto project = engine->GetCurrentProject())
            Serialization::SerializeAssetDataBase(project->GetAssetDatabasePath(), *engine->GetAssetIDManager());

        world.SetDirty(true);
        DEBUG_INFO("Lightmaps : baked to " + nameInProject + " - save the world to keep them.");
    }

    void RenderWorldData::ClearLightmaps(Worlds::World& world)
    {
        Core::IEngineContext* engine = &Core::GetEngine();

        if(!lightmapData.empty()){
            Filesystem::AssetIDManager* assetManager = engine->GetAssetIDManager();

            std::error_code ec;
            std::filesystem::remove((engine->GetFileManager()->GetProjectResRoot() / lightmapData).full, ec);

            Filesystem::AssetID id = assetManager->GetIDFromNameInProject(lightmapData);
            std::shared_ptr<Filesystem::AssetInfos> info = assetManager->GetAssetFromID(id);
            if(info && info->baseInfos.nameInProject == lightmapData){
                assetManager->DestroyID(id);

                if(auto project = engine->GetCurrentProject())
                    Serialization::SerializeAssetDataBase(project->GetAssetDatabasePath(), *engine->GetAssetIDManager());
            }

            engine->GetResourcesManager()->Unload(Core::Resources::AssetKind::Lightmap, lightmapData);
            lightmapData.clear();
        }

        for(auto& [id, model] : models)
            if(model)
                model->ClearLightmap();

        world.SetDirty(true);
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
        registry.RegisterBuiltinComponent<Billboard>("billboard");
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

            if (settings.contains("lightmaps") && settings["lightmaps"].is_object() &&
                settings["lightmaps"].contains("data") && settings["lightmaps"]["data"].is_string() &&
                !settings["lightmaps"]["data"].get<std::string>().empty())
                out.push_back({AssetKind::Lightmap, settings["lightmaps"]["data"].get<std::string>()});
        });
    }
}
