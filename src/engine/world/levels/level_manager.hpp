#pragma once

#include <functional>

#include "level.hpp"
#include "level_asset_prefetcher.hpp"

namespace Shard::Engine::Levels{

    class LevelManager {
        public:

            /// @brief Load a level
            /// @param lvl The pointer to the level
            void LoadLevel(std::shared_ptr<Level> lvl);

            /// @brief Starts loading `pathInProject` in the background: textures/meshes it (and its
            /// materials) reference are decoded in parallel on worker threads, then uploaded and the
            /// level deserialized once ready. Call PumpAsyncLoad() once per frame to advance it and
            /// GetAsyncLoadProgress()/IsAsyncLoadInProgress() to show a loading screen. No-op with a
            /// warning if a load is already in progress. Does NOT unload any currently loaded level -
            /// callers that are switching levels must unload the old one first.
            void LoadLevelAsync(const std::string& pathInProject);

            /// @brief Same as LoadLevelAsync, but blocks until the level is fully loaded, calling
            /// `tickCallback` (with the current progress) on every wait iteration - e.g. to keep
            /// pumping window events / drawing a splash frame so the app doesn't appear frozen.
            void LoadLevelBlocking(const std::string& pathInProject, const std::function<void(float)>& tickCallback = nullptr);

            /// @brief Advances an in-progress async load (started by LoadLevelAsync). Call once per
            /// frame. No-op if no async load is in progress.
            void PumpAsyncLoad();

            bool IsAsyncLoadInProgress() const { return asyncLoadPending; }

            /// @brief 0-1 (meaningless when IsAsyncLoadInProgress() is false)
            float GetAsyncLoadProgress() const { return prefetcher.GetProgress(); }

            /// @brief Getter for a loaded level
            /// @param index The index of the level to retrieve
            /// @return A pointer to the level loaded at "index"
            Level* GetLevelAt(int index);

            /// @brief Unload a loaded level
            /// @param index The index of the level to unload
            void UnloadLevel(int index);
            
            /// @brief Unload all loaded levels
            void UnloadAllLevels() {
                for(int i = 0; i < levelBuffer.size(); i++){
                    levelBuffer[i]->Unload();
                }
                levelBuffer.clear();
            }
    
            /// @brief Destroys a level
            void DestroyLevel(std::shared_ptr<Level> level){
                level->Unload();
            }
        
            /// @brief Destroys all levels
            void DestroyAllLevels() {
                for(int i = 0; i < levelBuffer.size(); i++){
                    levelBuffer[i]->Unload();
                }
                levelBuffer.clear();
            }
            
            /// @brief Getter for the total number loaded level
            /// @return The length of the level buffer
            int GetLoadedLevelCount() {
                return levelBuffer.size();
            }

            void Tick() {
                for(auto& level : levelBuffer){
                    level->Tick();
                }
            }
        
        private:

            void FinishAsyncLoad();

            std::vector<std::shared_ptr<Level>> levelBuffer;  // Buffer to store levels

            AssetPrefetcher prefetcher;
            bool asyncLoadPending = false;
            std::string asyncLoadPathInProject;
        };
}