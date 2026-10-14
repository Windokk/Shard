#include "vk_buffer.hpp"

#include <cstring>

#include "vk_vma.hpp"

namespace Shard::Engine::Rendering::VK {

    bool Buffer::Create(VkDeviceSize bufferSize, VkBufferUsageFlags bufferUsage, bool hostVisible, const std::string& name)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = std::max<VkDeviceSize>(bufferSize, 4);
        info.usage = bufferUsage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        if (hostVisible)
            allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        else
            allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VmaAllocationInfo result{};
        if (VK_CHECK(vmaCreateBuffer(Context::Get().GetAllocator(), &info, &allocationInfo, &buffer, &allocation, &result)) != VK_SUCCESS)
        {
            buffer = VK_NULL_HANDLE;
            return false;
        }

        size = info.size;
        usage = bufferUsage;
        mapped = static_cast<uint8_t*>(result.pMappedData);
        SetObjectName(VK_OBJECT_TYPE_BUFFER, buffer, name);
        return true;
    }

    void Buffer::Destroy()
    {
        if (buffer == VK_NULL_HANDLE)
            return;

        VmaAllocator allocator = Context::Get().GetAllocator();
        VkBuffer handle = buffer;
        VmaAllocation memory = allocation;
        Context::Get().Defer([allocator, handle, memory] { vmaDestroyBuffer(allocator, handle, memory); });

        buffer = VK_NULL_HANDLE;
        allocation = nullptr;
        mapped = nullptr;
        size = 0;
    }

    void GlobalBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
    {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = srcStage;
        barrier.srcAccessMask = srcAccess;
        barrier.dstStageMask = dstStage;
        barrier.dstAccessMask = dstAccess;

        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    bool CreateBufferWithData(Buffer& out, VkDeviceSize size, VkBufferUsageFlags usage, const void* data, const std::string& name)
    {
        if (!out.Create(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, name))
            return false;

        if (!data || size == 0)
            return true;

        Buffer staging;
        if (!staging.Create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, name + " (staging)"))
            return false;

        std::memcpy(staging.mapped, data, static_cast<size_t>(size));

        Context::Get().ImmediateSubmit([&](VkCommandBuffer cmd) {
            VkBufferCopy region{0, 0, size};
            vkCmdCopyBuffer(cmd, staging.buffer, out.buffer, 1, &region);
        });

        // The copy is done (ImmediateSubmit waits) : the staging buffer can go straight away
        VmaAllocator allocator = Context::Get().GetAllocator();
        vmaDestroyBuffer(allocator, staging.buffer, staging.allocation);
        return true;
    }

    bool RecordBufferUpdate(Buffer& destination, VkDeviceSize offset, const void* data, VkDeviceSize size)
    {
        if (size == 0)
            return true;

        Context& context = Context::Get();
        context.EnsureOutsideRendering();

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceSize stagingOffset = 0;
        void* mapped = nullptr;
        if (!context.AllocStaging(size, staging, stagingOffset, mapped))
            return false;

        std::memcpy(mapped, data, static_cast<size_t>(size));

        VkCommandBuffer cmd = context.Cmd();

        // What read the buffer before has to be done with it, then the copy has to be done before what reads it next
        GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkBufferCopy region{stagingOffset, offset, size};
        vkCmdCopyBuffer(cmd, staging, destination.buffer, 1, &region);

        GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        return true;
    }

    bool ReadBuffer(const Buffer& source, VkDeviceSize offset, VkDeviceSize size, void* out)
    {
        Context& context = Context::Get();
        context.EnsureOutsideRendering();

        // Host visible memory the GPU writes : a buffer of its own, mapped
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer destination = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo result{};
        if (VK_CHECK(vmaCreateBuffer(context.GetAllocator(), &info, &allocationInfo, &destination, &allocation, &result)) != VK_SUCCESS)
            return false;

        VkCommandBuffer cmd = context.Cmd();
        GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferCopy region{offset, 0, size};
        vkCmdCopyBuffer(cmd, source.buffer, destination, 1, &region);
        GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);

        context.Flush();

        vmaInvalidateAllocation(context.GetAllocator(), allocation, 0, size);
        std::memcpy(out, result.pMappedData, static_cast<size_t>(size));
        vmaDestroyBuffer(context.GetAllocator(), destination, allocation);
        return true;
    }

}
