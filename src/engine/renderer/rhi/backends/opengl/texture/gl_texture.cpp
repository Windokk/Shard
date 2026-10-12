#include "gl_texture.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"
#include "engine/renderer/rhi/gpu_memory.hpp"

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
        if (!RenderThread::Get().IsGraphicsThread())
            return RenderThread::Get().InvokeResult([glTextureID] { return GetBindlessHandle(glTextureID); });

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
        if (!RenderThread::Get().IsGraphicsThread())
            return RenderThread::Get().InvokeResult([glTextureID] { return HasBindlessHandle(glTextureID); });

        return glTextureID != 0 && s_BindlessHandles.find(glTextureID) != s_BindlessHandles.end();
    }

    void GLTexture2D::ReleaseBindlessHandle(uint32_t glTextureID)
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(ReleaseBindlessHandle(glTextureID))

        auto it = s_BindlessHandles.find(glTextureID);
        if (it == s_BindlessHandles.end())
            return;

        glMakeTextureHandleNonResidentARB(it->second);
        s_BindlessHandles.erase(it);
    }

    namespace {
        // What the driver really keeps for an uncompressed texture : RGB is padded to four channels, and a mip chain adds a third
        size_t EstimateTextureBytes(const TextureSpecifications& specs)
        {
            size_t bytesPerTexel = 4;
            switch (specs.internalFormat)
            {
                case TextureInternalFormat::RED:  bytesPerTexel = 1; break;
                case TextureInternalFormat::RGB9E5: bytesPerTexel = 4; break;
                case TextureInternalFormat::RG:
                case TextureInternalFormat::R16F:
                case TextureInternalFormat::Depth16: bytesPerTexel = 2; break;
                case TextureInternalFormat::RGB16F:
                case TextureInternalFormat::RGBA16F: bytesPerTexel = 8; break;
                case TextureInternalFormat::RGB32F:
                case TextureInternalFormat::RGB32I:
                case TextureInternalFormat::RGBA32F:
                case TextureInternalFormat::RGBA32I: bytesPerTexel = 16; break;
                default: break;
            }
            size_t bytes = size_t(specs.width) * specs.height * bytesPerTexel;
            return specs.generateMips ? bytes + bytes / 3 : bytes;
        }
    }

    GLTexture2D::GLTexture2D(const TextureSpecifications& specs, const CompressedImage& image, uint32_t firstMip)
    {
        // Only the levels from `firstMip` on go to the GPU : the image is that many times smaller (never below one level)
        firstMip = std::min<uint32_t>(firstMip, uint32_t(image.mips.size()) - 1);
        const uint32_t baseWidth = std::max(1u, image.width >> firstMip);
        const uint32_t baseHeight = std::max(1u, image.height >> firstMip);

        m_Specifications = specs;
        m_Specifications.width = baseWidth;
        m_Specifications.height = baseHeight;

        GLenum internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;
        switch (image.format)
        {
            case BlockFormat::BC4: internalFormat = GL_COMPRESSED_RED_RGTC1; m_Specifications.internalFormat = TextureInternalFormat::RED; break;
            case BlockFormat::BC5: internalFormat = GL_COMPRESSED_RG_RGTC2;  m_Specifications.internalFormat = TextureInternalFormat::RG; break;
            case BlockFormat::BC7:
                // The same blocks, read through the sRGB curve when the texture holds colours
                internalFormat = specs.srgb ? GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM : GL_COMPRESSED_RGBA_BPTC_UNORM;
                m_Specifications.internalFormat = TextureInternalFormat::RGBA;
                break;
        }
        m_GLInternalFormat = internalFormat;

        GLTextureSpec glSpecs = GLTextureSpec::FromTextureSpecifications(m_Specifications);
        const int levels = int(image.mips.size() - firstMip);

        glGenTextures(1, &ID);
        glBindTexture(GL_TEXTURE_2D, ID);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glSpecs.wrapModeS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glSpecs.wrapModeT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glSpecs.minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glSpecs.magFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);

        // Two channels (a normal map keeps only x and y, the shaders rebuild z) : the blue channel reads as 1 rather than 0, so
        // such a texture shown as an image (the asset browser) looks like the bluish normal map it is, not red and green
        if (image.format == BlockFormat::BC5)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_ONE);

        glTexStorage2D(GL_TEXTURE_2D, levels, internalFormat, baseWidth, baseHeight);

        uint32_t width = baseWidth, height = baseHeight;
        for (int level = 0; level < levels; level++)
        {
            const std::vector<uint8_t>& mip = image.mips[firstMip + level];
            glCompressedTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, width, height, internalFormat,
                                      static_cast<GLsizei>(mip.size()), mip.data());
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }

        if (levels > 1 && (GLAD_GL_ARB_texture_filter_anisotropic || GLAD_GL_EXT_texture_filter_anisotropic))
        {
            static GLfloat s_MaxAniso = []{
                GLfloat m = 1.0f;
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m);
                return m;
            }();

            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.0f, s_MaxAniso));
        }

        m_GpuBytes = 0;
        for (size_t level = firstMip; level < image.mips.size(); level++)
            m_GpuBytes += static_cast<int64_t>(image.mips[level].size());
        GpuMemory::Add(GpuMemory::Kind::Texture, m_GpuBytes);
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

        // The pixels are tightly packed (a row is width * channels bytes, whatever the width) : without this GL reads
        // each row from the next multiple of 4, so a texture whose rows aren't one (an RGB or single-channel image
        // with a width that isn't a multiple of 4) comes out skewed, row after row.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

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

        m_GpuBytes = static_cast<int64_t>(EstimateTextureBytes(specs));
        GpuMemory::Add(GpuMemory::Kind::Texture, m_GpuBytes);
    }

    void GLTexture2D::Bind(uint32_t slot) const
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
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

    void GLTexture2D::BindImage(uint32_t unit, TextureAccess access, uint32_t world) const
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
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

        glBindImageTexture(unit, ID, world, GL_FALSE, 0, glAccess, m_GLInternalFormat);
    }

    void GLTexture2D::ReadPixels(void* outData, size_t bufferSize, uint32_t world) const
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        size_t required = GetPixelDataSize(world);
        if (bufferSize < required)
        {
            DEBUG_ERROR("ReadPixels buffer too small (" + std::to_string(bufferSize) + " bytes, needs " + std::to_string(required) + ") - texture " + std::to_string(ID) + " was not read back.");
            return;
        }

        GLTextureSpec glSpecs = GLTextureSpec::FromTextureSpecifications(m_Specifications);

        glBindTexture(GL_TEXTURE_2D, ID);
        glGetTexImage(GL_TEXTURE_2D, world, glSpecs.format, glSpecs.type, outData);
    }

    bool GLTexture2D::IsValid() const
    {
        return ID != 0;     // a name was generated : the texture lives as long as this object
    }

    bool GLTexture2D::SwapContents(Texture2D& other)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        auto* glOther = dynamic_cast<GLTexture2D*>(&other);
        if (!glOther)
            return false;

        std::swap(ID, glOther->ID);
        std::swap(m_GLInternalFormat, glOther->m_GLInternalFormat);
        std::swap(m_GpuBytes, glOther->m_GpuBytes);
        std::swap(m_Specifications, glOther->m_Specifications);
        return true;
    }

    GLTexture2D::~GLTexture2D()
    {
        GpuMemory::Add(GpuMemory::Kind::Texture, -m_GpuBytes);

        const GLuint id = ID;
        RenderThread::Get().Post([id] {
            ReleaseBindlessHandle(id);
            glDeleteTextures(1, &id);
        });
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

            case TextureInternalFormat::R32F:
                glSpecs.internalFormat = GL_R32F;
                glSpecs.format = GL_RED;
                glSpecs.type = GL_FLOAT;
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
                glSpecs.internalFormat = spec.srgb ? GL_SRGB8 : GL_RGB8;
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
                glSpecs.internalFormat = spec.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
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

            // Shared exponent : the pixel data is one packed 32 bit word per texel
            case TextureInternalFormat::RGB9E5:
                glSpecs.internalFormat = GL_RGB9_E5;
                glSpecs.format = GL_RGB;
                glSpecs.type = GL_UNSIGNED_INT_5_9_9_9_REV;
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