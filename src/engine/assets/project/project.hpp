#pragma once

#include <string>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Projects{

    struct BuildSettings{
        // TODO : Target system


        /// @brief A list of world paths to include in the build
        std::vector<Filesystem::Path> buildIndex;
        

        int GetWorldBuildIndex(const Filesystem::Path worldPath) {

            for(int i = 0; i < buildIndex.size(); i++){
                if(buildIndex[i] == worldPath){
                    return i;
                }
            }
            return -1; // Not found
        }

        void AddToBuildSettings(Filesystem::Path worldPath){
            
            for(int i = 0; i < buildIndex.size(); i++){
                if(buildIndex[i] == worldPath){
                    DEBUG_INFO("World already added to build settings, not re-adding it");
                    return;
                }
            }

            buildIndex.push_back(worldPath);
        }

        void ChangeBuildIndex(Filesystem::Path worldPath, int newBuildIndex) {
            int oldBuildIndex = GetWorldBuildIndex(worldPath);
            if (oldBuildIndex == -1) {
                DEBUG_ERROR("Tried to change world's build index, but world is not yet registered in the build settings");
                return;
            }

            auto& indexList = buildIndex;

            // Erase the world from its old pos
            indexList.erase(indexList.begin() + oldBuildIndex);

            if (newBuildIndex > oldBuildIndex) {
                --newBuildIndex;
            }

            newBuildIndex = std::clamp(newBuildIndex, 0, static_cast<int>(indexList.size()));

            // Insert the world at the new position
            indexList.insert(indexList.begin() + newBuildIndex, worldPath);
        }
    };

    struct EditorPreferences{
        // TODO
    };

    struct PhysicsSettings{
        /// Acceleration applied to every dynamic body, in world units per second squared. The default
        /// is earth gravity expressed for a project where one unit is one metre - a project authored
        /// at another scale has to scale this to match, otherwise everything appears to fall in slow
        /// motion (units too small) or far too fast (units too large).
        glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);

        /// How much time one simulation step covers. Lower is more accurate and smoother, at the cost
        /// of more steps per second. It doesn't change how fast the simulation runs: the engine runs
        /// as many steps per frame as the elapsed real time paid for.
        float fixedTimeStep = 1.0f / 60.0f;

        /// Upper bound on the real time a single frame may contribute to the simulation. Caps how far
        /// the simulation tries to catch up after a stall instead of falling further behind each frame.
        float maxAccumulatedTime = 0.25f;
    };

    class Project{
        public:

            std::string name;

            int versionMajor;
            int versionMinor;
            int versionPatch;

            Project(std::string name, Filesystem::Path projectRoot,
                    Filesystem::Path projectResourcesRoot,
                    Filesystem::Path pluginsFolder,
                    BuildSettings buildSettings,
                    EditorPreferences editorPreferences, Filesystem::Path assetDatabasePath);

            void Shutdown(std::string path, const Filesystem::AssetIDManager& assetIDManager);

            Filesystem::Path GetProjectResourcesPath() { return projectResourcesRoot; }
            Filesystem::Path GetProjectRoot() { return projectRoot; }
            Filesystem::Path GetAssetDatabasePath() { return assetDatabasePath; }
            Filesystem::Path GetPluginsFolderPath() { return pluginsFolder; }

            BuildSettings* GetBuildSettings() { return &buildSettings; }

            PhysicsSettings* GetPhysicsSettings() { return &physicsSettings; }

            EditorPreferences GetEditorPrefs() { return editorPreferences; }

        private:
            Filesystem::Path projectRoot = Filesystem::Path("");
            Filesystem::Path projectResourcesRoot = Filesystem::Path("");
            Filesystem::Path pluginsFolder = Filesystem::Path("");
            Filesystem::Path assetDatabasePath = Filesystem::Path("");
            BuildSettings buildSettings;
            PhysicsSettings physicsSettings;
            EditorPreferences editorPreferences;
    };

}