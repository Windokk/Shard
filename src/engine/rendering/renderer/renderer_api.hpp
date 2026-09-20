#pragma once

#include <memory>

#include <cstdint>

#include <type_traits>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    class DrawCommand;
    class Mesh;
    class Material;
    class Pipeline;
    class ComputePipeline;
    class RenderPass;

    enum class ClearBit : uint32_t
    {
        None  = 0,
        Color = 1 << 0,
        Depth = 1 << 1,
        Stencil = 1 << 2
    };

    inline ClearBit operator|(ClearBit a, ClearBit b)
    {
        using T = std::underlying_type_t<ClearBit>;
        return static_cast<ClearBit>(static_cast<T>(a) | static_cast<T>(b));
    }

    inline ClearBit operator&(ClearBit a, ClearBit b)
    {
        using T = std::underlying_type_t<ClearBit>;
        return static_cast<ClearBit>(static_cast<T>(a) & static_cast<T>(b));
    }

    inline ClearBit& operator|=(ClearBit& a, ClearBit b)
    {
        a = a | b;
        return a;
    }

    enum class MemoryBarrierBit : uint32_t
    {
        None             = 0,
        ShaderStorage    = 1 << 0, // SSBO writes (glMemoryBarrier: GL_SHADER_STORAGE_BARRIER_BIT)
        ImageAccess      = 1 << 1, // image load/store writes (GL_SHADER_IMAGE_ACCESS_BARRIER_BIT)
        TextureFetch     = 1 << 2, // subsequent sampling of a written image/texture from another shader (GL_TEXTURE_FETCH_BARRIER_BIT)
        BufferUpdate     = 1 << 3, // glBufferData/glBufferSubData/glGetBufferSubData (StorageBuffer::GetData) after a compute write (GL_BUFFER_UPDATE_BARRIER_BIT)
        VertexAttribArray = 1 << 4, // vertex/index buffers written by compute, read by a draw (GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT)
        TextureUpdate    = 1 << 5, // glGetTexImage (Texture2D::ReadPixels) / glTexSubImage* after an image-load-store write (GL_TEXTURE_UPDATE_BARRIER_BIT) - distinct from TextureFetch, which is for shader sampling, not host readback
        All              = 0xFFFFFFFF
    };

    inline MemoryBarrierBit operator|(MemoryBarrierBit a, MemoryBarrierBit b)
    {
        using T = std::underlying_type_t<MemoryBarrierBit>;
        return static_cast<MemoryBarrierBit>(static_cast<T>(a) | static_cast<T>(b));
    }

    inline MemoryBarrierBit operator&(MemoryBarrierBit a, MemoryBarrierBit b)
    {
        using T = std::underlying_type_t<MemoryBarrierBit>;
        return static_cast<MemoryBarrierBit>(static_cast<T>(a) & static_cast<T>(b));
    }

    inline MemoryBarrierBit& operator|=(MemoryBarrierBit& a, MemoryBarrierBit b)
    {
        a = a | b;
        return a;
    }

    struct ComputeLimits
    {
        uint32_t maxWorkGroupCount[3] = { 0, 0, 0 };      // GL_MAX_COMPUTE_WORK_GROUP_COUNT per dimension
        uint32_t maxWorkGroupSize[3]  = { 0, 0, 0 };      // GL_MAX_COMPUTE_WORK_GROUP_SIZE per dimension
        uint32_t maxWorkGroupInvocations = 0;             // GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS (product of local size)
        uint32_t maxSharedMemorySize = 0;                 // GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, in bytes
    };

    /// Values are mirrored by lit.frag's `viewMode` uniform - keep in sync.
    enum class ViewMode : int
    {
        Lit = 0,
        Unlit = 1,
        Wireframe = 2,
        ShadedWireframe = 3,
        Normals = 4,
        Depth = 5,
        UVs = 6
    };

    struct DebugViewState
    {
        ViewMode mode = ViewMode::Lit;
        bool showLighting = true;
        bool showShadows = true;
        /// Rasterize every draw as lines (with a small depth bias so it can overlay a filled sweep).
        bool wireframeRaster = false;
    };

    class RendererAPI{
        public: 
            enum class API : uint32_t
            {
                OpenGL = 0,
                Vulkan,
                DX11,
                DX12
            };

            virtual ~RendererAPI() = default;

            /// @brief Editor-style viewport debug view, applied to the main scene view only (never to
            /// studio/immediate views). Set by the Renderer around the forward pass.
            virtual void SetDebugView(const DebugViewState& state) { m_DebugView = state; }
            const DebugViewState& GetDebugView() const { return m_DebugView; }

            virtual void ExecuteDrawCommand(const DrawCommand& command, const std::shared_ptr<RenderPass> pass) = 0;

            virtual void ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) = 0;

            virtual void MemoryBarrier(MemoryBarrierBit barriers) = 0;

            virtual const ComputeLimits& GetComputeLimits() = 0;

            virtual void ToggleMultisampling(const bool on) = 0;

            static std::shared_ptr<RendererAPI> Create(API api);

            const API GetAPI() const { return api; }

            virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

            virtual void SetClearColor(float r, float g, float b, float a) = 0;

            /// @brief The clear color currently set on the API (whoever set it, including raw calls).
            virtual glm::vec4 GetClearColor() = 0;

            virtual void Clear(ClearBit clearBits) = 0;

            /// @brief Forget every piece of API state the backend caches to skip redundant calls. Call after
            /// touching the API outside the normal draw path (immediate renders, raw calls, ...).
            virtual void InvalidateStateCache() = 0;

            virtual std::string GetDeviceVendor() = 0;
            virtual std::string GetRendererName() = 0;
            virtual std::string GetDriverVersion() = 0;

        protected:
            API api;
            DebugViewState m_DebugView;
    };

}