#include "engine/world/levels/level_manager.hpp"

#include "engine/core/diagnostics/logger.hpp"
#include "level_manager.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/world/objectID.hpp"
#include "engine/renderer/frontend/renderer.hpp"

#include "engine/assets/project/project.hpp"

namespace Shard::Engine::Levels{

    void RegisterLevelAssetKind(Core::Resources::ResourcesManager& resources)
    {
        using namespace Core::Resources;

        AssetKindInfo info;
        info.load = [](const std::string& pathInProject, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> {
            const Filesystem::Path& path = infos.baseInfos.path;
            std::shared_ptr<Level> level = std::make_shared<Level>("unitialized_level", path);
            int buildIndex = Core::GetEngine().GetBuildSettings()->GetLevelBuildIndex(pathInProject);
            if(buildIndex != -1){
                level->SetBuildIndex(buildIndex);
            }
            level->Deserialize(path);
            return level;
        };
        info.setAssetID = [](void* resource, Filesystem::AssetID id){ static_cast<Level*>(resource)->SetAssetID(id); };
        info.ownerType = Filesystem::Type::T_LEVEL;
        info.evictable = false;
        info.dependencies = [](const Filesystem::Path& path){
            std::vector<ResourceKey> keys;
            LevelAssetManifest manifest = CollectLevelAssetRefs(path);
            for(auto& p : manifest.meshPathsInProject) keys.push_back({AssetKind::Mesh, p});
            for(auto& p : manifest.materialPathsInProject) keys.push_back({AssetKind::Material, p});
            for(auto& p : manifest.probeBakePathsInProject) keys.push_back({AssetKind::ProbeBake, p});
            for(auto& p : manifest.soundPathsInProject) keys.push_back({AssetKind::Sound, p});
            if(!manifest.skyboxEnvMapPathInProject.empty())
                keys.push_back({AssetKind::EnvMap, manifest.skyboxEnvMapPathInProject});
            return keys;
        };
        resources.RegisterKind(AssetKind::Level, std::move(info));
    }

    void LevelManager::LoadLevel(std::shared_ptr<Level> lvl)
    {
        if(!lvl)
        DEBUG_FATAL("Cannot load level (because pointer is null)");
        levelBuffer.push_back(lvl);
        lvl->SetLoaded(true);

        int levelBuildIndex = lvl->GetBuildIndex();
        int levelAssetID = Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(Core::GetEngine().GetBuildSettings()->buildIndex[levelBuildIndex].full).GetAsInt();

        Core::GetEngine().GetEventDispatcher()->emitGlobal(Events::LevelStructureChangedEvent(
                                                    levelAssetID, Events::LOADED, "", Core::ObjectID(-1)));

        lvl->OnLoad();
    }

    void LevelManager::LoadLevelAsync(const std::string &pathInProject)
    {
        if(asyncLoadPending){
            DEBUG_WARNING("Level load already in progress, ignoring request to load : " + pathInProject);
            return;
        }

        asyncLoadPathInProject = pathInProject;
        asyncLoadPending = true;
        prefetcher.BeginLoad(pathInProject);
    }

    void LevelManager::LoadLevelBlocking(const std::string &pathInProject, const std::function<void(float)> &tickCallback)
    {
        if(asyncLoadPending){
            DEBUG_WARNING("Level load already in progress, ignoring request to load : " + pathInProject);
            return;
        }

        asyncLoadPathInProject = pathInProject;
        asyncLoadPending = true;
        prefetcher.BeginLoad(pathInProject);

        // Always hand the caller at least one tick, even when the load finishes on the first
        // Pump() (an empty level does). The editor drives its GUI init - including the viewport
        // camera - from this callback, and must get that chance before the first engine frame
        // renders; otherwise Renderer::Render() runs once with no active camera.
        bool loadComplete = false;
        do {
            loadComplete = prefetcher.Pump(-1.0f); // loading screen : no per-frame upload budget
            if(tickCallback)
                tickCallback(prefetcher.GetProgress());
        } while(!loadComplete);

        asyncLoadPending = false;
        FinishAsyncLoad();
    }

    void LevelManager::PumpAsyncLoad()
    {
        if(!asyncLoadPending)
            return;

        if(!prefetcher.Pump())
            return;

        asyncLoadPending = false;
        FinishAsyncLoad();
    }

    void LevelManager::FinishAsyncLoad()
    {
        std::string pathInProject = asyncLoadPathInProject;

        auto& engine = Core::GetEngine();
        auto* assetIDManager = engine.GetAssetIDManager();

        // Cheap, side-effect-free existence check first (no actor/ID allocation) so a bad/missing
        // target can't leave the editor with zero levels loaded - without actually deserializing
        // the new level yet (see below for why that has to wait).
        if(!assetIDManager->GetAssetFromID(assetIDManager->GetIDFromNameInProject(pathInProject))){
            DEBUG_ERROR("Error loading level : " + pathInProject);
            return;
        }

        if(!levelBuffer.empty()){
            auto* resourcesManager = engine.GetResourcesManager();

            while(!levelBuffer.empty()){
                std::string nameInProject = assetIDManager->GetAssetFromID(levelBuffer[0]->GetAssetID())->baseInfos.nameInProject;
                UnloadLevel(0);
                resourcesManager->Unload(Core::Resources::AssetKind::Level, nameInProject);
            }

            engine.GetRenderer()->ClearPassesContent();

            // Must happen before the new level is deserialized below: Reset() invalidates every
            // ObjectID and restarts the counter from 1. Deserializing first would hand the new
            // level's actors IDs that Reset() then wipes out from the manager (while the objects
            // themselves stay alive via the level's own containers), so the *next* level loaded
            // reuses those same low IDs - GetObjectFromID() then resolves them to the wrong actor
            // and RemoveActorRecursive() ends up tearing down the wrong level's tree entirely.
            engine.GetObjectIDManager()->Reset();
        }

        auto level = engine.GetResourcesManager()->Get<Levels::Level>(Core::Resources::AssetKind::Level, pathInProject);

        if(!level){
            DEBUG_ERROR("Error loading level : " + pathInProject);
            return;
        }

        LoadLevel(level);

        // Only now, with the new level's dependencies retained: whatever the old level held that the new
        // one doesn't need is evicted, and what they share is never unloaded/reloaded.
        engine.GetResourcesManager()->CollectUnused();
    }

    Level* LevelManager::GetLevelAt(int index){
        if (index >= 0 && index < levelBuffer.size()) {
            return levelBuffer[index].get();
        } else {
            DEBUG_ERROR("Invalid index (out of bounds). Unable to retrieve level.");
            return nullptr;
        }
    }

    void LevelManager::UnloadLevel(int index){
        if (index >= 0 && index < levelBuffer.size()) {
            levelBuffer[index]->Unload();
            levelBuffer.erase(levelBuffer.begin() + index);
        } else {
            DEBUG_ERROR("Invalid index (out of bounds). Unable to unload level.");
        }
    }
}