#include "lightmap_manager.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_map>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/core/jobs/thread_pool.hpp"
#include "engine/renderer/features/raytracing/bvh.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/shader/compute_shader.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        // How much of the GPU one frame of the editor gives the bake : paths traced per dispatch (kept short, so no single
        // dispatch comes near the driver's watchdog) and per frame. Each path starts at a texel and runs up to maxBounces bounces.
        constexpr uint32_t kSamplesPerDispatch = 4;
        constexpr uint32_t kPathsPerDispatch = 1u << 17;
        constexpr uint64_t kPathsPerFrame = 1ull << 20;

        // The sky's texture unit (see path_tracing.glsl) and the SSBO bindings of the baker's shaders (see lightmap_bake.comp)
        constexpr uint32_t kSkyUnit = 45;
        constexpr uint32_t kBvhBinding = 8;
        constexpr uint32_t kPositionsBinding = 9;
        constexpr uint32_t kAttribsBinding = 10;
        constexpr uint32_t kMaterialsBinding = 11;
        constexpr uint32_t kLightsBinding = 12;
        constexpr uint32_t kTexelsBinding = 13;
        constexpr uint32_t kEmissiveBinding = 16;     // the emissive triangles the paths sample directly (lightmap_bake.comp)

        bool FindElementOffset(const VertexLayout& layout, const char* name, uint32_t& offset)
        {
            for (const VertexElement& element : layout.GetElements())
            {
                if (element.name == name)
                {
                    offset = element.offset;
                    return true;
                }
            }
            return false;
        }

        struct MeshAreas
        {
            float surface = 0.0f;       // the area of the mesh's triangles, in its own units
            float uv = 0.0f;            // and their area in the lightmap square
        };

        MeshAreas ComputeMeshAreas(const MeshCPUCopy& cpu, uint32_t stride, uint32_t positionOffset, uint32_t uvOffset)
        {
            MeshAreas areas;
            double surface = 0.0, uv = 0.0;

            auto position = [&](uint32_t index) {
                glm::vec3 p;
                std::memcpy(&p, cpu.vertices.data() + size_t(index) * stride + positionOffset, sizeof(p));
                return p;
            };
            auto lightmapUV = [&](uint32_t index) {
                glm::vec2 p;
                std::memcpy(&p, cpu.vertices.data() + size_t(index) * stride + uvOffset, sizeof(p));
                return p;
            };

            for (size_t t = 0; t + 2 < cpu.indices.size(); t += 3)
            {
                const uint32_t i0 = cpu.indices[t], i1 = cpu.indices[t + 1], i2 = cpu.indices[t + 2];
                surface += 0.5 * glm::length(glm::cross(position(i1) - position(i0), position(i2) - position(i0)));
                const glm::vec2 e1 = lightmapUV(i1) - lightmapUV(i0), e2 = lightmapUV(i2) - lightmapUV(i0);
                uv += 0.5 * std::abs(double(e1.x) * e2.y - double(e1.y) * e2.x);
            }

            areas.surface = float(surface);
            areas.uv = float(uv);
            return areas;
        }

        // How much a world matrix scales areas (the square of the scale, for a uniform one)
        float AreaScale(const glm::mat4& world)
        {
            const float determinant = std::abs(glm::determinant(glm::mat3(world)));
            return std::pow(std::max(determinant, 1e-20f), 2.0f / 3.0f);
        }

        std::shared_ptr<Texture2D> MakeImage(uint32_t size, TextureInternalFormat format, const float* data)
        {
            TextureSpecifications spec;
            spec.width = size;
            spec.height = size;
            spec.format = TextureFormat::RGBA;
            spec.internalFormat = format;
            spec.generateMips = false;
            spec.immutableStorage = true;
            spec.minFilter = TextureFilter::Nearest;
            spec.magFilter = TextureFilter::Nearest;
            spec.wrapS = TextureWrap::ClampEdge;
            spec.wrapT = TextureWrap::ClampEdge;
            return Texture2D::Create(spec, data);
        }

        // Runs `f` on the engine's thread pool for work that takes a while (the pool is how the scene builds already run)
        template <typename F>
        auto RunInBackground(F&& f) -> std::future<std::invoke_result_t<std::decay_t<F>>>
        {
            if (Core::ThreadPool* pool = Core::GetEngine().GetThreadPool(Core::PoolKind::IO))
                return pool->Submit(std::forward<F>(f));
            return std::async(std::launch::async, std::forward<F>(f));
        }

        template <typename T>
        bool IsReady(std::future<T>& future)
        {
            return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }
    }

    LightmapManager::LightmapManager() = default;

    LightmapManager::~LightmapManager()
    {
        WaitForJobs();
    }

    void LightmapManager::WaitForJobs()
    {
        auto drain = [](auto& futures) {
            for (auto& future : futures)
                if (future.valid())
                    future.wait();
            futures.clear();
        };

        drain(m_AbandonedScenes);
        drain(m_AbandonedRasters);
        drain(m_AbandonedEncodes);

        if (m_Bake.sceneFuture.valid()) m_Bake.sceneFuture.wait();
        if (m_Bake.rasterFuture.valid()) m_Bake.rasterFuture.wait();
        if (m_Bake.encodeFuture.valid()) m_Bake.encodeFuture.wait();
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Run time : the baked pages of the world, as textures
    // ---------------------------------------------------------------------------------------------------------------------

    void LightmapManager::ProvideData(const std::string& path, std::shared_ptr<const LightmapData> data)
    {
        std::lock_guard<std::mutex> lock(m_InboxMutex);
        m_Inbox[path] = std::move(data);
    }

    void LightmapManager::ReleasePages()
    {
        m_Pages.clear();
        m_PageHandles.clear();
        m_PagesPath.clear();
    }

    uint64_t LightmapManager::GetPageHandle(int page) const
    {
        if (page < 0 || size_t(page) >= m_PageHandles.size())
            return 0;
        return m_PageHandles[size_t(page)];
    }

    void LightmapManager::Sync(const RenderSnapshot& scene)
    {
        const std::string& wanted = scene.environment.lightmapData;

        if (wanted.empty())
        {
            if (!m_PagesPath.empty() || !m_Pages.empty())
                ReleasePages();
            m_Requested.clear();
            return;
        }

        // The answer to an earlier request (or the pages a bake has just made) for the file the world names
        std::shared_ptr<const LightmapData> answer;
        bool answered = false;
        {
            std::lock_guard<std::mutex> lock(m_InboxMutex);
            auto it = m_Inbox.find(wanted);
            if (it != m_Inbox.end())
            {
                answer = it->second;
                answered = true;
                m_Inbox.erase(it);
            }
        }

        if (answered)
        {
            m_Requested.clear();
            ReleasePages();
            m_PagesPath = wanted;

            if (answer)
            {
                for (const LightmapPageData& page : answer->pages)
                {
                    TextureSpecifications spec;
                    spec.width = page.width;
                    spec.height = page.height;
                    spec.format = TextureFormat::RGB;
                    spec.internalFormat = TextureInternalFormat::RGB9E5;
                    spec.generateMips = false;
                    spec.minFilter = TextureFilter::Linear;
                    spec.magFilter = TextureFilter::Linear;
                    spec.wrapS = TextureWrap::ClampEdge;
                    spec.wrapT = TextureWrap::ClampEdge;

                    std::shared_ptr<Texture2D> texture = Texture2D::Create(spec, page.texels.data());
                    m_PageHandles.push_back(texture ? texture->GetBindlessHandle() : 0);
                    m_Pages.push_back(std::move(texture));
                }
                DEBUG_INFO("Lightmaps : " + std::to_string(m_Pages.size()) + " page(s) of " + wanted + " are in use.");
            }
            else
            {
                DEBUG_WARNING("Lightmaps : " + wanted + " could not be loaded - the world is lit without them.");
            }

            if (releaseData)
                releaseData(wanted);
            return;
        }

        if (wanted != m_PagesPath && wanted != m_Requested && requestData)
        {
            m_Requested = wanted;
            requestData(wanted);
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // Baking
    // ---------------------------------------------------------------------------------------------------------------------

    void LightmapManager::EnsureShaders()
    {
        if (m_BakePipeline && m_ResolvePipeline && m_AtrousPipeline)
            return;

        IRenderContext* renderer = Core::GetEngine().GetRenderContext();
        const Filesystem::Path resRoot = Core::GetEngine().GetFileManager()->GetEngineResRoot();

        auto load = [&](std::shared_ptr<ComputeShader>& shader, std::shared_ptr<ComputePipeline>& pipeline, const char* file, const char* name) {
            if (pipeline)
                return;

            shader = ComputeShader::Create(resRoot / (std::string("shaders/compute/") + file));
            if (!shader)
            {
                DEBUG_ERROR(std::string("LightmapManager : failed to load ") + file);
                return;
            }

            ComputePipelineSpecifications specs;
            specs.shader = shader;
            specs.debugName = name;
            pipeline = renderer->GetOrAddComputePipeline(specs);
        };

        load(m_BakeShader, m_BakePipeline, "lightmap_bake.comp", "LightmapBake");
        load(m_ResolveShader, m_ResolvePipeline, "lightmap_resolve.comp", "LightmapResolve");
        load(m_AtrousShader, m_AtrousPipeline, "lightmap_atrous.comp", "LightmapAtrous");
    }

    bool LightmapManager::PlanInstances(BakeJob& job)
    {
        const std::vector<Raytracing::ModelSnapshot>& models = job.snapshot->models;

        std::unordered_map<const Mesh*, MeshAreas> areas;
        std::vector<LightmapInstanceInput> inputs;
        size_t withoutCoordinates = 0;

        for (size_t i = 0; i < models.size(); i++)
        {
            const Raytracing::ModelSnapshot& model = models[i];
            job.staticKeys.push_back(model.key);

            uint32_t positionOffset = 0, uvOffset = 0;
            const VertexLayout& layout = model.mesh->GetVertexLayout();
            if (!model.cpu || !FindElementOffset(layout, "aPos", positionOffset) || !FindElementOffset(layout, "aLightmapUV", uvOffset))
            {
                withoutCoordinates++;
                continue;
            }

            auto found = areas.find(model.mesh.get());
            if (found == areas.end())
                found = areas.emplace(model.mesh.get(), ComputeMeshAreas(*model.cpu, layout.GetStride(), positionOffset, uvOffset)).first;

            if (found->second.surface <= 0.0f || found->second.uv <= 0.0f)
            {
                withoutCoordinates++;
                continue;
            }

            LightmapInstanceInput input;
            input.key = model.key;
            input.surfaceArea = found->second.surface * AreaScale(model.worldMatrix);
            input.uvArea = found->second.uv;
            input.resolutionOverride = uint32_t(std::max(model.lightmapResolution, 0));
            inputs.push_back(input);
            job.placementModel.push_back(i);
        }

        if (withoutCoordinates > 0)
            DEBUG_WARNING("LightmapManager : " + std::to_string(withoutCoordinates) + " static model(s) have no lightmap coordinates (mesh.lightmap_uvs was off when their mesh loaded) and keep the probe lighting.");

        if (inputs.empty())
        {
            DEBUG_ERROR("LightmapManager : none of the static models can take a lightmap.");
            return false;
        }

        job.layout = PlanLightmapLayout(inputs, job.settings);
        DEBUG_INFO("LightmapManager : " + std::to_string(inputs.size()) + " lightmap(s) in " + std::to_string(job.layout.pageCount) + " page(s) of " +
                   std::to_string(job.layout.pageSize) + "x" + std::to_string(job.layout.pageSize) + ".");
        return job.layout.pageCount > 0;
    }

    void LightmapManager::CollectLights(BakeJob& job)
    {
        // A Dynamic light never takes part. A Stationary one lights the surfaces the paths hit (its bounced light), a Static
        // one also lights the texels themselves (see lightmap_bake.comp)
        IRenderContext* renderer = Core::GetEngine().GetRenderContext();
        for (const std::shared_ptr<LightData>& light : renderer->GetLightManager()->GetLights())
            if (light && light->bakeMode >= int(LightBakeMode::Stationary) && light->intensity > 0.0f)
                job.lights.push_back(*light);
    }

    bool LightmapManager::BeginBake(const RenderSnapshot& scene, const LightmapSettings& requested, const Filesystem::Path& file)
    {
        // Called by the editor : the bake belongs to the render thread (it parks the caller meanwhile, `scene` stays valid)
        if (!RenderThread::Get().IsGraphicsThread())
            return RenderThread::Get().InvokeResult([&] { return BeginBake(scene, requested, file); });

        if (m_Bake.active)
        {
            DEBUG_WARNING("LightmapManager : a lightmap bake is already running, ignoring the new request.");
            return false;
        }

        BakeJob job;
        job.settings = requested;
        job.settings.samples = std::max(job.settings.samples, 1u);
        job.settings.maxBounces = std::max(job.settings.maxBounces, 1u);
        job.settings.maxPageSize = std::clamp(job.settings.maxPageSize, 64u, 8192u);
        job.settings.minResolution = std::max(job.settings.minResolution, 4u);
        job.settings.maxResolution = std::max(job.settings.maxResolution, job.settings.minResolution);
        job.settings.texelsPerMeter = std::max(job.settings.texelsPerMeter, 0.01f);
        job.file = file;

        // Only the static models take part : they are what the lightmaps are laid on, what blocks the light, what it bounces off
        RenderSnapshot staticScene;
        staticScene.environment = scene.environment;
        for (const MeshProxy& proxy : scene.meshes)
            if (proxy.staticLighting && proxy.mesh)
                staticScene.meshes.push_back(proxy);

        if (staticScene.meshes.empty())
        {
            DEBUG_ERROR("LightmapManager : the world has no static model to bake.");
            return false;
        }

        job.snapshot = std::make_shared<const Raytracing::RaytraceSceneSnapshot>(Raytracing::SceneBuilder::CaptureSnapshot(staticScene, false));
        if (job.snapshot->models.empty())
        {
            DEBUG_ERROR("LightmapManager : none of the static models has geometry to bake.");
            return false;
        }

        if (!PlanInstances(job))
            return false;

        CollectLights(job);
        if (job.lights.empty() && !scene.environment.skybox)
            DEBUG_WARNING("LightmapManager : there is no Static / Stationary light and no skybox : the lightmaps will only hold emissive surfaces.");

        job.sky = scene.environment.skybox ? scene.environment.skybox->GetCubemap() : nullptr;

        // The BVH, on a worker thread (the heavy part) while the first page is being laid out
        m_SceneBuilding.store(true, std::memory_order_relaxed);
        m_SceneBuildProgress.store(0.0f, std::memory_order_relaxed);
        job.sceneFuture = RunInBackground([this, snapshot = job.snapshot] {
            auto onProgress = [this](float fraction, const char*) { m_SceneBuildProgress.store(fraction, std::memory_order_relaxed); };
            return Raytracing::SceneBuilder::BuildFromSnapshot(*snapshot, onProgress);
        });

        job.phase = Phase::BuildingScene;
        job.active = true;
        m_Bake = std::move(job);
        m_LastBakeSucceeded = false;
        return true;
    }

    void LightmapManager::AbandonJobs(BakeJob& job)
    {
        if (job.sceneFuture.valid()) m_AbandonedScenes.push_back(std::move(job.sceneFuture));
        if (job.rasterFuture.valid()) m_AbandonedRasters.push_back(std::move(job.rasterFuture));
        if (job.encodeFuture.valid()) m_AbandonedEncodes.push_back(std::move(job.encodeFuture));
    }

    void LightmapManager::CancelBake()
    {
        SHARD_FORWARD_TO_GRAPHICS_THREAD(CancelBake())

        if (!m_Bake.active)
            return;

        AbandonJobs(m_Bake);
        m_SceneBuilding.store(false, std::memory_order_relaxed);
        m_Bake = BakeJob{};
        m_LastBakeSucceeded = false;
        DEBUG_INFO("LightmapManager : the lightmap bake was cancelled.");
    }

    void LightmapManager::UploadScene(BakeJob& job, const Raytracing::RaytraceScene& scene)
    {
        auto upload = [](const void* data, size_t bytes) {
            std::shared_ptr<StorageBuffer> buffer = StorageBuffer::Create(uint32_t(std::max<size_t>(bytes, 16)));
            if (bytes > 0)
                buffer->SetData(data, uint32_t(bytes));
            return buffer;
        };

        job.bvhBuffer = upload(scene.bvhNodes.data(), scene.bvhNodes.size() * sizeof(Raytracing::BVHNode));
        job.posBuffer = upload(scene.trianglePositions.data(), scene.trianglePositions.size() * sizeof(Raytracing::GPUTrianglePos));
        job.attribBuffer = upload(scene.triangleAttribs.data(), scene.triangleAttribs.size() * sizeof(Raytracing::GPUTriangleAttrib));
        job.matBuffer = upload(scene.materials.data(), scene.materials.size() * sizeof(Raytracing::GPUMaterial));
        job.lightBuffer = upload(job.lights.data(), job.lights.size() * sizeof(LightData));

        job.emissiveBuffer = upload(scene.emissiveTriangles.data(), scene.emissiveTriangles.size() * sizeof(Raytracing::GPUEmissiveTriangle));
        job.emissiveCount = int(scene.emissiveTriangles.size());
        job.emissivePower = scene.emissivePower;
    }

    void LightmapManager::StartPage(BakeJob& job)
    {
        std::vector<LightmapRasterInstance> instances;
        for (size_t k = 0; k < job.layout.placements.size(); k++)
        {
            const LightmapPlacement& placement = job.layout.placements[k];
            if (placement.page != job.page)
                continue;

            const Raytracing::ModelSnapshot& model = job.snapshot->models[job.placementModel[k]];
            const VertexLayout& layout = model.mesh->GetVertexLayout();

            LightmapRasterInstance instance;
            instance.vertices = model.cpu->vertices.data();
            instance.vertexStride = layout.GetStride();
            FindElementOffset(layout, "aPos", instance.positionOffset);
            uint32_t normalOffset = 0;
            instance.normalOffset = FindElementOffset(layout, "aNormal", normalOffset) ? int(normalOffset) : -1;
            FindElementOffset(layout, "aLightmapUV", instance.lightmapUVOffset);
            instance.indices = model.cpu->indices.data();
            instance.indexCount = model.cpu->indices.size();
            instance.worldMatrix = model.worldMatrix;
            instance.x = placement.x;
            instance.y = placement.y;
            instance.resolution = placement.resolution;
            instances.push_back(instance);
        }

        // The G-buffer of the page is drawn on a worker thread, from vertex data the snapshot keeps alive
        job.rasterFuture = RunInBackground([snapshot = job.snapshot, instances = std::move(instances), size = job.layout.pageSize] {
            return RasterizeLightmapPage(size, instances);
        });
        job.phase = Phase::Rasterizing;
    }

    void LightmapManager::CreatePageTextures(BakeJob& job)
    {
        const uint32_t size = job.layout.pageSize;
        LightmapPageRaster& raster = *job.raster;

        job.gPos = MakeImage(size, TextureInternalFormat::RGBA32F, raster.position.data());
        job.gNormal = MakeImage(size, TextureInternalFormat::RGBA16F, raster.normal.data());
        job.gGeo = MakeImage(size, TextureInternalFormat::RGBA16F, raster.geometric.data());
        job.accum = MakeImage(size, TextureInternalFormat::RGBA32F, nullptr);
        job.denoiseA = MakeImage(size, TextureInternalFormat::RGBA32F, nullptr);
        job.denoiseB = MakeImage(size, TextureInternalFormat::RGBA32F, nullptr);

        const size_t texelBytes = raster.texelList.size() * sizeof(uint32_t);
        job.texelBuffer = StorageBuffer::Create(uint32_t(std::max<size_t>(texelBytes, 16)));
        if (texelBytes > 0)
            job.texelBuffer->SetData(raster.texelList.data(), uint32_t(texelBytes));

        // The normals are on the GPU now : the position's alpha is all the encoding needs of the raster (which texels are covered)
        raster.normal = std::vector<float>();
        raster.geometric = std::vector<float>();

        job.texelCursor = 0;
        job.sampleCursor = 0;
        job.pathsDone = 0;
        job.pathsTotal = uint64_t(raster.texelList.size()) * job.settings.samples;
    }

    void LightmapManager::StepTracing(BakeJob& job)
    {
        const uint32_t texelCount = uint32_t(job.raster->texelList.size());
        if (texelCount == 0)
        {
            job.sampleCursor = job.settings.samples;
        }
        else
        {
            IRenderContext* renderer = Core::GetEngine().GetRenderContext();

            // Everything the baker reads is bound again every frame : the frame in between drew with its own bindings
            m_BakePipeline->Bind();
            job.accum->BindImage(0, TextureAccess::ReadWrite);
            job.gPos->BindImage(1, TextureAccess::ReadOnly);
            job.gNormal->BindImage(2, TextureAccess::ReadOnly);
            job.gGeo->BindImage(3, TextureAccess::ReadOnly);
            job.bvhBuffer->Bind(kBvhBinding);
            job.posBuffer->Bind(kPositionsBinding);
            job.attribBuffer->Bind(kAttribsBinding);
            job.matBuffer->Bind(kMaterialsBinding);
            job.lightBuffer->Bind(kLightsBinding);
            job.texelBuffer->Bind(kTexelsBinding);
            job.emissiveBuffer->Bind(kEmissiveBinding);

            const bool hasSky = job.sky != nullptr;
            if (hasSky)
                job.sky->Bind(kSkyUnit);

            m_BakeShader->SetInt("uLightCount", int(job.lights.size()));
            m_BakeShader->SetInt("uMaxBounces", int(job.settings.maxBounces));
            m_BakeShader->SetInt("uEmissiveCount", job.emissiveCount);
            m_BakeShader->SetFloat("uEmissivePower", job.emissivePower);
            m_BakeShader->SetBool("uHasSky", hasSky);
            m_BakeShader->SetVec3("uSkyColor", glm::vec3(0.0f));
            m_BakeShader->SetFloat("uAmbientIntensity", 0.0f);     // the forward pass adds the world's flat ambient at run time
            m_BakeShader->SetFloat("uFireflyClamp", job.settings.fireflyClamp);
            m_BakeShader->SetInt("uSeed", int(job.page));
            m_BakeShader->SetFloat("uIndirectScale", job.settings.indirectIntensity);

            int64_t budget = int64_t(kPathsPerFrame);
            while (budget > 0 && job.sampleCursor < job.settings.samples)
            {
                const uint32_t samples = std::min(kSamplesPerDispatch, job.settings.samples - job.sampleCursor);
                const uint32_t chunkLimit = std::max(64u, (kPathsPerDispatch / samples) & ~63u);
                const uint32_t chunk = std::min(chunkLimit, texelCount - job.texelCursor);

                m_BakeShader->SetInt("uTexelOffset", int(job.texelCursor));
                m_BakeShader->SetInt("uTexelCount", int(chunk));
                m_BakeShader->SetInt("uSampleStart", int(job.sampleCursor));
                m_BakeShader->SetInt("uSamples", int(samples));

                // The next dispatch reads what this one wrote when it comes back to the same texels : ImageAccess
                renderer->DispatchCompute(m_BakePipeline, (chunk + 63) / 64, 1, 1, MemoryBarrierBit::ImageAccess);

                job.texelCursor += chunk;
                if (job.texelCursor >= texelCount)
                {
                    job.texelCursor = 0;
                    job.sampleCursor += samples;
                }

                budget -= int64_t(chunk) * samples;
                job.pathsDone += uint64_t(chunk) * samples;
                job.pathsTraced += uint64_t(chunk) * samples;
            }
        }

        if (job.sampleCursor >= job.settings.samples)
            job.phase = Phase::Denoising;
    }

    void LightmapManager::RunDenoise(BakeJob& job)
    {
        IRenderContext* renderer = Core::GetEngine().GetRenderContext();
        const uint32_t size = job.layout.pageSize;
        const uint32_t groups = (size + 7) / 8;

        // The sums become a mean and the variance of that mean
        m_ResolvePipeline->Bind();
        job.accum->BindImage(0, TextureAccess::ReadOnly);
        job.denoiseA->BindImage(1, TextureAccess::WriteOnly);
        job.gPos->BindImage(2, TextureAccess::ReadOnly);
        m_ResolveShader->SetInt("uSize", int(size));
        m_ResolveShader->SetFloat("uSampleCount", float(job.settings.samples));
        renderer->DispatchCompute(m_ResolvePipeline, groups, groups, 1, MemoryBarrierBit::ImageAccess);

        job.result = job.denoiseA;

        if (job.settings.denoise)
        {
            const int iterations = int(std::clamp<uint32_t>(job.settings.denoiseIterations, 1u, 8u));
            for (int i = 0; i < iterations; i++)
            {
                const std::shared_ptr<Texture2D>& in = (i % 2 == 0) ? job.denoiseA : job.denoiseB;
                const std::shared_ptr<Texture2D>& out = (i % 2 == 0) ? job.denoiseB : job.denoiseA;

                m_AtrousPipeline->Bind();
                in->BindImage(0, TextureAccess::ReadOnly);
                out->BindImage(1, TextureAccess::WriteOnly);
                job.gPos->BindImage(2, TextureAccess::ReadOnly);
                job.gNormal->BindImage(3, TextureAccess::ReadOnly);
                m_AtrousShader->SetInt("uSize", int(size));
                m_AtrousShader->SetInt("uStep", 1 << i);
                m_AtrousShader->SetFloat("uLuminanceSigma", job.settings.denoiseLuminanceSigma);
                m_AtrousShader->SetFloat("uNormalPower", job.settings.denoiseNormalPower);
                m_AtrousShader->SetFloat("uPlaneSigma", job.settings.denoisePlaneSigma);
                renderer->DispatchCompute(m_AtrousPipeline, groups, groups, 1, MemoryBarrierBit::ImageAccess);

                job.result = out;
            }
        }
    }

    void LightmapManager::StartEncoding(BakeJob& job)
    {
        IRenderContext* renderer = Core::GetEngine().GetRenderContext();
        const uint32_t size = job.layout.pageSize;

        // The host reads what the compute shaders wrote
        renderer->GetRendererAPI()->MemoryBarrier(MemoryBarrierBit::TextureUpdate);

        std::vector<float> pixels(size_t(size) * size * 4);
        job.result->ReadPixels(pixels.data(), pixels.size() * sizeof(float));

        // The page's GPU resources are done with : the next page makes its own
        job.gPos.reset();
        job.gNormal.reset();
        job.gGeo.reset();
        job.accum.reset();
        job.denoiseA.reset();
        job.denoiseB.reset();
        job.result.reset();
        job.texelBuffer.reset();

        // The gutter and the shared exponent encoding are per texel work for the CPU : off the render thread
        job.encodeFuture = RunInBackground([pixels = std::move(pixels), raster = job.raster, size]() mutable {
            for (size_t i = 0; i < size_t(size) * size; i++)
                pixels[i * 4 + 3] = raster->position[i * 4 + 3] > 0.0f ? 1.0f : 0.0f;
            return EncodeLightmapPage(size, size, pixels.data());
        });
        job.phase = Phase::Encoding;
    }

    void LightmapManager::FinishPage(BakeJob& job)
    {
        job.raster.reset();
        job.page++;

        if (job.page < job.layout.pageCount)
            StartPage(job);
        else
            EndBake(true);
    }

    void LightmapManager::EndBake(bool success)
    {
        LightmapBakeResult result;
        result.settings = m_Bake.settings;
        result.file = m_Bake.file;
        result.pageSize = m_Bake.layout.pageSize;
        result.staticKeys = m_Bake.staticKeys;
        result.placements = m_Bake.layout.placements;

        if (success)
        {
            result.data = std::make_shared<LightmapData>(std::move(m_Bake.data));
            success = WriteLightmapFile(result.file, *result.data);
            if (success)
            {
                const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Bake.startTime).count();
                char summary[160];
                std::snprintf(summary, sizeof(summary), " in %.1f s (%.1f million paths).", seconds, double(m_Bake.pathsTraced) / 1e6);
                DEBUG_INFO("LightmapManager : lightmaps baked to " + result.file.full + summary);
            }
        }
        result.success = success;

        m_SceneBuilding.store(false, std::memory_order_relaxed);
        m_Bake = BakeJob{};
        m_LastBakeSucceeded = success;

        if (onBakeFinished)
            onBakeFinished(result);
    }

    float LightmapManager::GetProgressNow() const
    {
        if (!m_Bake.active)
            return 0.0f;

        // The BVH is the first stretch, then every page gets an equal share, split between drawing it, tracing it and the rest
        constexpr float kSceneShare = 0.15f;
        if (m_Bake.phase == Phase::BuildingScene)
            return kSceneShare * std::clamp(m_SceneBuildProgress.load(std::memory_order_relaxed), 0.0f, 1.0f);

        float inPage = 0.0f;
        switch (m_Bake.phase)
        {
            case Phase::Rasterizing: inPage = 0.02f; break;
            case Phase::Tracing:
                inPage = 0.05f + 0.85f * (m_Bake.pathsTotal > 0 ? float(double(m_Bake.pathsDone) / double(m_Bake.pathsTotal)) : 1.0f);
                break;
            case Phase::Denoising: inPage = 0.92f; break;
            case Phase::Encoding: inPage = 0.96f; break;
            default: break;
        }

        const float pages = float(std::max(m_Bake.layout.pageCount, 1u));
        return kSceneShare + (1.0f - kSceneShare) * std::clamp((float(m_Bake.page) + inPage) / pages, 0.0f, 1.0f);
    }

    const char* LightmapManager::GetPhaseNow() const
    {
        switch (m_Bake.phase)
        {
            case Phase::BuildingScene: return "Building the scene";
            case Phase::Rasterizing: return "Laying out the lightmaps";
            case Phase::Tracing: return "Tracing light";
            case Phase::Denoising: return "Denoising";
            case Phase::Encoding: return "Packing the lightmaps";
        }
        return "";
    }

    void LightmapManager::PublishStatus()
    {
        std::lock_guard<std::mutex> lock(m_StatusMutex);
        m_Status.baking = m_Bake.active;
        m_Status.progress = GetProgressNow();
        m_Status.phase = m_Bake.active ? GetPhaseNow() : "";
        m_Status.lastSucceeded = m_LastBakeSucceeded;
    }

    void LightmapManager::Update()
    {
        // The jobs a cancelled bake left behind are collected as they finish
        auto collect = [](auto& futures) {
            futures.erase(std::remove_if(futures.begin(), futures.end(), [](auto& f) {
                return !f.valid() || f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            }), futures.end());
        };
        collect(m_AbandonedScenes);
        collect(m_AbandonedRasters);
        collect(m_AbandonedEncodes);

        if (!m_Bake.active)
            return;

        BakeJob& job = m_Bake;

        EnsureShaders();
        if (!m_BakePipeline || !m_ResolvePipeline || !m_AtrousPipeline)
        {
            DEBUG_ERROR("LightmapManager : the baking shaders did not load, the bake is abandoned.");
            AbandonJobs(job);
            EndBake(false);
            return;
        }

        switch (job.phase)
        {
            case Phase::BuildingScene:
            {
                if (!IsReady(job.sceneFuture))
                    return;

                Raytracing::RaytraceScene scene;
                try
                {
                    scene = job.sceneFuture.get();
                }
                catch (const std::exception& e)
                {
                    DEBUG_ERROR(std::string("LightmapManager : building the scene failed : ") + e.what());
                    EndBake(false);
                    return;
                }

                m_SceneBuilding.store(false, std::memory_order_relaxed);
                if (scene.trianglePositions.empty())
                {
                    DEBUG_ERROR("LightmapManager : the static models have no triangle to bake.");
                    EndBake(false);
                    return;
                }

                UploadScene(job, scene);
                job.page = 0;
                StartPage(job);
                return;
            }

            case Phase::Rasterizing:
            {
                if (!IsReady(job.rasterFuture))
                    return;

                job.raster = job.rasterFuture.get();
                if (!job.raster)
                {
                    EndBake(false);
                    return;
                }

                CreatePageTextures(job);
                job.phase = Phase::Tracing;
                return;
            }

            case Phase::Tracing:
                StepTracing(job);
                return;

            case Phase::Denoising:
                RunDenoise(job);
                StartEncoding(job);
                return;

            case Phase::Encoding:
            {
                if (!IsReady(job.encodeFuture))
                    return;

                job.data.pages.push_back(job.encodeFuture.get());
                FinishPage(job);
                return;
            }
        }
    }
}
