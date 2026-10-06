#pragma once

#include <functional>

#include "world.hpp"
#include "world_asset_prefetcher.hpp"

namespace Shard::Engine::Core::Resources{
    class ResourcesManager;
}

namespace Shard::Engine::Worlds{

    /// Registers the world asset kind with the resources manager. Worlds are owned by the WorldManager,
    /// so the cache never sweeps them.
    void RegisterWorldAssetKind(Core::Resources::ResourcesManager& resources);

    class WorldManager {
        public:

            /// @brief Load a world
            /// @param world The pointer to the world
            void LoadWorld(std::shared_ptr<World> world);

            /// @brief Starts loading `pathInProject` in the background: textures/meshes it (and its
            /// materials) reference are decoded in parallel on worker threads, then uploaded and the
            /// world deserialized once ready. Call PumpAsyncLoad() once per frame to advance it and
            /// GetAsyncLoadProgress()/IsAsyncLoadInProgress() to show a loading screen. No-op with a
            /// warning if a load is already in progress. Does NOT unload any currently loaded world -
            /// callers that are switching worlds must unload the old one first.
            void LoadWorldAsync(const std::string& pathInProject);

            /// @brief Same as LoadWorldAsync, but blocks until the world is fully loaded, calling
            /// `tickCallback` (with the current progress) on every wait iteration - e.g. to keep
            /// pumping window events / drawing a splash frame so the app doesn't appear frozen.
            void LoadWorldBlocking(const std::string& pathInProject, const std::function<void(float)>& tickCallback = nullptr);

            /// @brief Advances an in-progress async load (started by LoadWorldAsync). Call once per
            /// frame. No-op if no async load is in progress.
            void PumpAsyncLoad();

            bool IsAsyncLoadInProgress() const { return asyncLoadPending; }

            /// @brief 0-1 (meaningless when IsAsyncLoadInProgress() is false)
            float GetAsyncLoadProgress() const { return prefetcher.GetProgress(); }

            /// @brief Getter for a loaded world
            /// @param index The index of the world to retrieve
            /// @return A pointer to the world loaded at "index"
            World* GetWorldAt(int index);

            /// @brief Unload a loaded world
            /// @param index The index of the world to unload
            void UnloadWorld(int index);
            
            /// @brief Unload all loaded worlds
            void UnloadAllWorlds() {
                for(int i = 0; i < worldBuffer.size(); i++){
                    worldBuffer[i]->Unload();
                }
                worldBuffer.clear();
            }
    
            /// @brief Destroys a world
            void DestroyWorld(std::shared_ptr<World> world){
                world->Unload();
            }
        
            /// @brief Destroys all worlds
            void DestroyAllWorlds() {
                for(int i = 0; i < worldBuffer.size(); i++){
                    worldBuffer[i]->Unload();
                }
                worldBuffer.clear();
            }
            
            /// @brief Getter for the total number loaded world
            /// @return The length of the world buffer
            int GetLoadedWorldCount() {
                return worldBuffer.size();
            }

            void Tick() {
                for(auto& world : worldBuffer){
                    world->Tick();
                }
            }

            /// Called when a world switch has unloaded every world, before the new one is built : whatever the
            /// modules submitted on behalf of the old worlds (the renderer's draw lists...) is dropped there.
            std::function<void()> onAllWorldsUnloaded;
        
        private:

            void FinishAsyncLoad();

            std::vector<std::shared_ptr<World>> worldBuffer;  // Buffer to store worlds

            AssetPrefetcher prefetcher;
            bool asyncLoadPending = false;
            std::string asyncLoadPathInProject;
        };
}