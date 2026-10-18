#include "vk_image.hpp"

#include <algorithm>

#include "vk_vma.hpp"
#include "engine/renderer/rhi/resources/texture/texture_filtering.hpp"

namespace Shard::Engine::Rendering::VK {

    namespace {

        /// What has to be waited for before / after an image is in a layout
        void LayoutAccess(VkImageLayout layout, VkPipelineStageFlags2& stage, VkAccessFlags2& access)
        {
            constexpr VkPipelineStageFlags2 kShaders = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

            switch (layout)
            {
                case VK_IMAGE_LAYOUT_UNDEFINED:
                    stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    access = 0;
                    break;
                case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    break;
                case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    break;
                case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                    stage = kShaders;
                    access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                    break;
                case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
                    stage = kShaders | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
                    access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                    break;
                case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                    access = VK_ACCESS_2_TRANSFER_READ_BIT;
                    break;
                case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                    stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
                    access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                    break;
                case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                    stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    access = 0;
                    break;
                default:    // GENERAL
                    stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                    break;
            }
        }

        bool WritesIn(VkImageLayout layout)
        {
            switch (layout)
            {
                case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                case VK_IMAGE_LAYOUT_GENERAL:
                    return true;
                default:
                    return false;
            }
        }

        std::mutex g_RegistryMutex;
        std::vector<TextureResource*> g_Registry(1, nullptr);   // slot 0 is "no texture"
        std::vector<uint32_t> g_FreeIds;

        uint64_t ViewKey(uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount, VkImageViewType type)
        {
            uint64_t key = baseMip;
            key = key * 131 + mipCount;
            key = key * 131 + baseLayer;
            key = key * 131 + layerCount;
            key = key * 16 + static_cast<uint64_t>(type);
            return key;
        }
    }

    void TransitionImageRaw(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                            uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount)
    {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};

        LayoutAccess(from, barrier.srcStageMask, barrier.srcAccessMask);
        LayoutAccess(to, barrier.dstStageMask, barrier.dstAccessMask);

        // Only what the old layout wrote has to be made available ; what it read needs no availability, just the ordering
        if (!WritesIn(from))
            barrier.srcAccessMask = 0;

        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {aspect, baseMip, mipCount, baseLayer, layerCount};

        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    void TransitionAcquiredImage(VkCommandBuffer cmd, VkImage image, VkImageLayout to, VkPipelineStageFlags2 waitStage)
    {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};

        // TOP_OF_PIPE (what UNDEFINED gives) would not be chained to the semaphore wait : the transition could run while
        // the image is still being presented
        barrier.srcStageMask = waitStage;
        barrier.srcAccessMask = 0;
        LayoutAccess(to, barrier.dstStageMask, barrier.dstAccessMask);

        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    static bool SupportsLinearBlit(VkFormat format)
    {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(Context::Get().GetPhysicalDevice(), format, &properties);
        const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        return (properties.optimalTilingFeatures & needed) == needed;
    }

    /// Fills the mip chain of `image` from its base level, which is in TRANSFER_DST ; leaves every level in `finalLayout`
    void GenerateMips(VkCommandBuffer cmd, Image& image, VkImageLayout finalLayout)
    {
        const VkFilter filter = SupportsLinearBlit(image.format) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

        int32_t width = static_cast<int32_t>(image.width);
        int32_t height = static_cast<int32_t>(image.height);

        for (uint32_t level = 1; level < image.mips; level++)
        {
            TransitionImageRaw(cmd, image.image, image.aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, level - 1, 1, 0, image.layers);

            VkImageBlit blit{};
            blit.srcSubresource = {image.aspect, level - 1, 0, image.layers};
            blit.srcOffsets[1] = {width, height, 1};
            width = std::max(1, width / 2);
            height = std::max(1, height / 2);
            blit.dstSubresource = {image.aspect, level, 0, image.layers};
            blit.dstOffsets[1] = {width, height, 1};

            vkCmdBlitImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter);
        }

        // Every level but the last is a source now, the last one is still a destination
        if (image.mips > 1)
        {
            TransitionImageRaw(cmd, image.image, image.aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.mips - 1, 1, 0, image.layers);
            TransitionImageRaw(cmd, image.image, image.aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, finalLayout, 0, image.mips, 0, image.layers);
        }
        else
            TransitionImageRaw(cmd, image.image, image.aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout, 0, 1, 0, image.layers);

        image.layout = finalLayout;
    }

    // ---- Image ---------------------------------------------------------------------------------------------------------------------

    bool Image::Create(const ImageCreateInfo& info)
    {
        Context& context = Context::Get();

        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        switch (info.viewType)
        {
            case VK_IMAGE_VIEW_TYPE_1D:
            case VK_IMAGE_VIEW_TYPE_1D_ARRAY:   imageInfo.imageType = VK_IMAGE_TYPE_1D; break;
            case VK_IMAGE_VIEW_TYPE_3D:         imageInfo.imageType = VK_IMAGE_TYPE_3D; break;
            default:                            imageInfo.imageType = VK_IMAGE_TYPE_2D; break;
        }
        imageInfo.format = info.format;
        imageInfo.extent = {std::max(info.width, 1u), std::max(info.height, 1u), std::max(info.depth, 1u)};
        imageInfo.mipLevels = std::max(info.mips, 1u);
        imageInfo.arrayLayers = std::max(info.layers, 1u);
        imageInfo.samples = info.samples;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = info.usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (info.cube)
            imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        if (info.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT))
            allocationInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        if (VK_CHECK(vmaCreateImage(context.GetAllocator(), &imageInfo, &allocationInfo, &image, &allocation, nullptr)) != VK_SUCCESS)
        {
            image = VK_NULL_HANDLE;
            return false;
        }

        format = info.format;
        width = imageInfo.extent.width;
        height = imageInfo.extent.height;
        depth = imageInfo.extent.depth;
        mips = imageInfo.mipLevels;
        layers = imageInfo.arrayLayers;
        samples = info.samples;
        aspect = AspectOf(info.format);
        usage = info.usage;
        viewType = info.viewType;
        layout = VK_IMAGE_LAYOUT_UNDEFINED;
        general = false;
        name = info.name;

        SetObjectName(VK_OBJECT_TYPE_IMAGE, image, info.name);

        // The view shaders sample : a depth-stencil image is sampled as its depth
        if (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT))
        {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = image;
            viewInfo.viewType = viewType;
            viewInfo.format = format;
            viewInfo.subresourceRange = {IsDepth() ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : aspect, 0, mips, 0, layers};
            if (info.swizzleBlueToOne)
                viewInfo.components.b = VK_COMPONENT_SWIZZLE_ONE;
            if (VK_CHECK(vkCreateImageView(context.GetDevice(), &viewInfo, nullptr, &view)) != VK_SUCCESS)
                return false;
            SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, view, info.name);
        }

        return true;
    }

    void Image::Destroy()
    {
        if (image == VK_NULL_HANDLE && view == VK_NULL_HANDLE)
            return;

        VkDevice device = Context::Get().GetDevice();
        VmaAllocator allocator = Context::Get().GetAllocator();

        std::vector<VkImageView> views;
        if (view)
            views.push_back(view);
        for (auto& [key, subView] : subViews)
            views.push_back(subView);

        VkImage handle = image;
        VmaAllocation memory = allocation;

        Context::Get().Defer([device, allocator, views, handle, memory] {
            for (VkImageView v : views)
                vkDestroyImageView(device, v, nullptr);
            if (handle)
                vmaDestroyImage(allocator, handle, memory);
        });

        image = VK_NULL_HANDLE;
        allocation = nullptr;
        view = VK_NULL_HANDLE;
        subViews.clear();
        layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    VkImageLayout Image::RestingLayout() const
    {
        if (general || !(usage & VK_IMAGE_USAGE_SAMPLED_BIT))
            return VK_IMAGE_LAYOUT_GENERAL;
        return IsDepth() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkImageView Image::SubView(uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount, VkImageViewType type)
    {
        const uint64_t key = ViewKey(baseMip, mipCount, baseLayer, layerCount, type);
        auto it = subViews.find(key);
        if (it != subViews.end())
            return it->second;

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = type;
        viewInfo.format = format;
        viewInfo.subresourceRange = {IsDepth() && !(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT) : aspect, baseMip, mipCount, baseLayer, layerCount};

        VkImageView subView = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImageView(Context::Get().GetDevice(), &viewInfo, nullptr, &subView));
        subViews[key] = subView;
        return subView;
    }

    void Image::Transition(VkCommandBuffer cmd, VkImageLayout to)
    {
        if (layout == to || image == VK_NULL_HANDLE)
            return;

        TransitionImageRaw(cmd, image, aspect, layout, to, 0, mips, 0, layers);
        layout = to;
    }

    // ---- TextureResource -----------------------------------------------------------------------------------------------------------

    TextureResource::TextureResource()
    {
        std::lock_guard<std::mutex> lock(g_RegistryMutex);
        if (!g_FreeIds.empty())
        {
            m_Id = g_FreeIds.back();
            g_FreeIds.pop_back();
            g_Registry[m_Id] = this;
        }
        else
        {
            m_Id = static_cast<uint32_t>(g_Registry.size());
            g_Registry.push_back(this);
        }
    }

    TextureResource::~TextureResource()
    {
        {
            std::lock_guard<std::mutex> lock(g_RegistryMutex);
            if (m_Id < g_Registry.size())
            {
                g_Registry[m_Id] = nullptr;
                g_FreeIds.push_back(m_Id);
            }
        }

        if (Context::Exists())
        {
            // The slot of the bindless array goes back to white : an id that is handed out again must not show this texture meanwhile
            const Image& white = Context::Get().GetDummyImage(ShaderSamplerType::Texture2D);
            if (white.IsValid())
                Context::Get().WriteBindless(m_Id, white.view, Context::Get().GetDummySampler(ShaderSamplerType::Texture2D), white.RestingLayout());

            image.Destroy();
        }
    }

    TextureResource* TextureResource::Find(uint32_t id)
    {
        std::lock_guard<std::mutex> lock(g_RegistryMutex);
        return id < g_Registry.size() ? g_Registry[id] : nullptr;
    }

    void TextureResource::SetSampler(const SamplerKey& key)
    {
        samplerKey = key;
        m_Sampler = Context::Get().GetSampler(key);
    }

    void TextureResource::PublishBindless()
    {
        if (!image.IsValid() || image.view == VK_NULL_HANDLE || m_Sampler == VK_NULL_HANDLE)
            return;
        Context::Get().WriteBindless(m_Id, image.view, m_Sampler, image.RestingLayout());
    }

    void TextureResource::SwapContents(TextureResource& other)
    {
        std::swap(image, other.image);
        std::swap(samplerKey, other.samplerKey);
        std::swap(m_Sampler, other.m_Sampler);
        PublishBindless();
        other.PublishBindless();
    }

    VkImageViewType ViewTypeOf(ShaderSamplerType type)
    {
        switch (type)
        {
            case ShaderSamplerType::Texture1D:
            case ShaderSamplerType::Texture1DShadow:                return VK_IMAGE_VIEW_TYPE_1D;
            case ShaderSamplerType::Texture1DArray:
            case ShaderSamplerType::Texture1DArrayShadow:           return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
            case ShaderSamplerType::Texture3D:                      return VK_IMAGE_VIEW_TYPE_3D;
            case ShaderSamplerType::TextureCube:
            case ShaderSamplerType::TextureCubeShadow:              return VK_IMAGE_VIEW_TYPE_CUBE;
            case ShaderSamplerType::TextureCubeArray:
            case ShaderSamplerType::TextureCubeArrayShadow:         return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            case ShaderSamplerType::Texture2DArray:
            case ShaderSamplerType::Texture2DArrayShadow:
            case ShaderSamplerType::Texture2DMultisampleArray:      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            default:                                                return VK_IMAGE_VIEW_TYPE_2D;
        }
    }

    SamplerKey MakeSamplerKey(const TextureSpecifications& specs, uint32_t mipLevels)
    {
        SamplerKey key;
        key.minFilter = ToVkFilter(specs.minFilter);
        key.magFilter = ToVkFilter(specs.magFilter);
        key.mipmapMode = ToVkMipmapMode(specs.minFilter);
        key.wrapU = ToVkWrap(specs.wrapS);
        key.wrapV = ToVkWrap(specs.wrapT);
        key.wrapW = ToVkWrap(specs.wrapR);
        key.compare = specs.compareMode == TextureCompareMode::CompareRefToTexture;
        key.compareOp = ToVkCompare(specs.compareFunc);

        const bool mipped = mipLevels > 1 && UsesMips(specs.minFilter);
        key.maxLod = mipped ? static_cast<float>(mipLevels - 1) : 0.0f;

        // Anisotropy on top of trilinear : what the GL backend asks for whenever there are mips
        key.anisotropy = mipLevels > 1 ? std::min(RequestedAnisotropy(), Context::Get().GetCaps().maxAnisotropy) : 1.0f;

        // GL takes any border colour, Vulkan three : the closest one
        COL_RGBA border = specs.borderColor;
        if (!(border == COL_RGBA(-1.0f)))
        {
            const bool bright = border.r() > 0.5f;
            if (border.a() < 0.5f)
                key.border = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            else
                key.border = bright ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        }

        return key;
    }

}
