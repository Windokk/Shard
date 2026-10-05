#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/filesystem/filesystem.hpp"

#include "engine/renderer/features/lighting/light_manager.hpp"

namespace Shard::Engine::Levels {
    class Level;
}

namespace Shard::Engine::Objects::Components {
    class Camera;
}

namespace Shard::Engine::Rendering {
    class StorageBuffer;
    class Texture2D;
    class ComputeShader;
    class ComputePipeline;
}

namespace Shard::Engine::Rendering::Raytracing {

    struct RaytraceSettings
    {
        uint32_t width = 800;
        uint32_t height = 600;
        uint32_t samplesPerPixel = 256;
        uint32_t maxBounces = 6;
        glm::vec3 skyColor = glm::vec3(0.05f, 0.07f, 0.1f);
    };

    enum class RaytraceStatus
    {
        Idle,
        Preparing,
        Rendering,
        Completed,
        Failed,
        Cancelled
    };

    // Incremental offline path tracer : unlike a single blocking call, this dispatches ONE accumulated
    // sample per Update() call. The intended usage is to call Update() once per editor frame while
    // IsRendering() is true (see ViewportWindow::Draw()) - since a single sample's compute dispatch is
    // fast, control returns to the caller (and thus to ImGui / the rest of the frame) immediately after,
    // keeping the editor responsive for the whole render instead of freezing it.
    //
    // This intentionally stays on the calling (main) thread rather than using a real background thread :
    // the engine has a single OpenGL context and several non-thread-safe singletons (ResourcesManager,
    // AssetIDManager, GLStateCache, ...), none of which are safe to touch from a second thread without a
    // shared GL context and a much larger synchronization effort. Frame-slicing the work is the
    // correct fit for this engine's architecture - "background" here means "non-blocking to the editor
    // UI", not "on another OS thread".
    class Raytracer
    {
        public:
            ~Raytracer();

            // Validates the level/camera and transitions to Preparing. This is intentionally cheap and
            // does NOT build the scene/BVH or touch the GPU yet - that heavy one-off work (still a single
            // blocking call internally) is deferred to the first Update() call after Start(), so the
            // caller gets a chance to render+present a "Preparing..." frame before the BVH-build stall
            // happens, instead of freezing before any feedback is visible at all. Returns false
            // immediately (check GetStatusMessage()) if the level/camera are missing.
            bool Start(Levels::Level* level, const std::shared_ptr<Objects::Components::Camera>& camera,
                const RaytraceSettings& settings, const Filesystem::Path& outputPath);

            // No-op unless IsActive(). While Preparing, performs the one-off scene/BVH build + GPU upload
            // and transitions to Rendering (or Failed). While Rendering, dispatches one tile's worth of
            // work for the current sample; on the tile that completes the last sample, also performs the
            // readback, normalization and file export, and transitions to Completed/Failed.
            void Update();

            // Stops early without exporting a file. No-op unless IsActive().
            void Cancel();

            bool IsPreparing() const { return status == RaytraceStatus::Preparing; }
            bool IsRendering() const { return status == RaytraceStatus::Rendering; }
            bool IsActive() const { return IsPreparing() || IsRendering(); }
            RaytraceStatus GetStatus() const { return status; }

            // In [0, 1], based on samples dispatched so far vs. settings.samplesPerPixel.
            float GetProgress() const;

            const std::string& GetStatusMessage() const { return statusMessage; }

        private:
            // The deferred heavy part of Start() : scene extraction + BVH build + GPU upload. Runs once,
            // on the first Update() call while Preparing.
            void BuildScene();

            void Finish(bool success);
            void ReleaseGPUResources();

            RaytraceStatus status = RaytraceStatus::Idle;
            RaytraceSettings settings;
            Filesystem::Path outputPath;
            std::string statusMessage;

            // Captured by Start(), consumed by BuildScene() on the next Update() call. Level is a raw,
            // non-owning pointer (same lifetime assumption as the old single-call Start() had).
            Levels::Level* pendingLevel = nullptr;
            std::shared_ptr<Objects::Components::Camera> pendingCamera;

            uint32_t currentSample = 0;

            // Progressive dispatch is tiled : each Update() call only computes a horizontal strip of the
            // current sample (rowsPerDispatch workgroup-rows out of groupsY), not the whole image. This
            // bounds how much GPU work a single Update() call can enqueue, so one call can't turn into a
            // multi-second CPU stall once the driver's command queue backs up (see Update()'s comments).
            uint32_t tileGroupY = 0;
            uint32_t rowsPerDispatch = 1;

            std::vector<LightData> flatLights;

            std::shared_ptr<StorageBuffer> bvhBuffer;
            std::shared_ptr<StorageBuffer> posBuffer;
            std::shared_ptr<StorageBuffer> attribBuffer;
            std::shared_ptr<StorageBuffer> matBuffer;
            std::shared_ptr<StorageBuffer> lightBuffer;
            std::shared_ptr<Texture2D> outputImage;
            std::shared_ptr<ComputeShader> shader;
            std::shared_ptr<ComputePipeline> pipeline;

            glm::vec3 camPos{ 0.0f };
            glm::vec3 camRight{ 0.0f };
            glm::vec3 camUp{ 0.0f };
            glm::vec3 camForward{ 0.0f };
            uint32_t groupsX = 0;
            uint32_t groupsY = 0;
    };

}
