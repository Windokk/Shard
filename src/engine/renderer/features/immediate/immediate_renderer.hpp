#pragma once

#include "engine/renderer/rhi/render_view.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class StorageBuffer;
    class Pipeline;
    class Material;
    class EnvironmentMap;
    class ImmediateRenderer;

    enum class ImmediateLighting {
        Unlit,
        Studio
    };

    struct ImmediateDesc {
        RenderView view;

        uint32_t width = 256;
        uint32_t height = 256;

        ImmediateLighting lighting = ImmediateLighting::Studio;

        glm::vec4 clearColor = glm::vec4(0.145f, 0.145f, 0.157f, 1.0f);

        bool multisample = true;

        glm::vec4 unlitColor = glm::vec4(1.0f);
    };

    class ImmediateTarget {
        public:
            ImmediateTarget() = default;
            ~ImmediateTarget() { Release(); }

            ImmediateTarget(const ImmediateTarget&) = delete;
            ImmediateTarget& operator=(const ImmediateTarget&) = delete;

            ImmediateTarget(ImmediateTarget&& other) noexcept { *this = std::move(other); }
            ImmediateTarget& operator=(ImmediateTarget&& other) noexcept;

            bool IsValid() const { return m_Framebuffer != nullptr; }

            uint32_t GetTexture() const { return m_Framebuffer ? m_Framebuffer->GetResolveColorAttachment() : 0; }

            const std::shared_ptr<Framebuffer>& GetFramebuffer() const { return m_Framebuffer; }

            uint32_t GetWidth() const { return m_Framebuffer ? m_Framebuffer->GetWidth() : 0; }
            uint32_t GetHeight() const { return m_Framebuffer ? m_Framebuffer->GetHeight() : 0; }

            void Release();

        private:
            friend class ImmediateRenderer;

            ImmediateRenderer* m_Owner = nullptr;
            std::shared_ptr<Framebuffer> m_Framebuffer;
    };

    // Main thread only, between Renderer::Render() and the UI draw. Invalidates the API state cache on return.
    class ImmediateRenderer {
        public:
            void Init(IRenderContext* renderer);

            void Shutdown();

            ImmediateTarget Render(const ImmediateDesc& desc, const std::vector<DrawCommand>& commands);

            bool ReadPixels(const ImmediateTarget& target, std::vector<uint8_t>& outPixels);

            void CopyTo(const ImmediateTarget& target, Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight);

            static RenderView FrameBounds(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float aspect = 1.0f, float fovDegrees = 35.0f);

        private:
            friend class ImmediateTarget;

            struct PoolEntry {
                std::shared_ptr<Framebuffer> framebuffer;
                bool inUse = false;
            };

            static constexpr size_t kMaxPooledTargets = 8;

            std::shared_ptr<Framebuffer> AcquireTarget(uint32_t width, uint32_t height, bool multisample);
            void ReleaseTarget(const std::shared_ptr<Framebuffer>& framebuffer);

            void EnsureStudioRig();
            void EnsureUnlitMaterial();
            void EnsureNeutralEnvironment();

            IRenderContext* m_Renderer = nullptr;
            bool m_Rendering = false;

            std::vector<PoolEntry> m_Pool;

            static constexpr int kStudioLightCount = 3;
            static constexpr float kStudioAmbient = 0.25f;
            std::shared_ptr<StorageBuffer> m_StudioLights;
            std::shared_ptr<StorageBuffer> m_EmptyClusterGrid;
            std::shared_ptr<StorageBuffer> m_EmptyClusterIndices;

            std::shared_ptr<EnvironmentMap> m_NeutralEnvironment;

            std::shared_ptr<Pipeline> m_UnlitPipeline;
            std::shared_ptr<Material> m_UnlitMaterial;
    };
}
