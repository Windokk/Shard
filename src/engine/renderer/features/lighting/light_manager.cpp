#include "light_manager.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>

#include "engine/world/engine.hpp"
#include "engine/platform/windowing/iwindow.hpp"
#include "engine/core/diagnostics/logger.hpp"

#include "engine/renderer/rhi/render_context.hpp"

#include "engine/renderer/features/lighting/shadow_manager.hpp"

#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"

namespace Shard::Engine::Rendering{

    LightManager::LightManager()
    {
        m_SSBO = StorageBuffer::Create(sizeof(LightData) * 0);
    }

    LightManager::~LightManager()
    {
        // m_SSBO is a shared_ptr and frees itself, an explicit ~StorageBuffer() freed it twice.
        m_SSBO.reset();
    }

    namespace {

        bool ApplyProxy(const LightProxy& proxy, LightData& light)
        {
            const glm::vec4 position(proxy.position, 0.0f);
            const glm::vec4 direction(proxy.direction, 0.0f);
            const glm::vec4 color(proxy.color, 0.0f);
            const int type = static_cast<int>(proxy.type);
            const int castShadow = proxy.castShadows ? 1 : 0;

            const bool changed = light.position != position || light.direction != direction || light.color != color ||
                                 light.intensity != proxy.intensity || light.radius != proxy.radius ||
                                 light.innerCutoff != proxy.innerCutoff || light.outerCutoff != proxy.outerCutoff ||
                                 light.type != type || light.castShadow != castShadow ||
                                 light.sourceSize != proxy.sourceSize || light.bakeMode != proxy.bakeMode;

            light.position = position;
            light.direction = direction;
            light.color = color;
            light.intensity = proxy.intensity;
            light.radius = proxy.radius;
            light.innerCutoff = proxy.innerCutoff;
            light.outerCutoff = proxy.outerCutoff;
            light.type = type;
            light.castShadow = castShadow;
            light.sourceSize = proxy.sourceSize;
            light.bakeMode = proxy.bakeMode;
            return changed;
        }
    }

    void LightManager::Sync(const std::vector<LightProxy>& proxies)
    {
        // Proxies are sorted by key. If the held lights are a prefix, only append, else indices shifted and shadow maps must re-register.
        bool appendOnly = proxies.size() >= m_Keys.size();
        for (size_t i = 0; appendOnly && i < m_Keys.size(); i++)
            appendOnly = m_Keys[i] == proxies[i].key;

        std::vector<std::shared_ptr<LightData>> lights;
        std::vector<uint64_t> keys;
        std::unordered_map<uint64_t, std::shared_ptr<LightData>> byKey;
        std::vector<size_t> dirty;
        lights.reserve(proxies.size());
        keys.reserve(proxies.size());

        for (size_t i = 0; i < proxies.size(); i++) {
            const LightProxy& proxy = proxies[i];

            std::shared_ptr<LightData> light;
            bool isNew = false;
            auto it = m_ByKey.find(proxy.key);
            if (it != m_ByKey.end()) {
                light = it->second;
            } else {
                light = std::make_shared<LightData>();
                isNew = true;
            }

            if (ApplyProxy(proxy, *light) || isNew)
                dirty.push_back(i);

            lights.push_back(light);
            keys.push_back(proxy.key);
            byKey.emplace(proxy.key, light);
        }

        const bool sameCount = lights.size() == m_Lights.size();
        if (appendOnly && sameCount && dirty.empty())
            return;

        auto shadows = Core::GetEngine().GetRenderContext()->GetShadowManager();

        const std::vector<uint64_t> oldKeys = std::move(m_Keys);

        m_Lights = std::move(lights);
        m_Keys = std::move(keys);
        m_ByKey = std::move(byKey);

        if (!appendOnly) {
            for (size_t i = 0; i < oldKeys.size(); i++)
                if (shadows->HasShadowMap(static_cast<int>(i)) && (i >= m_Keys.size() || m_Keys[i] != oldKeys[i]))
                    shadows->UnregisterLight(static_cast<int>(i));

            std::vector<bool> isDirty(m_Lights.size(), false);
            for (size_t i : dirty)
                isDirty[i] = true;

            for (size_t i = 0; i < m_Lights.size(); i++)
                if (isDirty[i] || (m_Lights[i]->castShadow && !shadows->HasShadowMap(static_cast<int>(i))))
                    shadows->RegisterOrUpdateLight(static_cast<int>(i), m_Lights[i]);
        } else {
            for (size_t i : dirty)
                shadows->RegisterOrUpdateLight(static_cast<int>(i), m_Lights[i]);
        }

        Upload();
    }

    void LightManager::Upload()
    {
        if (m_Lights.empty())
            return;

        std::vector<LightData> flatLights;
        flatLights.reserve(m_Lights.size());

        for (auto& light : m_Lights)
            flatLights.push_back(*light);

        m_SSBO->SetData(flatLights.data(), sizeof(LightData) * flatLights.size());
        m_SSBO->Bind(0);
    }

    void LightManager::Clear()
    {
        if (!m_Lights.empty())
            Core::GetEngine().GetRenderContext()->GetShadowManager()->ClearAll();

        m_Lights.clear();
        m_Keys.clear();
        m_ByKey.clear();
    }

    int LightManager::GetLightsCount()
    {
        return m_Lights.size();
    }

    std::vector<glm::vec4> getFrustumCornersWorldSpace(const glm::mat4& projview)
    {
        const auto inv = glm::inverse(projview);

        std::vector<glm::vec4> frustumCorners;
        for (unsigned int x = 0; x < 2; ++x)
        {
            for (unsigned int y = 0; y < 2; ++y)
            {
                for (unsigned int z = 0; z < 2; ++z)
                {
                    const glm::vec4 pt = inv * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                    frustumCorners.push_back(pt / pt.w);
                }
            }
        }

        return frustumCorners;
    }

    glm::mat4 LightData::GetLightMatrix(const glm::mat4& cameraView, const float fov, const float aspectRatio, const float cascadeNear, const float cascadeFar, const float dirShadowRes, const float outerCutoff, const bool stabilizeCascade, const float casterRange)
    {
        if (type == static_cast<int>(LightType::Directional) && cascadeFar != -1 && fov != -1 && aspectRatio != -1 && cascadeFar != -1)
        {
            const auto proj = glm::perspective(
                glm::radians(fov), aspectRatio, cascadeNear,
                cascadeFar);
            const auto corners = getFrustumCornersWorldSpace(proj * cameraView);

            glm::vec3 center(0.0f);
            for (auto& v : corners)
                center += glm::vec3(v);
            center /= corners.size();

            
            float radius = 0.0f;
            for (const auto& v : corners)
                radius = std::max(radius, glm::length(glm::vec3(v) - center));

            radius = std::ceil(radius * 16.0f) / 16.0f;

            glm::vec3 lightDir = glm::normalize(glm::vec3(direction));

            glm::vec3 up = fabs(glm::dot(lightDir, glm::vec3(0,1,0))) > 0.99f
                ? glm::vec3(1,0,0)
                : glm::vec3(0,1,0);

            // Reach every caster between the sun and the cascade, or ceilings and wall tops get clipped and light leaks.
            float backoff = std::max(radius * 2.0f, casterRange);

            glm::vec3 lightPos = center - lightDir * backoff;
            glm::mat4 lightView = glm::lookAt(lightPos, center, up);

            float extent = radius;
            float nearPlane = 0.0f;
            float farPlane  = backoff + radius;

            glm::mat4 lightProj = glm::ortho(
                -extent, extent,
                -extent, extent,
                nearPlane, farPlane
            );

            glm::mat4 lightVP = lightProj * lightView;

            glm::vec4 origin = lightVP * glm::vec4(0, 0, 0, 1);
            origin /= origin.w;

            glm::vec2 shadowOrigin = glm::vec2(origin) * (dirShadowRes * 0.5f);
            glm::vec2 roundedOrigin = glm::round(shadowOrigin);
            glm::vec2 offset = (roundedOrigin - shadowOrigin) * (2.0f / dirShadowRes);

            if (stabilizeCascade)
            {
                lightProj[3][0] += offset.x;
                lightProj[3][1] += offset.y;
            }

            return lightProj * lightView;
        }
        else if (type == static_cast<int>(LightType::Spot))
        {
            float fov = glm::degrees(acos(outerCutoff)) * 2.0f;
            fov = glm::clamp(fov, 1.0f, 179.0f);
            float aspect = 1.0f;
            glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, 0.1f, radius);

            glm::vec3 lightDir = glm::normalize(glm::vec3(direction));
            glm::vec3 lightPos = position;

            glm::vec3 up = glm::abs(glm::dot(lightDir, glm::vec3(0, 1, 0))) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            glm::mat4 view = glm::lookAt(lightPos, lightPos+lightDir, up);

            return proj * view;
        }

        return glm::mat4(1.0f);
    }
}