#include "gl_api.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/render_view.hpp"
#include "engine/renderer/rhi/material/material.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"
#include "engine/renderer/rhi/backends/opengl/material/gl_material.hpp"
#include "engine/renderer/rhi/backends/opengl/pipeline/gl_pipeline.hpp"
#include "engine/renderer/rhi/backends/opengl/pipeline/gl_compute_pipeline.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"
#include "engine/renderer/rhi/backends/opengl/mesh/gl_mesh.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"


#include "engine/renderer/rhi/backends/opengl/shader/gl_shader.hpp"

#include "engine/world/engine.hpp"
#include "engine/core/diagnostics/profiler.hpp"

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
        SHARD_ASSERT_GRAPHICS_THREAD();
        glViewport(x, y, width, height);
    }

    void GLRendererAPI::SetClearColor(float r, float g, float b, float a)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        glClearColor(r, g, b, a);
    }

    glm::vec4 GLRendererAPI::GetClearColor()
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        glm::vec4 color;
        glGetFloatv(GL_COLOR_CLEAR_VALUE, &color.x);
        return color;
    }

    void GLRendererAPI::Clear(ClearBit clearBits)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        GLStateCache::Reset();
        m_LastDraw = LastDraw{};

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

    void GLRendererAPI::BindTextureUnit(uint32_t binding, uint32_t handle)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        GLStateCache::BindTextureUnit(binding, handle);
    }

    void GLRendererAPI::InvalidateStateCache()
    {
        GLStateCache::Reset();
        m_LastDraw = LastDraw{};
    }

    void GLRendererAPI::SetDebugView(const DebugViewState& state)
    {
        m_DebugView = state;
        GLStateCache::SetForceLine(state.wireframeRaster);
    }

    std::string GLRendererAPI::GetDeviceVendor()
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        const GLubyte* vendor = glGetString(GL_VENDOR);
        return vendor ? reinterpret_cast<const char*>(vendor) : "Unknown";
    }

    std::string GLRendererAPI::GetRendererName()
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        const GLubyte* renderer = glGetString(GL_RENDERER);
        return renderer ? reinterpret_cast<const char*>(renderer) : "Unknown";
    }

    std::string GLRendererAPI::GetDriverVersion()
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
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

    void GLRendererAPI::BindMaterial(std::shared_ptr<Material> mat, std::shared_ptr<Shader> shader)
    {
        std::shared_ptr<GLMaterial> glMat = std::static_pointer_cast<GLMaterial>(mat);
        glMat->Bind(shader);
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
        // A pass gives its parameters to every material drawn in it, whose shaders do not all read them (the sky in the deferred lighting
        // pass has no G-buffer) : the ones a shader does not have are skipped, not reported
        const auto& samplers = material->GetShader()->GetActiveSamplersMap();
        const auto& uniforms = material->GetShader()->GetActiveUniformsMap();

        for (auto& [name, texture] : pass->customSamplers){
            if (samplers.find(name) != samplers.end())
                material->SetTextureParameter(name, texture);
        }

        for (auto& [name, value] : pass->customUniforms){
            if (uniforms.find(name) != uniforms.end())
                material->SetScalarParameter(name, value);
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

    void GLRendererAPI::SetIndirectDrawBuffer(uint32_t bufferHandle)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        m_IndirectBuffer = bufferHandle;
        // The binding point is only ever used for these draws, so it is bound once here rather than per draw
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, bufferHandle);
    }

    void GLRendererAPI::DrawIndexedIndirect(const std::shared_ptr<Pipeline> pipeline, uint32_t slot)
    {
        SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::DrawElements);

        // One DrawElementsIndirectCommand (5 x 32-bit) per slot, written by the occlusion-culling compute pass
        glDrawElementsIndirect(
            PrimitiveTopologyToGL(pipeline->GetSpecifications().topology),
            GL_UNSIGNED_INT,
            (const void*)(uintptr_t)((size_t)slot * 5 * sizeof(uint32_t))
        );
    }

    void GLRendererAPI::DrawFullScreenTriangle()
    {
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    bool GLRendererAPI::IsDepthPrepassCandidate(const DrawCommand& command, const RenderPass& pass)
    {
        if (command.fullscreenTri || !command.material || !command.mesh || pass.overridePipeline)
            return false;
        if (command.material->GetOpacity() != Opacity::Opaque)
            return false;

        const std::shared_ptr<Pipeline>& pipeline = command.material->GetPipeline();
        if (!pipeline)
            return false;
        const PipelineSpecifications& specs = pipeline->GetSpecifications();
        if (!specs.depthTest || !specs.depthWrite || specs.blending || specs.polygonMode != PolygonMode::Fill || !specs.shader)
            return false;

        return specs.shader->SupportsDepthPrepass();
    }

    void GLRendererAPI::SetDepthSweep(DepthSweep sweep)
    {
        if (m_Sweep == DepthSweep::Prepass && sweep != DepthSweep::Prepass)
        {
            // The shaders go back to shading : the switch is a program uniform, it would stay on for any later draw
            for (const std::shared_ptr<Shader>& shader : m_PrepassShaders)
            {
                std::static_pointer_cast<GLShader>(shader)->Bind();
                shader->SetBool("depthPrepass", false);
            }
            m_PrepassShaders.clear();
        }

        m_Sweep = sweep;

        switch (sweep)
        {
            case DepthSweep::None:
                GLStateCache::SetDepthMode(GLStateCache::DepthMode::Normal);
                GLStateCache::SetColorWrite(true);
                break;
            case DepthSweep::Prepass:
                GLStateCache::SetDepthMode(GLStateCache::DepthMode::Prepass);
                GLStateCache::SetColorWrite(false);
                break;
            case DepthSweep::Equal:
                GLStateCache::SetColorWrite(true);
                break;      // the mode follows each draw (a candidate or not)
        }

        // The programs, depth state and texture units the sweep left bound are not what the next one expects
        InvalidateStateCache();
    }

    void GLRendererAPI::ExecuteDepthOnlyDraw(const DrawCommand& command)
    {
        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::StateBinding);

            BindMesh(command.mesh);

            std::shared_ptr<Pipeline> pipeline = command.material->GetPipeline();
            BindPipeline(pipeline);

            std::shared_ptr<Shader> shader = pipeline->GetSpecifications().shader;
            const GLuint program = std::static_pointer_cast<GLShader>(shader)->GetProgram();

            if (GLStateCache::NeedsPassGlobalsUpdate(program, GLStateCache::PassGlobalsKind::DepthSweep))
            {
                shader->SetBool("depthPrepass", true);
                if (std::find(m_PrepassShaders.begin(), m_PrepassShaders.end(), shader) == m_PrepassShaders.end())
                    m_PrepassShaders.push_back(shader);
            }

            if (command.bindCameraState && GLStateCache::NeedsPassGlobalsUpdate(program, GLStateCache::PassGlobalsKind::Camera))
            {
                RenderView currentView;
                if (m_Scene->GetCurrentView(currentView))
                {
                    shader->SetMat4("uProjection", currentView.projection);
                    shader->SetMat4("uView", currentView.view);
                    shader->SetBool("uIsOrtho", currentView.orthographic);
                }
            }

            if (m_LastDraw.pipeline != pipeline.get() || m_LastDraw.modelMatrix != command.modelMatrix)
                shader->SetMat4("model", command.modelMatrix);

            m_LastDraw.pass = nullptr;
            m_LastDraw.pipeline = pipeline.get();
            m_LastDraw.material = nullptr;
            m_LastDraw.modelMatrix = command.modelMatrix;
        }

        if (command.indexCount == 0)
            return;

        if (m_IndirectBuffer != 0 && command.cullSlot >= 0)
            DrawIndexedIndirect(command.material->GetPipeline(), (uint32_t)command.cullSlot);
        else
            DrawIndexed(command.material->GetPipeline(), command.indexCount, command.indexOffset);
    }

    void GLRendererAPI::BindLightmap(const DrawCommand& command, const std::shared_ptr<Shader>& shader)
    {
        // A uniform the program does not have is a no-op (see GLShader::GetUniformLocationCached), so every shader can be
        // given these : only the lit one reads them
        shader->SetBool("lightmapEnabled", command.lightmapHandle != 0);
        if (command.lightmapHandle != 0)
        {
            shader->SetUVec2("lightmapHandle", uint32_t(command.lightmapHandle & 0xFFFFFFFFu), uint32_t(command.lightmapHandle >> 32));
            shader->SetVec4("lightmapScaleOffset", command.lightmapScaleOffset);
        }
    }

    void GLRendererAPI::ExecuteDrawCommand(const DrawCommand &command, const std::shared_ptr<RenderPass> pass)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        if (m_Sweep == DepthSweep::Prepass)
        {
            ExecuteDepthOnlyDraw(command);
            return;
        }

        if (m_Sweep == DepthSweep::Equal)
            GLStateCache::SetDepthMode(IsDepthPrepassCandidate(command, *pass) ? GLStateCache::DepthMode::Equal : GLStateCache::DepthMode::Normal);

        std::shared_ptr<Pipeline> pipeline = nullptr;

        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::StateBinding);

            if(!command.fullscreenTri)
                BindMesh(command.mesh);

            if(pass->overridePipeline)
                pipeline = pass->customPipeline;
            else
            {
                pipeline = command.material->GetPipeline();

                // A pass that draws the materials with another permutation of their shader (the G-buffer of the deferred pipeline)
                if(!pass->shaderVariant.empty())
                {
                    if(std::shared_ptr<Pipeline> variant = pipeline->GetVariant(pass->shaderVariant))
                        pipeline = variant;
                }
            }
            BindPipeline(pipeline);

            if(!command.fullscreenTri && command.material && command.material->IsDoubleSided())
                GLStateCache::SetCullMode(false, GL_BACK);

            std::shared_ptr<GLShader> glShader = std::static_pointer_cast<GLShader>(pipeline->GetSpecifications().shader);
            GLuint program = glShader->GetProgram();

            const Material* drawMaterial = pass->overridePipeline ? nullptr : command.material.get();
            const bool sameAsLastDraw = !command.fullscreenTri
                && m_LastDraw.pass == pass.get()
                && m_LastDraw.pipeline == pipeline.get()
                && m_LastDraw.material == drawMaterial;

            if(!command.fullscreenTri)
            {
                if(sameAsLastDraw)
                {
                    // Nothing scene-wide or material-specific can have changed since the previous draw : only
                    // what is genuinely per-object needs re-uploading.
                    if(command.modelMatrix != m_LastDraw.modelMatrix)
                        pipeline->GetSpecifications().shader->SetMat4("model", command.modelMatrix);
                    if(command.objectID != m_LastDraw.objectID)
                        pipeline->GetSpecifications().shader->SetInt("objID", command.objectID);
                    if(command.lightmapHandle != m_LastDraw.lightmapHandle || command.lightmapScaleOffset != m_LastDraw.lightmapScaleOffset)
                        BindLightmap(command, pipeline->GetSpecifications().shader);
                }
                else
                {
                    m_Scene->BindWorldState(*this, pipeline->GetSpecifications().shader, command.modelMatrix, command.objectID,
                        GLStateCache::NeedsPassGlobalsUpdate(program, GLStateCache::PassGlobalsKind::World));
                    BindLightmap(command, pipeline->GetSpecifications().shader);
                }

                m_LastDraw.pass = pass.get();
                m_LastDraw.pipeline = pipeline.get();
                m_LastDraw.material = drawMaterial;
                m_LastDraw.modelMatrix = command.modelMatrix;
                m_LastDraw.objectID = command.objectID;
                m_LastDraw.lightmapHandle = command.lightmapHandle;
                m_LastDraw.lightmapScaleOffset = command.lightmapScaleOffset;
            }
            else
            {
                m_LastDraw = LastDraw{};
            }

            if(command.bindCameraState && GLStateCache::NeedsPassGlobalsUpdate(program, GLStateCache::PassGlobalsKind::Camera)){
                RenderView currentView;
                if (m_Scene->GetCurrentView(currentView)) {
                    pipeline->GetSpecifications().shader->SetMat4("uProjection", currentView.projection);
                    pipeline->GetSpecifications().shader->SetMat4("uView", currentView.view);
                    pipeline->GetSpecifications().shader->SetBool("uIsOrtho", currentView.orthographic);
                }
            }

            if(sameAsLastDraw){
                // Same pass, pipeline and material as the draw before : its uniforms and texture units are still bound
            }
            else if(pass->overridePipeline){
                BindPassData(pass, pipeline);
            }
            else{
                m_Scene->BindMaterialScene(command.material);
                BindPassData(pass, command.material);
                BindMaterial(command.material, pipeline->GetSpecifications().shader);
            }
        }

        if(command.fullscreenTri){
            if(command.proceduralVertexCount > 0)
            {
                if(command.instanceCount > 0)
                    glDrawArraysInstanced(GL_TRIANGLES, 0, GLsizei(command.proceduralVertexCount), GLsizei(command.instanceCount));
            }
            else
                DrawFullScreenTriangle();
        }
        else{
            if (command.indexCount == 0) return;

            if (m_IndirectBuffer != 0 && command.cullSlot >= 0)
                DrawIndexedIndirect(pipeline, (uint32_t)command.cullSlot);
            else
                DrawIndexed(pipeline, command.indexCount, command.indexOffset);
        }
    }

    void GLRendererAPI::ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
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
        if (!m_ComputeLimitsQueried && !RenderThread::Get().IsGraphicsThread())
            RenderThread::Get().Invoke([this] { GetComputeLimits(); });     // the first call asks the driver
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

    void GLRendererAPI::PushDebugGroup(const char* name)
    {
        // KHR_debug is core since 4.3 : what RenderDoc and the driver's debug output read
        if (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug)
            glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
    }

    void GLRendererAPI::PopDebugGroup()
    {
        if (GLAD_GL_VERSION_4_3 || GLAD_GL_KHR_debug)
            glPopDebugGroup();
    }

    void GLRendererAPI::MemoryBarrier(MemoryBarrierBit barriers)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
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

            if ((uint32_t)barriers & (uint32_t)MemoryBarrierBit::Command)
                bits |= GL_COMMAND_BARRIER_BIT;
        }

        if (bits != 0)
            glMemoryBarrier(bits);
    }

    void GLRendererAPI::ToggleMultisampling(const bool on)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        if(on)
            glEnable(GL_MULTISAMPLE);
        else
            glDisable(GL_MULTISAMPLE);
    }
}

namespace Shard::Engine::Rendering{

    bool GLRendererAPI::SupportsBlockCompression()
    {
        // BC7 is BPTC (core since 4.2), BC4 / BC5 are RGTC (core since 3.0) ; glTexStorage2D needs 4.2 as well
        return GLAD_GL_VERSION_4_2 || (GLAD_GL_ARB_texture_compression_bptc && GLAD_GL_ARB_texture_storage);
    }

    GpuProfiler* GLRendererAPI::GetGpuProfiler()
    {
        if (!m_GpuProfiler)
            m_GpuProfiler = std::make_unique<GLGpuProfiler>();
        return m_GpuProfiler.get();
    }
}
