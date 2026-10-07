#pragma once

#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"

namespace Shard::Engine::Rendering{

    class GLStorageBuffer : public StorageBuffer
    {
        public:
            GLStorageBuffer(uint32_t size);
            
            ~GLStorageBuffer();

            void SetData(const void* data, uint32_t size) override;

            void GetData(void* outData, uint32_t size, uint32_t offset = 0) const override;

            void Bind(uint32_t binding) override;

            uint32_t GetSize() const override { return m_Size; }

            uint32_t GetHandle() const override { return m_Buffer; }

        private:
            uint32_t m_Buffer;
            uint32_t m_Size;
    };
}