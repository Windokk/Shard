#include "gl_cubemap.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"
#include "engine/renderer/rhi/backends/opengl/texture/gl_texture.hpp"

namespace Shard::Engine::Rendering{
    
    GLCubemap::GLCubemap(const TextureSpecifications &specs, std::array<unsigned char*, 6> faces)
    {
        m_Specifications = specs;

        glGenTextures(1, &m_ID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_ID);

        GLTextureSpec glSpecs = GLTextureSpec::FromTextureSpecifications(specs);

        // Tightly packed rows, see GLTexture2D
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        for(int i = 0; i < 6; i++){
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, glSpecs.internalFormat, specs.width, specs.height, 0, glSpecs.format, glSpecs.type, faces[i]);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, glSpecs.minFilter);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, glSpecs.magFilter);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, glSpecs.wrapModeS);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, glSpecs.wrapModeT);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, glSpecs.wrapModeR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, specs.compareMode == TextureCompareMode::CompareRefToTexture ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, glSpecs.compareFunc);

        if(specs.generateMips)
        {
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        }
    }

    void GLCubemap::Bind(uint32_t slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_ID);
    }

    bool GLCubemap::IsValid() const
    {
        return glIsTexture(m_ID);
    }

    GLCubemap::~GLCubemap()
    {
        glDeleteTextures(1, &m_ID);
    }
    
    GLCubemapArray::GLCubemapArray(const TextureSpecifications &specs, std::vector<std::array<unsigned char *, 6>> data)
    {
        m_Specifications = specs;

        m_CubemapCount = data.size();

        glGenTextures(1, &m_ID);
        glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_ID);

        GLTextureSpec glSpecs = GLTextureSpec::FromTextureSpecifications(specs);

        // Allocate storage
        int mipLevels = specs.generateMips
            ? (int)std::floor(std::log2(std::max(specs.width, specs.height))) + 1
            : 1;

        glTexStorage3D(
            GL_TEXTURE_CUBE_MAP_ARRAY,
            mipLevels,
            glSpecs.internalFormat,
            specs.width,
            specs.height,
            m_CubemapCount * 6
        );

        // Tightly packed rows, see GLTexture2D
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Upload each cubemap face
        for (int cube = 0; cube < m_CubemapCount; cube++)
        {
            for (int face = 0; face < 6; face++)
            {
                int layer = cube * 6 + face;

                if(data[cube][face]){
                    glTexSubImage3D(
                        GL_TEXTURE_CUBE_MAP_ARRAY,
                        0,
                        0,
                        0,
                        layer,
                        specs.width,
                        specs.height,
                        1,
                        glSpecs.format,
                        glSpecs.type,
                        data[cube][face]
                    );
                }
            }
        }

        // Texture parameters
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, glSpecs.minFilter);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, glSpecs.magFilter);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, glSpecs.wrapModeS);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, glSpecs.wrapModeT);
        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, glSpecs.wrapModeR);

        glTexParameteri(
            GL_TEXTURE_CUBE_MAP_ARRAY,
            GL_TEXTURE_COMPARE_MODE,
            specs.compareMode == TextureCompareMode::CompareRefToTexture
                ? GL_COMPARE_REF_TO_TEXTURE
                : GL_NONE
        );

        glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_FUNC, glSpecs.compareFunc);

        if (specs.generateMips)
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP_ARRAY);
    }

    void GLCubemapArray::Bind(uint32_t slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_ID);
    }

    bool GLCubemapArray::IsValid() const
    {
        return glIsTexture(m_ID);
    }

    uint32_t GLCubemapArray::GetCount() const
    {
        return m_CubemapCount;
    }

    GLCubemapArray::~GLCubemapArray()
    {
        glDeleteTextures(1, &m_ID);
    }
}