#include "vk_mesh.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "vk_api.hpp"

#include "engine/renderer/rhi/gpu_memory.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"

namespace Shard::Engine::Rendering {

    using namespace VK;

    // ---- VKMesh --------------------------------------------------------------------------------------------------------------------

    VKMesh::~VKMesh()
    {
        GpuMemory::Add(GpuMemory::Kind::Mesh, -m_GpuBytes);
        if (Context::Exists())
        {
            m_Vertex.Destroy();
            m_Index.Destroy();
        }
    }

    void VKMesh::Create(std::vector<uint8_t> vertices, std::vector<uint32_t> indices, const VertexLayout& layout)
    {
        m_Vertices.clear();
        m_Submeshes.clear();
        m_Indices.clear();

        m_Vertices = vertices;
        m_Indices = indices;
        m_Submeshes.push_back({0, indices.size(), vertices.size() / layout.GetStride()});

        m_VertexCount = vertices.size() / layout.GetStride();
        m_VertexLayout = layout;

        m_BoundsMin = glm::vec3(std::numeric_limits<float>::max());
        m_BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());

        const uint32_t stride = layout.GetStride();
        auto position = std::find_if(layout.GetElements().begin(), layout.GetElements().end(), [](const VertexElement& e) { return e.name == "aPos"; });

        if (position != layout.GetElements().end() && stride > 0)
        {
            for (size_t i = 0; i < m_VertexCount; i++)
            {
                glm::vec3 pos;
                std::memcpy(&pos, vertices.data() + i * stride + position->offset, sizeof(glm::vec3));
                m_BoundsMin = glm::min(m_BoundsMin, pos);
                m_BoundsMax = glm::max(m_BoundsMax, pos);
            }
        }

        m_Submeshes[0].boundsMin = m_BoundsMin;
        m_Submeshes[0].boundsMax = m_BoundsMax;

        CreateBuffers();
    }

    void VKMesh::CreateFromFBX(const ufbx_mesh* ufbx_mesh, double scene_unit_meters, ufbx_material_list& ufbx_mats, ufbx_node* mesh_node, COL_RGBA vertexColor)
    {
        CreateFromData(BuildMeshCPUDataFromFBX(ufbx_mesh, scene_unit_meters, ufbx_mats, mesh_node, vertexColor));
    }

    void VKMesh::CreateFromData(const MeshCPUData& data)
    {
        m_Vertices = data.vertices;
        m_Indices = data.indices;
        m_Submeshes = data.submeshes;
        m_VertexLayout = data.layout;
        m_VertexCount = data.vertices.size() / (data.layout.GetStride() ? data.layout.GetStride() : 1);
        m_BoundsMin = data.boundsMin;
        m_BoundsMax = data.boundsMax;

        CreateBuffers();
    }

    void VKMesh::CreateBuffers()
    {
        m_Vertex.Destroy();
        m_Index.Destroy();

        const VkBufferUsageFlags readable = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        CreateBufferWithData(m_Vertex, m_Vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | readable, m_Vertices.data(), "Mesh vertices");
        CreateBufferWithData(m_Index, m_Indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | readable, m_Indices.data(), "Mesh indices");

        const int64_t bytes = int64_t(m_Vertices.size()) + int64_t(m_Indices.size() * sizeof(uint32_t));
        GpuMemory::Add(GpuMemory::Kind::Mesh, bytes - m_GpuBytes);
        m_GpuBytes = bytes;
        m_VertexBytes = m_Vertices.size();

        // On the GPU now : the CPU copy is not needed (see Mesh::AcquireCPUData)
        FinishCPUData();
    }

    bool VKMesh::ReadBackCPUData(MeshCPUCopy& out)
    {
        if (!m_Vertex.IsValid() || !m_Index.IsValid())
            return false;

        out.vertices.resize(m_VertexBytes);
        out.indices.resize(m_IndexCount);
        return ReadBuffer(m_Vertex, 0, out.vertices.size(), out.vertices.data()) &&
               ReadBuffer(m_Index, 0, out.indices.size() * sizeof(uint32_t), out.indices.data());
    }

    // ---- VKStorageBuffer -----------------------------------------------------------------------------------------------------------

    namespace {
        std::mutex g_BufferMutex;
        std::unordered_map<uint32_t, VKStorageBuffer*> g_Buffers;
        uint32_t g_NextBufferId = 1;
    }

    VKStorageBuffer* VKStorageBuffer::Find(uint32_t id)
    {
        std::lock_guard<std::mutex> lock(g_BufferMutex);
        auto it = g_Buffers.find(id);
        return it == g_Buffers.end() ? nullptr : it->second;
    }

    VKStorageBuffer::VKStorageBuffer(uint32_t size)
    {
        {
            std::lock_guard<std::mutex> lock(g_BufferMutex);
            m_Id = g_NextBufferId++;
            g_Buffers[m_Id] = this;
        }

        Allocate(size);
        GpuMemory::Add(GpuMemory::Kind::Buffer, int64_t(size));
    }

    VKStorageBuffer::~VKStorageBuffer()
    {
        {
            std::lock_guard<std::mutex> lock(g_BufferMutex);
            g_Buffers.erase(m_Id);
        }

        GpuMemory::Add(GpuMemory::Kind::Buffer, -int64_t(m_Size));
        if (Context::Exists())
            m_Buffer.Destroy();
    }

    void VKStorageBuffer::Allocate(uint32_t size)
    {
        m_Buffer.Destroy();
        m_Size = size;

        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        if (!m_Buffer.Create(std::max(size, 4u), usage, false, "Storage buffer"))
        {
            DEBUG_ERROR("Could not create a storage buffer of ", size, " bytes.");
            return;
        }

        // Zeros : what a buffer nobody has written yet holds
        Context::Get().ImmediateSubmit([&](VkCommandBuffer cmd) { vkCmdFillBuffer(cmd, m_Buffer.buffer, 0, VK_WHOLE_SIZE, 0); });
    }

    void VKStorageBuffer::SetData(const void* data, uint32_t size)
    {
        if (size != m_Size)
        {
            // A new size is a new buffer : whatever was recorded against the old one still reads it (it is destroyed when they are done)
            GpuMemory::Add(GpuMemory::Kind::Buffer, int64_t(size) - int64_t(m_Size));

            m_Buffer.Destroy();
            m_Size = size;

            const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            if (!CreateBufferWithData(m_Buffer, std::max(size, 4u), usage, data, "Storage buffer"))
                DEBUG_ERROR("Could not create a storage buffer of ", size, " bytes.");
            return;
        }

        if (m_Buffer.IsValid() && data)
            RecordBufferUpdate(m_Buffer, 0, data, size);
    }

    void VKStorageBuffer::GetData(void* outData, uint32_t size, uint32_t offset) const
    {
        if ((uint64_t)offset + (uint64_t)size > (uint64_t)m_Size)
        {
            DEBUG_ERROR("GetData range [" + std::to_string(offset) + ", " + std::to_string(offset + size) + ") exceeds storage buffer size (" + std::to_string(m_Size) + ") - buffer was not read back.");
            return;
        }

        ReadBuffer(m_Buffer, offset, size, outData);
    }

    void VKStorageBuffer::Bind(uint32_t binding)
    {
        if (VKRendererAPI* api = VKRendererAPI::Get())
            api->BindStorageBuffer(binding, m_Id);
    }

}
