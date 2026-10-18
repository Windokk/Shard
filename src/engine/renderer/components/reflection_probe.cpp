#include "reflection_probe.hpp"

#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/components/render_extractor.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/renderer/features/raytracing/raytracer.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/world_manager.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/assets/assetID.hpp"
#include "engine/assets/project/project.hpp"
#include "engine/assets/asset_database_serializer.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "reflection_probe.reflection.hpp"

namespace Shard::Engine::Objects::Components{

    namespace {
        // Folder of the project the baked pictures go in
        constexpr const char* kBakeFolder = "reflection_probes/";
        constexpr const char* kBakeExtension = ".hdr";

        // One bake at a time : the raytracer owns the GPU buffers of the scene it traces, and a probe bake is short
        struct ActiveBake
        {
            std::unique_ptr<Rendering::Raytracing::Raytracer> tracer;
            uint64_t key = 0;
            Filesystem::Path file;
        };
        std::unique_ptr<ActiveBake> g_Bake;

        // Keeps a world/actor name usable as part of a file name on every platform.
        std::string SanitizeForFileName(const std::string& name)
        {
            std::string out;
            out.reserve(name.size());
            for (unsigned char c : name)
                out.push_back((std::isalnum(c) || c == '-' || c == '_') ? (char)c : '_');
            return out.empty() ? std::string("unnamed") : out;
        }
    }

    ReflectionProbe::ReflectionProbe(std::shared_ptr<Actor> parent, uint32_t local_id) : Volume(parent, local_id)
    {
        // A probe's box is the part of the world it stands for : a room, not Volume's generic 1x1x1 default
        halfExtent = glm::vec3(5.0f, 3.0f, 5.0f);
    }

    uint64_t ReflectionProbe::GetRenderKey() const
    {
        if(!parent || !parent->world)
            return 0;

        return Rendering::RenderKeyOf(*GetEngineContext()->GetWorldManager(), parent->world, parent->GetComponentIDInWorld(local_id));
    }

    void ReflectionProbe::Deserialize(const json componentData)
    {
        // The fields are only loaded, not applied : the picture is loaded by the engine's update (see TickAll)
        DeserializeReflectedFields(componentData, false);

        if (componentData.contains("bakedData") && componentData["bakedData"].is_string())
            bakedData = componentData["bakedData"].get<std::string>();
        else
            bakedData.clear();

        DeserializeActive(componentData, false);
    }

    ordered_json ReflectionProbe::Serialize()
    {
        ordered_json comp = SerializeReflected("reflectionProbe");

        if (!bakedData.empty())
            comp["bakedData"] = bakedData;

        return comp;
    }

    std::shared_ptr<Component> ReflectionProbe::Clone() const
    {
        std::shared_ptr<ReflectionProbe> clone = Object::Create<ReflectionProbe>(*this);

        // A copy is a different probe : if it kept the reference, baking either of the two would overwrite the file the other one reflects.
        clone->bakedData.clear();
        clone->m_Environment.reset();
        clone->m_EnvironmentPath.clear();

        return clone;
    }

    void ReflectionProbe::RefreshEnvironment()
    {
        if (bakedData == m_EnvironmentPath)
            return;

        // Remembered even when the load fails : a missing file is logged once, not at every update
        m_EnvironmentPath = bakedData;
        m_Environment.reset();

        if (!bakedData.empty())
            m_Environment = GetEngineContext()->GetResourcesManager()->Get<Rendering::EnvironmentMap>(Core::Resources::AssetKind::EnvMap, bakedData);
    }

    bool ReflectionProbe::Bake()
    {
        if (!activated || !parent || !parent->world || !parent->world->IsLoaded() || !parent->transform)
        {
            DEBUG_WARNING("ReflectionProbe : only an active probe in a loaded world can be baked.");
            return false;
        }

        if (g_Bake)
        {
            DEBUG_WARNING("ReflectionProbe : another probe is being baked, wait for it to finish.");
            return false;
        }

        Core::IEngineContext* engine = GetEngineContext();
        const Rendering::RenderSnapshot& snapshot = engine->GetRenderContext()->GetSnapshot();
        if (snapshot.meshes.empty())
        {
            DEBUG_WARNING("ReflectionProbe : the world has nothing to capture yet.");
            return false;
        }

        const Filesystem::Path resRoot = engine->GetFileManager()->GetProjectResRoot();

        // Re-bake : over this probe's own file. First bake : a new file in the project's folder. Its name only has to be unique NOW - once
        // written, the world file stores the full path, so it doesn't matter that actor names (or the world's) can change later.
        std::string relative = bakedData;
        if (relative.empty())
        {
            const std::string stem = kBakeFolder + SanitizeForFileName(parent->world->GetName()) + "_" + SanitizeForFileName(parent->GetName());
            relative = stem + kBakeExtension;

            for (int n = 2; (resRoot / relative).Exists(); n++)
                relative = stem + "_" + std::to_string(n) + kBakeExtension;
        }

        const Filesystem::Path file = resRoot / relative;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(file.full).parent_path(), ec);

        Rendering::Raytracing::RaytraceSettings settings;
        settings.panoramic = true;
        settings.width = static_cast<uint32_t>(std::max(bakeResolution, 16) & ~1);
        settings.height = settings.width / 2;
        settings.samplesPerPixel = static_cast<uint32_t>(std::max(bakeSamples, 1));
        settings.maxBounces = static_cast<uint32_t>(std::max(bakeBounces, 1));
        settings.useLevelSky = true;
        // A rare very bright path would be a white speck that averaging never removes, and the roughness filter spreads it over a whole lobe
        settings.fireflyClamp = 100.0f;
        settings.writePreview = false;

        // Only the position matters for a panorama
        Rendering::CameraProxy camera;
        camera.valid = true;
        camera.position = parent->transform->GetWorldPosition();

        auto tracer = std::make_unique<Rendering::Raytracing::Raytracer>();
        if (!tracer->Start(snapshot, camera, settings, file))
        {
            DEBUG_ERROR("ReflectionProbe : " + tracer->GetStatusMessage());
            return false;
        }

        g_Bake = std::make_unique<ActiveBake>();
        g_Bake->tracer = std::move(tracer);
        g_Bake->key = GetRenderKey();
        g_Bake->file = file;
        return true;
    }

    float ReflectionProbe::GetBakeProgress() const
    {
        if (!g_Bake || g_Bake->key != GetRenderKey())
            return -1.0f;

        return g_Bake->tracer->IsPreparing() ? 0.0f : g_Bake->tracer->GetProgress();
    }

    void ReflectionProbe::CancelBake()
    {
        if (!g_Bake || g_Bake->key != GetRenderKey())
            return;

        g_Bake->tracer->Cancel();
        g_Bake.reset();
    }

    namespace {
        // Called on the main thread when the bake of `probe` has written `file`
        void OnBakeWritten(ReflectionProbe& probe, const Filesystem::Path& file)
        {
            Core::IEngineContext* engine = &Core::GetEngine();
            Filesystem::FileManager* fileManager = engine->GetFileManager();

            const std::string nameInProject = fileManager->GetFileInfos(file).nameInProject;
            if (nameInProject.empty())
            {
                DEBUG_ERROR("ReflectionProbe : baked to '" + file.full + "', which is outside the project's resources - the world cannot reference it.");
                return;
            }

            // The asset database is otherwise only read at project load, so without this the file has no ID and the next world load couldn't resolve its path
            fileManager->RegisterAsset(file);

            // A previous bake of this probe may still be cached : it is the old picture
            engine->GetResourcesManager()->Unload(Core::Resources::AssetKind::EnvMap, nameInProject);
            probe.bakedData = nameInProject;
            probe.RefreshEnvironment();

            // Written straight away rather than at project shutdown, so a crash between here and then can't leave a saved world pointing at a
            // file the database has never heard of
            if (auto project = engine->GetCurrentProject())
                Serialization::SerializeAssetDataBase(project->GetAssetDatabasePath(), *engine->GetAssetIDManager());

            if (probe.parent && probe.parent->world)
                probe.parent->world->SetDirty(true);

            DEBUG_INFO("ReflectionProbe : baked to " + nameInProject + " - save the world to keep the reference.");
        }
    }

    void ReflectionProbe::TickAll()
    {
        Core::IEngineContext* engine = &Core::GetEngine();
        Worlds::WorldManager* worlds = engine->GetWorldManager();
        if (!worlds)
            return;

        for (int w = 0; w < worlds->GetLoadedWorldCount(); w++)
        {
            Worlds::World* world = worlds->GetWorldAt(w);
            if (!world)
                continue;

            for (auto& [id, probe] : world->Ext<Rendering::RenderWorldData>().reflectionProbes)
            {
                if (probe)
                    probe->RefreshEnvironment();
            }
        }

        if (!g_Bake)
            return;

        g_Bake->tracer->Update();
        if (g_Bake->tracer->IsActive())
            return;

        std::unique_ptr<ActiveBake> bake = std::move(g_Bake);
        if (bake->tracer->GetStatus() != Rendering::Raytracing::RaytraceStatus::Completed)
        {
            DEBUG_ERROR("ReflectionProbe : the bake failed : " + bake->tracer->GetStatusMessage());
            return;
        }

        if (ReflectionProbe* probe = Rendering::FindReflectionProbe(*worlds, bake->key))
            OnBakeWritten(*probe, bake->file);
        else
            DEBUG_WARNING("ReflectionProbe : the probe was removed during its bake, the picture was not kept.");
    }

    void ReflectionProbe::ClearBake()
    {
        CancelBake();

        Core::IEngineContext* engine = GetEngineContext();

        if (!bakedData.empty())
        {
            Filesystem::AssetIDManager* assetManager = engine->GetAssetIDManager();

            std::error_code ec;
            std::filesystem::remove((engine->GetFileManager()->GetProjectResRoot() / bakedData).full, ec);

            Filesystem::AssetID id = assetManager->GetIDFromNameInProject(bakedData);
            std::shared_ptr<Filesystem::AssetInfos> info = assetManager->GetAssetFromID(id);
            if (info && info->baseInfos.nameInProject == bakedData)
            {
                assetManager->DestroyID(id);

                if (auto project = engine->GetCurrentProject())
                    Serialization::SerializeAssetDataBase(project->GetAssetDatabasePath(), *engine->GetAssetIDManager());
            }

            engine->GetResourcesManager()->Unload(Core::Resources::AssetKind::EnvMap, bakedData);
            bakedData.clear();
        }

        m_Environment.reset();
        m_EnvironmentPath.clear();

        if (parent && parent->world)
            parent->world->SetDirty(true);
    }

}
