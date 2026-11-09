#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

namespace Shard::Engine::Rendering {

    // The vertex layout every model gets (position, uv, normal, colour, tangent, lightmap uv)
    VertexLayout StandardMeshLayout();

    // A model cooked by ShardCooker sits next to its source : <source>.smesh. It holds the finished vertices, indices and submeshes,
    // so the player neither parses the FBX nor rebuilds tangents and lightmap UVs.
    constexpr uint32_t kCookedMeshVersion = 1;
    std::string CookedMeshPath(const std::string& sourcePath);

    std::vector<uint8_t> SerializeMeshData(const MeshCPUData& data);
    bool DeserializeMeshData(const uint8_t* bytes, size_t size, MeshCPUData& out);
    // Reads <source>.smesh. False when there is none or it is not valid.
    bool LoadCookedMesh(const std::string& sourcePath, MeshCPUData& out);
}
