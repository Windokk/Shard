#include "gl_api.hpp"

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

    void GLRendererAPI::BindTextureUnit(uint32_t binding, uint32_t handle)
    {
        GLStateCache::BindTextureUnit(binding, handle);
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
                m_Scene->BindWorldState(*this, pipeline->GetSpecifications().shader, command.modelMatrix, command.objectID,
                    GLStateCache::NeedsPassGlobalsUpdate(program, GLStateCache::PassGlobalsKind::World));

            if(command.bindCameraState && GLStateCache::NeedsPassGlobalsUpdate(program, GLStateCache::PassGlobalsKind::Camera)){
                RenderView currentView;
                if (m_Scene->GetCurrentView(currentView)) {
                    pipeline->GetSpecifications().shader->SetMat4("uProjection", currentView.projection);
                    pipeline->GetSpecifications().shader->SetMat4("uView", currentView.view);
                    pipeline->GetSpecifications().shader->SetBool("uIsOrtho", currentView.orthographic);
                }
            }

            if(pass->overridePipeline){
                BindPassData(pass, pipeline);
            }
            else{
                m_Scene->BindMaterialScene(command.material);
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
