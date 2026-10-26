#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <cstdint>

#include <type_traits>

#include <glm/glm.hpp>

#include "engine/renderer/rhi/lighting_terms.hpp"

namespace Shard::Engine::Rendering {

    class DrawCommand;
    class Mesh;
    class Material;
    class Pipeline;
    class ComputePipeline;
    struct RenderPass;      // a struct (render_pass.hpp) : MSVC mangles class and struct differently
    struct OverlayProxy;    // render_snapshot.hpp

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
        Command          = 1 << 6, // indirect draw/dispatch arguments written by compute, consumed by a draw (GL_COMMAND_BARRIER_BIT)
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
        UVs = 6,
        GlobalIllumination = 7,
        SSAO = 8,
        LightmapUVs = 9,
        ShadowCascades = 10
    };

    struct DebugViewState
    {
        ViewMode mode = ViewMode::Lit;
        bool showLighting = true;
        bool showShadows = true;
        /// Rasterize every draw as lines (with a small depth bias so it can overlay a filled sweep).
        bool wireframeRaster = false;
        /// The terms of the lighting the lit view draws (LightingTerm bits, see lighting_terms.hpp) : render.lighting_terms
        uint32_t lightingTerms = LightingTermsAll;
    };

    /// What a sweep over a pass's draw list is for : an opaque pass can lay its depth down first (Prepass : depth only, cheap
    /// vertex work, no shading) and then shade (Equal : only what is visible, depth test EQUAL), so every pixel is shaded once.
    enum class DepthSweep : uint8_t { None = 0, Prepass, Equal };

    class Shader;
    class GpuProfiler;
    struct RenderView;

    class RendererAPI;

    /// @brief What the backend asks of the frontend while it issues a draw : the scene-wide shader inputs (lights,
    /// probes, environment, shadows...) are the frontend's business, the backend only knows when to ask for them.
    class ISceneBinding{
        public:
            virtual ~ISceneBinding() = default;

            /// Sets the per-object and scene-wide inputs of a lit draw. `applyPassGlobals` : the shader has not
            /// received this frame's scene-wide inputs yet.
            virtual void BindWorldState(RendererAPI& api, std::shared_ptr<Shader> shader, glm::mat4 modelMatrix, int objectID, bool applyPassGlobals) = 0;

            /// What the scene needs bound on a material (shadow maps, SSAO...)
            virtual void BindMaterialScene(std::shared_ptr<Material> material) = 0;

            /// The view the draws are currently issued from. False when there is none.
            virtual bool GetCurrentView(RenderView& out) = 0;
    };

    /// @brief What a graphics backend asks of the window it presents on. The window belongs to the platform layer : whoever creates both
    /// (the engine) fills this in before the renderer is created, the backend only calls it.
    struct WindowHooks
    {
        /// The instance extensions the window's surface needs (Vulkan)
        std::function<std::vector<std::string>()> vulkanInstanceExtensions;

        /// Makes the surface of the window on a Vulkan instance (the handle of the surface, 0 on failure)
        std::function<uint64_t(void* vulkanInstance)> createVulkanSurface;

        int width = 1280;
        int height = 720;
        bool vsync = true;
        bool validation = false;

        /// Whether the vertical sync is wanted right now (Vulkan : polled each frame, the swapchain is rebuilt when it changes). Empty : `vsync` stays.
        std::function<bool()> vsyncWanted;
    };

    /// @brief What an application that draws its own user interface on the Vulkan backend (the editor : Dear ImGui) needs to set its renderer up.
    /// The handles are Vulkan's, as opaque pointers : the engine's headers stay free of the Vulkan ones.
    struct UIBackendInfo
    {
        void* instance = nullptr;               ///< VkInstance
        void* physicalDevice = nullptr;         ///< VkPhysicalDevice
        void* device = nullptr;                 ///< VkDevice
        void* queue = nullptr;                  ///< VkQueue (the one the frames are submitted on)
        uint32_t queueFamily = 0;
        uint32_t apiVersion = 0;
        uint32_t imageCount = 0;                ///< images of the swapchain
        uint32_t swapchainFormat = 0;           ///< VkFormat the UI is drawn in
    };

    /// What the user interface's renderer does with a texture it is asked to show : turns the (sampler, view, layout) of a Vulkan image into
    /// the id it draws it by (ImGui_ImplVulkan_AddTexture), and gives that id back
    struct UITextureHooks
    {
        std::function<uint64_t(void* sampler, void* imageView, uint32_t imageLayout)> add;
        std::function<void(uint64_t id)> remove;
    };

    /// Draws the user interface on the swapchain image of the frame, between its clear and its presentation : the command buffer (a VkCommandBuffer)
    /// is in a dynamic rendering pass on that image
    using UIDrawCallback = std::function<void(void* commandBuffer)>;

    class RendererAPI;

    /// A named stretch of the commands of the current backend (see RendererAPI::PushDebugGroup)
    class DebugScope
    {
        public:
            DebugScope(RendererAPI* api, const char* name);
            ~DebugScope();
            DebugScope(const DebugScope&) = delete;
            DebugScope& operator=(const DebugScope&) = delete;

        private:
            RendererAPI* m_Api;
    };

    class RendererAPI{
        public: 
            enum class API : uint32_t
            {
                OpenGL = 0,
                Vulkan,
                DX11,
                DX12,
                /// No graphics API : everything exists, nothing is drawn (headless runs, see backends/null)
                Null
            };

            virtual ~RendererAPI() = default;

            /// @brief Editor-style viewport debug view, applied to the main scene view only (never to
            /// studio/immediate views). Set by the Renderer around the forward pass.
            virtual void SetDebugView(const DebugViewState& state) { m_DebugView = state; }
            const DebugViewState& GetDebugView() const { return m_DebugView; }

            /// Draws what a plugin's overlay recorded (2D geometry, scissors, clears : shard.render) into the framebuffer that is bound, of
            /// `width` x `height` pixels. A backend that cannot draws nothing. Graphics thread.
            virtual void DrawOverlay(const OverlayProxy& overlay, uint32_t width, uint32_t height) { (void)overlay; (void)width; (void)height; }
            /// Gives back the buffers an OverlayGeometry made (its `gpu` array). Graphics thread.
            virtual void ReleaseOverlayGeometry(const uint32_t gpu[3]) { (void)gpu; }

            /// A frame of the renderer begins / ends : a backend that records commands submits them at the end
            virtual void BeginFrame() {}
            virtual void EndFrame() {}

            /// Names a stretch of the commands (a pass, a compute step) for the tools that read a capture of the frame (RenderDoc, the
            /// validation layers). Nests. Nothing for a backend that has no such thing.
            virtual void PushDebugGroup(const char* name) { (void)name; }
            virtual void PopDebugGroup() {}

            static void SetWindowHooks(const WindowHooks& hooks);
            static const WindowHooks& GetWindowHooks();

            // ---- The user interface of an application on the Vulkan backend ----
            // The OpenGL one draws its interface itself on the window's framebuffer, there is nothing to ask of the backend

            /// What the UI's renderer is set up with. False on a backend that has no such need.
            virtual bool GetUIBackendInfo(UIBackendInfo& out) { (void)out; return false; }

            /// Where the UI's renderer registers the textures it shows (see GetUITextureID)
            virtual void SetUITextureHooks(const UITextureHooks& hooks) { (void)hooks; }

            /// The id the UI draws a texture by (the texture is named by its GetHandle()). OpenGL : the handle itself.
            virtual uint64_t GetUITextureID(uint32_t handle) { return handle; }

            /// Draws the UI as the frame's picture : acquires the swapchain image (of the size of the window), clears it, calls `draw`, presents.
            /// False when there is no picture this time (a minimised window, a swapchain being rebuilt).
            virtual bool PresentUI(const UIDrawCallback& draw, uint32_t windowWidth, uint32_t windowHeight, const float clearColor[4]) { (void)draw; (void)windowWidth; (void)windowHeight; (void)clearColor; return false; }

            virtual void ExecuteDrawCommand(const DrawCommand& command, const std::shared_ptr<RenderPass> pass) = 0;

            virtual void ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) = 0;

            virtual void MemoryBarrier(MemoryBarrierBit barriers) = 0;

            virtual const ComputeLimits& GetComputeLimits() = 0;

            virtual void ToggleMultisampling(const bool on) = 0;

            static std::shared_ptr<RendererAPI> Create(API api);

            /// Who provides the scene state while drawing (set once by the Renderer)
            void SetSceneBinding(ISceneBinding* scene) { m_Scene = scene; }

            /// Binds a texture (by its handle) to a texture unit of the current program
            virtual void BindTextureUnit(uint32_t binding, uint32_t handle) = 0;

            /// @brief The API the renderer runs on (null before Renderer::Init). The rhi factories pick their
            /// backend from it, so the rhi does not have to reach up to the renderer for it.
            static RendererAPI* Current();
            static void SetCurrent(RendererAPI* api);

            API GetAPI() const { return api; }

            virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

            virtual void SetClearColor(float r, float g, float b, float a) = 0;

            /// @brief The clear color currently set on the API (whoever set it, including raw calls).
            virtual glm::vec4 GetClearColor() = 0;

            virtual void Clear(ClearBit clearBits) = 0;

            /// @brief While non-zero (a buffer handle, see StorageBuffer::GetHandle), a DrawCommand whose cullSlot is
            /// >= 0 is issued as an indirect indexed draw reading its arguments from that buffer at
            /// cullSlot * 20 bytes (count, instanceCount, firstIndex, baseVertex, baseInstance) - so a compute
            /// shader decides, on the GPU, whether the draw produces anything. 0 = every draw is a plain direct draw.
            virtual void SetIndirectDrawBuffer(uint32_t bufferHandle) = 0;

            /// @brief Forget every piece of API state the backend caches to skip redundant calls. Call after
            /// touching the API outside the normal draw path (immediate renders, raw calls, ...).
            virtual void InvalidateStateCache() = 0;

            /// Sets what the draws that follow are part of (see DepthSweep). Prepass writes depth only, Equal only draws
            /// the candidates again with a depth test that matches what the prepass wrote.
            virtual void SetDepthSweep(DepthSweep sweep) { (void)sweep; }

            /// Whether a command is drawn by the two sweeps : an opaque, depth written, unblended draw of a shader that
            /// understands the prepass (see Shader::SupportsDepthPrepass). Everything else is drawn by the Equal sweep alone,
            /// as it would be without sweeps.
            virtual bool IsDepthPrepassCandidate(const DrawCommand& command, const RenderPass& pass) { (void)command; (void)pass; return false; }

            /// Whether the GPU takes BC4 / BC5 / BC7 textures as they are (see texture_compression.hpp)
            virtual bool SupportsBlockCompression() { return false; }

            /// GPU timings of the render passes (null when the backend has none)
            virtual GpuProfiler* GetGpuProfiler() { return nullptr; }

            virtual std::string GetDeviceVendor() = 0;
            virtual std::string GetRendererName() = 0;
            virtual std::string GetDriverVersion() = 0;

        protected:
            API api = API::OpenGL;
            ISceneBinding* m_Scene = nullptr;
            DebugViewState m_DebugView;
    };

}