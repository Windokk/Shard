#include "gl_envmap.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"

#include "engine/renderer/rhi/shader/shader.hpp"

#include <stb/stb_image.h>

#include "engine/assets/resources_manager.hpp"

#include "engine/renderer/rhi/backends/opengl/shader/gl_shader.hpp"

namespace Shard::Engine::Rendering{

    std::shared_ptr<EnvironmentMap> GLEnvironmentMapGenerator::GenerateFromFiles(TextureSpecifications &specs, const std::vector<Filesystem::Path> imageFiles)
    {
        if(imageFiles.size() < 6)
            return nullptr;

        std::array<Filesystem::Path, 6> files = {imageFiles[0], imageFiles[1], imageFiles[2], imageFiles[3], imageFiles[4], imageFiles[5]};
        std::shared_ptr<Cubemap> cubemap = Cubemap::Create(specs, files);

        std::shared_ptr<Cubemap> irradianceMap;
        std::shared_ptr<Cubemap> prefilterMap;
        std::shared_ptr<Texture2D> brdfLUT;

        GenerateGeometry();

        std::array<unsigned char*, 6> dataArray = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

        TextureSpecifications texSpecs;

        texSpecs.magFilter = TextureFilter::Linear;
        texSpecs.minFilter = TextureFilter::Linear;
        texSpecs.wrapS = TextureWrap::ClampEdge;
        texSpecs.wrapT = TextureWrap::ClampEdge;
        texSpecs.wrapR = TextureWrap::ClampEdge;
        texSpecs.generateMips = false;
        texSpecs.internalFormat = TextureInternalFormat::RGB16F;
        texSpecs.width = 32;
        texSpecs.height = 32;

        irradianceMap = Cubemap::Create(texSpecs, dataArray);

        CreateIrradiance(irradianceMap, cubemap);

        texSpecs.magFilter = TextureFilter::Linear;
        texSpecs.minFilter = TextureFilter::Linear;
        texSpecs.wrapS = TextureWrap::ClampEdge;
        texSpecs.wrapT = TextureWrap::ClampEdge;
        texSpecs.wrapR = TextureWrap::ClampEdge;
        texSpecs.generateMips = true;
        texSpecs.internalFormat = TextureInternalFormat::RGB16F;
        texSpecs.width = 128;
        texSpecs.height = 128;

        prefilterMap = Cubemap::Create(texSpecs, dataArray);

        CreatePrefilter(prefilterMap, cubemap);

        texSpecs.magFilter = TextureFilter::Linear;
        texSpecs.minFilter = TextureFilter::Linear;
        texSpecs.wrapS = TextureWrap::ClampEdge;
        texSpecs.wrapT = TextureWrap::ClampEdge;
        texSpecs.wrapR = TextureWrap::ClampEdge;
        texSpecs.generateMips = false;
        texSpecs.internalFormat = TextureInternalFormat::RG;
        texSpecs.width = 512;
        texSpecs.height = 512;

        brdfLUT = Texture2D::Create(texSpecs, nullptr);

        CreateBRDFLUT(brdfLUT);

        return std::make_shared<GLEnvironmentMap>(specs, cubemap, irradianceMap, prefilterMap, brdfLUT);
    }

    std::shared_ptr<EnvironmentMap> GLEnvironmentMapGenerator::GenerateFromHDR(TextureSpecifications &specs, const Filesystem::Path hdrFile)
    {
        //Extract cubemap data from the hdr file

        void* data = nullptr;

        int width, height, nrChannels;
        TextureFormat format;

        if (hdrFile.Exists()) {
            std::string file = hdrFile.ReadFile();
            
            stbi_set_flip_vertically_on_load(true);
            data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(file.data()),
                                            static_cast<int>(file.size()),
                                            &width, &height, &nrChannels, 0);

        } else {
            DEBUG_ERROR("Couldn't find texture : " + hdrFile.full);
            return nullptr;
        }

        if (data) {
            if (nrChannels == 1){
                format = TextureFormat::RED;
            }
            else if (nrChannels == 2){
                format = TextureFormat::RG;
            }
            else if (nrChannels == 3){
                format = TextureFormat::RGB;
            }
            else if (nrChannels == 4){
                format = TextureFormat::RGBA;
            }
            else{
                format = TextureFormat::RGB; // Default to RGB
            }

        } else {
            DEBUG_ERROR("Couldn't load texture : " + hdrFile.full);
            return nullptr;
        }

        if(format != TextureFormat::RGB)
        {
            DEBUG_ERROR("Currently cannot create an environment map without rgb format : "+hdrFile.full);
            return nullptr;
        }

        specs.internalFormat = TextureInternalFormat::RGB8;
        specs.width = width;
        specs.height = height;
        specs.wrapS = TextureWrap::ClampEdge;
        specs.wrapT = TextureWrap::ClampEdge;
        specs.wrapR = TextureWrap::ClampEdge;
        specs.magFilter = TextureFilter::Linear;
        specs.minFilter = TextureFilter::Linear;

        std::shared_ptr<Texture2D> hdrTex = Texture2D::Create(specs, data);

        std::shared_ptr<Shader> equirectangularToCubemapShaderAbstract = Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Core::Resources::AssetKind::Shader, "shaders/ibl/equirectToCubemap");

        std::shared_ptr<GLShader> equirectangularToCubemapShader = std::static_pointer_cast<GLShader>(equirectangularToCubemapShaderAbstract);

        std::array<unsigned char*, 6> dataArray = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        specs.internalFormat = TextureInternalFormat::RGB;
            
        const uint32_t CUBEMAP_SIZE = 512;

        specs.width = CUBEMAP_SIZE;
        specs.height = CUBEMAP_SIZE;

        std::shared_ptr<Cubemap> cubemap = Cubemap::Create(specs, dataArray);
        {
            if(!equirectangularToCubemapShader)
            {
                DEBUG_ERROR("HDR cubemap couldn't be created because shader \"shaders/ibl/equirectToCubemap\" is not loaded !");
                return nullptr;
            }

            glGenFramebuffers(1, &captureFBO);
            glGenRenderbuffers(1, &captureRBO);

            glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
            glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, specs.width, specs.height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

            equirectangularToCubemapShader->Bind();
            equirectangularToCubemapShader->SetInt("uEquirectangularMap", 0);
            equirectangularToCubemapShader->SetMat4("uProjection", captureProjection);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, hdrTex->GetHandle());

            glViewport(0, 0, CUBEMAP_SIZE, CUBEMAP_SIZE);
            glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
            for (unsigned int i = 0; i < 6; ++i)
            {
                equirectangularToCubemapShader->SetMat4("uView", glm::mat4(glm::mat3(captureViews[i])));
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemap->GetHandle(), 0);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                {
                    DEBUG_ERROR("Capture FBO is not complete for skybox : ", hdrFile.full);
                }
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                RenderUnitCube(); // renders a 1x1 cube
            }
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    
        std::shared_ptr<Cubemap> irradianceMap;
        std::shared_ptr<Cubemap> prefilterMap;
        std::shared_ptr<Texture2D> brdfLUT;

        GenerateGeometry();

        TextureSpecifications texSpecs;

        texSpecs.magFilter = TextureFilter::Linear;
        texSpecs.minFilter = TextureFilter::Linear;
        texSpecs.wrapS = TextureWrap::ClampEdge;
        texSpecs.wrapT = TextureWrap::ClampEdge;
        texSpecs.wrapR = TextureWrap::ClampEdge;
        texSpecs.generateMips = false;
        texSpecs.internalFormat = TextureInternalFormat::RGB16F;
        texSpecs.width = 32;
        texSpecs.height = 32;

        irradianceMap = Cubemap::Create(texSpecs, dataArray);

        CreateIrradiance(irradianceMap, cubemap);

        texSpecs.magFilter = TextureFilter::Linear;
        texSpecs.minFilter = TextureFilter::Linear;
        texSpecs.wrapS = TextureWrap::ClampEdge;
        texSpecs.wrapT = TextureWrap::ClampEdge;
        texSpecs.wrapR = TextureWrap::ClampEdge;
        texSpecs.generateMips = true;
        texSpecs.internalFormat = TextureInternalFormat::RGB16F;
        texSpecs.width = 128;
        texSpecs.height = 128;

        prefilterMap = Cubemap::Create(texSpecs, dataArray);

        CreatePrefilter(prefilterMap, cubemap);

        texSpecs.magFilter = TextureFilter::Linear;
        texSpecs.minFilter = TextureFilter::Linear;
        texSpecs.wrapS = TextureWrap::ClampEdge;
        texSpecs.wrapT = TextureWrap::ClampEdge;
        texSpecs.wrapR = TextureWrap::ClampEdge;
        texSpecs.generateMips = false;
        texSpecs.internalFormat = TextureInternalFormat::RG16F;
        texSpecs.width = 512;
        texSpecs.height = 512;

        brdfLUT = Texture2D::Create(texSpecs, nullptr);

        CreateBRDFLUT(brdfLUT);

        return std::make_shared<GLEnvironmentMap>(specs, cubemap, irradianceMap, prefilterMap, brdfLUT);
    }

    void GLEnvironmentMapGenerator::GenerateGeometry()
    {
        if (cubeVAO != 0)
            return;

        float skyboxVertices[] = {
            // positions          
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f,  1.0f,
            1.0f,  1.0f,  1.0f,
            1.0f,  1.0f,  1.0f,
            1.0f,  1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            1.0f,  1.0f,  1.0f,
            1.0f,  1.0f,  1.0f,
            1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            -1.0f,  1.0f, -1.0f,
            1.0f,  1.0f, -1.0f,
            1.0f,  1.0f,  1.0f,
            1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
            1.0f, -1.0f, -1.0f,
            1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
            1.0f, -1.0f,  1.0f
        };

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        glBindVertexArray(0);

        float quadVertices[] = {
            // positions        // texture Coords
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        // setup plane VAO
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }

    void GLEnvironmentMapGenerator::CreateIrradiance(std::shared_ptr<Cubemap> irradianceMap, std::shared_ptr<Cubemap> cubemap)
    {
        std::shared_ptr<Shader> irradianceShaderAbstract = Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Core::Resources::AssetKind::Shader, "shaders/ibl/irradiance");

        std::shared_ptr<GLShader> irradianceShader = std::static_pointer_cast<GLShader>(irradianceShaderAbstract);

        if(!irradianceShader)
        {
            DEBUG_ERROR("Cubemap irradiance map couldn't be created because shader \"shaders/ibl/irradiance\" is not loaded !");
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 32, 32);

        irradianceShader->Bind();
        irradianceShader->SetInt("uEnvironmentMap", 0);
        irradianceShader->SetMat4("uProjection", captureProjection);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap->GetHandle());

        glViewport(0, 0, 32, 32);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        for (unsigned int i = 0; i < 6; ++i)
        {
            irradianceShader->SetMat4("uView", captureViews[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, irradianceMap->GetHandle(), 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            RenderUnitCube();
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void GLEnvironmentMapGenerator::CreatePrefilter(std::shared_ptr<Cubemap> prefilterMap, std::shared_ptr<Cubemap> cubemap)
    {
        std::shared_ptr<Shader> prefilterShaderAbstract = Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Core::Resources::AssetKind::Shader, "shaders/ibl/prefilter");

        std::shared_ptr<GLShader> prefilterShader = std::static_pointer_cast<GLShader>(prefilterShaderAbstract);

        if(!prefilterShader)
        {
            DEBUG_ERROR("Cubemap prefilter map couldn't be created because shader \"shaders/ibl/equirectToCubemap\" is not loaded !");
            return;
        }

        prefilterShader->Bind();
        prefilterShader->SetInt("uEnvironmentMap", 0);
        prefilterShader->SetMat4("uProjection", captureProjection);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap->GetHandle());

        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        unsigned int maxMipLevels = 2;
        for (unsigned int mip = 0; mip < maxMipLevels; ++mip)
        {
            // reisze framebuffer according to mip-level size.
            unsigned int mipWidth  = static_cast<unsigned int>(128 * std::pow(0.5, mip));
            unsigned int mipHeight = static_cast<unsigned int>(128 * std::pow(0.5, mip));
            glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
            glViewport(0, 0, mipWidth, mipHeight);

            float roughness = (float)mip / (float)(maxMipLevels - 1);
            prefilterShader->SetFloat("roughness", roughness);
            for (unsigned int i = 0; i < 6; ++i)
            {
                prefilterShader->SetMat4("uView", captureViews[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, prefilterMap->GetHandle(), mip);

                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                RenderUnitCube();
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void GLEnvironmentMapGenerator::CreateBRDFLUT(std::shared_ptr<Texture2D> brdfLUT)
    {
        std::shared_ptr<Shader> brdfShaderAbstract = Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Core::Resources::AssetKind::Shader, "shaders/ibl/brdf");

        std::shared_ptr<GLShader> brdfShader = std::static_pointer_cast<GLShader>(brdfShaderAbstract);

        if(!brdfShader)
        {
            DEBUG_ERROR("Cubemap brdf lookup table couldn't be created because shader \"shaders/ibl/brdf\" is not loaded !");
            return;
        }

        // then re-configure capture framebuffer object and render screen-space quad with BRDF shader.
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, brdfLUT->GetHandle(), 0);

        glViewport(0, 0, 512, 512);
        brdfShader->Bind();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderUnitQuad();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void GLEnvironmentMapGenerator::RenderUnitCube()
    {
        if (cubeVAO == 0)
            GenerateGeometry();

        glBindVertexArray(cubeVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    void GLEnvironmentMapGenerator::RenderUnitQuad()
    {
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }

    GLEnvironmentMap::GLEnvironmentMap(const TextureSpecifications &infos, std::shared_ptr<Cubemap> cubemap, std::shared_ptr<Cubemap> irradianceMap, std::shared_ptr<Cubemap> prefilterMap, std::shared_ptr<Texture2D> brdfLUT)
    {
        m_Infos = infos;
        m_Cubemap = cubemap;
        m_IrradianceMap = irradianceMap;
        m_PrefilterMap = prefilterMap;
        m_BrdfLUT = brdfLUT;
    }
}