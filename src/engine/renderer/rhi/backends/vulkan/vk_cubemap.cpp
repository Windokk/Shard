#include "vk_cubemap.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "vk_api.hpp"
#include "vk_buffer.hpp"
#include "vk_texture.hpp"
#include "vk_vma.hpp"

namespace Shard::Engine::Rendering {

    using namespace VK;

    bool VK::CreateCubeImage(Image& image, const TextureSpecifications& specs, uint32_t cubes, bool array, const std::vector<std::array<unsigned char*, 6>>& data, const char* name)
    {
        const VkFormat format = ToVkFormat(specs.internalFormat, specs.srgb);
        const bool depth = IsDepthFormat(format);

        ImageCreateInfo info;
        info.width = specs.width;
        info.height = specs.height;
        info.layers = cubes * 6;
        info.cube = true;
        info.viewType = array ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
        info.format = format;
        info.name = name;
        info.mips = (specs.generateMips && !depth) ? static_cast<uint32_t>(std::floor(std::log2(std::max({specs.width, specs.height, 1u})))) + 1 : 1;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (depth)
            info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else if (SupportsColorAttachment(format))
            info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (!depth && SupportsStorage(format))
            info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;

        if (!image.Create(info))
        {
            DEBUG_ERROR("Could not create the cubemap '", name, "' (", specs.width, "x", specs.height, ")");
            return false;
        }

        Context& context = Context::Get();

        // Which faces have pixels, and the staging buffer that holds them one after the other
        const uint32_t faceTexels = image.width * image.height;
        const uint32_t texelBytes = TexelSize(format);
        std::vector<uint32_t> layersWithData;
        for (uint32_t cube = 0; cube < data.size() && cube < cubes; cube++)
            for (uint32_t face = 0; face < 6; face++)
                if (data[cube][face])
                    layersWithData.push_back(cube * 6 + face);

        Buffer staging;
        std::vector<VkBufferImageCopy> regions;

        if (!layersWithData.empty() && !depth)
        {
            if (!staging.Create(size_t(faceTexels) * texelBytes * layersWithData.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, "Cubemap staging"))
                return false;

            size_t offset = 0;
            for (uint32_t layer : layersWithData)
            {
                const uint32_t cube = layer / 6, face = layer % 6;
                const std::vector<uint8_t> pixels = ConvertSourcePixels(specs, data[cube][face], format, image.width, image.height);
                std::memcpy(staging.mapped + offset, pixels.data(), pixels.size());

                VkBufferImageCopy region{};
                region.bufferOffset = offset;
                region.imageSubresource = {image.aspect, 0, layer, 1};
                region.imageExtent = {image.width, image.height, 1};
                regions.push_back(region);
                offset += pixels.size();
            }
        }

        context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            image.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            // Zeros (or the far depth) for what has no pixels
            VkImageSubresourceRange range{depth ? VK_IMAGE_ASPECT_DEPTH_BIT : image.aspect, 0, image.mips, 0, image.layers};
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

            if (!regions.empty())
            {
                // The clear and the copies write the same texels : the copies come after
                VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                dependency.memoryBarrierCount = 1;
                dependency.pMemoryBarriers = &barrier;
                vkCmdPipelineBarrier2(cmd, &dependency);

                vkCmdCopyBufferToImage(cmd, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
            }

            if (image.mips > 1)
                GenerateMips(cmd, image, image.RestingLayout());
            else
                image.Transition(cmd, image.RestingLayout());
        });

        if (staging.IsValid())
            vmaDestroyBuffer(context.GetAllocator(), staging.buffer, staging.allocation);

        return true;
    }

    VKCubemap::VKCubemap(const TextureSpecifications& specs, std::array<unsigned char*, 6> faces)
    {
        m_Specifications = specs;

        if (!CreateCubeImage(m_Resource.image, specs, 1, false, {faces}, "Cubemap"))
            return;

        m_Resource.SetSampler(MakeSamplerKey(specs, m_Resource.image.mips));
        m_Resource.PublishBindless();
    }

    void VKCubemap::Bind(uint32_t slot) const
    {
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->BindTextureUnit(slot, m_Resource.GetId());
    }

    VKCubemapArray::VKCubemapArray(const TextureSpecifications& specs, std::vector<std::array<unsigned char*, 6>> data)
    {
        m_Specifications = specs;
        m_Count = static_cast<uint32_t>(data.size());

        if (!CreateCubeImage(m_Resource.image, specs, m_Count, true, data, "Cubemap array"))
            return;

        m_Resource.SetSampler(MakeSamplerKey(specs, m_Resource.image.mips));
        m_Resource.PublishBindless();
    }

    void VKCubemapArray::Bind(uint32_t slot) const
    {
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->BindTextureUnit(slot, m_Resource.GetId());
    }

}
