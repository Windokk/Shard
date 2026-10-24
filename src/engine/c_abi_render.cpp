#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_precision.hpp>

#include <stb/stb_image.h>

#include <shard/shard_render.h>

#include "engine/assets/resources_manager.hpp"
#include "engine/c_abi_render.hpp"
#include "engine/c_abi_render_internal.hpp"
#include "engine/core/math/large_world.hpp"
#include "engine/plugins/abi/c_plugin.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

// shard.render : the resources a plugin makes (meshes, textures, materials), the things it keeps on screen that use them (render objects, instance
// batches, sprite batches), and the camera. The plugin records ; the GPU side is made on the main thread, in the Extract stage (the snapshot provider
// below), and the renderer draws from the snapshot like it does for the components. The 2D overlay is in c_abi_overlay.cpp.

namespace Shard::Engine::Plugins::CAbi {

    using namespace Rendering;

    namespace {
        constexpr uint16_t kObjectWorld = 0xFFFE;      // the "world index" in the keys of the proxies of render objects
        constexpr uint16_t kInstanceWorld = 0xFFFD;
        constexpr uint16_t kSpriteWorld = 0xFFFC;
        constexpr size_t kMaxInstancesPerBatch = 4096;
        constexpr int kUploadsPerFrame = 16;

        // The engine's vertex (mesh.cpp, lit.vert) : the plugin's mesh is converted to it
        struct EngineVertex {
            glm::vec3 position;
            glm::vec2 texCoord;
            glm::vec3 normal;
            glm::vec4 color;
            glm::vec3 tangent;
            glm::vec2 lightmapUV;
        };

        VertexLayout EngineLayout()
        {
            return VertexLayout{
                {{"aPos", ShaderDataType::Vec3, 0, uint32_t(offsetof(EngineVertex, position))},
                 {"aTexCoord", ShaderDataType::Vec2, 1, uint32_t(offsetof(EngineVertex, texCoord))},
                 {"aNormal", ShaderDataType::Vec3, 2, uint32_t(offsetof(EngineVertex, normal))},
                 {"aColor", ShaderDataType::Vec4, 3, uint32_t(offsetof(EngineVertex, color))},
                 {"aTangent", ShaderDataType::Vec3, 4, uint32_t(offsetof(EngineVertex, tangent))},
                 {"aLightmapUV", ShaderDataType::Vec2, 5, uint32_t(offsetof(EngineVertex, lightmapUV))}},
                uint32_t(sizeof(EngineVertex)) };
        }

        // ---- Records ----

        struct MeshRec {
            PluginOwner owner = 0;
            std::mutex mutex;
            uint32_t vertexFlags = 0;
            std::vector<EngineVertex> vertices;
            std::vector<uint32_t> indices;
            std::vector<ShardSubmesh> submeshes;
            std::vector<glm::u16vec4> joints;       // skinned meshes only
            std::vector<glm::vec4> weights;
            bool skinned = false;
            bool boundsGiven = false;
            glm::vec3 boundsMin = glm::vec3(0), boundsMax = glm::vec3(0);
            std::shared_ptr<Mesh> gpu;
            std::shared_ptr<Mesh> asset;            // mesh_from_asset : the engine's own, nothing to upload
            uint64_t version = 1;
            uint64_t uploaded = 0;
            size_t slots = 1;
        };

        struct MaterialRec {
            PluginOwner owner = 0;
            std::mutex mutex;
            std::shared_ptr<Material> material;
            std::map<std::string, float> floats;
            std::map<std::string, glm::vec4> vectors;
            std::map<std::string, ShardTexture> textures;
            uint64_t version = 1;
            uint64_t applied = 0;
        };

        struct ObjectRec {
            PluginOwner owner = 0;
            std::mutex mutex;
            ShardMesh mesh = 0;
            std::vector<ShardMaterial> materials;
            uint32_t flags = 0;
            ShardActor pick = 0;
            ShardRenderTransform transform{};
            std::vector<glm::mat4> pose;
            uint64_t poseVersion = 0;
            std::shared_ptr<Mesh> skinned;          // the mesh deformed by `pose`, made on the CPU
            uint64_t skinnedPose = 0;
            uint64_t skinnedFrom = 0;
            int componentId = 0;
        };

        struct InstanceBatchRec {
            PluginOwner owner = 0;
            std::mutex mutex;
            ShardMesh mesh = 0;
            std::vector<ShardMaterial> materials;
            uint32_t flags = 0;
            double origin[3] = {};
            std::vector<ShardInstance> instances;
            uint32_t maxInstances = 0;
            int componentBase = 0;
        };

        struct SpriteBatchRec {
            PluginOwner owner = 0;
            std::mutex mutex;
            ShardSpriteBatchDesc desc{};
            std::vector<ShardSprite> sprites;
        };

        OwnedTable<MeshRec> gMeshes;
        OwnedTable<MaterialRec> gMaterials;
        OwnedTable<ObjectRec> gObjects;
        OwnedTable<InstanceBatchRec> gInstanceBatches;
        OwnedTable<SpriteBatchRec> gSpriteBatches;
        OwnedTable<TextureRec> gTextures;

        std::mutex gHookMutex;
        std::map<PluginOwner, bool> gHooked;
        std::atomic<int> gNextComponentId{ 0x40000000 };
        int gUploads = 0;                           // meshes and textures made this frame (the extract thread)

        void HookRelease(PluginContext& ctx, PluginOwner owner)
        {
            {
                std::lock_guard<std::mutex> lock(gHookMutex);
                if (gHooked[owner]) return;
                gHooked[owner] = true;
            }
            ctx.OnRelease("render", [owner] {
                gObjects.RemoveOwner(owner);
                gInstanceBatches.RemoveOwner(owner);
                gSpriteBatches.RemoveOwner(owner);
                ReleaseOverlays(owner);
                gMaterials.RemoveOwner(owner);
                gMeshes.RemoveOwner(owner);
                gTextures.RemoveOwner(owner);
                std::lock_guard<std::mutex> lock(gHookMutex);
                gHooked.erase(owner);
            });
        }

        PluginContext* Prepare(ShardPluginCtx handle, ShardResult& result)
        {
            PluginContext* ctx = FindContext(handle);
            if (!ctx) { result = SHARD_E_STALE_HANDLE; return nullptr; }
            HookRelease(*ctx, PluginOwner(handle));
            result = SHARD_OK;
            return ctx;
        }

        Core::IEngineContext* Engine() { return Core::gSharedEnginePtr; }

        bool FiniteV(const float* v, int n) { for (int i = 0; i < n; ++i) if (!std::isfinite(v[i])) return false; return true; }

        // ---- Meshes ----

        uint32_t VertexStride(uint32_t flags)
        {
            return ((flags & SHARD_VERTEX_POSITION) ? 12u : 0u) + ((flags & SHARD_VERTEX_NORMAL) ? 12u : 0u) + ((flags & SHARD_VERTEX_TANGENT) ? 16u : 0u) +
                   ((flags & SHARD_VERTEX_UV0) ? 8u : 0u) + ((flags & SHARD_VERTEX_UV1) ? 8u : 0u) + ((flags & SHARD_VERTEX_COLOR) ? 4u : 0u) +
                   ((flags & SHARD_VERTEX_JOINTS) ? 8u : 0u) + ((flags & SHARD_VERTEX_WEIGHTS) ? 16u : 0u);
        }

        void ComputeNormals(std::vector<EngineVertex>& vertices, const std::vector<uint32_t>& indices)
        {
            for (EngineVertex& v : vertices) v.normal = glm::vec3(0.0f);
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                EngineVertex& a = vertices[indices[i]];
                EngineVertex& b = vertices[indices[i + 1]];
                EngineVertex& c = vertices[indices[i + 2]];
                const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);         // area weighted
                a.normal += n; b.normal += n; c.normal += n;
            }
            for (EngineVertex& v : vertices) {
                const float l = glm::length(v.normal);
                v.normal = l > 1e-12f ? v.normal / l : glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }

        void ComputeTangents(std::vector<EngineVertex>& vertices, const std::vector<uint32_t>& indices)
        {
            for (EngineVertex& v : vertices) v.tangent = glm::vec3(0.0f);
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                EngineVertex& v0 = vertices[indices[i]];
                EngineVertex& v1 = vertices[indices[i + 1]];
                EngineVertex& v2 = vertices[indices[i + 2]];
                const glm::vec3 e1 = v1.position - v0.position, e2 = v2.position - v0.position;
                const glm::vec2 d1 = v1.texCoord - v0.texCoord, d2 = v2.texCoord - v0.texCoord;
                const float denom = d1.x * d2.y - d2.x * d1.y;
                const float f = denom == 0.0f ? 0.0f : 1.0f / denom;
                const glm::vec3 t = f * (d2.y * e1 - d1.y * e2);
                v0.tangent += t; v1.tangent += t; v2.tangent += t;
            }
            for (EngineVertex& v : vertices) {
                glm::vec3 t = v.tangent - v.normal * glm::dot(v.normal, v.tangent);
                if (glm::dot(t, t) < 1e-12f) {
                    // No UVs, or a degenerate fan : any direction perpendicular to the normal will do
                    const glm::vec3 axis = std::abs(v.normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                    t = glm::cross(v.normal, axis);
                }
                v.tangent = glm::normalize(t);
            }
        }

        // The vertices of a stream, laid out as the flags say, into the engine's vertices (and the skin streams). `first` is the vertex it starts at.
        void ReadVertices(MeshRec& m, uint32_t flags, const uint8_t* src, uint32_t first, uint32_t count, bool& hadNormal, bool& hadTangent)
        {
            const uint32_t stride = VertexStride(flags);
            hadNormal = (flags & SHARD_VERTEX_NORMAL) != 0;
            hadTangent = (flags & SHARD_VERTEX_TANGENT) != 0;
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t* p = src + size_t(i) * stride;
                EngineVertex& v = m.vertices[first + i];
                std::memcpy(&v.position, p, 12); p += 12;
                if (flags & SHARD_VERTEX_NORMAL) { std::memcpy(&v.normal, p, 12); p += 12; }
                if (flags & SHARD_VERTEX_TANGENT) { float t[4]; std::memcpy(t, p, 16); v.tangent = glm::vec3(t[0], t[1], t[2]); p += 16; }
                if (flags & SHARD_VERTEX_UV0) { std::memcpy(&v.texCoord, p, 8); p += 8; }
                if (flags & SHARD_VERTEX_UV1) { std::memcpy(&v.lightmapUV, p, 8); p += 8; }
                if (flags & SHARD_VERTEX_COLOR) { v.color = glm::vec4(float(p[0]), float(p[1]), float(p[2]), float(p[3])) / 255.0f; p += 4; }
                if (flags & SHARD_VERTEX_JOINTS) { uint16_t j[4]; std::memcpy(j, p, 8); m.joints[first + i] = glm::u16vec4(j[0], j[1], j[2], j[3]); p += 8; }
                if (flags & SHARD_VERTEX_WEIGHTS) { std::memcpy(&m.weights[first + i], p, 16); p += 16; }
            }
        }

        bool FillMesh(MeshRec& m, const ShardMeshDesc& d, std::string& why)
        {
            if (d.structSize < offsetof(ShardMeshDesc, name)) { why = "its ShardMeshDesc is too small"; return false; }
            if (!(d.vertexFlags & SHARD_VERTEX_POSITION)) { why = "a mesh needs POSITION"; return false; }
            const bool hasJoints = (d.vertexFlags & SHARD_VERTEX_JOINTS) != 0, hasWeights = (d.vertexFlags & SHARD_VERTEX_WEIGHTS) != 0;
            if (hasJoints != hasWeights) { why = "a skinned mesh needs both JOINTS and WEIGHTS"; return false; }
            if (d.vertexCount == 0 || d.indexCount == 0 || d.indexCount % 3 != 0 || !d.vertices || !d.indices) { why = "a mesh needs vertices and a multiple of 3 indices"; return false; }
            if (d.vertexCount > (1u << 24) || d.indexCount > (1u << 26)) { why = "a mesh is limited to 16M vertices and 64M indices"; return false; }

            std::vector<uint32_t> indices(d.indexCount);
            const bool index16 = (d.flags & SHARD_MESH_INDEX16) != 0;
            for (uint32_t i = 0; i < d.indexCount; ++i) {
                const uint32_t index = index16 ? static_cast<const uint16_t*>(d.indices)[i] : static_cast<const uint32_t*>(d.indices)[i];
                if (index >= d.vertexCount) { why = "an index is past the last vertex"; return false; }
                indices[i] = index;
            }

            std::vector<ShardSubmesh> parts;
            if (d.submeshCount == 0) {
                parts.push_back(ShardSubmesh{ 0, d.indexCount, 0 });
            } else {
                if (!d.submeshes) { why = "submeshCount without submeshes"; return false; }
                for (uint32_t i = 0; i < d.submeshCount; ++i) {
                    const ShardSubmesh& s = d.submeshes[i];
                    if (s.indexCount == 0 || s.indexOffset % 3 != 0 || s.indexCount % 3 != 0 || uint64_t(s.indexOffset) + s.indexCount > d.indexCount) { why = "a submesh is out of the indices"; return false; }
                    if (s.materialSlot > 255) { why = "a material slot above 255"; return false; }
                    parts.push_back(s);
                }
            }

            m.vertices.assign(d.vertexCount, EngineVertex{ glm::vec3(0), glm::vec2(0), glm::vec3(0, 1, 0), glm::vec4(1), glm::vec3(1, 0, 0), glm::vec2(0) });
            m.joints.clear();
            m.weights.clear();
            if (hasJoints) { m.joints.assign(d.vertexCount, glm::u16vec4(0)); m.weights.assign(d.vertexCount, glm::vec4(1, 0, 0, 0)); }
            bool hadNormal = false, hadTangent = false;
            ReadVertices(m, d.vertexFlags, static_cast<const uint8_t*>(d.vertices), 0, d.vertexCount, hadNormal, hadTangent);
            for (const EngineVertex& v : m.vertices)
                if (!FiniteV(&v.position.x, 3)) { why = "a vertex position is not finite"; return false; }

            m.indices = std::move(indices);
            if (!hadNormal) ComputeNormals(m.vertices, m.indices);
            if (!hadTangent) ComputeTangents(m.vertices, m.indices);

            m.submeshes = std::move(parts);
            m.slots = 1;
            for (const ShardSubmesh& s : m.submeshes) m.slots = std::max<size_t>(m.slots, size_t(s.materialSlot) + 1);
            m.vertexFlags = d.vertexFlags;
            m.skinned = hasJoints;
            m.boundsGiven = FiniteV(d.boundsMin, 3) && FiniteV(d.boundsMax, 3) && (d.boundsMin[0] != d.boundsMax[0] || d.boundsMin[1] != d.boundsMax[1] || d.boundsMin[2] != d.boundsMax[2]);
            if (m.boundsGiven) { m.boundsMin = glm::vec3(d.boundsMin[0], d.boundsMin[1], d.boundsMin[2]); m.boundsMax = glm::vec3(d.boundsMax[0], d.boundsMax[1], d.boundsMax[2]); }
            ++m.version;
            return true;
        }

        // The mesh as the renderer wants it (MeshCPUData -> Mesh::CreateFromData)
        std::shared_ptr<Mesh> UploadMesh(const std::vector<EngineVertex>& vertices, const std::vector<uint32_t>& indices, const std::vector<ShardSubmesh>& parts,
                                         bool boundsGiven, const glm::vec3& givenMin, const glm::vec3& givenMax)
        {
            MeshCPUData data;
            data.vertices.resize(vertices.size() * sizeof(EngineVertex));
            std::memcpy(data.vertices.data(), vertices.data(), data.vertices.size());
            data.indices = indices;
            data.layout = EngineLayout();
            data.boundsMin = glm::vec3(std::numeric_limits<float>::max());
            data.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
            for (const EngineVertex& v : vertices) { data.boundsMin = glm::min(data.boundsMin, v.position); data.boundsMax = glm::max(data.boundsMax, v.position); }
            if (boundsGiven) { data.boundsMin = givenMin; data.boundsMax = givenMax; }
            for (const ShardSubmesh& s : parts) {
                SubMesh sub;
                sub.indexOffset = s.indexOffset;
                sub.indexCount = s.indexCount;
                sub.vertexCount = vertices.size();
                sub.materialIndex = s.materialSlot;
                glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
                for (uint32_t i = s.indexOffset; i < s.indexOffset + s.indexCount; ++i) {
                    lo = glm::min(lo, vertices[indices[i]].position);
                    hi = glm::max(hi, vertices[indices[i]].position);
                }
                sub.boundsMin = lo;
                sub.boundsMax = hi;
                data.submeshes.push_back(sub);
            }
            data.success = true;
            std::shared_ptr<Mesh> mesh = Mesh::Create();
            mesh->CreateFromData(data);
            return mesh;
        }

        // The GPU mesh of a record, made (or remade after an update) now. Null while the frame's budget of uploads is spent and there is no mesh yet.
        std::shared_ptr<Mesh> GpuMesh(MeshRec& m)
        {
            std::lock_guard<std::mutex> lock(m.mutex);
            if (m.asset) return m.asset;
            if (m.gpu && m.uploaded == m.version) return m.gpu;
            if (gUploads >= kUploadsPerFrame) return m.gpu;          // keeps showing the old one, the new one comes next frame
            ++gUploads;
            m.gpu = UploadMesh(m.vertices, m.indices, m.submeshes, m.boundsGiven, m.boundsMin, m.boundsMax);
            m.uploaded = m.version;
            return m.gpu;
        }

        // A skinned mesh deformed on the CPU by a pose (the host has no GPU skinning yet)
        std::shared_ptr<Mesh> SkinnedMesh(ObjectRec& o, MeshRec& m)
        {
            if (o.skinned && o.skinnedPose == o.poseVersion && o.skinnedFrom == m.version) return o.skinned;
            if (gUploads >= kUploadsPerFrame) return o.skinned;
            ++gUploads;
            std::vector<EngineVertex> deformed;
            std::vector<uint32_t> indices;
            std::vector<ShardSubmesh> parts;
            {
                std::lock_guard<std::mutex> lock(m.mutex);
                deformed = m.vertices;
                indices = m.indices;
                parts = m.submeshes;
                const size_t bones = o.pose.size();
                for (size_t i = 0; i < deformed.size(); ++i) {
                    glm::mat4 skin(0.0f);
                    for (int k = 0; k < 4; ++k) {
                        const float w = m.weights[i][k];
                        if (w <= 0.0f) continue;
                        skin += w * o.pose[std::min<size_t>(m.joints[i][k], bones - 1)];
                    }
                    EngineVertex& v = deformed[i];
                    const glm::mat3 linear(skin);
                    v.position = glm::vec3(skin * glm::vec4(v.position, 1.0f));
                    const glm::vec3 n = linear * v.normal, t = linear * v.tangent;
                    v.normal = glm::dot(n, n) > 1e-12f ? glm::normalize(n) : v.normal;
                    v.tangent = glm::dot(t, t) > 1e-12f ? glm::normalize(t) : v.tangent;
                }
                o.skinnedFrom = m.version;
            }
            o.skinned = UploadMesh(deformed, indices, parts, false, glm::vec3(0), glm::vec3(0));
            o.skinnedPose = o.poseVersion;
            return o.skinned;
        }

        // ---- API: meshes ----

        ShardResult SHARD_CALL MeshCreate(ShardPluginCtx handle, const ShardMeshDesc* desc, ShardMesh* out)
        {
            try {
                if (!out || !desc) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                PluginContext* ctx = Prepare(handle, prepared);
                if (!ctx) return prepared;
                auto rec = std::make_shared<MeshRec>();
                rec->owner = PluginOwner(handle);
                std::string why;
                if (!FillMesh(*rec, *desc, why)) { ctx->Error("mesh : " + why); return SHARD_E_INVALID_ARG; }
                *out = gMeshes.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MeshUpdate(ShardPluginCtx handle, ShardMesh mesh, const ShardMeshDesc* desc)
        {
            try {
                if (!desc) return SHARD_E_INVALID_ARG;
                PluginContext* ctx = FindContext(handle);
                if (!ctx) return SHARD_E_STALE_HANDLE;
                std::shared_ptr<MeshRec> rec = gMeshes.Get(PluginOwner(handle), mesh);
                if (!rec || rec->asset) return SHARD_E_STALE_HANDLE;
                // Built apart, swapped in whole : a failed update leaves the mesh as it was
                MeshRec next;
                std::string why;
                if (!FillMesh(next, *desc, why)) { ctx->Error("mesh update : " + why); return SHARD_E_INVALID_ARG; }
                std::lock_guard<std::mutex> lock(rec->mutex);
                rec->vertexFlags = next.vertexFlags;
                rec->vertices = std::move(next.vertices);
                rec->indices = std::move(next.indices);
                rec->submeshes = std::move(next.submeshes);
                rec->joints = std::move(next.joints);
                rec->weights = std::move(next.weights);
                rec->skinned = next.skinned;
                rec->boundsGiven = next.boundsGiven;
                rec->boundsMin = next.boundsMin;
                rec->boundsMax = next.boundsMax;
                rec->slots = next.slots;
                ++rec->version;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MeshUpdateRange(ShardPluginCtx handle, ShardMesh mesh, const ShardMeshRange* range)
        {
            try {
                if (!range || range->structSize < offsetof(ShardMeshRange, vertices) + sizeof(void*) || !range->vertices) return SHARD_E_INVALID_ARG;
                std::shared_ptr<MeshRec> rec = gMeshes.Get(PluginOwner(handle), mesh);
                if (!rec || rec->asset) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(rec->mutex);
                if (uint64_t(range->firstVertex) + range->vertexCount > rec->vertices.size()) return SHARD_E_INVALID_ARG;
                bool hadNormal = false, hadTangent = false;
                const uint32_t flags = rec->vertexFlags;
                ReadVertices(*rec, flags, static_cast<const uint8_t*>(range->vertices), range->firstVertex, range->vertexCount, hadNormal, hadTangent);
                if (!hadNormal) ComputeNormals(rec->vertices, rec->indices);
                if (!hadTangent) ComputeTangents(rec->vertices, rec->indices);
                ++rec->version;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MeshFromAsset(ShardPluginCtx handle, int32_t assetId, ShardMesh* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                if (!Prepare(handle, prepared)) return prepared;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = Engine();
                if (!engine || !engine->GetAssetIDManager() || !engine->GetResourcesManager()) return SHARD_E_UNSUPPORTED;
                auto infos = engine->GetAssetIDManager()->GetAssetFromID(Filesystem::AssetID(assetId));
                if (!infos || infos->baseInfos.type != Filesystem::Type::T_MODEL) return SHARD_E_NOT_FOUND;
                auto mesh = engine->GetResourcesManager()->Get<Mesh>(Core::Resources::AssetKind::Mesh, infos->baseInfos.nameInProject);
                if (!mesh) return SHARD_E_NOT_FOUND;
                auto rec = std::make_shared<MeshRec>();
                rec->owner = PluginOwner(handle);
                rec->asset = mesh;
                rec->slots = mesh->GetMaterialSlotCount();
                *out = gMeshes.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MeshDestroy(ShardPluginCtx handle, ShardMesh mesh)
        {
            return gMeshes.Remove(PluginOwner(handle), mesh) ? SHARD_OK : SHARD_E_STALE_HANDLE;
        }

        // ---- API: textures ----

        uint32_t PixelBytes(uint32_t format)
        {
            switch (format) {
                case SHARD_TEXTURE_R8: return 1;
                case SHARD_TEXTURE_RG8: case SHARD_TEXTURE_R16F: return 2;
                case SHARD_TEXTURE_RGBA8: case SHARD_TEXTURE_R32F: return 4;
                case SHARD_TEXTURE_RGBA16F: return 8;
                default: return 0;
            }
        }

        TextureSpecifications SpecsOf(const TextureRec& r)
        {
            TextureSpecifications spec;
            spec.width = r.width;
            spec.height = r.height;
            switch (r.format) {
                case SHARD_TEXTURE_R8: spec.format = TextureFormat::RED; spec.internalFormat = TextureInternalFormat::RED; break;
                case SHARD_TEXTURE_RG8: spec.format = TextureFormat::RG; spec.internalFormat = TextureInternalFormat::RG; break;
                case SHARD_TEXTURE_RGBA8: spec.format = TextureFormat::RGBA; spec.internalFormat = TextureInternalFormat::RGBA8; break;
                case SHARD_TEXTURE_R16F: spec.format = TextureFormat::RED; spec.internalFormat = TextureInternalFormat::R16F; spec.halfFloatPixelData = true; break;
                case SHARD_TEXTURE_RGBA16F: spec.format = TextureFormat::RGBA; spec.internalFormat = TextureInternalFormat::RGBA16F; spec.halfFloatPixelData = true; break;
                case SHARD_TEXTURE_R32F: spec.format = TextureFormat::RED; spec.internalFormat = TextureInternalFormat::R32F; break;
            }
            spec.generateMips = (r.flags & SHARD_TEXTURE_MIPS) != 0;
            spec.srgb = (r.flags & SHARD_TEXTURE_SRGB) != 0;
            const bool nearest = (r.flags & SHARD_TEXTURE_NEAREST) != 0;
            spec.magFilter = nearest ? TextureFilter::Nearest : TextureFilter::Linear;
            spec.minFilter = spec.generateMips ? (nearest ? TextureFilter::NearestMipmapNearest : TextureFilter::LinearMipmapLinear) : (nearest ? TextureFilter::Nearest : TextureFilter::Linear);
            const TextureWrap wrap = (r.flags & SHARD_TEXTURE_CLAMP) ? TextureWrap::ClampEdge : TextureWrap::Repeat;
            spec.wrapS = spec.wrapT = spec.wrapR = wrap;
            return spec;
        }

        // Reads the image file a texture_load asked for (the rows from the top, as the ABI has them)
        void DecodeFile(TextureRec& r)
        {
            r.decoded = true;
            const Filesystem::Path file(r.path, true);
            const std::string bytes = file.ReadFile();
            if (bytes.empty()) { r.failed = true; return; }
            stbi_set_flip_vertically_on_load_thread(0);
            int w = 0, h = 0, channels = 0;
            unsigned char* data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), int(bytes.size()), &w, &h, &channels, 4);
            if (!data || w <= 0 || h <= 0) { if (data) stbi_image_free(data); r.failed = true; return; }
            r.width = uint32_t(w);
            r.height = uint32_t(h);
            r.format = SHARD_TEXTURE_RGBA8;
            r.pixels.assign(data, data + size_t(w) * h * 4);
            stbi_image_free(data);
        }

        // The GPU side of a texture, made if it is not yet ; the updates that were waiting are applied. Main thread.
        void EnsureTexture(TextureRec& r)
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            if (r.failed) return;
            if (!r.decoded) DecodeFile(r);
            if (r.failed) return;
            if (!r.texture && !r.framebuffer) {
                if (gUploads >= kUploadsPerFrame * 4) return;
                ++gUploads;
                if (r.flags & SHARD_TEXTURE_RENDER_TARGET) {
                    FramebufferSpecifications specs;
                    specs.width = r.width;
                    specs.height = r.height;
                    specs.hasColor = true;
                    specs.hasDepth = false;
                    specs.colorSpecs.internalFormat = TextureInternalFormat::RGBA8;
                    specs.colorSpecs.generateMips = false;
                    specs.colorSpecs.wrapS = specs.colorSpecs.wrapT = TextureWrap::ClampEdge;
                    r.framebuffer = Framebuffer::Create(specs);
                } else {
                    TextureSpecifications spec = SpecsOf(r);
                    if (r.pixels.empty()) r.pixels.assign(size_t(r.width) * r.height * PixelBytes(r.format), 0);
                    r.texture = Texture2D::Create(spec, r.pixels.data());
                    r.pixels.clear();
                    r.pixels.shrink_to_fit();
                }
            }
            if (r.texture) {
                for (const TextureRec::Region& region : r.regions)
                    r.texture->UpdateRegion(region.x, region.y, region.w, region.h, region.bytes.data(), 0);
                r.regions.clear();
            }
        }

        ShardResult SHARD_CALL TextureCreate(ShardPluginCtx handle, const ShardTextureDesc* desc, ShardTexture* out)
        {
            try {
                if (!out || !desc) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                PluginContext* ctx = Prepare(handle, prepared);
                if (!ctx) return prepared;
                if (desc->structSize < offsetof(ShardTextureDesc, name)) return SHARD_E_INVALID_ARG;
                const uint32_t pixel = PixelBytes(desc->format);
                if (!pixel || desc->width == 0 || desc->height == 0 || desc->width > 16384 || desc->height > 16384) return SHARD_E_INVALID_ARG;
                if (desc->layers > 1) { ctx->Error("texture : this host has no texture arrays yet"); return SHARD_E_UNSUPPORTED; }
                if ((desc->flags & SHARD_TEXTURE_RENDER_TARGET) && desc->format != SHARD_TEXTURE_RGBA8) return SHARD_E_INVALID_ARG;
                auto rec = std::make_shared<TextureRec>();
                rec->owner = PluginOwner(handle);
                rec->width = desc->width;
                rec->height = desc->height;
                rec->format = desc->format;
                rec->flags = desc->flags;
                if (desc->pixels) rec->pixels.assign(static_cast<const uint8_t*>(desc->pixels), static_cast<const uint8_t*>(desc->pixels) + size_t(desc->width) * desc->height * pixel);
                *out = gTextures.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TextureUpdate(ShardPluginCtx handle, ShardTexture texture, const ShardTextureRegion* region)
        {
            try {
                if (!region || region->structSize < offsetof(ShardTextureRegion, pixels) + sizeof(void*) || !region->pixels) return SHARD_E_INVALID_ARG;
                std::shared_ptr<TextureRec> r = gTextures.Get(PluginOwner(handle), texture);
                if (!r) return SHARD_E_STALE_HANDLE;
                if (r->flags & SHARD_TEXTURE_RENDER_TARGET) return SHARD_E_INVALID_ARG;
                if (region->layer != 0 || region->width == 0 || region->height == 0 || uint64_t(region->x) + region->width > r->width || uint64_t(region->y) + region->height > r->height) return SHARD_E_INVALID_ARG;
                const uint32_t pixel = PixelBytes(r->format);
                const uint32_t pitch = region->rowPitch ? region->rowPitch : region->width * pixel;
                if (pitch < region->width * pixel) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(r->mutex);
                if (!r->texture && !r->decoded) return SHARD_E_INVALID_ARG;          // a file not read yet
                TextureRec::Region copy;
                copy.x = region->x; copy.y = region->y; copy.w = region->width; copy.h = region->height;
                copy.bytes.resize(size_t(region->width) * region->height * pixel);
                for (uint32_t row = 0; row < region->height; ++row)
                    std::memcpy(copy.bytes.data() + size_t(row) * region->width * pixel, static_cast<const uint8_t*>(region->pixels) + size_t(row) * pitch, size_t(region->width) * pixel);
                if (!r->texture) {
                    // Not on the GPU yet : into the pixels it will be made from
                    if (r->pixels.empty()) r->pixels.assign(size_t(r->width) * r->height * pixel, 0);
                    for (uint32_t row = 0; row < region->height; ++row)
                        std::memcpy(r->pixels.data() + (size_t(region->y + row) * r->width + region->x) * pixel, copy.bytes.data() + size_t(row) * region->width * pixel, size_t(region->width) * pixel);
                } else {
                    r->regions.push_back(std::move(copy));
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TextureLoad(ShardPluginCtx handle, ShardStr path, uint32_t flags, ShardTexture* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                PluginContext* ctx = Prepare(handle, prepared);
                if (!ctx) return prepared;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                std::string file;
                if (!ResolveProjectFile(*ctx, ToString(path), file)) return SHARD_E_NOT_FOUND;
                auto rec = std::make_shared<TextureRec>();
                rec->owner = PluginOwner(handle);
                rec->path = file;
                rec->decoded = false;
                rec->flags = flags & ~SHARD_TEXTURE_RENDER_TARGET;
                *out = gTextures.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TextureFromAsset(ShardPluginCtx handle, int32_t assetId, ShardTexture* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                if (!Prepare(handle, prepared)) return prepared;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = Engine();
                if (!engine || !engine->GetAssetIDManager() || !engine->GetResourcesManager()) return SHARD_E_UNSUPPORTED;
                auto infos = engine->GetAssetIDManager()->GetAssetFromID(Filesystem::AssetID(assetId));
                if (!infos || infos->baseInfos.type != Filesystem::Type::T_IMAGE) return SHARD_E_NOT_FOUND;
                auto texture = engine->GetResourcesManager()->Get<Texture2D>(Core::Resources::AssetKind::Texture, infos->baseInfos.nameInProject);
                if (!texture) return SHARD_E_NOT_FOUND;
                auto rec = std::make_shared<TextureRec>();
                rec->owner = PluginOwner(handle);
                rec->width = texture->GetWidth();
                rec->height = texture->GetHeight();
                rec->format = SHARD_TEXTURE_RGBA8;
                rec->texture = texture;
                *out = gTextures.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TextureSize(ShardPluginCtx handle, ShardTexture texture, uint32_t* width, uint32_t* height)
        {
            try {
                if (!width || !height) return SHARD_E_INVALID_ARG;
                std::shared_ptr<TextureRec> r = gTextures.Get(PluginOwner(handle), texture);
                if (!r) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(r->mutex);
                *width = r->width;
                *height = r->height;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL TextureDestroy(ShardPluginCtx handle, ShardTexture texture)
        {
            return gTextures.Remove(PluginOwner(handle), texture) ? SHARD_OK : SHARD_E_STALE_HANDLE;
        }

        // ---- API: materials ----

        ShardResult SHARD_CALL MaterialLoad(ShardPluginCtx handle, ShardStr path, ShardMaterial* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                if (!Prepare(handle, prepared)) return prepared;
                if (!OnMainThread()) return SHARD_E_WRONG_THREAD;
                Core::IEngineContext* engine = Engine();
                if (!engine || !engine->GetResourcesManager()) return SHARD_E_UNSUPPORTED;
                std::string name = ToString(path);
                std::replace(name.begin(), name.end(), '\\', '/');
                auto source = engine->GetResourcesManager()->Get<Material>(Core::Resources::AssetKind::Material, name);
                if (!source) return SHARD_E_NOT_FOUND;
                // A private copy : its parameters are the plugin's to set
                auto copy = Material::Create(source->GetShader(), source->GetPipeline(), source->GetRecieveShadows(), source->GetOpacity());
                copy->AssignContent(*source);
                auto rec = std::make_shared<MaterialRec>();
                rec->owner = PluginOwner(handle);
                rec->material = copy;
                *out = gMaterials.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MaterialSetFloat(ShardPluginCtx handle, ShardMaterial material, ShardStr name, float value)
        {
            try {
                std::shared_ptr<MaterialRec> r = gMaterials.Get(PluginOwner(handle), material);
                if (!r) return SHARD_E_STALE_HANDLE;
                if (!std::isfinite(value)) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->floats[ToString(name)] = value;
                ++r->version;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MaterialSetVec4(ShardPluginCtx handle, ShardMaterial material, ShardStr name, const float value[4])
        {
            try {
                if (!value || !FiniteV(value, 4)) return SHARD_E_INVALID_ARG;
                std::shared_ptr<MaterialRec> r = gMaterials.Get(PluginOwner(handle), material);
                if (!r) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->vectors[ToString(name)] = glm::vec4(value[0], value[1], value[2], value[3]);
                ++r->version;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MaterialSetTexture(ShardPluginCtx handle, ShardMaterial material, ShardStr name, ShardTexture texture)
        {
            try {
                std::shared_ptr<MaterialRec> r = gMaterials.Get(PluginOwner(handle), material);
                if (!r) return SHARD_E_STALE_HANDLE;
                if (texture && !gTextures.Get(PluginOwner(handle), texture)) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->textures[ToString(name)] = texture;
                ++r->version;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL MaterialDestroy(ShardPluginCtx handle, ShardMaterial material)
        {
            return gMaterials.Remove(PluginOwner(handle), material) ? SHARD_OK : SHARD_E_STALE_HANDLE;
        }

        // The material with the parameters the plugin set (applied when they changed, or a texture of its became real)
        std::shared_ptr<Material> ResolveMaterial(PluginOwner owner, ShardMaterial handle)
        {
            std::shared_ptr<MaterialRec> r = gMaterials.Get(owner, handle);
            if (!r || !r->material) return nullptr;
            std::lock_guard<std::mutex> lock(r->mutex);
            // A texture the plugin set can come to exist later (a file being read) : the parameters are applied until all of them hold
            bool pending = false;
            if (r->applied != r->version || !r->textures.empty()) {
                for (const auto& [name, value] : r->floats) r->material->SetScalarParameter(name, NumericValue(value));
                for (const auto& [name, value] : r->vectors) r->material->SetScalarParameter(name, NumericValue(value));
                for (const auto& [name, texture] : r->textures) {
                    uint64_t native = 0;
                    if (texture) {
                        if (std::shared_ptr<Framebuffer> fb = ResolveRenderTarget(owner, texture)) native = fb->GetColorAttachment();
                        else if (std::shared_ptr<Texture2D> t = ResolveTexture(owner, texture)) native = t->GetHandle();
                        else pending = true;
                    }
                    r->material->SetTextureParameter(name, native);
                }
                if (!pending) r->applied = r->version;
            }
            return r->material;
        }

        // ---- API: render objects ----

        ShardResult SHARD_CALL ObjectCreate(ShardPluginCtx handle, const ShardRenderObjectDesc* desc, ShardRenderObject* out)
        {
            try {
                if (!out || !desc || desc->structSize < offsetof(ShardRenderObjectDesc, transform)) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                if (!Prepare(handle, prepared)) return prepared;
                if (!gMeshes.Get(PluginOwner(handle), desc->mesh)) return SHARD_E_STALE_HANDLE;
                if (desc->materialCount && !desc->materials) return SHARD_E_INVALID_ARG;
                auto rec = std::make_shared<ObjectRec>();
                rec->owner = PluginOwner(handle);
                rec->mesh = desc->mesh;
                rec->materials.assign(desc->materials, desc->materials + desc->materialCount);
                rec->flags = desc->flags;
                rec->pick = desc->pickActor;
                rec->transform = desc->transform;
                if (!(std::isfinite(rec->transform.position[0]) && std::isfinite(rec->transform.position[1]) && std::isfinite(rec->transform.position[2]) &&
                      FiniteV(rec->transform.rotation, 4) && FiniteV(rec->transform.scale, 3))) return SHARD_E_INVALID_ARG;
                rec->componentId = gNextComponentId++;
                *out = gObjects.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ObjectSetTransform(ShardPluginCtx handle, ShardRenderObject object, const ShardRenderTransform* transform)
        {
            try {
                if (!transform || !std::isfinite(transform->position[0]) || !std::isfinite(transform->position[1]) || !std::isfinite(transform->position[2]) ||
                    !FiniteV(transform->rotation, 4) || !FiniteV(transform->scale, 3)) return SHARD_E_INVALID_ARG;
                std::shared_ptr<ObjectRec> r = gObjects.Get(PluginOwner(handle), object);
                if (!r) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->transform = *transform;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ObjectSetMesh(ShardPluginCtx handle, ShardRenderObject object, ShardMesh mesh)
        {
            try {
                std::shared_ptr<ObjectRec> r = gObjects.Get(PluginOwner(handle), object);
                if (!r || !gMeshes.Get(PluginOwner(handle), mesh)) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->mesh = mesh;
                r->skinned.reset();
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ObjectSetMaterials(ShardPluginCtx handle, ShardRenderObject object, const ShardMaterial* materials, uint32_t count)
        {
            try {
                if (count && !materials) return SHARD_E_INVALID_ARG;
                std::shared_ptr<ObjectRec> r = gObjects.Get(PluginOwner(handle), object);
                if (!r) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->materials.assign(materials, materials + count);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ObjectSetFlags(ShardPluginCtx handle, ShardRenderObject object, uint32_t flags)
        {
            std::shared_ptr<ObjectRec> r = gObjects.Get(PluginOwner(handle), object);
            if (!r) return SHARD_E_STALE_HANDLE;
            std::lock_guard<std::mutex> lock(r->mutex);
            r->flags = flags;
            return SHARD_OK;
        }

        ShardResult SHARD_CALL ObjectSetPose(ShardPluginCtx handle, ShardRenderObject object, const float* matrices, uint32_t count)
        {
            try {
                if (!matrices || count == 0 || count > 1024 || !FiniteV(matrices, int(count) * 16)) return SHARD_E_INVALID_ARG;
                std::shared_ptr<ObjectRec> r = gObjects.Get(PluginOwner(handle), object);
                if (!r) return SHARD_E_STALE_HANDLE;
                std::shared_ptr<MeshRec> m = gMeshes.Get(PluginOwner(handle), r->mesh);
                if (!m || !m->skinned) return SHARD_E_INVALID_ARG;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->pose.resize(count);
                std::memcpy(r->pose.data(), matrices, size_t(count) * 16 * sizeof(float));
                ++r->poseVersion;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ObjectDestroy(ShardPluginCtx handle, ShardRenderObject object)
        {
            return gObjects.Remove(PluginOwner(handle), object) ? SHARD_OK : SHARD_E_STALE_HANDLE;
        }

        // ---- API: sprite and instance batches ----

        ShardResult SHARD_CALL SpriteBatchCreate(ShardPluginCtx handle, const ShardSpriteBatchDesc* desc, ShardSpriteBatch* out)
        {
            try {
                if (!out || !desc || desc->structSize < offsetof(ShardSpriteBatchDesc, origin)) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                if (!Prepare(handle, prepared)) return prepared;
                if (desc->maxSprites == 0 || desc->maxSprites > 262144 || desc->facing > SHARD_SPRITE_FACE_FIXED || desc->blend > SHARD_BLEND_PREMULTIPLIED) return SHARD_E_INVALID_ARG;
                if (desc->texture && !gTextures.Get(PluginOwner(handle), desc->texture)) return SHARD_E_STALE_HANDLE;
                auto rec = std::make_shared<SpriteBatchRec>();
                rec->owner = PluginOwner(handle);
                rec->desc = *desc;
                if (desc->structSize < sizeof(ShardSpriteBatchDesc)) rec->desc.origin[0] = rec->desc.origin[1] = rec->desc.origin[2] = 0.0;
                *out = gSpriteBatches.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SpriteBatchSet(ShardPluginCtx handle, ShardSpriteBatch batch, const ShardSprite* sprites, uint32_t count)
        {
            try {
                if (count && !sprites) return SHARD_E_INVALID_ARG;
                std::shared_ptr<SpriteBatchRec> r = gSpriteBatches.Get(PluginOwner(handle), batch);
                if (!r) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->sprites.assign(sprites, sprites + std::min(count, r->desc.maxSprites));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SpriteBatchSetFlags(ShardPluginCtx handle, ShardSpriteBatch batch, uint32_t flags)
        {
            std::shared_ptr<SpriteBatchRec> r = gSpriteBatches.Get(PluginOwner(handle), batch);
            if (!r) return SHARD_E_STALE_HANDLE;
            std::lock_guard<std::mutex> lock(r->mutex);
            r->desc.flags = flags;
            return SHARD_OK;
        }

        ShardResult SHARD_CALL SpriteBatchDestroy(ShardPluginCtx handle, ShardSpriteBatch batch)
        {
            return gSpriteBatches.Remove(PluginOwner(handle), batch) ? SHARD_OK : SHARD_E_STALE_HANDLE;
        }

        ShardResult SHARD_CALL InstanceBatchCreate(ShardPluginCtx handle, const ShardInstanceBatchDesc* desc, ShardInstanceBatch* out)
        {
            try {
                if (!out || !desc || desc->structSize < offsetof(ShardInstanceBatchDesc, origin)) return SHARD_E_INVALID_ARG;
                *out = 0;
                ShardResult prepared;
                if (!Prepare(handle, prepared)) return prepared;
                if (!gMeshes.Get(PluginOwner(handle), desc->mesh)) return SHARD_E_STALE_HANDLE;
                if (desc->maxInstances == 0 || (desc->materialCount && !desc->materials)) return SHARD_E_INVALID_ARG;
                auto rec = std::make_shared<InstanceBatchRec>();
                rec->owner = PluginOwner(handle);
                rec->mesh = desc->mesh;
                rec->materials.assign(desc->materials, desc->materials + desc->materialCount);
                rec->flags = desc->flags;
                rec->maxInstances = std::min<uint32_t>(desc->maxInstances, uint32_t(kMaxInstancesPerBatch));
                if (desc->structSize >= sizeof(ShardInstanceBatchDesc)) std::memcpy(rec->origin, desc->origin, sizeof(rec->origin));
                rec->componentBase = gNextComponentId.fetch_add(int(kMaxInstancesPerBatch));
                *out = gInstanceBatches.Add(PluginOwner(handle), rec);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL InstanceBatchSet(ShardPluginCtx handle, ShardInstanceBatch batch, const ShardInstance* instances, uint32_t count)
        {
            try {
                if (count && !instances) return SHARD_E_INVALID_ARG;
                std::shared_ptr<InstanceBatchRec> r = gInstanceBatches.Get(PluginOwner(handle), batch);
                if (!r) return SHARD_E_STALE_HANDLE;
                std::lock_guard<std::mutex> lock(r->mutex);
                r->instances.assign(instances, instances + std::min(count, r->maxInstances));
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL InstanceBatchDestroy(ShardPluginCtx handle, ShardInstanceBatch batch)
        {
            return gInstanceBatches.Remove(PluginOwner(handle), batch) ? SHARD_OK : SHARD_E_STALE_HANDLE;
        }

        // ---- API: the camera ----

        const CameraProxy* ActiveCamera()
        {
            Core::IEngineContext* engine = Engine();
            IRenderContext* renderer = engine ? engine->GetRenderContext() : nullptr;
            if (!renderer) return nullptr;
            const CameraProxy& camera = renderer->GetSnapshotBuffer().Latest().camera;
            return camera.valid ? &camera : nullptr;
        }

        // The camera in the world : its position (the render origin and the offset to it) and the matrix that brings a position relative to it into the view
        struct CameraFrame {
            glm::dvec3 position;
            glm::mat4 rotationView;
            glm::mat4 projection;
            uint32_t width = 0, height = 0;
            const CameraProxy* proxy = nullptr;
        };

        bool GetCameraFrame(CameraFrame& out)
        {
            const CameraProxy* camera = ActiveCamera();
            if (!camera) return false;
            Core::IEngineContext* engine = Engine();
            const RenderSnapshot& snapshot = engine->GetRenderContext()->GetSnapshotBuffer().Latest();
            out.proxy = camera;
            out.position = Core::WorldPosition::Offset(snapshot.renderOrigin, Core::WorldPosition()) + glm::dvec3(camera->position);
            // Only the orientation : the positions are brought to the camera before they are multiplied
            out.rotationView = glm::mat4(glm::mat3(camera->view));
            out.projection = camera->projection;
            out.width = uint32_t(std::max(snapshot.viewportWidth, 1));
            out.height = uint32_t(std::max(snapshot.viewportHeight, 1));
            return true;
        }

        ShardResult SHARD_CALL CameraInfo(ShardCameraInfo* out)
        {
            try {
                if (!out || out->structSize < sizeof(uint32_t)) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                if (!Engine() || !Engine()->GetRenderContext()) return SHARD_E_UNSUPPORTED;
                ShardCameraInfo info{};
                info.structSize = sizeof(ShardCameraInfo);
                CameraFrame frame;
                if (GetCameraFrame(frame)) {
                    const CameraProxy& c = *frame.proxy;
                    info.valid = SHARD_TRUE;
                    info.width = frame.width; info.height = frame.height;
                    info.orthographic = c.orthographic ? SHARD_TRUE : SHARD_FALSE;
                    info.fovDegrees = c.fovDegrees;
                    info.orthoSize = c.orthoSize;
                    info.nearPlane = c.nearPlane; info.farPlane = c.farPlane;
                    for (int i = 0; i < 3; ++i) info.position[i] = frame.position[i];
                    // The rotation as a quaternion from the view's rotation (its transpose is the camera's)
                    const glm::quat q = glm::quat_cast(glm::transpose(glm::mat3(c.view)));
                    info.rotation[0] = q.x; info.rotation[1] = q.y; info.rotation[2] = q.z; info.rotation[3] = q.w;
                    std::memcpy(info.view, &frame.rotationView[0][0], sizeof(info.view));
                    std::memcpy(info.projection, &frame.projection[0][0], sizeof(info.projection));
                }
                const uint32_t asked = out->structSize;
                std::memcpy(out, &info, std::min<size_t>(asked, sizeof(ShardCameraInfo)));
                out->structSize = asked;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL WorldToScreen(const double position[3], float out[3])
        {
            try {
                if (!position || !out || !std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                CameraFrame frame;
                if (!GetCameraFrame(frame)) return SHARD_E_UNSUPPORTED;
                const glm::vec3 relative = glm::vec3(glm::dvec3(position[0], position[1], position[2]) - frame.position);
                const glm::vec4 view = frame.rotationView * glm::vec4(relative, 1.0f);
                const glm::vec4 clip = frame.projection * view;
                out[2] = -view.z;                                       // the distance along the view axis (negative : behind)
                if (std::abs(clip.w) < 1e-9f) { out[0] = out[1] = 0.0f; return SHARD_OK; }
                const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                out[0] = (ndc.x * 0.5f + 0.5f) * float(frame.width);
                out[1] = (1.0f - (ndc.y * 0.5f + 0.5f)) * float(frame.height);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL ScreenToRay(float x, float y, double origin[3], float direction[3])
        {
            try {
                if (!origin || !direction || !std::isfinite(x) || !std::isfinite(y)) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                CameraFrame frame;
                if (!GetCameraFrame(frame)) return SHARD_E_UNSUPPORTED;
                const glm::vec2 ndc((x / float(frame.width)) * 2.0f - 1.0f, 1.0f - (y / float(frame.height)) * 2.0f);
                const glm::mat4 inverse = glm::inverse(frame.projection);
                glm::vec4 near = inverse * glm::vec4(ndc, -1.0f, 1.0f), far = inverse * glm::vec4(ndc, 1.0f, 1.0f);
                near /= near.w; far /= far.w;
                // In view space the camera is at the origin ; back to the world through the rotation
                const glm::mat3 toWorld = glm::transpose(glm::mat3(frame.rotationView));
                const glm::vec3 nearWorld = toWorld * glm::vec3(near);
                glm::vec3 dir = toWorld * glm::normalize(glm::vec3(far) - glm::vec3(near));
                if (frame.proxy->orthographic) {
                    for (int i = 0; i < 3; ++i) origin[i] = frame.position[i] + double(nearWorld[i]);
                } else {
                    for (int i = 0; i < 3; ++i) origin[i] = frame.position[i];
                }
                dir = glm::normalize(dir);
                direction[0] = dir.x; direction[1] = dir.y; direction[2] = dir.z;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        // ---- The snapshot ----

        glm::mat4 WorldMatrix(const ShardRenderTransform& t, const Core::WorldPosition& renderOrigin, const glm::dvec3& extra = glm::dvec3(0.0))
        {
            const glm::vec3 position = Core::WorldPosition::FromDVec3(glm::dvec3(t.position[0], t.position[1], t.position[2]) + extra).RelativeTo(renderOrigin);
            glm::quat q(t.rotation[3], t.rotation[0], t.rotation[1], t.rotation[2]);
            q = glm::dot(q, q) > 1e-12f ? glm::normalize(q) : glm::quat(1, 0, 0, 0);
            return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), glm::vec3(t.scale[0], t.scale[1], t.scale[2]));
        }

        std::shared_ptr<Material> DefaultMaterial()
        {
            static std::shared_ptr<Material> material;
            if (!material) {
                Core::IEngineContext* engine = Engine();
                if (engine && engine->GetResourcesManager())
                    material = engine->GetResourcesManager()->Get<Material>(Core::Resources::AssetKind::Material, "materials/default.mat");
            }
            return material;
        }

        // The materials of an object or a batch, one per slot of its mesh (the missing ones are the default)
        std::vector<std::shared_ptr<Material>> MaterialsOf(PluginOwner owner, const std::vector<ShardMaterial>& handles, size_t slots)
        {
            std::vector<std::shared_ptr<Material>> out;
            const size_t count = std::max(slots, handles.size());
            for (size_t i = 0; i < count; ++i) {
                std::shared_ptr<Material> m = i < handles.size() ? ResolveMaterial(owner, handles[i]) : nullptr;
                out.push_back(m ? m : DefaultMaterial());
            }
            return out;
        }

        bool ActorId(ShardActor handle, int& id)
        {
            if (!handle) return false;
            std::shared_ptr<Objects::Actor> actor = ActorOf(handle);
            if (!actor) return false;
            id = actor->GetID().GetAsInt();
            return true;
        }
    }

    // ---- Shared with the overlay file ----

    void HookRenderRelease(PluginContext& ctx, PluginOwner owner) { HookRelease(ctx, owner); }

    std::shared_ptr<Texture2D> ResolveTexture(PluginOwner owner, ShardTexture handle)
    {
        std::shared_ptr<TextureRec> r = gTextures.Get(owner, handle);
        if (!r) return nullptr;
        EnsureTexture(*r);
        std::lock_guard<std::mutex> lock(r->mutex);
        return r->texture;
    }

    std::shared_ptr<Framebuffer> ResolveRenderTarget(PluginOwner owner, ShardTexture handle)
    {
        std::shared_ptr<TextureRec> r = gTextures.Get(owner, handle);
        if (!r || !(r->flags & SHARD_TEXTURE_RENDER_TARGET)) return nullptr;
        EnsureTexture(*r);
        std::lock_guard<std::mutex> lock(r->mutex);
        return r->framebuffer;
    }

    uint64_t NativeTextureId(ShardTexture handle, bool* flipV)
    {
        std::shared_ptr<TextureRec> r = gTextures.GetAny(handle);
        if (!r) return 0;
        EnsureTexture(*r);
        std::lock_guard<std::mutex> lock(r->mutex);
        if (r->framebuffer) { if (flipV) *flipV = true; return r->framebuffer->GetColorAttachment(); }
        if (flipV) *flipV = false;
        return r->texture ? r->texture->GetHandle() : 0;
    }

    void ExtractPluginRender(const RenderSnapshot& previous, RenderSnapshot& next)
    {
        gUploads = 0;
        // The instances of the batches, as one mesh proxy each
        gInstanceBatches.ForEachAll([&](uint64_t handle, InstanceBatchRec& batch, PluginOwner owner) {
            std::shared_ptr<MeshRec> meshRec = gMeshes.Get(owner, batch.mesh);
            if (!meshRec || (batch.flags & SHARD_OBJECT_HIDDEN)) return;
            std::shared_ptr<Mesh> mesh = GpuMesh(*meshRec);
            if (!mesh) return;
            std::vector<std::shared_ptr<Material>> materials = MaterialsOf(owner, batch.materials, meshRec->slots);
            std::lock_guard<std::mutex> lock(batch.mutex);
            for (size_t i = 0; i < batch.instances.size(); ++i) {
                const ShardInstance& inst = batch.instances[i];
                ShardRenderTransform t{};
                for (int k = 0; k < 3; ++k) { t.position[k] = double(inst.position[k]); t.scale[k] = inst.scale[k]; }
                for (int k = 0; k < 4; ++k) t.rotation[k] = inst.rotation[k];
                MeshProxy proxy;
                proxy.key = (uint64_t(kInstanceWorld) << 48) | ((handle & 0xFFFFFFull) << 24) | uint64_t(i & 0xFFFFFF);
                proxy.componentId = batch.componentBase + int(i);
                proxy.mesh = mesh;
                proxy.materials = materials;
                proxy.worldMatrix = WorldMatrix(t, next.renderOrigin, glm::dvec3(batch.origin[0], batch.origin[1], batch.origin[2]));
                proxy.staticLighting = false;
                proxy.lightmapPage = -1;
                next.meshes.push_back(std::move(proxy));
            }
        });

        // The render objects
        gObjects.ForEachAll([&](uint64_t handle, ObjectRec& object, PluginOwner owner) {
            if (object.flags & SHARD_OBJECT_HIDDEN) return;
            std::shared_ptr<MeshRec> meshRec = gMeshes.Get(owner, object.mesh);
            if (!meshRec) return;
            std::shared_ptr<Mesh> mesh;
            {
                std::lock_guard<std::mutex> lock(object.mutex);
                mesh = (meshRec->skinned && !object.pose.empty()) ? SkinnedMesh(object, *meshRec) : GpuMesh(*meshRec);
            }
            if (!mesh) return;
            std::vector<std::shared_ptr<Material>> materials = MaterialsOf(owner, object.materials, meshRec->slots);
            std::lock_guard<std::mutex> lock(object.mutex);
            MeshProxy proxy;
            proxy.key = (uint64_t(kObjectWorld) << 48) | (handle & 0xFFFFFFFFull);
            int actorId = -1;
            proxy.objectId = ActorId(object.pick, actorId) ? actorId : -1;
            proxy.componentId = object.componentId;
            proxy.mesh = mesh;
            proxy.materials = std::move(materials);
            proxy.worldMatrix = WorldMatrix(object.transform, next.renderOrigin);
            proxy.staticLighting = false;
            proxy.lightmapPage = -1;
            next.meshes.push_back(std::move(proxy));
        });

        // The sprite batches
        gSpriteBatches.ForEachAll([&](uint64_t handle, SpriteBatchRec& batch, PluginOwner owner) {
            std::lock_guard<std::mutex> lock(batch.mutex);
            if (batch.sprites.empty()) return;
            SpriteBatchProxy proxy;
            proxy.key = (uint64_t(kSpriteWorld) << 48) | (handle & 0xFFFFFFFFull);
            proxy.facing = uint8_t(batch.desc.facing);
            proxy.blend = uint8_t(batch.desc.blend);
            proxy.sort = uint8_t(batch.desc.sort);
            proxy.flags = uint8_t(batch.desc.flags);
            if (batch.desc.texture) proxy.texture = ResolveTexture(owner, batch.desc.texture);
            const glm::vec3 origin = Core::WorldPosition::FromDVec3(glm::dvec3(batch.desc.origin[0], batch.desc.origin[1], batch.desc.origin[2])).RelativeTo(next.renderOrigin);
            proxy.sprites.reserve(batch.sprites.size());
            for (const ShardSprite& s : batch.sprites) {
                SpriteInstance in;
                in.position = origin + glm::vec3(s.position[0], s.position[1], s.position[2]);
                in.rotation = s.rotation;
                in.size = glm::vec2(s.size[0], s.size[1]);
                in.color = s.color;
                in.uv = glm::vec4(s.uvRect[0], s.uvRect[1], s.uvRect[2], s.uvRect[3]);
                in.axis = glm::vec3(s.axis[0], s.axis[1], s.axis[2]);
                in.custom = s.custom;
                proxy.sprites.push_back(in);
            }
            next.spriteBatches.push_back(std::move(proxy));
        });

        ExtractOverlays(previous, next);
    }

    const void* RenderApi()
    {
        static const ShardRenderApi api = {
            sizeof(ShardRenderApi),
            &MeshCreate, &MeshUpdate, &MeshUpdateRange, &MeshFromAsset, &MeshDestroy,
            &TextureCreate, &TextureUpdate, &TextureLoad, &TextureFromAsset, &TextureSize, &TextureDestroy,
            &MaterialLoad, &MaterialSetFloat, &MaterialSetVec4, &MaterialSetTexture, &MaterialDestroy,
            &ObjectCreate, &ObjectSetTransform, &ObjectSetMesh, &ObjectSetMaterials, &ObjectSetFlags, &ObjectSetPose, &ObjectDestroy,
            &SpriteBatchCreate, &SpriteBatchSet, &SpriteBatchSetFlags, &SpriteBatchDestroy,
            &InstanceBatchCreate, &InstanceBatchSet, &InstanceBatchDestroy,
            &OverlayAdd, &OverlayRemove, &GeometryCreate, &GeometryDestroy,
            &CameraInfo, &WorldToScreen, &ScreenToRay };
        return &api;
    }
}

namespace Shard::Engine::Plugins {

    uint64_t RenderTextureForImGui(uint64_t texture, bool* flipV)
    {
        // The editor asks from its drawing code, on the main thread : the texture is made if it is not yet
        return CAbi::NativeTextureId(texture, flipV);
    }
}
