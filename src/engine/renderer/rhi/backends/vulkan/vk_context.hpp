#pragma once

#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "vk_common.hpp"

namespace Shard::Engine::Rendering::VK {

    struct Image;

    struct InitInfo
    {
        std::string appName = "Shard";

        bool validation = false;

        std::vector<std::string> instanceExtensions;

        std::function<uint64_t(VkInstance)> createSurface;

        int width = 1280;
        int height = 720;
        bool vsync = true;
        std::function<bool()> vsyncWanted;

        std::string pipelineCachePath;
    };

    struct SamplerKey
    {
        VkFilter minFilter = VK_FILTER_LINEAR;
        VkFilter magFilter = VK_FILTER_LINEAR;
        VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        VkSamplerAddressMode wrapU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode wrapV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode wrapW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        bool compare = false;
        VkCompareOp compareOp = VK_COMPARE_OP_LESS;
        float anisotropy = 1.0f;
        float maxLod = 0.0f;
        VkBorderColor border = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        bool operator==(const SamplerKey& other) const;
    };

    struct SamplerKeyHash { size_t operator()(const SamplerKey& key) const; };

    struct Capabilities
    {
        bool blockCompression = false;
        bool depthClipControl = false;
        bool conservativeRaster = false;
        bool debugUtils = false;
        bool nullDescriptor = false;
        bool samplerAnisotropy = false;
        bool fillModeNonSolid = false;
        bool geometryShader = false;
        bool timestamps = false;
        float timestampPeriodNs = 1.0f;
        float maxAnisotropy = 1.0f;
        VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkFormat depthStencilFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        uint32_t uniformAlignment = 256;
        uint32_t maxBindlessTextures = 0;
        bool validationEnabled = false;
    };

    // Device plus everything kept for the run. Commands go in the current slot's command buffer.
    // Submit() moves to the next slot, which is fence-waited before reuse (that is when Defer'd work runs). ImmediateSubmit does not wait for the frame.
    class Context
    {
        public:

            Context();
            ~Context();

            static Context& Get();
            static bool Exists();

            bool Init(const InitInfo& info);
            void Shutdown();

            VkInstance GetInstance() const { return m_Instance; }
            VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
            VkDevice GetDevice() const { return m_Device; }
            VkQueue GetQueue() const { return m_Queue; }
            uint32_t GetQueueFamily() const { return m_QueueFamily; }
            VmaAllocator GetAllocator() const { return m_Allocator; }
            VkPipelineCache GetPipelineCache() const { return m_PipelineCache; }
            const VkPhysicalDeviceProperties& GetProperties() const { return m_Properties; }
            const VkPhysicalDeviceLimits& GetLimits() const { return m_Properties.limits; }
            const Capabilities& GetCaps() const { return m_Caps; }
            bool HasSurface() const { return m_Surface != VK_NULL_HANDLE; }

            static constexpr uint32_t kSlots = 4;

            VkCommandBuffer Cmd();
            bool IsRecording() const { return m_Slots[m_Current].recording; }

            void Submit(VkSemaphore wait = VK_NULL_HANDLE, VkPipelineStageFlags waitStage = 0, VkSemaphore signal = VK_NULL_HANDLE);

            void SavePipelineCache();

            void Flush();

            void WaitIdle();

            void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& work);

            void Defer(std::function<void()> work);

            void SetOutsideRenderingHook(std::function<void()> hook) { m_OutsideRenderingHook = std::move(hook); }
            void EnsureOutsideRendering() { if (m_OutsideRenderingHook) m_OutsideRenderingHook(); }

            void SetCommandBufferHooks(std::function<void(VkCommandBuffer)> onBegin, std::function<void(VkCommandBuffer)> onEnd)
            {
                m_OnCommandBufferBegin = std::move(onBegin);
                m_OnCommandBufferEnd = std::move(onEnd);
            }

            uint64_t FrameIndex() const { return m_FrameCounter; }
            uint32_t CurrentSlot() const { return m_Current; }

            VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout);

            VkDescriptorSet FindCachedSet(uint64_t key) const;
            void CacheSet(uint64_t key, VkDescriptorSet set);

            bool AllocUniform(uint32_t size, uint32_t& offset, void*& mapped);
            VkBuffer GetUniformRing() const { return m_UniformRing; }

            VkSampler GetSampler(const SamplerKey& key);

            bool AllocStaging(VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset, void*& mapped);

            VkDescriptorSet AllocatePersistentSet(VkDescriptorSetLayout layout);
            void FreePersistentSet(VkDescriptorSet set);

            VkDescriptorSetLayout GetBindlessLayout() const { return m_BindlessLayout; }
            VkDescriptorSet GetBindlessSet() const { return m_BindlessSet; }
            void WriteBindless(uint32_t index, VkImageView view, VkSampler sampler, VkImageLayout layout);

            const Image& GetDummyImage(ShaderSamplerType type) const;
            VkSampler GetDummySampler(ShaderSamplerType type);

            bool CreateSwapchain(int width, int height, bool vsync);
            void DestroySwapchain();
            uint32_t GetSwapchainWidth() const { return m_SwapchainExtent.width; }
            uint32_t GetSwapchainHeight() const { return m_SwapchainExtent.height; }

            bool Present(Image& source, uint32_t width, uint32_t height);

            bool PresentUI(const std::function<void(VkCommandBuffer)>& draw, uint32_t width, uint32_t height, const float clearColor[4]);

            VkFormat GetSwapchainFormat() const { return m_SwapchainFormat; }
            uint32_t GetSwapchainImageCount() const { return static_cast<uint32_t>(m_SwapchainImages.size()); }

            bool SupportsPresentation() const { return m_Swapchain != VK_NULL_HANDLE; }

        private:

            struct DescriptorPoolSet
            {
                std::vector<VkDescriptorPool> pools;
                size_t current = 0;
            };

            struct Slot
            {
                VkCommandPool pool = VK_NULL_HANDLE;
                VkCommandBuffer cmd = VK_NULL_HANDLE;
                VkFence fence = VK_NULL_HANDLE;
                bool recording = false;
                bool submitted = false;
                DescriptorPoolSet descriptors;
                uint32_t uniformUsed = 0;
                VkDeviceSize stagingUsed = 0;
                std::vector<std::function<void()>> deferred;
                std::unordered_map<uint64_t, VkDescriptorSet> setCache;
            };

            bool CreateInstanceAndDevice(const InitInfo& info);
            bool CreateSlots();
            bool CreateUniformRing();
            bool CreateBindless();
            bool CreateDummies();
            void BeginSlot(Slot& slot);
            VkDescriptorPool NewPool();

            VkInstance m_Instance = VK_NULL_HANDLE;
            VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
            VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
            VkDevice m_Device = VK_NULL_HANDLE;
            VkQueue m_Queue = VK_NULL_HANDLE;
            uint32_t m_QueueFamily = 0;
            VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
            VmaAllocator m_Allocator = nullptr;
            VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;
            VkPhysicalDeviceProperties m_Properties{};
            Capabilities m_Caps;

            std::array<Slot, kSlots> m_Slots;
            uint32_t m_Current = 0;
            uint64_t m_FrameCounter = 0;
            std::mutex m_DeferMutex;
            std::function<void()> m_OutsideRenderingHook;
            std::function<void(VkCommandBuffer)> m_OnCommandBufferBegin;
            std::function<void(VkCommandBuffer)> m_OnCommandBufferEnd;
            std::vector<std::function<void()>> m_PendingDefers;

            VkCommandPool m_ImmediatePool = VK_NULL_HANDLE;
            VkFence m_ImmediateFence = VK_NULL_HANDLE;

            VkBuffer m_StagingRing = VK_NULL_HANDLE;
            VmaAllocation m_StagingRingAllocation = nullptr;
            uint8_t* m_StagingRingMapped = nullptr;
            static constexpr VkDeviceSize kStagingBytesPerSlot = 32ull * 1024ull * 1024ull;
            VkDescriptorPool m_PersistentPool = VK_NULL_HANDLE;

            VkBuffer m_UniformRing = VK_NULL_HANDLE;
            VmaAllocation m_UniformRingAllocation = nullptr;
            uint8_t* m_UniformRingMapped = nullptr;
            static constexpr uint32_t kUniformBytesPerSlot = 24u * 1024u * 1024u;
            static constexpr uint32_t kUniformTailPadding = 64u * 1024u;

            std::unordered_map<SamplerKey, VkSampler, SamplerKeyHash> m_Samplers;

            VkDescriptorSetLayout m_BindlessLayout = VK_NULL_HANDLE;
            VkDescriptorPool m_BindlessPool = VK_NULL_HANDLE;
            VkDescriptorSet m_BindlessSet = VK_NULL_HANDLE;

            struct Dummies;
            std::unique_ptr<Dummies> m_Dummies;

            VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
            std::vector<VkImage> m_SwapchainImages;
            std::vector<VkImageView> m_SwapchainViews;
            std::vector<VkSemaphore> m_RenderFinished;
            std::array<VkSemaphore, kSlots> m_ImageAcquired{};
            VkExtent2D m_SwapchainExtent{};
            VkFormat m_SwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
            bool m_Vsync = true;
            std::function<bool()> m_VsyncWanted;
            std::string m_PipelineCachePath;
            bool m_SwapchainDirty = false;
            uint32_t m_AcquireIndex = 0;
    };

}
