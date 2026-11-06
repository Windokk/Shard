#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Projects{

    struct BuildSettings{
        // TODO : Target system


        std::vector<Filesystem::Path> buildIndex;
        

        int GetWorldBuildIndex(const Filesystem::Path worldPath) {

            for(int i = 0; i < static_cast<int>(buildIndex.size()); i++){
                if(buildIndex[i] == worldPath){
                    return i;
                }
            }
            return -1;
        }

        void AddToBuildSettings(Filesystem::Path worldPath){
            
            for(int i = 0; i < static_cast<int>(buildIndex.size()); i++){
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

            indexList.erase(indexList.begin() + oldBuildIndex);

            if (newBuildIndex > oldBuildIndex) {
                --newBuildIndex;
            }

            newBuildIndex = std::clamp(newBuildIndex, 0, static_cast<int>(indexList.size()));

            indexList.insert(indexList.begin() + newBuildIndex, worldPath);
        }
    };

    struct PhysicsSettings{
        // World units per second squared. Default is 1 unit = 1 m. Projects at another scale must rescale it.
        glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f);

        float fixedTimeStep = 1.0f / 60.0f;

        // Caps how much real time one frame feeds the simulation, so a stall does not spiral.
        float maxAccumulatedTime = 0.25f;
    };

    // Resolutions are the side of a square shadow map in texels. Biases are in texels where stated, in world units for point lights.
    struct ShadowSettings{
        bool enabled = true;

        uint32_t directionalResolution = 2048;

        float directionalDistance = 0.0f;

        // The suns' shadow maps share a fixed budget of texture units in lit.frag
        static constexpr uint32_t MaxCascades = 6;

        // Fractions (0..1, ascending) of the near plane to shadow distance range. The last cascade has no entry, so N splits make N + 1 cascades.
        std::vector<float> cascadeSplits = { 0.27f, 0.54f };

        std::vector<glm::vec3> cascadeColors = { glm::vec3(1.0f, 0.2f, 0.2f), glm::vec3(0.2f, 1.0f, 0.2f), glm::vec3(0.3f, 0.4f, 1.0f) };

        uint32_t CascadeCount() const { return uint32_t(cascadeSplits.size()) + 1; }

        void SanitizeCascades()
        {
            if (cascadeSplits.size() > MaxCascades - 1)
                cascadeSplits.resize(MaxCascades - 1);
            const float minGap = 0.001f;
            float previous = 0.0f;
            for (size_t i = 0; i < cascadeSplits.size(); i++)
            {
                const float remaining = float(cascadeSplits.size() - i);
                cascadeSplits[i] = std::clamp(cascadeSplits[i], previous + minGap, 1.0f - minGap * remaining);
                previous = cascadeSplits[i];
            }
            const glm::vec3 fill = cascadeColors.empty() ? glm::vec3(1.0f) : cascadeColors.back();
            cascadeColors.resize(CascadeCount(), fill);
        }

        // Snaps cascades to shadow map texels so shadows do not shimmer when the camera moves
        bool stabilizeCascades = true;

        float casterRange = 500.0f;

        // Levels are rarely closed volumes (a roof is often one surface), and back faces only would leak light through edges. Off is cheaper and can reduce acne on closed meshes.
        bool directionalDoubleSided = true;

        // Closes sub-texel gaps (joints between model pieces). Needs OpenGL with GL_NV_conservative_raster, ignored otherwise.
        bool directionalConservativeRaster = true;

        float directionalNormalBias = 2.0f;

        float directionalDepthBias = 0.75f;

        float directionalSlopeBias = 1.0f;

        uint32_t directionalFilterRadius = 2;

        float directionalFilterSpread = 1.0f;

        uint32_t spotResolution = 1024;

        float spotDepthBias = 0.0005f;

        float spotMinDepthBias = 0.00005f;

        uint32_t spotFilterRadius = 2;

        float spotFilterSpread = 1.0f;

        uint32_t pointResolution = 1024;

        float pointDepthBias = 0.15f;

        uint32_t pointFilterSamples = 20;

        float pointFilterRadius = 0.05f;
    };

    struct RenderingSettings{
        bool occlusionCulling = true;

        uint32_t msaaSamples = 4;

        std::string renderPipeline = "forward+";

        ShadowSettings shadows;
    };

    struct TagSettings{
        std::map<std::string, std::string> tags;

        std::map<std::string, std::string> redirects;
    };

    void ApplyTagSettings(const TagSettings& settings);

    class Project{
        public:

            std::string name;

            int versionMajor;
            int versionMinor;
            int versionPatch;

            Project(std::string name, Filesystem::Path projectRoot,
                    Filesystem::Path projectResourcesRoot,
                    Filesystem::Path pluginsFolder,
                    BuildSettings buildSettings, Filesystem::Path assetDatabasePath);

            void Shutdown(const Filesystem::AssetIDManager& assetIDManager);

            bool Save(const std::string& path);
            void MarkSaved();
            bool HasUnsavedChanges();

            void RemapWorlds(const std::string& oldName, const std::string& newName, bool isDirectory, const std::string& path);

            Filesystem::Path GetProjectResourcesPath() { return projectResourcesRoot; }
            Filesystem::Path GetProjectRoot() { return projectRoot; }
            Filesystem::Path GetAssetDatabasePath() { return assetDatabasePath; }
            Filesystem::Path GetPluginsFolderPath() { return pluginsFolder; }

            // The "plugins" array as the file has it, kept untouched so saving does not lose what the plugin manager reads
            const nlohmann::json& GetPluginsConfig() const { return pluginsConfig; }
            void SetPluginsConfig(nlohmann::json config) { pluginsConfig = std::move(config); }

            BuildSettings* GetBuildSettings() { return &buildSettings; }

            PhysicsSettings* GetPhysicsSettings() { return &physicsSettings; }

            RenderingSettings* GetRenderingSettings() { return &renderingSettings; }

            TagSettings* GetTagSettings() { return &tagSettings; }

        private:
            Filesystem::Path projectRoot = Filesystem::Path("");
            Filesystem::Path projectResourcesRoot = Filesystem::Path("");
            Filesystem::Path pluginsFolder = Filesystem::Path("");
            nlohmann::json pluginsConfig;
            Filesystem::Path assetDatabasePath = Filesystem::Path("");
            BuildSettings buildSettings;
            PhysicsSettings physicsSettings;
            RenderingSettings renderingSettings;
            TagSettings tagSettings;

            std::string savedState;
    };

}