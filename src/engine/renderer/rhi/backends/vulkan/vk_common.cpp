#include "vk_common.hpp"

#include "vk_context.hpp"

namespace Shard::Engine::Rendering::VK {

    const char* ResultName(VkResult result)
    {
        switch (result)
        {
            case VK_SUCCESS:                        return "VK_SUCCESS";
            case VK_NOT_READY:                      return "VK_NOT_READY";
            case VK_TIMEOUT:                        return "VK_TIMEOUT";
            case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
            case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
            case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_TOO_MANY_OBJECTS:         return "VK_ERROR_TOO_MANY_OBJECTS";
            case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "VK_ERROR_FORMAT_NOT_SUPPORTED";
            case VK_ERROR_FRAGMENTED_POOL:          return "VK_ERROR_FRAGMENTED_POOL";
            case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
            case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
            case VK_ERROR_OUT_OF_POOL_MEMORY:       return "VK_ERROR_OUT_OF_POOL_MEMORY";
            default:                                return "VK_ERROR_UNKNOWN";
        }
    }

    VkResult CheckResult(VkResult result, const char* expression, const char* file, int line)
    {
        if (result < 0)
            DEBUG_ERROR("Vulkan call failed : ", expression, " = ", ResultName(result), " (", file, ":", line, ")");
        return result;
    }

    VkFormat ToVkFormat(TextureInternalFormat format, bool srgb)
    {
        const Capabilities* caps = Context::Exists() ? &Context::Get().GetCaps() : nullptr;

        switch (format)
        {
            case TextureInternalFormat::RED:        return VK_FORMAT_R8_UNORM;
            case TextureInternalFormat::R16F:       return VK_FORMAT_R16_SFLOAT;
            case TextureInternalFormat::R32F:       return VK_FORMAT_R32_SFLOAT;
            case TextureInternalFormat::RG:         return VK_FORMAT_R8G8_UNORM;
            case TextureInternalFormat::RG16F:      return VK_FORMAT_R16G16_SFLOAT;
            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:
            case TextureInternalFormat::RGBA:
            case TextureInternalFormat::RGBA8:      return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            case TextureInternalFormat::RGB16F:
            case TextureInternalFormat::RGBA16F:    return VK_FORMAT_R16G16B16A16_SFLOAT;
            case TextureInternalFormat::RGB32F:
            case TextureInternalFormat::RGBA32F:    return VK_FORMAT_R32G32B32A32_SFLOAT;
            case TextureInternalFormat::RGB32I:
            case TextureInternalFormat::RGBA32I:    return VK_FORMAT_R32G32B32A32_SINT;
            case TextureInternalFormat::RGB9E5:     return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32;
            case TextureInternalFormat::Depth16:    return VK_FORMAT_D16_UNORM;
            case TextureInternalFormat::Depth24:
            case TextureInternalFormat::Depth32:
            case TextureInternalFormat::Depth32F:   return caps ? caps->depthFormat : VK_FORMAT_D32_SFLOAT;
            case TextureInternalFormat::Depth24Stencil8: return caps ? caps->depthStencilFormat : VK_FORMAT_D32_SFLOAT_S8_UINT;
        }
        return VK_FORMAT_R8G8B8A8_UNORM;
    }

    bool IsDepthFormat(VkFormat format)
    {
        switch (format)
        {
            case VK_FORMAT_D16_UNORM:
            case VK_FORMAT_X8_D24_UNORM_PACK32:
            case VK_FORMAT_D32_SFLOAT:
            case VK_FORMAT_D16_UNORM_S8_UINT:
            case VK_FORMAT_D24_UNORM_S8_UINT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT:
                return true;
            default:
                return false;
        }
    }

    bool HasStencil(VkFormat format)
    {
        return format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    VkImageAspectFlags AspectOf(VkFormat format)
    {
        if (!IsDepthFormat(format))
            return VK_IMAGE_ASPECT_COLOR_BIT;
        return HasStencil(format) ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    uint32_t TexelSize(VkFormat format)
    {
        switch (format)
        {
            case VK_FORMAT_R8_UNORM:                    return 1;
            case VK_FORMAT_R8G8_UNORM:
            case VK_FORMAT_R16_SFLOAT:
            case VK_FORMAT_D16_UNORM:                   return 2;
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_R16G16_SFLOAT:
            case VK_FORMAT_R32_SFLOAT:
            case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
            case VK_FORMAT_D32_SFLOAT:
            case VK_FORMAT_X8_D24_UNORM_PACK32:
            case VK_FORMAT_D24_UNORM_S8_UINT:           return 4;
            case VK_FORMAT_R16G16B16A16_SFLOAT:
            case VK_FORMAT_D32_SFLOAT_S8_UINT:          return 8;
            case VK_FORMAT_R32G32B32A32_SFLOAT:
            case VK_FORMAT_R32G32B32A32_SINT:           return 16;
            default:                                    return 4;
        }
    }

    uint32_t SourceChannels(TextureInternalFormat format)
    {
        switch (format)
        {
            case TextureInternalFormat::RED:
            case TextureInternalFormat::R16F:
            case TextureInternalFormat::R32F:
            case TextureInternalFormat::Depth16:
            case TextureInternalFormat::Depth24:
            case TextureInternalFormat::Depth32:
            case TextureInternalFormat::Depth32F:
            case TextureInternalFormat::Depth24Stencil8:
            case TextureInternalFormat::RGB9E5:         return 1;       // RGB9E5 is one packed word per texel
            case TextureInternalFormat::RG:
            case TextureInternalFormat::RG16F:          return 2;
            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:
            case TextureInternalFormat::RGB16F:
            case TextureInternalFormat::RGB32F:
            case TextureInternalFormat::RGB32I:         return 3;
            default:                                    return 4;
        }
    }

    uint32_t SourceTexelBytes(const TextureSpecifications& specs)
    {
        const uint32_t channels = SourceChannels(specs.internalFormat);

        switch (specs.internalFormat)
        {
            case TextureInternalFormat::RED:
            case TextureInternalFormat::RG:
            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:
            case TextureInternalFormat::RGBA:
            case TextureInternalFormat::RGBA8:          return channels;
            case TextureInternalFormat::R16F:
            case TextureInternalFormat::RG16F:
            case TextureInternalFormat::RGB16F:
            case TextureInternalFormat::RGBA16F:        return channels * (specs.halfFloatPixelData ? 2 : 4);
            case TextureInternalFormat::Depth16:        return 2;
            default:                                    return channels * 4;
        }
    }

    VkFilter ToVkFilter(TextureFilter filter)
    {
        switch (filter)
        {
            case TextureFilter::Nearest:
            case TextureFilter::NearestMipmapLinear:
            case TextureFilter::NearestMipmapNearest:   return VK_FILTER_NEAREST;
            default:                                    return VK_FILTER_LINEAR;
        }
    }

    VkSamplerMipmapMode ToVkMipmapMode(TextureFilter filter)
    {
        switch (filter)
        {
            case TextureFilter::LinearMipmapLinear:
            case TextureFilter::NearestMipmapLinear:    return VK_SAMPLER_MIPMAP_MODE_LINEAR;
            default:                                    return VK_SAMPLER_MIPMAP_MODE_NEAREST;
        }
    }

    bool UsesMips(TextureFilter filter)
    {
        switch (filter)
        {
            case TextureFilter::LinearMipmapLinear:
            case TextureFilter::LinearMipmapNearest:
            case TextureFilter::NearestMipmapLinear:
            case TextureFilter::NearestMipmapNearest:   return true;
            default:                                    return false;
        }
    }

    VkSamplerAddressMode ToVkWrap(TextureWrap wrap)
    {
        switch (wrap)
        {
            case TextureWrap::ClampEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case TextureWrap::ClampBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case TextureWrap::Mirror:       return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default:                        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    VkCompareOp ToVkCompare(TextureCompareFunc func)
    {
        switch (func)
        {
            case TextureCompareFunc::Always:        return VK_COMPARE_OP_ALWAYS;
            case TextureCompareFunc::Greater:       return VK_COMPARE_OP_GREATER;
            case TextureCompareFunc::LessOrEqual:   return VK_COMPARE_OP_LESS_OR_EQUAL;
            case TextureCompareFunc::Never:         return VK_COMPARE_OP_NEVER;
            default:                                return VK_COMPARE_OP_LESS;
        }
    }

    VkCompareOp ToVkCompare(DepthCompareOp op)
    {
        switch (op)
        {
            case DepthCompareOp::Always:        return VK_COMPARE_OP_ALWAYS;
            case DepthCompareOp::Greater:       return VK_COMPARE_OP_GREATER;
            case DepthCompareOp::LessOrEqual:   return VK_COMPARE_OP_LESS_OR_EQUAL;
            case DepthCompareOp::Never:         return VK_COMPARE_OP_NEVER;
            default:                            return VK_COMPARE_OP_LESS;
        }
    }

    VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology)
    {
        switch (topology)
        {
            case PrimitiveTopology::Points:         return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            case PrimitiveTopology::Lines:          return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::LineStrip:      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PrimitiveTopology::TriangleStrip:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case PrimitiveTopology::TriangleFan:    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
            default:                                return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }

    VkBlendFactor ToVkBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
            case BlendFactor::Zero:             return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One:              return VK_BLEND_FACTOR_ONE;
            case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            default:                            return VK_BLEND_FACTOR_SRC_ALPHA;
        }
    }

    VkBlendOp ToVkBlendOp(BlendOp op)
    {
        return op == BlendOp::Subtract ? VK_BLEND_OP_SUBTRACT : VK_BLEND_OP_ADD;
    }

    VkFormat ToVkVertexFormat(ShaderDataType type)
    {
        switch (type)
        {
            case ShaderDataType::Float: return VK_FORMAT_R32_SFLOAT;
            case ShaderDataType::Vec2:  return VK_FORMAT_R32G32_SFLOAT;
            case ShaderDataType::Vec3:  return VK_FORMAT_R32G32B32_SFLOAT;
            case ShaderDataType::Vec4:  return VK_FORMAT_R32G32B32A32_SFLOAT;
            case ShaderDataType::Int:
            case ShaderDataType::Bool:  return VK_FORMAT_R32_SINT;
            case ShaderDataType::UInt:  return VK_FORMAT_R32_UINT;
            case ShaderDataType::UVec2: return VK_FORMAT_R32G32_UINT;
            default:                    return VK_FORMAT_R32G32B32_SFLOAT;
        }
    }

    void SetObjectName(VkObjectType type, uint64_t handle, const std::string& name)
    {
        if (!Context::Exists() || !Context::Get().GetCaps().debugUtils || !vkSetDebugUtilsObjectNameEXT || name.empty())
            return;

        VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name.c_str();
        vkSetDebugUtilsObjectNameEXT(Context::Get().GetDevice(), &info);
    }
}
