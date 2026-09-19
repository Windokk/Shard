#include "raytrace_scene.hpp"

#include "engine/rendering/raytracing/bvh.hpp"
#include "engine/rendering/raytracing/parallel_build_budget.hpp"

#include "engine/levels/level.hpp"

#include "engine/objects/actors/actor.hpp"
#include "engine/objects/components/rendering/model_component.hpp"
#include "engine/objects/components/misc/transform.hpp"

#include "engine/rendering/mesh/mesh.hpp"
#include "engine/rendering/material/material.hpp"
#include "engine/rendering/pipeline/pipeline.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <system_error>
#include <thread>
#include <unordered_map>

namespace Shard::Engine::Rendering::Raytracing {

    namespace {

        // Spawns worker threads that each repeatedly pull the next unclaimed grain of work (a
        // [begin, end) sub-range, at most `grainSize` elements) and run `fn` over it, until every grain
        // of [0, count) has been claimed - a work-stealing parallel-for, so it needs no a-priori
        // per-thread partitioning and load-balances fine even when far fewer threads actually get
        // spawned than hoped (see below).
        //
        // How many threads actually get spawned is bounded by the process-wide budget in
        // parallel_build_budget.hpp, shared with the flatten pass's own worker loop below and with
        // bvh.cpp's BVH build - not just this call's own hardware_concurrency(). Multiple scene builds
        // can be in flight at once (ProbeManager::RebuildScene() supersedes rather than cancels a
        // still-running previous build - see its comment), and each independently trying to spawn up to
        // hardware_concurrency() threads of its own is what used to let concurrent builds pile up more
        // OS threads than the machine could hand out, which throws std::system_error - hence both the
        // shared budget and the try/catch around the spawn itself, so a failed spawn just means this
        // grain runs on the calling thread instead of propagating.
        template <typename F>
        void ParallelForRange(size_t count, size_t grainSize, F&& fn)
        {
            if (count == 0)
                return;

            std::atomic<size_t> nextBegin{ 0 };
            auto pullAndRun = [&]()
            {
                size_t begin;
                while ((begin = nextBegin.fetch_add(grainSize, std::memory_order_relaxed)) < count)
                    fn(begin, std::min(count, begin + grainSize));
            };

            std::vector<std::future<void>> workers;
            size_t grainCount = (count + grainSize - 1) / grainSize;
            int maxExtraWorkers = std::max(0, std::min((int)grainCount - 1, (int)std::thread::hardware_concurrency() - 1));
            for (int i = 0; i < maxExtraWorkers; i++)
            {
                if (!TryAcquireWorkerSlot())
                    break;

                try
                {
                    workers.push_back(std::async(std::launch::async, [&pullAndRun]() {
                        pullAndRun();
                        ReleaseWorkerSlot();
                    }));
                }
                catch (const std::system_error&)
                {
                    ReleaseWorkerSlot();
                    break;
                }
            }

            pullAndRun(); // the calling thread pulls grains too, instead of sitting idle until others finish

            for (auto& w : workers)
                w.wait();
        }

        bool FindElementOffset(const VertexLayout& layout, const std::string& name, uint32_t& outOffset)
        {
            for (const auto& element : layout.GetElements())
            {
                if (element.name == name)
                {
                    outOffset = element.offset;
                    return true;
                }
            }
            return false;
        }

        glm::vec3 ReadVec3(const uint8_t* base, size_t vertexOffset, uint32_t elementOffset)
        {
            glm::vec3 v;
            std::memcpy(&v, base + vertexOffset + elementOffset, sizeof(glm::vec3));
            return v;
        }

        glm::vec2 ReadVec2(const uint8_t* base, size_t vertexOffset, uint32_t elementOffset)
        {
            glm::vec2 v;
            std::memcpy(&v, base + vertexOffset + elementOffset, sizeof(glm::vec2));
            return v;
        }

        glm::uvec2 PackBindlessHandle(uint64_t handle)
        {
            return glm::uvec2((uint32_t)(handle & 0xFFFFFFFFu), (uint32_t)(handle >> 32));
        }

        bool IsMasked(const std::shared_ptr<Material>& mat)
        {
            if (!mat)
                return false;

            if (auto v = mat->GetScalarParameter("masked"))
                if (auto* b = std::get_if<bool>(&*v))
                    return *b;

            return false;
        }

    }

    // Reads scalar parameters, then (when the material has one bound) a bindless handle for each of
    // the 5 texture slots the raytracer understands - same sampler names the rasterizer's "lit" shader
    // uses, so any material authored for the rasterizer picks up its textures here for free.
    GPUMaterial ExtractMaterial(const std::shared_ptr<Material>& mat)
    {
        GPUMaterial gm;

        if (!mat)
            return gm;

        if (auto v = mat->GetScalarParameter("albedo"))
        {
            if (auto* vec3v = std::get_if<glm::vec3>(&*v))
                gm.albedo = glm::vec4(*vec3v, 1.0f);
            else if (auto* vec4v = std::get_if<glm::vec4>(&*v))
                gm.albedo = *vec4v;
        }

        if (auto v = mat->GetScalarParameter("roughness"))
            if (auto* f = std::get_if<float>(&*v))
                gm.roughness = *f;

        if (auto v = mat->GetScalarParameter("metallic"))
            if (auto* f = std::get_if<float>(&*v))
                gm.metallic = *f;

        if (auto v = mat->GetScalarParameter("emissive"))
        {
            if (auto* vec3v = std::get_if<glm::vec3>(&*v))
                gm.emissive = glm::vec4(*vec3v, 1.0f);
            else if (auto* f = std::get_if<float>(&*v))
                gm.emissive = glm::vec4(*f);
        }

        if (uint64_t h = mat->GetTextureParameter("albedo"))
        {
            gm.albedoTex = PackBindlessHandle(h);
            gm.textureFlags |= GPUMaterialTexAlbedo;
        }

        if (uint64_t h = mat->GetTextureParameter("metallicMap"))
        {
            gm.metallicTex = PackBindlessHandle(h);
            gm.textureFlags |= GPUMaterialTexMetallic;
        }

        if (uint64_t h = mat->GetTextureParameter("roughnessMap"))
        {
            gm.roughnessTex = PackBindlessHandle(h);
            gm.textureFlags |= GPUMaterialTexRoughness;
        }

        if (uint64_t h = mat->GetTextureParameter("normalMap"))
        {
            gm.normalTex = PackBindlessHandle(h);
            gm.textureFlags |= GPUMaterialTexNormal;
        }

        // Multiplies the `emissive` scalar above (glTF-style, same as lit.frag) rather than replacing it,
        // so a map with no `emissive` factor stays dark here exactly as it does in the forward pass.
        if (uint64_t h = mat->GetTextureParameter("emissiveMap"))
        {
            gm.emissiveTex = PackBindlessHandle(h);
            gm.textureFlags |= GPUMaterialTexEmissive;
        }

        return gm;
    }

    RaytraceScene SceneBuilder::Build(Levels::Level* level)
    {
        return BuildFromSnapshot(CaptureSnapshot(level));
    }

    RaytraceSceneSnapshot SceneBuilder::CaptureSnapshot(Levels::Level* level, bool excludeMasked)
    {
        RaytraceSceneSnapshot snapshot;

        if (!level)
            return snapshot;

        std::unordered_map<Rendering::Material*, uint32_t> materialIndices;

        for (auto& [id, model] : level->models)
        {
            if (!model || !model->Active())
                continue;

            std::shared_ptr<Mesh> mesh = model->GetMesh();
            if (!mesh)
                continue;

            std::vector<std::shared_ptr<Material>> mats = model->GetMaterials();
            if (mats.empty())
                continue;

            const std::vector<SubMesh>& submeshes = mesh->GetSubMeshes();

            ModelSnapshot modelSnap;
            modelSnap.mesh = mesh;
            modelSnap.worldMatrix = model->parent->transform->GetWorldMatrix();
            modelSnap.materialIndicesPerSubmesh.resize(submeshes.size());

            for (size_t submeshIdx = 0; submeshIdx < submeshes.size(); submeshIdx++)
            {
                // Submeshes are in FBX material-slot order; use each submesh's own
                // slot so it lines up with the model component's material list.
                size_t slot = std::min<size_t>(submeshes[submeshIdx].materialIndex, mats.size() - 1);
                std::shared_ptr<Material> mat = mats[slot];

                if (excludeMasked && IsMasked(mat))
                {
                    modelSnap.materialIndicesPerSubmesh[submeshIdx] = kSkippedSubmesh;
                    continue;
                }

                uint32_t materialIndex;
                auto it = materialIndices.find(mat.get());
                if (it != materialIndices.end())
                {
                    materialIndex = it->second;
                }
                else
                {
                    materialIndex = (uint32_t)snapshot.materials.size();
                    snapshot.materials.push_back(ExtractMaterial(mat));
                    materialIndices.emplace(mat.get(), materialIndex);
                }

                modelSnap.materialIndicesPerSubmesh[submeshIdx] = materialIndex;
            }

            snapshot.models.push_back(std::move(modelSnap));
        }

        return snapshot;
    }

    namespace {

        // Resolved once per model (vertex layout lookups + the world/normal/tangent matrices), read-only
        // from every worker thread that later flattens one of that model's triangle chunks below.
        struct FlattenModelInfo
        {
            const Mesh* mesh = nullptr;
            uint32_t stride = 0;
            uint32_t posOffset = 0, normalOffset = 0, uvOffset = 0, tangentOffset = 0;
            bool hasNormal = false, hasUV = false, hasTangent = false;
            glm::mat4 worldMatrix{ 1.0f };
            glm::mat3 normalMatrix{ 1.0f };
            glm::mat3 tangentMatrix{ 1.0f };
        };

        // One unit of parallel flatten work : a contiguous run of `triangleCount` triangles starting at
        // `indexOffset` into its model's index buffer, destined for positions/attribs[destOffset ...].
        // Kept to at most kChunkTriangles triangles (see BuildFromSnapshot) so one oversized submesh
        // (e.g. a Sponza floor slab) still gets spread across every worker thread instead of pinning one.
        struct FlattenChunk
        {
            uint32_t modelInfoIdx;
            size_t indexOffset;
            uint32_t materialIndex;
            size_t triangleCount;
            size_t destOffset;
        };

        // Vertex-fetch + world-space transform for one chunk's triangles - the actual per-triangle cost
        // that used to run single-threaded in a nested model/submesh/triangle loop. Writes only to
        // positions/attribs[chunk.destOffset, chunk.destOffset + chunk.triangleCount) - disjoint from
        // every other chunk's range, so this is safe to call concurrently from multiple threads with no
        // locking, as long as each chunk is only ever handed to one thread (see the work-stealing loop
        // in BuildFromSnapshot).
        void FlattenChunkInto(const FlattenChunk& chunk, const FlattenModelInfo& info,
            std::vector<GPUTrianglePos>& positions, std::vector<GPUTriangleAttrib>& attribs)
        {
            const std::vector<uint8_t>& vertexData = info.mesh->GetVertices();
            const std::vector<uint32_t>& indices = info.mesh->GetIndices();

            for (size_t t = 0; t < chunk.triangleCount; t++)
            {
                GPUTrianglePos triPos{};
                GPUTriangleAttrib triAttrib{};
                triAttrib.uvMatID.w = (float)chunk.materialIndex;

                for (int k = 0; k < 3; k++)
                {
                    uint32_t vertexIdx = indices[chunk.indexOffset + t * 3 + k];
                    size_t vertexByteOffset = (size_t)vertexIdx * info.stride;

                    glm::vec3 localPos = ReadVec3(vertexData.data(), vertexByteOffset, info.posOffset);
                    glm::vec3 worldPos = glm::vec3(info.worldMatrix * glm::vec4(localPos, 1.0f));

                    glm::vec3 worldNormal(0.0f, 1.0f, 0.0f);
                    if (info.hasNormal)
                        worldNormal = glm::normalize(info.normalMatrix * ReadVec3(vertexData.data(), vertexByteOffset, info.normalOffset));

                    glm::vec2 uv(0.0f);
                    if (info.hasUV)
                        uv = ReadVec2(vertexData.data(), vertexByteOffset, info.uvOffset);

                    glm::vec3 worldTangent(1.0f, 0.0f, 0.0f);
                    if (info.hasTangent)
                        worldTangent = glm::normalize(info.tangentMatrix * ReadVec3(vertexData.data(), vertexByteOffset, info.tangentOffset));

                    if (k == 0)
                    {
                        triPos.v0 = glm::vec4(worldPos, 0.0f);
                        triAttrib.n0 = glm::vec4(worldNormal, uv.x);
                        triAttrib.uvMatID.x = uv.y;
                        triAttrib.t0 = glm::vec4(worldTangent, 0.0f);
                    }
                    else if (k == 1)
                    {
                        triPos.v1 = glm::vec4(worldPos, 0.0f);
                        triAttrib.n1 = glm::vec4(worldNormal, uv.x);
                        triAttrib.uvMatID.y = uv.y;
                        triAttrib.t1 = glm::vec4(worldTangent, 0.0f);
                    }
                    else
                    {
                        triPos.v2 = glm::vec4(worldPos, 0.0f);
                        triAttrib.n2 = glm::vec4(worldNormal, uv.x);
                        triAttrib.uvMatID.z = uv.y;
                        triAttrib.t2 = glm::vec4(worldTangent, 0.0f);
                    }
                }

                positions[chunk.destOffset + t] = triPos;
                attribs[chunk.destOffset + t] = triAttrib;
            }
        }

        // Reports `fraction` through `onProgress` only if it's a new high-water mark - once flatten work
        // (and, in bvh.cpp, the BVH build itself) runs on multiple worker threads, a slower thread can
        // finish a chunk after a faster one already reported further along; without this clamp the
        // progress bar would visibly jump backward. `reported` is shared across every calling thread.
        void ReportMonotonic(std::atomic<float>& reported, float fraction, const char* phase,
            const SceneBuilder::ProgressCallback& onProgress)
        {
            if (!onProgress)
                return;

            float prev = reported.load(std::memory_order_relaxed);
            while (fraction > prev)
            {
                if (reported.compare_exchange_weak(prev, fraction, std::memory_order_relaxed))
                {
                    onProgress(fraction, phase);
                    break;
                }
            }
        }

    }

    RaytraceScene SceneBuilder::BuildFromSnapshot(const RaytraceSceneSnapshot &snapshot, const ProgressCallback& onProgress)
    {
        // Flatten runs 0 -> kFlattenEnd, BVH build kFlattenEnd -> kBvhEnd, the final payload reorder
        // kBvhEnd -> 1. The split is a rough guess at the relative cost, not measured.
        constexpr float kFlattenEnd = 0.45f;
        constexpr float kBvhEnd = 0.95f;
        // Within the flatten phase : laying out chunks (cheap, submesh-count-bound) gets a small fixed
        // slice, the actual per-triangle vertex flatten below (the expensive part) gets the rest.
        constexpr float kChunkLayoutEnd = kFlattenEnd * 0.1f;

        std::atomic<float> reportedFraction{ 0.0f };
        auto report = [&](float fraction, const char* phase)
        {
            ReportMonotonic(reportedFraction, fraction, phase, onProgress);
        };

        report(0.0f, "Baking scene info");

        RaytraceScene scene;
        scene.materials = snapshot.materials;

        // ---- Pass 1 : resolve each model's layout/matrices once and lay out every surviving submesh's
        // triangles into fixed-size chunks, computing each chunk's final destination offset up front.
        // Cheap - bounded by submesh count, not triangle count - so this stays single-threaded; pass 2
        // below is what actually parallelizes.
        constexpr size_t kChunkTriangles = 2048;

        std::vector<FlattenModelInfo> modelInfos;
        modelInfos.reserve(snapshot.models.size());
        std::vector<FlattenChunk> chunks;

        size_t totalTriangles = 0;

        const size_t modelCount = snapshot.models.size();
        size_t modelIdx = 0;
        for (auto& modelSnap : snapshot.models)
        {
            ++modelIdx;
            if (modelCount > 0)
                report(kChunkLayoutEnd * (float)modelIdx / (float)modelCount, "Baking scene info");

            const Mesh* mesh = modelSnap.mesh.get();
            if (!mesh)
                continue;

            const VertexLayout& layout = mesh->GetVertexLayout();

            FlattenModelInfo info;
            info.mesh = mesh;
            info.stride = layout.GetStride();
            info.hasNormal = FindElementOffset(layout, "aNormal", info.normalOffset);
            info.hasUV = FindElementOffset(layout, "aTexCoord", info.uvOffset);
            info.hasTangent = FindElementOffset(layout, "aTangent", info.tangentOffset);
            bool hasPos = FindElementOffset(layout, "aPos", info.posOffset);

            if (!hasPos)
                continue;

            info.worldMatrix = modelSnap.worldMatrix;
            info.normalMatrix = glm::transpose(glm::inverse(glm::mat3(info.worldMatrix)));
            // Tangents are surface directions, not normals - transform with the plain model 3x3 (matching
            // lit.vert's `mat3(model) * aTangent`), not the inverse-transpose normal matrix above.
            info.tangentMatrix = glm::mat3(info.worldMatrix);

            uint32_t modelInfoIdx = (uint32_t)modelInfos.size();
            modelInfos.push_back(info);

            const std::vector<SubMesh>& submeshes = mesh->GetSubMeshes();
            for (size_t submeshIdx = 0; submeshIdx < submeshes.size(); submeshIdx++)
            {
                const SubMesh& submesh = submeshes[submeshIdx];
                uint32_t materialIndex = modelSnap.materialIndicesPerSubmesh[submeshIdx];

                if (materialIndex == kSkippedSubmesh)
                    continue;

                size_t triangleCount = submesh.indexCount / 3;
                for (size_t start = 0; start < triangleCount; start += kChunkTriangles)
                {
                    size_t count = std::min(kChunkTriangles, triangleCount - start);
                    chunks.push_back(FlattenChunk{ modelInfoIdx, submesh.indexOffset + start * 3, materialIndex, count, totalTriangles });
                    totalTriangles += count;
                }
            }
        }

        if (totalTriangles == 0)
        {
            scene.bvhNodes = { BVHNode{ glm::vec3(0.0f), 0, glm::vec3(0.0f), 0 } };
            report(1.0f, "Building BVH");
            return scene;
        }

        report(kChunkLayoutEnd, "Baking scene info");

        // ---- Pass 2 : flatten every chunk's triangles. Each chunk owns a disjoint destination range
        // (see FlattenChunkInto), so this is a plain work-stealing parallel-for : every worker thread
        // (including the calling one, so a single-core machine or a tiny chunk list still just runs
        // this inline) pulls the next unclaimed chunk index from a shared atomic counter until none are
        // left. Load-balances even a single huge submesh across every thread, since it was already cut
        // into kChunkTriangles-sized pieces above rather than handed out whole.
        std::vector<GPUTrianglePos> positions(totalTriangles);
        std::vector<GPUTriangleAttrib> attribs(totalTriangles);

        std::atomic<size_t> nextChunk{ 0 };
        std::atomic<size_t> chunksDone{ 0 };

        auto worker = [&]()
        {
            size_t idx;
            while ((idx = nextChunk.fetch_add(1, std::memory_order_relaxed)) < chunks.size())
            {
                const FlattenChunk& chunk = chunks[idx];
                FlattenChunkInto(chunk, modelInfos[chunk.modelInfoIdx], positions, attribs);

                size_t done = chunksDone.fetch_add(1, std::memory_order_relaxed) + 1;
                float frac = kChunkLayoutEnd + (kFlattenEnd - kChunkLayoutEnd) * (float)done / (float)chunks.size();
                report(frac, "Baking scene info");
            }
        };

        // Threads spawned here draw from the same process-wide budget as bvh.cpp's BVH build and this
        // function's own ParallelForRange() calls below - see parallel_build_budget.hpp for why a
        // per-call hardware_concurrency()-sized budget isn't safe when more than one scene build can be
        // in flight at once (ProbeManager::RebuildScene() supersedes rather than cancels a still-running
        // previous build). A failed spawn attempt just means this chunk list gets fewer helper threads,
        // not a propagated exception - worker() below always runs on the calling thread regardless.
        std::vector<std::future<void>> workers;
        int maxExtraWorkers = std::max(0, std::min((int)chunks.size() - 1, (int)std::thread::hardware_concurrency() - 1));
        for (int i = 0; i < maxExtraWorkers; i++)
        {
            if (!TryAcquireWorkerSlot())
                break;

            try
            {
                workers.push_back(std::async(std::launch::async, [&worker]() {
                    worker();
                    ReleaseWorkerSlot();
                }));
            }
            catch (const std::system_error&)
            {
                ReleaseWorkerSlot();
                break;
            }
        }
        worker(); // the calling thread pulls chunks too, instead of sitting idle until the others finish
        for (auto& w : workers)
            w.wait();

        report(kFlattenEnd, "Building BVH");

        std::vector<BVHPrimitive> primitives(positions.size());
        ParallelForRange(positions.size(), 4096, [&](size_t begin, size_t end)
        {
            for (size_t i = begin; i < end; i++)
            {
                glm::vec3 v0 = glm::vec3(positions[i].v0);
                glm::vec3 v1 = glm::vec3(positions[i].v1);
                glm::vec3 v2 = glm::vec3(positions[i].v2);

                primitives[i].boundsMin = glm::min(v0, glm::min(v1, v2));
                primitives[i].boundsMax = glm::max(v0, glm::max(v1, v2));
                primitives[i].centroid = (v0 + v1 + v2) / 3.0f;
            }
        });

        std::vector<uint32_t> order;
        scene.bvhNodes = BVHBuilder::Build(primitives, order,
            [&](float p) { report(kFlattenEnd + (kBvhEnd - kFlattenEnd) * p, "Building BVH"); });

        // Gather through `order` (BVHBuilder::Build's leaf-triangle permutation) into the final,
        // BVH-friendly layout - a random-access scatter/gather over up to ~160 bytes/triangle, so on a
        // large scene this is cache-unfriendly enough to be memory-bandwidth bound rather than
        // compute bound; splitting it across cores lets several memory-bound streams overlap instead of
        // one thread alone waiting on cache misses for the whole array.
        scene.trianglePositions.resize(order.size());
        scene.triangleAttribs.resize(order.size());
        ParallelForRange(order.size(), 4096, [&](size_t begin, size_t end)
        {
            for (size_t i = begin; i < end; i++)
            {
                scene.trianglePositions[i] = positions[order[i]];
                scene.triangleAttribs[i] = attribs[order[i]];
            }
        });

        report(1.0f, "Building BVH");
        return scene;
    }

}
