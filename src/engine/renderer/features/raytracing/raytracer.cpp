#include "raytracer.hpp"

#include "engine/renderer/features/raytracing/bvh.hpp"
#include "engine/renderer/features/raytracing/raytrace_scene.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/resources/texture/image_export.hpp"
#include "engine/renderer/rhi/shader/compute_shader.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"

#include "engine/core/diagnostics/logger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Shard::Engine::Rendering::Raytracing {

    Raytracer::~Raytracer()
    {
        ReleaseGPUResources();
    }

    bool Raytracer::Start(const RenderSnapshot& scene, const CameraProxy& camera,
        const RaytraceSettings& settingsIn, const Filesystem::Path& outputPathIn)
    {
        settings = settingsIn;
        outputPath = outputPathIn;
        currentSample = 0;
        statusMessage.clear();

        if (!camera.valid)
        {
            statusMessage = "Missing world or camera.";
            status = RaytraceStatus::Failed;
            return false;
        }

        pendingScene = scene;
        pendingCamera = camera;
        hasPending = true;

        status = RaytraceStatus::Preparing;
        statusMessage = "Preparing render (building BVH)...";
        return true;
    }

    void Raytracer::BuildScene()
    {
        RenderSnapshot sceneSnapshot = std::move(pendingScene);
        CameraProxy camera = pendingCamera;
        pendingScene = RenderSnapshot();
        hasPending = false;

        // The world's own sky and flat ambient, so a ray that leaves the scene sees what the viewport draws
        skyCube = sceneSnapshot.environment.skybox ? sceneSnapshot.environment.skybox->GetCubemap() : nullptr;
        ambientIntensity = sceneSnapshot.environment.ambientIntensity;
        display = sceneSnapshot.environment.display;

        RaytraceScene scene = SceneBuilder::Build(sceneSnapshot);
        if (scene.trianglePositions.empty())
        {
            statusMessage = "Scene has no triangles to render.";
            status = RaytraceStatus::Failed;
            return;
        }

        IRenderContext* renderer = Core::GetEngine().GetRenderContext();

        bvhBuffer = StorageBuffer::Create((uint32_t)(scene.bvhNodes.size() * sizeof(BVHNode)));
        bvhBuffer->SetData(scene.bvhNodes.data(), (uint32_t)(scene.bvhNodes.size() * sizeof(BVHNode)));

        posBuffer = StorageBuffer::Create((uint32_t)(scene.trianglePositions.size() * sizeof(GPUTrianglePos)));
        posBuffer->SetData(scene.trianglePositions.data(), (uint32_t)(scene.trianglePositions.size() * sizeof(GPUTrianglePos)));

        attribBuffer = StorageBuffer::Create((uint32_t)(scene.triangleAttribs.size() * sizeof(GPUTriangleAttrib)));
        attribBuffer->SetData(scene.triangleAttribs.data(), (uint32_t)(scene.triangleAttribs.size() * sizeof(GPUTriangleAttrib)));

        matBuffer = StorageBuffer::Create((uint32_t)(scene.materials.size() * sizeof(GPUMaterial)));
        if (!scene.materials.empty())
            matBuffer->SetData(scene.materials.data(), (uint32_t)(scene.materials.size() * sizeof(GPUMaterial)));

        // Lights : snapshot LightManager's current light list into our own SSBO rather than reusing its
        // live buffer directly - keeps the raytracer decoupled from the real-time renderer's lifecycle.
        const auto& sceneLights = renderer->GetLightManager()->GetLights();
        flatLights.clear();
        flatLights.reserve(sceneLights.size());
        for (auto& light : sceneLights)
            if (light)
                flatLights.push_back(*light);

        lightBuffer = StorageBuffer::Create((uint32_t)(flatLights.size() * sizeof(LightData)));
        if (!flatLights.empty())
            lightBuffer->SetData(flatLights.data(), (uint32_t)(flatLights.size() * sizeof(LightData)));

        // ---- Output accumulation image ----
        TextureSpecifications outSpec;
        outSpec.width = settings.width;
        outSpec.height = settings.height;
        outSpec.internalFormat = TextureInternalFormat::RGBA32F;
        outSpec.generateMips = false;
        outSpec.immutableStorage = true;
        outputImage = Texture2D::Create(outSpec, nullptr);

        // ---- Compute shader / pipeline ----
        Filesystem::Path shaderPath = Core::GetEngine().GetFileManager()->GetEngineResRoot() / "shaders/compute/path_trace.comp";
        shader = ComputeShader::Create(shaderPath);
        if (!shader || !shaderPath.Exists())
        {
            statusMessage = "Failed to load path_trace.comp at " + shaderPath.full;
            status = RaytraceStatus::Failed;
            ReleaseGPUResources();
            return;
        }

        ComputePipelineSpecifications pipelineSpecs;
        pipelineSpecs.shader = shader;
        pipelineSpecs.debugName = "OfflineRaytracer";
        pipeline = renderer->GetOrAddComputePipeline(pipelineSpecs);

        // ---- Camera basis (world-space, pre-scaled so primary rays only need forward + right*x + up*y) ----
        camPos = camera.position;
        camForward = camera.forward;
        camRight = camera.right;
        camUp = camera.up;

        float aspect = (float)settings.width / (float)settings.height;
        float tanHalfFov = std::tan(glm::radians(camera.fovDegrees) * 0.5f);
        camRight *= tanHalfFov * aspect;
        camUp *= tanHalfFov;

        groupsX = (settings.width + 7) / 8;
        groupsY = (settings.height + 7) / 8;

        // Cap each dispatch to roughly kMaxGroupsPerDispatch workgroups (a full-width strip of rows) so
        // a single Update() call stays cheap regardless of image resolution - see the member comment on
        // rowsPerDispatch / tileGroupY.
        constexpr uint32_t kMaxGroupsPerDispatch = 512;
        rowsPerDispatch = std::max(1u, kMaxGroupsPerDispatch / std::max(groupsX, 1u));
        tileGroupY = 0;

        status = RaytraceStatus::Rendering;
        statusMessage = "Rendering...";
    }

    void Raytracer::Update()
    {
        // The scene build and the dispatches use the graphics API : they belong to the graphics thread, which the caller
        // (the editor's UI, on the main thread) parks on meanwhile
        OnGraphicsThread([this] { UpdateOnGraphicsThread(); });
    }

    void Raytracer::UpdateOnGraphicsThread()
    {
        // Deferred from Start() so a "Preparing..." frame gets presented before this blocking call - see
        // BuildScene()'s comment on the header. Runs exactly once, on the first Update() after Start().
        if (status == RaytraceStatus::Preparing)
        {
            BuildScene();
            return;
        }

        if (status != RaytraceStatus::Rendering)
            return;

        // Wall-clock, not a pure GPU timer : glDispatchCompute submits asynchronously, so this only
        // reflects real GPU cost once the driver's submission queue backs up enough to make the CPU wait
        // - but that's exactly the number that explains an observed editor stall, so it's more useful
        // here than a GPU-side query would be for diagnosing "why does this feel frozen".
        auto updateStart = std::chrono::steady_clock::now();

        IRenderContext* renderer = Core::GetEngine().GetRenderContext();

        pipeline->Bind();

        bvhBuffer->Bind(0);
        posBuffer->Bind(1);
        attribBuffer->Bind(2);
        matBuffer->Bind(3);
        lightBuffer->Bind(4);

        outputImage->BindImage(0, TextureAccess::ReadWrite);

        // The sky : a raw bind on a unit the forward pass never uses (see path_tracing.glsl's uSkyCube)
        const bool useSky = settings.useLevelSky && skyCube != nullptr;
        if (useSky)
            skyCube->Bind(45);
        shader->SetBool("uHasSky", useSky);
        shader->SetFloat("uAmbientIntensity", ambientIntensity);
        shader->SetFloat("uFireflyClamp", settings.fireflyClamp);

        shader->SetVec3("uCameraPos", camPos);
        shader->SetVec3("uCameraRight", camRight);
        shader->SetVec3("uCameraUp", camUp);
        shader->SetVec3("uCameraForward", camForward);
        shader->SetVec2("uImageSize", glm::vec2((float)settings.width, (float)settings.height));
        shader->SetInt("uFrameIndex", (int)currentSample);
        shader->SetInt("uLightCount", (int)flatLights.size());
        shader->SetInt("uMaxBounces", (int)settings.maxBounces);
        shader->SetVec3("uSkyColor", settings.skyColor);

        // Only dispatch a strip of rowsPerDispatch workgroup-rows, not the whole image, so this call
        // can't turn into a multi-second CPU stall (see rowsPerDispatch's declaration in raytracer.hpp).
        uint32_t rowsThisDispatch = std::min(rowsPerDispatch, groupsY - tileGroupY);
        shader->SetVec2("uTileOffset", glm::vec2(0.0f, (float)(tileGroupY * 8)));

        // ImageAccess : the next dispatch's imageLoad must see this dispatch's imageStore into the same
        // accumulation image (successive dispatches, not a single one - GL doesn't order these for us
        // without an explicit barrier).
        renderer->DispatchCompute(pipeline, groupsX, rowsThisDispatch, 1, MemoryBarrierBit::ImageAccess);

        tileGroupY += rowsThisDispatch;
        bool sampleComplete = tileGroupY >= groupsY;
        if (sampleComplete)
        {
            tileGroupY = 0;
            currentSample++;
        }

        double updateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count();
        char msgBuf[160];
        std::snprintf(msgBuf, sizeof(msgBuf), "Rendering... (%u / %u samples, %.2f ms/tile)",
            currentSample, settings.samplesPerPixel, updateMs);
        statusMessage = msgBuf;

        if (currentSample < settings.samplesPerPixel)
            return;

        // ---- Last sample : readback, normalize, export ----

        // TextureUpdate : makes this dispatch's image writes visible to the host-side ReadPixels below
        // (distinct from ImageAccess, which only orders GPU-side shader access).
        renderer->GetRendererAPI()->MemoryBarrier(MemoryBarrierBit::TextureUpdate);

        std::vector<float> raw(outputImage->GetPixelDataSize() / sizeof(float));
        outputImage->ReadPixels(raw.data(), raw.size() * sizeof(float));

        // uAccum stores (sum, sampleCount) per pixel, not an average (see path_trace.comp) - normalize
        // to a mean and drop the alpha/count channel before writing out a plain RGB .hdr file.
        std::vector<float> rgb((size_t)settings.width * settings.height * 3);
        for (size_t i = 0; i < (size_t)settings.width * settings.height; i++)
        {
            float count = std::max(raw[i * 4 + 3], 1.0f);
            rgb[i * 3 + 0] = raw[i * 4 + 0] / count;
            rgb[i * 3 + 1] = raw[i * 4 + 1] / count;
            rgb[i * 3 + 2] = raw[i * 4 + 2] / count;
        }

        bool success = ImageExport::WriteHDR(outputPath, settings.width, settings.height, 3, rgb.data());

        if (success && settings.writePreview)
        {
            std::vector<uint8_t> preview(rgb.size());
            for (size_t i = 0; i + 2 < rgb.size(); i += 3)
            {
                const glm::vec3 shown = DisplayTransform(glm::vec3(rgb[i], rgb[i + 1], rgb[i + 2]), display);
                for (int c = 0; c < 3; c++)
                    preview[i + c] = (uint8_t)std::lround(shown[c] * 255.0f);
            }

            const std::string previewPath = std::filesystem::path(outputPath.full).replace_extension(".png").generic_string();
            ImageExport::WritePNG(Filesystem::Path(previewPath, true), settings.width, settings.height, 3, preview.data());
        }

        Finish(success);
    }

    void Raytracer::Cancel()
    {
        if (!IsActive())
            return;

        status = RaytraceStatus::Cancelled;
        statusMessage = "Cancelled after " + std::to_string(currentSample) + " / " + std::to_string(settings.samplesPerPixel) + " samples.";
        pendingScene = RenderSnapshot();
        pendingCamera = CameraProxy();
        hasPending = false;
        ReleaseGPUResources();
    }

    void Raytracer::Finish(bool success)
    {
        status = success ? RaytraceStatus::Completed : RaytraceStatus::Failed;
        statusMessage = success ? ("Saved to " + outputPath.full) : "Failed to write output file.";
        ReleaseGPUResources();
    }

    void Raytracer::ReleaseGPUResources()
    {
        bvhBuffer.reset();
        posBuffer.reset();
        attribBuffer.reset();
        matBuffer.reset();
        lightBuffer.reset();
        outputImage.reset();
        skyCube.reset();
    }

    float Raytracer::GetProgress() const
    {
        if (settings.samplesPerPixel == 0)
            return 0.0f;
        float sampleFrac = groupsY > 0 ? (float)tileGroupY / (float)groupsY : 0.0f;
        return std::min(1.0f, ((float)currentSample + sampleFrac) / (float)settings.samplesPerPixel);
    }

}
