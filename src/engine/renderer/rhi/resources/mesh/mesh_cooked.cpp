#include "engine/renderer/rhi/resources/mesh/mesh_cooked.hpp"

#include <cstring>
#include <fstream>

namespace Shard::Engine::Rendering {

    std::string CookedMeshPath(const std::string& sourcePath) { return sourcePath + ".smesh"; }

    namespace {
        // File layout (native byte order : a cooked file is for the platform it was cooked on, and checked by its version) :
        //   "SMSH", u32 version, u32 stride, u64 vertexBytes, u64 indexCount, u32 submeshCount, 6 floats bounds, then vertices, indices, submeshes
        //   submesh : u64 indexOffset, u64 indexCount, u64 vertexCount, u32 materialIndex, 6 floats bounds
        constexpr char kMagic[4] = { 'S', 'M', 'S', 'H' };

        void Put(std::vector<uint8_t>& out, const void* data, size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            out.insert(out.end(), p, p + size);
        }

        template <class T>
        void PutValue(std::vector<uint8_t>& out, const T& value) { Put(out, &value, sizeof(T)); }

        struct Cursor {
            const uint8_t* data;
            size_t size;
            size_t at = 0;
            bool Take(void* dst, size_t count)
            {
                if (count > size - at) return false;
                if (count) std::memcpy(dst, data + at, count);
                at += count;
                return true;
            }
            template <class T>
            bool TakeValue(T& value) { return Take(&value, sizeof(T)); }
        };
    }

    std::vector<uint8_t> SerializeMeshData(const MeshCPUData& data)
    {
        std::vector<uint8_t> out;
        out.reserve(64 + data.vertices.size() + data.indices.size() * 4 + data.submeshes.size() * 56);
        Put(out, kMagic, 4);
        PutValue(out, kCookedMeshVersion);
        PutValue(out, uint32_t(data.layout.GetStride()));
        PutValue(out, uint64_t(data.vertices.size()));
        PutValue(out, uint64_t(data.indices.size()));
        PutValue(out, uint32_t(data.submeshes.size()));
        PutValue(out, data.boundsMin);
        PutValue(out, data.boundsMax);
        Put(out, data.vertices.data(), data.vertices.size());
        Put(out, data.indices.data(), data.indices.size() * sizeof(uint32_t));
        for (const SubMesh& s : data.submeshes) {
            PutValue(out, uint64_t(s.indexOffset));
            PutValue(out, uint64_t(s.indexCount));
            PutValue(out, uint64_t(s.vertexCount));
            PutValue(out, s.materialIndex);
            PutValue(out, s.boundsMin);
            PutValue(out, s.boundsMax);
        }
        return out;
    }

    bool DeserializeMeshData(const uint8_t* bytes, size_t size, MeshCPUData& out)
    {
        Cursor in{ bytes, size };
        char magic[4];
        uint32_t version = 0, stride = 0, submeshCount = 0;
        uint64_t vertexBytes = 0, indexCount = 0;
        MeshCPUData mesh;
        if (!in.Take(magic, 4) || std::memcmp(magic, kMagic, 4) != 0 || !in.TakeValue(version) || version != kCookedMeshVersion) return false;
        if (!in.TakeValue(stride) || !in.TakeValue(vertexBytes) || !in.TakeValue(indexCount) || !in.TakeValue(submeshCount)) return false;
        if (!in.TakeValue(mesh.boundsMin) || !in.TakeValue(mesh.boundsMax)) return false;

        const VertexLayout layout = StandardMeshLayout();
        if (stride == 0 || stride != layout.GetStride() || vertexBytes % stride != 0) return false;
        const size_t remaining = size - in.at;
        if (vertexBytes > remaining || indexCount > (remaining - vertexBytes) / sizeof(uint32_t)) return false;

        mesh.vertices.resize(size_t(vertexBytes));
        mesh.indices.resize(size_t(indexCount));
        if (!in.Take(mesh.vertices.data(), mesh.vertices.size()) || !in.Take(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t))) return false;

        const uint64_t vertexCount = vertexBytes / stride;
        mesh.submeshes.reserve(submeshCount);
        for (uint32_t i = 0; i < submeshCount; ++i) {
            SubMesh s;
            uint64_t offset = 0, count = 0, vertices = 0;
            if (!in.TakeValue(offset) || !in.TakeValue(count) || !in.TakeValue(vertices) || !in.TakeValue(s.materialIndex) ||
                !in.TakeValue(s.boundsMin) || !in.TakeValue(s.boundsMax))
                return false;
            if (offset > indexCount || count > indexCount - offset || vertices > vertexCount) return false;
            s.indexOffset = size_t(offset);
            s.indexCount = size_t(count);
            s.vertexCount = size_t(vertices);
            mesh.submeshes.push_back(s);
        }
        for (uint32_t index : mesh.indices)
            if (index >= vertexCount) return false;

        mesh.layout = layout;
        mesh.success = true;
        out = std::move(mesh);
        return true;
    }

    bool LoadCookedMesh(const std::string& sourcePath, MeshCPUData& out)
    {
        std::ifstream file(CookedMeshPath(sourcePath), std::ios::binary | std::ios::ate);
        if (!file) return false;
        const std::streamsize size = file.tellg();
        if (size <= 0) return false;
        file.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return false;
        return DeserializeMeshData(bytes.data(), bytes.size(), out);
    }
}
