#pragma once

// What every file of the Vulkan backend shares : the loader, the allocator types, the error check and the format tables.
// Nothing outside renderer/rhi/backends/vulkan includes this (the rest of the engine only knows the rhi interfaces).

#include <volk.h>

#include <cstdint>
#include <string>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/shader/shader_binary.hpp"

// VMA's handles, without pulling its 20 000 lines into every file (vk_mem_alloc.h is only included by vk_memory.cpp)
struct VmaAllocator_T;
struct VmaAllocation_T;
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace Shard::Engine::Rendering::VK {

    const char* ResultName(VkResult result);

    /// Logs a failed Vulkan call with where it was made. The value of the expression is the result, so that a caller can still react.
    VkResult CheckResult(VkResult result, const char* expression, const char* file, int line);

    #define VK_CHECK(expression) ::Shard::Engine::Rendering::VK::CheckResult((expression), #expression, __FILE__, __LINE__)

    /// The format a texture of the rhi is stored in. RGB formats are widened (a GPU has few three channel formats, and none it can render to)
    /// and a depth format is the closest one the device supports.
    VkFormat ToVkFormat(TextureInternalFormat format, bool srgb);

    bool IsDepthFormat(VkFormat format);
    bool HasStencil(VkFormat format);
    VkImageAspectFlags AspectOf(VkFormat format);

    /// Bytes of one texel of an uncompressed format
    uint32_t TexelSize(VkFormat format);

    /// How many channels the CPU side of a texture of this format has per pixel (what `data` holds : an RGB texture has three, although the GPU has four)
    uint32_t SourceChannels(TextureInternalFormat format);

    /// The size of one source texel in bytes, as the pixel data of Texture2D::Create holds it
    uint32_t SourceTexelBytes(const TextureSpecifications& specs);

    VkFilter ToVkFilter(TextureFilter filter);
    VkSamplerMipmapMode ToVkMipmapMode(TextureFilter filter);
    bool UsesMips(TextureFilter filter);
    VkSamplerAddressMode ToVkWrap(TextureWrap wrap);
    VkCompareOp ToVkCompare(TextureCompareFunc func);
    VkCompareOp ToVkCompare(DepthCompareOp op);

    VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology);
    VkBlendFactor ToVkBlendFactor(BlendFactor factor);
    VkBlendOp ToVkBlendOp(BlendOp op);
    VkFormat ToVkVertexFormat(ShaderDataType type);

    inline uint64_t HashCombine64(uint64_t seed, uint64_t value)
    {
        seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }

    /// Names a Vulkan object for the validation layers and RenderDoc (no-op when the debug utils are not there)
    void SetObjectName(VkObjectType type, uint64_t handle, const std::string& name);

    template<typename T>
    inline void SetObjectName(VkObjectType type, T handle, const std::string& name)
    {
        SetObjectName(type, reinterpret_cast<uint64_t>(handle), name);
    }
}
