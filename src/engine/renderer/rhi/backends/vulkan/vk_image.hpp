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
        bool cube = false;                  ///< layers are the faces (6 per cube)
        bool swizzleBlueToOne = false;      ///< a two channel texture (a normal map keeps x and y) reads 1 in blue, not 0
        std::string name;
    };

    /// @brief A Vulkan image, its memory and its views, and the layout it is in. The backend keeps every subresource of an image in the same
    /// layout (Transition moves them all), which is what makes "this texture is being sampled" or "this one is being drawn into" a single fact.
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkImageView view = VK_NULL_HANDLE;              ///< the whole image, as a sampled texture

        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0, height = 0, depth = 1;
        uint32_t mips = 1, layers = 1;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageUsageFlags usage = 0;
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

        /// Written by compute shaders as a storage image : it stays in GENERAL, the layout both the stores and the sampling accept
        bool general = false;

        std::string name;

        bool IsValid() const { return image != VK_NULL_HANDLE; }
        bool IsDepth() const { return IsDepthFormat(format); }

        bool Create(const ImageCreateInfo& info);

        /// Hands the image to the GPU's deferred destruction (it may still be in use by a command that was recorded)
        void Destroy();

        /// The layout the image waits in between its uses
        VkImageLayout RestingLayout() const;

        /// A view of some of the image (one mip, one layer...), made once and kept
        VkImageView SubView(uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount, VkImageViewType type);

        /// A view of one aspect, for a depth-stencil image sampled as depth
        VkImageView DepthView() const { return view; }

        /// Records the barrier that takes every subresource to `to`. A no-op if it is there already.
        void Transition(VkCommandBuffer cmd, VkImageLayout to);

        /// Takes the image to the layout it waits in
        void TransitionToResting(VkCommandBuffer cmd) { Transition(cmd, RestingLayout()); }

        std::unordered_map<uint64_t, VkImageView> subViews;
    };

    /// Fills the mip chain of `image` (every layer) from its base level, which is in TRANSFER_DST ; leaves all the levels in `finalLayout`
    void GenerateMips(VkCommandBuffer cmd, Image& image, VkImageLayout finalLayout);

    void TransitionImageRaw(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to, uint32_t baseMip = 0, uint32_t mipCount = VK_REMAINING_MIP_LEVELS, uint32_t baseLayer = 0, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS);

    /// UNDEFINED -> `to` for a swapchain image just acquired : the barrier starts at `waitStage`, the stage the submit waits for
    /// the acquire semaphore at, so the transition is chained after the presentation engine has let the image go
    void TransitionAcquiredImage(VkCommandBuffer cmd, VkImage image, VkImageLayout to, VkPipelineStageFlags2 waitStage);

    /// @brief What shaders sample : an image, the way it is sampled, and the number it goes by. Textures, the attachments of a framebuffer and
    /// the cubemaps are all one : the rest of the engine only ever holds the number (the `GetHandle()` of the rhi), which is
    /// also the texture's place in the array of bindless textures.
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

            /// Writes the image in its slot of the bindless array. To be called whenever the image (or its resting layout) changes.
            void PublishBindless();

            /// Takes the content of `other` (an image and its sampler), which is left with this one's : the id of each stays
            void SwapContents(TextureResource& other);

            /// A texture that cannot be sampled any more (a dead object) is looked up as null
            static TextureResource* Find(uint32_t id);

        private:

            uint32_t m_Id = 0;
            VkSampler m_Sampler = VK_NULL_HANDLE;
    };

    VkImageViewType ViewTypeOf(ShaderSamplerType type);

    /// How a texture of these specifications is sampled. `mipLevels` : how many levels it has (a texture that has one is sampled at the base only).
    SamplerKey MakeSamplerKey(const TextureSpecifications& specs, uint32_t mipLevels);

}
