#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace Shard::Engine::Rendering {

    class EnvironmentMap;

    enum class ViewLighting {
        // Clustered culling and SSAO are built for the active camera, so only meaningful for the main frame.
        Scene,
        Studio
    };

    struct RenderView {
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);
        glm::vec3 position = glm::vec3(0.0f);
        bool orthographic = false;

        ViewLighting lighting = ViewLighting::Scene;

        int studioLightCount = 0;
        float studioAmbient = 0.0f;
        std::shared_ptr<EnvironmentMap> studioEnvironment;
    };
}
