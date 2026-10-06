#pragma once

#include <memory>

#include "engine/core/color.hpp"

namespace Shard::Engine::Rendering {

    class StorageBuffer;

    enum LightType : uint32_t{
        Directional = 0,
        Point = 1,
        Spot = 2
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

        /// @brief Getter for lights matrices
        /// @return The view-projection matrix from the light's point of view
        glm::mat4 GetLightMatrix(const glm::mat4 &cameraView = glm::mat4(1.0f), const float fov = -1.0f, const float aspectRatio = -1.0f, const float cascadeNear = -1.0f, const float cascadeFar = -1.0f, const float dirShadowRes = -1.0, const float outerCutoff = -1.0);
    };

    class LightManager {
        public:

            static constexpr uint32_t MAX_LIGHTS = 128;

            static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 3;

            LightManager();

            ~LightManager();

            /// @note Only call this AFTER modifying the light data
            /// @brief Update the light system's storage buffer, and shadow maps
            /// @param index The global index (world-relative) of the modified light
            void Update(int index);

            /// @brief Add a light to the renderer
            /// @param index The global index (world-relative) of the light
            /// @param light The light's parameters
            void AddLight(int lightIndex, std::shared_ptr<LightData> data);

            /// @brief Remove all lights from the renderer (and their associated shadow maps)
            void Clear();

            /// @brief Remove light from renderer (and its associated shadow map)
            /// @param lightIndex The global (world-relative) light index to remove
            void RemoveLight(int lightIndex);

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
            std::vector<std::shared_ptr<LightData>> m_Lights;
            std::shared_ptr<StorageBuffer> m_SSBO;
    };
}