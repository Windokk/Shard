#include "vk_framebuffer.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

#include "vk_buffer.hpp"
#include "vk_texture.hpp"
#include "vk_vma.hpp"

#include "engine/renderer/rhi/gpu_memory.hpp"

namespace Shard::Engine::Rendering {

    using namespace VK;

    namespace {

        uint32_t NextFramebufferHandle()
        {
            static std::atomic<uint32_t> counter{1};
            return counter++;
        }

        void ClearToResting(VkCommandBuffer cmd, Image& image)
        {
            image.Transition(cmd, image.RestingLayout());
        }
    }

    uint64_t VK::RenderTarget::Key() const
    {
        uint64_t key = static_cast<uint64_t>(ColorFormat());
        key = HashCombine64(key, static_cast<uint64_t>(DepthFormat()));
        key = HashCombine64(key, static_cast<uint64_t>(samples));
        for (uint32_t i = 0; i < extraColorCount; i++)
            key = HashCombine64(key, static_cast<uint64_t>(extraColors[i] ? extraColors[i]->format : VK_FORMAT_UNDEFINED));
        return key;
    }

    VKFramebuffer::VKFramebuffer(const FramebufferSpecifications& specs)
    {
        m_Specifications = specs;
        m_Handle = NextFramebufferHandle();
        Allocate();
    }

    VKFramebuffer::~VKFramebuffer()
    {
        if (!m_Destroyed)
            Destroy();
    }

    VkSampleCountFlagBits VKFramebuffer::ChooseSamples() const
    {
        const VkSampleCountFlags supported = Context::Get().GetCaps().sampleCounts;
        uint32_t wanted = std::max(m_Specifications.samplesCount, 1u);

        // The biggest count the GPU does that is not above what is asked for
        for (uint32_t samples = 64; samples >= 1; samples /= 2)
        {
            if (samples <= wanted && (supported & samples))
                return static_cast<VkSampleCountFlagBits>(samples);
        }
        return VK_SAMPLE_COUNT_1_BIT;
    }

    void VKFramebuffer::Allocate()
    {
        const FramebufferSpecifications& specs = m_Specifications;
        const VkSampleCountFlagBits samples = specs.multisampled ? ChooseSamples() : VK_SAMPLE_COUNT_1_BIT;
        const bool multisampled = specs.multisampled && samples != VK_SAMPLE_COUNT_1_BIT;

        const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        auto make = [&](std::unique_ptr<TextureResource>& resource, const TextureSpecifications& textureSpecs, bool depth, VkSampleCountFlagBits count, const char* name)
        {
            if (!resource)
                resource = std::make_unique<TextureResource>();

            ImageCreateInfo info;
            info.width = std::max(specs.width, 1u);
            info.height = std::max(specs.height, 1u);
            info.format = ToVkFormat(textureSpecs.internalFormat, textureSpecs.srgb);
            info.samples = count;
            info.usage = depth ? depthUsage : colorUsage;
            info.name = name;

            if (!resource->image.Create(info))
            {
                DEBUG_ERROR("Could not create the ", name, " of a framebuffer (", info.width, "x", info.height, ")");
                return;
            }

            // A target that is sampled with a filter the GPU may not have (depth, 32 bit floats) : the specifications decide, nearest by default
            SamplerKey sampler = MakeSamplerKey(textureSpecs, 1);
            if (count != VK_SAMPLE_COUNT_1_BIT)
                sampler.compare = false;
            resource->SetSampler(sampler);
            resource->PublishBindless();
        };

        if (specs.hasColor)
            make(m_Color, specs.colorSpecs, false, multisampled ? samples : VK_SAMPLE_COUNT_1_BIT, "Framebuffer color");
        if (specs.hasDepth)
            make(m_Depth, specs.depthSpecs, true, multisampled ? samples : VK_SAMPLE_COUNT_1_BIT, "Framebuffer depth");

        if (specs.hasColor)
        {
            // The extra outputs have the sample count of the colour : one draw writes all of them. They are never resolved, a G-buffer is read sample by sample
            m_ExtraColors.resize(specs.extraColorSpecs.size());
            for (size_t i = 0; i < specs.extraColorSpecs.size(); i++)
                make(m_ExtraColors[i], specs.extraColorSpecs[i], false, multisampled ? samples : VK_SAMPLE_COUNT_1_BIT, "Framebuffer extra color");
        }

        if (multisampled)
        {
            if (specs.hasColor)
                make(m_ResolveColor, specs.colorSpecs, false, VK_SAMPLE_COUNT_1_BIT, "Framebuffer resolved color");
            if (specs.hasDepth)
                make(m_ResolveDepth, specs.depthSpecs, true, VK_SAMPLE_COUNT_1_BIT, "Framebuffer resolved depth");
        }

        // What the GPU keeps for this framebuffer
        int64_t bytes = 0;
        std::vector<const std::unique_ptr<TextureResource>*> everything = {&m_Color, &m_Depth, &m_ResolveColor, &m_ResolveDepth};
        for (const auto& extra : m_ExtraColors)
            everything.push_back(&extra);
        for (const auto* resource : everything)
        {
            if (*resource && (*resource)->image.IsValid())
            {
                const Image& image = (*resource)->image;
                bytes += int64_t(image.width) * image.height * TexelSize(image.format) * static_cast<int64_t>(image.samples);
            }
        }
        GpuMemory::Add(GpuMemory::Kind::RenderTarget, bytes - m_GpuBytes);
        m_GpuBytes = bytes;
    }

    void VKFramebuffer::Release()
    {
        VKRendererAPI* api = VKRendererAPI::Get();
        if (api)
            api->ForgetTarget(this);

        for (auto* resource : {&m_Color, &m_Depth, &m_ResolveColor, &m_ResolveDepth})
        {
            if (*resource)
                (*resource)->image.Destroy();
        }
        for (auto& extra : m_ExtraColors)
        {
            if (extra)
                extra->image.Destroy();
        }
    }

    void VKFramebuffer::Destroy()
    {
        if (m_Destroyed)
            return;

        GpuMemory::Add(GpuMemory::Kind::RenderTarget, -m_GpuBytes);
        m_GpuBytes = 0;
        m_Destroyed = true;

        Release();
        m_Color.reset();
        m_Depth.reset();
        m_ResolveColor.reset();
        m_ResolveDepth.reset();
        m_ExtraColors.clear();
    }

    void VKFramebuffer::Bind()
    {
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->SetTarget(this);
    }

    void VKFramebuffer::Unbind()
    {
        if (VKRendererAPI* api = VKRendererAPI::Get())
        {
            if (api->GetTarget() == this)
                api->SetTarget(nullptr);
        }
    }

    uint32_t VKFramebuffer::GetResolveColorAttachment() const
    {
        if (m_ResolveColor)
            return m_ResolveColor->GetId();
        return m_Color ? m_Color->GetId() : 0;
    }

    uint32_t VKFramebuffer::GetResolveDepthAttachment() const
    {
        if (m_ResolveDepth)
            return m_ResolveDepth->GetId();
        return m_Depth ? m_Depth->GetId() : 0;
    }

    Image* VKFramebuffer::ResolvedColorImage()
    {
        if (m_ResolveColor)
            return &m_ResolveColor->image;
        return m_Color ? &m_Color->image : nullptr;
    }

    RenderTarget VKFramebuffer::GetRenderTarget()
    {
        RenderTarget target;
        target.width = m_Specifications.width;
        target.height = m_Specifications.height;

        if (m_Color && m_Color->image.IsValid())
        {
            target.color = &m_Color->image;
            target.colorView = m_Color->image.SubView(0, 1, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
            target.samples = m_Color->image.samples;

            for (size_t i = 0; i < m_ExtraColors.size() && i < RenderTarget::kMaxExtraColors; i++)
            {
                if (!m_ExtraColors[i] || !m_ExtraColors[i]->image.IsValid())
                    break;
                target.extraColors[i] = &m_ExtraColors[i]->image;
                target.extraColorViews[i] = m_ExtraColors[i]->image.SubView(0, 1, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
                target.extraColorCount++;
            }
        }

        if (m_AttachedCubemap != 0)
        {
            // The shadow cubes of the point lights : every layer of the array is a target, the geometry shader says which one a primitive goes to
            if (TextureResource* cubemap = TextureResource::Find(m_AttachedCubemap); cubemap && cubemap->image.IsValid())
            {
                target.depth = &cubemap->image;
                target.depthView = cubemap->image.SubView(0, 1, 0, cubemap->image.layers, VK_IMAGE_VIEW_TYPE_2D_ARRAY);
                target.layers = cubemap->image.layers;
                target.width = cubemap->image.width;
                target.height = cubemap->image.height;
                target.samples = cubemap->image.samples;
            }
        }
        else if (m_AttachedLayerTexture != 0)
        {
            // A layer of the shadow maps of the suns or of the spot lights : the others are left as they are
            TextureResource* array = TextureResource::Find(m_AttachedLayerTexture);
            if (array && array->image.IsValid() && m_AttachedLayer < array->image.layers)
            {
                target.depth = &array->image;
                target.depthView = array->image.SubView(0, 1, m_AttachedLayer, 1, VK_IMAGE_VIEW_TYPE_2D);
                target.width = array->image.width;
                target.height = array->image.height;
                if (!target.color)
                    target.samples = array->image.samples;
            }
        }
        else if (m_Depth && m_Depth->image.IsValid())
        {
            target.depth = &m_Depth->image;
            target.depthView = m_Depth->image.SubView(0, 1, 0, 1, VK_IMAGE_VIEW_TYPE_2D);
            if (!target.color)
                target.samples = m_Depth->image.samples;
        }

        return target;
    }

    void VKFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        m_Specifications.width = width;
        m_Specifications.height = height;

        Release();
        Allocate();
    }

    void VKFramebuffer::SetSamplesCount(uint32_t samples)
    {
        if (!m_Specifications.multisampled || samples == m_Specifications.samplesCount)
            return;

        m_Specifications.samplesCount = std::max(samples, 1u);
        Resize(m_Specifications.width, m_Specifications.height);
    }

    void VKFramebuffer::AttachCubemapArray(uint32_t texture)
    {
        m_AttachedCubemap = texture;
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->ForgetTarget(this);
    }

    void VKFramebuffer::DetachCubemapArray()
    {
        m_AttachedCubemap = 0;
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->ForgetTarget(this);
    }

    void VKFramebuffer::AttachDepthLayer(uint32_t texture, uint32_t layer)
    {
        m_AttachedLayerTexture = texture;
        m_AttachedLayer = layer;
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->ForgetTarget(this);
    }

    void VKFramebuffer::ResolveMultisampled()
    {
        if (!m_Specifications.multisampled || !m_ResolveColor || !m_Color)
            return;

        Context& context = Context::Get();
        context.EnsureOutsideRendering();
        VkCommandBuffer cmd = context.Cmd();

        Image& source = m_Color->image;
        Image& destination = m_ResolveColor->image;
        if (!source.IsValid() || !destination.IsValid())
            return;

        source.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        destination.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageResolve region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {source.width, source.height, 1};
        vkCmdResolveImage(cmd, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        ClearToResting(cmd, source);
        ClearToResting(cmd, destination);

        // The depth is not resolved by a command of the API (a depth image cannot be) : whoever needs a single sample depth reads the samples
    }

    void VKFramebuffer::CopyFrom(std::shared_ptr<Framebuffer> src)
    {
        VKFramebuffer* source = dynamic_cast<VKFramebuffer*>(src.get());
        if (!source)
            return;

        Context& context = Context::Get();
        context.EnsureOutsideRendering();
        VkCommandBuffer cmd = context.Cmd();

        for (int pass = 0; pass < 2; pass++)
        {
            const bool depth = pass == 1;
            auto& from = depth ? source->m_Depth : source->m_Color;
            auto& to = depth ? m_Depth : m_Color;
            if (!from || !to || !from->image.IsValid() || !to->image.IsValid())
                continue;

            Image& a = from->image;
            Image& b = to->image;
            const bool sameKind = a.format == b.format && a.samples == b.samples;

            if (a.width == b.width && a.height == b.height && sameKind)
            {
                a.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                b.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

                VkImageCopy region{};
                region.srcSubresource = {a.aspect, 0, 0, 1};
                region.dstSubresource = {b.aspect, 0, 0, 1};
                region.extent = {a.width, a.height, 1};
                vkCmdCopyImage(cmd, a.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }
            else if (a.samples == VK_SAMPLE_COUNT_1_BIT && b.samples == VK_SAMPLE_COUNT_1_BIT)
            {
                a.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                b.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

                VkImageBlit region{};
                region.srcSubresource = {a.aspect, 0, 0, 1};
                region.srcOffsets[1] = {static_cast<int32_t>(a.width), static_cast<int32_t>(a.height), 1};
                region.dstSubresource = {b.aspect, 0, 0, 1};
                region.dstOffsets[1] = {static_cast<int32_t>(b.width), static_cast<int32_t>(b.height), 1};
                vkCmdBlitImage(cmd, a.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, depth ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
            }
            else
            {
                DEBUG_WARNING("Framebuffer::CopyFrom between two multisampled targets of another format is not supported by the Vulkan backend.");
                continue;
            }

            ClearToResting(cmd, a);
            ClearToResting(cmd, b);
        }
    }

    void VKFramebuffer::CopyDepthFrom(std::shared_ptr<Framebuffer> src)
    {
        VKFramebuffer* source = dynamic_cast<VKFramebuffer*>(src.get());
        if (!source || !source->m_Depth || !m_Depth || !source->m_Depth->image.IsValid() || !m_Depth->image.IsValid())
            return;

        Image& a = source->m_Depth->image;
        Image& b = m_Depth->image;
        if (a.format != b.format || a.samples != b.samples || a.width != b.width || a.height != b.height)
        {
            DEBUG_WARNING("Framebuffer::CopyDepthFrom needs two depth targets of the same size, format and samples : nothing was copied.");
            return;
        }

        Context& context = Context::Get();
        context.EnsureOutsideRendering();
        VkCommandBuffer cmd = context.Cmd();

        a.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        b.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        region.extent = {a.width, a.height, 1};
        vkCmdCopyImage(cmd, a.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        ClearToResting(cmd, a);
        ClearToResting(cmd, b);
    }

    void VKFramebuffer::BlitToScreen(uint32_t screenWidth, uint32_t screenHeight)
    {
        VKRendererAPI* api = VKRendererAPI::Get();
        Image* color = ResolvedColorImage();
        if (api && color)
            api->PresentImage(*color, screenWidth, screenHeight);
    }

    bool VKFramebuffer::ReadPixelsRGBA8(std::vector<uint8_t>& outPixels)
    {
        Image* image = ResolvedColorImage();
        if (!m_Specifications.hasColor || !image || !image->IsValid())
            return false;

        Context& context = Context::Get();
        context.EnsureOutsideRendering();
        context.Flush();

        switch (image->format)
        {
            case VK_FORMAT_R8_UNORM:
            case VK_FORMAT_R8G8_UNORM:
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_R16_SFLOAT:
            case VK_FORMAT_R16G16_SFLOAT:
            case VK_FORMAT_R16G16B16A16_SFLOAT:
            case VK_FORMAT_R32_SFLOAT:
            case VK_FORMAT_R32G32B32A32_SFLOAT:
                break;
            default:
                DEBUG_WARNING("Framebuffer::ReadPixelsRGBA8 does not support this color format : nothing was read.");
                return false;
        }

        const uint32_t width = image->width;
        const uint32_t height = image->height;
        const uint32_t texel = TexelSize(image->format);
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
            return false;

        context.ImmediateSubmit([&](VkCommandBuffer cmd) {
            const VkImageLayout before = image->layout == VK_IMAGE_LAYOUT_UNDEFINED ? image->RestingLayout() : image->layout;
            image->Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width, height, 1};
            vkCmdCopyImageToBuffer(cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);

            image->Transition(cmd, before);
        });

        vmaInvalidateAllocation(context.GetAllocator(), allocation, 0, bytes);
        const uint8_t* gpu = static_cast<const uint8_t*>(result.pMappedData);

        // The rows are stored bottom first (like OpenGL's) : the picture the caller wants starts at the top
        const size_t rowBytes = size_t(width) * 4;
        outPixels.resize(rowBytes * height);

        const auto toByte = [](float value) {
            return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };

        for (uint32_t y = 0; y < height; y++)
        {
            uint8_t* destination = outPixels.data() + size_t(y) * rowBytes;
            const uint8_t* row = gpu + size_t(height - 1 - y) * width * texel;

            if (image->format == VK_FORMAT_R8G8B8A8_UNORM || image->format == VK_FORMAT_R8G8B8A8_SRGB)
            {
                std::memcpy(destination, row, rowBytes);
                continue;
            }

            // The channels the target does not have read as 0, alpha as 1 (what glReadPixels gives)
            for (uint32_t x = 0; x < width; x++)
            {
                uint8_t* out = destination + size_t(x) * 4;
                const uint8_t* in = row + size_t(x) * texel;
                out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 255;

                switch (image->format)
                {
                    case VK_FORMAT_R8_UNORM:
                        out[0] = in[0];
                        break;
                    case VK_FORMAT_R8G8_UNORM:
                        out[0] = in[0]; out[1] = in[1];
                        break;
                    case VK_FORMAT_B8G8R8A8_UNORM:
                        out[0] = in[2]; out[1] = in[1]; out[2] = in[0]; out[3] = in[3];
                        break;
                    case VK_FORMAT_R16_SFLOAT:
                    case VK_FORMAT_R16G16_SFLOAT:
                    case VK_FORMAT_R16G16B16A16_SFLOAT:
                    {
                        const uint32_t channels = texel / 2;
                        uint16_t halves[4];
                        std::memcpy(halves, in, texel);
                        for (uint32_t c = 0; c < channels; c++)
                            out[c] = toByte(HalfToFloat(halves[c]));
                        break;
                    }
                    case VK_FORMAT_R32_SFLOAT:
                    case VK_FORMAT_R32G32B32A32_SFLOAT:
                    {
                        const uint32_t channels = texel / 4;
                        float floats[4];
                        std::memcpy(floats, in, texel);
                        for (uint32_t c = 0; c < channels; c++)
                            out[c] = toByte(floats[c]);
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        vmaDestroyBuffer(context.GetAllocator(), readback, allocation);
        return true;
    }

    void VKFramebuffer::UploadColorRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const uint8_t* rgba)
    {
        if (!m_Specifications.hasColor || m_Specifications.multisampled || !m_Color || !m_Color->image.IsValid())
            return;

        Image& image = m_Color->image;
        if (image.format != VK_FORMAT_R8G8B8A8_UNORM && image.format != VK_FORMAT_R8G8B8A8_SRGB)
        {
            DEBUG_WARNING("Framebuffer::UploadColorRegion needs an 8 bit RGBA target.");
            return;
        }

        Context& context = Context::Get();
        context.EnsureOutsideRendering();

        const size_t rowBytes = size_t(width) * 4;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        void* mapped = nullptr;
        if (!context.AllocStaging(rowBytes * height, staging, offset, mapped))
            return;

        // The rows come top first, the texture stores them bottom first
        for (uint32_t row = 0; row < height; row++)
            std::memcpy(static_cast<uint8_t*>(mapped) + size_t(row) * rowBytes, rgba + size_t(height - 1 - row) * rowBytes, rowBytes);

        VkCommandBuffer cmd = context.Cmd();
        image.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        ClearToResting(cmd, image);
    }

    void VKFramebuffer::BlitColorTo(Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight)
    {
        VKFramebuffer* destination = dynamic_cast<VKFramebuffer*>(&dst);
        Image* source = ResolvedColorImage();
        if (!destination || !source || !destination->m_Color || !destination->m_Color->image.IsValid() || !source->IsValid())
            return;

        Image& target = destination->m_Color->image;
        if (target.samples != VK_SAMPLE_COUNT_1_BIT)
            return;

        Context& context = Context::Get();
        context.EnsureOutsideRendering();
        VkCommandBuffer cmd = context.Cmd();

        source->Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        target.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {static_cast<int32_t>(source->width), static_cast<int32_t>(source->height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[0] = {static_cast<int32_t>(dstX), static_cast<int32_t>(dstY), 0};
        region.dstOffsets[1] = {static_cast<int32_t>(dstX + dstWidth), static_cast<int32_t>(dstY + dstHeight), 1};
        vkCmdBlitImage(cmd, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);

        ClearToResting(cmd, *source);
        ClearToResting(cmd, target);
    }

}
