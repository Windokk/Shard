#include "mesh.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh_cooked.hpp"

#include "engine/renderer/rhi/resources/mesh/lightmap_uv.hpp"
#include "engine/renderer/rhi/backends/opengl/mesh/gl_mesh.hpp"
#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/backends/vulkan/vk_mesh.hpp"
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
        glm::vec2 lightmapUV;
        // Not part of what the GPU gets (see kGpuVertexSize) : the skin follows the vertex while the loader dedups, cuts and splits.
        glm::u16vec4 joints = glm::u16vec4(0);
        glm::vec4 weights = glm::vec4(0.0f);

        bool operator==(const Vertex& other) const {
            const float eps = 0.0001f;
            return glm::all(glm::epsilonEqual(position, other.position, eps)) &&
                glm::all(glm::epsilonEqual(normal, other.normal, eps)) &&
                glm::all(glm::epsilonEqual(color, other.color, eps)) &&
                glm::all(glm::epsilonEqual(texCoord, other.texCoord, eps))&&
                glm::all(glm::epsilonEqual(tangent, other.tangent, eps)) &&
                glm::all(glm::epsilonEqual(lightmapUV, other.lightmapUV, eps)) &&
                joints == other.joints &&
                glm::all(glm::epsilonEqual(weights, other.weights, eps));
        }
    };

    // The bytes of a vertex the GPU reads : everything before the skin
    constexpr size_t kGpuVertexSize = offsetof(Vertex, joints);

    inline int Quantize(float v) {
        return static_cast<int>(v * 10000.0f);
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

        class VertexDeduplicator {
        public:
            explicit VertexDeduplicator(size_t expected) { Reset(NextPowerOfTwo(std::max<size_t>(expected, 16) * 2)); }

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

            // Fibonacci hashing: uses the top bits of the product, whatever the quality of the low hash bits.
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
            for (auto &v : vertices) {
                v.tangent = glm::vec3(0.0f);
            }

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

                v0.tangent += tangent;
                v1.tangent += tangent;
                v2.tangent += tangent;
            }

            for (auto &v : vertices) {
                glm::vec3 n = v.normal;
                glm::vec3 t = v.tangent;

                t = t - n * glm::dot(n, t);

                // Zero tangent (no UVs, degenerate fan) normalizes to NaN, which reaches the shader TBN and makes every normal NaN.
                if (glm::dot(t, t) < 1e-12f) {
                    const glm::vec3 axis = glm::abs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                    t = glm::cross(n, axis);
                }

                v.tangent = glm::normalize(t);
            }
        }
    }

    namespace {

        struct ChunkRange {
            size_t firstIndex = 0;
            size_t indexCount = 0;
            size_t vertexCount = 0;
            glm::vec3 boundsMin = glm::vec3(0);
            glm::vec3 boundsMax = glm::vec3(0);
        };

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

        struct GroupedTriangles {
            std::vector<Vertex> verts;
            std::vector<unsigned int> localIndices;
            std::unique_ptr<VertexDeduplicator> dedup;
        };

        Core::CVar<bool> cvLightmapUVs("mesh.lightmap_uvs", true,
            "Give every mesh a second set of texture coordinates for baked lighting (lightmaps) when it is loaded. Meshes loaded without them cannot be lightmapped. Applies to meshes loaded afterwards",
            Core::CVarFlags::Archive);

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

    namespace {

        struct FbxInstance {
            const ufbx_mesh* mesh = nullptr;
            const ufbx_node* node = nullptr;
        };

        glm::mat4 ToMat4(const ufbx_matrix& m, double translationScale)
        {
            glm::mat4 r(1.0f);
            for (int c = 0; c < 3; ++c)
                r[c] = glm::vec4(float(m.cols[c].x), float(m.cols[c].y), float(m.cols[c].z), 0.0f);
            r[3] = glm::vec4(float(m.cols[3].x * translationScale), float(m.cols[3].y * translationScale), float(m.cols[3].z * translationScale), 1.0f);
            return r;
        }

        // What the skin of a vertex of one mesh comes to : the joints and weights, and the matrices that bring the vertex (and its normal)
        // to the bind pose of the scene.
        struct VertexSkin {
            bool done = false;
            glm::u16vec4 joints = glm::u16vec4(0);
            glm::vec4 weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            ufbx_matrix position{};
            ufbx_matrix normal{};
        };

        // The meshes of a file as one mesh. `scene` is given to bring the skeleton and the skin weights along (a skinned mesh is drawn in its
        // bind pose) ; null : the meshes are placed where the scene's current pose has them.
        MeshCPUData BuildFromInstances(const std::vector<FbxInstance>& instances, double scene_scale, const ufbx_scene* scene, COL_RGBA vertexColor)
        {
            MeshCPUData result;

            // One group per FBX material slot, in slot order. World files address materials by index, so submesh N must map to slot N, even when empty.
            // The slots of the meshes follow one another.
            std::vector<uint32_t> slotBase(instances.size(), 0);
            uint32_t materialSlotCount = 0;
            for (size_t i = 0; i < instances.size(); i++)
            {
                slotBase[i] = materialSlotCount;
                materialSlotCount += (instances[i].mesh->materials.count > 0) ? (uint32_t)instances[i].mesh->materials.count : 1u;
            }
            if (materialSlotCount == 0)
                materialSlotCount = 1;

            bool skinned = false;
            if (scene)
                for (const FbxInstance& instance : instances)
                    if (instance.mesh->skin_deformers.count > 0)
                        skinned = true;

            std::shared_ptr<MeshSkin> skin;
            std::vector<int32_t> jointOfNode;
            if (skinned)
            {
                skin = std::make_shared<MeshSkin>();
                jointOfNode.assign(scene->nodes.count, -1);
                skin->nodes.resize(scene->nodes.count);
                for (size_t n = 0; n < scene->nodes.count; n++)
                {
                    const ufbx_node* node = scene->nodes.data[n];
                    MeshSkinNode& out = skin->nodes[n];
                    out.name.assign(node->name.data ? node->name.data : "", node->name.length);
                    out.parent = node->parent ? int32_t(node->parent->typed_id) : -1;
                    const ufbx_transform& t = node->local_transform;
                    out.translation = glm::vec3(float(t.translation.x * scene_scale), float(t.translation.y * scene_scale), float(t.translation.z * scene_scale));
                    out.rotation = glm::quat(float(t.rotation.w), float(t.rotation.x), float(t.rotation.y), float(t.rotation.z));
                    out.scale = glm::vec3(float(t.scale.x), float(t.scale.y), float(t.scale.z));
                }
            }

            // The joint of a node, made on first use with the bind pose it is given
            auto jointFor = [&](const ufbx_node* node, const ufbx_matrix& bindToWorld) -> int
            {
                int32_t& joint = jointOfNode[node->typed_id];
                if (joint < 0)
                {
                    if (skin->jointNodes.size() >= 65535)
                        return 0;
                    joint = int32_t(skin->jointNodes.size());
                    skin->jointNodes.push_back(int32_t(node->typed_id));
                    skin->inverseBind.push_back(glm::inverse(ToMat4(bindToWorld, scene_scale)));
                }
                return joint;
            };

            std::vector<GroupedTriangles> materialGroups(materialSlotCount);

            {
                std::vector<size_t> corners(materialSlotCount, 0);
                for (size_t m = 0; m < instances.size(); m++)
                {
                    const ufbx_mesh* fbxMesh = instances[m].mesh;
                    const uint32_t slots = (fbxMesh->materials.count > 0) ? (uint32_t)fbxMesh->materials.count : 1u;
                    for (size_t i = 0; i < fbxMesh->num_faces; i++) {
                        uint32_t mat_index = (fbxMesh->face_material.data && i < fbxMesh->face_material.count) ? fbxMesh->face_material.data[i] : 0;
                        if (mat_index >= slots)
                            mat_index = 0;
                        const ufbx_face face = fbxMesh->faces.data[i];
                        corners[slotBase[m] + mat_index] += face.num_indices >= 3 ? (face.num_indices - 2) * 3 : 0;
                    }
                }
                for (uint32_t slot = 0; slot < materialSlotCount; slot++) {
                    materialGroups[slot].dedup = std::make_unique<VertexDeduplicator>(corners[slot]);
                    materialGroups[slot].verts.reserve(corners[slot]);
                    materialGroups[slot].localIndices.reserve(corners[slot]);
                }
            }

            for (size_t m = 0; m < instances.size(); m++)
            {
                const ufbx_mesh* fbxMesh = instances[m].mesh;
                const ufbx_node* node = instances[m].node;
                const uint32_t slots = (fbxMesh->materials.count > 0) ? (uint32_t)fbxMesh->materials.count : 1u;
                const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&node->geometry_to_world);

                const ufbx_skin_deformer* deformer = (skinned && fbxMesh->skin_deformers.count > 0) ? fbxMesh->skin_deformers.data[0] : nullptr;
                std::vector<ufbx_matrix> clusterBind;
                std::vector<int> clusterJoint;
                if (deformer)
                {
                    clusterBind.resize(deformer->clusters.count);
                    clusterJoint.assign(deformer->clusters.count, -1);
                    for (size_t c = 0; c < deformer->clusters.count; c++)
                    {
                        const ufbx_skin_cluster* cluster = deformer->clusters.data[c];
                        if (!cluster->bone_node)
                            continue;
                        clusterBind[c] = ufbx_matrix_mul(&cluster->bind_to_world, &cluster->geometry_to_bone);
                        clusterJoint[c] = jointFor(cluster->bone_node, cluster->bind_to_world);
                    }
                }

                // A mesh without weights follows its own node as a single bone ; the same for a vertex the skin does not reach
                int rigidJoint = -1;
                std::vector<VertexSkin> vertexSkin(skinned ? fbxMesh->num_vertices : 0);
                auto skinOf = [&](size_t vertex) -> const VertexSkin&
                {
                    VertexSkin& s = vertexSkin[vertex];
                    if (s.done)
                        return s;
                    s.done = true;

                    if (deformer && vertex < deformer->vertices.count)
                    {
                        const ufbx_skin_vertex sv = deformer->vertices.data[vertex];
                        float total = 0.0f;
                        int count = 0;
                        for (uint32_t k = 0; k < sv.num_weights && count < 4; k++)
                        {
                            const ufbx_skin_weight sw = deformer->weights.data[sv.weight_begin + k];
                            if (sw.cluster_index >= clusterJoint.size() || clusterJoint[sw.cluster_index] < 0 || sw.weight <= 0.0)
                                continue;
                            s.joints[count] = uint16_t(clusterJoint[sw.cluster_index]);
                            s.weights[count] = float(sw.weight);
                            total += float(sw.weight);
                            count++;
                        }
                        if (count > 0 && total > 1e-6f)
                        {
                            ufbx_matrix blend{};
                            for (int k = 0; k < 4; k++)
                            {
                                s.weights[k] = k < count ? s.weights[k] / total : 0.0f;
                                if (k >= count)
                                    s.joints[k] = 0;
                            }
                            // Accumulate the weighted bind matrices of the influences (the cluster of each influence, found again from its joint)
                            for (uint32_t k = 0, taken = 0; k < sv.num_weights && taken < uint32_t(count); k++)
                            {
                                const ufbx_skin_weight sw = deformer->weights.data[sv.weight_begin + k];
                                if (sw.cluster_index >= clusterJoint.size() || clusterJoint[sw.cluster_index] < 0 || sw.weight <= 0.0)
                                    continue;
                                const float w = s.weights[taken++];
                                const ufbx_matrix& cm = clusterBind[sw.cluster_index];
                                for (int c = 0; c < 4; c++)
                                {
                                    blend.cols[c].x += w * cm.cols[c].x;
                                    blend.cols[c].y += w * cm.cols[c].y;
                                    blend.cols[c].z += w * cm.cols[c].z;
                                }
                            }
                            s.position = blend;
                            s.normal = ufbx_matrix_for_normals(&blend);
                            return s;
                        }
                    }

                    if (rigidJoint < 0)
                        rigidJoint = jointFor(node, node->node_to_world);
                    s.joints = glm::u16vec4(uint16_t(rigidJoint), 0, 0, 0);
                    s.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                    s.position = node->geometry_to_world;
                    s.normal = normalMatrix;
                    return s;
                };

                for (size_t i = 0; i < fbxMesh->num_faces; i++) {
                    ufbx_face face = fbxMesh->faces.data[i];

                    uint32_t mat_index = 0;
                    if (fbxMesh->face_material.data && i < fbxMesh->face_material.count) {
                        mat_index = fbxMesh->face_material.data[i];
                    }
                    if (mat_index >= slots) {
                        mat_index = 0;
                    }

                    GroupedTriangles& group = materialGroups[slotBase[m] + mat_index];
                    size_t start = face.index_begin;
                    size_t count = face.num_indices;

                    for (size_t j = 1; j + 1 < count; j++) {
                        size_t triIndices[3] = {
                            start + 0,
                            start + j,
                            start + j + 1
                        };

                        for (int k = 0; k < 3; k++) {
                            size_t vertex_index = triIndices[k];
                            Vertex v{};

                            const ufbx_matrix* placeMatrix = &node->geometry_to_world;
                            const ufbx_matrix* normalPlace = &normalMatrix;
                            if (skinned)
                            {
                                const VertexSkin& s = skinOf(fbxMesh->vertex_indices.data[vertex_index]);
                                placeMatrix = &s.position;
                                normalPlace = &s.normal;
                                v.joints = s.joints;
                                v.weights = s.weights;
                            }

                            ufbx_vec3 pos = ufbx_get_vertex_vec3(&fbxMesh->vertex_position, vertex_index);
                            ufbx_vec3 wpos = ufbx_transform_position(placeMatrix, pos);
                            v.position = { wpos.x * scene_scale, wpos.y * scene_scale, wpos.z * scene_scale };

                            result.boundsMin = glm::min(result.boundsMin, v.position);
                            result.boundsMax = glm::max(result.boundsMax, v.position);

                            if (fbxMesh->vertex_normal.exists) {
                                ufbx_vec3 normal = ufbx_get_vertex_vec3(&fbxMesh->vertex_normal, vertex_index);
                                ufbx_vec3 wn = ufbx_transform_direction(normalPlace, normal);
                                v.normal = glm::normalize(glm::vec3{ wn.x, wn.y, wn.z });
                            }

                            if (fbxMesh->vertex_uv.exists) {
                                ufbx_vec2 uv = ufbx_get_vertex_vec2(&fbxMesh->vertex_uv, vertex_index);
                                v.texCoord = { uv.x, uv.y };
                            }

                            v.color = vertexColor;

                            group.localIndices.push_back(group.dedup->FindOrAdd(v, group.verts));
                        }
                    }
                }
            }

            for (GroupedTriangles& group : materialGroups)
                group.dedup.reset();

            // Tangents are computed before the lightmap cut. Both halves of a vertex split on a seam must shade with the same tangent.
            for (GroupedTriangles& group : materialGroups)
                ComputeTangents(group.verts, group.localIndices);

            if (cvLightmapUVs.Get())
                AssignLightmapUVs(materialGroups);

            uint32_t totalVertexCount = 0;

            for (uint32_t slot = 0; slot < materialSlotCount; slot++) {
                auto& group = materialGroups[slot];

                std::vector<ChunkRange> chunks = SplitIntoChunks(group.verts, group.localIndices, kMaxSubMeshTriangles);

                size_t vertexOffset = totalVertexCount;
                totalVertexCount += group.verts.size();

                size_t byteOffset = result.vertices.size();
                size_t byteSize   = group.verts.size() * kGpuVertexSize;

                result.vertices.resize(byteOffset + byteSize);

                // The GPU gets the vertex without its skin, which goes to the mesh's skin in the same order
                for (size_t i = 0; i < group.verts.size(); i++)
                    memcpy(result.vertices.data() + byteOffset + i * kGpuVertexSize, &group.verts[i], kGpuVertexSize);

                if (skin)
                    for (const Vertex& v : group.verts)
                    {
                        skin->joints.push_back(v.joints);
                        skin->weights.push_back(v.weights);
                    }

                size_t slotIndexOffset = result.indices.size();

                for (auto idx : group.localIndices)
                    result.indices.push_back(static_cast<uint32_t>(vertexOffset + idx));

                for (const ChunkRange& chunk : chunks) {
                    // Field by field: designated initializers are C++20, MSVC refuses them in C++17.
                    SubMesh submesh;
                    submesh.indexOffset = slotIndexOffset + chunk.firstIndex;
                    submesh.indexCount = chunk.indexCount;
                    submesh.vertexCount = chunk.vertexCount;
                    submesh.materialIndex = slot;
                    submesh.boundsMin = chunk.boundsMin;
                    submesh.boundsMax = chunk.boundsMax;
                    result.submeshes.push_back(submesh);
                }
            }

            result.layout = StandardMeshLayout();

            result.skin = std::move(skin);
            result.success = true;

            return result;
        }
    }

    VertexLayout StandardMeshLayout()
    {
        // What the GPU reads : the vertex without its skin, which stays on the CPU (MeshSkin)
        return {
            {{"aPos",     ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
            {"aTexCoord",ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
            {"aNormal",  ShaderDataType::Vec3, 2, offsetof(Vertex, normal)},
            {"aColor",   ShaderDataType::Vec4, 3, offsetof(Vertex, color)},
            {"aTangent", ShaderDataType::Vec3, 4, offsetof(Vertex, tangent)},
            {"aLightmapUV", ShaderDataType::Vec2, 5, offsetof(Vertex, lightmapUV)}}, kGpuVertexSize
        };
    }

    MeshCPUData BuildMeshCPUDataFromFBX(const ufbx_mesh *ufbx_mesh, double scene_unit_meters,
        ufbx_material_list &ufbx_mats, ufbx_node *mesh_node, COL_RGBA vertexColor)
    {
        (void)ufbx_mats;
        return BuildFromInstances({ FbxInstance{ ufbx_mesh, mesh_node } }, scene_unit_meters, nullptr, vertexColor);
    }

    MeshCPUData BuildMeshCPUDataFromScene(const ufbx_scene *scene, COL_RGBA vertexColor)
    {
        std::vector<FbxInstance> instances;
        for (size_t i = 0; i < scene->nodes.count; i++)
        {
            const ufbx_node* node = scene->nodes.data[i];
            if (node->mesh && node->mesh->num_faces > 0)
                instances.push_back(FbxInstance{ node->mesh, node });
        }
        if (instances.empty())
            return MeshCPUData{};
        return BuildFromInstances(instances, scene->settings.unit_meters, scene, vertexColor);
    }

    MeshCPUData DecodeMeshFile(const Filesystem::Path &path)
    {
        MeshCPUData result;

        if (LoadCookedMesh(path.GetNativePath(), result))
            return result;
        result = MeshCPUData();

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

        result = BuildMeshCPUDataFromScene(scene);
        if (!result.success)
            DEBUG_ERROR("No mesh in " + path.full);

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

        // Read back without the lock: the readback waits for the graphics thread, which may be asking for this mesh.
        auto copy = std::make_shared<MeshCPUCopy>();
        if (!ReadBackCPUData(*copy))
            return nullptr;

        std::lock_guard<std::mutex> lock(m_CPUMutex);
        if (std::shared_ptr<MeshCPUCopy> alive = m_ReadBackCPU.lock())
            return alive;
        m_ReadBackCPU = copy;
        return copy;
    }

    std::shared_ptr<Mesh> Mesh::Create()
    {
        switch(RendererAPI::Current()->GetAPI())
        {
            case RendererAPI::API::OpenGL:
                return std::make_shared<GLMesh>();

            case RendererAPI::API::Vulkan:
                return std::make_shared<VKMesh>();

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

    MeshCPUData AddGpuSkin(MeshCPUData data, const std::vector<glm::u16vec4>& joints, const std::vector<glm::vec4>& weights)
    {
        const uint32_t stride = data.layout.GetStride();
        if (!data.success || stride == 0 || data.vertices.size() % stride != 0) return data;
        const size_t count = data.vertices.size() / stride;
        if (count == 0 || joints.size() != count || weights.size() != count) return data;
        for (const VertexElement& element : data.layout.GetElements())
            if (element.location == kSkinJointsLocation || element.location == kSkinWeightsLocation) return data;

        const uint32_t jointsOffset = stride, weightsOffset = stride + uint32_t(sizeof(glm::vec4));
        const uint32_t skinnedStride = stride + 2 * uint32_t(sizeof(glm::vec4));
        std::vector<uint8_t> vertices(count * skinnedStride);
        for (size_t i = 0; i < count; ++i)
        {
            uint8_t* out = vertices.data() + i * skinnedStride;
            std::memcpy(out, data.vertices.data() + i * stride, stride);
            const glm::vec4 j(float(joints[i].x), float(joints[i].y), float(joints[i].z), float(joints[i].w));
            std::memcpy(out + jointsOffset, &j, sizeof(j));
            std::memcpy(out + weightsOffset, &weights[i], sizeof(glm::vec4));
        }

        std::vector<VertexElement> elements = data.layout.GetElements();
        elements.push_back({ "aJoints", ShaderDataType::Vec4, kSkinJointsLocation, jointsOffset });
        elements.push_back({ "aWeights", ShaderDataType::Vec4, kSkinWeightsLocation, weightsOffset });
        data.layout = VertexLayout(std::move(elements), skinnedStride);
        data.vertices = std::move(vertices);
        return data;
    }

    std::vector<DrawCommand> Mesh::CreateDrawCommands(const glm::mat4& worldMatrix, int objectID, int modelID, std::vector<std::shared_ptr<Material>> mats,
        uint64_t lightmapHandle, const glm::vec4& lightmapScaleOffset, const std::shared_ptr<SkinPalette>& skin)
    {
        if(mats.empty()){
            DEBUG_ERROR("Cannot create draw commands: no materials supplied");
            return {};
        }

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

            uint32_t slot = m_Submeshes[i].materialIndex;
            if (slot >= mats.size())
                slot = (uint32_t)mats.size() - 1;

            if (!mats[slot])
                continue;

            cmd.mesh        = shared_from_this();
            cmd.material    = mats[slot];
            cmd.modelMatrix = worldMatrix;
            cmd.objectID    = objectID;
            cmd.modelID     = modelID;
            cmd.lightmapHandle = lightmapHandle;
            cmd.lightmapScaleOffset = lightmapScaleOffset;

            cmd.boundsMax = m_Submeshes[i].boundsMax;
            cmd.boundsMin = m_Submeshes[i].boundsMin;

            if (skin)
            {
                // The pose moves the vertices out of the bind pose : cull with the whole mesh, grown
                const glm::vec3 pad = glm::max(0.5f * (m_BoundsMax - m_BoundsMin), glm::vec3(0.25f));
                cmd.boundsMin = m_BoundsMin - pad;
                cmd.boundsMax = m_BoundsMax + pad;
                cmd.skin = skin;
            }

            cmds.push_back(cmd);
        }

        return cmds;
    }
}