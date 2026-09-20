#pragma once

#include "engine/rendering/renderer/render_view.hpp"
#include "engine/rendering/framebuffer/framebuffer.hpp"
#include "engine/rendering/mesh/mesh.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace Shard::Engine::Rendering {

    class Renderer;
    class StorageBuffer;
    class Pipeline;
    class Material;
    class EnvironmentMap;
    class ImmediateRenderer;

    enum class ImmediateLighting {
        /// Every surface drawn as one flat color (ImmediateDesc::unlitColor), whatever its material.
        Unlit,
        /// Real materials lit by a fixed 3-light studio rig, independent of the level (see ViewLighting::Studio).
        Studio
    };

    struct ImmediateDesc {
        /// Only the matrices, position and orthographic flag are read - the lighting fields are filled in
        /// by the renderer from `lighting`.
        RenderView view;

        uint32_t width = 256;
        uint32_t height = 256;

        ImmediateLighting lighting = ImmediateLighting::Studio;

        /// Use alpha 0 for a transparent background.
        glm::vec4 clearColor = glm::vec4(0.145f, 0.145f, 0.157f, 1.0f);

        /// 4x MSAA, resolved before Render() returns.
        bool multisample = true;

        /// Unlit mode only.
        glm::vec4 unlitColor = glm::vec4(1.0f);
    };

    /// @brief A pooled render target on loan from the ImmediateRenderer. Move-only : the target goes back
    /// to the pool when the lease is destroyed or Release()d, after which its texture must not be used.
    class ImmediateTarget {
        public:
            ImmediateTarget() = default;
            ~ImmediateTarget() { Release(); }

            ImmediateTarget(const ImmediateTarget&) = delete;
            ImmediateTarget& operator=(const ImmediateTarget&) = delete;

            ImmediateTarget(ImmediateTarget&& other) noexcept { *this = std::move(other); }
            ImmediateTarget& operator=(ImmediateTarget&& other) noexcept;

            bool IsValid() const { return m_Framebuffer != nullptr; }

            /// Resolved color texture (GL texture id) - display it with the usual V flip.
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

    /// @brief "Render this now" : draws a transient list of draw commands from a given view into a pooled
    /// offscreen target, synchronously, without touching the retained pass graph or the active camera.
    /// Meant for thumbnails, scene captures and previews.
    ///
    /// Rules :
    ///  - Main thread only (single GL context, non-thread-safe singletons).
    ///  - Call it between frames (after Renderer::Render(), before the UI is drawn) : it reuses global
    ///    per-frame state and leaves the API state cache invalidated when it returns.
    ///  - Commands need a material (Studio) and are never frustum culled.
    class ImmediateRenderer {
        public:
            void Init(Renderer* renderer);

            /// @brief Frees every pooled target. Leases still alive must not be used afterwards.
            void Shutdown();

            /// @return an invalid target if the render could not be done (bad size, re-entrant call, ...)
            ImmediateTarget Render(const ImmediateDesc& desc, const std::vector<DrawCommand>& commands);

            /// @brief Blocking readback of a target as RGBA8, top row first.
            bool ReadPixels(const ImmediateTarget& target, std::vector<uint8_t>& outPixels);

            /// @brief Copies (and rescales) a target into a rectangle of a color framebuffer, e.g. an atlas.
            void CopyTo(const ImmediateTarget& target, Framebuffer& dst, uint32_t dstX, uint32_t dstY, uint32_t dstWidth, uint32_t dstHeight);

            /// @brief A perspective view that frames a bounding box from a fixed 3/4 "product shot" angle
            /// (the same direction for every object, so a grid of thumbnails reads consistently).
            /// @param boundsMin / boundsMax World-space bounds of what should fill the frame.
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

            Renderer* m_Renderer = nullptr;
            bool m_Rendering = false;

            std::vector<PoolEntry> m_Pool;

            // Studio rig : the light buffer replacing the scene's at slot 0, plus an all-empty cluster
            // grid / index list replacing the scene's at slots 2/3 (a single cluster with no lights).
            static constexpr int kStudioLightCount = 3;
            static constexpr float kStudioAmbient = 0.25f;
            std::shared_ptr<StorageBuffer> m_StudioLights;
            std::shared_ptr<StorageBuffer> m_EmptyClusterGrid;
            std::shared_ptr<StorageBuffer> m_EmptyClusterIndices;

            // Soft grey sky-to-ground gradient : the image-based lighting of every Studio render, so a
            // thumbnail never depends on which level's skybox happens to be loaded.
            std::shared_ptr<EnvironmentMap> m_NeutralEnvironment;

            std::shared_ptr<Pipeline> m_UnlitPipeline;
            std::shared_ptr<Material> m_UnlitMaterial;
    };
}
