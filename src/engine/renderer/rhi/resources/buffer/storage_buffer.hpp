#pragma once

#include <memory>
#include <cstdint>

namespace Shard::Engine::Rendering{

    class StorageBuffer
    {
        public:
            virtual ~StorageBuffer() = default;

            virtual void SetData(const void* data, uint32_t size) = 0;

            // Blocking readback of this buffer's content into CPU memory (e.g. reading back results a
            // compute shader wrote via an SSBO). If a compute shader wrote to this buffer, a
            // MemoryBarrierBit::BufferUpdate barrier must be issued after the dispatch and before this
            // call, or the read may return stale data.
            virtual void GetData(void* outData, uint32_t size, uint32_t offset = 0) const = 0;

            virtual void Bind(uint32_t binding) = 0;

            virtual uint32_t GetSize() const = 0;

            static std::shared_ptr<StorageBuffer> Create(uint32_t size);
    };

}