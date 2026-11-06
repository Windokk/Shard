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

    // Dynamic: real time only. Stationary: real time plus baked bounce. Static: fully baked, a lightmapped surface skips it in real time.
    enum LightBakeMode : uint8_t{
        Dynamic = 0,
        Stationary = 1,
        Static = 2
    };

    struct LightData {
        glm::vec4 position = glm::vec4(0);
        glm::vec4 direction = glm::vec4(0);
        glm::vec4 color = glm::vec4(0);

        float intensity = 0.0f;

        float radius = 0.0f;

        float innerCutoff = 0.0f;

        float outerCutoff = 0.0f;

        int type = 0;
        int castShadow = 0;

        // Persistent shadow slot, -1 if none. Use it instead of scan order to find the shadow map (see ShadowManager, lit.frag).
        int shadowIndex = -1;

        int padding = 0;

        // Emitter size for soft shadows in the offline renderers only. Point/spot: sphere radius. Directional: sun disc angular radius in degrees. 0 is hard.
        float sourceSize = 0.0f;

        int bakeMode = 0;

        int padding2 = 0;
        int padding3 = 0;

        glm::mat4 GetLightMatrix(const glm::mat4 &cameraView = glm::mat4(1.0f), const float fov = -1.0f, const float aspectRatio = -1.0f, const float cascadeNear = -1.0f, const float cascadeFar = -1.0f, const float dirShadowRes = -1.0, const float outerCutoff = -1.0, const bool stabilizeCascade = true, const float casterRange = 500.0f);
    };

    // The SSBO is the raw memory of std::vector<LightData>, indexed by the GLSL struct of the same name.
    static_assert(sizeof(LightData) % 16 == 0, "LightData must match the std430 array stride of its GLSL twin (light.glsl)");
    static_assert(sizeof(LightData) == 96, "LightData and light.glsl's Light have to change together");

    class LightManager {
        public:

            static constexpr uint32_t MAX_LIGHTS = 128;

            static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 3;

            LightManager();

            ~LightManager();

            void Sync(const std::vector<LightProxy>& proxies);

            void Clear();

            int GetLightsCount();

            const std::vector<std::shared_ptr<LightData>>& GetLights() const { return m_Lights; }

            std::shared_ptr<StorageBuffer> GetSSBO() const { return m_SSBO; }

        private:
            void Upload();

            std::vector<std::shared_ptr<LightData>> m_Lights;
            std::vector<uint64_t> m_Keys;
            std::unordered_map<uint64_t, std::shared_ptr<LightData>> m_ByKey;
            std::shared_ptr<StorageBuffer> m_SSBO;
    };
}