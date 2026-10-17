#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/core/color.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {

    class StorageBuffer;

    enum LightType : uint8_t{
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    /// What a light does for baked lighting (see the lightmaps, LightmapManager) :
    ///  - Dynamic    lit in real time only, never part of a lightmap.
    ///  - Stationary lit in real time, and its bounced (indirect) light is baked into the lightmaps on top.
    ///  - Static     baked entirely (direct and indirect) into the lightmaps : a surface with a lightmap no longer lights
    ///               itself with this light in real time, the others still do.
    enum LightBakeMode : uint8_t{
        Dynamic = 0,
        Stationary = 1,
        Static = 2
    };

    struct LightData {
        glm::vec4 position = glm::vec4(0);   // xyz + padding
        glm::vec4 direction = glm::vec4(0);  // xyz + padding
        glm::vec4 color = glm::vec4(0);      // rgb + padding

        float intensity = 0.0f;

        // max distance at which geometry is lit by this light
        float radius = 0.0f;

        // in radians
        float innerCutoff = 0.0f;

        // in radians
        float outerCutoff = 0.0f;

        int type = 0;
        int castShadow = 0;

        // Persistent slot into the shadow-map arrays / cube array layer (see ShadowManager), -1 if
        // none assigned. Must be used instead of scan-order position when looking up this light's
        // shadow map - see ShadowManager::BindShadowMaps / lit.frag.
        int shadowIndex = -1;

        int padding = 0; // align to 16

        // Size of the emitter, for the soft shadows of the offline renderers (the offline path tracer and the lightmap
        // baker : the forward pass ignores it). Point / spot : radius of the emitting sphere in world units. Directional :
        // angular radius of the sun disc in degrees. 0 : an ideal point / direction, hard shadows.
        float sourceSize = 0.0f;

        // LightBakeMode
        int bakeMode = 0;

        int padding2 = 0;
        int padding3 = 0; // keeps the struct a multiple of 16 bytes, the std430 array stride of its GLSL twin (light.glsl)

        /// @brief Getter for lights matrices
        /// @return The view-projection matrix from the light's point of view
        glm::mat4 GetLightMatrix(const glm::mat4 &cameraView = glm::mat4(1.0f), const float fov = -1.0f, const float aspectRatio = -1.0f, const float cascadeNear = -1.0f, const float cascadeFar = -1.0f, const float dirShadowRes = -1.0, const float outerCutoff = -1.0, const bool stabilizeCascade = true, const float casterRange = 500.0f);
    };

    // The lights SSBO is the raw memory of std::vector<LightData>, indexed by the GLSL struct of the same name
    static_assert(sizeof(LightData) % 16 == 0, "LightData must match the std430 array stride of its GLSL twin (light.glsl)");
    static_assert(sizeof(LightData) == 96, "LightData and light.glsl's Light have to change together");

    class LightManager {
        public:

            static constexpr uint32_t MAX_LIGHTS = 128;

            static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 3;

            LightManager();

            ~LightManager();

            /// @brief Brings the lights the renderer lights the scene with in line with the snapshot's : adds the new ones,
            /// updates the ones that changed, drops the ones that are gone (with their shadow maps), and uploads the storage
            /// buffer if anything changed. A light is identified by its proxy key, its index is its position in the (sorted)
            /// proxy list.
            void Sync(const std::vector<LightProxy>& proxies);

            /// @brief Remove all lights from the renderer (and their associated shadow maps)
            void Clear();

            /// @brief Getter for lights count
            /// @return The total number of lights in the renderer
            int GetLightsCount();

            /// @brief Read-only access to the current light list (e.g. for the offline raytracer to
            /// snapshot scene lights into its own SSBO, independent from this manager's live buffer)
            const std::vector<std::shared_ptr<LightData>>& GetLights() const { return m_Lights; }

            /// @brief Access to the raw light SSBO, so other systems (e.g. LightCullingManager) can
            /// defensively re-bind it to binding point 0 before a compute dispatch that depends on it -
            /// binding points are global GL state, not scoped per-pipeline (see ProbeManager's own note
            /// on this).
            std::shared_ptr<StorageBuffer> GetSSBO() const { return m_SSBO; }

        private:
            void Upload();

            std::vector<std::shared_ptr<LightData>> m_Lights;
            std::vector<uint64_t> m_Keys;                                  // the proxy key of each of m_Lights
            std::unordered_map<uint64_t, std::shared_ptr<LightData>> m_ByKey;
            std::shared_ptr<StorageBuffer> m_SSBO;
    };
}