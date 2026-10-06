#include "engine/world/world_manager.hpp"

#include "engine/core/diagnostics/logger.hpp"
#include "world_manager.hpp"

#include "engine/world/engine.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/world/objectID.hpp"
#include "engine/world/engine.hpp"

#include "engine/assets/project/project.hpp"

namespace Shard::Engine::Worlds{

    void RegisterWorldAssetKind(Core::Resources::ResourcesManager& resources)
    {
        using namespace Core::Resources;

        AssetKindInfo info;
        info.load = [](const std::string& pathInProject, const Filesystem::AssetInfos& infos) -> std::shared_ptr<void> {
            const Filesystem::Path& path = infos.baseInfos.path;
            std::shared_ptr<World> world = std::make_shared<World>("unitialized_world", path);
            int buildIndex = Core::GetEngine().GetBuildSettings()->GetWorldBuildIndex(pathInProject);
            if(buildIndex != -1){
                world->SetBuildIndex(buildIndex);
            }
            world->Deserialize(path);
            return world;
        };
        info.setAssetID = [](void* resource, Filesystem::AssetID id){ static_cast<World*>(resource)->SetAssetID(id); };
        info.ownerType = Filesystem::Type::T_WORLD;
        info.evictable = false;
        info.dependencies = [](const Filesystem::Path& path){
            // What a world references is known by the modules that own the components and the settings
            return CollectWorldAssetRefs(path);
        };
        resources.RegisterKind(AssetKind::World, std::move(info));
    }

    void WorldManager::LoadWorld(std::shared_ptr<World> world)
    {
        if(!world)
        DEBUG_FATAL("Cannot load world (because pointer is null)");
        worldBuffer.push_back(world);
        world->SetLoaded(true);

        int worldBuildIndex = world->GetBuildIndex();
        int worldAssetID = Core::GetEngine().GetAssetIDManager()->GetIDFromNameInProject(Core::GetEngine().GetBuildSettings()->buildIndex[worldBuildIndex].full).GetAsInt();

        Core::GetEngine().GetEventDispatcher()->emitGlobal(Events::WorldStructureChangedEvent(
                                                    worldAssetID, Events::LOADED, "", Core::ObjectID(-1)));

        world->OnLoad();
    }

    void WorldManager::LoadWorldAsync(const std::string &pathInProject)
    {
        if(asyncLoadPending){
            DEBUG_WARNING("World load already in progress, ignoring request to load : " + pathInProject);
            return;
        }

        asyncLoadPathInProject = pathInProject;
        asyncLoadPending = true;
        prefetcher.BeginLoad(pathInProject);
    }

    void WorldManager::LoadWorldBlocking(const std::string &pathInProject, const std::function<void(float)> &tickCallback)
    {
        if(asyncLoadPending){
            DEBUG_WARNING("World load already in progress, ignoring request to load : " + pathInProject);
            return;
        }

        asyncLoadPathInProject = pathInProject;
        asyncLoadPending = true;
        prefetcher.BeginLoad(pathInProject);

        // Always hand the caller at least one tick, even when the load finishes on the first
        // Pump() (an empty world does). The editor drives its GUI init - including the viewport
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

    void WorldManager::PumpAsyncLoad()
    {
        if(!asyncLoadPending)
            return;

        if(!prefetcher.Pump())
            return;

        asyncLoadPending = false;
        FinishAsyncLoad();
    }

    void WorldManager::FinishAsyncLoad()
    {
        std::string pathInProject = asyncLoadPathInProject;

        auto& engine = Core::GetEngine();
        auto* assetIDManager = engine.GetAssetIDManager();

        // Cheap, side-effect-free existence check first (no actor/ID allocation) so a bad/missing
        // target can't leave the editor with zero worlds loaded - without actually deserializing
        // the new world yet (see below for why that has to wait).
        if(!assetIDManager->GetAssetFromID(assetIDManager->GetIDFromNameInProject(pathInProject))){
            DEBUG_ERROR("Error loading world : " + pathInProject);
            return;
        }

        if(!worldBuffer.empty()){
            auto* resourcesManager = engine.GetResourcesManager();

            while(!worldBuffer.empty()){
                std::string nameInProject = assetIDManager->GetAssetFromID(worldBuffer[0]->GetAssetID())->baseInfos.nameInProject;
                UnloadWorld(0);
                resourcesManager->Unload(Core::Resources::AssetKind::World, nameInProject);
            }

            if(onAllWorldsUnloaded)
                onAllWorldsUnloaded();

            // Must happen before the new world is deserialized below: Reset() invalidates every
            // ObjectID and restarts the counter from 1. Deserializing first would hand the new
            // world's actors IDs that Reset() then wipes out from the manager (while the objects
            // themselves stay alive via the world's own containers), so the *next* world loaded
            // reuses those same low IDs - GetObjectFromID() then resolves them to the wrong actor
            // and RemoveActorRecursive() ends up tearing down the wrong world's tree entirely.
            engine.GetObjectIDManager()->Reset();
        }

        auto world = engine.GetResourcesManager()->Get<Worlds::World>(Core::Resources::AssetKind::World, pathInProject);

        if(!world){
            DEBUG_ERROR("Error loading world : " + pathInProject);
            return;
        }

        LoadWorld(world);

        // Only now, with the new world's dependencies retained: whatever the old world held that the new
        // one doesn't need is evicted, and what they share is never unloaded/reloaded.
        engine.GetResourcesManager()->CollectUnused();
    }

    World* WorldManager::GetWorldAt(int index){
        if (index >= 0 && index < worldBuffer.size()) {
            return worldBuffer[index].get();
        } else {
            DEBUG_ERROR("Invalid index (out of bounds). Unable to retrieve world.");
            return nullptr;
        }
    }

    void WorldManager::UnloadWorld(int index){
        if (index >= 0 && index < worldBuffer.size()) {
            worldBuffer[index]->Unload();
            worldBuffer.erase(worldBuffer.begin() + index);
        } else {
            DEBUG_ERROR("Invalid index (out of bounds). Unable to unload world.");
        }
    }
}