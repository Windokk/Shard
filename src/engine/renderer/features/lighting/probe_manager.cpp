#include "probe_manager.hpp"
#include "engine/renderer/rhi/render_thread.hpp"

#include "engine/renderer/features/raytracing/bvh.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/renderer/features/raytracing/raytrace_scene.hpp"


#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/shader/compute_shader.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"


#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/jobs/thread_pool.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <random>

#include <glm/gtc/quaternion.hpp>

namespace Shard::Engine::Rendering {

    namespace {

        // Bounded random rotation (Archimedes axis). A full one made DDGI_VisibilityWeight flicker between ~1 and ~0 each frame.
        glm::mat3 RandomRotation(std::mt19937& rng, float maxAngleRadians)
        {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);

            float z = dist(rng) * 2.0f - 1.0f;
            float theta = dist(rng) * 6.28318530718f;
            float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            glm::vec3 axis(r * std::cos(theta), r * std::sin(theta), z);

            float angle = (dist(rng) * 2.0f - 1.0f) * maxAngleRadians;

            return glm::mat3_cast(glm::angleAxis(angle, axis));
        }

        // Shoemake random unit quaternion, uniform over SO(3). Only for the bake's averaging phase, where the flicker averages out.
        glm::mat3 UniformRandomRotation(std::mt19937& rng)
        {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);

            const float u1 = dist(rng);
            const float u2 = dist(rng) * 6.28318530718f;
            const float u3 = dist(rng) * 6.28318530718f;
            const float a = std::sqrt(1.0f - u1);
            const float b = std::sqrt(u1);

            return glm::mat3_cast(glm::quat(b * std::cos(u3), a * std::sin(u2), a * std::cos(u2), b * std::sin(u3)));
        }

        // Each ray is one texel of the octahedral tile, so round raysPerProbe down to a perfect square, capped at kMaxRaysPerProbe. Rebuild and bake compatibility check must agree.
        uint32_t ComputeTileSize(int raysPerProbe)
        {
            int rays = std::clamp(raysPerProbe, 1, kMaxRaysPerProbe);
            return (uint32_t)std::max(1, (int)std::floor(std::sqrt((float)rays)));
        }

        TextureSpecifications MakeAtlasSpec(uint32_t atlasSize, TextureInternalFormat format, bool halfFloatPixelData)
        {
            TextureSpecifications spec;
            spec.width = atlasSize;
            spec.height = atlasSize;
            spec.internalFormat = format;
            spec.generateMips = false;
            spec.immutableStorage = true;
            spec.minFilter = TextureFilter::Linear;
            spec.magFilter = TextureFilter::Linear;
            spec.wrapS = TextureWrap::ClampEdge;
            spec.wrapT = TextureWrap::ClampEdge;
            spec.halfFloatPixelData = halfFloatPixelData;
            return spec;
        }

    }

    ProbeManager::ProbeManager() : m_RayRNG(std::random_device{}()) {}
    ProbeManager::~ProbeManager()
    {
        WaitForSceneBuilds();
    }

    void ProbeManager::RebuildScene(const RenderSnapshot& scene)
    {
        Raytracing::RaytraceSceneSnapshot snapshot = Raytracing::SceneBuilder::CaptureSnapshot(scene, /*excludeMasked=*/true);

        // std::async futures block in their destructor, so park the old build instead of reassigning.
        if (m_PendingSceneBuild.valid())
            m_AbandonedSceneBuilds.push_back(std::move(m_PendingSceneBuild));

        const uint64_t generation = m_SceneBuildGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
        m_PendingSceneBuildGeneration = generation;

        m_SceneBuilding.store(true, std::memory_order_relaxed);
        m_SceneBuildProgress.store(0.0f, std::memory_order_relaxed);
        m_SceneBuildPhase.store("Baking scene info", std::memory_order_relaxed);

        auto buildJob =
            [this, generation, snapshot = std::move(snapshot)]()
            {
                auto onProgress = [this, generation](float fraction, const char* phase)
                {
                    if (m_SceneBuildGeneration.load(std::memory_order_relaxed) != generation)
                        return;
                    m_SceneBuildProgress.store(fraction, std::memory_order_relaxed);
                    m_SceneBuildPhase.store(phase, std::memory_order_relaxed);
                };
                return Raytracing::SceneBuilder::BuildFromSnapshot(snapshot, onProgress);
            };

        if (Core::ThreadPool* io = Core::GetEngine().GetThreadPool(Core::PoolKind::IO))
            m_PendingSceneBuild = io->Submit(std::move(buildJob));
        else
            m_PendingSceneBuild = std::async(std::launch::async, std::move(buildJob));

        m_SceneBuilt = false;
    }

    void ProbeManager::UploadScene(const Raytracing::RaytraceScene &scene)
    {
        if (scene.trianglePositions.empty())
        {
            m_SceneBuilt = false;
            return;
        }

        m_BVHBuffer = StorageBuffer::Create((uint32_t)(scene.bvhNodes.size() * sizeof(Raytracing::BVHNode)));
        m_BVHBuffer->SetData(scene.bvhNodes.data(), (uint32_t)(scene.bvhNodes.size() * sizeof(Raytracing::BVHNode)));

        m_PosBuffer = StorageBuffer::Create((uint32_t)(scene.trianglePositions.size() * sizeof(Raytracing::GPUTrianglePos)));
        m_PosBuffer->SetData(scene.trianglePositions.data(), (uint32_t)(scene.trianglePositions.size() * sizeof(Raytracing::GPUTrianglePos)));

        m_AttribBuffer = StorageBuffer::Create((uint32_t)(scene.triangleAttribs.size() * sizeof(Raytracing::GPUTriangleAttrib)));
        m_AttribBuffer->SetData(scene.triangleAttribs.data(), (uint32_t)(scene.triangleAttribs.size() * sizeof(Raytracing::GPUTriangleAttrib)));

        m_MatBuffer = StorageBuffer::Create((uint32_t)(scene.materials.size() * sizeof(Raytracing::GPUMaterial)));
        if (!scene.materials.empty())
            m_MatBuffer->SetData(scene.materials.data(), (uint32_t)(scene.materials.size() * sizeof(Raytracing::GPUMaterial)));

        m_SceneBuilt = true;
    }

    ProbeManager::VolumeSlot* ProbeManager::FindSlot(uint64_t key)
    {
        for (auto& slot : m_Volumes)
            if (slot.key == key)
                return &slot;
        return nullptr;
    }

    const ProbeManager::VolumeSlot* ProbeManager::FindSlot(uint64_t key) const
    {
        for (auto& slot : m_Volumes)
            if (slot.key == key)
                return &slot;
        return nullptr;
    }

    const ProbeManager::VolumeSlot* ProbeManager::FindSlot(int index) const
    {
        if (index < 0 || index >= (int)m_Volumes.size())
            return nullptr;
        return &m_Volumes[index];
    }

    void ProbeManager::RebuildGrid(VolumeSlot& slot)
    {
        const ProbeVolumeProxy& volume = slot.params;

        slot.baked = false;

        glm::ivec3 counts = glm::max(volume.probeCounts, glm::ivec3(1));
        slot.probeCount = (uint32_t)(counts.x * counts.y * counts.z);

        glm::vec3 origin = volume.GridOrigin();
        glm::vec3 spacing = volume.GridSpacing();

        std::vector<GPUProbe> probes(slot.probeCount);
        uint32_t idx = 0;
        for (int z = 0; z < counts.z; z++)
            for (int y = 0; y < counts.y; y++)
                for (int x = 0; x < counts.x; x++)
                    probes[idx++].position = glm::vec4(origin + spacing * glm::vec3((float)x, (float)y, (float)z), 0.0f);

        slot.probeBuffer = StorageBuffer::Create((uint32_t)(probes.size() * sizeof(GPUProbe)));
        slot.probeBuffer->SetData(probes.data(), (uint32_t)(probes.size() * sizeof(GPUProbe)));

        std::vector<glm::vec4> initialState(slot.probeCount, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        slot.probeStateBuffer = StorageBuffer::Create((uint32_t)(initialState.size() * sizeof(glm::vec4)));
        slot.probeStateBuffer->SetData(initialState.data(), (uint32_t)(initialState.size() * sizeof(glm::vec4)));

        slot.tileSize = ComputeTileSize(volume.raysPerProbe);

        slot.atlasProbesPerRow = std::max(1u, (uint32_t)std::ceil(std::sqrt((float)slot.probeCount)));
        uint32_t strideWithBorder = slot.tileSize + 2;
        slot.atlasSize = slot.atlasProbesPerRow * strideWithBorder;

        TextureSpecifications atlasSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RGBA16F, false);

        // Zero-fill. Until a probe's turn comes round its tile is read as bounce source, undefined contents would inject garbage light. A zero distance atlas also gets it excluded from the blend.
        std::vector<float> zeros((size_t)slot.atlasSize * slot.atlasSize * 4, 0.0f);

        TextureSpecifications publishedSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RGBA16F, true);

        slot.rayAtlas = Texture2D::Create(atlasSpec, zeros.data());
        slot.irradianceAtlas = Texture2D::Create(atlasSpec, zeros.data());
        slot.publishedAtlas = Texture2D::Create(publishedSpec, zeros.data());

        TextureSpecifications distAtlasSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RG16F, false);
        TextureSpecifications publishedDistSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RG16F, true);

        slot.rayDistAtlas = Texture2D::Create(distAtlasSpec, zeros.data());
        slot.distanceAtlas = Texture2D::Create(distAtlasSpec, zeros.data());
        slot.publishedDistanceAtlas = Texture2D::Create(publishedDistSpec, zeros.data());

        slot.frameIndex = 0;
    }

    void ProbeManager::MarkBaked(VolumeSlot& slot, const glm::vec3& origin, const glm::vec3& spacing)
    {
        slot.baked = true;
        slot.bakedOrigin = origin;
        slot.bakedSpacing = spacing;
        slot.frameIndex = 0;

        slot.probeBuffer.reset();
        slot.rayAtlas.reset();
        slot.irradianceAtlas.reset();
        slot.rayDistAtlas.reset();
        slot.distanceAtlas.reset();
    }

    void ProbeManager::ApplyBake(VolumeSlot& slot, const ProbeBakeData& baked)
    {
        slot.probeCount = baked.probeCount;
        slot.tileSize = baked.tileSize;
        slot.atlasProbesPerRow = baked.atlasProbesPerRow;
        slot.atlasSize = baked.atlasSize;

        const uint32_t stateBytes = (uint32_t)(baked.probeState.size() * sizeof(glm::vec4));
        slot.probeStateBuffer = StorageBuffer::Create(stateBytes);
        slot.probeStateBuffer->SetData(baked.probeState.data(), stateBytes);

        TextureSpecifications irradianceSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RGBA16F, true);
        TextureSpecifications distanceSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RG16F, true);
        slot.publishedAtlas = Texture2D::Create(irradianceSpec, baked.irradiance.data());
        slot.publishedDistanceAtlas = Texture2D::Create(distanceSpec, baked.distance.data());

        MarkBaked(slot, baked.gridOrigin, baked.gridSpacing);
    }

    void ProbeManager::RebuildGrid(uint64_t key)
    {
        VolumeSlot* slot = FindSlot(key);
        if (!slot)
            return;

        if (m_Bake.active && m_Bake.key == key)
            EndBake(false);

        RebuildGrid(*slot);
    }

    namespace {

        float DistanceToBox(const glm::vec3& point, const glm::vec3& center, const glm::vec3& halfExtent)
        {
            const glm::vec3 outside = glm::max(glm::abs(point - center) - halfExtent, glm::vec3(0.0f));
            return glm::length(outside);
        }

        constexpr float kSlotKeepMargin = 3.0f;
    }

    bool ProbeManager::AddActiveVolume(const ProbeVolumeProxy& proxy, const std::shared_ptr<const ProbeBakeData>& baked)
    {
        const uint64_t key = proxy.volume.key;

        if (VolumeSlot* existing = FindSlot(key))
            return existing->baked;

        if ((int)m_Volumes.size() >= kMaxProbeVolumes)
        {
            DEBUG_ERROR("ProbeManager : cannot activate a ProbeVolume, already at the max of ", kMaxProbeVolumes, " simultaneously active volumes");
            return false;
        }

        VolumeSlot slot;
        slot.key = key;
        slot.params = proxy;
        m_Volumes.push_back(std::move(slot));
        VolumeSlot& added = m_Volumes.back();

        if (baked)
        {
            std::string whyNot;
            if (IsProbeBakeCompatible(*baked, glm::max(proxy.probeCounts, glm::ivec3(1)), ComputeTileSize(proxy.raysPerProbe),
                                      proxy.GridOrigin(), proxy.GridSpacing(), &whyNot))
            {
                ApplyBake(added, *baked);
                DEBUG_INFO("ProbeManager : ProbeVolume loaded from baked probe data (", added.probeCount, " probes, ", added.atlasSize, "x", added.atlasSize, " atlas) - no scene build or tracing needed.");
                return true;
            }

            DEBUG_WARNING("ProbeManager : the baked probe data of this volume is out of date (", whyNot, ") - running it live. Re-bake the volume to refresh it.");
        }

        RebuildGrid(added);
        return false;
    }

    void ProbeManager::RemoveActiveVolume(uint64_t key)
    {
        if (m_Bake.active && m_Bake.key == key)
            EndBake(false);

        auto it = std::find_if(m_Volumes.begin(), m_Volumes.end(),
            [key](const VolumeSlot& slot) { return slot.key == key; });

        if (it != m_Volumes.end())
            m_Volumes.erase(it);
    }

    bool ProbeManager::IsVolumeBaked(uint64_t key) const
    {
        std::lock_guard<std::mutex> lock(m_StatusMutex);
        return std::find(m_Status.bakedKeys.begin(), m_Status.bakedKeys.end(), key) != m_Status.bakedKeys.end();
    }

    void ProbeManager::PublishStatus()
    {
        Status status;
        status.baking = m_Bake.active;
        status.bakingKey = m_Bake.key;
        status.bakeProgress = GetBakeProgressNow();
        status.bakePhase = GetBakePhaseNow();
        status.lastBakeSucceeded = m_LastBakeSucceeded;
        for (const VolumeSlot& slot : m_Volumes)
            if (slot.baked)
                status.bakedKeys.push_back(slot.key);

        std::lock_guard<std::mutex> lock(m_StatusMutex);
        m_Status = std::move(status);
    }

    void ProbeManager::ProvideBakedData(const std::string& path, std::shared_ptr<const ProbeBakeData> data)
    {
        std::lock_guard<std::mutex> lock(m_BakedInboxMutex);
        m_BakedInbox[path] = std::move(data);
    }

    void ProbeManager::Sync(const RenderSnapshot& scene)
    {
        // A volume keeps its slot unless another is this much closer to the camera.
        std::vector<const ProbeVolumeProxy*> candidates;
        for (const ProbeVolumeProxy& proxy : scene.probeVolumes)
            if (proxy.volume.active)
                candidates.push_back(&proxy);

        auto score = [&](const ProbeVolumeProxy& proxy) {
            if (m_Bake.active && m_Bake.key == proxy.volume.key)
                return -1e30f;

            float distance = scene.camera.valid ? DistanceToBox(scene.camera.position, proxy.volume.center, proxy.volume.halfExtent) : 0.0f;
            if (FindSlot(proxy.volume.key))
                distance -= kSlotKeepMargin;
            return distance;
        };

        std::stable_sort(candidates.begin(), candidates.end(),
            [&](const ProbeVolumeProxy* a, const ProbeVolumeProxy* b) { return score(*a) < score(*b); });
        if ((int)candidates.size() > kMaxProbeVolumes)
            candidates.resize(kMaxProbeVolumes);

        auto wanted = [&](uint64_t key) {
            for (const ProbeVolumeProxy* proxy : candidates)
                if (proxy->volume.key == key)
                    return true;
            return false;
        };

        for (size_t i = 0; i < m_Volumes.size();)
        {
            const uint64_t key = m_Volumes[i].key;
            if (wanted(key))
                i++;
            else
            {
                DEBUG_INFO("ProbeManager : a GI probe volume gave up its slot (", kMaxProbeVolumes, " can be shaded at once, the volumes closest to the camera win).");
                RemoveActiveVolume(key);
            }
        }

        bool needsScene = false;

        for (const ProbeVolumeProxy* proxy : candidates)
        {
            VolumeSlot* slot = FindSlot(proxy->volume.key);

            if (!slot)
            {
                std::shared_ptr<const ProbeBakeData> baked;
                if (!proxy->bakedData.empty() && requestBakedData)
                {
                    std::lock_guard<std::mutex> lock(m_BakedInboxMutex);
                    auto answer = m_BakedInbox.find(proxy->bakedData);
                    if (answer == m_BakedInbox.end())
                    {
                        if (m_BakedRequested.insert(proxy->bakedData).second)
                            requestBakedData(proxy->bakedData);
                        continue;
                    }
                    baked = answer->second;
                    m_BakedInbox.erase(answer);
                    m_BakedRequested.erase(proxy->bakedData);
                }

                if (!AddActiveVolume(*proxy, baked))
                    needsScene = true;

                if (!proxy->bakedData.empty() && releaseBakedData)
                    releaseBakedData(proxy->bakedData);
                continue;
            }

            const ProbeVolumeProxy& old = slot->params;
            const bool baking = m_Bake.active && m_Bake.key == slot->key;

            const bool gridChanged = old.volume.halfExtent != proxy->volume.halfExtent || old.probeCounts != proxy->probeCounts ||
                                     old.raysPerProbe != proxy->raysPerProbe || old.enableRelocation != proxy->enableRelocation;

            const bool bakeCleared = slot->baked && !old.bakedData.empty() && proxy->bakedData.empty();

            slot->params = *proxy;

            if (!baking && (gridChanged || bakeCleared))
            {
                const bool wasBaked = slot->baked;
                RebuildGrid(*slot);
                if (wasBaked)
                    needsScene = true;
            }
        }

        if (needsScene)
            RebuildScene(scene);
    }

    bool ProbeManager::BeginBake(uint64_t key, const RenderSnapshot& scene, const Filesystem::Path& file)
    {
        if (!RenderThread::Get().IsGraphicsThread())
            return RenderThread::Get().InvokeResult([&] { return BeginBake(key, scene, file); });

        if (m_Bake.active)
        {
            DEBUG_WARNING("ProbeManager : a probe bake is already running, ignoring the new request.");
            return false;
        }

        VolumeSlot* slot = FindSlot(key);
        if (!slot)
        {
            const ProbeVolumeProxy* proxy = nullptr;
            for (const ProbeVolumeProxy& candidate : scene.probeVolumes)
                if (candidate.volume.key == key)
                    proxy = &candidate;

            if (!proxy || !proxy->volume.active)
            {
                DEBUG_ERROR("ProbeManager : cannot bake a ProbeVolume that isn't active.");
                return false;
            }

            if ((int)m_Volumes.size() >= kMaxProbeVolumes)
            {
                size_t farthest = 0;
                float farthestDistance = -1.0f;
                for (size_t i = 0; i < m_Volumes.size(); i++)
                {
                    const float distance = scene.camera.valid
                        ? DistanceToBox(scene.camera.position, m_Volumes[i].params.volume.center, m_Volumes[i].params.volume.halfExtent) : 0.0f;
                    if (distance > farthestDistance)
                    {
                        farthestDistance = distance;
                        farthest = i;
                    }
                }
                RemoveActiveVolume(m_Volumes[farthest].key);
            }

            AddActiveVolume(*proxy);
            slot = FindSlot(key);
            if (!slot)
                return false;
        }

        RebuildGrid(*slot);

        RebuildScene(scene);

        m_Bake = BakeJob{};
        m_Bake.active = true;
        m_Bake.key = key;
        m_Bake.file = file;
        m_LastBakeSucceeded = false;

        return true;
    }

    void ProbeManager::CancelBake()
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(CancelBake())

        if (m_Bake.active)
            EndBake(false);
    }

    void ProbeManager::EndBake(bool succeeded)
    {
        m_Bake = BakeJob{};
        m_LastBakeSucceeded = succeeded;
    }

    float ProbeManager::GetBakeProgress() const
    {
        std::lock_guard<std::mutex> lock(m_StatusMutex);
        return m_Status.bakeProgress;
    }

    const char* ProbeManager::GetBakePhase() const
    {
        std::lock_guard<std::mutex> lock(m_StatusMutex);
        return m_Status.bakePhase;
    }

    float ProbeManager::GetBakeProgressNow() const
    {
        if (!m_Bake.active)
            return 0.0f;

        constexpr float kSceneShare = 0.2f;

        if (m_SceneBuilding.load(std::memory_order_relaxed))
            return kSceneShare * std::clamp(m_SceneBuildProgress.load(std::memory_order_relaxed), 0.0f, 1.0f);

        constexpr int total = kBakeConvergenceIterations + kBakeAveragingIterations;
        return kSceneShare + (1.0f - kSceneShare) * std::clamp((float)m_Bake.iteration / (float)total, 0.0f, 1.0f);
    }

    const char* ProbeManager::GetBakePhaseNow() const
    {
        if (m_SceneBuilding.load(std::memory_order_relaxed))
            return m_SceneBuildPhase.load(std::memory_order_relaxed);

        return "Tracing probes";
    }

    void ProbeManager::EnsureShaders()
    {
        if (m_TracePipeline && m_ConvolvePipeline && m_BorderFixupPipeline && m_ClassifyPipeline && m_RelocatePipeline && m_TemporalBlendPipeline && m_DistanceTemporalBlendPipeline)
            return;

        IRenderContext* renderer = Core::GetEngine().GetRenderContext();
        Filesystem::Path resRoot = Core::GetEngine().GetFileManager()->GetEngineResRoot();

        if (!m_TracePipeline)
        {
            m_TraceShader = ComputeShader::Create(resRoot / "shaders/compute/probes/probe_trace.comp");
            if (!m_TraceShader)
            {
                DEBUG_ERROR("ProbeManager : failed to load probe_trace.comp");
                return;
            }

            ComputePipelineSpecifications specs;
            specs.shader = m_TraceShader;
            specs.debugName = "ProbeTrace";
            m_TracePipeline = renderer->GetOrAddComputePipeline(specs);
        }

        if (!m_ConvolvePipeline)
        {
            m_ConvolveShader = ComputeShader::Create(resRoot / "shaders/compute/probes/probe_irradiance_convolve.comp");
            if (!m_ConvolveShader)
            {
                DEBUG_ERROR("ProbeManager : failed to load probe_irradiance_convolve.comp");
                return;
            }

            ComputePipelineSpecifications specs;
            specs.shader = m_ConvolveShader;
            specs.debugName = "ProbeIrradianceConvolve";
            m_ConvolvePipeline = renderer->GetOrAddComputePipeline(specs);
        }

        if (!m_BorderFixupPipeline)
        {
            m_BorderFixupShader = ComputeShader::Create(resRoot / "shaders/compute/probes/probe_border_fixup.comp");
            if (!m_BorderFixupShader)
            {
                DEBUG_ERROR("ProbeManager : failed to load probe_border_fixup.comp");
                return;
            }

            ComputePipelineSpecifications specs;
            specs.shader = m_BorderFixupShader;
            specs.debugName = "ProbeBorderFixup";
            m_BorderFixupPipeline = renderer->GetOrAddComputePipeline(specs);
        }

        if (!m_ClassifyPipeline)
        {
            m_ClassifyShader = ComputeShader::Create(resRoot / "shaders/compute/probes/probe_classify.comp");
            if (!m_ClassifyShader)
            {
                DEBUG_ERROR("ProbeManager : failed to load probe_classify.comp");
                return;
            }

            ComputePipelineSpecifications specs;
            specs.shader = m_ClassifyShader;
            specs.debugName = "ProbeClassify";
            m_ClassifyPipeline = renderer->GetOrAddComputePipeline(specs);
        }

        if (!m_RelocatePipeline)
        {
            m_RelocateShader = ComputeShader::Create(resRoot / "shaders/compute/probes/probe_relocate.comp");
            if (!m_RelocateShader)
            {
                DEBUG_ERROR("ProbeManager : failed to load probe_relocate.comp");
                return;
            }

            ComputePipelineSpecifications specs;
            specs.shader = m_RelocateShader;
            specs.debugName = "ProbeRelocate";
            m_RelocatePipeline = renderer->GetOrAddComputePipeline(specs);
        }

        if (!m_TemporalBlendPipeline)
        {
            m_TemporalBlendShader = ComputeShader::Create(resRoot / "shaders/compute/probes/probe_temporal_blend.comp");
            if (!m_TemporalBlendShader)
            {
                DEBUG_ERROR("ProbeManager : failed to load probe_temporal_blend.comp");
                return;
            }

            ComputePipelineSpecifications specs;
            specs.shader = m_TemporalBlendShader;
            specs.debugName = "ProbeTemporalBlend";
            m_TemporalBlendPipeline = renderer->GetOrAddComputePipeline(specs);
        }

        if (!m_DistanceTemporalBlendPipeline)
        {
            m_DistanceTemporalBlendShader = ComputeShader::Create(resRoot / "shaders/compute/probes/probe_distance_temporal_blend.comp");
            if (!m_DistanceTemporalBlendShader)
            {
                DEBUG_ERROR("ProbeManager : failed to load probe_distance_temporal_blend.comp");
                return;
            }

            ComputePipelineSpecifications specs;
            specs.shader = m_DistanceTemporalBlendShader;
            specs.debugName = "ProbeDistanceTemporalBlend";
            m_DistanceTemporalBlendPipeline = renderer->GetOrAddComputePipeline(specs);
        }
    }

    void ProbeManager::UpdateVolume(VolumeSlot& slot, IRenderContext* renderer, const BakeStep* bake)
    {
        if (!slot.rayAtlas || !slot.irradianceAtlas || !slot.publishedAtlas ||
            !slot.rayDistAtlas || !slot.distanceAtlas || !slot.publishedDistanceAtlas || slot.probeCount == 0)
            return;

        const int stride = bake ? 1 : std::clamp(slot.params.probeUpdateStride, 1, kMaxProbeUpdateStride);
        const uint32_t phase = slot.frameIndex % (uint32_t)stride;
        const uint32_t updateCount = phase < slot.probeCount
            ? (slot.probeCount - phase + (uint32_t)stride - 1) / (uint32_t)stride
            : 0;

        // frameIndex must still advance or the phase never moves and the volume freezes.
        if (updateCount == 0)
        {
            slot.frameIndex++;
            return;
        }

        uint32_t raysPerProbe = slot.tileSize * slot.tileSize;
        uint32_t traceGroupsX = (updateCount * raysPerProbe + 63) / 64;
        uint32_t perProbeGroupsX = (updateCount + 63) / 64;

        // Realtime jitter is 1/4 of the ray spacing (more flickers visibility). Bake uses 1/2, and a fully random rotation in the averaging phase so aliasing becomes plain noise.
        const bool averagingPhase = bake && bake->averaging;

        float raySpacingRadians = 3.5449077f / std::max(1.0f, (float)slot.tileSize);
        glm::mat3 rayRotation = averagingPhase
            ? UniformRandomRotation(m_RayRNG)
            : RandomRotation(m_RayRNG, raySpacingRadians * (bake ? 0.5f : 0.25f));

        glm::vec3 gridOrigin = slot.params.GridOrigin();
        glm::vec3 gridSpacing = slot.params.GridSpacing();
        glm::ivec3 probeCounts = glm::max(slot.params.probeCounts, glm::ivec3(1));

        // A never-published probe gets a full overwrite instead of fading in from the zeroed atlas. With stride N the last phase first updates on frame N-1.
        const bool firstUpdateForTheseProbes = slot.frameIndex < (uint32_t)stride;
        // Hysteresis raised to the stride so response time in seconds is unchanged.
        const float hysteresis = firstUpdateForTheseProbes
            ? 0.0f
            : (bake ? bake->hysteresis : std::pow(kBaseTemporalHysteresis, (float)stride));

        {
            m_TracePipeline->Bind();

            m_BVHBuffer->Bind(8);
            m_PosBuffer->Bind(9);
            m_AttribBuffer->Bind(10);
            m_MatBuffer->Bind(11);
            if (m_LightBuffer)
                m_LightBuffer->Bind(12);
            slot.probeBuffer->Bind(13);
            slot.probeStateBuffer->Bind(14);

            slot.rayAtlas->BindImage(0, TextureAccess::ReadWrite);
            slot.rayDistAtlas->BindImage(1, TextureAccess::ReadWrite);

            // Bounce source is the published atlas, one hop per frame. Units 40/41 must match probe_trace.comp.
            slot.publishedAtlas->Bind(40);
            slot.publishedDistanceAtlas->Bind(41);
            if (m_SkyIrradiance)
                m_SkyIrradiance->Bind(46);

            m_TraceShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_TraceShader->SetInt("uTileSize", (int)slot.tileSize);
            m_TraceShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_TraceShader->SetInt("uAtlasSize", (int)slot.atlasSize);
            m_TraceShader->SetInt("uLightCount", (int)m_FlatLights.size());
            m_TraceShader->SetBool("uHasSky", m_SkyIrradiance != nullptr);
            m_TraceShader->SetVec3("uGridOrigin", gridOrigin);
            m_TraceShader->SetVec3("uGridSpacing", gridSpacing);
            m_TraceShader->SetVec3("uProbeCounts", glm::vec3(probeCounts));
            m_TraceShader->SetMat3("uRayRotation", rayRotation);
            m_TraceShader->SetFloat("uIndirectIntensity", std::max(slot.params.indirectIntensity, 0.0f));
            m_TraceShader->SetBool("uUseIndirect", slot.frameIndex > 0);
            m_TraceShader->SetInt("uUpdateStride", stride);
            m_TraceShader->SetInt("uUpdatePhase", (int)phase);
            m_TraceShader->SetInt("uUpdateCount", (int)updateCount);

            renderer->DispatchCompute(m_TracePipeline, traceGroupsX, 1, 1, MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);
        }

        if (!averagingPhase)
        {
            m_ClassifyPipeline->Bind();

            slot.rayAtlas->BindImage(0, TextureAccess::ReadOnly);

            m_ClassifyShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_ClassifyShader->SetInt("uTileSize", (int)slot.tileSize);
            m_ClassifyShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_ClassifyShader->SetInt("uUpdateStride", stride);
            m_ClassifyShader->SetInt("uUpdatePhase", (int)phase);

            renderer->DispatchCompute(m_ClassifyPipeline, perProbeGroupsX, 1, 1, MemoryBarrierBit::ShaderStorage);

            if (slot.params.enableRelocation)
            {
                m_RelocatePipeline->Bind();

                slot.rayAtlas->BindImage(0, TextureAccess::ReadOnly);
                slot.rayDistAtlas->BindImage(1, TextureAccess::ReadOnly);
                slot.probeStateBuffer->Bind(14);

                m_RelocateShader->SetInt("uProbeCount", (int)slot.probeCount);
                m_RelocateShader->SetInt("uTileSize", (int)slot.tileSize);
                m_RelocateShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
                m_RelocateShader->SetMat3("uRayRotation", rayRotation);
                m_RelocateShader->SetVec3("uGridSpacing", gridSpacing);
                m_RelocateShader->SetInt("uUpdateStride", stride);
                m_RelocateShader->SetInt("uUpdatePhase", (int)phase);

                renderer->DispatchCompute(m_RelocatePipeline, perProbeGroupsX, 1, 1, MemoryBarrierBit::ShaderStorage);
            }
        }

        {
            m_ConvolvePipeline->Bind();

            slot.rayAtlas->Bind(42);
            slot.irradianceAtlas->BindImage(0, TextureAccess::WriteOnly);

            slot.rayDistAtlas->Bind(43);
            slot.distanceAtlas->BindImage(2, TextureAccess::WriteOnly);

            m_ConvolveShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_ConvolveShader->SetInt("uTileSize", (int)slot.tileSize);
            m_ConvolveShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_ConvolveShader->SetInt("uUpdateStride", stride);
            m_ConvolveShader->SetInt("uUpdatePhase", (int)phase);
            m_ConvolveShader->SetMat3("uRayRotation", rayRotation);

            renderer->DispatchCompute(m_ConvolvePipeline, updateCount, 1, 1, MemoryBarrierBit::ImageAccess);
        }

        {
            m_BorderFixupPipeline->Bind();

            slot.irradianceAtlas->BindImage(0, TextureAccess::ReadWrite);
            slot.distanceAtlas->BindImage(1, TextureAccess::ReadWrite);

            m_BorderFixupShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_BorderFixupShader->SetInt("uTileSize", (int)slot.tileSize);
            m_BorderFixupShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_BorderFixupShader->SetInt("uUpdateStride", stride);
            m_BorderFixupShader->SetInt("uUpdatePhase", (int)phase);
            m_BorderFixupShader->SetInt("uUpdateCount", (int)updateCount);

            uint32_t totalBorderTexels = updateCount * (slot.tileSize * 4 + 4);
            uint32_t borderGroupsX = (totalBorderTexels + 63) / 64;
            renderer->DispatchCompute(m_BorderFixupPipeline, borderGroupsX, 1, 1, MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);
        }

        {
            uint32_t tileStride = slot.tileSize + 2;
            uint32_t blendGroupsX = (updateCount * tileStride * tileStride + 63) / 64;

            m_TemporalBlendPipeline->Bind();

            slot.irradianceAtlas->Bind(44);
            slot.publishedAtlas->BindImage(0, TextureAccess::ReadWrite);

            m_TemporalBlendShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_TemporalBlendShader->SetInt("uTileSize", (int)slot.tileSize);
            m_TemporalBlendShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_TemporalBlendShader->SetInt("uUpdateStride", stride);
            m_TemporalBlendShader->SetInt("uUpdatePhase", (int)phase);
            m_TemporalBlendShader->SetInt("uUpdateCount", (int)updateCount);
            m_TemporalBlendShader->SetFloat("uHysteresis", hysteresis);

            // TextureFetch is needed, ImageAccess doesn't order sampler reads and lit.frag would see stale atlas data.
            renderer->DispatchCompute(m_TemporalBlendPipeline, blendGroupsX, 1, 1, MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);

            m_DistanceTemporalBlendPipeline->Bind();

            // Unit 45 must match uFresh binding in probe_distance_temporal_blend.comp.
            slot.distanceAtlas->Bind(45);
            slot.publishedDistanceAtlas->BindImage(0, TextureAccess::ReadWrite);

            m_DistanceTemporalBlendShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_DistanceTemporalBlendShader->SetInt("uTileSize", (int)slot.tileSize);
            m_DistanceTemporalBlendShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_DistanceTemporalBlendShader->SetInt("uUpdateStride", stride);
            m_DistanceTemporalBlendShader->SetInt("uUpdatePhase", (int)phase);
            m_DistanceTemporalBlendShader->SetInt("uUpdateCount", (int)updateCount);
            m_DistanceTemporalBlendShader->SetFloat("uHysteresis", hysteresis);

            renderer->DispatchCompute(m_DistanceTemporalBlendPipeline, blendGroupsX, 1, 1, MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);
        }

        slot.frameIndex++;
    }

    void ProbeManager::WaitForSceneBuilds()
    {
        for (std::future<Raytracing::RaytraceScene>& f : m_AbandonedSceneBuilds)
            if (f.valid())
                f.wait();
        m_AbandonedSceneBuilds.clear();

        if (m_PendingSceneBuild.valid())
            m_PendingSceneBuild.wait();
    }

    void ProbeManager::Update()
    {
        m_AbandonedSceneBuilds.erase(
            std::remove_if(m_AbandonedSceneBuilds.begin(), m_AbandonedSceneBuilds.end(),
                [](std::future<Raytracing::RaytraceScene>& f) {
                    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                }),
            m_AbandonedSceneBuilds.end());

        if (m_PendingSceneBuild.valid() &&
            m_PendingSceneBuild.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            try
            {
                Raytracing::RaytraceScene scene = m_PendingSceneBuild.get();

                if (m_PendingSceneBuildGeneration == m_SceneBuildGeneration.load(std::memory_order_relaxed))
                    UploadScene(scene);
            }
            catch (const std::exception& e)
            {
                DEBUG_ERROR("ProbeManager : scene build failed - ", e.what());
            }

            m_SceneBuildProgress.store(1.0f, std::memory_order_relaxed);
            m_SceneBuilding.store(false, std::memory_order_relaxed);
        }

        if (m_Bake.active && !m_SceneBuilding.load(std::memory_order_relaxed) && !m_SceneBuilt)
        {
            DEBUG_ERROR("ProbeManager : probe bake aborted, the world has no geometry to trace against.");
            EndBake(false);
        }

        const bool anyLive = std::any_of(m_Volumes.begin(), m_Volumes.end(),
            [](const VolumeSlot& slot) { return !slot.baked; });

        if (!m_SceneBuilt || !anyLive)
            return;

        EnsureShaders();
        if (!m_TracePipeline || !m_ConvolvePipeline || !m_BorderFixupPipeline || !m_ClassifyPipeline || !m_RelocatePipeline || !m_TemporalBlendPipeline || !m_DistanceTemporalBlendPipeline)
            return;

        IRenderContext* renderer = Core::GetEngine().GetRenderContext();

        const auto& sceneLights = renderer->GetLightManager()->GetLights();
        m_FlatLights.clear();
        m_FlatLights.reserve(sceneLights.size());
        for (auto& light : sceneLights)
            if (light)
                m_FlatLights.push_back(*light);

        if (!m_FlatLights.empty())
        {
            uint32_t neededSize = (uint32_t)(m_FlatLights.size() * sizeof(LightData));
            if (!m_LightBuffer || m_LightBuffer->GetSize() != neededSize)
                m_LightBuffer = StorageBuffer::Create(neededSize);
            m_LightBuffer->SetData(m_FlatLights.data(), neededSize);
        }

        m_SkyIrradiance = nullptr;
        if (const auto& sky = renderer->GetSnapshot().environment.skybox)
            m_SkyIrradiance = sky->GetIrradiance();

        if (m_Bake.active)
            StepBake(renderer);

        for (auto& slot : m_Volumes)
        {
            if (slot.baked)
                continue;

            if (m_Bake.active && slot.key == m_Bake.key)
                continue;

            UpdateVolume(slot, renderer);
        }
    }

    void ProbeManager::StepBake(IRenderContext* renderer)
    {
        VolumeSlot* slot = FindSlot(m_Bake.key);
        if (!slot || slot->baked)
        {
            EndBake(false);
            return;
        }

        constexpr int totalIterations = kBakeConvergenceIterations + kBakeAveragingIterations;

        const uint32_t raysPerIteration = std::max(1u, slot->probeCount * slot->tileSize * slot->tileSize);
        const int iterationsThisFrame = std::clamp((int)(kBakeRaysPerFrame / raysPerIteration), 1, kBakeMaxIterationsPerFrame);

        for (int n = 0; n < iterationsThisFrame && m_Bake.iteration < totalIterations; n++)
        {
            BakeStep step;
            if (m_Bake.iteration < kBakeConvergenceIterations)
            {
                step.hysteresis = kBakeConvergenceHysteresis;
            }
            else
            {
                const float j = (float)(m_Bake.iteration - kBakeConvergenceIterations + 1);
                step.hysteresis = j / (j + 1.0f);
                step.averaging = true;
            }

            UpdateVolume(*slot, renderer, &step);
            m_Bake.iteration++;
        }

        if (m_Bake.iteration >= totalIterations)
            FinishBake(*slot);
    }

    void ProbeManager::FinishBake(VolumeSlot& slot)
    {
        const ProbeVolumeProxy& volume = slot.params;

        Core::GetEngine().GetRenderContext()->GetRendererAPI()->MemoryBarrier(MemoryBarrierBit::TextureUpdate | MemoryBarrierBit::BufferUpdate);

        ProbeBakeData data;
        data.probeCounts = glm::max(volume.probeCounts, glm::ivec3(1));
        data.tileSize = slot.tileSize;
        data.atlasProbesPerRow = slot.atlasProbesPerRow;
        data.atlasSize = slot.atlasSize;
        data.probeCount = slot.probeCount;
        data.gridOrigin = volume.GridOrigin();
        data.gridSpacing = volume.GridSpacing();

        data.irradiance.resize((size_t)slot.atlasSize * slot.atlasSize * 4);
        data.distance.resize((size_t)slot.atlasSize * slot.atlasSize * 2);
        data.probeState.resize(slot.probeCount);

        slot.publishedAtlas->ReadPixels(data.irradiance.data(), data.irradiance.size() * sizeof(uint16_t));
        slot.publishedDistanceAtlas->ReadPixels(data.distance.data(), data.distance.size() * sizeof(uint16_t));
        slot.probeStateBuffer->GetData(data.probeState.data(), (uint32_t)(data.probeState.size() * sizeof(glm::vec4)));

        const Filesystem::Path file = m_Bake.file;

        if (!WriteProbeBakeFile(file, data))
        {
            EndBake(false);
            return;
        }

        MarkBaked(slot, data.gridOrigin, data.gridSpacing);
        EndBake(true);

        if (onBakeFinished)
            onBakeFinished(slot.key, file);
    }

    bool ProbeManager::IsSlotReady(const VolumeSlot& slot, bool sceneBuilt)
    {
        if (!slot.publishedAtlas || !slot.publishedDistanceAtlas)
            return false;

        return slot.baked || (sceneBuilt && slot.frameIndex > 0);
    }

    bool ProbeManager::IsReady() const
    {
        for (auto& slot : m_Volumes)
            if (IsSlotReady(slot, m_SceneBuilt))
                return true;

        return false;
    }

    bool ProbeManager::IsVolumeReady(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return slot && IsSlotReady(*slot, m_SceneBuilt);
    }

    std::shared_ptr<Texture2D> ProbeManager::GetIrradianceAtlas(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return slot ? slot->publishedAtlas : nullptr;
    }

    std::shared_ptr<Texture2D> ProbeManager::GetDistanceAtlas(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return slot ? slot->publishedDistanceAtlas : nullptr;
    }

    std::shared_ptr<StorageBuffer> ProbeManager::GetProbeStateBuffer(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return slot ? slot->probeStateBuffer : nullptr;
    }

    glm::vec3 ProbeManager::GetGridOrigin(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        if (slot && slot->baked)
            return slot->bakedOrigin;
        return slot ? slot->params.GridOrigin() : glm::vec3(0.0f);
    }

    glm::vec3 ProbeManager::GetGridSpacing(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        if (slot && slot->baked)
            return slot->bakedSpacing;
        return slot ? slot->params.GridSpacing() : glm::vec3(1.0f);
    }

    glm::ivec3 ProbeManager::GetProbeCounts(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return slot ? glm::max(slot->params.probeCounts, glm::ivec3(1)) : glm::ivec3(0);
    }

    uint32_t ProbeManager::GetTileSize(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return slot ? slot->tileSize : 0;
    }

    uint32_t ProbeManager::GetAtlasProbesPerRow(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return slot ? slot->atlasProbesPerRow : 0;
    }

    uint32_t ProbeManager::GetAtlasSize(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return slot ? slot->atlasSize : 0;
    }

}
