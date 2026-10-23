#include "gl_texture_array.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/rhi/gpu_memory.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"
#include "engine/renderer/rhi/backends/opengl/texture/gl_texture.hpp"

namespace Shard::Engine::Rendering{

    GLTexture2DArray::GLTexture2DArray(const TextureSpecifications& specs, uint32_t layers)
    {
        m_Specifications = specs;
        m_Layers = layers;

        glGenTextures(1, &m_ID);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_ID);

        GLTextureSpec glSpecs = GLTextureSpec::FromTextureSpecifications(specs);

        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, glSpecs.internalFormat, specs.width, specs.height, layers);

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, glSpecs.minFilter);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, glSpecs.magFilter);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, glSpecs.wrapModeS);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, glSpecs.wrapModeT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, specs.compareMode == TextureCompareMode::CompareRefToTexture ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, glSpecs.compareFunc);
        if(m_Specifications.borderColor != COL_RGBA(-1.0f)){
            COL_RGBA& border = m_Specifications.borderColor;
            float borderColor[] = {border.r(), border.g(), border.b(), border.a()};
            glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
        }

        // A layer no pass has drawn into yet reads as "nothing there" : the far depth, or zeros
        if (specs.format == TextureFormat::Depth)
        {
            const float far = 1.0f;
            glClearTexImage(m_ID, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &far);
        }
        else
        {
            glClearTexImage(m_ID, 0, glSpecs.format, glSpecs.type, nullptr);
        }

        const int64_t texelBytes = specs.internalFormat == TextureInternalFormat::RGBA16F || specs.internalFormat == TextureInternalFormat::RGB16F ? 8 :
                                   specs.internalFormat == TextureInternalFormat::RGBA32F || specs.internalFormat == TextureInternalFormat::RGB32F ? 16 : 4;
        m_GpuBytes = int64_t(specs.width) * int64_t(specs.height) * int64_t(layers) * texelBytes;
        GpuMemory::Add(GpuMemory::Kind::RenderTarget, m_GpuBytes);
    }

    void GLTexture2DArray::Bind(uint32_t slot) const
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_ID);
    }

    GLTexture2DArray::~GLTexture2DArray()
    {
        GpuMemory::Add(GpuMemory::Kind::RenderTarget, -m_GpuBytes);
        const GLuint id = m_ID;
        RenderThread::Get().Post([id] { glDeleteTextures(1, &id); });
    }
}
