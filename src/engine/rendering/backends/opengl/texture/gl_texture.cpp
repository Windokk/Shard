#include "gl_texture.hpp"

#include "engine/rendering/backends/opengl/gl_utils.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Shard::Engine::Rendering{

    namespace {
        std::unordered_map<uint32_t, uint64_t> s_BindlessHandles;
        bool s_WarnedBindlessUnsupported = false;
    }

    uint64_t GLTexture2D::GetBindlessHandle(uint32_t glTextureID)
    {
        if (glTextureID == 0)
            return 0;

        auto it = s_BindlessHandles.find(glTextureID);
        if (it != s_BindlessHandles.end())
            return it->second;

        if (!GLAD_GL_ARB_bindless_texture)
        {
            if (!s_WarnedBindlessUnsupported)
            {
                DEBUG_WARNING("GL_ARB_bindless_texture is not supported on this GPU/driver - raytraced materials will render without textures.");
                s_WarnedBindlessUnsupported = true;
            }
            return 0;
        }

        GLuint64 handle = glGetTextureHandleARB(glTextureID);
        if (handle == 0)
            return 0;

        glMakeTextureHandleResidentARB(handle);
        s_BindlessHandles[glTextureID] = handle;
        return handle;
    }

    bool GLTexture2D::HasBindlessHandle(uint32_t glTextureID)
    {
        return glTextureID != 0 && s_BindlessHandles.find(glTextureID) != s_BindlessHandles.end();
    }

    void GLTexture2D::ReleaseBindlessHandle(uint32_t glTextureID)
    {
        auto it = s_BindlessHandles.find(glTextureID);
        if (it == s_BindlessHandles.end())
            return;

        glMakeTextureHandleNonResidentARB(it->second);
        s_BindlessHandles.erase(it);
    }

    GLTexture2D::GLTexture2D(TextureSpecifications &specs, const void *data)
    {
        m_Specifications = specs;

        glGenTextures(1, &ID);
        glBindTexture(GL_TEXTURE_2D, ID);

        GLTextureSpec glSpecs = GLTextureSpec::FromTextureSpecifications(specs);
        m_GLInternalFormat = glSpecs.internalFormat;

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glSpecs.wrapModeS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glSpecs.wrapModeT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glSpecs.minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glSpecs.magFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, specs.compareMode == TextureCompareMode::CompareRefToTexture ? GL_COMPARE_REF_TO_TEXTURE : GL_NONE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, glSpecs.compareFunc);
        if(specs.borderColor != COL_RGBA(-1.0f)){
            float borderColor[] = {specs.borderColor.r(), specs.borderColor.g(), specs.borderColor.b(), specs.borderColor.a()};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        }

        if (specs.immutableStorage)
        {
            int mipLevels = specs.generateMips
                ? (int)std::floor(std::log2(std::max(specs.width, specs.height))) + 1
                : 1;

            glTexStorage2D(GL_TEXTURE_2D, mipLevels, glSpecs.internalFormat, specs.width, specs.height);

            if (data)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, specs.width, specs.height, glSpecs.format, glSpecs.type, data);
        }
        else
        {
            glTexImage2D(GL_TEXTURE_2D, 0, glSpecs.internalFormat, specs.width, specs.height, 0, glSpecs.format, glSpecs.type, data);
        }

        if(specs.generateMips)
        {
            glGenerateMipmap(GL_TEXTURE_2D);

            if (GLAD_GL_ARB_texture_filter_anisotropic || GLAD_GL_EXT_texture_filter_anisotropic)
            {
                static GLfloat s_MaxAniso = []{
                    GLfloat m = 1.0f;
                    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m);
                    return m;
                }();

                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.0f, s_MaxAniso));
            }
        }
    }

    void GLTexture2D::Bind(uint32_t slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, ID);
    }

    static bool IsImageLoadStoreFormat(uint32_t glInternalFormat)
    {
        switch (glInternalFormat)
        {
            case GL_RGBA32F: case GL_RGBA16F: case GL_RG32F: case GL_RG16F:
            case GL_R32F: case GL_R16F:
            case GL_RGBA32UI: case GL_RGBA16UI: case GL_RGBA8UI:
            case GL_RG32UI: case GL_RG16UI: case GL_RG8UI:
            case GL_R32UI: case GL_R16UI: case GL_R8UI:
            case GL_RGBA32I: case GL_RGBA16I: case GL_RGBA8I:
            case GL_RG32I: case GL_RG16I: case GL_RG8I:
            case GL_R32I: case GL_R16I: case GL_R8I:
            case GL_RGBA16: case GL_RGBA8: case GL_RG16: case GL_RG8: case GL_R16: case GL_R8:
            case GL_RGBA16_SNORM: case GL_RGBA8_SNORM: case GL_RG16_SNORM: case GL_RG8_SNORM:
            case GL_R16_SNORM: case GL_R8_SNORM:
                return true;
            default:
                return false;
        }
    }

    void GLTexture2D::BindImage(uint32_t unit, TextureAccess access, uint32_t level) const
    {
        if (!IsImageLoadStoreFormat(m_GLInternalFormat))
        {
            DEBUG_WARNING("Texture bound as image at unit " + std::to_string(unit) + " uses a format not supported by image load/store (e.g. RGB/RGB32F/RGB32I or depth formats aren't allowed - use RGBA32F, RGBA8, R32F, etc).");
        }

        GLenum glAccess = GL_READ_WRITE;
        switch (access)
        {
            case TextureAccess::ReadOnly:  glAccess = GL_READ_ONLY; break;
            case TextureAccess::WriteOnly: glAccess = GL_WRITE_ONLY; break;
            case TextureAccess::ReadWrite: glAccess = GL_READ_WRITE; break;
        }

        glBindImageTexture(unit, ID, level, GL_FALSE, 0, glAccess, m_GLInternalFormat);
    }

    void GLTexture2D::ReadPixels(void* outData, size_t bufferSize, uint32_t level) const
    {
        size_t required = GetPixelDataSize(level);
        if (bufferSize < required)
        {
            DEBUG_ERROR("ReadPixels buffer too small (" + std::to_string(bufferSize) + " bytes, needs " + std::to_string(required) + ") - texture " + std::to_string(ID) + " was not read back.");
            return;
        }

        GLTextureSpec glSpecs = GLTextureSpec::FromTextureSpecifications(m_Specifications);

        glBindTexture(GL_TEXTURE_2D, ID);
        glGetTexImage(GL_TEXTURE_2D, level, glSpecs.format, glSpecs.type, outData);
    }

    bool GLTexture2D::IsValid() const
    {
        return glIsTexture(ID);
    }

    GLTexture2D::~GLTexture2D()
    {
        ReleaseBindlessHandle(ID);

        glDeleteTextures(1, &ID);
    }

    GLTextureSpec GLTextureSpec::FromTextureSpecifications(const TextureSpecifications &spec)
    {
        GLTextureSpec glSpecs{};

        // Filtering
        glSpecs.magFilter = (spec.magFilter == TextureFilter::Nearest) ? GL_NEAREST : GL_LINEAR;

        switch (spec.minFilter)
        {
            case TextureFilter::Linear:                 glSpecs.minFilter = GL_LINEAR; break;
            case TextureFilter::LinearMipmapLinear:     glSpecs.minFilter = GL_LINEAR_MIPMAP_LINEAR; break;
            case TextureFilter::LinearMipmapNearest:    glSpecs.minFilter = GL_LINEAR_MIPMAP_NEAREST; break;
            case TextureFilter::Nearest:                glSpecs.minFilter = GL_NEAREST; break;
            case TextureFilter::NearestMipmapNearest:   glSpecs.minFilter = GL_NEAREST_MIPMAP_NEAREST; break;
            case TextureFilter::NearestMipmapLinear:    glSpecs.minFilter = GL_NEAREST_MIPMAP_LINEAR; break;
            default:                                    glSpecs.minFilter = GL_LINEAR; break;
        }

        // Wrapping
        auto WrapToGL = [](TextureWrap wrap)
        {
            switch (wrap)
            {
                case TextureWrap::ClampEdge:   return GL_CLAMP_TO_EDGE;
                case TextureWrap::ClampBorder: return GL_CLAMP_TO_BORDER;
                case TextureWrap::Mirror:      return GL_MIRRORED_REPEAT; // fixed
                case TextureWrap::Repeat:      return GL_REPEAT;
            }
            return GL_REPEAT;
        };

        glSpecs.wrapModeS = WrapToGL(spec.wrapS);
        glSpecs.wrapModeT = WrapToGL(spec.wrapT);
        glSpecs.wrapModeR = WrapToGL(spec.wrapR);

        // Internal Format + Format + Type
        switch (spec.internalFormat)
        {
            // R 
            case TextureInternalFormat::RED:
                glSpecs.internalFormat = GL_R8;
                glSpecs.format = GL_RED;
                glSpecs.type = GL_UNSIGNED_BYTE;
                break;

            case TextureInternalFormat::R16F:
                glSpecs.internalFormat = GL_R16F;
                glSpecs.format = GL_RED;
                glSpecs.type = spec.halfFloatPixelData ? GL_HALF_FLOAT : GL_FLOAT;
                break;

            // RG
            case TextureInternalFormat::RG:
                glSpecs.internalFormat = GL_RG8;
                glSpecs.format = GL_RG;
                glSpecs.type = GL_UNSIGNED_BYTE;
                break;

            case TextureInternalFormat::RG16F:
                glSpecs.internalFormat = GL_RG16F;
                glSpecs.format = GL_RG;
                glSpecs.type = spec.halfFloatPixelData ? GL_HALF_FLOAT : GL_FLOAT;
                break;

            // RGB
            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:
                glSpecs.internalFormat = GL_RGB8;
                glSpecs.format = GL_RGB;
                glSpecs.type = GL_UNSIGNED_BYTE;
                break;

            case TextureInternalFormat::RGB16F:
                glSpecs.internalFormat = GL_RGB16F;
                glSpecs.format = GL_RGB;
                glSpecs.type = spec.halfFloatPixelData ? GL_HALF_FLOAT : GL_FLOAT;
                break;

            case TextureInternalFormat::RGB32F:
                glSpecs.internalFormat = GL_RGB32F;
                glSpecs.format = GL_RGB;
                glSpecs.type = GL_FLOAT;
                break;

            case TextureInternalFormat::RGB32I:
                glSpecs.internalFormat = GL_RGB32I;
                glSpecs.format = GL_RGB_INTEGER;
                glSpecs.type = GL_INT;
                break;

            // RGBA
            case TextureInternalFormat::RGBA:
            case TextureInternalFormat::RGBA8:
                glSpecs.internalFormat = GL_RGBA8;
                glSpecs.format = GL_RGBA;
                glSpecs.type = GL_UNSIGNED_BYTE;
                break;

            case TextureInternalFormat::RGBA16F:
                glSpecs.internalFormat = GL_RGBA16F;
                glSpecs.format = GL_RGBA;
                glSpecs.type = spec.halfFloatPixelData ? GL_HALF_FLOAT : GL_FLOAT;
                break;

            case TextureInternalFormat::RGBA32F:
                glSpecs.internalFormat = GL_RGBA32F;
                glSpecs.format = GL_RGBA;
                glSpecs.type = GL_FLOAT;
                break;

            case TextureInternalFormat::RGBA32I:
                glSpecs.internalFormat = GL_RGBA32I;
                glSpecs.format = GL_RGBA_INTEGER;
                glSpecs.type = GL_INT;
                break;

            // Depth
            case TextureInternalFormat::Depth16:
                glSpecs.internalFormat = GL_DEPTH_COMPONENT16;
                glSpecs.format = GL_DEPTH_COMPONENT;
                glSpecs.type = GL_UNSIGNED_SHORT;
                break;

            case TextureInternalFormat::Depth24:
                glSpecs.internalFormat = GL_DEPTH_COMPONENT24;
                glSpecs.format = GL_DEPTH_COMPONENT;
                glSpecs.type = GL_UNSIGNED_INT;
                break;

            case TextureInternalFormat::Depth32:
                glSpecs.internalFormat = GL_DEPTH_COMPONENT32;
                glSpecs.format = GL_DEPTH_COMPONENT;
                glSpecs.type = GL_UNSIGNED_INT;
                break;

            case TextureInternalFormat::Depth32F:
                glSpecs.internalFormat = GL_DEPTH_COMPONENT32F;
                glSpecs.format = GL_DEPTH_COMPONENT;
                glSpecs.type = GL_FLOAT;
                break;

            case TextureInternalFormat::Depth24Stencil8:
                glSpecs.internalFormat = GL_DEPTH24_STENCIL8;
                glSpecs.format = GL_DEPTH_STENCIL;
                glSpecs.type = GL_UNSIGNED_INT_24_8;
                break;

            default:
                glSpecs.internalFormat = GL_RGBA8;
                glSpecs.format = GL_RGBA;
                glSpecs.type = GL_UNSIGNED_BYTE;
                break;
        }

        // Compare func (for shadow maps)
        switch (spec.compareFunc)
        {
            case TextureCompareFunc::Always:      glSpecs.compareFunc = GL_ALWAYS; break;
            case TextureCompareFunc::Greater:     glSpecs.compareFunc = GL_GREATER; break;
            case TextureCompareFunc::Less:        glSpecs.compareFunc = GL_LESS; break;
            case TextureCompareFunc::LessOrEqual: glSpecs.compareFunc = GL_LEQUAL; break;
            case TextureCompareFunc::Never:       glSpecs.compareFunc = GL_NEVER; break;
            default:                             glSpecs.compareFunc = GL_LESS; break;
        }

        return glSpecs;
    }

}