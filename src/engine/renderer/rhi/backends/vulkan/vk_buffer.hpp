#pragma once

#include <string>

#include "vk_context.hpp"

namespace Shard::Engine::Rendering::VK {

    /// @brief A Vulkan buffer and its memory.
    struct Buffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        uint8_t* mapped = nullptr;      ///< only for a host visible buffer

        bool IsValid() const { return buffer != VK_NULL_HANDLE; }

        /// `hostVisible` : the CPU can write it directly (and it stays mapped) ; otherwise it lives in device memory
        bool Create(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, const std::string& name);

        /// Hands the buffer to the GPU's deferred destruction (a command that was recorded may still read it)
        void Destroy();
    };

    /// A new device buffer holding `data`, written right away (nothing that was recorded can use a buffer that did not exist)
    bool CreateBufferWithData(Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage, const void* data, const std::string& name);

    /// Records the copy of `data` into an existing buffer, in the command stream of the frame : whatever was recorded before sees the old content,
    /// whatever is recorded after sees the new one.
    bool RecordBufferUpdate(Buffer& destination, VkDeviceSize offset, const void* data, VkDeviceSize size);

    /// Reads a range of a buffer back to the CPU : submits and waits for everything recorded so far
    bool ReadBuffer(const Buffer& source, VkDeviceSize offset, VkDeviceSize size, void* out);

    /// A barrier between everything that was recorded and everything that will be (memory, all stages)
    void GlobalBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

}
