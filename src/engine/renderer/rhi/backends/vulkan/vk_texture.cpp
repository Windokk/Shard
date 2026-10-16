#include "vk_texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "vk_api.hpp"
#include "vk_buffer.hpp"
#include "vk_vma.hpp"

#include "engine/renderer/rhi/gpu_memory.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

namespace Shard::Engine::Rendering {

    uint16_t FloatToHalf(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));

        const uint32_t sign = (bits >> 16) & 0x8000u;
        int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
        uint32_t mantissa = bits & 0x7FFFFFu;

        if (((bits >> 23) & 0xFFu) == 0xFFu)                    // inf, nan
            return static_cast<uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));

        if (exponent >= 31)                                      // too big : infinity
            return static_cast<uint16_t>(sign | 0x7C00u);

        if (exponent <= 0)
        {
            if (exponent < -10)
                return static_cast<uint16_t>(sign);              // too small : zero
            mantissa |= 0x800000u;
            const uint32_t shift = static_cast<uint32_t>(14 - exponent);
            uint32_t half = mantissa >> shift;
            if ((mantissa >> (shift - 1)) & 1u)
                half++;
            return static_cast<uint16_t>(sign | half);
        }

        uint32_t half = sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13);
        if (mantissa & 0x1000u)
            half++;                                              // round to nearest
        return static_cast<uint16_t>(half);
    }

    float HalfToFloat(uint16_t value)
    {
        const uint32_t sign = (static_cast<uint32_t>(value) & 0x8000u) << 16;
        uint32_t exponent = (value >> 10) & 0x1Fu;
        uint32_t mantissa = value & 0x3FFu;

        uint32_t bits;
        if (exponent == 0)
        {
            if (mantissa == 0)
                bits = sign;
            else
            {
                // a subnormal half is a normal float
                exponent = 127 - 15 + 1;
                while ((mantissa & 0x400u) == 0)
                {
                    mantissa <<= 1;
                    exponent--;
                }
                mantissa &= 0x3FFu;
                bits = sign | (exponent << 23) | (mantissa << 13);
            }
        }
        else if (exponent == 31)
            bits = sign | 0x7F800000u | (mantissa << 13);
        else
            bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);

        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    namespace VK {

        /// The texels of the GPU format, made of what the caller of Texture2D::Create gave (see TextureSpecifications) : an RGB texture gets
        /// its fourth channel, float pixel data becomes half floats where the texture holds halves.
        std::vector<uint8_t> ConvertSourcePixels(const TextureSpecifications& specs, const void* data, VkFormat format, uint32_t width, uint32_t height)
        {
            const size_t texels = size_t(width) * height;
            const uint32_t channels = SourceChannels(specs.internalFormat);
            const uint8_t* source = static_cast<const uint8_t*>(data);

            std::vector<uint8_t> out(texels * TexelSize(format));

            switch (specs.internalFormat)
            {
                case TextureInternalFormat::RGB:
                case TextureInternalFormat::RGB8:
                    for (size_t i = 0; i < texels; i++)
                    {
                        out[i * 4 + 0] = source[i * 3 + 0];
                        out[i * 4 + 1] = source[i * 3 + 1];
                        out[i * 4 + 2] = source[i * 3 + 2];
                        out[i * 4 + 3] = 255;
                    }
                    break;

                case TextureInternalFormat::R16F:
                case TextureInternalFormat::RG16F:
                case TextureInternalFormat::RGB16F:
                case TextureInternalFormat::RGBA16F:
                {
                    const uint32_t gpuChannels = (specs.internalFormat == TextureInternalFormat::R16F) ? 1u : (specs.internalFormat == TextureInternalFormat::RG16F ? 2u : 4u);
                    uint16_t* destination = reinterpret_cast<uint16_t*>(out.data());

                    for (size_t i = 0; i < texels; i++)
                    {
                        for (uint32_t c = 0; c < gpuChannels; c++)
                        {
                            uint16_t value;
                            if (c < channels)
                            {
                                if (specs.halfFloatPixelData)
                                    std::memcpy(&value, source + (i * channels + c) * 2, 2);
                                else
                                {
                                    float f;
                                    std::memcpy(&f, source + (i * channels + c) * 4, 4);
                                    value = FloatToHalf(f);
                                }
                            }
                            else
                                value = FloatToHalf(1.0f);          // the alpha an RGB texture does not have
                            destination[i * gpuChannels + c] = value;
                        }
                    }
                    break;
                }

                case TextureInternalFormat::RGB32F:
                {
                    float* destination = reinterpret_cast<float*>(out.data());
                    for (size_t i = 0; i < texels; i++)
                    {
                        std::memcpy(destination + i * 4, source + i * 12, 12);
                        destination[i * 4 + 3] = 1.0f;
                    }
                    break;
                }

                case TextureInternalFormat::RGB32I:
                {
                    int32_t* destination = reinterpret_cast<int32_t*>(out.data());
                    for (size_t i = 0; i < texels; i++)
                    {
                        std::memcpy(destination + i * 4, source + i * 12, 12);
                        destination[i * 4 + 3] = 0;
                    }
                    break;
                }

                case TextureInternalFormat::Depth24:
                case TextureInternalFormat::Depth32:
                {
                    // Normalised unsigned integers (OpenGL's GL_UNSIGNED_INT depth) become the floats the depth format holds
                    float* destination = reinterpret_cast<float*>(out.data());
                    const uint32_t* integers = reinterpret_cast<const uint32_t*>(source);
                    for (size_t i = 0; i < texels; i++)
                        destination[i] = static_cast<float>(static_cast<double>(integers[i]) / 4294967295.0);
                    break;
                }

                default:
                    std::memcpy(out.data(), source, std::min(out.size(), texels * SourceTexelBytes(specs)));
                    break;
            }

            return out;
        }

        bool SupportsColorAttachment(VkFormat format)
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(Context::Get().GetPhysicalDevice(), format, &properties);
            return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
        }

        bool SupportsStorage(VkFormat format)
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(Context::Get().GetPhysicalDevice(), format, &properties);
            return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
        }

        size_t EstimateBytes(const Image& image)
        {
            size_t bytes = size_t(image.width) * image.height * TexelSize(image.format) * image.layers;
            return image.mips > 1 ? bytes + bytes / 3 : bytes;
        }
    }

    using namespace VK;

    void VKTexture2D::CreateImage(VkFormat format, uint32_t mips, bool storage, const std::string& name)
    {
        ImageCreateInfo info;
        info.width = m_Specifications.width;
        info.height = m_Specifications.height;
        info.mips = mips;
        info.format = format;
        info.name = name;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (IsDepthFormat(format))
            info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else
        {
            if (SupportsColorAttachment(format))
                info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // what the generators of the image based lighting draw into
            if (storage)
                info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        if (!m_Resource.image.Create(info))
            DEBUG_ERROR("Could not create the texture '", name, "' (", info.width, "x", info.height, ")");
    }

    VKTexture2D::VKTexture2D(TextureSpecifications& specs, const void* data)
    {
        m_Specifications = specs;

        const VkFormat format = ToVkFormat(specs.internalFormat, specs.srgb);
        const bool depth = IsDepthFormat(format);

        uint32_t mips = 1;
        if (specs.generateMips && !depth)
            mips = static_cast<uint32_t>(std::floor(std::log2(std::max({specs.width, specs.height, 1u})))) + 1;

        CreateImage(format, mips, SupportsStorage(format), "Texture2D");
        Image& image = m_Resource.image;
        if (!image.IsValid())
            return;

        Context& context = Context::Get();

        if (data && !depth)
        {
            const std::vector<uint8_t> pixels = ConvertSourcePixels(specs, data, format, image.width, image.height);

            Buffer staging;
            if (staging.Create(pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "Texture staging"))
            {
                std::memcpy(staging.mapped, pixels.data(), pixels.size());

                context.ImmediateSubmit([&](VkCommandBuffer cmd) {
                    image.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

                    VkBufferImageCopy region{};
                    region.imageSubresource = {image.aspect, 0, 0, 1};
                    region.imageExtent = {image.width, image.height, 1};
                    vkCmdCopyBufferToImage(cmd, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                    VK::GenerateMips(cmd, image, image.RestingLayout());
                });

                vmaDestroyBuffer(context.GetAllocator(), staging.buffer, staging.allocation);
            }
        }
        else
        {
            // No pixels : zeros, which is what a texture made for a compute shader to write starts as
            context.ImmediateSubmit([&](VkCommandBuffer cmd) {
                image.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

                VkImageSubresourceRange range{depth ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : image.aspect, 0, image.mips, 0, image.layers};
                if (depth)
                {
                    VkClearDepthStencilValue value{1.0f, 0};
                    vkCmdClearDepthStencilImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
                }
                else
                {
                    VkClearColorValue value{};
                    vkCmdClearColorImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
                }

                image.Transition(cmd, image.RestingLayout());
            });
        }

        m_Resource.SetSampler(MakeSamplerKey(m_Specifications, image.mips));
        m_Resource.PublishBindless();

        m_GpuBytes = static_cast<int64_t>(EstimateBytes(image));
        GpuMemory::Add(GpuMemory::Kind::Texture, m_GpuBytes);
    }

    VKTexture2D::VKTexture2D(const TextureSpecifications& specs, const CompressedImage& compressed, uint32_t firstMip)
    {
        firstMip = std::min<uint32_t>(firstMip, static_cast<uint32_t>(compressed.mips.size()) - 1);
        const uint32_t baseWidth = std::max(1u, compressed.width >> firstMip);
        const uint32_t baseHeight = std::max(1u, compressed.height >> firstMip);
        const uint32_t levels = static_cast<uint32_t>(compressed.mips.size()) - firstMip;

        m_Specifications = specs;
        m_Specifications.width = baseWidth;
        m_Specifications.height = baseHeight;

        VkFormat format = VK_FORMAT_BC7_UNORM_BLOCK;
        bool swizzle = false;
        switch (compressed.format)
        {
            case BlockFormat::BC4: format = VK_FORMAT_BC4_UNORM_BLOCK; m_Specifications.internalFormat = TextureInternalFormat::RED; break;
            case BlockFormat::BC5: format = VK_FORMAT_BC5_UNORM_BLOCK; m_Specifications.internalFormat = TextureInternalFormat::RG; swizzle = true; break;
            case BlockFormat::BC7:
                format = specs.srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
                m_Specifications.internalFormat = TextureInternalFormat::RGBA;
                break;
        }

        ImageCreateInfo info;
        info.width = baseWidth;
        info.height = baseHeight;
        info.mips = levels;
        info.format = format;
        info.name = "Compressed texture";
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.swizzleBlueToOne = swizzle;

        Image& image = m_Resource.image;
        if (!image.Create(info))
        {
            DEBUG_ERROR("Could not create a compressed texture (", baseWidth, "x", baseHeight, ")");
            return;
        }

        size_t total = 0;
        for (uint32_t level = 0; level < levels; level++)
            total += compressed.mips[firstMip + level].size();

        Buffer staging;
        if (!staging.Create(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "Texture staging"))
            return;

        std::vector<VkBufferImageCopy> regions;
        size_t offset = 0;
        uint32_t width = baseWidth, height = baseHeight;
        for (uint32_t level = 0; level < levels; level++)
        {
            const std::vector<uint8_t>& mip = compressed.mips[firstMip + level];
            std::memcpy(staging.mapped + offset, mip.data(), mip.size());

            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            region.imageExtent = {width, height, 1};
            regions.push_back(region);

            offset += mip.size();
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }

        Context& context = Context::Get();
        context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            image.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(cmd, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
            image.Transition(cmd, image.RestingLayout());
        });
        vmaDestroyBuffer(context.GetAllocator(), staging.buffer, staging.allocation);

        m_Resource.SetSampler(MakeSamplerKey(m_Specifications, levels));
        m_Resource.PublishBindless();

        m_GpuBytes = static_cast<int64_t>(total);
        GpuMemory::Add(GpuMemory::Kind::Texture, m_GpuBytes);
    }

    VKTexture2D::~VKTexture2D()
    {
        GpuMemory::Add(GpuMemory::Kind::Texture, -m_GpuBytes);
    }

    void VKTexture2D::Bind(uint32_t slot) const
    {
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->BindTextureUnit(slot, m_Resource.GetId());
    }

    void VKTexture2D::BindImage(uint32_t unit, TextureAccess access, uint32_t world) const
    {
        if (!(m_Resource.image.usage & VK_IMAGE_USAGE_STORAGE_BIT))
            DEBUG_WARNING("Texture bound as image at unit " + std::to_string(unit) + " uses a format that cannot be an image load/store target (RGB, compressed or depth formats).");

        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->BindStorageImage(unit, m_Resource.GetId(), access, world);
    }

    void VKTexture2D::ReadPixels(void* outData, size_t bufferSize, uint32_t world) const
    {
        if (!m_Resource.image.IsValid())
            return;

        const size_t required = GetPixelDataSize(world);
        if (bufferSize < required)
        {
            DEBUG_ERROR("ReadPixels buffer too small (" + std::to_string(bufferSize) + " bytes, needs " + std::to_string(required) + ") - texture " + std::to_string(GetHandle()) + " was not read back.");
            return;
        }

        Image& image = m_Resource.image;
        Context& context = Context::Get();

        // What was drawn or computed into it has to be done
        context.EnsureOutsideRendering();
        context.Flush();

        const uint32_t width = std::max(1u, image.width >> world);
        const uint32_t height = std::max(1u, image.height >> world);
        const uint32_t texel = TexelSize(image.format);
        const VkDeviceSize bytes = VkDeviceSize(width) * height * texel;

        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = bytes;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer readback = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo result{};
        if (VK_CHECK(vmaCreateBuffer(context.GetAllocator(), &bufferInfo, &allocationInfo, &readback, &allocation, &result)) != VK_SUCCESS)
            return;

        context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            const VkImageLayout before = image.layout;
            image.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            VkBufferImageCopy region{};
            region.imageSubresource = {image.IsDepth() ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : image.aspect, world, 0, 1};
            region.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);

            image.Transition(cmd, before == VK_IMAGE_LAYOUT_UNDEFINED ? image.RestingLayout() : before);
        });

        vmaInvalidateAllocation(context.GetAllocator(), allocation, 0, bytes);
        const uint8_t* gpu = static_cast<const uint8_t*>(result.pMappedData);
        uint8_t* out = static_cast<uint8_t*>(outData);
        const size_t texels = size_t(width) * height;

        std::vector<uint8_t> converted;
        const uint8_t* source = gpu;
        size_t sourceBytes = static_cast<size_t>(bytes);

        // Back to what a caller of Texture2D::Create gave : the alpha an RGB texture does not have goes, halves become floats
        switch (m_Specifications.internalFormat)
        {
            case TextureInternalFormat::RGB:
            case TextureInternalFormat::RGB8:
                converted.resize(texels * 3);
                for (size_t i = 0; i < texels; i++)
                    std::memcpy(&converted[i * 3], gpu + i * 4, 3);
                break;

            case TextureInternalFormat::RGB32F:
            case TextureInternalFormat::RGB32I:
                converted.resize(texels * 12);
                for (size_t i = 0; i < texels; i++)
                    std::memcpy(&converted[i * 12], gpu + i * 16, 12);
                break;

            case TextureInternalFormat::R16F:
            case TextureInternalFormat::RG16F:
            case TextureInternalFormat::RGB16F:
            case TextureInternalFormat::RGBA16F:
            {
                const uint32_t gpuChannels = m_Specifications.internalFormat == TextureInternalFormat::R16F ? 1u : (m_Specifications.internalFormat == TextureInternalFormat::RG16F ? 2u : 4u);
                const uint32_t channels = SourceChannels(m_Specifications.internalFormat);
                const uint16_t* halves = reinterpret_cast<const uint16_t*>(gpu);

                if (m_Specifications.halfFloatPixelData)
                {
                    converted.resize(texels * channels * 2);
                    for (size_t i = 0; i < texels; i++)
                        std::memcpy(&converted[i * channels * 2], halves + i * gpuChannels, channels * 2);
                }
                else
                {
                    converted.resize(texels * channels * 4);
                    float* floats = reinterpret_cast<float*>(converted.data());
                    for (size_t i = 0; i < texels; i++)
                        for (uint32_t c = 0; c < channels; c++)
                            floats[i * channels + c] = HalfToFloat(halves[i * gpuChannels + c]);
                }
                break;
            }

            default:
                break;
        }

        if (!converted.empty())
        {
            source = converted.data();
            sourceBytes = converted.size();
        }

        std::memcpy(out, source, std::min(sourceBytes, bufferSize));
        vmaDestroyBuffer(context.GetAllocator(), readback, allocation);
    }

    bool VKTexture2D::SwapContents(Texture2D& other)
    {
        VKTexture2D* vkOther = dynamic_cast<VKTexture2D*>(&other);
        if (!vkOther)
            return false;

        m_Resource.SwapContents(vkOther->m_Resource);
        std::swap(m_GpuBytes, vkOther->m_GpuBytes);
        std::swap(m_Specifications, vkOther->m_Specifications);
        return true;
    }

}
