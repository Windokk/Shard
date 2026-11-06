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
    class Texture2DArray;
    class Framebuffer;
    struct DrawCommand;
    
    static constexpr uint32_t MAX_CASCADES_PER_LIGHT = Projects::ShadowSettings::MaxCascades;
    static constexpr uint32_t NUM_CASCADES = (LightManager::MAX_DIRECTIONAL_LIGHTS * 3);

    // Mirrors the fixed arrays in lit.frag (spotLightSpaceMatrices[10], pointLightFarPlanes[10]). Beyond the cap shadowIndex = -1 and the light is unshadowed.
    static constexpr int MAX_SPOT_LIGHTS = 10;
    static constexpr int MAX_POINT_LIGHTS = 10;

    struct ShadowMap {
        LightData light;

        std::shared_ptr<LightData> lightPtr;

        std::shared_ptr<Framebuffer> framebuffer[MAX_CASCADES_PER_LIGHT] = {nullptr};

        uint32_t attachedArray = 0;
        int attachedBaseLayer = -2;

        glm::mat4 lightMatrix[MAX_CASCADES_PER_LIGHT];
        float cascadeSplits[MAX_CASCADES_PER_LIGHT];
        uint32_t cascadeCount = 0;
        float splitCameraNear = -1.0f;
        float splitCameraFar = -1.0f;

        std::shared_ptr<Cubemap> depthCubemap;
        glm::mat4 shadowMatrices[6];
        int cubeArrayLayer = -1;

        int resolution = 0;
        
        std::vector<std::string> passes;
    };

    class ShadowManager {
    public:

        void Init(int pointShadowsResolution, int spotShadowsResolution, int dirShadowsResolution);
        
        void Configure(const Projects::ShadowSettings& settings);

        const Projects::ShadowSettings& GetSettings() const { return m_Settings; }

        void RegisterOrUpdateLight(int lightIndex, std::shared_ptr<LightData> light);

        void UnregisterLight(int lightIndex);

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

        void ComputeCascadeSplits(float* out, float cameraNear, float shadowFar) const;

        float GetDirectionalShadowFar(float cameraNear, float cameraFar) const;
        void EnsureCubeArrayCapacity(int requiredPointLights);
        void TryShrinkCubeArray();

        void RecreateCubeArray(int capacity);

        // Run when the array is recreated or layers shift, else shadow_point.geom writes a deleted texture or another light's layer.
        void SyncPointShadowTargets();

        void ReassignShadowIndices();

        void SyncLayeredShadowTargets();

        std::shared_ptr<Texture2DArray> CreateShadowArray(int resolution, uint32_t layers, bool compare) const;

        // The framebuffer is the whole cube array, so clear it once per frame here and never in each light's pass.
        void AddPointClearPass();
        void RemovePointClearPass();

        std::map<int, ShadowMap> m_ShadowMaps;
        std::shared_ptr<CubemapArray> m_CubeArrayTex;
        std::shared_ptr<Texture2DArray> m_DirArrayTex;
        std::shared_ptr<Texture2DArray> m_SpotArrayTex;
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