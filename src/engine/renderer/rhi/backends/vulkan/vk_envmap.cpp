#include "vk_envmap.hpp"

#include <stb/stb_image.h>

#include "vk_api.hpp"
#include "vk_cubemap.hpp"
#include "vk_pipeline.hpp"
#include "vk_shader.hpp"
#include "vk_texture.hpp"

#include "engine/assets/resources_manager.hpp"
#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Rendering {

    using namespace VK;

    VKEnvironmentMap::VKEnvironmentMap(const TextureSpecifications& infos, std::shared_ptr<Cubemap> cubemap, std::shared_ptr<Cubemap> irradianceMap,
                                       std::shared_ptr<Cubemap> prefilterMap, std::shared_ptr<Texture2D> brdfLUT)
    {
        m_Infos = infos;
        m_Cubemap = cubemap;
        m_IrradianceMap = irradianceMap;
        m_PrefilterMap = prefilterMap;
        m_BrdfLUT = brdfLUT;
    }

    namespace {

        std::shared_ptr<Shader> LoadShader(const char* path)
        {
            return Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Core::Resources::AssetKind::Shader, path);
        }

        std::shared_ptr<Pipeline> MakePipeline(const std::shared_ptr<Shader>& shader, const VertexLayout& layout, PrimitiveTopology topology, const char* name)
        {
            PipelineSpecifications specs;
            specs.shader = shader;
            specs.vertexLayout = layout;
            specs.topology = topology;
            specs.cullMode = CullMode::None;
            specs.depthTest = false;
            specs.depthWrite = false;
            specs.blending = false;
            specs.debugName = name;
            return std::make_shared<VKPipeline>(specs);
        }

        VKRendererAPI* Api()
        {
            return VKRendererAPI::Get();
        }

        /// A cube map of the generator's images : in the right layout to be drawn into whatever it was doing before
        Image& ImageOf(const std::shared_ptr<Cubemap>& cubemap)
        {
            return static_cast<VKCubemap*>(cubemap.get())->Resource().image;
        }

        void RegenerateMips(Image& image)
        {
            if (image.mips <= 1)
                return;

            Context& context = Context::Get();
            context.EnsureOutsideRendering();
            VkCommandBuffer cmd = context.Cmd();
            image.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            GenerateMips(cmd, image, image.RestingLayout());
        }
    }

    void VKEnvironmentMapGenerator::EnsureGeometry()
    {
        if (m_Cube)
            return;

        const float cube[] = {
            -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,
        };

        VertexLayout cubeLayout({{"aPos", ShaderDataType::Vec3, 0, 0}}, 3 * sizeof(float));
        std::vector<uint8_t> cubeBytes(sizeof(cube));
        std::memcpy(cubeBytes.data(), cube, sizeof(cube));
        std::vector<uint32_t> cubeIndices(36);
        for (uint32_t i = 0; i < 36; i++)
            cubeIndices[i] = i;

        m_Cube = Mesh::Create();
        m_Cube->Create(cubeBytes, cubeIndices, cubeLayout);

        const float quad[] = {
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };

        VertexLayout quadLayout({{"aPos", ShaderDataType::Vec3, 0, 0}, {"aTexCoord", ShaderDataType::Vec2, 1, 3 * sizeof(float)}}, 5 * sizeof(float));
        std::vector<uint8_t> quadBytes(sizeof(quad));
        std::memcpy(quadBytes.data(), quad, sizeof(quad));

        m_Quad = Mesh::Create();
        m_Quad->Create(quadBytes, {0, 1, 2, 3}, quadLayout);
    }

    void VKEnvironmentMapGenerator::DrawCubeFace(const std::shared_ptr<Shader>& shader, const std::string& samplerName, uint32_t sourceHandle,
                                                 const std::shared_ptr<Cubemap>& target, uint32_t mip, uint32_t face, bool stripView, float roughness)
    {
        VKRendererAPI* api = Api();
        Image& image = ImageOf(target);

        RenderTarget renderTarget;
        renderTarget.color = &image;
        renderTarget.colorView = image.SubView(mip, 1, face, 1, VK_IMAGE_VIEW_TYPE_2D);
        renderTarget.width = std::max(1u, image.width >> mip);
        renderTarget.height = std::max(1u, image.height >> mip);
        renderTarget.layers = 1;
        renderTarget.samples = VK_SAMPLE_COUNT_1_BIT;

        VertexLayout layout({{"aPos", ShaderDataType::Vec3, 0, 0}}, 3 * sizeof(float));
        std::shared_ptr<Pipeline> pipeline = MakePipeline(shader, layout, PrimitiveTopology::Triangles, "IBL capture");

        api->SetTargetOverride(&renderTarget);
        api->SetViewport(0, 0, renderTarget.width, renderTarget.height);
        api->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        api->Clear(ClearBit::Color);

        api->BindTextureUnit(0, sourceHandle);
        shader->SetInt(samplerName, 0);
        shader->SetMat4("uProjection", m_CaptureProjection);
        shader->SetMat4("uView", stripView ? glm::mat4(glm::mat3(m_CaptureViews[face])) : m_CaptureViews[face]);
        if (roughness >= 0.0f)
            shader->SetFloat("roughness", roughness);

        DrawCommand command{};
        command.mesh = m_Cube;
        command.indexCount = 36;
        command.vertexCount = 36;
        api->DrawDirect(command, pipeline);

        api->SetTargetOverride(nullptr);
    }

    void VKEnvironmentMapGenerator::BuildDerived(const std::shared_ptr<Cubemap>& cubemap, std::shared_ptr<Cubemap>& irradiance, std::shared_ptr<Cubemap>& prefilter, std::shared_ptr<Texture2D>& brdf)
    {
        EnsureGeometry();

        std::array<unsigned char*, 6> noData = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

        TextureSpecifications texSpecs;
        texSpecs.magFilter = TextureFilter::Linear;
        texSpecs.minFilter = TextureFilter::Linear;
        texSpecs.wrapS = TextureWrap::ClampEdge;
        texSpecs.wrapT = TextureWrap::ClampEdge;
        texSpecs.wrapR = TextureWrap::ClampEdge;

        // ---- Irradiance ----
        texSpecs.generateMips = false;
        texSpecs.internalFormat = TextureInternalFormat::RGB16F;
        texSpecs.width = 32;
        texSpecs.height = 32;
        irradiance = Cubemap::Create(texSpecs, noData);

        if (std::shared_ptr<Shader> shader = LoadShader("shaders/ibl/irradiance"))
        {
            for (uint32_t face = 0; face < 6; face++)
                DrawCubeFace(shader, "uEnvironmentMap", cubemap->GetHandle(), irradiance, 0, face, false, -1.0f);
        }
        else
            DEBUG_ERROR("Cubemap irradiance map couldn't be created because shader \"shaders/ibl/irradiance\" is not loaded !");

        // ---- Prefilter : the first two levels, as the OpenGL generator does ----
        texSpecs.generateMips = true;
        texSpecs.width = 128;
        texSpecs.height = 128;
        prefilter = Cubemap::Create(texSpecs, noData);

        if (std::shared_ptr<Shader> shader = LoadShader("shaders/ibl/prefilter"))
        {
            const uint32_t levels = 2;
            for (uint32_t mip = 0; mip < levels; mip++)
            {
                const float roughness = static_cast<float>(mip) / static_cast<float>(levels - 1);
                for (uint32_t face = 0; face < 6; face++)
                    DrawCubeFace(shader, "uEnvironmentMap", cubemap->GetHandle(), prefilter, mip, face, false, roughness);
            }
        }
        else
            DEBUG_ERROR("Cubemap prefilter map couldn't be created because shader \"shaders/ibl/prefilter\" is not loaded !");

        // ---- BRDF table ----
        texSpecs.generateMips = false;
        texSpecs.internalFormat = TextureInternalFormat::RG16F;
        texSpecs.width = 512;
        texSpecs.height = 512;
        brdf = Texture2D::Create(texSpecs, nullptr);

        if (std::shared_ptr<Shader> shader = LoadShader("shaders/ibl/brdf"))
        {
            VKRendererAPI* api = Api();
            Image& image = static_cast<VKTexture2D*>(brdf.get())->Resource().image;

            RenderTarget renderTarget;
            renderTarget.color = &image;
            renderTarget.colorView = image.SubView(0, 1, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
            renderTarget.width = image.width;
            renderTarget.height = image.height;
            renderTarget.samples = VK_SAMPLE_COUNT_1_BIT;

            VertexLayout layout({{"aPos", ShaderDataType::Vec3, 0, 0}, {"aTexCoord", ShaderDataType::Vec2, 1, 3 * sizeof(float)}}, 5 * sizeof(float));
            std::shared_ptr<Pipeline> pipeline = MakePipeline(shader, layout, PrimitiveTopology::TriangleStrip, "BRDF table");

            api->SetTargetOverride(&renderTarget);
            api->SetViewport(0, 0, image.width, image.height);
            api->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            api->Clear(ClearBit::Color);

            DrawCommand command{};
            command.mesh = m_Quad;
            command.indexCount = 4;
            command.vertexCount = 4;
            api->DrawDirect(command, pipeline);

            api->SetTargetOverride(nullptr);
        }
        else
            DEBUG_ERROR("Cubemap brdf lookup table couldn't be created because shader \"shaders/ibl/brdf\" is not loaded !");

        RegenerateMips(ImageOf(prefilter));
    }

    std::shared_ptr<EnvironmentMap> VKEnvironmentMapGenerator::GenerateFromFiles(TextureSpecifications& specs, const std::vector<Filesystem::Path> imageFiles)
    {
        if (imageFiles.size() < 6)
            return nullptr;

        std::array<Filesystem::Path, 6> files = {imageFiles[0], imageFiles[1], imageFiles[2], imageFiles[3], imageFiles[4], imageFiles[5]};

        // Six ordinary 8 bit pictures : colours in the sRGB curve, which the GPU turns into linear light as it samples them
        specs.srgb = true;
        std::shared_ptr<Cubemap> cubemap = Cubemap::Create(specs, files);

        std::shared_ptr<Cubemap> irradiance, prefilter;
        std::shared_ptr<Texture2D> brdf;
        BuildDerived(cubemap, irradiance, prefilter, brdf);

        return std::make_shared<VKEnvironmentMap>(specs, cubemap, irradiance, prefilter, brdf);
    }

    std::shared_ptr<EnvironmentMap> VKEnvironmentMapGenerator::GenerateFromHDR(TextureSpecifications& specs, const Filesystem::Path hdrFile)
    {
        if (!hdrFile.Exists())
        {
            DEBUG_ERROR("Couldn't find texture : " + hdrFile.full);
            return nullptr;
        }

        std::string file = hdrFile.ReadFile();

        // As floats, in linear light : a Radiance .hdr keeps the radiance it stores. Three channels whatever the file has.
        stbi_set_flip_vertically_on_load_thread(true);
        int width = 0, height = 0, channels = 0;
        void* data = stbi_loadf_from_memory(reinterpret_cast<const unsigned char*>(file.data()), static_cast<int>(file.size()), &width, &height, &channels, 3);
        if (!data)
        {
            DEBUG_ERROR("Couldn't load texture : " + hdrFile.full);
            return nullptr;
        }

        specs.internalFormat = TextureInternalFormat::RGB16F;
        specs.width = width;
        specs.height = height;
        specs.wrapS = TextureWrap::ClampEdge;
        specs.wrapT = TextureWrap::ClampEdge;
        specs.wrapR = TextureWrap::ClampEdge;
        specs.magFilter = TextureFilter::Linear;
        specs.minFilter = TextureFilter::Linear;

        std::shared_ptr<Texture2D> hdrTexture = Texture2D::Create(specs, data);
        stbi_image_free(data);

        std::shared_ptr<Shader> shader = LoadShader("shaders/ibl/equirectToCubemap");
        if (!shader)
        {
            DEBUG_ERROR("HDR cubemap couldn't be created because shader \"shaders/ibl/equirectToCubemap\" is not loaded !");
            return nullptr;
        }

        EnsureGeometry();

        std::array<unsigned char*, 6> noData = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        specs.internalFormat = TextureInternalFormat::RGB16F;       // the sky keeps its dynamic range
        specs.width = 512;
        specs.height = 512;
        std::shared_ptr<Cubemap> cubemap = Cubemap::Create(specs, noData);

        for (uint32_t face = 0; face < 6; face++)
            DrawCubeFace(shader, "uEquirectangularMap", hdrTexture->GetHandle(), cubemap, 0, face, true, -1.0f);

        RegenerateMips(ImageOf(cubemap));

        std::shared_ptr<Cubemap> irradiance, prefilter;
        std::shared_ptr<Texture2D> brdf;
        BuildDerived(cubemap, irradiance, prefilter, brdf);

        return std::make_shared<VKEnvironmentMap>(specs, cubemap, irradiance, prefilter, brdf);
    }

}
