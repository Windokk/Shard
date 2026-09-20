#include "gl_api.hpp"

#include "engine/rendering/backends/opengl/gl_utils.hpp"
#include "engine/rendering/backends/opengl/material/gl_material.hpp"
#include "engine/rendering/backends/opengl/pipeline/gl_pipeline.hpp"
#include "engine/rendering/backends/opengl/pipeline/gl_compute_pipeline.hpp"
#include "engine/rendering/pipeline/compute_pipeline.hpp"
#include "engine/rendering/backends/opengl/mesh/gl_mesh.hpp"
#include "engine/rendering/camera/camera_manager.hpp"
#include "engine/rendering/pipeline/pipeline.hpp"
#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/shader/shader.hpp"
#include "engine/rendering/lighting/light_manager.hpp"
#include "engine/rendering/texture/cubemap/envmap.hpp"
#include "engine/rendering/lighting/shadow_manager.hpp"
#include "engine/rendering/lighting/probe_manager.hpp"
#include "engine/rendering/lighting/ssao_manager.hpp"
#include "engine/rendering/lighting/light_culling_manager.hpp"
#include "engine/rendering/buffer/storage_buffer.hpp"
#include "engine/rendering/texture/texture.hpp"

#include "engine/objects/actors/actor.hpp"

#include "engine/rendering/backends/opengl/shader/gl_shader.hpp"

#include "engine/core/engine.hpp"
#include "engine/levels/level_manager.hpp"
#include "engine/debugging/profiler.hpp"

namespace Shard::Engine::Rendering{

    GLenum PrimitiveTopologyToGL(PrimitiveTopology topology)
    {
        switch (topology)
        {
            case PrimitiveTopology::Points:    return GL_POINTS;
            case PrimitiveTopology::Lines:     return GL_LINES;
            case PrimitiveTopology::LineStrip: return GL_LINE_STRIP;
            case PrimitiveTopology::Triangles: return GL_TRIANGLES;
            case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveTopology::TriangleFan: return GL_TRIANGLE_FAN;
        }

        return GL_TRIANGLES;
    }

    void GLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        glViewport(x, y, width, height);
    }

    void GLRendererAPI::SetClearColor(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
    }

    glm::vec4 GLRendererAPI::GetClearColor()
    {
        glm::vec4 color;
        glGetFloatv(GL_COLOR_CLEAR_VALUE, &color.x);
        return color;
    }

    void GLRendererAPI::Clear(ClearBit clearBits)
    {
        GLStateCache::Reset();

        GLbitfield bits = 0;

        if ((uint32_t)clearBits & (uint32_t)ClearBit::Color)
            bits |= GL_COLOR_BUFFER_BIT;

        if ((uint32_t)clearBits & (uint32_t)ClearBit::Depth){
            glDepthMask(GL_TRUE);
            bits |= GL_DEPTH_BUFFER_BIT;
        }

        if ((uint32_t)clearBits & (uint32_t)ClearBit::Stencil)
            bits |= GL_STENCIL_BUFFER_BIT;

        glClear(bits);
    }

    void GLRendererAPI::InvalidateStateCache()
    {
        GLStateCache::Reset();
    }

    void GLRendererAPI::SetDebugView(const DebugViewState& state)
    {
        m_DebugView = state;
        GLStateCache::SetForceLine(state.wireframeRaster);
    }

    std::string GLRendererAPI::GetDeviceVendor()
    {
        const GLubyte* vendor = glGetString(GL_VENDOR);
        return vendor ? reinterpret_cast<const char*>(vendor) : "Unknown";
    }

    std::string GLRendererAPI::GetRendererName()
    {
        const GLubyte* renderer = glGetString(GL_RENDERER);
        return renderer ? reinterpret_cast<const char*>(renderer) : "Unknown";
    }

    std::string GLRendererAPI::GetDriverVersion()
    {
        const GLubyte* version = glGetString(GL_VERSION);
        return version ? reinterpret_cast<const char*>(version) : "Unknown";
    }

    void GLRendererAPI::BindMesh(std::shared_ptr<Mesh> mesh)
    {
        std::shared_ptr<GLMesh> glMesh = std::static_pointer_cast<GLMesh>(mesh);
        assert(glMesh && glMesh->GetVAO() != 0 && "Invalid GLMesh");
        GLStateCache::BindVertexArray(glMesh->GetVAO());
    }

    void GLRendererAPI::BindPipeline(std::shared_ptr<Pipeline> pipeline)
    {
        std::shared_ptr<GLPipeline> glPipeline = std::static_pointer_cast<GLPipeline>(pipeline);
        glPipeline->Bind();
    }

    void GLRendererAPI::BindMaterial(std::shared_ptr<Material> mat)
    {
        std::shared_ptr<GLMaterial> glMat = std::static_pointer_cast<GLMaterial>(mat);
        glMat->Bind();
    }

    void GLRendererAPI::BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Pipeline> pipeline)
    {
        std::shared_ptr<GLShader> glShader = std::static_pointer_cast<GLShader>(pipeline->GetSpecifications().shader);
        glShader->Bind();

        uint32_t textureSlot = 0;

        for (auto& [name, texture] : pass->customSamplers)
        {
            GLStateCache::BindTextureUnit(textureSlot, texture);

            glShader->SetInt(name, textureSlot);

            textureSlot++;
        }

        for (auto& [name, value] : pass->customUniforms)
        {
            std::visit([&](auto&& v)
            {
                using T = std::decay_t<decltype(v)>;

                if constexpr (std::is_same_v<T, bool>)
                    glShader->SetBool(name, v);

                else if constexpr (std::is_same_v<T, int>)
                    glShader->SetInt(name, v);

                else if constexpr (std::is_same_v<T, float>)
                    glShader->SetFloat(name, v);

                else if constexpr (std::is_same_v<T, glm::vec2>)
                    glShader->SetVec2(name, v);

                else if constexpr (std::is_same_v<T, glm::vec3>)
                    glShader->SetVec3(name, v);

                else if constexpr (std::is_same_v<T, glm::vec4>)
                    glShader->SetVec4(name, v);

                else if constexpr (std::is_same_v<T, glm::mat2>)
                    glShader->SetMat2(name, v);

                else if constexpr (std::is_same_v<T, glm::mat3>)
                    glShader->SetMat3(name, v);

                else if constexpr (std::is_same_v<T, glm::mat4>)
                    glShader->SetMat4(name, v);

            }, value);
        }
    }

    void GLRendererAPI::BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Material> material)
    {
        for (auto& [name, texture] : pass->customSamplers){
            material->SetTextureParameter(name, texture);
        }

        for (auto& [name, value] : pass->customUniforms){
            material->SetScalarParameter(name, value);
        }
    }

    void GLRendererAPI::BindLevelState(std::shared_ptr<Shader> shader, glm::mat4 modelMatrix, int objectID, bool applyPassGlobals)
    {
        // model/objID are genuinely per-object and must always be set.
        shader->SetMat4("model", modelMatrix);
        shader->SetInt("objID", objectID);

        shader->SetVec3("emissive", glm::vec3(0.0f));

        // Editor debug view (main scene view only - studio/immediate renders always look normal).
        {
            RenderView view;
            const bool studioView = Core::GetEngine().GetRenderer()->GetCurrentView(view) && view.lighting == ViewLighting::Studio;
            const DebugViewState& dv = m_DebugView;
            shader->SetInt("viewMode", studioView ? 0 : static_cast<int>(dv.mode));
            shader->SetBool("showLighting", studioView || dv.showLighting);
            shader->SetBool("showShadows", studioView || dv.showShadows);
        }

        auto level = Core::GetEngine().GetLevelManager()->GetLevelAt(0);

        RenderView currentView;
        Core::GetEngine().GetRenderer()->GetCurrentView(currentView);
        const bool studio = currentView.lighting == ViewLighting::Studio;

        if (studio)
        {
            // Fixed studio look : none of the level's lighting settings apply, and SSAO's texture is
            // the main viewport's (screen-space), which would be meaningless for another view.
            shader->SetFloat("ambientIntensity", currentView.studioAmbient);
            shader->SetBool("ssaoEnabled", false);
        }
        else if (level)
        {
            shader->SetFloat("ambientIntensity", level->ambientIntensity);

            shader->SetBool("ssaoEnabled", level->ssaoEnabled);
            shader->SetFloat("ssaoIntensity", level->ssaoIntensity);
        }

        if (!applyPassGlobals)
            return;

        const auto& uniforms = shader->GetActiveUniformsMap();
        auto it = uniforms.find("useEnvReflections");

        // A studio view brings its own environment so its look never depends on the level.
        std::shared_ptr<EnvironmentMap> envMap;
        if (studio)
            envMap = currentView.studioEnvironment;
        else if (level && level->skybox)
            envMap = level->skybox->GetEnvMap();

        if(it != uniforms.end() && envMap){
            //Bind skybox data
            const auto& samplers = shader->GetActiveSamplersMap();

            GLStateCache::BindTextureUnit(samplers.find("ibl_irradianceMap")->second.binding, envMap->GetIrradiance()->GetHandle());

            GLStateCache::BindTextureUnit(samplers.find("ibl_prefilteredEnvMap")->second.binding, envMap->GetPrefilter()->GetHandle());

            GLStateCache::BindTextureUnit(samplers.find("ibl_brdfLUT")->second.binding, envMap->GetBRDFLUT()->GetHandle());
        }

        auto probeManager = Core::GetEngine().GetRenderer()->GetProbeManager();
        bool ddgiReady = !studio && probeManager && probeManager->IsReady();
        shader->SetBool("ddgi_enabled", ddgiReady);

        if (ddgiReady)
        {
            const auto& samplers = shader->GetActiveSamplersMap();
            int volumeCount = 0;
            int lastReady = -1;

            for (int i = 0; i < probeManager->GetActiveVolumeCount(); i++)
            {
                if (!probeManager->IsVolumeReady(i))
                    continue;

                std::string idx = "[" + std::to_string(volumeCount) + "]"; // ddgi_* uniform-array element
                std::string num = std::to_string(volumeCount);              // ddgi_*Atlas<n> scalar sampler

                auto atlasSampler = samplers.find("ddgi_irradianceAtlas" + num);
                if (atlasSampler != samplers.end())
                    GLStateCache::BindTextureUnit(atlasSampler->second.binding, probeManager->GetIrradianceAtlas(i)->GetHandle());

                auto distAtlasSampler = samplers.find("ddgi_distanceAtlas" + num);
                if (distAtlasSampler != samplers.end())
                    GLStateCache::BindTextureUnit(distAtlasSampler->second.binding, probeManager->GetDistanceAtlas(i)->GetHandle());

                auto probeStateBuffer = probeManager->GetProbeStateBuffer(i);
                if (probeStateBuffer)
                    probeStateBuffer->Bind(14 + volumeCount);

                shader->SetVec3("ddgi_gridOrigin" + idx, probeManager->GetGridOrigin(i));
                shader->SetVec3("ddgi_gridSpacing" + idx, probeManager->GetGridSpacing(i));
                glm::ivec3 counts = probeManager->GetProbeCounts(i);
                shader->SetVec3("ddgi_probeCounts" + idx, glm::vec3(counts)); // ivec3 stored as vec3, see lit.frag
                shader->SetInt("ddgi_tileSize" + idx, (int)probeManager->GetTileSize(i));
                shader->SetInt("ddgi_atlasProbesPerRow" + idx, (int)probeManager->GetAtlasProbesPerRow(i));
                shader->SetInt("ddgi_atlasSize" + idx, (int)probeManager->GetAtlasSize(i));

                lastReady = i;
                volumeCount++;
            }

            for (int s = volumeCount; lastReady >= 0 && s < kMaxProbeVolumes; s++)
            {
                std::string num = std::to_string(s);

                auto atlasSampler = samplers.find("ddgi_irradianceAtlas" + num);
                if (atlasSampler != samplers.end())
                    GLStateCache::BindTextureUnit(atlasSampler->second.binding, probeManager->GetIrradianceAtlas(lastReady)->GetHandle());

                auto distAtlasSampler = samplers.find("ddgi_distanceAtlas" + num);
                if (distAtlasSampler != samplers.end())
                    GLStateCache::BindTextureUnit(distAtlasSampler->second.binding, probeManager->GetDistanceAtlas(lastReady)->GetHandle());

                auto padStateBuffer = probeManager->GetProbeStateBuffer(lastReady);
                if (padStateBuffer)
                    padStateBuffer->Bind(14 + s);
            }

            shader->SetInt("ddgi_volumeCount", volumeCount);
        }

        shader->SetInt("lightNB", studio ? currentView.studioLightCount : Core::GetEngine().GetRenderer()->GetLightManager()->GetLightsCount());
        shader->SetVec3("camPos", currentView.position);
        auto lightCullingManager = Core::GetEngine().GetRenderer()->GetLightCullingManager();
        if (studio)
        {
            // The studio rig has no point/spot lights : a single-cluster grid whose one (empty) entry
            // ImmediateRenderer binds in place of the scene's cluster buffers.
            shader->SetUVec2("clusterGridSizeXY", 1, 1);
            shader->SetFloat("clusterScaleZ", 0.0f);
            shader->SetFloat("clusterBiasZ", 0.0f);
        }
        else if (lightCullingManager)
        {
            shader->SetUVec2("clusterGridSizeXY", lightCullingManager->GetGridSizeX(), lightCullingManager->GetGridSizeY());
            shader->SetFloat("clusterScaleZ", lightCullingManager->GetClusterScaleZ());
            shader->SetFloat("clusterBiasZ", lightCullingManager->GetClusterBiasZ());
        }
    }

    void GLRendererAPI::DrawIndexed(const std::shared_ptr<Pipeline> pipeline, uint32_t indexCount, uint32_t indexOffset)
    {
        SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::DrawElements);

        glDrawElements(
            PrimitiveTopologyToGL(pipeline->GetSpecifications().topology),
            indexCount,
            GL_UNSIGNED_INT,
            (void*)(indexOffset * sizeof(uint32_t))
        );
    }

    void GLRendererAPI::DrawFullScreenTriangle()
    {
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void GLRendererAPI::ExecuteDrawCommand(const DrawCommand &command, const std::shared_ptr<RenderPass> pass)
    {
        std::shared_ptr<Pipeline> pipeline = nullptr;

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::StateBinding);

            if(!command.fullscreenTri)
                BindMesh(command.mesh);

            if(pass->overridePipeline)
                pipeline = pass->customPipeline;
            else
                pipeline = command.material->GetPipeline();
            BindPipeline(pipeline);

            std::shared_ptr<GLShader> glShader = std::static_pointer_cast<GLShader>(pipeline->GetSpecifications().shader);
            GLuint program = glShader->GetProgram();

            if(!command.fullscreenTri)
                BindLevelState(pipeline->GetSpecifications().shader, command.modelMatrix, command.objectID,
                    GLStateCache::NeedsPassGlobalsUpdate(program, GLStateCache::PassGlobalsKind::Level));

            if(command.bindCameraState && GLStateCache::NeedsPassGlobalsUpdate(program, GLStateCache::PassGlobalsKind::Camera)){
                RenderView currentView;
                if (Core::GetEngine().GetRenderer()->GetCurrentView(currentView)) {
                    pipeline->GetSpecifications().shader->SetMat4("uProjection", currentView.projection);
                    pipeline->GetSpecifications().shader->SetMat4("uView", currentView.view);
                    pipeline->GetSpecifications().shader->SetBool("uIsOrtho", currentView.orthographic);
                }
            }

            if(pass->overridePipeline){
                BindPassData(pass, pipeline);
            }
            else{
                if(command.material->GetRecieveShadows())
                    Core::GetEngine().GetRenderer()->GetShadowManager()->BindShadowMaps(command.material);
                Core::GetEngine().GetRenderer()->GetSSAOManager()->BindSSAOTexture(command.material);
                BindPassData(pass, command.material);
                BindMaterial(command.material);
            }
        }

        if(command.fullscreenTri){
            DrawFullScreenTriangle();
        }
        else{
            if (command.indexCount == 0) return;
            DrawIndexed(pipeline, command.indexCount, command.indexOffset);
        }
    }

    void GLRendererAPI::ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        const ComputeLimits& limits = GetComputeLimits();

        uint32_t requested[3] = { groupsX, groupsY, groupsZ };
        uint32_t clamped[3] = { groupsX, groupsY, groupsZ };

        for (int i = 0; i < 3; i++)
        {
            if (limits.maxWorkGroupCount[i] > 0 && requested[i] > limits.maxWorkGroupCount[i])
                clamped[i] = limits.maxWorkGroupCount[i];
        }

        if (clamped[0] != requested[0] || clamped[1] != requested[1] || clamped[2] != requested[2])
        {
            DEBUG_WARNING("Compute dispatch (" + std::to_string(requested[0]) + ", " + std::to_string(requested[1]) + ", " + std::to_string(requested[2]) +
                ") exceeds GL_MAX_COMPUTE_WORK_GROUP_COUNT (" + std::to_string(limits.maxWorkGroupCount[0]) + ", " + std::to_string(limits.maxWorkGroupCount[1]) + ", " + std::to_string(limits.maxWorkGroupCount[2]) +
                "), clamping to fit the driver's limits.");
        }

        std::shared_ptr<GLComputePipeline> glPipeline = std::static_pointer_cast<GLComputePipeline>(pipeline);
        glPipeline->Bind();
        glDispatchCompute(clamped[0], clamped[1], clamped[2]);
    }

    const ComputeLimits& GLRendererAPI::GetComputeLimits()
    {
        if (m_ComputeLimitsQueried)
            return m_ComputeLimits;

        GLint value = 0;

        for (int i = 0; i < 3; i++)
        {
            GLint indexedValue = 0;

            glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &indexedValue);
            m_ComputeLimits.maxWorkGroupCount[i] = static_cast<uint32_t>(indexedValue);

            glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, i, &indexedValue);
            m_ComputeLimits.maxWorkGroupSize[i] = static_cast<uint32_t>(indexedValue);
        }

        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &value);
        m_ComputeLimits.maxWorkGroupInvocations = static_cast<uint32_t>(value);

        glGetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, &value);
        m_ComputeLimits.maxSharedMemorySize = static_cast<uint32_t>(value);

        m_ComputeLimitsQueried = true;

        return m_ComputeLimits;
    }

    void GLRendererAPI::MemoryBarrier(MemoryBarrierBit barriers)
    {
        GLbitfield bits = 0;

        if (barriers == MemoryBarrierBit::All)
        {
            bits = GL_ALL_BARRIER_BITS;
        }
        else
        {
            if ((uint32_t)barriers & (uint32_t)MemoryBarrierBit::ShaderStorage)
                bits |= GL_SHADER_STORAGE_BARRIER_BIT;

            if ((uint32_t)barriers & (uint32_t)MemoryBarrierBit::ImageAccess)
                bits |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;

            if ((uint32_t)barriers & (uint32_t)MemoryBarrierBit::TextureFetch)
                bits |= GL_TEXTURE_FETCH_BARRIER_BIT;

            if ((uint32_t)barriers & (uint32_t)MemoryBarrierBit::BufferUpdate)
                bits |= GL_BUFFER_UPDATE_BARRIER_BIT;

            if ((uint32_t)barriers & (uint32_t)MemoryBarrierBit::VertexAttribArray)
                bits |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;

            if ((uint32_t)barriers & (uint32_t)MemoryBarrierBit::TextureUpdate)
                bits |= GL_TEXTURE_UPDATE_BARRIER_BIT;
        }

        if (bits != 0)
            glMemoryBarrier(bits);
    }

    void GLRendererAPI::ToggleMultisampling(const bool on)
    {
        if(on)
            glEnable(GL_MULTISAMPLE);
        else
            glDisable(GL_MULTISAMPLE);
    }
}
