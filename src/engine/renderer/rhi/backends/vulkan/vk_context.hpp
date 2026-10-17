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

    /// What the application gives the Vulkan device to be created with
    struct InitInfo
    {
        std::string appName = "Shard";

        /// The validation layer (VK_LAYER_KHRONOS_validation) : on when the SDK is installed and the caller asks for it
        bool validation = false;

        /// What the window needs of the instance (its surface extensions). Empty for an offscreen run.
        std::vector<std::string> instanceExtensions;

        /// Makes the surface of the window (null for an offscreen run : the picture is only ever read back)
        std::function<uint64_t(VkInstance)> createSurface;

        int width = 1280;
        int height = 720;
        bool vsync = true;
        std::function<bool()> vsyncWanted;      // polled each frame : the present mode follows it
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
        float maxLod = 0.0f;                    ///< 0 : the base level only
        VkBorderColor border = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        bool operator==(const SamplerKey& other) const;
    };

    struct SamplerKeyHash { size_t operator()(const SamplerKey& key) const; };

    /// What the device can do, as far as the backend cares
    struct Capabilities
    {
        bool blockCompression = false;
        bool depthClipControl = false;
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

    /// @brief The Vulkan device and everything the backend keeps for the whole run : the queue, the memory allocator, the command buffers
    /// of the frames in flight, the descriptor pools, the swapchain. There is one (see Get()), created by VKRendererAPI.
    ///
    /// Commands are recorded in the command buffer of the current frame slot (Cmd()). A slot is submitted by Submit(), which moves to
    /// the next one ; the slot that comes back is waited for (its fence) before it is recorded into again, which is also when what
    /// was released during it (Defer) is destroyed. Uploads of new resources go through ImmediateSubmit() : they do not wait for the
    /// frame, nothing that is recorded can depend on what they create before it exists.
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

            // ---- Frames ----

            static constexpr uint32_t kSlots = 4;

            /// The command buffer being recorded (one is started if none is)
            VkCommandBuffer Cmd();
            bool IsRecording() const { return m_Slots[m_Current].recording; }

            /// Ends the command buffer of the current slot and submits it. `wait` / `signal` : the semaphores of a presentation.
            void Submit(VkSemaphore wait = VK_NULL_HANDLE, VkPipelineStageFlags waitStage = 0, VkSemaphore signal = VK_NULL_HANDLE);

            /// Submits what is recorded and waits for the GPU to have run it (a readback, a resize, a shutdown)
            void Flush();

            void WaitIdle();

            /// Runs `work` in a throw-away command buffer and waits for it : the way a new texture or buffer gets its content
            void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& work);

            /// Runs `work` once the GPU is done with everything recorded so far (the destruction of what a command may still use)
            void Defer(std::function<void()> work);

            /// A transfer, a barrier, a layout change cannot be recorded while a render pass is open : the API that owns the pass says how to close it
            void SetOutsideRenderingHook(std::function<void()> hook) { m_OutsideRenderingHook = std::move(hook); }
            void EnsureOutsideRendering() { if (m_OutsideRenderingHook) m_OutsideRenderingHook(); }

            /// Called when a command buffer has begun and just before it ends : the API keeps its debug groups balanced with them
            void SetCommandBufferHooks(std::function<void(VkCommandBuffer)> onBegin, std::function<void(VkCommandBuffer)> onEnd)
            {
                m_OnCommandBufferBegin = std::move(onBegin);
                m_OnCommandBufferEnd = std::move(onEnd);
            }

            uint64_t FrameIndex() const { return m_FrameCounter; }
            uint32_t CurrentSlot() const { return m_Current; }

            // ---- Descriptors ----

            VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout);

            /// Sets built this frame, by what they hold : a draw that binds the same things as an earlier one reuses its set
            VkDescriptorSet FindCachedSet(uint64_t key) const;
            void CacheSet(uint64_t key, VkDescriptorSet set);

            /// Room in the uniform ring of the current slot. False if it is full (the caller gets nothing and logs).
            bool AllocUniform(uint32_t size, uint32_t& offset, void*& mapped);
            VkBuffer GetUniformRing() const { return m_UniformRing; }

            VkSampler GetSampler(const SamplerKey& key);

            /// Room for `size` bytes the CPU writes and the GPU then copies from, valid until the current slot is done. Comes from a ring of the slot,
            /// or from a buffer of its own when the ring is not big enough (the buffer goes with the slot).
            bool AllocStaging(VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset, void*& mapped);

            /// A set of the persistent pool : it lives until FreePersistentSet (the uniform block of a shader)
            VkDescriptorSet AllocatePersistentSet(VkDescriptorSetLayout layout);
            void FreePersistentSet(VkDescriptorSet set);

            // ---- Bindless textures ----

            VkDescriptorSetLayout GetBindlessLayout() const { return m_BindlessLayout; }
            VkDescriptorSet GetBindlessSet() const { return m_BindlessSet; }
            void WriteBindless(uint32_t index, VkImageView view, VkSampler sampler, VkImageLayout layout);

            /// A texture to bind where nothing is : a 1x1 white one of the kind of the sampler
            const Image& GetDummyImage(ShaderSamplerType type) const;
            VkSampler GetDummySampler(ShaderSamplerType type);

            // ---- Presentation ----

            bool CreateSwapchain(int width, int height, bool vsync);
            void DestroySwapchain();
            uint32_t GetSwapchainWidth() const { return m_SwapchainExtent.width; }
            uint32_t GetSwapchainHeight() const { return m_SwapchainExtent.height; }

            /// Copies `source` (colour, 8 bit) over the window and presents it. False if there is no window or it cannot be presented to now.
            bool Present(Image& source, uint32_t width, uint32_t height);

            /// Presents what `draw` records on the swapchain image (cleared first) : the interface of an application that draws it itself. The command
            /// buffer is inside a dynamic rendering pass on the image, which is in the colour attachment layout.
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
            std::vector<std::function<void()>> m_PendingDefers;     // from any thread, moved into the current slot on the render thread

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

            // Swapchain
            VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
            std::vector<VkImage> m_SwapchainImages;
            std::vector<VkImageView> m_SwapchainViews;      // for the interface, which is drawn on the images
            std::vector<VkSemaphore> m_RenderFinished;      // per swapchain image
            std::array<VkSemaphore, kSlots> m_ImageAcquired{};
            VkExtent2D m_SwapchainExtent{};
            VkFormat m_SwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
            bool m_Vsync = true;
            std::function<bool()> m_VsyncWanted;
            bool m_SwapchainDirty = false;
            uint32_t m_AcquireIndex = 0;
    };

}
