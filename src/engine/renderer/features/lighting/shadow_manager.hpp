#pragma once

#include "light_manager.hpp"

#include <map>

#include <engine/debugging/logger.hpp>

namespace Shard::Engine::Objects::Components{
    class Camera;
}

namespace Shard::Engine::Rendering {

    class Shader;
    class Material;
    class Mesh;
    class Texture2D;
    class Cubemap;
    class CubemapArray;
    class Framebuffer;
    struct DrawCommand;
    
    static constexpr uint32_t CASCADES_PER_LIGHT = 3;
    static constexpr uint32_t NUM_CASCADES = (LightManager::MAX_DIRECTIONAL_LIGHTS * CASCADES_PER_LIGHT);

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
        std::shared_ptr<Framebuffer> framebuffer[CASCADES_PER_LIGHT] = {nullptr};       // One FBO per cascade (only 1 used for spot)
        
        // Directional (CSM)
        glm::mat4 lightMatrix[CASCADES_PER_LIGHT];                                      // One matrix per cascade
        float cascadeSplits[CASCADES_PER_LIGHT];                                        // Only used for directional lights

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

        const ShadowMap* GetShadowMapFromLightIndex(int lightIndex) const {
            auto it = m_ShadowMaps.find(lightIndex);
            if (it != m_ShadowMaps.end())
                return &it->second;

            DEBUG_ERROR("Couldn't find shadow map for light : %d", lightIndex);
            return nullptr;
        }

        // DO NOT USE
        void ClearAll();

        const int GetShadowMapsCount() const {
            return m_ShadowMaps.size();
        }

        const int GetMaxShadowMapsCount() const {
            return 128;
        }

        void UpdatePassUniforms();
        
    private:

        void SubmitPasses(int lightIndex);
        void EnsureCubeArrayCapacity(int requiredPointLights);
        void TryShrinkCubeArray();

        // Recomputes and writes back LightData::shadowIndex for every currently-registered
        // shadow-casting light (directional/spot: ordinal position among same-type shadow casters in
        // ascending light-index order; point: the persistent cubeArrayLayer). Must run after any change
        // to the set of registered shadow-casting lights (RegisterOrUpdateLight, UnregisterLight) -
        // callers must ensure LightManager::Update() runs afterward so the new indices reach the GPU.
        void ReassignShadowIndices();

        std::map<int, ShadowMap> m_ShadowMaps;
        std::shared_ptr<CubemapArray> m_CubeArrayTex;
        std::shared_ptr<Shader> m_DirShader;
        std::shared_ptr<Shader> m_SpotShader;
        std::shared_ptr<Shader> m_PointShader; 
        int m_PointShadowsResolution = 0;
        int m_SpotShadowsResolution = 0;
        int m_DirShadowsResolution = 0;
        int m_PointLightCount = 0;
        int m_CurrentPointLightCapacity = 0;
        std::unordered_map<int, int> m_PreviousLightTypes;
    };
}