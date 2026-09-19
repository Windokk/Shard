#include "probe_manager.hpp"

#include "engine/rendering/raytracing/bvh.hpp"
#include "engine/rendering/raytracing/raytrace_scene.hpp"

#include "engine/objects/components/rendering/probe_volume.hpp"

#include "engine/core/engine.hpp"

#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/renderer/renderer_api.hpp"
#include "engine/rendering/buffer/storage_buffer.hpp"
#include "engine/rendering/texture/texture.hpp"
#include "engine/rendering/texture/cubemap/cubemap.hpp"
#include "engine/rendering/texture/cubemap/envmap.hpp"
#include "engine/rendering/shader/compute_shader.hpp"
#include "engine/rendering/pipeline/compute_pipeline.hpp"

#include "engine/levels/level_manager.hpp"
#include "engine/objects/skybox/skybox.hpp"

#include "engine/debugging/logger.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <exception>
#include <random>

#include <glm/gtc/quaternion.hpp>

namespace Shard::Engine::Rendering {

    namespace {

        // Small random rotation (random axis, uniformly-distributed on the sphere via Archimedes'
        // method, but a bounded rather than fully-random angle) - see the m_RayRNG comment in
        // probe_manager.hpp for why Update() applies a fresh one of these to each volume's ray fan every
        // frame. `maxAngleRadians` deliberately keeps this to a fraction of the angle between adjacent
        // rays in the tile (see the call site) : a FULL random rotation (the first version of this)
        // meant two consecutive frames could sample completely unrelated directions for the same nominal
        // texel, and right at an occlusion boundary that swings DDGI_VisibilityWeight's Chebyshev test
        // from ~1 (clear line of sight) to ~0 (blocked) every single frame - no amount of temporal
        // blending hides an input that keeps jumping between two extremes, only one that varies gently
        // around a stable value. A small jitter instead lets the temporal blend do what it's meant to :
        // antialias a stable signal, not average away wholesale resampling.
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

        // Uniformly distributed random rotation over SO(3) (Shoemake's random unit quaternion). Unlike
        // RandomRotation() above this has NO bounded angle : it is only for a bake's averaging phase, where
        // the visibility flicker that motivated the bound averages out over hundreds of iterations, and where
        // a bounded jitter would leave every probe with the same fixed angular quadrature (see the comment on
        // uRayRotation in probe_irradiance_convolve.comp).
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

        // Each ray maps directly to one texel of the probe's octahedral tile (see probe_trace.comp), so
        // raysPerProbe is rounded down to the nearest perfect square, and capped at kMaxRaysPerProbe
        // (probe_irradiance_convolve.comp stages a whole tile in shared memory, which has to be a
        // compile-time size - and past ~256 rays, more rays is the wrong way to spend the budget
        // anyway : noise falls with their square root while cost is linear in them). Shared by the grid
        // (re)build and by the baked-data compatibility check, which must agree on it exactly.
        uint32_t ComputeTileSize(int raysPerProbe)
        {
            int rays = std::clamp(raysPerProbe, 1, kMaxRaysPerProbe);
            return (uint32_t)std::max(1, (int)std::floor(std::sqrt((float)rays)));
        }

        // Atlas texture layout shared by every atlas (see VolumeSlot). `halfFloatPixelData` is set for the
        // two PUBLISHED atlases, the only ones ever saved to or restored from a bake file : it makes
        // their upload data and readback raw halves (see TextureSpecifications::halfFloatPixelData).
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
    ProbeManager::~ProbeManager() = default;

    void ProbeManager::RebuildScene(Levels::Level* level)
    {
        if (!level)
            return;

        // CaptureSnapshot() is the only GL-touching (bindless texture handle resolution) part, and
        // it's cheap (bounded by model/material count) - do it here, synchronously, on the calling
        // (main/GL) thread. The expensive part (triangle flatten + BVH build, bounded by triangle
        // count) runs in the background via BuildFromSnapshot(), which touches no engine/GL state.
        Raytracing::RaytraceSceneSnapshot snapshot = Raytracing::SceneBuilder::CaptureSnapshot(level, /*excludeMasked=*/true);

        // Don't let a still-running previous build block this call - std::async futures block their
        // destructor until the task finishes, so reassigning m_PendingSceneBuild directly would defeat
        // the point of going async. Park it instead; Update() drains finished entries opportunistically.
        if (m_PendingSceneBuild.valid())
            m_AbandonedSceneBuilds.push_back(std::move(m_PendingSceneBuild));

        const uint64_t generation = m_SceneBuildGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
        m_PendingSceneBuildGeneration = generation;

        // Reset the progress state the editor polls, before the worker thread starts touching it.
        m_SceneBuilding.store(true, std::memory_order_relaxed);
        m_SceneBuildProgress.store(0.0f, std::memory_order_relaxed);
        m_SceneBuildPhase.store("Baking scene info", std::memory_order_relaxed);

        m_PendingSceneBuild = std::async(std::launch::async,
            [this, generation, snapshot = std::move(snapshot)]()
            {
                auto onProgress = [this, generation](float fraction, const char* phase)
                {
                    // A newer RebuildScene() has superseded this build - stop writing progress so its
                    // stale percentage doesn't fight the current build's for the notification.
                    if (m_SceneBuildGeneration.load(std::memory_order_relaxed) != generation)
                        return;
                    m_SceneBuildProgress.store(fraction, std::memory_order_relaxed);
                    m_SceneBuildPhase.store(phase, std::memory_order_relaxed);
                };
                return Raytracing::SceneBuilder::BuildFromSnapshot(snapshot, onProgress);
            });

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

    ProbeManager::VolumeSlot* ProbeManager::FindSlot(Objects::Components::ProbeVolume* volume)
    {
        for (auto& slot : m_Volumes)
            if (slot.volume == volume)
                return &slot;
        return nullptr;
    }

    const ProbeManager::VolumeSlot* ProbeManager::FindSlot(const Objects::Components::ProbeVolume* volume) const
    {
        for (auto& slot : m_Volumes)
            if (slot.volume == volume)
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
        Objects::Components::ProbeVolume* volume = slot.volume;
        if (!volume)
            return;

        // A full live rebuild always leaves the slot live : whatever baked data it held is for the
        // old grid, and the scratch atlases a live slot needs are (re)created below.
        slot.baked = false;

        glm::ivec3 counts = glm::max(volume->probeCounts, glm::ivec3(1));
        slot.probeCount = (uint32_t)(counts.x * counts.y * counts.z);

        glm::vec3 origin = volume->GetGridOrigin();
        glm::vec3 spacing = volume->GetGridSpacing();

        std::vector<GPUProbe> probes(slot.probeCount);
        uint32_t idx = 0;
        for (int z = 0; z < counts.z; z++)
            for (int y = 0; y < counts.y; y++)
                for (int x = 0; x < counts.x; x++)
                    probes[idx++].position = glm::vec4(origin + spacing * glm::vec3((float)x, (float)y, (float)z), 0.0f);

        slot.probeBuffer = StorageBuffer::Create((uint32_t)(probes.size() * sizeof(GPUProbe)));
        slot.probeBuffer->SetData(probes.data(), (uint32_t)(probes.size() * sizeof(GPUProbe)));

        // Per-probe state (see probeStateBuffer on VolumeSlot) : .w = 1 (all-active until the first
        // classify dispatch runs - can't start zeroed/uninitialized or every probe leaks before then),
        // .xyz = 0 (zero relocation offset - probe_relocate.comp integrates on top of this). Reallocating
        // here also resets accumulated offsets, which is what should happen when the grid resolution/
        // bounds change or ProbeVolume::enableRelocation is toggled.
        std::vector<glm::vec4> initialState(slot.probeCount, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        slot.probeStateBuffer = StorageBuffer::Create((uint32_t)(initialState.size() * sizeof(glm::vec4)));
        slot.probeStateBuffer->SetData(initialState.data(), (uint32_t)(initialState.size() * sizeof(glm::vec4)));

        // See ComputeTileSize() for how raysPerProbe becomes a tile size.
        slot.tileSize = ComputeTileSize(volume->raysPerProbe);

        slot.atlasProbesPerRow = std::max(1u, (uint32_t)std::ceil(std::sqrt((float)slot.probeCount)));
        uint32_t strideWithBorder = slot.tileSize + 2; // +1 texel of border on each side, see probe_border_fixup.comp
        slot.atlasSize = slot.atlasProbesPerRow * strideWithBorder;

        TextureSpecifications atlasSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RGBA16F, false);

        // Zero-fill rather than leaving the (immutable-storage) contents undefined. With round-robin
        // probe updates a probe's tile isn't written until its own turn comes round, up to
        // probeUpdateStride frames in, and until then lit.frag samples the published atlas and
        // probe_trace.comp reads it back as its bounce source. Undefined contents there is garbage
        // light injected into the scene AND into the feedback loop; zeros are the correct "no light
        // measured here yet" value, and a zeroed distance atlas additionally makes DDGI_VisibilityWeight
        // return 0 for such a probe, so it is excluded from the blend entirely until it has real data.
        std::vector<float> zeros((size_t)slot.atlasSize * slot.atlasSize * 4, 0.0f);

        // The published pair is the one that gets saved/restored by a bake, hence the raw-half data
        // path. All-zero bytes are a valid all-zero half, and `zeros` is over-sized for half data
        // (it is laid out for the 32-bit float path the other atlases use), which is harmless.
        TextureSpecifications publishedSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RGBA16F, true);

        slot.rayAtlas = Texture2D::Create(atlasSpec, zeros.data());
        slot.irradianceAtlas = Texture2D::Create(atlasSpec, zeros.data());
        slot.publishedAtlas = Texture2D::Create(publishedSpec, zeros.data());

        // Distance atlas trio - same size/layout, RG16F (mean, mean^2) instead of RGBA16F radiance.
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

        // Live-only resources : see VolumeSlot::baked.
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

        // Straight from the file's bytes - the atlases were saved in exactly the format they are
        // uploaded in (see TextureSpecifications::halfFloatPixelData), so this is one glTexSubImage2D
        // each, no conversion.
        TextureSpecifications irradianceSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RGBA16F, true);
        TextureSpecifications distanceSpec = MakeAtlasSpec(slot.atlasSize, TextureInternalFormat::RG16F, true);
        slot.publishedAtlas = Texture2D::Create(irradianceSpec, baked.irradiance.data());
        slot.publishedDistanceAtlas = Texture2D::Create(distanceSpec, baked.distance.data());

        MarkBaked(slot, baked.gridOrigin, baked.gridSpacing);
    }

    void ProbeManager::RebuildGrid(Objects::Components::ProbeVolume* volume)
    {
        VolumeSlot* slot = FindSlot(volume);
        if (!slot)
            return;

        // The bake in flight was tracing the grid that is about to be replaced.
        if (m_Bake.active && m_Bake.volume == volume)
            EndBake(false);

        RebuildGrid(*slot);
    }

    bool ProbeManager::AddActiveVolume(Objects::Components::ProbeVolume* volume, const std::shared_ptr<const ProbeBakeData>& baked)
    {
        if (!volume)
            return false;

        if (VolumeSlot* existing = FindSlot(volume))
            return existing->baked;

        if ((int)m_Volumes.size() >= kMaxProbeVolumes)
        {
            DEBUG_ERROR("ProbeManager : cannot activate ProbeVolume, already at the max of ", kMaxProbeVolumes, " simultaneously active volumes");
            return false;
        }

        VolumeSlot slot;
        slot.volume = volume;
        m_Volumes.push_back(std::move(slot));
        VolumeSlot& added = m_Volumes.back();

        if (baked)
        {
            std::string whyNot;
            if (IsProbeBakeCompatible(*baked, glm::max(volume->probeCounts, glm::ivec3(1)), ComputeTileSize(volume->raysPerProbe),
                                      volume->GetGridOrigin(), volume->GetGridSpacing(), &whyNot))
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

    void ProbeManager::RemoveActiveVolume(Objects::Components::ProbeVolume* volume)
    {
        if (m_Bake.active && m_Bake.volume == volume)
            EndBake(false);

        auto it = std::find_if(m_Volumes.begin(), m_Volumes.end(),
            [volume](const VolumeSlot& slot) { return slot.volume == volume; });

        if (it != m_Volumes.end())
            m_Volumes.erase(it);
    }

    bool ProbeManager::IsVolumeBaked(const Objects::Components::ProbeVolume* volume) const
    {
        const VolumeSlot* slot = FindSlot(volume);
        return slot && slot->baked;
    }

    bool ProbeManager::BeginBake(Objects::Components::ProbeVolume* volume, Levels::Level* level, const Filesystem::Path& file)
    {
        if (!volume || !level)
            return false;

        if (m_Bake.active)
        {
            DEBUG_WARNING("ProbeManager : a probe bake is already running, ignoring the new request.");
            return false;
        }

        VolumeSlot* slot = FindSlot(volume);
        if (!slot)
        {
            DEBUG_ERROR("ProbeManager : cannot bake a ProbeVolume that isn't active.");
            return false;
        }

        // Start from a clean grid : zeroed atlases, all-active probes with no relocation offset, frame 0.
        // A bake must not depend on whatever the live volume (or a previous bake) had accumulated.
        RebuildGrid(*slot);

        // Always rebuild the scene, even if one is already built - it may predate geometry edits, and a
        // bake is precisely the moment the result has to reflect the level as it is now.
        RebuildScene(level);

        m_Bake = BakeJob{};
        m_Bake.active = true;
        m_Bake.volume = volume;
        m_Bake.file = file;
        m_LastBakeSucceeded = false;

        return true;
    }

    void ProbeManager::CancelBake()
    {
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
        if (!m_Bake.active)
            return 0.0f;

        constexpr float kSceneShare = 0.2f;

        if (m_SceneBuilding.load(std::memory_order_relaxed))
            return kSceneShare * std::clamp(m_SceneBuildProgress.load(std::memory_order_relaxed), 0.0f, 1.0f);

        constexpr int total = kBakeConvergenceIterations + kBakeAveragingIterations;
        return kSceneShare + (1.0f - kSceneShare) * std::clamp((float)m_Bake.iteration / (float)total, 0.0f, 1.0f);
    }

    const char* ProbeManager::GetBakePhase() const
    {
        if (m_SceneBuilding.load(std::memory_order_relaxed))
            return m_SceneBuildPhase.load(std::memory_order_relaxed);

        return "Tracing probes";
    }

    void ProbeManager::EnsureShaders()
    {
        if (m_TracePipeline && m_ConvolvePipeline && m_BorderFixupPipeline && m_ClassifyPipeline && m_RelocatePipeline && m_TemporalBlendPipeline && m_DistanceTemporalBlendPipeline)
            return;

        Renderer* renderer = Core::GetEngine().GetRenderer();
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

    void ProbeManager::UpdateVolume(VolumeSlot& slot, Renderer* renderer, const BakeStep* bake)
    {
        if (!slot.volume || !slot.rayAtlas || !slot.irradianceAtlas || !slot.publishedAtlas ||
            !slot.rayDistAtlas || !slot.distanceAtlas || !slot.publishedDistanceAtlas || slot.probeCount == 0)
            return;

        // Round-robin probe update (see the class comment) : this frame only touches the probes whose
        // index is congruent to `phase` modulo `stride`. updateCount is how many that is - every
        // dispatch below is sized from it rather than from probeCount, which is the whole point (a
        // stride of N costs 1/N the rays, 1/N the convolve work, 1/N the blend work). A bake always
        // updates every probe on every iteration : its cost is the iteration count, not the frame time.
        const int stride = bake ? 1 : std::clamp(slot.volume->probeUpdateStride, 1, kMaxProbeUpdateStride);
        const uint32_t phase = slot.frameIndex % (uint32_t)stride;
        const uint32_t updateCount = phase < slot.probeCount
            ? (slot.probeCount - phase + (uint32_t)stride - 1) / (uint32_t)stride
            : 0;

        // Only reachable when a volume has fewer probes than its stride, so this frame's phase lands
        // past the last probe. frameIndex still has to advance or the phase would never move on and the
        // volume would freeze for good.
        if (updateCount == 0)
        {
            slot.frameIndex++;
            return;
        }

        // Ray atlas and irradiance atlas share the same tile size, so this doubles as both the ray count
        // per probe (trace) and the irradiance texel count per probe (convolve).
        uint32_t raysPerProbe = slot.tileSize * slot.tileSize;
        uint32_t traceGroupsX = (updateCount * raysPerProbe + 63) / 64;
        uint32_t perProbeGroupsX = (updateCount + 63) / 64; // classify / relocate : one thread per probe

        // Real-time keeps the jitter to a quarter of the angle between adjacent rays (see
        // RandomRotation() - anything wilder makes the visibility test flicker frame to frame). A bake
        // averages hundreds of iterations, so there it can afford, and benefits from, a jitter of half
        // that spacing : every texel's ray then lands uniformly anywhere inside its own cell of the
        // octahedral map, i.e. the average converges to the cell's integral, not to a handful of
        // fixed sample points.
        //
        // The bake's AVERAGING phase goes further and uses a fully random rotation per iteration. With any
        // bounded jitter the average converges to a fixed set of cell integrals, weighted at the cell
        // centres - a deterministic 10x10 angular quadrature whose aliasing error (a small bright emitter
        // is hit by 0 or 5 rays depending on where it falls in each probe's cell grid, worth +-12% per
        // probe at 100 rays and up to ~35% at the worst) is different for every probe and does NOT average
        // away however many iterations run - visible as blotches and crosses in the lit result. A random
        // rotation per iteration makes each iteration an independent unbiased quadrature instead, so that
        // error becomes ordinary noise that the running mean shrinks by 1/sqrt(iterations). Classification
        // and relocation are frozen for those iterations (they threshold a per-iteration ray sample and
        // would otherwise flip borderline probes at random).
        const bool averagingPhase = bake && bake->averaging;

        float raySpacingRadians = 3.5449077f / std::max(1.0f, (float)slot.tileSize);
        glm::mat3 rayRotation = averagingPhase
            ? UniformRandomRotation(m_RayRNG)
            : RandomRotation(m_RayRNG, raySpacingRadians * (bake ? 0.5f : 0.25f));

        glm::vec3 gridOrigin = slot.volume->GetGridOrigin();
        glm::vec3 gridSpacing = slot.volume->GetGridSpacing();
        glm::ivec3 probeCounts = glm::max(slot.volume->probeCounts, glm::ivec3(1));

        // A probe that has never been published yet must not fade in from the zeroed atlas through the
        // hysteresis blend - it gets a full overwrite on its own first update instead. With a stride of
        // N, the last phase's probes reach their first update on frame N-1, hence the comparison against
        // the stride rather than against 0.
        const bool firstUpdateForTheseProbes = slot.frameIndex < (uint32_t)stride;
        // ^ see kBaseTemporalHysteresis : raised to the power of the stride so the volume's response
        // time in seconds doesn't change when probes are spread over more frames (a probe updated every
        // N frames takes one blend step every N frames, so each step has to move N times as far).
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

            // The bounce source is the PUBLISHED atlas - i.e. the result of every previous frame, which
            // already carries every bounce those frames had resolved. This one read is what replaced the
            // old in-frame maxBounces loop (see the class comment) : same feedback, one trace pass.
            // Units 40/41 must match probe_trace.comp's uPrevIrradianceAtlas/uPrevDistanceAtlas.
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
            m_TraceShader->SetFloat("uIndirectIntensity", std::max(slot.volume->indirectIntensity, 0.0f));
            // Nothing to read back on the very first frame - the published atlas is still all zeros.
            m_TraceShader->SetBool("uUseIndirect", slot.frameIndex > 0);
            m_TraceShader->SetInt("uUpdateStride", stride);
            m_TraceShader->SetInt("uUpdatePhase", (int)phase);
            m_TraceShader->SetInt("uUpdateCount", (int)updateCount);

            renderer->DispatchCompute(m_TracePipeline, traceGroupsX, 1, 1, MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);
        }

        // Skipped during a bake's averaging phase - see averagingPhase above.
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

            if (slot.volume->enableRelocation)
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

                // ShaderStorage : same reasoning as the classify dispatch above - the SSBO write is
                // read by next frame's trace dispatch and by lit.frag's forward pass.
                renderer->DispatchCompute(m_RelocatePipeline, perProbeGroupsX, 1, 1, MemoryBarrierBit::ShaderStorage);
            }
        }

        {
            // Convolve : turn the raw per-ray radiance into an actual cosine-weighted irradiance map -
            // see probe_irradiance_convolve.comp for why this step can't be skipped. ONE WORKGROUP PER
            // UPDATED PROBE (not one thread per texel) : that shader stages a whole tile in shared
            // memory, so the workgroup, not the thread, is the unit of work here.
            m_ConvolvePipeline->Bind();

            // Unit numbers here (42/43) must match probe_irradiance_convolve.comp's uRayAtlas/
            // uRayDistAtlas layout(binding=...) - see that shader's comment for why they're not 1/3.
            slot.rayAtlas->Bind(42);
            slot.irradianceAtlas->BindImage(0, TextureAccess::WriteOnly);

            slot.rayDistAtlas->Bind(43);
            slot.distanceAtlas->BindImage(2, TextureAccess::WriteOnly);

            m_ConvolveShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_ConvolveShader->SetInt("uTileSize", (int)slot.tileSize);
            m_ConvolveShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_ConvolveShader->SetInt("uUpdateStride", stride);
            m_ConvolveShader->SetInt("uUpdatePhase", (int)phase);
            // The rotation this iteration's rays were actually traced with - the convolve weights each ray
            // by its real direction.
            m_ConvolveShader->SetMat3("uRayRotation", rayRotation);

            renderer->DispatchCompute(m_ConvolvePipeline, updateCount, 1, 1, MemoryBarrierBit::ImageAccess);
        }

        {
            // Border-fixup : duplicate tile-edge texels so bilinear sampling doesn't bleed across probes.
            m_BorderFixupPipeline->Bind();

            slot.irradianceAtlas->BindImage(0, TextureAccess::ReadWrite);
            slot.distanceAtlas->BindImage(1, TextureAccess::ReadWrite);

            m_BorderFixupShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_BorderFixupShader->SetInt("uTileSize", (int)slot.tileSize);
            m_BorderFixupShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_BorderFixupShader->SetInt("uUpdateStride", stride);
            m_BorderFixupShader->SetInt("uUpdatePhase", (int)phase);
            m_BorderFixupShader->SetInt("uUpdateCount", (int)updateCount);

            // One thread per border texel : 4 * tileSize edge texels + 4 corner texels around each tile.
            uint32_t totalBorderTexels = updateCount * (slot.tileSize * 4 + 4);
            uint32_t borderGroupsX = (totalBorderTexels + 63) / 64;
            renderer->DispatchCompute(m_BorderFixupPipeline, borderGroupsX, 1, 1, MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);
        }

        {
            // Temporal blend : smooth this frame's raw, noisy result into the persistent published
            // atlas - the one lit.frag samples AND next frame's trace reads back as its bounce source.
            // See probe_temporal_blend.comp and the publishedAtlas comment on VolumeSlot.
            //
            // One thread per texel of an UPDATED probe's tile, border included - (tileSize + 2)^2 each,
            // which is exactly the atlas region this frame rewrote.
            uint32_t tileStride = slot.tileSize + 2;
            uint32_t blendGroupsX = (updateCount * tileStride * tileStride + 63) / 64;

            m_TemporalBlendPipeline->Bind();

            // Unit number here (44) must match probe_temporal_blend.comp's uFresh layout(binding=...) -
            // see that shader's comment for why it's not 1.
            slot.irradianceAtlas->Bind(44);
            slot.publishedAtlas->BindImage(0, TextureAccess::ReadWrite);

            m_TemporalBlendShader->SetInt("uProbeCount", (int)slot.probeCount);
            m_TemporalBlendShader->SetInt("uTileSize", (int)slot.tileSize);
            m_TemporalBlendShader->SetInt("uAtlasProbesPerRow", (int)slot.atlasProbesPerRow);
            m_TemporalBlendShader->SetInt("uUpdateStride", stride);
            m_TemporalBlendShader->SetInt("uUpdatePhase", (int)phase);
            m_TemporalBlendShader->SetInt("uUpdateCount", (int)updateCount);
            m_TemporalBlendShader->SetFloat("uHysteresis", hysteresis);

            // TextureFetch (not just ImageAccess) : this is the last writer before the forward pass
            // samples the atlas through `sampler2D ddgi_irradianceAtlas*` in lit.frag -
            // GL_SHADER_IMAGE_ACCESS_BARRIER_BIT only orders subsequent imageLoad/imageStore, not
            // sampler reads, so without it the driver is free to let lit.frag see stale/incoherent atlas
            // data every frame (probe volumes silently doing nothing).
            renderer->DispatchCompute(m_TemporalBlendPipeline, blendGroupsX, 1, 1, MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);

            // Distance atlas equivalent - separate pipeline (see m_DistanceTemporalBlendPipeline in the
            // header for why it can't reuse m_TemporalBlendPipeline), same group count since both
            // atlases share the same tile layout.
            m_DistanceTemporalBlendPipeline->Bind();

            // Unit number here (45) must match probe_distance_temporal_blend.comp's uFresh
            // layout(binding=...) - see that shader's comment for why it's not 1.
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

        // A bake is waiting on the scene build. If that build finished without leaving a scene (the level
        // has no geometry, or the build threw), nothing will ever unblock it - fail it instead of letting
        // the editor's progress notification sit there forever.
        if (m_Bake.active && !m_SceneBuilding.load(std::memory_order_relaxed) && !m_SceneBuilt)
        {
            DEBUG_ERROR("ProbeManager : probe bake aborted, the level has no geometry to trace against.");
            EndBake(false);
        }

        // Baked volumes do no per-frame work at all (and need no scene), so a level made only of those
        // never gets past here.
        const bool anyLive = std::any_of(m_Volumes.begin(), m_Volumes.end(),
            [](const VolumeSlot& slot) { return !slot.baked; });

        if (!m_SceneBuilt || !anyLive)
            return;

        EnsureShaders();
        if (!m_TracePipeline || !m_ConvolvePipeline || !m_BorderFixupPipeline || !m_ClassifyPipeline || !m_RelocatePipeline || !m_TemporalBlendPipeline || !m_DistanceTemporalBlendPipeline)
            return;

        Renderer* renderer = Core::GetEngine().GetRenderer();

        // Snapshot the live LightManager's lights every frame : cheap, and keeps this decoupled from the
        // live renderer buffer's lifecycle (same pattern as Raytracer::BuildScene()). Shared across every
        // volume's trace dispatch below.
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

        // Sky radiance for rays that escape the scene - see m_SkyIrradiance. Re-read every frame (it's
        // two pointer hops) instead of cached, so a level swap or a skybox change can't leave this
        // pointing at a freed cubemap.
        m_SkyIrradiance = nullptr;
        Levels::LevelManager* levelManager = Core::GetEngine().GetLevelManager();
        if (levelManager && levelManager->GetLoadedLevelCount() > 0)
        {
            Levels::Level* level = levelManager->GetLevelAt(0);
            if (level && level->skybox && level->skybox->GetEnvMap())
                m_SkyIrradiance = level->skybox->GetEnvMap()->GetIrradiance();
        }

        if (m_Bake.active)
            StepBake(renderer);

        for (auto& slot : m_Volumes)
        {
            if (slot.baked)
                continue;

            // The volume being baked is driven by StepBake() above, on the bake's own schedule.
            if (m_Bake.active && slot.volume == m_Bake.volume)
                continue;

            UpdateVolume(slot, renderer);
        }
    }

    void ProbeManager::StepBake(Renderer* renderer)
    {
        VolumeSlot* slot = FindSlot(m_Bake.volume);
        if (!slot || slot->baked)
        {
            EndBake(false);
            return;
        }

        constexpr int totalIterations = kBakeConvergenceIterations + kBakeAveragingIterations;

        // As many iterations as fit in this frame's ray budget - see kBakeRaysPerFrame.
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
                // j-th averaging iteration : blending with weight 1/(j+1) makes the published atlas the
                // running mean of everything since the phase began (the converged state counts as the
                // first sample).
                const float j = (float)(m_Bake.iteration - kBakeConvergenceIterations + 1);
                step.hysteresis = j / (j + 1.0f);
                step.averaging = true;
            }

            // The very first iteration overrides this to 0 inside UpdateVolume() - a full overwrite of
            // the zeroed atlas rather than a fade-in from black.
            UpdateVolume(*slot, renderer, &step);
            m_Bake.iteration++;
        }

        if (m_Bake.iteration >= totalIterations)
            FinishBake(*slot);
    }

    void ProbeManager::FinishBake(VolumeSlot& slot)
    {
        Objects::Components::ProbeVolume* volume = slot.volume;

        // The last blend dispatch wrote the published atlases through image stores, and the state buffer
        // through an SSBO : both need their host-readback barrier before the blocking reads below.
        Core::GetEngine().GetRenderer()->GetRendererAPI()->MemoryBarrier(MemoryBarrierBit::TextureUpdate | MemoryBarrierBit::BufferUpdate);

        ProbeBakeData data;
        data.probeCounts = glm::max(volume->probeCounts, glm::ivec3(1));
        data.tileSize = slot.tileSize;
        data.atlasProbesPerRow = slot.atlasProbesPerRow;
        data.atlasSize = slot.atlasSize;
        data.probeCount = slot.probeCount;
        data.gridOrigin = volume->GetGridOrigin();
        data.gridSpacing = volume->GetGridSpacing();

        data.irradiance.resize((size_t)slot.atlasSize * slot.atlasSize * 4);
        data.distance.resize((size_t)slot.atlasSize * slot.atlasSize * 2);
        data.probeState.resize(slot.probeCount);

        // The published atlases were created with halfFloatPixelData, so ReadPixels() hands back their
        // raw halves - exactly what goes into the file.
        slot.publishedAtlas->ReadPixels(data.irradiance.data(), data.irradiance.size() * sizeof(uint16_t));
        slot.publishedDistanceAtlas->ReadPixels(data.distance.data(), data.distance.size() * sizeof(uint16_t));
        slot.probeStateBuffer->GetData(data.probeState.data(), (uint32_t)(data.probeState.size() * sizeof(glm::vec4)));

        const Filesystem::Path file = m_Bake.file;

        if (!WriteProbeBakeFile(file, data))
        {
            // Leaves the volume live, still holding its converged atlases - nothing is lost but the file.
            EndBake(false);
            return;
        }

        // Keep using the atlases that were just baked, in place : they ARE the baked data, so there is
        // nothing to reload.
        MarkBaked(slot, data.gridOrigin, data.gridSpacing);
        EndBake(true);

        volume->OnBakeFinished(file);
    }

    bool ProbeManager::IsSlotReady(const VolumeSlot& slot, bool sceneBuilt)
    {
        if (!slot.publishedAtlas || !slot.publishedDistanceAtlas)
            return false;

        // A baked slot's atlases are complete the moment they are uploaded, and it never depends on the
        // scene. A live one needs the scene built and at least one trace pass dispatched.
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
        return (slot && slot->volume) ? slot->volume->GetGridOrigin() : glm::vec3(0.0f);
    }

    glm::vec3 ProbeManager::GetGridSpacing(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        if (slot && slot->baked)
            return slot->bakedSpacing;
        return (slot && slot->volume) ? slot->volume->GetGridSpacing() : glm::vec3(1.0f);
    }

    glm::ivec3 ProbeManager::GetProbeCounts(int index) const
    {
        const VolumeSlot* slot = FindSlot(index);
        return (slot && slot->volume) ? glm::max(slot->volume->probeCounts, glm::ivec3(1)) : glm::ivec3(0);
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
