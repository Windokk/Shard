#include "mesh.hpp"

#include "engine/renderer/rhi/resources/mesh/lightmap_uv.hpp"
#include "engine/renderer/rhi/backends/opengl/mesh/gl_mesh.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/world/engine.hpp"
#include "engine/core/config/cvar.hpp"

#include <memory>
#include <unordered_map>
#include <cstring>
#include <algorithm>
#include <glm/gtc/epsilon.hpp>

namespace {

    struct Vertex {
        glm::vec3 position;
        glm::vec2 texCoord;
        glm::vec3 normal;
        glm::vec4 color;
        glm::vec3 tangent;
        // Where the vertex sits in the mesh's lightmap layout (see lightmap_uv.hpp) : the second set of texture coordinates,
        // for baked lighting. (0, 0) on a mesh that was not unwrapped.
        glm::vec2 lightmapUV;

        bool operator==(const Vertex& other) const {
            const float eps = 0.0001f;
            return glm::all(glm::epsilonEqual(position, other.position, eps)) &&
                glm::all(glm::epsilonEqual(normal, other.normal, eps)) &&
                glm::all(glm::epsilonEqual(color, other.color, eps)) &&
                glm::all(glm::epsilonEqual(texCoord, other.texCoord, eps))&&
                glm::all(glm::epsilonEqual(tangent, other.tangent, eps)) &&
                glm::all(glm::epsilonEqual(lightmapUV, other.lightmapUV, eps));
        }
    };

    inline int Quantize(float v) {
        return static_cast<int>(v * 10000.0f); // match epsilon ~0.0001
    }

    inline void HashCombine(std::size_t& seed, std::size_t hash)
    {
        seed ^= hash + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
}

namespace std {
    template <>
    struct hash<Vertex> {
        size_t operator()(const Vertex& v) const {
            size_t seed = 0;

            auto h = [&](float f) {
                return std::hash<int>()(Quantize(f));
            };

            HashCombine(seed, h(v.position.x));
            HashCombine(seed, h(v.position.y));
            HashCombine(seed, h(v.position.z));

            HashCombine(seed, h(v.normal.x));
            HashCombine(seed, h(v.normal.y));
            HashCombine(seed, h(v.normal.z));

            HashCombine(seed, h(v.texCoord.x));
            HashCombine(seed, h(v.texCoord.y));

            HashCombine(seed, h(v.color.r));
            HashCombine(seed, h(v.color.g));
            HashCombine(seed, h(v.color.b));
            HashCombine(seed, h(v.color.a));

            HashCombine(seed, h(v.tangent.x));
            HashCombine(seed, h(v.tangent.y));
            HashCombine(seed, h(v.tangent.z));

            HashCombine(seed, h(v.lightmapUV.x));
            HashCombine(seed, h(v.lightmapUV.y));

            return seed;
        }
    };
}

namespace Shard::Engine::Rendering{

    namespace {

        // Finds the vertex equal to a given one among those already added, or adds it. Open addressing over the hash of the
        // vertex (the hash is kept in the slot, two vertices are only compared when their hashes are the same : the same
        // answers as the std::unordered_map this replaces, which a Sponza sized mesh (millions of vertices) spent most of
        // its loading time in allocating nodes).
        class VertexDeduplicator {
        public:
            explicit VertexDeduplicator(size_t expected) { Reset(NextPowerOfTwo(std::max<size_t>(expected, 16) * 2)); }

            /// The index of the vertex equal to `v` in `verts`, which it is appended to when there is none
            unsigned int FindOrAdd(const Vertex& v, std::vector<Vertex>& verts)
            {
                const size_t hash = std::hash<Vertex>()(v);
                size_t slot = SlotOf(hash);
                for (;;)
                {
                    Slot& s = m_Slots[slot];
                    if (s.index == kEmpty)
                    {
                        s.hash = hash;
                        s.index = static_cast<unsigned int>(verts.size());
                        verts.push_back(v);
                        if (++m_Count * 2 > m_Slots.size())
                            Grow();
                        return static_cast<unsigned int>(verts.size() - 1);
                    }
                    if (s.hash == hash && verts[s.index] == v)
                        return s.index;
                    slot = (slot + 1) & m_Mask;
                }
            }

        private:
            static constexpr unsigned int kEmpty = std::numeric_limits<unsigned int>::max();
            struct Slot { size_t hash = 0; unsigned int index = kEmpty; };

            static size_t NextPowerOfTwo(size_t n)
            {
                size_t p = 1;
                while (p < n)
                    p <<= 1;
                return p;
            }

            void Reset(size_t capacity)
            {
                m_Slots.assign(capacity, Slot{});
                m_Mask = capacity - 1;
                m_Shift = 64;
                for (size_t c = capacity; c > 1; c >>= 1)
                    m_Shift--;
            }

            // Fibonacci hashing : the top bits of the product, whatever the quality of the low bits of the hash
            size_t SlotOf(size_t hash) const { return static_cast<size_t>((static_cast<uint64_t>(hash) * 0x9E3779B97F4A7C15ULL) >> m_Shift) & m_Mask; }

            void Grow()
            {
                std::vector<Slot> old;
                old.swap(m_Slots);
                Reset(old.size() * 2);
                for (const Slot& s : old)
                {
                    if (s.index == kEmpty)
                        continue;
                    size_t slot = SlotOf(s.hash);
                    while (m_Slots[slot].index != kEmpty)
                        slot = (slot + 1) & m_Mask;
                    m_Slots[slot] = s;
                }
            }

            std::vector<Slot> m_Slots;
            size_t m_Mask = 0;
            unsigned int m_Shift = 0;
            size_t m_Count = 0;
        };

        void ComputeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
        {
            // Initialize tangent accumulators
            for (auto &v : vertices) {
                v.tangent = glm::vec3(0.0f);
            }

            // For each triangle:
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                Vertex &v0 = vertices[indices[i + 0]];
                Vertex &v1 = vertices[indices[i + 1]];
                Vertex &v2 = vertices[indices[i + 2]];

                glm::vec3 edge1 = v1.position - v0.position;
                glm::vec3 edge2 = v2.position - v0.position;
                glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
                glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

                float denom = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
                float f = (denom == 0.0f) ? 0.0f : 1.0f / denom;

                glm::vec3 tangent = glm::vec3(0.0f);
                tangent.x = f * ( deltaUV2.y * edge1.x - deltaUV1.y * edge2.x );
                tangent.y = f * ( deltaUV2.y * edge1.y - deltaUV1.y * edge2.y );
                tangent.z = f * ( deltaUV2.y * edge1.z - deltaUV1.y * edge2.z );

                // Accumulate to each vertex
                v0.tangent += tangent;
                v1.tangent += tangent;
                v2.tangent += tangent;
            }

            // Finally normalize and orthogonalize
            for (auto &v : vertices) {
                // Gram-Schmidt orthogonalization
                glm::vec3 n = v.normal;
                glm::vec3 t = v.tangent;

                // Remove component in normal direction:
                t = glm::normalize(t - n * glm::dot(n, t));

                v.tangent = t;
            }
        }
    }

    namespace {

        // One run of a slot's triangles, after SplitIntoChunks has made it spatially compact.
        struct ChunkRange {
            size_t firstIndex = 0;
            size_t indexCount = 0;
            size_t vertexCount = 0; // distinct vertices the chunk's triangles reference
            glm::vec3 boundsMin = glm::vec3(0);
            glm::vec3 boundsMax = glm::vec3(0);
        };

        // Splits one material slot's triangles into chunks of at most `maxTriangles`, by recursively cutting at
        // the median triangle centroid along the longest axis (a kd-tree) - so every chunk covers a compact region
        // and can be frustum-culled on its own. When the slot is larger than one chunk, `verts` and `indices`
        // are rewritten so each chunk's indices are contiguous and the vertices are in first-use order (a
        // chunk then reads a compact range of the vertex buffer instead of strided-over-the-whole-slot).
        // Vertices are shared between chunks, never duplicated.
        std::vector<ChunkRange> SplitIntoChunks(std::vector<Vertex>& verts, std::vector<unsigned int>& indices, size_t maxTriangles)
        {
            const size_t triangleCount = indices.size() / 3;
            std::vector<ChunkRange> chunks;

            if (triangleCount <= maxTriangles) {
                ChunkRange only;
                only.firstIndex = 0;
                only.indexCount = indices.size();
                only.vertexCount = verts.size();
                if (triangleCount > 0) {
                    only.boundsMin = glm::vec3(std::numeric_limits<float>::max());
                    only.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
                    for (unsigned int index : indices) {
                        only.boundsMin = glm::min(only.boundsMin, verts[index].position);
                        only.boundsMax = glm::max(only.boundsMax, verts[index].position);
                    }
                }
                chunks.push_back(only);
                return chunks;
            }

            std::vector<glm::vec3> centroids(triangleCount);
            for (size_t t = 0; t < triangleCount; t++)
                centroids[t] = (verts[indices[t * 3]].position + verts[indices[t * 3 + 1]].position + verts[indices[t * 3 + 2]].position) / 3.0f;

            std::vector<uint32_t> order(triangleCount);
            for (size_t t = 0; t < triangleCount; t++)
                order[t] = static_cast<uint32_t>(t);

            // Depth-first with the left half popped first, so the leaves come out in a spatially coherent order.
            std::vector<std::pair<size_t, size_t>> leaves;
            std::vector<std::pair<size_t, size_t>> stack = { { 0, triangleCount } };

            while (!stack.empty()) {
                auto [begin, end] = stack.back();
                stack.pop_back();

                if (end - begin <= maxTriangles) {
                    leaves.emplace_back(begin, end);
                    continue;
                }

                glm::vec3 cMin(std::numeric_limits<float>::max());
                glm::vec3 cMax(std::numeric_limits<float>::lowest());
                for (size_t i = begin; i < end; i++) {
                    cMin = glm::min(cMin, centroids[order[i]]);
                    cMax = glm::max(cMax, centroids[order[i]]);
                }

                glm::vec3 extent = cMax - cMin;
                int axis = (extent.y > extent.x) ? 1 : 0;
                if (extent.z > extent[axis])
                    axis = 2;

                size_t mid = begin + (end - begin) / 2;
                std::nth_element(order.begin() + begin, order.begin() + mid, order.begin() + end,
                    [&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });

                stack.emplace_back(mid, end);
                stack.emplace_back(begin, mid);
            }

            std::vector<Vertex> newVerts;
            newVerts.reserve(verts.size());
            std::vector<unsigned int> newIndices;
            newIndices.reserve(indices.size());

            constexpr unsigned int unmapped = std::numeric_limits<unsigned int>::max();
            std::vector<unsigned int> remap(verts.size(), unmapped);
            std::vector<unsigned int> lastChunkSeen(verts.size(), unmapped);

            for (size_t chunkIndex = 0; chunkIndex < leaves.size(); chunkIndex++) {
                auto [begin, end] = leaves[chunkIndex];

                // Source order inside a chunk keeps whatever locality the file already had
                std::sort(order.begin() + begin, order.begin() + end);

                ChunkRange chunk;
                chunk.firstIndex = newIndices.size();
                chunk.indexCount = (end - begin) * 3;
                chunk.boundsMin = glm::vec3(std::numeric_limits<float>::max());
                chunk.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());

                for (size_t i = begin; i < end; i++) {
                    for (size_t k = 0; k < 3; k++) {
                        unsigned int oldIndex = indices[order[i] * 3 + k];

                        if (remap[oldIndex] == unmapped) {
                            remap[oldIndex] = static_cast<unsigned int>(newVerts.size());
                            newVerts.push_back(verts[oldIndex]);
                        }
                        if (lastChunkSeen[oldIndex] != chunkIndex) {
                            lastChunkSeen[oldIndex] = static_cast<unsigned int>(chunkIndex);
                            chunk.vertexCount++;
                        }

                        newIndices.push_back(remap[oldIndex]);
                        chunk.boundsMin = glm::min(chunk.boundsMin, verts[oldIndex].position);
                        chunk.boundsMax = glm::max(chunk.boundsMax, verts[oldIndex].position);
                    }
                }

                chunks.push_back(chunk);
            }

            verts = std::move(newVerts);
            indices = std::move(newIndices);
            return chunks;
        }
    }

    namespace {

        // The triangles of one material slot while the mesh is being built
        struct GroupedTriangles {
            std::vector<Vertex> verts;
            std::vector<unsigned int> localIndices;
            std::unique_ptr<VertexDeduplicator> dedup;
        };

        // Whether a mesh gets its lightmap texture coordinates when it is built (they cost a vertex attribute and the vertices
        // cut along the charts' seams). A mesh built without them cannot take a baked lightmap. Applies to meshes loaded afterwards.
        Core::CVar<bool> cvLightmapUVs("mesh.lightmap_uvs", true,
            "Give every mesh a second set of texture coordinates for baked lighting (lightmaps) when it is loaded. Meshes loaded without them cannot be lightmapped. Applies to meshes loaded afterwards",
            Core::CVarFlags::Archive);

        // Lays the whole mesh (every material slot) out in one lightmap square, see UnwrapForLightmap() : the vertices on the
        // seams between two charts are split, and every vertex gets its lightmapUV
        void AssignLightmapUVs(std::vector<GroupedTriangles>& groups)
        {
            std::vector<std::vector<glm::vec3>> positions(groups.size()), normals(groups.size());
            std::vector<LightmapUnwrapGroup> input(groups.size());

            for (size_t g = 0; g < groups.size(); g++)
            {
                const std::vector<Vertex>& verts = groups[g].verts;
                positions[g].reserve(verts.size());
                normals[g].reserve(verts.size());
                for (const Vertex& v : verts)
                {
                    positions[g].push_back(v.position);
                    normals[g].push_back(v.normal);
                }

                input[g].positions = positions[g].data();
                input[g].normals = normals[g].data();
                input[g].vertexCount = verts.size();
                input[g].indices = groups[g].localIndices.data();
                input[g].indexCount = groups[g].localIndices.size();
            }

            const LightmapUnwrapResult unwrapped = UnwrapForLightmap(input);
            if (!unwrapped.success)
                return;

            for (size_t g = 0; g < groups.size(); g++)
            {
                const LightmapUnwrapGroupResult& cut = unwrapped.groups[g];
                if (cut.indices.empty())
                    continue;

                std::vector<Vertex> cutVerts;
                cutVerts.reserve(cut.sourceVertex.size());
                for (size_t i = 0; i < cut.sourceVertex.size(); i++)
                {
                    Vertex v = groups[g].verts[cut.sourceVertex[i]];
                    v.lightmapUV = cut.uv[i];
                    cutVerts.push_back(v);
                }

                groups[g].verts = std::move(cutVerts);
                groups[g].localIndices.assign(cut.indices.begin(), cut.indices.end());
            }
        }
    }

    MeshCPUData BuildMeshCPUDataFromFBX(const ufbx_mesh *ufbx_mesh, double scene_unit_meters,
        ufbx_material_list &ufbx_mats, ufbx_node *mesh_node, COL_RGBA vertexColor)
    {
        MeshCPUData result;

        double scene_scale = scene_unit_meters;

        // One group per FBX material slot, kept in the mesh's own material-index
        // order. World files (and DCC tools) address materials by that index, so
        // submesh N must map to material slot N. Faces are not necessarily sorted
        // by material in the file, so grouping by encounter order would permute
        // the mapping - an unused slot instead yields an empty submesh that keeps
        // the alignment intact.
        uint32_t materialSlotCount = (ufbx_mesh->materials.count > 0)
            ? (uint32_t)ufbx_mesh->materials.count
            : 1u;

        std::vector<GroupedTriangles> materialGroups(materialSlotCount);

        // How many vertices each slot can have at most (one per face corner of its faces) : its table never has to grow
        {
            std::vector<size_t> corners(materialSlotCount, 0);
            for (size_t i = 0; i < ufbx_mesh->num_faces; i++) {
                uint32_t mat_index = (ufbx_mesh->face_material.data && i < ufbx_mesh->face_material.count) ? ufbx_mesh->face_material.data[i] : 0;
                if (mat_index >= materialSlotCount)
                    mat_index = 0;
                const ufbx_face face = ufbx_mesh->faces.data[i];
                corners[mat_index] += face.num_indices >= 3 ? (face.num_indices - 2) * 3 : 0;
            }
            for (uint32_t slot = 0; slot < materialSlotCount; slot++) {
                materialGroups[slot].dedup = std::make_unique<VertexDeduplicator>(corners[slot]);
                materialGroups[slot].verts.reserve(corners[slot]);
                materialGroups[slot].localIndices.reserve(corners[slot]);
            }
        }

        // The same for every vertex of the mesh
        const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&mesh_node->geometry_to_world);

        for (size_t i = 0; i < ufbx_mesh->num_faces; i++) {
            ufbx_face face = ufbx_mesh->faces.data[i];

            uint32_t mat_index = 0;
            if (ufbx_mesh->face_material.data && i < ufbx_mesh->face_material.count) {
                mat_index = ufbx_mesh->face_material.data[i];
            }
            if (mat_index >= materialSlotCount) {
                mat_index = 0;
            }

            GroupedTriangles& group = materialGroups[mat_index];
            size_t start = face.index_begin;
            size_t count = face.num_indices;

            // Triangle fan triangulation
            for (size_t j = 1; j + 1 < count; j++) {
                size_t triIndices[3] = {
                    start + 0,
                    start + j,
                    start + j + 1
                };

                for (int k = 0; k < 3; k++) {
                    size_t vertex_index = triIndices[k];
                    Vertex v{};

                    // Position
                    ufbx_vec3 pos = ufbx_get_vertex_vec3(&ufbx_mesh->vertex_position, vertex_index);
                    ufbx_vec3 wpos = ufbx_transform_position(&mesh_node->geometry_to_world, pos);
                    v.position = { wpos.x * scene_scale, wpos.y * scene_scale, wpos.z * scene_scale };

                    result.boundsMin = glm::min(result.boundsMin, v.position);
                    result.boundsMax = glm::max(result.boundsMax, v.position);

                    // Normal
                    if (ufbx_mesh->vertex_normal.exists) {
                        ufbx_vec3 normal = ufbx_get_vertex_vec3(&ufbx_mesh->vertex_normal, vertex_index);
                        ufbx_vec3 wn = ufbx_transform_direction(&normalMatrix, normal);
                        v.normal = glm::normalize(glm::vec3{ wn.x, wn.y, wn.z });
                    }

                    // UV
                    if (ufbx_mesh->vertex_uv.exists) {
                        ufbx_vec2 uv = ufbx_get_vertex_vec2(&ufbx_mesh->vertex_uv, vertex_index);
                        v.texCoord = { uv.x, uv.y };
                    }

                    // Color
                    v.color = vertexColor;

                    // Deduplication
                    group.localIndices.push_back(group.dedup->FindOrAdd(v, group.verts));
                }
            }
        }

        (void)ufbx_mats;

        for (GroupedTriangles& group : materialGroups)
            group.dedup.reset();

        // The tangents are worked out on the mesh as it is, before the lightmap layout cuts its vertices : the two halves of a
        // vertex split along a lightmap seam are the same point of the surface and must shade with the same tangent
        for (GroupedTriangles& group : materialGroups)
            ComputeTangents(group.verts, group.localIndices);

        if (cvLightmapUVs.Get())
            AssignLightmapUVs(materialGroups);

        uint32_t totalVertexCount = 0;

        for (uint32_t slot = 0; slot < materialSlotCount; slot++) {
            auto& group = materialGroups[slot];

            // One submesh per spatial chunk of this slot, all pointing at the same material slot. An unused slot
            // still yields one (empty) submesh so the slot numbering stays aligned.
            std::vector<ChunkRange> chunks = SplitIntoChunks(group.verts, group.localIndices, kMaxSubMeshTriangles);

            size_t vertexOffset = totalVertexCount;
            totalVertexCount += group.verts.size();

            size_t byteOffset = result.vertices.size();
            size_t byteSize   = group.verts.size() * sizeof(Vertex);

            result.vertices.resize(byteOffset + byteSize);

            if (byteSize > 0) {
                memcpy(
                    result.vertices.data() + byteOffset,
                    group.verts.data(),
                    byteSize
                );
            }

            size_t slotIndexOffset = result.indices.size();

            for (auto idx : group.localIndices)
                result.indices.push_back(static_cast<uint32_t>(vertexOffset + idx));

            for (const ChunkRange& chunk : chunks) {
                result.submeshes.push_back(SubMesh{
                    .indexOffset = slotIndexOffset + chunk.firstIndex,
                    .indexCount = chunk.indexCount,
                    .vertexCount = chunk.vertexCount,
                    .materialIndex = slot,
                    .boundsMin = chunk.boundsMin,
                    .boundsMax = chunk.boundsMax
                });
            }
        }

        result.layout = {
            {{"aPos",     ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
            {"aTexCoord",ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
            {"aNormal",  ShaderDataType::Vec3, 2, offsetof(Vertex, normal)},
            {"aColor",   ShaderDataType::Vec4, 3, offsetof(Vertex, color)},
            {"aTangent", ShaderDataType::Vec3, 4, offsetof(Vertex, tangent)},
            {"aLightmapUV", ShaderDataType::Vec2, 5, offsetof(Vertex, lightmapUV)}}, sizeof(Vertex)
        };

        result.success = true;

        return result;
    }

    MeshCPUData DecodeMeshFile(const Filesystem::Path &path)
    {
        MeshCPUData result;

        ufbx_load_opts opts = { 0 };
        ufbx_error error;
        const std::string filePath = path.GetNativePath();
        ufbx_scene *scene = ufbx_load_file(filePath.c_str(), &opts, &error);
        if (!scene) {
            DEBUG_ERROR(
                "Failed to load " + path.full + " : " +
                (error.description.data ? error.description.data : "Unknown error"));
            return result;
        }

        if (scene->meshes.count > 1) {
            ufbx_free_scene(scene);
            DEBUG_ERROR("Multiple meshes per fbx file isn't supported yet.");
            return result;
        }

        ufbx_mesh* ufbx_mesh = scene->meshes.data[0];

        ufbx_node* mesh_node = nullptr;

        for (size_t i = 0; i < scene->nodes.count; i++) {
            ufbx_node* node = scene->nodes.data[i];
            if (node->mesh == ufbx_mesh) {
                mesh_node = node;
                break;
            }
        }

        result = BuildMeshCPUDataFromFBX(ufbx_mesh, scene->settings.unit_meters, scene->materials, mesh_node);

        ufbx_free_scene(scene);

        return result;
    }

    namespace {
        Core::CVar<bool> cvKeepMeshData("mesh.keep_cpu_data", false,
            "Keep a CPU copy of every mesh after it is uploaded (doubles the memory of the meshes ; the ray traced GI otherwise reads them back from the GPU when it needs them). Applies to meshes loaded afterwards",
            Core::CVarFlags::Archive);
    }

    void Mesh::FinishCPUData()
    {
        m_IndexCount = m_Indices.size();

        std::lock_guard<std::mutex> lock(m_CPUMutex);
        if (cvKeepMeshData.Get())
        {
            m_KeptCPU = std::make_shared<MeshCPUCopy>();
            m_KeptCPU->vertices = std::move(m_Vertices);
            m_KeptCPU->indices = std::move(m_Indices);
        }
        std::vector<uint8_t>().swap(m_Vertices);
        std::vector<uint32_t>().swap(m_Indices);
        m_ReadBackCPU.reset();
    }

    std::shared_ptr<const MeshCPUCopy> Mesh::AcquireCPUData()
    {
        {
            std::lock_guard<std::mutex> lock(m_CPUMutex);
            if (m_KeptCPU)
                return m_KeptCPU;
            if (std::shared_ptr<MeshCPUCopy> alive = m_ReadBackCPU.lock())
                return alive;
        }

        // Read back without the lock held : the readback waits for the graphics thread, which may be asking for this very mesh
        auto copy = std::make_shared<MeshCPUCopy>();
        if (!ReadBackCPUData(*copy))
            return nullptr;

        std::lock_guard<std::mutex> lock(m_CPUMutex);
        if (std::shared_ptr<MeshCPUCopy> alive = m_ReadBackCPU.lock())
            return alive;       // another thread was faster : share its copy
        m_ReadBackCPU = copy;
        return copy;
    }

    std::shared_ptr<Mesh> Mesh::Create()
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLMesh>();

            case RendererAPI::API::Null:
                return std::make_shared<Null::NullMesh>();

            /*case RendererAPI::API::Vulkan:
                return std::make_shared<VKMesh>();

            case RendererAPI::API::DX11:
                return std::make_shared<DX11Mesh>();

            case RendererAPI::API::DX12:
                return std::make_shared<DX12Mesh>();*/

            default:
                return nullptr;
        }
    }

    std::vector<DrawCommand> Mesh::CreateDrawCommands(const glm::mat4& worldMatrix, int objectID, int modelID, std::vector<std::shared_ptr<Material>> mats,
        uint64_t lightmapHandle, const glm::vec4& lightmapScaleOffset)
    {
        if(mats.empty()){
            DEBUG_ERROR("Cannot create draw commands: no materials supplied");
            return {};
        }

        // Compared against material slots, not submeshes : one slot may be split into several spatial submeshes
        if(mats.size() != GetMaterialSlotCount() && GetMaterialSlotCount() != 1){
            DEBUG_WARNING("Mesh material slot count and supplied material count differ; clamping material slots");
        }

        std::vector<DrawCommand> cmds;

        for (size_t i = 0; i < m_Submeshes.size(); i++)
        {
            DrawCommand cmd;

            cmd.indexOffset = m_Submeshes[i].indexOffset;
            cmd.indexCount  = m_Submeshes[i].indexCount;
            cmd.vertexCount = m_Submeshes[i].vertexCount;

            // Submeshes are emitted in FBX material-slot order; pick the material
            // for this submesh's slot, clamping if the caller supplied fewer.
            uint32_t slot = m_Submeshes[i].materialIndex;
            if (slot >= mats.size())
                slot = (uint32_t)mats.size() - 1;

            cmd.mesh        = shared_from_this();
            cmd.material    = mats[slot];
            cmd.modelMatrix = worldMatrix;
            cmd.objectID    = objectID;
            cmd.modelID     = modelID;
            cmd.lightmapHandle = lightmapHandle;
            cmd.lightmapScaleOffset = lightmapScaleOffset;

            // Per-submesh bounds, so frustum culling can drop the parts of a big mesh that are out of view
            cmd.boundsMax = m_Submeshes[i].boundsMax;
            cmd.boundsMin = m_Submeshes[i].boundsMin;

            cmds.push_back(cmd);
        }

        return cmds;
    }
}