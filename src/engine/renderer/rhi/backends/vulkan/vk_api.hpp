#pragma once

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/gpu_profiler.hpp"

#include "vk_buffer.hpp"
#include "vk_context.hpp"
#include "vk_image.hpp"

namespace Shard::Engine::Rendering {

    class VKFramebuffer;
    class VKProgram;
    class VKShader;
    class VKPipeline;
    class VKMesh;
    class VKStorageBuffer;

    namespace VK {

        /// What a render pass draws into : the attachments of a framebuffer, as the API begins rendering with them
        struct RenderTarget
        {
            Image* color = nullptr;
            Image* depth = nullptr;
            VkImageView colorView = VK_NULL_HANDLE;
            VkImageView depthView = VK_NULL_HANDLE;

            /// The colour attachments after the first (a G-buffer)
            static constexpr uint32_t kMaxExtraColors = 7;
            Image* extraColors[kMaxExtraColors] = {};
            VkImageView extraColorViews[kMaxExtraColors] = {};
            uint32_t extraColorCount = 0;

            uint32_t ColorCount() const { return color ? 1 + extraColorCount : 0; }
            Image* ColorAt(uint32_t index) const { return index == 0 ? color : extraColors[index - 1]; }
            VkImageView ColorViewAt(uint32_t index) const { return index == 0 ? colorView : extraColorViews[index - 1]; }
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t layers = 1;        ///< more than one : a layered depth target (the point lights' shadow cubes), drawn with gl_Layer
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

            bool IsValid() const { return color || depth; }
            VkFormat ColorFormat() const { return color ? color->format : VK_FORMAT_UNDEFINED; }
            VkFormat DepthFormat() const { return depth ? depth->format : VK_FORMAT_UNDEFINED; }

            /// What a pipeline has to be built for to draw into this target
            uint64_t Key() const;
        };
    }

    /// @brief The Vulkan side of the renderer : it takes the same calls the OpenGL backend does (a bind of a framebuffer, a clear, uniforms
    /// set by name on a shader, textures bound to units, a draw) and turns them into recorded commands.
    ///
    /// What OpenGL keeps as state of the context is kept here as tables : the texture units, the storage buffers, the image units. A draw
    /// resolves them against the reflection of the shader it uses into descriptor sets, and the uniforms the shader was given by name into
    /// a slice of the uniform ring. A render pass is begun when the first thing is drawn into the target (so that a Clear that follows a Bind
    /// becomes the load operation of the pass), and ended when something else needs the command buffer to be outside of one.
    class VKRendererAPI final : public RendererAPI
    {
        public:

            VKRendererAPI();
            ~VKRendererAPI() override;

            static VKRendererAPI* Get() { return s_Instance; }

            /// Creates the device. The window (if any) gives the surface the picture is presented on.
            bool Init();

            // ---- RendererAPI ----
            void ExecuteDrawCommand(const DrawCommand& command, const std::shared_ptr<RenderPass> pass) override;
            void ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipeline, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;
            void MemoryBarrier(MemoryBarrierBit barriers) override;
            const ComputeLimits& GetComputeLimits() override { return m_ComputeLimits; }
            void ToggleMultisampling(const bool on) override { (void)on; }
            void BindTextureUnit(uint32_t binding, uint32_t handle) override;
            void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
            void SetClearColor(float r, float g, float b, float a) override { m_ClearColor = glm::vec4(r, g, b, a); }
            glm::vec4 GetClearColor() override { return m_ClearColor; }
            void Clear(ClearBit clearBits) override;
            void SetIndirectDrawBuffer(uint32_t bufferHandle) override { m_IndirectBuffer = bufferHandle; }
            void InvalidateStateCache() override;
            void SetDepthSweep(DepthSweep sweep) override;
            bool IsDepthPrepassCandidate(const DrawCommand& command, const RenderPass& pass) override;
            bool SupportsBlockCompression() override { return VK::Context::Get().GetCaps().blockCompression; }
            GpuProfiler* GetGpuProfiler() override;
            void SetDebugView(const DebugViewState& state) override;
            void BeginFrame() override;
            void EndFrame() override;

            bool GetUIBackendInfo(UIBackendInfo& out) override;
            void SetUITextureHooks(const UITextureHooks& hooks) override { m_UIHooks = hooks; }
            uint64_t GetUITextureID(uint32_t handle) override;
            bool PresentUI(const UIDrawCallback& draw, uint32_t windowWidth, uint32_t windowHeight, const float clearColor[4]) override;

            std::string GetDeviceVendor() override;
            std::string GetRendererName() override;
            std::string GetDriverVersion() override;

            // ---- What the objects of the backend call ----

            void BindStorageBuffer(uint32_t binding, uint32_t bufferId);
            void BindStorageImage(uint32_t unit, uint32_t textureId, TextureAccess access, uint32_t mip);

            /// Framebuffer::Bind : what is drawn from now on goes there (null : nowhere)
            void SetTarget(VKFramebuffer* target);
            VKFramebuffer* GetTarget() const { return m_Target; }

            /// Draws go to this render target (a face and a level of a cube map, a level of a texture...) instead of the framebuffer, until it is
            /// set to null : what the generators of the image based lighting use. Ends the pass in progress.
            void SetTargetOverride(const VK::RenderTarget* target);

            /// Issues a draw of `command` (a mesh and its index range) with a pipeline, the way it is, without the scene state a draw of the renderer gets
            void DrawDirect(const DrawCommand& command, const std::shared_ptr<Pipeline>& pipeline);

            /// A framebuffer is going away or changing its attachments : it must not be the target
            void ForgetTarget(const VKFramebuffer* target);

            /// Closes the render pass in progress (applying a clear nothing has drawn after). A no-op outside one.
            void EndRendering();

            /// Whether a render pass is open
            bool IsRendering() const { return m_RenderingActive; }

            /// Presents the picture : copies `color` over the window
            bool PresentImage(VK::Image& color, uint32_t screenWidth, uint32_t screenHeight);

            /// Debug markers around a group of commands (RenderDoc, the validation layers' reports) : they stay balanced when the commands are
            /// submitted in the middle of a group (the label is closed in one command buffer and opened again in the next)
            void PushDebugGroup(const char* name) override;
            void PopDebugGroup() override;

            uint32_t GetBoundTextureUnit(uint32_t unit) const { return unit < m_TextureUnits.size() ? m_TextureUnits[unit] : 0; }

        private:

            /// A texture the user interface shows : the id it is drawn by is made of the image, the sampler and the layout at the time
            struct UITexture
            {
                VkImageView view = VK_NULL_HANDLE;
                VkSampler sampler = VK_NULL_HANDLE;
                uint64_t id = 0;
            };

            void ReleaseUITexture(uint64_t id);

            UITextureHooks m_UIHooks;
            std::unordered_map<uint32_t, UITexture> m_UITextures;

            struct ImageUnit
            {
                uint32_t texture = 0;
                TextureAccess access = TextureAccess::ReadWrite;
                uint32_t mip = 0;
            };

            void BeginRendering();
            void PrepareSampledImages(VKShader& shader);

            /// Builds the descriptor sets the program reads from the tables (texture units, storage buffers, image units) and the uniforms, and binds them
            void BindDescriptors(VKProgram& program, VkPipelineBindPoint bindPoint, bool layoutChanged);

            void IssueDraw(const DrawCommand& command, const std::shared_ptr<Pipeline>& pipeline, bool fullscreen, bool depthOnly);

            void BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Pipeline> pipeline);
            void BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Material> material);
            void BindLightmap(const DrawCommand& command, const std::shared_ptr<Shader>& shader);
            void ExecuteDepthOnlyDraw(const DrawCommand& command);

            /// The first time a shader is drawn with this since the pass started (see NeedsPassGlobalsUpdate in the OpenGL backend)
            enum class PassGlobalsKind { World = 0, Camera = 1, DepthSweep = 2, Count = 3 };
            bool NeedsPassGlobalsUpdate(uint32_t shaderId, PassGlobalsKind kind);

            static VKRendererAPI* s_Instance;

            VK::Context m_Context;
            bool m_Initialised = false;

            ComputeLimits m_ComputeLimits;
            glm::vec4 m_ClearColor = glm::vec4(0, 0, 0, 1);

            // The tables OpenGL keeps in its context
            std::array<uint32_t, 80> m_TextureUnits{};
            std::array<ImageUnit, 16> m_ImageUnits{};
            std::array<uint32_t, 40> m_StorageBuffers{};
            uint32_t m_IndirectBuffer = 0;

            // The pass in progress
            VKFramebuffer* m_Target = nullptr;
            VK::RenderTarget m_OverrideTarget;
            bool m_HasOverride = false;
            bool HasTarget() const { return m_Target != nullptr || m_HasOverride; }
            VK::RenderTarget m_ActiveTarget;
            bool m_RenderingActive = false;
            ClearBit m_PendingClear = ClearBit::None;
            VkViewport m_Viewport{};
            bool m_ViewportSet = false;

            // What is bound on the command buffer, to skip what is already in place
            VkPipeline m_BoundPipeline = VK_NULL_HANDLE;
            VkPipelineBindPoint m_BoundBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            VkBuffer m_BoundVertexBuffer = VK_NULL_HANDLE;
            VkBuffer m_BoundIndexBuffer = VK_NULL_HANDLE;
            bool m_DynamicStateValid = false;
            VkCullModeFlags m_CullMode = 0;
            bool m_DepthTest = false, m_DepthWrite = false;
            VkCompareOp m_DepthCompare = VK_COMPARE_OP_LESS;
            VkPrimitiveTopology m_Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkViewport m_BoundViewport{};
            bool m_BoundViewportValid = false;

            // Depth sweeps and wireframe
            DepthSweep m_Sweep = DepthSweep::None;
            std::vector<std::shared_ptr<Shader>> m_PrepassShaders;
            bool m_ForceLine = false;

            struct LastDraw
            {
                const RenderPass* pass = nullptr;
                const Pipeline* pipeline = nullptr;
                const Material* material = nullptr;
                glm::mat4 modelMatrix = glm::mat4(1.0f);
                uint32_t objectID = 0;
                uint64_t lightmapHandle = 0;
                glm::vec4 lightmapScaleOffset = glm::vec4(0);
            };
            LastDraw m_LastDraw;

            uint32_t m_PassGlobalsShader[static_cast<size_t>(PassGlobalsKind::Count)] = {};
            bool m_PassGlobalsValid[static_cast<size_t>(PassGlobalsKind::Count)] = {};

            std::unique_ptr<GpuProfiler> m_GpuProfiler;
            std::vector<std::string> m_Labels;

            VK::Buffer m_DummyBuffer;
            VkDeviceSize m_DummyBufferSize = 1024;

            VkPipelineLayout m_BoundLayout = VK_NULL_HANDLE;
            std::array<VkDescriptorSet, BindingSets::Count> m_BoundSets{};
            bool m_DepthEqual = false;
    };

}
