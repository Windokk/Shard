#pragma once

#include <atomic>
#include <memory>
#include <vector>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/features/lighting/light_manager.hpp"
#include "engine/renderer/features/lighting/probe_bake.hpp"
#include "engine/renderer/features/raytracing/raytrace_scene.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class StorageBuffer;
    class Texture2D;
    class Cubemap;
    class ComputeShader;
    class ComputePipeline;

    struct GPUProbe
    {
        glm::vec4 position = glm::vec4(0.0f);
    };

    // Fixed cap so lit.frag can declare fixed-size arrays. Must match MAX_PROBE_VOLUMES in lit.frag.
    constexpr int kMaxProbeVolumes = 2;

    // Must match MAX_TILE_TEXELS in probe_irradiance_convolve.comp (16x16 octahedral tile in shared memory).
    constexpr int kMaxRaysPerProbe = 256;

    constexpr int kMaxProbeUpdateStride = 8;

    // Base hysteresis. 0.97 settles in ~33 frames with a ~12% noise floor. Kept low because bounces feed back across frames and lag multiplies per hop.
    constexpr float kBaseTemporalHysteresis = 0.97f;

    constexpr int kBakeConvergenceIterations = 256;
    constexpr int kBakeAveragingIterations = 256;
    constexpr float kBakeConvergenceHysteresis = 0.95f;

    constexpr uint32_t kBakeRaysPerFrame = 1u << 20;
    constexpr int kBakeMaxIterationsPerFrame = 16;

    // DDGI-like diffuse GI.
    // Per probe: rays against a persistent BVH, octahedral irradiance atlas plus a distance atlas for the Chebyshev visibility test (DDGI_VisibilityWeight). Bounces feed back across frames via the published atlas. Volumes can be baked.
    class ProbeManager
    {
        public:
            ProbeManager();
            ~ProbeManager();

            void RebuildScene(const RenderSnapshot& scene);

            void Sync(const RenderSnapshot& scene);

            bool IsVolumeBaked(uint64_t key) const;

            bool BeginBake(uint64_t key, const RenderSnapshot& scene, const Filesystem::Path& file);

            std::function<void(uint64_t, const Filesystem::Path&)> onBakeFinished;

            std::function<void(const std::string&)> requestBakedData;
            void ProvideBakedData(const std::string& path, std::shared_ptr<const ProbeBakeData> data);

            std::function<void(const std::string&)> releaseBakedData;

            void CancelBake();

            bool IsBaking() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.baking; }
            uint64_t GetBakingKey() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.baking ? m_Status.bakingKey : 0; }

            float GetBakeProgress() const;
            const char* GetBakePhase() const;

            bool DidLastBakeSucceed() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.lastBakeSucceeded; }

            void Update();

            void PublishStatus();

            void WaitForSceneBuilds();

            bool HasActiveVolume() const { return !m_Volumes.empty(); }
            int GetActiveVolumeCount() const { return (int)m_Volumes.size(); }

            bool IsSceneBuilding() const { return m_SceneBuilding.load(std::memory_order_relaxed); }
            float GetSceneBuildProgress() const { return m_SceneBuildProgress.load(std::memory_order_relaxed); }
            const char* GetSceneBuildPhase() const { return m_SceneBuildPhase.load(std::memory_order_relaxed); }

            bool IsReady() const;

            bool IsVolumeReady(int index) const;
            std::shared_ptr<Texture2D> GetIrradianceAtlas(int index) const;
            std::shared_ptr<Texture2D> GetDistanceAtlas(int index) const;
            std::shared_ptr<StorageBuffer> GetProbeStateBuffer(int index) const;
            glm::vec3 GetGridOrigin(int index) const;
            glm::vec3 GetGridSpacing(int index) const;
            glm::ivec3 GetProbeCounts(int index) const;
            uint32_t GetTileSize(int index) const;
            uint32_t GetAtlasProbesPerRow(int index) const;
            uint32_t GetAtlasSize(int index) const;

        private:
            struct VolumeSlot
            {
                uint64_t key = 0;
                ProbeVolumeProxy params;

                std::shared_ptr<StorageBuffer> probeBuffer;
                uint32_t probeCount = 0;

                // Per-probe vec4 at SSBO 14. w: classification (0 inactive, 1 active). xyz: relocation offset. Initialised to (0,0,0,1).
                std::shared_ptr<StorageBuffer> probeStateBuffer;

                // rayAtlas is scratch. irradianceAtlas is this frame's raw result. publishedAtlas is blended over time and is what lit.frag samples and the next trace reads as bounce.
                std::shared_ptr<Texture2D> rayAtlas;
                std::shared_ptr<Texture2D> irradianceAtlas;
                std::shared_ptr<Texture2D> publishedAtlas;

                std::shared_ptr<Texture2D> rayDistAtlas;
                std::shared_ptr<Texture2D> distanceAtlas;
                std::shared_ptr<Texture2D> publishedDistanceAtlas;

                uint32_t tileSize = 0;
                uint32_t atlasProbesPerRow = 0;
                uint32_t atlasSize = 0;

                uint32_t frameIndex = 0;

                bool baked = false;

                glm::vec3 bakedOrigin = glm::vec3(0.0f);
                glm::vec3 bakedSpacing = glm::vec3(1.0f);
            };

            struct BakeStep
            {
                float hysteresis = 0.0f;
                bool averaging = false;
            };

            struct BakeJob
            {
                bool active = false;
                uint64_t key = 0;
                Filesystem::Path file;
                int iteration = 0;
            };

            void EnsureShaders();
            void UploadScene(const Raytracing::RaytraceScene& scene);

            VolumeSlot* FindSlot(uint64_t key);
            const VolumeSlot* FindSlot(uint64_t key) const;

            bool AddActiveVolume(const ProbeVolumeProxy& proxy, const std::shared_ptr<const ProbeBakeData>& baked = nullptr);
            void RemoveActiveVolume(uint64_t key);
            void RebuildGrid(uint64_t key);
            const VolumeSlot* FindSlot(int index) const;

            void RebuildGrid(VolumeSlot& slot);

            void ApplyBake(VolumeSlot& slot, const ProbeBakeData& baked);

            void MarkBaked(VolumeSlot& slot, const glm::vec3& origin, const glm::vec3& spacing);

            static bool IsSlotReady(const VolumeSlot& slot, bool sceneBuilt);

            void UpdateVolume(VolumeSlot& slot, IRenderContext* renderer, const BakeStep* bake = nullptr);

            void StepBake(IRenderContext* renderer);
            void FinishBake(VolumeSlot& slot);
            void EndBake(bool succeeded);

            std::vector<VolumeSlot> m_Volumes;

            BakeJob m_Bake;
            bool m_LastBakeSucceeded = false;

            struct Status {
                bool baking = false;
                uint64_t bakingKey = 0;
                float bakeProgress = 0.0f;
                const char* bakePhase = "";
                bool lastBakeSucceeded = false;
                std::vector<uint64_t> bakedKeys;
            };
            Status m_Status;
            mutable std::mutex m_StatusMutex;
            float GetBakeProgressNow() const;
            const char* GetBakePhaseNow() const;

            std::mutex m_BakedInboxMutex;
            std::unordered_map<std::string, std::shared_ptr<const ProbeBakeData>> m_BakedInbox;
            std::unordered_set<std::string> m_BakedRequested;

            bool m_SceneBuilt = false;
            std::shared_ptr<StorageBuffer> m_BVHBuffer;
            std::shared_ptr<StorageBuffer> m_PosBuffer;
            std::shared_ptr<StorageBuffer> m_AttribBuffer;
            std::shared_ptr<StorageBuffer> m_MatBuffer;

            std::future<Raytracing::RaytraceScene> m_PendingSceneBuild;
            std::atomic<uint64_t> m_SceneBuildGeneration{ 0 };
            uint64_t m_PendingSceneBuildGeneration = 0;
            std::vector<std::future<Raytracing::RaytraceScene>> m_AbandonedSceneBuilds;

            std::atomic<bool> m_SceneBuilding{ false };
            std::atomic<float> m_SceneBuildProgress{ 0.0f };
            std::atomic<const char*> m_SceneBuildPhase{ "" };

            std::shared_ptr<StorageBuffer> m_LightBuffer;
            std::vector<LightData> m_FlatLights;

            // Same cubemap lit.frag samples as ibl_irradianceMap. Re-read every frame so a skybox swap can't leave it dangling.
            std::shared_ptr<Cubemap> m_SkyIrradiance;

            std::shared_ptr<ComputeShader> m_TraceShader;
            std::shared_ptr<ComputePipeline> m_TracePipeline;
            std::shared_ptr<ComputeShader> m_ConvolveShader;
            std::shared_ptr<ComputePipeline> m_ConvolvePipeline;
            std::shared_ptr<ComputeShader> m_BorderFixupShader;
            std::shared_ptr<ComputePipeline> m_BorderFixupPipeline;
            std::shared_ptr<ComputeShader> m_ClassifyShader;
            std::shared_ptr<ComputePipeline> m_ClassifyPipeline;
            std::shared_ptr<ComputeShader> m_RelocateShader;
            std::shared_ptr<ComputePipeline> m_RelocatePipeline;
            std::shared_ptr<ComputeShader> m_TemporalBlendShader;
            std::shared_ptr<ComputePipeline> m_TemporalBlendPipeline;
            // Separate shader, the rgba16f image format of the other one can't bind an RG16F atlas.
            std::shared_ptr<ComputeShader> m_DistanceTemporalBlendShader;
            std::shared_ptr<ComputePipeline> m_DistanceTemporalBlendPipeline;

            std::mt19937 m_RayRNG;
    };
}
