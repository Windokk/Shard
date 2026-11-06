#pragma once

#include <memory>
#include <cstdint>

namespace Shard::Engine::Rendering{

    class StorageBuffer
    {
        public:
            virtual ~StorageBuffer() = default;

            virtual void SetData(const void* data, uint32_t size) = 0;

            // Needs a MemoryBarrierBit::BufferUpdate barrier after a compute write, or the read may be stale.
            virtual void GetData(void* outData, uint32_t size, uint32_t offset = 0) const = 0;

            virtual void Bind(uint32_t binding) = 0;

            virtual uint32_t GetSize() const = 0;

            virtual uint32_t GetHandle() const = 0;

            static std::shared_ptr<StorageBuffer> Create(uint32_t size);
    };

}