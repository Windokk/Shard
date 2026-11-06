#pragma once

#include <functional>

#include "world.hpp"
#include "world_asset_prefetcher.hpp"

namespace Shard::Engine::Core::Resources{
    class ResourcesManager;
}

namespace Shard::Engine::Worlds{

    void RegisterWorldAssetKind(Core::Resources::ResourcesManager& resources);

    class WorldManager {
        public:

            void LoadWorld(std::shared_ptr<World> world);

            void LoadWorldAsync(const std::string& pathInProject);

            void LoadWorldBlocking(const std::string& pathInProject, const std::function<void(float)>& tickCallback = nullptr);

            void PumpAsyncLoad();

            bool IsAsyncLoadInProgress() const { return asyncLoadPending; }

            float GetAsyncLoadProgress() const { return prefetcher.GetProgress(); }

            World* GetWorldAt(int index);

            void UnloadWorld(int index);
            
            void UnloadLoadedWorlds();

            void UnloadAllWorlds() {
                for(size_t i = 0; i < worldBuffer.size(); i++){
                    worldBuffer[i]->Unload();
                }
                worldBuffer.clear();
            }
    
            void DestroyWorld(std::shared_ptr<World> world){
                world->Unload();
            }
        
            void DestroyAllWorlds() {
                for(size_t i = 0; i < worldBuffer.size(); i++){
                    worldBuffer[i]->Unload();
                }
                worldBuffer.clear();
            }
            
            int GetLoadedWorldCount() {
                return worldBuffer.size();
            }

            void Tick() {
                for(auto& world : worldBuffer){
                    world->Tick();
                }
            }

            void EditTick() {
                for(auto& world : worldBuffer){
                    world->EditTick();
                }
            }

            void FixedTick() {
                for(auto& world : worldBuffer){
                    world->FixedTick();
                }
            }

            void LateTick() {
                for(auto& world : worldBuffer){
                    world->LateTick();
                }
            }

            std::function<void()> onAllWorldsUnloaded;
        
        private:

            void FinishAsyncLoad();

            std::vector<std::shared_ptr<World>> worldBuffer;

            AssetPrefetcher prefetcher;
            bool asyncLoadPending = false;
            std::string asyncLoadPathInProject;
        };
}