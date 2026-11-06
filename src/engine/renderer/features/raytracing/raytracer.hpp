#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/assets/vfs/filesystem.hpp"

#include "engine/renderer/features/lighting/light_manager.hpp"
#include "engine/renderer/rhi/display_transform.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Rendering {
    class StorageBuffer;
    class Texture2D;
    class Cubemap;
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

        bool useLevelSky = true;
        glm::vec3 skyColor = glm::vec3(0.05f, 0.07f, 0.1f);

        float fireflyClamp = 0.0f;

        bool panoramic = false;

        bool writePreview = true;

        bool writeTerms = false;
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

    // One accumulated sample per Update(), on the main thread: single GL context and non-thread-safe singletons.
    class Raytracer
    {
        public:
            ~Raytracer();

            // Cheap on purpose. The BVH build is deferred to the first Update() so a "Preparing..." frame gets presented.
            bool Start(const RenderSnapshot& scene, const CameraProxy& camera,
                const RaytraceSettings& settings, const Filesystem::Path& outputPath);

            void Update();

            void Cancel();

            bool IsPreparing() const { return status == RaytraceStatus::Preparing; }
            bool IsRendering() const { return status == RaytraceStatus::Rendering; }
            bool IsActive() const { return IsPreparing() || IsRendering(); }
            RaytraceStatus GetStatus() const { return status; }

            float GetProgress() const;

            const std::string& GetStatusMessage() const { return statusMessage; }

        private:
            void BuildScene();

            void Finish(bool success);
            void ReleaseGPUResources();

            void UpdateOnGraphicsThread();

            RaytraceStatus status = RaytraceStatus::Idle;
            RaytraceSettings settings;
            Filesystem::Path outputPath;
            std::string statusMessage;

            RenderSnapshot pendingScene;
            CameraProxy pendingCamera;
            bool hasPending = false;

            std::shared_ptr<Cubemap> skyCube;
            glm::vec3 ambientColor = glm::vec3(0.0f);
            DisplaySettings display;

            uint32_t currentSample = 0;

            uint32_t tileGroupY = 0;
            uint32_t rowsPerDispatch = 1;

            std::vector<LightData> flatLights;

            std::shared_ptr<StorageBuffer> bvhBuffer;
            std::shared_ptr<StorageBuffer> posBuffer;
            std::shared_ptr<StorageBuffer> attribBuffer;
            std::shared_ptr<StorageBuffer> matBuffer;
            std::shared_ptr<StorageBuffer> lightBuffer;
            std::shared_ptr<Texture2D> outputImage;
            std::shared_ptr<Texture2D> termImages[4];
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
