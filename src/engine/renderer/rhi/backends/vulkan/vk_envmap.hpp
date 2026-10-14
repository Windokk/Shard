#pragma once

#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Shard::Engine::Rendering {

    class Mesh;
    class Pipeline;
    class Shader;

    class VKEnvironmentMap final : public EnvironmentMap
    {
        public:

            VKEnvironmentMap(const TextureSpecifications& infos, std::shared_ptr<Cubemap> cubemap, std::shared_ptr<Cubemap> irradianceMap,
                             std::shared_ptr<Cubemap> prefilterMap, std::shared_ptr<Texture2D> brdfLUT);
    };

    /// Builds what the image based lighting reads (the sky as a cube map, its diffuse convolution, its specular prefilter, the BRDF table) by
    /// drawing into the faces of cube maps with the shaders of shaders/ibl : the same work the OpenGL generator does, on the render target override of the
    /// Vulkan API (a face and a level of an image).
    class VKEnvironmentMapGenerator final : public EnvironmentMapGenerator
    {
        public:

            std::shared_ptr<EnvironmentMap> GenerateFromFiles(TextureSpecifications& specs, const std::vector<Filesystem::Path> imageFiles) override;
            std::shared_ptr<EnvironmentMap> GenerateFromHDR(TextureSpecifications& specs, const Filesystem::Path hdrFile) override;

        private:

            void EnsureGeometry();

            /// Draws the unit cube into `face` of level `mip` of `target` with `shader` (which reads `source`, a cube map or a 2D texture, as `samplerName`)
            void DrawCubeFace(const std::shared_ptr<Shader>& shader, const std::string& samplerName, uint32_t sourceHandle, const std::shared_ptr<Cubemap>& target,
                              uint32_t mip, uint32_t face, bool stripView, float roughness);

            void BuildDerived(const std::shared_ptr<Cubemap>& cubemap, std::shared_ptr<Cubemap>& irradiance, std::shared_ptr<Cubemap>& prefilter, std::shared_ptr<Texture2D>& brdf);

            std::shared_ptr<Mesh> m_Cube;
            std::shared_ptr<Mesh> m_Quad;

            glm::mat4 m_CaptureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
            glm::mat4 m_CaptureViews[6] = {
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
                glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
            };
    };

}
