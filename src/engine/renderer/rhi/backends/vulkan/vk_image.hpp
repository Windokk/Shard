#pragma once

#include <unordered_map>
#include <vector>

#include "vk_context.hpp"

namespace Shard::Engine::Rendering::VK {

    struct ImageCreateInfo
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t mips = 1;
        uint32_t layers = 1;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
        bool cube = false;
        bool swizzleBlueToOne = false;
        std::string name;
    };

    // Every subresource stays in one layout (Transition moves them all), so "sampled" or "drawn into" is a single fact.
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;

        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0, height = 0, depth = 1;
        uint32_t mips = 1, layers = 1;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageUsageFlags usage = 0;
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

        bool general = false;

        std::string name;

        bool IsValid() const { return image != VK_NULL_HANDLE; }
        bool IsDepth() const { return IsDepthFormat(format); }

        bool Create(const ImageCreateInfo& info);

        void Destroy();

        VkImageLayout RestingLayout() const;

        VkImageView SubView(uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount, VkImageViewType type);

        VkImageView DepthView() const { return view; }

        void Transition(VkCommandBuffer cmd, VkImageLayout to);

        void TransitionToResting(VkCommandBuffer cmd) { Transition(cmd, RestingLayout()); }

        std::unordered_map<uint64_t, VkImageView> subViews;
    };

    void GenerateMips(VkCommandBuffer cmd, Image& image, VkImageLayout finalLayout);

    void TransitionImageRaw(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, uint32_t baseMip = 0, uint32_t mipCount = VK_REMAINING_MIP_LEVELS, uint32_t baseLayer = 0, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS);

    void TransitionAcquiredImage(VkCommandBuffer cmd, VkImage image, VkImageLayout to, VkPipelineStageFlags2 waitStage);

    // Textures, framebuffer attachments and cubemaps are one thing. The engine holds only the number (GetHandle()), which is the slot in the bindless array.
    class TextureResource
    {
        public:

            TextureResource();
            ~TextureResource();
            TextureResource(const TextureResource&) = delete;
            TextureResource& operator=(const TextureResource&) = delete;

            Image image;
            SamplerKey samplerKey;

            uint32_t GetId() const { return m_Id; }
            VkSampler GetSampler() const { return m_Sampler; }
            VkImageView GetView() const { return image.view; }

            void SetSampler(const SamplerKey& key);

            void PublishBindless();

            void SwapContents(TextureResource& other);

            static TextureResource* Find(uint32_t id);

        private:

            uint32_t m_Id = 0;
            VkSampler m_Sampler = VK_NULL_HANDLE;
    };

    VkImageViewType ViewTypeOf(ShaderSamplerType type);

    SamplerKey MakeSamplerKey(const TextureSpecifications& specs, uint32_t mipLevels);

}
