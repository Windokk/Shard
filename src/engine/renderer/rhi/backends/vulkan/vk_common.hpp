#pragma once

#include <volk.h>

#include <cstdint>
#include <string>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/shader/shader_binary.hpp"

// Forward declaration. vk_mem_alloc.h is only included by vk_memory.cpp.
struct VmaAllocator_T;
struct VmaAllocation_T;
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace Shard::Engine::Rendering::VK {

    const char* ResultName(VkResult result);

    VkResult CheckResult(VkResult result, const char* expression, const char* file, int line);

    #define VK_CHECK(expression) ::Shard::Engine::Rendering::VK::CheckResult((expression), #expression, __FILE__, __LINE__)

    // RGB is widened to four channels (GPUs lack renderable three-channel formats). Depth uses the closest supported format.
    VkFormat ToVkFormat(TextureInternalFormat format, bool srgb);

    bool IsDepthFormat(VkFormat format);
    bool HasStencil(VkFormat format);
    VkImageAspectFlags AspectOf(VkFormat format);

    uint32_t TexelSize(VkFormat format);

    uint32_t SourceChannels(TextureInternalFormat format);

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

    void SetObjectName(VkObjectType type, uint64_t handle, const std::string& name);

    template<typename T>
    inline void SetObjectName(VkObjectType type, T handle, const std::string& name)
    {
        SetObjectName(type, reinterpret_cast<uint64_t>(handle), name);
    }
}
