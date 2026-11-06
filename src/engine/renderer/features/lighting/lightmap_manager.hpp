#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"
#include "engine/renderer/features/lighting/lightmap_data.hpp"
#include "engine/renderer/features/lighting/lightmap_layout.hpp"
#include "engine/renderer/features/lighting/lightmap_raster.hpp"
#include "engine/renderer/features/lighting/light_manager.hpp"
#include "engine/renderer/features/raytracing/raytrace_scene.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {

    class StorageBuffer;
    class Texture2D;
    class Cubemap;
    class ComputeShader;
    class ComputePipeline;

    struct LightmapBakeResult
    {
        bool success = false;
        Filesystem::Path file;
        std::shared_ptr<LightmapData> data;
        uint32_t pageSize = 0;
        std::vector<LightmapPlacement> placements;
        std::vector<uint64_t> staticKeys;
        LightmapSettings settings;
    };

    // Baked diffuse light for static models, path traced.
    // BVH on a worker, atlas layout, then per page: G-buffer raster, GPU trace, resolve, a-trous denoise, CPU gutter and RGB9E5 encode. Runs a slice per frame on the render thread.
    class LightmapManager
    {
        public:
            LightmapManager();
            ~LightmapManager();

            void Sync(const RenderSnapshot& scene);

            uint64_t GetPageHandle(int page) const;

            bool HasPages() const { return !m_Pages.empty(); }

            std::function<void(const std::string&)> requestData;
            void ProvideData(const std::string& path, std::shared_ptr<const LightmapData> data);

            std::function<void(const std::string&)> releaseData;

            bool BeginBake(const RenderSnapshot& scene, const LightmapSettings& settings, const Filesystem::Path& file);

            void CancelBake();

            std::function<void(const LightmapBakeResult&)> onBakeFinished;

            void Update();

            void PublishStatus();

            bool IsBaking() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.baking; }
            float GetBakeProgress() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.progress; }
            const char* GetBakePhase() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.phase; }
            bool DidLastBakeSucceed() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.lastSucceeded; }

            // The jobs run on the engine's thread pool, so wait before it is destroyed.
            void WaitForJobs();

        private:
            enum class Phase { BuildingScene, Rasterizing, Tracing, Denoising, Encoding };

            struct BakeInstance
            {
                size_t modelIndex = 0;
            };

            struct BakeJob
            {
                bool active = false;
                Phase phase = Phase::BuildingScene;
                LightmapSettings settings;
                Filesystem::Path file;

                std::shared_ptr<const Raytracing::RaytraceSceneSnapshot> snapshot;
                std::vector<uint64_t> staticKeys;
                std::vector<size_t> placementModel;
                LightmapLayout layout;

                std::vector<LightData> lights;
                std::shared_ptr<Cubemap> sky;

                std::future<Raytracing::RaytraceScene> sceneFuture;

                std::shared_ptr<StorageBuffer> bvhBuffer, posBuffer, attribBuffer, matBuffer, lightBuffer, texelBuffer, emissiveBuffer;

                int emissiveCount = 0;
                float emissivePower = 0.0f;

                uint32_t page = 0;
                std::future<std::shared_ptr<LightmapPageRaster>> rasterFuture;
                std::shared_ptr<LightmapPageRaster> raster;
                std::shared_ptr<Texture2D> gPos, gNormal, gGeo, accum, denoiseA, denoiseB;
                std::shared_ptr<Texture2D> result;
                uint32_t texelCursor = 0;
                uint32_t sampleCursor = 0;
                uint64_t pathsDone = 0;
                uint64_t pathsTotal = 0;
                std::future<LightmapPageData> encodeFuture;

                std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
                uint64_t pathsTraced = 0;

                LightmapData data;
            };

            void EnsureShaders();
            bool PlanInstances(BakeJob& job);
            void CollectLights(BakeJob& job);
            void UploadScene(BakeJob& job, const Raytracing::RaytraceScene& scene);
            void StartPage(BakeJob& job);
            void CreatePageTextures(BakeJob& job);
            void StepTracing(BakeJob& job);
            void RunDenoise(BakeJob& job);
            void StartEncoding(BakeJob& job);
            void FinishPage(BakeJob& job);
            void EndBake(bool success);
            void AbandonJobs(BakeJob& job);
            float GetProgressNow() const;
            const char* GetPhaseNow() const;

            void ReleasePages();

            std::vector<std::shared_ptr<Texture2D>> m_Pages;
            std::vector<uint64_t> m_PageHandles;
            std::string m_PagesPath;

            std::mutex m_InboxMutex;
            std::unordered_map<std::string, std::shared_ptr<const LightmapData>> m_Inbox;
            std::string m_Requested;

            BakeJob m_Bake;
            bool m_LastBakeSucceeded = false;

            std::shared_ptr<ComputeShader> m_BakeShader, m_ResolveShader, m_AtrousShader;
            std::shared_ptr<ComputePipeline> m_BakePipeline, m_ResolvePipeline, m_AtrousPipeline;

            std::atomic<float> m_SceneBuildProgress{ 0.0f };
            std::atomic<bool> m_SceneBuilding{ false };

            std::vector<std::future<Raytracing::RaytraceScene>> m_AbandonedScenes;
            std::vector<std::future<std::shared_ptr<LightmapPageRaster>>> m_AbandonedRasters;
            std::vector<std::future<LightmapPageData>> m_AbandonedEncodes;

            struct Status
            {
                bool baking = false;
                float progress = 0.0f;
                const char* phase = "";
                bool lastSucceeded = false;
            };
            Status m_Status;
            mutable std::mutex m_StatusMutex;
    };
}
