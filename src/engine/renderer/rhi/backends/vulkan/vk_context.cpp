#include "vk_context.hpp"

#include <algorithm>
#include <cstring>

#include <VkBootstrap.h>

#include "vk_image.hpp"
#include "vk_vma.hpp"

namespace Shard::Engine::Rendering::VK {

    namespace {

        Context* g_Context = nullptr;

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
                                                     const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
        {
            (void)type;
            const char* message = data && data->pMessage ? data->pMessage : "";

            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                DEBUG_ERROR("[Vulkan validation] ", message);
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                DEBUG_WARNING("[Vulkan validation] ", message);
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
                DEBUG_LOG("[Vulkan] ", message);

            return VK_FALSE;
        }

        bool FormatSupports(VkPhysicalDevice device, VkFormat format, VkFormatFeatureFlags features)
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(device, format, &properties);
            return (properties.optimalTilingFeatures & features) == features;
        }
    }

    // ---- SamplerKey ----------------------------------------------------------------------------------------------------------------

    bool SamplerKey::operator==(const SamplerKey& other) const
    {
        return minFilter == other.minFilter && magFilter == other.magFilter && mipmapMode == other.mipmapMode &&
            wrapU == other.wrapU && wrapV == other.wrapV && wrapW == other.wrapW && compare == other.compare &&
            compareOp == other.compareOp && anisotropy == other.anisotropy && maxLod == other.maxLod && border == other.border;
    }

    size_t SamplerKeyHash::operator()(const SamplerKey& key) const
    {
        uint64_t h = 1469598103934665603ULL;
        for (uint64_t part : {uint64_t(key.minFilter), uint64_t(key.magFilter), uint64_t(key.mipmapMode), uint64_t(key.wrapU), uint64_t(key.wrapV),
                              uint64_t(key.wrapW), uint64_t(key.compare), uint64_t(key.compareOp), uint64_t(key.anisotropy * 16.0f),
                              uint64_t(key.maxLod * 16.0f), uint64_t(key.border)})
            h = HashCombine64(h, part);
        return static_cast<size_t>(h);
    }

    // ---- Context -------------------------------------------------------------------------------------------------------------------

    struct Context::Dummies
    {
        // One 1x1 image for each kind of sampler a shader can declare
        std::unordered_map<int, Image> images;
        std::unordered_map<int, VkSampler> samplers;
    };

    Context::Context() = default;
    Context::~Context() = default;

    Context& Context::Get()
    {
        return *g_Context;
    }

    bool Context::Exists()
    {
        return g_Context != nullptr;
    }

    bool Context::CreateInstanceAndDevice(const InitInfo& info)
    {
        if (volkInitialize() != VK_SUCCESS)
        {
            DEBUG_ERROR("Vulkan is not available : no Vulkan loader (vulkan-1.dll / libvulkan.so) was found on this machine.");
            return false;
        }

        vkb::InstanceBuilder instanceBuilder;
        instanceBuilder.set_app_name(info.appName.c_str())
            .set_engine_name("Shard")
            .require_api_version(1, 3, 0);

        for (const std::string& extension : info.instanceExtensions)
            instanceBuilder.enable_extension(extension.c_str());

        if (info.validation)
        {
            instanceBuilder.request_validation_layers(true);
            instanceBuilder.set_debug_callback(DebugCallback);
            instanceBuilder.set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
        }

        auto instanceResult = instanceBuilder.build();
        if (!instanceResult)
        {
            DEBUG_ERROR("Could not create the Vulkan instance : ", instanceResult.error().message());
            return false;
        }

        vkb::Instance vkbInstance = instanceResult.value();
        m_Instance = vkbInstance.instance;
        m_Messenger = vkbInstance.debug_messenger;
        m_Caps.validationEnabled = info.validation && m_Messenger != VK_NULL_HANDLE;
        if (info.validation && !m_Caps.validationEnabled)
            DEBUG_WARNING("The Vulkan validation layer is not installed (it comes with the Vulkan SDK) : running without it.");

        volkLoadInstance(m_Instance);

        if (info.createSurface)
            m_Surface = reinterpret_cast<VkSurfaceKHR>(info.createSurface(m_Instance));

        VkPhysicalDeviceFeatures features{};
        features.samplerAnisotropy = VK_TRUE;
        features.fillModeNonSolid = VK_TRUE;
        features.geometryShader = VK_TRUE;
        features.multiDrawIndirect = VK_TRUE;
        features.drawIndirectFirstInstance = VK_TRUE;
        features.independentBlend = VK_TRUE;
        features.shaderStorageImageExtendedFormats = VK_TRUE;
        features.shaderStorageImageReadWithoutFormat = VK_TRUE;
        features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        features.depthClamp = VK_TRUE;
        features.fullDrawIndexUint32 = VK_TRUE;
        features.imageCubeArray = VK_TRUE;
        features.fragmentStoresAndAtomics = VK_TRUE;
        features.vertexPipelineStoresAndAtomics = VK_TRUE;
        features.sampleRateShading = VK_TRUE;                     // gl_SampleID : the deferred lighting runs once per sample of a multisampled G-buffer

        VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        features11.shaderDrawParameters = VK_TRUE;

        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        features12.descriptorIndexing = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;
        features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        features12.hostQueryReset = VK_TRUE;
        features12.timelineSemaphore = VK_TRUE;

        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        features13.shaderDemoteToHelperInvocation = VK_TRUE;      // `discard` of a fragment shader

        vkb::PhysicalDeviceSelector selector(vkbInstance);
        selector.set_minimum_version(1, 3)
            .set_required_features(features)
            .set_required_features_11(features11)
            .set_required_features_12(features12)
            .set_required_features_13(features13)
            .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete);

        if (m_Surface)
            selector.set_surface(m_Surface);
        else
            selector.defer_surface_initialization();

        auto physicalResult = selector.select();
        if (!physicalResult)
        {
            DEBUG_ERROR("No GPU can run the Vulkan backend (it needs Vulkan 1.3 with dynamic rendering and descriptor indexing) : ", physicalResult.error().message());
            return false;
        }

        vkb::PhysicalDevice physical = physicalResult.value();

        // What the backend can do without, when the GPU has it
        VkPhysicalDeviceDepthClipControlFeaturesEXT depthClip{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT};
        depthClip.depthClipControl = VK_TRUE;
        m_Caps.depthClipControl = physical.enable_extension_features_if_present(depthClip);

        VkPhysicalDeviceRobustness2FeaturesEXT robustness{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
        robustness.nullDescriptor = VK_TRUE;
        m_Caps.nullDescriptor = physical.enable_extension_features_if_present(robustness);

        if (!m_Caps.depthClipControl)
        {
            DEBUG_ERROR("This GPU does not have VK_EXT_depth_clip_control : the Vulkan backend needs it to keep the clip space of the shaders (OpenGL's, z from -1 to 1).");
            return false;
        }

        m_PhysicalDevice = physical.physical_device;
        m_Properties = physical.properties;

        vkb::DeviceBuilder deviceBuilder(physical);
        auto deviceResult = deviceBuilder.build();
        if (!deviceResult)
        {
            DEBUG_ERROR("Could not create the Vulkan device : ", deviceResult.error().message());
            return false;
        }

        vkb::Device vkbDevice = deviceResult.value();
        m_Device = vkbDevice.device;
        volkLoadDevice(m_Device);

        auto queue = vkbDevice.get_queue(vkb::QueueType::graphics);
        auto family = vkbDevice.get_queue_index(vkb::QueueType::graphics);
        if (!queue || !family)
        {
            DEBUG_ERROR("The Vulkan device has no graphics queue.");
            return false;
        }
        m_Queue = queue.value();
        m_QueueFamily = family.value();

        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supported);
        m_Caps.blockCompression = supported.textureCompressionBC == VK_TRUE;
        m_Caps.samplerAnisotropy = true;
        m_Caps.fillModeNonSolid = true;
        m_Caps.geometryShader = true;
        m_Caps.maxAnisotropy = m_Properties.limits.maxSamplerAnisotropy;
        m_Caps.sampleCounts = m_Properties.limits.framebufferColorSampleCounts & m_Properties.limits.framebufferDepthSampleCounts;
        m_Caps.uniformAlignment = std::max<uint32_t>(256, static_cast<uint32_t>(m_Properties.limits.minUniformBufferOffsetAlignment));
        m_Caps.timestamps = m_Properties.limits.timestampComputeAndGraphics == VK_TRUE;
        m_Caps.timestampPeriodNs = m_Properties.limits.timestampPeriod;
        m_Caps.debugUtils = vkSetDebugUtilsObjectNameEXT != nullptr;

        VkPhysicalDeviceDescriptorIndexingProperties indexing{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES};
        VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties2.pNext = &indexing;
        vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &properties2);
        m_Caps.maxBindlessTextures = std::min<uint32_t>(16384, indexing.maxDescriptorSetUpdateAfterBindSampledImages);

        // Depth formats : a 32 bit float one is the portable choice (24 bit ones are missing on some GPUs)
        const VkFormatFeatureFlags depthFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        m_Caps.depthFormat = FormatSupports(m_PhysicalDevice, VK_FORMAT_D32_SFLOAT, depthFeatures) ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D16_UNORM;
        if (FormatSupports(m_PhysicalDevice, VK_FORMAT_D32_SFLOAT_S8_UINT, depthFeatures))
            m_Caps.depthStencilFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        else
            m_Caps.depthStencilFormat = VK_FORMAT_D24_UNORM_S8_UINT;

        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_Device;
        allocatorInfo.instance = m_Instance;
        allocatorInfo.pVulkanFunctions = &functions;
        if (VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_Allocator)) != VK_SUCCESS)
            return false;

        // Pipelines rebuilt for another state (a resize, a reloaded shader) reuse what the driver already compiled
        VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        if (VK_CHECK(vkCreatePipelineCache(m_Device, &cacheInfo, nullptr, &m_PipelineCache)) != VK_SUCCESS)
            m_PipelineCache = VK_NULL_HANDLE;                              // not fatal : VK_NULL_HANDLE is a valid "no cache"

        return true;
    }

    bool Context::CreateSlots()
    {
        for (Slot& slot : m_Slots)
        {
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            poolInfo.queueFamilyIndex = m_QueueFamily;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &slot.pool)) != VK_SUCCESS)
                return false;

            VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocateInfo.commandPool = slot.pool;
            allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocateInfo.commandBufferCount = 1;
            if (VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocateInfo, &slot.cmd)) != VK_SUCCESS)
                return false;

            VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            if (VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &slot.fence)) != VK_SUCCESS)
                return false;
        }

        VkCommandPoolCreateInfo immediatePool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        immediatePool.queueFamilyIndex = m_QueueFamily;
        immediatePool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (VK_CHECK(vkCreateCommandPool(m_Device, &immediatePool, nullptr, &m_ImmediatePool)) != VK_SUCCESS)
            return false;

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_ImmediateFence)) != VK_SUCCESS)
            return false;

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkSemaphore& semaphore : m_ImageAcquired)
        {
            if (VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore)) != VK_SUCCESS)
                return false;
        }

        return true;
    }

    bool Context::CreateUniformRing()
    {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = static_cast<VkDeviceSize>(kUniformBytesPerSlot) * kSlots + kUniformTailPadding;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VmaAllocationInfo result{};
        if (VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocationInfo, &m_UniformRing, &m_UniformRingAllocation, &result)) != VK_SUCCESS)
            return false;

        m_UniformRingMapped = static_cast<uint8_t*>(result.pMappedData);
        SetObjectName(VK_OBJECT_TYPE_BUFFER, m_UniformRing, "Uniform ring");
        if (!m_UniformRingMapped)
            return false;

        // The staging ring : what the CPU writes for the GPU to copy (buffer updates of the frame, readbacks of a region...)
        VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        stagingInfo.size = kStagingBytesPerSlot * kSlots;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo stagingAllocation{};
        stagingAllocation.usage = VMA_MEMORY_USAGE_AUTO;
        stagingAllocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo stagingResult{};
        if (VK_CHECK(vmaCreateBuffer(m_Allocator, &stagingInfo, &stagingAllocation, &m_StagingRing, &m_StagingRingAllocation, &stagingResult)) != VK_SUCCESS)
            return false;
        m_StagingRingMapped = static_cast<uint8_t*>(stagingResult.pMappedData);
        SetObjectName(VK_OBJECT_TYPE_BUFFER, m_StagingRing, "Staging ring");

        // The sets that are not rebuilt every frame
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256}};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1024;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = sizes;
        if (VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_PersistentPool)) != VK_SUCCESS)
            return false;

        return m_StagingRingMapped != nullptr;
    }

    bool Context::CreateBindless()
    {
        const uint32_t count = std::max<uint32_t>(m_Caps.maxBindlessTextures, 1024);

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = count;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        const VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        flagsInfo.bindingCount = 1;
        flagsInfo.pBindingFlags = &bindingFlags;

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.pNext = &flagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        if (VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_BindlessLayout)) != VK_SUCCESS)
            return false;

        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        if (VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_BindlessPool)) != VK_SUCCESS)
            return false;

        VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = m_BindlessPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &m_BindlessLayout;
        return VK_CHECK(vkAllocateDescriptorSets(m_Device, &allocateInfo, &m_BindlessSet)) == VK_SUCCESS;
    }

    bool Context::CreateDummies()
    {
        m_Dummies = std::make_unique<Dummies>();

        struct Kind { ShaderSamplerType type; bool depth; };
        static const Kind kinds[] = {
            {ShaderSamplerType::Texture2D, false}, {ShaderSamplerType::Texture3D, false}, {ShaderSamplerType::TextureCube, false},
            {ShaderSamplerType::Texture2DArray, false}, {ShaderSamplerType::TextureCubeArray, false}, {ShaderSamplerType::Texture1D, false},
            {ShaderSamplerType::Texture1DArray, false}, {ShaderSamplerType::Texture2DMultisample, false},
            {ShaderSamplerType::Texture2DMultisampleArray, false},
            {ShaderSamplerType::Texture2DShadow, true}, {ShaderSamplerType::TextureCubeShadow, true}, {ShaderSamplerType::Texture2DArrayShadow, true},
            {ShaderSamplerType::TextureCubeArrayShadow, true}, {ShaderSamplerType::Texture1DShadow, true}, {ShaderSamplerType::Texture1DArrayShadow, true},
        };

        for (const Kind& kind : kinds)
        {
            Image image;
            ImageCreateInfo info;
            info.width = info.height = 1;
            info.viewType = ViewTypeOf(kind.type);
            info.cube = info.viewType == VK_IMAGE_VIEW_TYPE_CUBE || info.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            info.layers = info.cube ? (info.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY ? 12 : 6) : (info.viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY || info.viewType == VK_IMAGE_VIEW_TYPE_1D_ARRAY ? 2 : 1);
            info.depth = info.viewType == VK_IMAGE_VIEW_TYPE_3D ? 1 : 1;
            info.format = kind.depth ? m_Caps.depthFormat : VK_FORMAT_R8G8B8A8_UNORM;
            info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (kind.depth)
                info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;       // the layout depth images are sampled in needs it
            if (kind.type == ShaderSamplerType::Texture2DMultisample || kind.type == ShaderSamplerType::Texture2DMultisampleArray)
            {
                info.samples = (m_Caps.sampleCounts & VK_SAMPLE_COUNT_4_BIT) ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
                info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            }
            info.name = "Dummy texture";

            if (!image.Create(info))
            {
                DEBUG_WARNING("Could not create a dummy texture of one kind : a shader that samples nothing there may read garbage.");
                continue;
            }

            m_Dummies->images[static_cast<int>(kind.type)] = std::move(image);
        }

        // Clear them (white, depth 1) and take them to the layout they are sampled in
        ImmediateSubmit([&](VkCommandBuffer cmd) {
            for (auto& [type, image] : m_Dummies->images)
            {
                image.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

                VkImageSubresourceRange range{image.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
                if (image.IsDepth())
                {
                    VkClearDepthStencilValue value{1.0f, 0};
                    range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    vkCmdClearDepthStencilImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
                }
                else
                {
                    VkClearColorValue value{};
                    value.float32[0] = value.float32[1] = value.float32[2] = value.float32[3] = 1.0f;
                    vkCmdClearColorImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &range);
                }

                image.TransitionToResting(cmd);
            }
        });

        // Every entry of the bindless array holds the white texture to begin with : an index nobody wrote reads white, not garbage
        const Image& white = GetDummyImage(ShaderSamplerType::Texture2D);
        VkSampler sampler = GetDummySampler(ShaderSamplerType::Texture2D);
        if (white.IsValid() && sampler != VK_NULL_HANDLE && m_BindlessSet != VK_NULL_HANDLE)
        {
            const uint32_t count = std::max<uint32_t>(m_Caps.maxBindlessTextures, 1024);
            std::vector<VkDescriptorImageInfo> infos(count, VkDescriptorImageInfo{sampler, white.view, white.RestingLayout()});

            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = m_BindlessSet;
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorCount = count;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = infos.data();
            vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
        }

        return true;
    }

    const Image& Context::GetDummyImage(ShaderSamplerType type) const
    {
        static const Image empty;
        if (!m_Dummies)
            return empty;

        auto it = m_Dummies->images.find(static_cast<int>(type));
        if (it != m_Dummies->images.end())
            return it->second;

        // The kinds that have none (a 1D one...) read the 2D one
        it = m_Dummies->images.find(static_cast<int>(ShaderSamplerType::Texture2D));
        return it != m_Dummies->images.end() ? it->second : empty;
    }

    VkSampler Context::GetDummySampler(ShaderSamplerType type)
    {
        const int key = static_cast<int>(type);
        auto it = m_Dummies->samplers.find(key);
        if (it != m_Dummies->samplers.end())
            return it->second;

        SamplerKey sampler;
        sampler.minFilter = sampler.magFilter = VK_FILTER_NEAREST;
        sampler.wrapU = sampler.wrapV = sampler.wrapW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        switch (type)
        {
            case ShaderSamplerType::Texture1DShadow:
            case ShaderSamplerType::Texture2DShadow:
            case ShaderSamplerType::TextureCubeShadow:
            case ShaderSamplerType::Texture1DArrayShadow:
            case ShaderSamplerType::Texture2DArrayShadow:
            case ShaderSamplerType::TextureCubeArrayShadow:
                sampler.compare = true;
                sampler.compareOp = VK_COMPARE_OP_LESS;
                break;
            default:
                break;
        }

        VkSampler handle = GetSampler(sampler);
        m_Dummies->samplers[key] = handle;
        return handle;
    }

    bool Context::Init(const InitInfo& info)
    {
        g_Context = this;

        if (!CreateInstanceAndDevice(info) || !CreateSlots() || !CreateUniformRing() || !CreateBindless() || !CreateDummies())
        {
            Shutdown();
            return false;
        }

        m_VsyncWanted = info.vsyncWanted;
        if (m_Surface && !CreateSwapchain(info.width, info.height, info.vsync))
            DEBUG_WARNING("The swapchain could not be created : the picture will only be available through readbacks.");

        DEBUG_INFO("Vulkan device : ", m_Properties.deviceName, " (API ", VK_API_VERSION_MAJOR(m_Properties.apiVersion), ".",
                   VK_API_VERSION_MINOR(m_Properties.apiVersion), ".", VK_API_VERSION_PATCH(m_Properties.apiVersion), ", driver ",
                   m_Properties.driverVersion, ")", m_Caps.validationEnabled ? ", validation layer on" : "");
        return true;
    }

    void Context::Shutdown()
    {
        if (m_Device)
        {
            vkDeviceWaitIdle(m_Device);

            DestroySwapchain();

            // Everything that was waiting to be destroyed
            for (std::function<void()>& work : m_PendingDefers)
                work();
            m_PendingDefers.clear();

            for (Slot& slot : m_Slots)
            {
                for (std::function<void()>& work : slot.deferred)
                    work();
                slot.deferred.clear();
            }

            if (m_Dummies)
            {
                for (auto& [type, image] : m_Dummies->images)
                    image.Destroy();

                // Destroy() defers : nothing is in flight any more, run it now
                for (std::function<void()>& work : m_PendingDefers)
                    work();
                m_PendingDefers.clear();
                for (Slot& slot : m_Slots)
                {
                    for (std::function<void()>& work : slot.deferred)
                        work();
                    slot.deferred.clear();
                }
                m_Dummies.reset();
            }

            for (auto& [key, sampler] : m_Samplers)
                vkDestroySampler(m_Device, sampler, nullptr);
            m_Samplers.clear();

            if (m_BindlessPool)
                vkDestroyDescriptorPool(m_Device, m_BindlessPool, nullptr);
            if (m_BindlessLayout)
                vkDestroyDescriptorSetLayout(m_Device, m_BindlessLayout, nullptr);
            m_BindlessPool = VK_NULL_HANDLE;
            m_BindlessLayout = VK_NULL_HANDLE;

            if (m_UniformRing)
                vmaDestroyBuffer(m_Allocator, m_UniformRing, m_UniformRingAllocation);
            m_UniformRing = VK_NULL_HANDLE;
            if (m_StagingRing)
                vmaDestroyBuffer(m_Allocator, m_StagingRing, m_StagingRingAllocation);
            m_StagingRing = VK_NULL_HANDLE;
            if (m_PersistentPool)
                vkDestroyDescriptorPool(m_Device, m_PersistentPool, nullptr);
            m_PersistentPool = VK_NULL_HANDLE;

            for (VkSemaphore semaphore : m_ImageAcquired)
            {
                if (semaphore)
                    vkDestroySemaphore(m_Device, semaphore, nullptr);
            }

            for (Slot& slot : m_Slots)
            {
                for (VkDescriptorPool pool : slot.descriptors.pools)
                    vkDestroyDescriptorPool(m_Device, pool, nullptr);
                slot.descriptors.pools.clear();
                if (slot.fence)
                    vkDestroyFence(m_Device, slot.fence, nullptr);
                if (slot.pool)
                    vkDestroyCommandPool(m_Device, slot.pool, nullptr);
                slot = Slot{};
            }

            if (m_ImmediateFence)
                vkDestroyFence(m_Device, m_ImmediateFence, nullptr);
            if (m_ImmediatePool)
                vkDestroyCommandPool(m_Device, m_ImmediatePool, nullptr);
            m_ImmediateFence = VK_NULL_HANDLE;
            m_ImmediatePool = VK_NULL_HANDLE;
        }

        if (m_PipelineCache)
            vkDestroyPipelineCache(m_Device, m_PipelineCache, nullptr);
        m_PipelineCache = VK_NULL_HANDLE;

        if (m_Allocator)
            vmaDestroyAllocator(m_Allocator);
        m_Allocator = nullptr;

        if (m_Device)
            vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;

        if (m_Surface && m_Instance)
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;

        if (m_Messenger && m_Instance)
            vkDestroyDebugUtilsMessengerEXT(m_Instance, m_Messenger, nullptr);
        m_Messenger = VK_NULL_HANDLE;

        if (m_Instance)
            vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;

        g_Context = nullptr;
    }

    // ---- Frames --------------------------------------------------------------------------------------------------------------------

    void Context::BeginSlot(Slot& slot)
    {
        if (slot.submitted)
        {
            VK_CHECK(vkWaitForFences(m_Device, 1, &slot.fence, VK_TRUE, UINT64_MAX));
            VK_CHECK(vkResetFences(m_Device, 1, &slot.fence));
            slot.submitted = false;

            // The GPU is done with everything this slot recorded : what was released during it can go
            std::vector<std::function<void()>> deferred;
            deferred.swap(slot.deferred);
            for (std::function<void()>& work : deferred)
                work();

            for (VkDescriptorPool pool : slot.descriptors.pools)
                vkResetDescriptorPool(m_Device, pool, 0);
            slot.descriptors.current = 0;
            slot.setCache.clear();
            slot.uniformUsed = 0;
            slot.stagingUsed = 0;

            VK_CHECK(vkResetCommandPool(m_Device, slot.pool, 0));
        }

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(slot.cmd, &beginInfo));

        if (m_OnCommandBufferBegin)
            m_OnCommandBufferBegin(slot.cmd);
    }

    VkCommandBuffer Context::Cmd()
    {
        Slot& slot = m_Slots[m_Current];
        if (!slot.recording)
        {
            BeginSlot(slot);
            slot.recording = true;
        }
        return slot.cmd;
    }

    void Context::Submit(VkSemaphore wait, VkPipelineStageFlags waitStage, VkSemaphore signal)
    {
        VkCommandBuffer cmd = Cmd();
        Slot& slot = m_Slots[m_Current];

        if (m_OnCommandBufferEnd)
            m_OnCommandBufferEnd(cmd);

        VK_CHECK(vkEndCommandBuffer(cmd));

        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = cmd;

        VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        waitInfo.semaphore = wait;
        waitInfo.stageMask = waitStage ? static_cast<VkPipelineStageFlags2>(waitStage) : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signalInfo.semaphore = signal;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandInfo;
        submitInfo.waitSemaphoreInfoCount = wait ? 1 : 0;
        submitInfo.pWaitSemaphoreInfos = &waitInfo;
        submitInfo.signalSemaphoreInfoCount = signal ? 1 : 0;
        submitInfo.pSignalSemaphoreInfos = &signalInfo;

        VK_CHECK(vkQueueSubmit2(m_Queue, 1, &submitInfo, slot.fence));

        slot.recording = false;
        slot.submitted = true;

        {
            std::lock_guard<std::mutex> lock(m_DeferMutex);
            for (std::function<void()>& work : m_PendingDefers)
                slot.deferred.push_back(std::move(work));
            m_PendingDefers.clear();
        }

        m_Current = (m_Current + 1) % kSlots;
        m_FrameCounter++;
    }

    void Context::Flush()
    {
        if (m_Slots[m_Current].recording)
            Submit();
        VK_CHECK(vkQueueWaitIdle(m_Queue));
    }

    void Context::WaitIdle()
    {
        if (m_Device)
            vkDeviceWaitIdle(m_Device);
    }

    void Context::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& work)
    {
        VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = m_ImmediatePool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocateInfo, &cmd)) != VK_SUCCESS)
            return;

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

        work(cmd);

        VK_CHECK(vkEndCommandBuffer(cmd));

        VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        commandInfo.commandBuffer = cmd;
        VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandInfo;

        VK_CHECK(vkQueueSubmit2(m_Queue, 1, &submitInfo, m_ImmediateFence));
        VK_CHECK(vkWaitForFences(m_Device, 1, &m_ImmediateFence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(m_Device, 1, &m_ImmediateFence));

        vkFreeCommandBuffers(m_Device, m_ImmediatePool, 1, &cmd);
    }

    void Context::Defer(std::function<void()> work)
    {
        std::lock_guard<std::mutex> lock(m_DeferMutex);
        m_PendingDefers.push_back(std::move(work));
    }

    // ---- Descriptors ---------------------------------------------------------------------------------------------------------------

    VkDescriptorPool Context::NewPool()
    {
        const VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 * 8},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096 * 4},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096 * 2},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024},
        };

        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = 4096;
        info.poolSizeCount = static_cast<uint32_t>(sizeof(sizes) / sizeof(sizes[0]));
        info.pPoolSizes = sizes;

        VkDescriptorPool pool = VK_NULL_HANDLE;
        VK_CHECK(vkCreateDescriptorPool(m_Device, &info, nullptr, &pool));
        return pool;
    }

    VkDescriptorSet Context::AllocateSet(VkDescriptorSetLayout layout)
    {
        Slot& slot = m_Slots[m_Current];
        DescriptorPoolSet& pools = slot.descriptors;

        if (pools.pools.empty())
            pools.pools.push_back(NewPool());

        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;

        for (int attempt = 0; attempt < 2; attempt++)
        {
            info.descriptorPool = pools.pools[pools.current];

            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkResult result = vkAllocateDescriptorSets(m_Device, &info, &set);
            if (result == VK_SUCCESS)
                return set;

            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
            {
                VK_CHECK(result);
                return VK_NULL_HANDLE;
            }

            // The pool of this frame is full : the next one (a new one if there is none)
            pools.current++;
            if (pools.current >= pools.pools.size())
                pools.pools.push_back(NewPool());
        }

        return VK_NULL_HANDLE;
    }

    VkDescriptorSet Context::FindCachedSet(uint64_t key) const
    {
        const auto& cache = m_Slots[m_Current].setCache;
        auto it = cache.find(key);
        return it == cache.end() ? VK_NULL_HANDLE : it->second;
    }

    void Context::CacheSet(uint64_t key, VkDescriptorSet set)
    {
        m_Slots[m_Current].setCache[key] = set;
    }

    bool Context::AllocUniform(uint32_t size, uint32_t& offset, void*& mapped)
    {
        // A slot that has not begun its frame still holds what it used 4 frames ago (and the GPU may still read it) : BeginSlot, which
        // waits for that and starts the ring over, runs on the first Cmd(). Allocating before it hands out space that is given out again
        // right after
        Cmd();

        Slot& slot = m_Slots[m_Current];
        const uint32_t align = m_Caps.uniformAlignment;
        const uint32_t start = (slot.uniformUsed + align - 1) / align * align;

        if (start + size > kUniformBytesPerSlot)
            return false;

        offset = m_Current * kUniformBytesPerSlot + start;
        mapped = m_UniformRingMapped + offset;
        slot.uniformUsed = start + size;
        return true;
    }

    bool Context::AllocStaging(VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset, void*& mapped)
    {
        // See AllocUniform : the slot has to have begun its frame, or the first buffers staged after a submit overlap the next ones
        // (a scene uploaded buffer by buffer right after a readback came out as a mix of its buffers)
        Cmd();

        Slot& slot = m_Slots[m_Current];
        const VkDeviceSize start = (slot.stagingUsed + 255) / 256 * 256;

        if (start + size <= kStagingBytesPerSlot)
        {
            buffer = m_StagingRing;
            offset = m_Current * kStagingBytesPerSlot + start;
            mapped = m_StagingRingMapped + offset;
            slot.stagingUsed = start + size;
            return true;
        }

        // Too big for the ring : a buffer for this alone, destroyed when the slot is done
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocation allocation = nullptr;
        VmaAllocationInfo result{};
        if (VK_CHECK(vmaCreateBuffer(m_Allocator, &bufferInfo, &allocationInfo, &buffer, &allocation, &result)) != VK_SUCCESS)
            return false;

        offset = 0;
        mapped = result.pMappedData;

        VmaAllocator allocator = m_Allocator;
        VkBuffer handle = buffer;
        Defer([allocator, handle, allocation] { vmaDestroyBuffer(allocator, handle, allocation); });
        return mapped != nullptr;
    }

    VkDescriptorSet Context::AllocatePersistentSet(VkDescriptorSetLayout layout)
    {
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = m_PersistentPool;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateDescriptorSets(m_Device, &info, &set));
        return set;
    }

    void Context::FreePersistentSet(VkDescriptorSet set)
    {
        if (set == VK_NULL_HANDLE)
            return;

        VkDevice device = m_Device;
        VkDescriptorPool pool = m_PersistentPool;
        Defer([device, pool, set] { vkFreeDescriptorSets(device, pool, 1, &set); });
    }

    VkSampler Context::GetSampler(const SamplerKey& key)
    {
        auto it = m_Samplers.find(key);
        if (it != m_Samplers.end())
            return it->second;

        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.minFilter = key.minFilter;
        info.magFilter = key.magFilter;
        info.mipmapMode = key.mipmapMode;
        info.addressModeU = key.wrapU;
        info.addressModeV = key.wrapV;
        info.addressModeW = key.wrapW;
        info.compareEnable = key.compare ? VK_TRUE : VK_FALSE;
        info.compareOp = key.compareOp;
        info.anisotropyEnable = key.anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
        info.maxAnisotropy = std::min(key.anisotropy, m_Caps.maxAnisotropy);
        info.minLod = 0.0f;
        info.maxLod = key.maxLod;
        info.borderColor = key.border;

        VkSampler sampler = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSampler(m_Device, &info, nullptr, &sampler));
        m_Samplers[key] = sampler;
        return sampler;
    }

    void Context::WriteBindless(uint32_t index, VkImageView view, VkSampler sampler, VkImageLayout layout)
    {
        if (index >= m_Caps.maxBindlessTextures || m_BindlessSet == VK_NULL_HANDLE || view == VK_NULL_HANDLE)
            return;

        VkDescriptorImageInfo imageInfo{sampler, view, layout};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = m_BindlessSet;
        write.dstBinding = 0;
        write.dstArrayElement = index;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }

    // ---- Swapchain -----------------------------------------------------------------------------------------------------------------

    bool Context::CreateSwapchain(int width, int height, bool vsync)
    {
        if (m_Surface == VK_NULL_HANDLE)
            return false;

        vkDeviceWaitIdle(m_Device);

        vkb::SwapchainBuilder builder(m_PhysicalDevice, m_Device, m_Surface, m_QueueFamily, m_QueueFamily);
        builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
            .set_desired_present_mode(vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR)
            .add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
            .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(static_cast<uint32_t>(std::max(width, 1)), static_cast<uint32_t>(std::max(height, 1)))
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            .set_old_swapchain(m_Swapchain);

        auto result = builder.build();
        if (!result)
        {
            DEBUG_ERROR("Could not create the swapchain : ", result.error().message());
            return false;
        }

        vkb::Swapchain swapchain = result.value();

        // Whatever came from the old one
        std::vector<VkSemaphore> oldSemaphores;
        oldSemaphores.swap(m_RenderFinished);
        for (VkSemaphore semaphore : oldSemaphores)
            vkDestroySemaphore(m_Device, semaphore, nullptr);
        for (VkImageView view : m_SwapchainViews)
            vkDestroyImageView(m_Device, view, nullptr);
        m_SwapchainViews.clear();

        if (m_Swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);

        m_Swapchain = swapchain.swapchain;
        m_SwapchainFormat = swapchain.image_format;
        m_SwapchainExtent = swapchain.extent;
        m_Vsync = vsync;
        m_SwapchainDirty = false;
        m_SwapchainImages = swapchain.get_images().value();

        m_SwapchainViews.resize(m_SwapchainImages.size());
        for (size_t i = 0; i < m_SwapchainImages.size(); i++)
        {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = m_SwapchainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_SwapchainFormat;
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_SwapchainViews[i]));
        }

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        m_RenderFinished.resize(m_SwapchainImages.size());
        for (VkSemaphore& semaphore : m_RenderFinished)
            VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore));

        return true;
    }

    void Context::DestroySwapchain()
    {
        if (m_Device == VK_NULL_HANDLE)
            return;

        for (VkSemaphore semaphore : m_RenderFinished)
            vkDestroySemaphore(m_Device, semaphore, nullptr);
        m_RenderFinished.clear();

        for (VkImageView view : m_SwapchainViews)
            vkDestroyImageView(m_Device, view, nullptr);
        m_SwapchainViews.clear();

        if (m_Swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
        m_SwapchainImages.clear();
    }

    bool Context::Present(Image& source, uint32_t width, uint32_t height)
    {
        if (m_Swapchain == VK_NULL_HANDLE || !source.IsValid())
            return false;

        if (width == 0 || height == 0)
            return false;      // minimised

        // A window that is hidden or minimised has a surface of no size : nothing to draw on
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities) == VK_SUCCESS &&
                capabilities.currentExtent.width != 0xFFFFFFFFu && (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0))
                return false;
        }

        const bool vsyncWanted = m_VsyncWanted ? m_VsyncWanted() : m_Vsync;
        if (m_SwapchainDirty || vsyncWanted != m_Vsync || width != m_SwapchainExtent.width || height != m_SwapchainExtent.height)
        {
            Flush();
            if (!CreateSwapchain(static_cast<int>(width), static_cast<int>(height), vsyncWanted))
                return false;
        }

        // The slot the present is recorded in is the one after the frame's : its acquire semaphore is free once its fence has been waited for
        VkCommandBuffer cmd = Cmd();
        const VkSemaphore acquired = m_ImageAcquired[m_Current];

        uint32_t index = 0;
        VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_SwapchainDirty = true;
            // Nothing to present : close the command buffer of this slot empty
            Submit();
            return false;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            VK_CHECK(result);
            Submit();
            return false;
        }

        VkImage target = m_SwapchainImages[index];

        const VkImageLayout sourceLayout = source.layout;
        source.Transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        TransitionAcquiredImage(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT);

        // The picture is stored bottom row first, like OpenGL's : the blit flips it
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[0] = {0, static_cast<int32_t>(source.height), 0};
        blit.srcOffsets[1] = {static_cast<int32_t>(source.width), 0, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {static_cast<int32_t>(m_SwapchainExtent.width), static_cast<int32_t>(m_SwapchainExtent.height), 1};
        vkCmdBlitImage(cmd, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        TransitionImageRaw(cmd, target, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        (void)sourceLayout;
        source.TransitionToResting(cmd);

        Submit(acquired, VK_PIPELINE_STAGE_TRANSFER_BIT, m_RenderFinished[index]);

        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_RenderFinished[index];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_Swapchain;
        presentInfo.pImageIndices = &index;

        result = vkQueuePresentKHR(m_Queue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            m_SwapchainDirty = true;
        else
            VK_CHECK(result);

        return true;
    }

    bool Context::PresentUI(const std::function<void(VkCommandBuffer)>& draw, uint32_t width, uint32_t height, const float clearColor[4])
    {
        if (m_Swapchain == VK_NULL_HANDLE || width == 0 || height == 0)
            return false;      // no window, or minimised

        // A window that is hidden or minimised has a surface of no size : nothing to draw on
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities) == VK_SUCCESS &&
                capabilities.currentExtent.width != 0xFFFFFFFFu && (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0))
                return false;
        }

        const bool vsyncWanted = m_VsyncWanted ? m_VsyncWanted() : m_Vsync;
        if (m_SwapchainDirty || vsyncWanted != m_Vsync || width != m_SwapchainExtent.width || height != m_SwapchainExtent.height)
        {
            Flush();
            if (!CreateSwapchain(static_cast<int>(width), static_cast<int>(height), vsyncWanted))
                return false;
        }

        VkCommandBuffer cmd = Cmd();
        const VkSemaphore acquired = m_ImageAcquired[m_Current];

        uint32_t index = 0;
        VkResult result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_SwapchainDirty = true;
            Submit();
            return false;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            VK_CHECK(result);
            Submit();
            return false;
        }

        TransitionAcquiredImage(cmd, m_SwapchainImages[index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = m_SwapchainViews[index];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.clearValue.color = {{clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};

        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, m_SwapchainExtent};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;

        vkCmdBeginRendering(cmd, &rendering);

        VkViewport viewport{0.0f, 0.0f, static_cast<float>(m_SwapchainExtent.width), static_cast<float>(m_SwapchainExtent.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, m_SwapchainExtent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        draw(cmd);

        vkCmdEndRendering(cmd);
        TransitionImageRaw(cmd, m_SwapchainImages[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        Submit(acquired, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, m_RenderFinished[index]);

        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_RenderFinished[index];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_Swapchain;
        presentInfo.pImageIndices = &index;

        result = vkQueuePresentKHR(m_Queue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            m_SwapchainDirty = true;
        else
            VK_CHECK(result);

        return true;
    }

}
