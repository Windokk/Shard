#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace Shard::Engine::Rendering {

    class EnvironmentMap;

    /// @brief Where a view's scene lighting comes from.
    enum class ViewLighting {
        /// The live level : its light buffer, clustered light culling, DDGI, SSAO and shadows. The
        /// clustered culling and SSAO results are built for the active camera, so this is only
        /// meaningful for the main frame.
        Scene,
        /// A self-contained fixed studio rig (see ImmediateRenderer) : a few directional lights, a flat
        /// ambient term, and no clustered lights / DDGI / SSAO / shadows. Independent of the level.
        Studio
    };

    /// @brief Everything a draw needs to know about "where we are looking from".
    ///
    /// The renderer used to read the CameraManager's active camera directly at every draw. That made
    /// it impossible to render the same scene from anywhere else (thumbnails, scene captures, ...)
    /// without stealing the active camera. A RenderView is the plain-data snapshot of those reads :
    /// the main frame derives one from the active camera, and Renderer::PushView() lets a caller
    /// substitute its own for the duration of an immediate render.
    struct RenderView {
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);
        glm::vec3 position = glm::vec3(0.0f);
        bool orthographic = false;

        ViewLighting lighting = ViewLighting::Scene;

        /// Only read when lighting == Studio : number of lights in the rig bound at light buffer slot 0,
        /// and the flat ambient intensity that replaces Level::ambientIntensity.
        int studioLightCount = 0;
        float studioAmbient = 0.0f;
        /// Image-based lighting used in place of the level's skybox. May be null (no IBL bound).
        std::shared_ptr<EnvironmentMap> studioEnvironment;
    };
}
