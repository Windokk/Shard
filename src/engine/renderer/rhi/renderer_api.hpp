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
    struct RenderPass;      // struct in render_pass.hpp: MSVC mangles class and struct differently
    struct OverlayProxy;

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
        ShaderStorage    = 1 << 0,
        ImageAccess      = 1 << 1,
        TextureFetch     = 1 << 2,
        BufferUpdate     = 1 << 3,
        VertexAttribArray = 1 << 4,
        Command          = 1 << 6,
        TextureUpdate    = 1 << 5, // glGetTexImage / glTexSubImage* after image store (GL_TEXTURE_UPDATE_BARRIER_BIT). Host readback, unlike TextureFetch.
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
        uint32_t maxWorkGroupCount[3] = { 0, 0, 0 };
        uint32_t maxWorkGroupSize[3]  = { 0, 0, 0 };
        uint32_t maxWorkGroupInvocations = 0;
        uint32_t maxSharedMemorySize = 0;
    };

    // Mirrored by lit.frag's `viewMode` uniform, keep in sync.
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
        bool wireframeRaster = false;
        uint32_t lightingTerms = LightingTermsAll;
    };

    enum class DepthSweep : uint8_t { None = 0, Prepass, Equal };

    class Shader;
    class GpuProfiler;
    struct RenderView;

    class RendererAPI;

    class ISceneBinding{
        public:
            virtual ~ISceneBinding() = default;

            virtual void BindWorldState(RendererAPI& api, std::shared_ptr<Shader> shader, glm::mat4 modelMatrix, int objectID, bool applyPassGlobals) = 0;

            virtual void BindMaterialScene(std::shared_ptr<Material> material) = 0;

            virtual bool GetCurrentView(RenderView& out) = 0;
    };

    struct WindowHooks
    {
        std::function<std::vector<std::string>()> vulkanInstanceExtensions;

        std::function<uint64_t(void* vulkanInstance)> createVulkanSurface;

        int width = 1280;
        int height = 720;
        bool vsync = true;
        bool validation = false;

        std::function<bool()> vsyncWanted;
    };

    struct UIBackendInfo
    {
        void* instance = nullptr;
        void* physicalDevice = nullptr;
        void* device = nullptr;
        void* queue = nullptr;
        uint32_t queueFamily = 0;
        uint32_t apiVersion = 0;
        uint32_t imageCount = 0;
        uint32_t swapchainFormat = 0;
    };

    struct UITextureHooks
    {
        std::function<uint64_t(void* sampler, void* imageView, uint32_t imageLayout)> add;
        std::function<void(uint64_t id)> remove;
    };

    using UIDrawCallback = std::function<void(void* commandBuffer)>;

    class RendererAPI;

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
                Null
            };

            virtual ~RendererAPI() = default;

            virtual void SetDebugView(const DebugViewState& state) { m_DebugView = state; }
            const DebugViewState& GetDebugView() const { return m_DebugView; }

            virtual void DrawOverlay(const OverlayProxy& overlay, uint32_t width, uint32_t height) { (void)overlay; (void)width; (void)height; }
            virtual void ReleaseOverlayGeometry(const uint32_t gpu[3]) { (void)gpu; }

            virtual void BeginFrame() {}
            virtual void EndFrame() {}

            virtual void PushDebugGroup(const char* name) { (void)name; }
            virtual void PopDebugGroup() {}

            static void SetWindowHooks(const WindowHooks& hooks);
            static const WindowHooks& GetWindowHooks();

            virtual bool GetUIBackendInfo(UIBackendInfo& out) { (void)out; return false; }

            virtual void SetUITextureHooks(const UITextureHooks& hooks) { (void)hooks; }

            virtual uint64_t GetUITextureID(uint32_t handle) { return handle; }

            virtual bool PresentUI(const UIDrawCallback& draw, uint32_t windowWidth, uint32_t windowHeight, const float clearColor[4]) { (void)draw; (void)windowWidth; (void)windowHeight; (void)clearColor; return false; }

            virtual void ExecuteDrawCommand(const DrawCommand& command, const std::shared_ptr<RenderPass> pass) = 0;

            virtual void ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) = 0;

            virtual void MemoryBarrier(MemoryBarrierBit barriers) = 0;

            virtual const ComputeLimits& GetComputeLimits() = 0;

            virtual void ToggleMultisampling(const bool on) = 0;

            static std::shared_ptr<RendererAPI> Create(API api);

            void SetSceneBinding(ISceneBinding* scene) { m_Scene = scene; }

            virtual void BindTextureUnit(uint32_t binding, uint32_t handle) = 0;

            static RendererAPI* Current();
            static void SetCurrent(RendererAPI* api);

            API GetAPI() const { return api; }

            virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

            virtual void SetClearColor(float r, float g, float b, float a) = 0;

            virtual glm::vec4 GetClearColor() = 0;

            virtual void Clear(ClearBit clearBits) = 0;

            // While non-zero, draws with cullSlot >= 0 are indirect indexed draws reading 20-byte args at cullSlot * 20 (count, instanceCount, firstIndex, baseVertex, baseInstance).
            virtual void SetIndirectDrawBuffer(uint32_t bufferHandle) = 0;

            virtual void InvalidateStateCache() = 0;

            virtual void SetDepthSweep(DepthSweep sweep) { (void)sweep; }

            virtual bool IsDepthPrepassCandidate(const DrawCommand& command, const RenderPass& pass) { (void)command; (void)pass; return false; }

            virtual bool SupportsBlockCompression() { return false; }

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