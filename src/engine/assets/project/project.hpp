#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/core/diagnostics/logger.hpp"

namespace Shard::Engine::Projects{

    struct BuildSettings{
        // TODO : Target system


        /// @brief A list of world paths to include in the build
        std::vector<Filesystem::Path> buildIndex;
        

        int GetWorldBuildIndex(const Filesystem::Path worldPath) {

            for(int i = 0; i < static_cast<int>(buildIndex.size()); i++){
                if(buildIndex[i] == worldPath){
                    return i;
                }
            }
            return -1; // Not found
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

    /// How the lights' shadows are made. Resolutions are the side of a square shadow map, in texels. The biases are given in texels of the
    /// shadow map where they say so (they follow the resolution), in world units for point lights.
    struct ShadowSettings{
        /// Draws any shadow at all. Off : every light is unshadowed (the shadow maps are still rendered)
        bool enabled = true;

        // ---- Directional lights (cascaded shadow maps : 1 to MaxCascades cascades per light) ----

        /// Side of each cascade's map
        uint32_t directionalResolution = 2048;

        /// How far from the camera the sun's shadows reach, in world units (0 : up to the camera's far plane). The cascades are fitted to
        /// this range : a shorter one gives sharper shadows near the camera.
        float directionalDistance = 0.0f;

        /// Most cascades a light can have : the shadow maps of the suns share a fixed budget of texture units in lit.frag
        static constexpr uint32_t MaxCascades = 6;

        /// Where each cascade ends, as a fraction (0..1, ascending) of the range from the camera's near plane to the shadow distance. The last
        /// cascade always ends at the end of the range, so it has no entry : N splits make N + 1 cascades (so 1 to MaxCascades).
        std::vector<float> cascadeSplits = { 0.27f, 0.54f };

        /// The color of each cascade in the "Shadow Cascades" debug view (one per cascade : cascadeSplits.size() + 1)
        std::vector<glm::vec3> cascadeColors = { glm::vec3(1.0f, 0.2f, 0.2f), glm::vec3(0.2f, 1.0f, 0.2f), glm::vec3(0.3f, 0.4f, 1.0f) };

        uint32_t CascadeCount() const { return uint32_t(cascadeSplits.size()) + 1; }

        /// Brings the splits and colors back to something usable : 1 to MaxCascades cascades, ascending splits inside (0, 1), one color each
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

        /// Snaps each cascade to the texels of its map, so the shadows do not shimmer when the camera moves (at the cost of a slightly looser fit)
        bool stabilizeCascades = true;

        /// How far from a cascade, towards the light, the shadow casters are still drawn, in world units. A caster further than this
        /// casts no shadow on it (a high ceiling over a close cascade).
        float casterRange = 500.0f;

        /// Draws both faces of the geometry into the sun's shadow maps. Levels are rarely made of closed volumes (a roof or a wall is often
        /// one surface) : drawing only the faces turned away from the sun lets its light through the edges of such surfaces. Turning it off
        /// is cheaper and can reduce acne on closed meshes.
        bool directionalDoubleSided = true;

        /// Draws every texel a caster touches into the sun's shadow maps, not only the ones whose centre it covers. A gap thinner than a
        /// texel (the joint between two pieces of a model) would otherwise let a thin line of light through. Needs OpenGL and a GPU with
        /// GL_NV_conservative_raster ; ignored otherwise.
        bool directionalConservativeRaster = true;

        /// How far the lit point is moved along the surface's normal before it is looked up in the map, in texels (more on a surface that grazes the light)
        float directionalNormalBias = 2.0f;

        /// Constant depth offset of the comparison, in texels
        float directionalDepthBias = 0.75f;

        /// Scale of the receiver plane bias, which follows the slope of the surface across the filter (0 : none, 1 : exact)
        float directionalSlopeBias = 1.0f;

        /// The filter takes (2 * radius + 1)^2 taps : 0 is a single (hardware filtered) tap, 2 the default 5 x 5
        uint32_t directionalFilterRadius = 2;

        /// Distance between two taps of the filter, in texels : wider gives softer edges (and bands when the taps are far apart)
        float directionalFilterSpread = 1.0f;

        // ---- Spot lights ----

        uint32_t spotResolution = 1024;

        /// Depth bias that grows with the surface's slope to the light (in depth units of the map)
        float spotDepthBias = 0.0005f;

        /// Smallest depth bias, on a surface facing the light
        float spotMinDepthBias = 0.00005f;

        /// The filter takes (2 * radius + 1)^2 taps
        uint32_t spotFilterRadius = 2;

        /// Distance between two taps, in texels
        float spotFilterSpread = 1.0f;

        // ---- Point lights ----

        /// Side of each of the six faces of the cube map
        uint32_t pointResolution = 1024;

        /// Depth bias, in world units
        float pointDepthBias = 0.15f;

        /// Taps of the filter (1 to 20), spread on a disc around the lookup direction
        uint32_t pointFilterSamples = 20;

        /// Radius of that disc, in world units at the surface
        float pointFilterRadius = 0.05f;
    };

    struct RenderingSettings{
        /// Lets the GPU skip the draws of geometry that is hidden behind other geometry (Hi-Z occlusion culling,
        /// see OcclusionCullingManager). It never changes the image, only how much is drawn to produce it, so the
        /// only reason to turn it off is to compare, or to rule it out when chasing a rendering problem.
        bool occlusionCulling = true;

        /// Samples per pixel of the multisampled viewport targets (2, 4, 8 or 16 ; the GPU clamps it to what it supports).
        uint32_t msaaSamples = 4;

        /// How the frames are built (see RenderPipeline) : the name of a render pipeline asset, or of a type (`forward+`, `deferred`)
        std::string renderPipeline = "forward+";

        /// How the shadows are made (see ShadowSettings)
        ShadowSettings shadows;
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
                    BuildSettings buildSettings, Filesystem::Path assetDatabasePath);

            /// Writes the asset database. The project file is only written by Save : what was not saved is not kept.
            void Shutdown(const Filesystem::AssetIDManager& assetIDManager);

            /// Writes the project file to `path` ; what is written becomes the saved state. False when the file could not be written.
            bool Save(const std::string& path);
            /// The project as it is now is what its file holds (after loading it, or writing it)
            void MarkSaved();
            /// The project differs from what its file held when it was last loaded or saved
            bool HasUnsavedChanges();

            /// @brief Follows a world (or a folder of worlds, `isDirectory`) renamed, moved or deleted (empty `newName`) on disk :
            /// in the build list, and in the saved project file as well, written to `path` at once (not when `path` is empty).
            /// The file follows the disk like the asset database does, without taking the changes that were not saved along.
            void RemapWorlds(const std::string& oldName, const std::string& newName, bool isDirectory, const std::string& path);

            Filesystem::Path GetProjectResourcesPath() { return projectResourcesRoot; }
            Filesystem::Path GetProjectRoot() { return projectRoot; }
            Filesystem::Path GetAssetDatabasePath() { return assetDatabasePath; }
            Filesystem::Path GetPluginsFolderPath() { return pluginsFolder; }

            BuildSettings* GetBuildSettings() { return &buildSettings; }

            PhysicsSettings* GetPhysicsSettings() { return &physicsSettings; }

            RenderingSettings* GetRenderingSettings() { return &renderingSettings; }

        private:
            Filesystem::Path projectRoot = Filesystem::Path("");
            Filesystem::Path projectResourcesRoot = Filesystem::Path("");
            Filesystem::Path pluginsFolder = Filesystem::Path("");
            Filesystem::Path assetDatabasePath = Filesystem::Path("");
            BuildSettings buildSettings;
            PhysicsSettings physicsSettings;
            RenderingSettings renderingSettings;

            /// The project file as it was last loaded or saved (ProjectToJson, dumped)
            std::string savedState;
    };

}