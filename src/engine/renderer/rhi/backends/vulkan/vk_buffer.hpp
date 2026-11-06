#pragma once

#include <string>

#include "vk_context.hpp"

namespace Shard::Engine::Rendering::VK {

    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        uint8_t* mapped = nullptr;

        bool IsValid() const { return buffer != VK_NULL_HANDLE; }

        bool Create(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, const std::string& name);

        void Destroy();
    };

    bool CreateBufferWithData(Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage, const void* data, const std::string& name);

    // Recorded in the frame's command stream: earlier commands see the old content, later ones the new.
    bool RecordBufferUpdate(Buffer& destination, VkDeviceSize offset, const void* data, VkDeviceSize size);

    bool ReadBuffer(const Buffer& source, VkDeviceSize offset, VkDeviceSize size, void* out);

    void GlobalBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

}
