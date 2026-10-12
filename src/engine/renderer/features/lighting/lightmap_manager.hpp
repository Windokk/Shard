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

    /// What a finished bake hands to whoever owns the world : the file it wrote, the baked pages themselves (so they can be used
    /// without reading the file back) and where every model's lightmap is in them.
    struct LightmapBakeResult
    {
        bool success = false;
        Filesystem::Path file;
        std::shared_ptr<LightmapData> data;
        uint32_t pageSize = 0;
        std::vector<LightmapPlacement> placements;      // the models that got a lightmap, by proxy key
        std::vector<uint64_t> staticKeys;               // every model that took part in the bake (a model of these without a placement could not be lightmapped)
        LightmapSettings settings;
    };

    /// @brief Baked lighting for the static part of a world : the diffuse light each surface receives - the sky, the Static
    /// lights and everything they bounce off - computed once, with the path tracer, and stored in textures laid over the
    /// surfaces (lightmaps), where the forward pass reads it back. What a few probes per room cannot give : light, shadow and
    /// colour bleeding at a texel per few centimetres, noise free (the bake is denoised), for nothing per frame.
    ///
    /// The baker :
    ///  1. every model marked static is flattened into the path tracer's scene (a BVH) on a worker thread, while the lightmap of
    ///     each is given a place in a shared atlas (PlanLightmapLayout) from the second set of texture coordinates every mesh
    ///     carries (see UnwrapForLightmap) ;
    ///  2. page after page : the CPU draws the layout into G-buffer images (the surface point and normal of every texel,
    ///     RasterizeLightmapPage), the GPU traces paths from each texel over the following frames (lightmap_bake.comp), the sums
    ///     are turned into a mean and a variance (lightmap_resolve.comp) and filtered (lightmap_atrous.comp), then the page is
    ///     read back, its gutter filled and packed to shared exponent texels on a worker thread ;
    ///  3. the pages are written to a file, and the owner of the world is handed the result to store the placements in the
    ///     models and the file's name in the world.
    /// Everything runs on the render thread, a slice per frame, so the editor stays alive during the bake.
    ///
    /// At run time the manager keeps the baked pages of the world as textures, and hands the renderer the bindless handle of the
    /// page each model reads (lit.frag).
    class LightmapManager
    {
        public:
            LightmapManager();
            ~LightmapManager();

            // ---- Run time ----

            /// Brings the pages in line with the world's : asks for the file the snapshot names when it is not the one in use,
            /// uploads what was provided, drops pages the world no longer has. Called once per frame from Renderer::BeginFrame().
            void Sync(const RenderSnapshot& scene);

            /// The handle a draw samples page `page` by, or 0 when there is no such page (not loaded yet, no lightmap)
            uint64_t GetPageHandle(int page) const;

            /// Whether pages are uploaded
            bool HasPages() const { return !m_Pages.empty(); }

            /// The renderer does not touch the resources manager : when the world names a lightmap file it asks for it by this,
            /// which must not block. The owner loads it where it can and answers with ProvideData() (null if the file is
            /// unusable).
            std::function<void(const std::string&)> requestData;
            void ProvideData(const std::string& path, std::shared_ptr<const LightmapData> data);

            /// Called once a file's pages are uploaded, so its owner can drop the CPU copy
            std::function<void(const std::string&)> releaseData;

            // ---- Baking ----

            /// Starts baking `scene`'s static models into `file`. Returns false (logged) if it could not start (a bake is
            /// already running, no static model). When it is done onBakeFinished is called (on the render thread).
            bool BeginBake(const RenderSnapshot& scene, const LightmapSettings& settings, const Filesystem::Path& file);

            /// Aborts a bake in progress. No-op if none is running.
            void CancelBake();

            std::function<void(const LightmapBakeResult&)> onBakeFinished;

            /// Advances the running bake by this frame's share. Called once per frame from Renderer::BeginFrame().
            void Update();

            /// Copies what the other threads may ask about : the render thread calls it once per frame, after Update()
            void PublishStatus();

            // Safe to call from any thread, they read what PublishStatus() copied
            bool IsBaking() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.baking; }
            float GetBakeProgress() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.progress; }
            const char* GetBakePhase() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.phase; }
            bool DidLastBakeSucceed() const { std::lock_guard<std::mutex> lock(m_StatusMutex); return m_Status.lastSucceeded; }

            /// Blocks until every background job (scene build, rasterizing, encoding), the current ones and the abandoned
            /// ones, is done. They run on the engine's thread pool, so this has to happen before it is destroyed.
            void WaitForJobs();

        private:
            enum class Phase { BuildingScene, Rasterizing, Tracing, Denoising, Encoding };

            struct BakeInstance
            {
                size_t modelIndex = 0;      // in the captured snapshot's models
            };

            struct BakeJob
            {
                bool active = false;
                Phase phase = Phase::BuildingScene;
                LightmapSettings settings;
                Filesystem::Path file;

                // The static models, flattened : kept alive for the worker threads that read their vertices
                std::shared_ptr<const Raytracing::RaytraceSceneSnapshot> snapshot;
                std::vector<uint64_t> staticKeys;
                std::vector<size_t> placementModel;     // per placement of the layout, the model it is for
                LightmapLayout layout;

                std::vector<LightData> lights;          // the lights the bake sees : Stationary and Static ones
                std::shared_ptr<Cubemap> sky;

                std::future<Raytracing::RaytraceScene> sceneFuture;

                std::shared_ptr<StorageBuffer> bvhBuffer, posBuffer, attribBuffer, matBuffer, lightBuffer, texelBuffer;

                // The page being baked
                uint32_t page = 0;
                std::future<std::shared_ptr<LightmapPageRaster>> rasterFuture;
                std::shared_ptr<LightmapPageRaster> raster;
                std::shared_ptr<Texture2D> gPos, gNormal, gGeo, accum, denoiseA, denoiseB;
                std::shared_ptr<Texture2D> result;      // what the denoiser ended with (denoiseA or denoiseB)
                uint32_t texelCursor = 0;
                uint32_t sampleCursor = 0;
                uint64_t pathsDone = 0;
                uint64_t pathsTotal = 0;
                std::future<LightmapPageData> encodeFuture;

                std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
                uint64_t pathsTraced = 0;               // all the pages together

                LightmapData data;                      // the pages done so far
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

            // ---- Run time ----
            void ReleasePages();

            std::vector<std::shared_ptr<Texture2D>> m_Pages;
            std::vector<uint64_t> m_PageHandles;
            std::string m_PagesPath;                    // the file the pages in use come from

            std::mutex m_InboxMutex;
            std::unordered_map<std::string, std::shared_ptr<const LightmapData>> m_Inbox;   // answered, waiting for Sync() (a null entry : asked for, unusable)
            std::string m_Requested;                    // the file asked for and not answered yet

            // ---- Baking ----
            BakeJob m_Bake;
            bool m_LastBakeSucceeded = false;

            std::shared_ptr<ComputeShader> m_BakeShader, m_ResolveShader, m_AtrousShader;
            std::shared_ptr<ComputePipeline> m_BakePipeline, m_ResolvePipeline, m_AtrousPipeline;

            // Written by the scene build's worker thread, polled by the status
            std::atomic<float> m_SceneBuildProgress{ 0.0f };
            std::atomic<bool> m_SceneBuilding{ false };

            // What a cancelled bake leaves running : its jobs cannot be interrupted, only waited for
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
