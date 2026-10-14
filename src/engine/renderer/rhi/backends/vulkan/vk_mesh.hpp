#pragma once

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"

#include "vk_buffer.hpp"

namespace Shard::Engine::Rendering {

    class VKMesh final : public Mesh
    {
        public:

            VKMesh() = default;
            ~VKMesh() override;

            void Create(std::vector<uint8_t> vertices, std::vector<uint32_t> indices, const VertexLayout& layout) override;

            void CreateFromFBX(const ufbx_mesh* ufbx_mesh, double scene_unit_meters, ufbx_material_list& ufbx_mats, ufbx_node* mesh_node,
                               COL_RGBA vertexColor = COL_RGBA(0.99f, 0.06f, 0.75f, 1.0f)) override;

            void CreateFromData(const MeshCPUData& data) override;

            VkBuffer GetVertexBuffer() const { return m_Vertex.buffer; }
            VkBuffer GetIndexBuffer() const { return m_Index.buffer; }

            bool ReadBackCPUData(MeshCPUCopy& out) override;

        private:

            void CreateBuffers();

            VK::Buffer m_Vertex;
            VK::Buffer m_Index;
            int64_t m_GpuBytes = 0;
            size_t m_VertexBytes = 0;
    };

    /// A buffer the shaders read and write (an SSBO). It goes by a number, like a texture : the binding tables of the API look it up when they are used.
    class VKStorageBuffer final : public StorageBuffer
    {
        public:

            explicit VKStorageBuffer(uint32_t size);
            ~VKStorageBuffer() override;

            void SetData(const void* data, uint32_t size) override;
            void GetData(void* outData, uint32_t size, uint32_t offset = 0) const override;
            void Bind(uint32_t binding) override;
            uint32_t GetSize() const override { return m_Size; }
            uint32_t GetHandle() const override { return m_Id; }

            VkBuffer GetBuffer() const { return m_Buffer.buffer; }

            /// The buffer that goes by this number (null if there is none any more)
            static VKStorageBuffer* Find(uint32_t id);

        private:

            void Allocate(uint32_t size);

            VK::Buffer m_Buffer;
            uint32_t m_Size = 0;
            uint32_t m_Id = 0;
    };

}
