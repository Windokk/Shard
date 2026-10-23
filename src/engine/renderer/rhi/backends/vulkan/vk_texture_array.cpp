#include "vk_texture_array.hpp"

#include "engine/renderer/rhi/gpu_memory.hpp"

#include "vk_api.hpp"
#include "vk_texture.hpp"

namespace Shard::Engine::Rendering {

    using namespace VK;

    VKTexture2DArray::VKTexture2DArray(const TextureSpecifications& specs, uint32_t layers)
    {
        m_Specifications = specs;
        m_Layers = layers;

        const VkFormat format = ToVkFormat(specs.internalFormat, specs.srgb);
        const bool depth = IsDepthFormat(format);

        ImageCreateInfo info;
        info.width = specs.width;
        info.height = specs.height;
        info.layers = layers;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        info.format = format;
        info.name = "Texture array";
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (depth)
            info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else if (SupportsColorAttachment(format))
            info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        Image& image = m_Resource.image;
        if (!image.Create(info))
        {
            DEBUG_ERROR("Could not create a texture array (", specs.width, "x", specs.height, ", ", layers, " layers)");
            return;
        }

        // A layer no pass has drawn into yet reads as "nothing there" : the far depth, or zeros
        Context::Get().ImmediateSubmit([&](VkCommandBuffer cmd) {
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

            image.TransitionToResting(cmd);
        });

        // Not in the bindless array : its slots are sampler2D, which an array view cannot stand for
        m_Resource.SetSampler(MakeSamplerKey(specs, image.mips));

        m_GpuBytes = int64_t(image.width) * int64_t(image.height) * int64_t(image.layers) * int64_t(TexelSize(format));
        GpuMemory::Add(GpuMemory::Kind::RenderTarget, m_GpuBytes);
    }

    VKTexture2DArray::~VKTexture2DArray()
    {
        GpuMemory::Add(GpuMemory::Kind::RenderTarget, -m_GpuBytes);
    }

    void VKTexture2DArray::Bind(uint32_t slot) const
    {
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->BindTextureUnit(slot, m_Resource.GetId());
    }

}
