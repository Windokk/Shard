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
            world->Deserialize(path);
            return world;
        };
        info.setAssetID = [](void* resource, Filesystem::AssetID id){ static_cast<World*>(resource)->SetAssetID(id); };
        info.ownerType = Filesystem::Type::T_WORLD;
        info.evictable = false;
        info.dependencies = [](const Filesystem::Path& path){
            return CollectWorldAssetRefs(path);
        };
        resources.RegisterKind(AssetKind::World, std::move(info));
    }

    void WorldManager::LoadWorld(std::shared_ptr<World> world)
    {
        if(!world){
            DEBUG_FATAL("Cannot load world (because pointer is null)");
            return;
        }
        worldBuffer.push_back(world);
        world->SetLoaded(true);

        int worldAssetID = world->ResolveAssetID();

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

        // Always give the caller one tick, even if the load ends on the first Pump(). The editor inits its viewport camera from this callback before the first frame renders.
        bool loadComplete = false;
        do {
            loadComplete = prefetcher.Pump(-1.0f);
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

    void WorldManager::UnloadLoadedWorlds()
    {
        if(worldBuffer.empty())
            return;

        auto& engine = Core::GetEngine();
        auto* assetIDManager = engine.GetAssetIDManager();
        auto* resourcesManager = engine.GetResourcesManager();

        while(!worldBuffer.empty()){
            auto info = assetIDManager->GetAssetFromID(worldBuffer[0]->GetAssetID());
            const std::string nameInProject = info ? info->baseInfos.nameInProject : "";
            UnloadWorld(0);
            if(!nameInProject.empty())
                resourcesManager->Unload(Core::Resources::AssetKind::World, nameInProject);
        }

        if(onAllWorldsUnloaded)
            onAllWorldsUnloaded();

        // Must run before deserializing the new world. Reset() invalidates every ObjectID, so IDs handed out earlier get reused and resolve to the wrong actor.
        engine.GetObjectIDManager()->Reset();
    }

    void WorldManager::FinishAsyncLoad()
    {
        std::string pathInProject = asyncLoadPathInProject;

        auto& engine = Core::GetEngine();
        auto* assetIDManager = engine.GetAssetIDManager();

        if(!assetIDManager->GetAssetFromID(assetIDManager->GetIDFromNameInProject(pathInProject))){
            DEBUG_ERROR("Error loading world : " + pathInProject);
            return;
        }

        UnloadLoadedWorlds();

        auto world = engine.GetResourcesManager()->Get<Worlds::World>(Core::Resources::AssetKind::World, pathInProject);

        if(!world){
            DEBUG_ERROR("Error loading world : " + pathInProject);
            return;
        }

        LoadWorld(world);

        engine.GetResourcesManager()->CollectUnused();
    }

    World* WorldManager::GetWorldAt(int index){
        if (index >= 0 && static_cast<size_t>(index) < worldBuffer.size()) {
            return worldBuffer[index].get();
        } else {
            DEBUG_ERROR("Invalid index (out of bounds). Unable to retrieve world.");
            return nullptr;
        }
    }

    void WorldManager::UnloadWorld(int index){
        if (index >= 0 && static_cast<size_t>(index) < worldBuffer.size()) {
            worldBuffer[index]->Unload();
            worldBuffer.erase(worldBuffer.begin() + index);
        } else {
            DEBUG_ERROR("Invalid index (out of bounds). Unable to unload world.");
        }
    }
}