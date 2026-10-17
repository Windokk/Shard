#pragma once

#include "light_manager.hpp"

#include <map>

#include <engine/core/diagnostics/logger.hpp>
#include "engine/assets/project/project.hpp"


namespace Shard::Engine::Rendering {

    class Shader;
    class Material;
    class Mesh;
    class Texture2D;
    class Cubemap;
    class CubemapArray;
    class Framebuffer;
    struct DrawCommand;
    
    // Most cascades one light can have, and the cascade shadow map slots lit.frag has (its dirShadowMaps array) : the suns share them,
    // each taking as many as the project's cascade count, so fewer suns can cast shadows when there are more cascades
    static constexpr uint32_t MAX_CASCADES_PER_LIGHT = Projects::ShadowSettings::MaxCascades;
    static constexpr uint32_t NUM_CASCADES = (LightManager::MAX_DIRECTIONAL_LIGHTS * 3);

    // Mirrors the fixed-size shadow sampler arrays in lit.frag (spotShadowMaps[10], the point cube
    // array's implicit layer cap) - a shadow-casting light beyond these caps gets shadowIndex = -1
    // (see ShadowManager::ReassignShadowIndices) and renders unshadowed rather than reading garbage.
    static constexpr int MAX_SPOT_LIGHTS = 10;
    static constexpr int MAX_POINT_LIGHTS = 10;

    struct ShadowMap {
        LightData light;

        // The live LightData this ShadowMap mirrors (same object LightManager uploads to the SSBO) -
        // kept so ReassignShadowIndices can write shadowIndex back onto it, not just this struct's own
        // `light` copy.
        std::shared_ptr<LightData> lightPtr;

        // Directional and spot
        std::shared_ptr<Framebuffer> framebuffer[MAX_CASCADES_PER_LIGHT] = {nullptr};       // One FBO per cascade (only 1 used for spot)
        
        // Directional (CSM)
        glm::mat4 lightMatrix[MAX_CASCADES_PER_LIGHT];                                      // One matrix per cascade
        float cascadeSplits[MAX_CASCADES_PER_LIGHT];                                        // Only used for directional lights
        uint32_t cascadeCount = 0;                                                      // Cascades this light was built with (directional)
        float splitCameraNear = -1.0f;                                                  // The camera planes the splits were computed for : they follow the camera
        float splitCameraFar = -1.0f;

        // Point 
        std::shared_ptr<Cubemap> depthCubemap;                                          // Single cubemap for point lights
        glm::mat4 shadowMatrices[6];                                                    // 6 face matrices (point lights)
        int cubeArrayLayer = -1;

        int resolution = 0;
        
        std::vector<std::string> passes;
    };

    class ShadowManager {
    public:

        /// @brief Initializer for the shadow manager
        /// @param resolution The resolution to use for shadow maps
        void Init(int pointShadowsResolution, int spotShadowsResolution, int dirShadowsResolution);
        
        /// @brief Changes how the shadows are made (the project's rendering settings). A new resolution or face culling rebuilds the
        /// shadow maps of every light already registered ; the rest (cascade fit, filtering, biases) takes effect on the next frame.
        void Configure(const Projects::ShadowSettings& settings);

        /// @brief The settings in use : lit.frag's filtering and biases come from them
        const Projects::ShadowSettings& GetSettings() const { return m_Settings; }

        /// @brief Register/Update a light for shadow mappings
        /// @param lightIndex The light's global index in the scene
        /// @param light The shared pointer to the light's data
        void RegisterOrUpdateLight(int lightIndex, std::shared_ptr<LightData> light);

        /// @brief Remove a light from shadow mappings
        /// @param lightIndex The light's global index in the scene
        void UnregisterLight(int lightIndex);

        /// @brief Bind the shadow maps to a material
        /// @param material The material to bind shadow maps to
        void BindShadowMaps(std::shared_ptr<Shard::Engine::Rendering::Material> material);

        std::vector<std::string> GetAllShadowPasses() {
            std::vector<std::string> passes;
            for(const auto& sm : m_ShadowMaps){
                passes.insert(passes.end(), sm.second.passes.begin(), sm.second.passes.end());
            }
            return passes;
        }

        bool HasShadowMap(int lightIndex) const { return m_ShadowMaps.find(lightIndex) != m_ShadowMaps.end(); }

        const ShadowMap* GetShadowMapFromLightIndex(int lightIndex) const {
            auto it = m_ShadowMaps.find(lightIndex);
            if (it != m_ShadowMaps.end())
                return &it->second;

            DEBUG_ERROR("Couldn't find shadow map for light : %d", lightIndex);
            return nullptr;
        }

        // DO NOT USE
        void ClearAll();

        int GetShadowMapsCount() const {
            return static_cast<int>(m_ShadowMaps.size());
        }

        int GetMaxShadowMapsCount() const {
            return 128;
        }

        void UpdatePassUniforms();
        
    private:

        void SubmitPasses(int lightIndex);

        /// Where each of the project's cascades ends, in view depth, for a camera range [cameraNear, shadowFar]
        void ComputeCascadeSplits(float* out, float cameraNear, float shadowFar) const;

        /// The far end of the sun's last cascade : the camera's far plane, or the shadow distance when it is shorter
        float GetDirectionalShadowFar(float cameraNear, float cameraFar) const;
        void EnsureCubeArrayCapacity(int requiredPointLights);
        void TryShrinkCubeArray();

        // Replaces the point lights' cube map array with one of `capacity` layers (none when 0), then re-targets every point light at it
        void RecreateCubeArray(int capacity);

        // Points every point light's framebuffer at the current cube map array and its pass at its current layer : run whenever the
        // array is recreated or the layers shift (a light removed), else shadow_point.geom writes a deleted texture or another light's layer
        void SyncPointShadowTargets();

        // Recomputes and writes back LightData::shadowIndex for every currently-registered
        // shadow-casting light (directional/spot: ordinal position among same-type shadow casters in
        // ascending light-index order; point: the persistent cubeArrayLayer). Must run after any change
        // to the set of registered shadow-casting lights (RegisterOrUpdateLight, UnregisterLight) -
        // callers must ensure LightManager::Update() runs afterward so the new indices reach the GPU.
        void ReassignShadowIndices();

        // Every point light's framebuffer is the whole (layered) cube map array : a pass clearing it would wipe the other lights' layers.
        // The array is cleared once per frame by this pass instead, and every point light's pass comes after it without clearing.
        void AddPointClearPass();
        void RemovePointClearPass();

        std::map<int, ShadowMap> m_ShadowMaps;
        std::shared_ptr<CubemapArray> m_CubeArrayTex;
        std::shared_ptr<Framebuffer> m_PointClearTarget;
        std::shared_ptr<Shader> m_DirShader;
        std::shared_ptr<Shader> m_SpotShader;
        std::shared_ptr<Shader> m_PointShader; 
        int m_PointShadowsResolution = 0;
        int m_SpotShadowsResolution = 0;
        int m_DirShadowsResolution = 0;
        Projects::ShadowSettings m_Settings;
        int m_PointLightCount = 0;
        int m_CurrentPointLightCapacity = 0;
        std::unordered_map<int, int> m_PreviousLightTypes;
    };
}