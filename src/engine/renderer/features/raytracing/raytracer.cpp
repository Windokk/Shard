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

        skyCube = sceneSnapshot.environment.skybox ? sceneSnapshot.environment.skybox->GetCubemap() : nullptr;
        ambientColor = sceneSnapshot.environment.ambientColor;
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

        const auto& sceneLights = renderer->GetLightManager()->GetLights();
        flatLights.clear();
        flatLights.reserve(sceneLights.size());
        for (auto& light : sceneLights)
            if (light)
                flatLights.push_back(*light);

        lightBuffer = StorageBuffer::Create((uint32_t)(flatLights.size() * sizeof(LightData)));
        if (!flatLights.empty())
            lightBuffer->SetData(flatLights.data(), (uint32_t)(flatLights.size() * sizeof(LightData)));

        TextureSpecifications outSpec;
        outSpec.width = settings.width;
        outSpec.height = settings.height;
        outSpec.internalFormat = TextureInternalFormat::RGBA32F;
        outSpec.generateMips = false;
        outSpec.immutableStorage = true;
        outputImage = Texture2D::Create(outSpec, nullptr);
        if (settings.writeTerms)
            for (auto& image : termImages)
                image = Texture2D::Create(outSpec, nullptr);

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

        constexpr uint32_t kMaxGroupsPerDispatch = 512;
        rowsPerDispatch = std::max(1u, kMaxGroupsPerDispatch / std::max(groupsX, 1u));
        tileGroupY = 0;

        status = RaytraceStatus::Rendering;
        statusMessage = "Rendering...";
    }

    void Raytracer::Update()
    {
        OnGraphicsThread([this] { UpdateOnGraphicsThread(); });
    }

    void Raytracer::UpdateOnGraphicsThread()
    {
        if (status == RaytraceStatus::Preparing)
        {
            BuildScene();
            return;
        }

        if (status != RaytraceStatus::Rendering)
            return;

        auto updateStart = std::chrono::steady_clock::now();

        IRenderContext* renderer = Core::GetEngine().GetRenderContext();

        pipeline->Bind();

        bvhBuffer->Bind(0);
        posBuffer->Bind(1);
        attribBuffer->Bind(2);
        matBuffer->Bind(3);
        lightBuffer->Bind(4);

        outputImage->BindImage(0, TextureAccess::ReadWrite);
        shader->SetBool("uWriteTerms", settings.writeTerms);
        // The shader declares the term images even when it does not write them : Vulkan needs a valid RGBA32F image on each unit.
        for (uint32_t t = 0; t < 4; t++)
            (settings.writeTerms ? termImages[t] : outputImage)->BindImage(1 + t, TextureAccess::ReadWrite);

        const bool useSky = settings.useLevelSky && skyCube != nullptr;
        if (useSky)
            skyCube->Bind(45);
        shader->SetBool("uHasSky", useSky);
        shader->SetVec3("uAmbientColor", ambientColor);
        shader->SetFloat("uFireflyClamp", settings.fireflyClamp);

        shader->SetVec3("uCameraPos", camPos);
        shader->SetVec3("uCameraRight", camRight);
        shader->SetVec3("uCameraUp", camUp);
        shader->SetVec3("uCameraForward", camForward);
        shader->SetBool("uPanorama", settings.panoramic);
        shader->SetVec2("uImageSize", glm::vec2((float)settings.width, (float)settings.height));
        shader->SetInt("uFrameIndex", (int)currentSample);
        shader->SetInt("uLightCount", (int)flatLights.size());
        shader->SetInt("uMaxBounces", (int)settings.maxBounces);
        shader->SetVec3("uSkyColor", settings.skyColor);

        // Dispatch only a strip of rows so one call cannot stall the CPU for seconds.
        uint32_t rowsThisDispatch = std::min(rowsPerDispatch, groupsY - tileGroupY);
        shader->SetVec2("uTileOffset", glm::vec2(0.0f, (float)(tileGroupY * 8)));

        // Next dispatch's imageLoad must see this dispatch's imageStore, GL does not order them.
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

        // TextureUpdate makes the image writes visible to the host ReadPixels.
        renderer->GetRendererAPI()->MemoryBarrier(MemoryBarrierBit::TextureUpdate);

        std::vector<float> raw(outputImage->GetPixelDataSize() / sizeof(float));
        outputImage->ReadPixels(raw.data(), raw.size() * sizeof(float));

        // uAccum holds (sum, count) per pixel, normalize to a mean before writing the RGB .hdr.
        std::vector<float> rgb((size_t)settings.width * settings.height * 3);
        for (size_t i = 0; i < (size_t)settings.width * settings.height; i++)
        {
            float count = std::max(raw[i * 4 + 3], 1.0f);
            rgb[i * 3 + 0] = raw[i * 4 + 0] / count;
            rgb[i * 3 + 1] = raw[i * 4 + 1] / count;
            rgb[i * 3 + 2] = raw[i * 4 + 2] / count;
        }

        bool success = ImageExport::WriteHDR(outputPath, settings.width, settings.height, 3, rgb.data());

        if (success && settings.writeTerms)
        {
            std::vector<float> term(raw.size());
            std::vector<float> termRGB(rgb.size());
            for (uint32_t t = 0; t < 4 && success; t++)
            {
                termImages[t]->ReadPixels(term.data(), term.size() * sizeof(float));
                for (size_t i = 0; i < (size_t)settings.width * settings.height; i++)
                {
                    float count = std::max(raw[i * 4 + 3], 1.0f);
                    for (size_t c = 0; c < 3; c++)
                        termRGB[i * 3 + c] = term[i * 4 + c] / count;
                }

                const std::filesystem::path picture(outputPath.full);
                const std::string termPath = (picture.parent_path() / (picture.stem().string() + "." + LightingTermName(kLightingTerms[t]) + ".hdr")).generic_string();
                success = ImageExport::WriteHDR(Filesystem::Path(termPath), settings.width, settings.height, 3, termRGB.data());
            }
        }

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
        for (auto& image : termImages)
            image.reset();
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
