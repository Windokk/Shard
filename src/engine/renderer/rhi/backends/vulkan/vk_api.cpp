#include "vk_api.hpp"

#include <algorithm>
#include <cstring>

#include "vk_buffer.hpp"
#include "vk_framebuffer.hpp"
#include "vk_material.hpp"
#include "vk_mesh.hpp"
#include "vk_pipeline.hpp"
#include "vk_profiler.hpp"
#include "vk_shader.hpp"
#include "vk_texture.hpp"

#include "engine/core/diagnostics/profiler.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/render_view.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"

namespace Shard::Engine::Rendering {

    using namespace VK;

    VKRendererAPI* VKRendererAPI::s_Instance = nullptr;

    namespace {

        bool IsActiveAttachment(const RenderTarget& target, const Image* image)
        {
            if (!image)
                return false;
            if (image == target.color || image == target.depth)
                return true;
            for (uint32_t i = 0; i < target.extraColorCount; i++)
            {
                if (image == target.extraColors[i])
                    return true;
            }
            return false;
        }

        /// A texture of the table that a shader of this kind of sampler can read : the right kind of view, the right sample count, depth for a shadow sampler
        bool Matches(const Image& image, const SamplerKey& key, ShaderSamplerType expected)
        {
            if (!image.IsValid() || image.view == VK_NULL_HANDLE)
                return false;

            const VkImageViewType expectedView = ViewTypeOf(expected);
            VkImageViewType actual = image.viewType;
            if (actual != expectedView)
                return false;

            const bool multisampled = expected == ShaderSamplerType::Texture2DMultisample || expected == ShaderSamplerType::Texture2DMultisampleArray;
            if (multisampled != (image.samples != VK_SAMPLE_COUNT_1_BIT))
                return false;

            const bool shadow = expected == ShaderSamplerType::Texture1DShadow || expected == ShaderSamplerType::Texture2DShadow ||
                expected == ShaderSamplerType::TextureCubeShadow || expected == ShaderSamplerType::Texture1DArrayShadow ||
                expected == ShaderSamplerType::Texture2DArrayShadow || expected == ShaderSamplerType::TextureCubeArrayShadow;
            if (shadow && !image.IsDepth())
                return false;

            // A sampler that compares only reads through a shadow sampler, and a shadow sampler only reads one that compares
            if (key.compare != shadow)
                return false;
            return true;
        }
    }

    VKRendererAPI::VKRendererAPI()
    {
        api = API::Vulkan;
        s_Instance = this;
    }

    VKRendererAPI::~VKRendererAPI()
    {
        if (m_Initialised)
        {
            VK::Context::Get().WaitIdle();
            m_DummyBuffer.Destroy();
            m_PrepassShaders.clear();
            m_GpuProfiler.reset();
            ClearShaderLayoutCache();
            m_Context.Shutdown();
        }

        if (s_Instance == this)
            s_Instance = nullptr;
    }

    bool VKRendererAPI::Init()
    {
        const WindowHooks& hooks = RendererAPI::GetWindowHooks();

        InitInfo info;
        info.validation = hooks.validation;
        info.width = hooks.width;
        info.height = hooks.height;
        info.vsync = hooks.vsync;
        info.vsyncWanted = hooks.vsyncWanted;

        if (hooks.createVulkanSurface)
        {
            if (hooks.vulkanInstanceExtensions)
                info.instanceExtensions = hooks.vulkanInstanceExtensions();
            info.createSurface = [&hooks](VkInstance instance) { return hooks.createVulkanSurface(instance); };
        }

        if (!m_Context.Init(info))
            return false;

        m_Initialised = true;

        VK::Context::Get().SetOutsideRenderingHook([this] { EndRendering(); });

        // A command buffer is submitted in the middle of a group (a readback flushes the frame) : the groups it had open are closed, and open again
        // in the next one, so that every command buffer is balanced
        VK::Context::Get().SetCommandBufferHooks(
            [this](VkCommandBuffer cmd) {
                // Nothing is bound in a command buffer that has just begun : what was remembered of the last one says nothing about this one
                m_BoundVertexBuffer = VK_NULL_HANDLE;
                m_BoundIndexBuffer = VK_NULL_HANDLE;
                m_BoundLayout = VK_NULL_HANDLE;
                m_BoundSets = {};
                InvalidateStateCache();

                if (!VK::Context::Get().GetCaps().debugUtils || !vkCmdBeginDebugUtilsLabelEXT)
                    return;
                for (const std::string& name : m_Labels)
                {
                    VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
                    label.pLabelName = name.c_str();
                    vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
                }
            },
            [this](VkCommandBuffer cmd) {
                if (!VK::Context::Get().GetCaps().debugUtils || !vkCmdEndDebugUtilsLabelEXT)
                    return;
                for (size_t i = 0; i < m_Labels.size(); i++)
                    vkCmdEndDebugUtilsLabelEXT(cmd);
            });

        const VkPhysicalDeviceLimits& limits = VK::Context::Get().GetLimits();
        for (int i = 0; i < 3; i++)
        {
            m_ComputeLimits.maxWorkGroupCount[i] = limits.maxComputeWorkGroupCount[i];
            m_ComputeLimits.maxWorkGroupSize[i] = limits.maxComputeWorkGroupSize[i];
        }
        m_ComputeLimits.maxWorkGroupInvocations = limits.maxComputeWorkGroupInvocations;
        m_ComputeLimits.maxSharedMemorySize = limits.maxComputeSharedMemorySize;

        // What a table entry nobody filled reads
        if (!m_DummyBuffer.Create(m_DummyBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, "Dummy storage buffer"))
            return false;
        VK::Context::Get().ImmediateSubmit([&](VkCommandBuffer cmd) { vkCmdFillBuffer(cmd, m_DummyBuffer.buffer, 0, VK_WHOLE_SIZE, 0); });

        return true;
    }

    // ---- Information ---------------------------------------------------------------------------------------------------------------

    std::string VKRendererAPI::GetDeviceVendor()
    {
        switch (VK::Context::Get().GetProperties().vendorID)
        {
            case 0x10DE: return "NVIDIA Corporation";
            case 0x1002: return "AMD";
            case 0x8086: return "Intel";
            case 0x13B5: return "ARM";
            case 0x5143: return "Qualcomm";
            default:     return "Unknown";
        }
    }

    std::string VKRendererAPI::GetRendererName()
    {
        return std::string(VK::Context::Get().GetProperties().deviceName) + " (Vulkan)";
    }

    std::string VKRendererAPI::GetDriverVersion()
    {
        const VkPhysicalDeviceProperties& properties = VK::Context::Get().GetProperties();
        return "Vulkan " + std::to_string(VK_API_VERSION_MAJOR(properties.apiVersion)) + "." + std::to_string(VK_API_VERSION_MINOR(properties.apiVersion)) +
               "." + std::to_string(VK_API_VERSION_PATCH(properties.apiVersion)) + ", driver " + std::to_string(properties.driverVersion);
    }

    GpuProfiler* VKRendererAPI::GetGpuProfiler()
    {
        if (!m_GpuProfiler && VK::Context::Get().GetCaps().timestamps)
            m_GpuProfiler = std::make_unique<VKGpuProfiler>();
        return m_GpuProfiler.get();
    }

    // ---- Frames --------------------------------------------------------------------------------------------------------------------

    void VKRendererAPI::BeginFrame()
    {
    }

    void VKRendererAPI::EndFrame()
    {
        EndRendering();
        VK::Context& context = VK::Context::Get();
        if (context.IsRecording())
            context.Submit();
    }

    bool VKRendererAPI::GetUIBackendInfo(UIBackendInfo& out)
    {
        if (!VK::Context::Exists())
            return false;

        VK::Context& context = VK::Context::Get();
        out.instance = context.GetInstance();
        out.physicalDevice = context.GetPhysicalDevice();
        out.device = context.GetDevice();
        out.queue = context.GetQueue();
        out.queueFamily = context.GetQueueFamily();
        out.apiVersion = VK_API_VERSION_1_3;
        out.imageCount = std::max(context.GetSwapchainImageCount(), 2u);
        out.swapchainFormat = static_cast<uint32_t>(context.GetSwapchainFormat());
        return context.SupportsPresentation();
    }

    uint64_t VKRendererAPI::GetUITextureID(uint32_t handle)
    {
        TextureResource* resource = TextureResource::Find(handle);
        if (!resource || !resource->image.IsValid() || !m_UIHooks.add)
            return 0;

        UITexture& entry = m_UITextures[handle];

        // The texture was made again (a framebuffer that was resized) : what the id points at is gone
        if (entry.id != 0 && (entry.view != resource->GetView() || entry.sampler != resource->GetSampler()))
        {
            ReleaseUITexture(entry.id);
            entry.id = 0;
        }

        if (entry.id == 0)
        {
            entry.view = resource->GetView();
            entry.sampler = resource->GetSampler();
            entry.id = m_UIHooks.add(entry.sampler, entry.view, static_cast<uint32_t>(resource->image.RestingLayout()));
        }

        return entry.id;
    }

    void VKRendererAPI::ReleaseUITexture(uint64_t id)
    {
        if (id == 0 || !m_UIHooks.remove)
            return;

        // The frames in flight may still read it : it goes when the GPU is done with them
        auto remove = m_UIHooks.remove;
        VK::Context::Get().Defer([remove, id] { remove(id); });
    }

    bool VKRendererAPI::PresentUI(const UIDrawCallback& draw, uint32_t windowWidth, uint32_t windowHeight, const float clearColor[4])
    {
        EndRendering();

        VK::Context& context = VK::Context::Get();
        VkCommandBuffer cmd = context.Cmd();

        // What the interface shows is in the layout its descriptors say ; the ones whose texture is gone are let go
        for (auto it = m_UITextures.begin(); it != m_UITextures.end();)
        {
            TextureResource* resource = TextureResource::Find(it->first);
            if (!resource || !resource->image.IsValid())
            {
                ReleaseUITexture(it->second.id);
                it = m_UITextures.erase(it);
                continue;
            }

            if (it->second.id != 0)
                resource->image.TransitionToResting(cmd);
            ++it;
        }

        return context.PresentUI([&draw](VkCommandBuffer commands) { draw(commands); }, windowWidth, windowHeight, clearColor);
    }

    bool VKRendererAPI::PresentImage(Image& color, uint32_t screenWidth, uint32_t screenHeight)
    {
        EndRendering();
        return VK::Context::Get().Present(color, screenWidth, screenHeight);
    }

    void VKRendererAPI::PushDebugGroup(const char* name)
    {
        // The command buffer first : if it begins now, it opens the groups that were open (not this one)
        VkCommandBuffer cmd = VK::Context::Get().Cmd();
        m_Labels.push_back(name);

        if (!VK::Context::Get().GetCaps().debugUtils || !vkCmdBeginDebugUtilsLabelEXT)
            return;

        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
    }

    void VKRendererAPI::PopDebugGroup()
    {
        if (m_Labels.empty())
            return;

        VkCommandBuffer cmd = VK::Context::Get().Cmd();
        m_Labels.pop_back();

        if (VK::Context::Get().GetCaps().debugUtils && vkCmdEndDebugUtilsLabelEXT)
            vkCmdEndDebugUtilsLabelEXT(cmd);
    }

    // ---- State tables --------------------------------------------------------------------------------------------------------------

    void VKRendererAPI::BindTextureUnit(uint32_t binding, uint32_t handle)
    {
        if (binding < m_TextureUnits.size())
            m_TextureUnits[binding] = handle;
    }

    void VKRendererAPI::BindStorageBuffer(uint32_t binding, uint32_t bufferId)
    {
        if (binding < m_StorageBuffers.size())
            m_StorageBuffers[binding] = bufferId;
    }

    void VKRendererAPI::BindStorageImage(uint32_t unit, uint32_t textureId, TextureAccess access, uint32_t mip)
    {
        if (unit < m_ImageUnits.size())
            m_ImageUnits[unit] = {textureId, access, mip};
    }

    void VKRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        m_Viewport = {float(x), float(y), float(width), float(height), 0.0f, 1.0f};
        m_ViewportSet = true;
    }

    void VKRendererAPI::SetDebugView(const DebugViewState& state)
    {
        m_DebugView = state;
        m_ForceLine = state.wireframeRaster;
    }

    void VKRendererAPI::InvalidateStateCache()
    {
        m_LastDraw = LastDraw{};
        for (bool& valid : m_PassGlobalsValid)
            valid = false;

        m_BoundPipeline = VK_NULL_HANDLE;
        m_DynamicStateValid = false;
        m_BoundViewportValid = false;
    }

    bool VKRendererAPI::NeedsPassGlobalsUpdate(uint32_t shaderId, PassGlobalsKind kind)
    {
        const size_t i = static_cast<size_t>(kind);
        if (m_PassGlobalsValid[i] && m_PassGlobalsShader[i] == shaderId)
            return false;

        m_PassGlobalsShader[i] = shaderId;
        m_PassGlobalsValid[i] = true;
        return true;
    }

    // ---- Targets and render passes -------------------------------------------------------------------------------------------------

    void VKRendererAPI::SetTarget(VKFramebuffer* target)
    {
        if (target == m_Target)
            return;

        EndRendering();
        m_Target = target;
        m_PendingClear = ClearBit::None;
    }

    void VKRendererAPI::ForgetTarget(const VKFramebuffer* target)
    {
        // The images it drew into are about to change : the pass in progress ends while they are valid
        if (m_Target == target)
            EndRendering();
    }

    void VKRendererAPI::SetTargetOverride(const RenderTarget* target)
    {
        EndRendering();
        m_PendingClear = ClearBit::None;

        if (target)
        {
            m_OverrideTarget = *target;
            m_HasOverride = true;
        }
        else
            m_HasOverride = false;
    }

    void VKRendererAPI::SetScissor(const VkRect2D* rect)
    {
        m_ScissorSet = rect != nullptr;
        if (rect)
            m_Scissor = *rect;
    }

    void VKRendererAPI::DrawDirect(const DrawCommand& command, const std::shared_ptr<Pipeline>& pipeline)
    {
        IssueDraw(command, pipeline, false, false);
    }

    void VKRendererAPI::BeginRendering()
    {
        if (m_RenderingActive || !HasTarget())
            return;

        m_ActiveTarget = m_HasOverride ? m_OverrideTarget : m_Target->GetRenderTarget();
        if (!m_ActiveTarget.IsValid())
            return;

        VK::Context& context = VK::Context::Get();
        VkCommandBuffer cmd = context.Cmd();

        const bool clearColor = (m_PendingClear & ClearBit::Color) != ClearBit::None && m_ActiveTarget.color;
        const bool clearDepth = (m_PendingClear & ClearBit::Depth) != ClearBit::None && m_ActiveTarget.depth;

        for (uint32_t i = 0; i < m_ActiveTarget.ColorCount(); i++)
            m_ActiveTarget.ColorAt(i)->Transition(cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (m_ActiveTarget.depth)
            m_ActiveTarget.depth->Transition(cmd, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        VkRenderingAttachmentInfo colorAttachments[1 + RenderTarget::kMaxExtraColors];
        for (uint32_t i = 0; i < m_ActiveTarget.ColorCount(); i++)
        {
            VkRenderingAttachmentInfo& colorAttachment = colorAttachments[i];
            colorAttachment = VkRenderingAttachmentInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colorAttachment.imageView = m_ActiveTarget.ColorViewAt(i);
            colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.clearValue.color = {{m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a}};
        }

        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        if (m_ActiveTarget.depth)
        {
            depthAttachment.imageView = m_ActiveTarget.depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil = {1.0f, 0};
        }

        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
        info.renderArea = {{0, 0}, {m_ActiveTarget.width, m_ActiveTarget.height}};
        info.layerCount = std::max(m_ActiveTarget.layers, 1u);
        info.colorAttachmentCount = m_ActiveTarget.ColorCount();
        info.pColorAttachments = colorAttachments;
        if (m_ActiveTarget.depth)
        {
            info.pDepthAttachment = &depthAttachment;
            if (HasStencil(m_ActiveTarget.DepthFormat()))
            {
                VkRenderingAttachmentInfo stencil = depthAttachment;
                stencil.loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                info.pStencilAttachment = &stencil;
                vkCmdBeginRendering(cmd, &info);
                m_RenderingActive = true;
                m_PendingClear = ClearBit::None;
                m_BoundViewportValid = false;
                m_DynamicStateValid = false;
                return;
            }
        }

        vkCmdBeginRendering(cmd, &info);
        m_RenderingActive = true;
        m_PendingClear = ClearBit::None;
        m_BoundViewportValid = false;
        m_DynamicStateValid = false;
    }

    void VKRendererAPI::EndRendering()
    {
        // A clear that nothing was drawn after still has to happen
        if (!m_RenderingActive && HasTarget() && m_PendingClear != ClearBit::None)
            BeginRendering();

        if (!m_RenderingActive)
            return;

        VK::Context& context = VK::Context::Get();
        VkCommandBuffer cmd = context.Cmd();
        vkCmdEndRendering(cmd);
        m_RenderingActive = false;

        // Where the targets wait until something samples them
        for (uint32_t i = 0; i < m_ActiveTarget.ColorCount(); i++)
            m_ActiveTarget.ColorAt(i)->TransitionToResting(cmd);
        if (m_ActiveTarget.depth)
            m_ActiveTarget.depth->TransitionToResting(cmd);

        m_BoundPipeline = VK_NULL_HANDLE;
    }

    void VKRendererAPI::Clear(ClearBit clearBits)
    {
        // The start of a pass : what was given to the shaders for the last one is not what the next one expects
        m_LastDraw = LastDraw{};
        for (bool& valid : m_PassGlobalsValid)
            valid = false;
        m_BoundPipeline = VK_NULL_HANDLE;
        m_DynamicStateValid = false;
        m_BoundViewportValid = false;

        if (clearBits == ClearBit::None || !HasTarget())
            return;

        if (!m_RenderingActive)
        {
            m_PendingClear = m_PendingClear | clearBits;
            return;
        }

        // Inside a pass already (a second pass on the same target) : cleared where it stands
        VkCommandBuffer cmd = VK::Context::Get().Cmd();

        VkClearAttachment attachments[2 + RenderTarget::kMaxExtraColors];
        uint32_t count = 0;

        if ((clearBits & ClearBit::Color) != ClearBit::None)
        {
            for (uint32_t i = 0; i < m_ActiveTarget.ColorCount(); i++)
            {
                attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                attachments[count].colorAttachment = i;
                attachments[count].clearValue.color = {{m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a}};
                count++;
            }
        }

        if ((clearBits & ClearBit::Depth) != ClearBit::None && m_ActiveTarget.depth)
        {
            attachments[count].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            attachments[count].clearValue.depthStencil = {1.0f, 0};
            count++;
        }

        if (count)
        {
            VkClearRect rect{{{0, 0}, {m_ActiveTarget.width, m_ActiveTarget.height}}, 0, std::max(m_ActiveTarget.layers, 1u)};
            vkCmdClearAttachments(cmd, count, attachments, 1, &rect);
        }
    }

    // ---- Barriers, depth sweeps -----------------------------------------------------------------------------------------------------

    void VKRendererAPI::MemoryBarrier(MemoryBarrierBit barriers)
    {
        if (barriers == MemoryBarrierBit::None)
            return;

        EndRendering();

        VkCommandBuffer cmd = VK::Context::Get().Cmd();
        GlobalBarrier(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }

    bool VKRendererAPI::IsDepthPrepassCandidate(const DrawCommand& command, const RenderPass& pass)
    {
        if (command.fullscreenTri || !command.material || !command.mesh || pass.overridePipeline)
            return false;
        if (command.material->GetOpacity() != Opacity::Opaque)
            return false;

        const std::shared_ptr<Pipeline>& pipeline = command.material->GetPipeline();
        if (!pipeline)
            return false;
        const PipelineSpecifications& specs = pipeline->GetSpecifications();
        if (!specs.depthTest || !specs.depthWrite || specs.blending || specs.polygonMode != PolygonMode::Fill || !specs.shader)
            return false;

        return specs.shader->SupportsDepthPrepass();
    }

    void VKRendererAPI::SetDepthSweep(DepthSweep sweep)
    {
        if (m_Sweep == DepthSweep::Prepass && sweep != DepthSweep::Prepass)
        {
            // The shaders go back to shading : the switch is a uniform of the program, it would stay on for any later draw
            for (const std::shared_ptr<Shader>& shader : m_PrepassShaders)
                shader->SetBool("depthPrepass", false);
            m_PrepassShaders.clear();
        }

        m_Sweep = sweep;
        InvalidateStateCache();
    }

    // ---- Descriptors ---------------------------------------------------------------------------------------------------------------

    void VKRendererAPI::PrepareSampledImages(VKShader& shader)
    {
        VKProgram& program = shader.Program();
        const ShaderReflection& reflection = program.GetBinary().reflection;
        VK::Context& context = VK::Context::Get();

        std::vector<Image*> toTransition;

        for (const ReflectedBinding& binding : reflection.bindings)
        {
            if (binding.kind != BindingKind::SampledImage)
                continue;

            for (uint32_t j = 0; j < std::max(binding.count, 1u); j++)
            {
                const uint32_t unit = program.GetSamplerUnit(binding, j);
                TextureResource* texture = unit < m_TextureUnits.size() ? TextureResource::Find(m_TextureUnits[unit]) : nullptr;
                if (!texture || !texture->image.IsValid())
                    continue;

                Image& image = texture->image;
                if (m_RenderingActive && IsActiveAttachment(m_ActiveTarget, &image))
                    continue;       // read while it is drawn into : it reads the dummy instead

                if (image.layout != image.RestingLayout())
                    toTransition.push_back(&image);
            }
        }

        if (toTransition.empty())
            return;

        EndRendering();
        VkCommandBuffer cmd = context.Cmd();
        for (Image* image : toTransition)
            image->TransitionToResting(cmd);
    }

    // ---- Drawing -------------------------------------------------------------------------------------------------------------------

    void VKRendererAPI::BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Pipeline> pipeline)
    {
        std::shared_ptr<Shader> shader = pipeline->GetSpecifications().shader;

        uint32_t textureSlot = 0;

        for (auto& [name, texture] : pass->customSamplers)
        {
            BindTextureUnit(textureSlot, static_cast<uint32_t>(texture));
            shader->SetInt(name, static_cast<int>(textureSlot));
            textureSlot++;
        }

        for (auto& [name, value] : pass->customUniforms)
        {
            std::visit([&](auto&& v)
            {
                using T = std::decay_t<decltype(v)>;

                if constexpr (std::is_same_v<T, bool>)
                    shader->SetBool(name, v);
                else if constexpr (std::is_same_v<T, int>)
                    shader->SetInt(name, v);
                else if constexpr (std::is_same_v<T, float>)
                    shader->SetFloat(name, v);
                else if constexpr (std::is_same_v<T, glm::vec2>)
                    shader->SetVec2(name, v);
                else if constexpr (std::is_same_v<T, glm::vec3>)
                    shader->SetVec3(name, v);
                else if constexpr (std::is_same_v<T, glm::vec4>)
                    shader->SetVec4(name, v);
                else if constexpr (std::is_same_v<T, glm::mat2>)
                    shader->SetMat2(name, v);
                else if constexpr (std::is_same_v<T, glm::mat3>)
                    shader->SetMat3(name, v);
                else if constexpr (std::is_same_v<T, glm::mat4>)
                    shader->SetMat4(name, v);
            }, value);
        }
    }

    void VKRendererAPI::BindPassData(const std::shared_ptr<RenderPass> pass, std::shared_ptr<Material> material)
    {
        // The parameters of the pass a material's shader does not read are skipped, not reported
        const auto& samplers = material->GetShader()->GetActiveSamplersMap();
        const auto& uniforms = material->GetShader()->GetActiveUniformsMap();

        for (auto& [name, texture] : pass->customSamplers)
        {
            if (samplers.find(name) != samplers.end())
                material->SetTextureParameter(name, texture);
        }

        for (auto& [name, value] : pass->customUniforms)
        {
            if (uniforms.find(name) != uniforms.end())
                material->SetScalarParameter(name, value);
        }
    }

    void VKRendererAPI::BindLightmap(const DrawCommand& command, const std::shared_ptr<Shader>& shader)
    {
        // A uniform the program does not have is a no-op, so every shader can be given these : only the lit one reads them
        shader->SetBool("lightmapEnabled", command.lightmapHandle != 0);
        if (command.lightmapHandle != 0)
        {
            shader->SetUVec2("lightmapHandle", uint32_t(command.lightmapHandle & 0xFFFFFFFFu), uint32_t(command.lightmapHandle >> 32));
            shader->SetVec4("lightmapScaleOffset", command.lightmapScaleOffset);
        }
    }

    void VKRendererAPI::ExecuteDepthOnlyDraw(const DrawCommand& command)
    {
        std::shared_ptr<Pipeline> pipeline = command.material->GetPipeline();
        std::shared_ptr<Shader> shader = pipeline->GetSpecifications().shader;
        VKShader* vkShader = static_cast<VKShader*>(shader.get());
        const uint32_t program = vkShader->Program().GetId();

        if (NeedsPassGlobalsUpdate(program, PassGlobalsKind::DepthSweep))
        {
            shader->SetBool("depthPrepass", true);
            if (std::find(m_PrepassShaders.begin(), m_PrepassShaders.end(), shader) == m_PrepassShaders.end())
                m_PrepassShaders.push_back(shader);
        }

        if (command.bindCameraState && NeedsPassGlobalsUpdate(program, PassGlobalsKind::Camera))
        {
            RenderView currentView;
            if (m_Scene->GetCurrentView(currentView))
            {
                shader->SetMat4("uProjection", currentView.projection);
                shader->SetMat4("uView", currentView.view);
                shader->SetBool("uIsOrtho", currentView.orthographic);
            }
        }

        if (m_LastDraw.pipeline != pipeline.get() || m_LastDraw.modelMatrix != command.modelMatrix)
            shader->SetMat4("model", command.modelMatrix);

        m_LastDraw.pass = nullptr;
        m_LastDraw.pipeline = pipeline.get();
        m_LastDraw.material = nullptr;
        m_LastDraw.modelMatrix = command.modelMatrix;

        if (command.indexCount == 0)
            return;

        IssueDraw(command, pipeline, false, true);
    }

    void VKRendererAPI::ExecuteDrawCommand(const DrawCommand& command, const std::shared_ptr<RenderPass> pass)
    {
        if (m_Sweep == DepthSweep::Prepass)
        {
            ExecuteDepthOnlyDraw(command);
            return;
        }

        m_DepthEqual = m_Sweep == DepthSweep::Equal && IsDepthPrepassCandidate(command, *pass);

        std::shared_ptr<Pipeline> pipeline = pass->overridePipeline ? pass->customPipeline : command.material->GetPipeline();

        // A pass that draws the materials with another permutation of their shader (the G-buffer of the deferred pipeline)
        if (!pass->overridePipeline && !pass->shaderVariant.empty())
        {
            if (std::shared_ptr<Pipeline> variant = pipeline->GetVariant(pass->shaderVariant))
                pipeline = variant;
        }

        VKShader* vkShader = static_cast<VKShader*>(pipeline->GetSpecifications().shader.get());
        if (!vkShader || !vkShader->Program().IsValid())
            return;

        const uint32_t program = vkShader->Program().GetId();
        const std::shared_ptr<Shader>& shader = pipeline->GetSpecifications().shader;

        const Material* drawMaterial = pass->overridePipeline ? nullptr : command.material.get();
        const bool sameAsLastDraw = !command.fullscreenTri
            && m_LastDraw.pass == pass.get()
            && m_LastDraw.pipeline == pipeline.get()
            && m_LastDraw.material == drawMaterial;

        if (!command.fullscreenTri)
        {
            if (sameAsLastDraw)
            {
                // Nothing scene-wide or material-specific can have changed since the previous draw : only what is genuinely per-object
                if (command.modelMatrix != m_LastDraw.modelMatrix)
                    shader->SetMat4("model", command.modelMatrix);
                if (command.objectID != m_LastDraw.objectID)
                    shader->SetInt("objID", command.objectID);
                if (command.lightmapHandle != m_LastDraw.lightmapHandle || command.lightmapScaleOffset != m_LastDraw.lightmapScaleOffset)
                    BindLightmap(command, shader);
            }
            else
            {
                m_Scene->BindWorldState(*this, shader, command.modelMatrix, command.objectID, NeedsPassGlobalsUpdate(program, PassGlobalsKind::World));
                BindLightmap(command, shader);
            }

            m_LastDraw.pass = pass.get();
            m_LastDraw.pipeline = pipeline.get();
            m_LastDraw.material = drawMaterial;
            m_LastDraw.modelMatrix = command.modelMatrix;
            m_LastDraw.objectID = command.objectID;
            m_LastDraw.lightmapHandle = command.lightmapHandle;
            m_LastDraw.lightmapScaleOffset = command.lightmapScaleOffset;
        }
        else
        {
            m_LastDraw = LastDraw{};
        }

        if (command.bindCameraState && NeedsPassGlobalsUpdate(program, PassGlobalsKind::Camera))
        {
            RenderView currentView;
            if (m_Scene->GetCurrentView(currentView))
            {
                shader->SetMat4("uProjection", currentView.projection);
                shader->SetMat4("uView", currentView.view);
                shader->SetBool("uIsOrtho", currentView.orthographic);
            }
        }

        if (sameAsLastDraw)
        {
            // Same pass, pipeline and material as the draw before : its uniforms and texture units are still set
        }
        else if (pass->overridePipeline)
        {
            BindPassData(pass, pipeline);
        }
        else
        {
            m_Scene->BindMaterialScene(command.material);
            BindPassData(pass, command.material);
            static_cast<VKMaterial*>(command.material.get())->Bind(pipeline->GetSpecifications().shader);
        }

        if (!command.fullscreenTri && command.indexCount == 0)
            return;

        IssueDraw(command, pipeline, command.fullscreenTri, false);
    }

    void VKRendererAPI::IssueDraw(const DrawCommand& command, const std::shared_ptr<Pipeline>& pipelineBase, bool fullscreen, bool depthOnly)
    {
        VKPipeline* pipeline = static_cast<VKPipeline*>(pipelineBase.get());
        VKShader* shader = pipeline->GetShader();
        if (!shader || !shader->Program().IsValid() || !HasTarget())
            return;

        if (fullscreen && command.proceduralVertexCount > 0 && command.instanceCount == 0)
            return;

        VKMesh* mesh = fullscreen ? nullptr : static_cast<VKMesh*>(command.mesh.get());
        if (!fullscreen && (!mesh || mesh->GetVertexBuffer() == VK_NULL_HANDLE))
            return;

        VK::Context& context = VK::Context::Get();

        // Layout changes cannot be recorded inside a pass : they come first
        PrepareSampledImages(*shader);

        if (!m_RenderingActive)
        {
            BeginRendering();
            if (!m_RenderingActive)
                return;
        }

        VkCommandBuffer cmd = context.Cmd();
        VKProgram& program = shader->Program();

        // ---- Pipeline ----
        const VertexLayout emptyLayout;
        PipelineVariant variant;
        variant.depthOnly = depthOnly;
        variant.wireframe = m_ForceLine;

        VkPipeline vkPipeline = pipeline->Get(m_ActiveTarget, fullscreen ? emptyLayout : mesh->GetVertexLayout(), variant);
        if (vkPipeline == VK_NULL_HANDLE)
            return;

        const bool layoutChanged = program.GetPipelineLayout() != m_BoundLayout;
        if (vkPipeline != m_BoundPipeline)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
            m_BoundPipeline = vkPipeline;
            m_BoundBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        }

        // ---- Dynamic state ----
        const PipelineSpecifications& specs = pipeline->GetSpecifications();

        bool depthTest = specs.depthTest;
        bool depthWrite = specs.depthWrite;
        VkCompareOp depthCompare = ToVkCompare(specs.depthCompare);

        if (m_Sweep == DepthSweep::Prepass)
        {
            depthTest = true;
            depthWrite = true;
            depthCompare = VK_COMPARE_OP_LESS;
        }
        else if (m_DepthEqual)
        {
            depthTest = true;
            depthWrite = false;
            depthCompare = VK_COMPARE_OP_EQUAL;
        }

        VkCullModeFlags cull = VK_CULL_MODE_NONE;
        switch (specs.cullMode)
        {
            case CullMode::Back:  cull = VK_CULL_MODE_BACK_BIT; break;
            case CullMode::Front: cull = VK_CULL_MODE_FRONT_BIT; break;
            default:              cull = VK_CULL_MODE_NONE; break;
        }

        VkViewport viewport = m_ViewportSet ? m_Viewport : VkViewport{0, 0, float(m_ActiveTarget.width), float(m_ActiveTarget.height), 0.0f, 1.0f};
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        if (!m_BoundViewportValid || std::memcmp(&viewport, &m_BoundViewport, sizeof(viewport)) != 0)
        {
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, {m_ActiveTarget.width, m_ActiveTarget.height}};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            m_BoundScissor = scissor;
            m_BoundViewport = viewport;
            m_BoundViewportValid = true;
        }

        {
            const VkRect2D scissor = m_ScissorSet ? m_Scissor : VkRect2D{{0, 0}, {m_ActiveTarget.width, m_ActiveTarget.height}};
            if (std::memcmp(&scissor, &m_BoundScissor, sizeof(scissor)) != 0)
            {
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                m_BoundScissor = scissor;
            }
        }

        if (!m_DynamicStateValid || cull != m_CullMode)
        {
            vkCmdSetCullMode(cmd, cull);
            m_CullMode = cull;
        }
        if (!m_DynamicStateValid || depthTest != m_DepthTest)
        {
            vkCmdSetDepthTestEnable(cmd, depthTest ? VK_TRUE : VK_FALSE);
            m_DepthTest = depthTest;
        }
        if (!m_DynamicStateValid || depthWrite != m_DepthWrite)
        {
            vkCmdSetDepthWriteEnable(cmd, depthWrite ? VK_TRUE : VK_FALSE);
            m_DepthWrite = depthWrite;
        }
        if (!m_DynamicStateValid || depthCompare != m_DepthCompare)
        {
            vkCmdSetDepthCompareOp(cmd, depthCompare);
            m_DepthCompare = depthCompare;
        }
        m_DynamicStateValid = true;

        // ---- Descriptors ----
        BindDescriptors(program, VK_PIPELINE_BIND_POINT_GRAPHICS, layoutChanged);
        m_BoundLayout = program.GetPipelineLayout();

        // ---- Vertices and draw ----
        if (fullscreen)
        {
            const uint32_t vertices = command.proceduralVertexCount > 0 ? command.proceduralVertexCount : 3;
            const uint32_t instances = command.proceduralVertexCount > 0 ? command.instanceCount : 1;
            vkCmdDraw(cmd, vertices, instances, 0, 0);
            return;
        }

        VkBuffer vertexBuffer = mesh->GetVertexBuffer();
        VkBuffer indexBuffer = mesh->GetIndexBuffer();
        if (vertexBuffer != m_BoundVertexBuffer)
        {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
            m_BoundVertexBuffer = vertexBuffer;
        }
        if (indexBuffer != m_BoundIndexBuffer)
        {
            vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            m_BoundIndexBuffer = indexBuffer;
        }

        VKStorageBuffer* indirect = (m_IndirectBuffer != 0 && command.cullSlot >= 0) ? VKStorageBuffer::Find(m_IndirectBuffer) : nullptr;
        if (indirect)
            vkCmdDrawIndexedIndirect(cmd, indirect->GetBuffer(), VkDeviceSize(command.cullSlot) * 5 * sizeof(uint32_t), 1, 0);
        else
            vkCmdDrawIndexed(cmd, command.indexCount, 1, command.indexOffset, 0, 0);
    }

    // ---- Compute -------------------------------------------------------------------------------------------------------------------

    void VKRendererAPI::ExecuteComputeDispatch(const std::shared_ptr<ComputePipeline> pipelineBase, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        VKComputePipeline* pipeline = static_cast<VKComputePipeline*>(pipelineBase.get());
        VKComputeShader* shader = pipeline ? pipeline->GetShader() : nullptr;
        if (!shader || !shader->Program().IsValid())
            return;

        VKProgram& program = shader->Program();
        const ShaderReflection& reflection = program.GetBinary().reflection;

        uint32_t groups[3] = {groupsX, groupsY, groupsZ};
        for (int i = 0; i < 3; i++)
        {
            if (m_ComputeLimits.maxWorkGroupCount[i] > 0 && groups[i] > m_ComputeLimits.maxWorkGroupCount[i])
            {
                DEBUG_WARNING("Compute dispatch exceeds the device's maximum work group count : clamping to fit.");
                groups[i] = m_ComputeLimits.maxWorkGroupCount[i];
            }
        }

        EndRendering();
        VK::Context& context = VK::Context::Get();
        VkCommandBuffer cmd = context.Cmd();

        // What the shader samples is in its resting layout, what it stores to is in GENERAL
        for (const ReflectedBinding& binding : reflection.bindings)
        {
            if (binding.kind == BindingKind::SampledImage)
            {
                for (uint32_t j = 0; j < std::max(binding.count, 1u); j++)
                {
                    const uint32_t unit = program.GetSamplerUnit(binding, j);
                    TextureResource* texture = unit < m_TextureUnits.size() ? TextureResource::Find(m_TextureUnits[unit]) : nullptr;
                    if (texture && texture->image.IsValid())
                        texture->image.TransitionToResting(cmd);
                }
            }
            else if (binding.kind == BindingKind::StorageImage)
            {
                TextureResource* texture = binding.binding < m_ImageUnits.size() ? TextureResource::Find(m_ImageUnits[binding.binding].texture) : nullptr;
                if (texture && texture->image.IsValid())
                {
                    if (!texture->image.general)
                    {
                        texture->image.general = true;
                        texture->PublishBindless();
                    }
                    texture->image.Transition(cmd, VK_IMAGE_LAYOUT_GENERAL);
                }
            }
        }

        VkPipeline vkPipeline = pipeline->Get();
        if (vkPipeline == VK_NULL_HANDLE)
            return;

        const bool layoutChanged = program.GetPipelineLayout() != m_BoundLayout;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline);
        m_BoundPipeline = VK_NULL_HANDLE;       // the graphics pipeline that was bound is not any more

        BindDescriptors(program, VK_PIPELINE_BIND_POINT_COMPUTE, layoutChanged);
        m_BoundLayout = program.GetPipelineLayout();

        vkCmdDispatch(cmd, groups[0], groups[1], groups[2]);
    }

    // ---- Descriptor sets -----------------------------------------------------------------------------------------------------------

    void VKRendererAPI::BindDescriptors(VKProgram& program, VkPipelineBindPoint bindPoint, bool layoutChanged)
    {
        VK::Context& context = VK::Context::Get();
        VkCommandBuffer cmd = context.Cmd();
        const ShaderReflection& reflection = program.GetBinary().reflection;
        VkPipelineLayout layout = program.GetPipelineLayout();

        if (layoutChanged)
        {
            for (VkDescriptorSet& set : m_BoundSets)
                set = VK_NULL_HANDLE;
        }

        auto bind = [&](uint32_t index, VkDescriptorSet set, uint32_t dynamicOffsets = 0, const uint32_t* offsets = nullptr)
        {
            if (set == VK_NULL_HANDLE)
                return;

            // A set with a dynamic offset is bound again whenever it moves
            if (dynamicOffsets == 0 && m_BoundSets[index] == set)
                return;

            vkCmdBindDescriptorSets(cmd, bindPoint, layout, index, 1, &set, dynamicOffsets, offsets);
            m_BoundSets[index] = dynamicOffsets ? VK_NULL_HANDLE : set;
        };

        // ---- Set 0 : the textures, from the texture units ----
        {
            uint64_t key = HashCombine64(reinterpret_cast<uint64_t>(program.GetSetLayout(BindingSets::Textures)), 0x7E87);
            static thread_local std::vector<VkDescriptorImageInfo> infos;
            infos.clear();
            static thread_local std::vector<VkWriteDescriptorSet> writes;
            writes.clear();
            infos.reserve(64);

            struct Pending { const ReflectedBinding* binding; size_t first; };
            static thread_local std::vector<Pending> pending;
            pending.clear();

            for (const ReflectedBinding& binding : reflection.bindings)
            {
                if (binding.set != BindingSets::Textures || binding.kind != BindingKind::SampledImage)
                    continue;

                const size_t first = infos.size();
                for (uint32_t j = 0; j < std::max(binding.count, 1u); j++)
                {
                    const uint32_t unit = program.GetSamplerUnit(binding, j);
                    TextureResource* texture = unit < m_TextureUnits.size() ? TextureResource::Find(m_TextureUnits[unit]) : nullptr;

                    VkDescriptorImageInfo info{};
                    bool usable = texture && Matches(texture->image, texture->samplerKey, binding.samplerType) && !(m_RenderingActive && IsActiveAttachment(m_ActiveTarget, &texture->image));

                    // Reading what is drawn into, or a texture of another kind than the sampler : the dummy of the kind of the sampler
                    if (usable)
                    {
                        info.imageView = texture->image.view;
                        info.sampler = texture->GetSampler();
                        info.imageLayout = texture->image.RestingLayout();
                    }
                    else
                    {
                        const Image& dummy = context.GetDummyImage(binding.samplerType);
                        info.imageView = dummy.view;
                        info.sampler = context.GetDummySampler(binding.samplerType);
                        info.imageLayout = dummy.RestingLayout();
                    }

                    key = HashCombine64(key, reinterpret_cast<uint64_t>(info.imageView));
                    key = HashCombine64(key, reinterpret_cast<uint64_t>(info.sampler));
                    key = HashCombine64(key, static_cast<uint64_t>(info.imageLayout));
                    infos.push_back(info);
                }
                pending.push_back({&binding, first});
            }

            if (!pending.empty())
            {
                VkDescriptorSet set = context.FindCachedSet(key);
                if (set == VK_NULL_HANDLE)
                {
                    set = context.AllocateSet(program.GetSetLayout(BindingSets::Textures));
                    if (set != VK_NULL_HANDLE)
                    {
                        for (const Pending& p : pending)
                        {
                            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                            write.dstSet = set;
                            write.dstBinding = p.binding->binding;
                            write.descriptorCount = std::max(p.binding->count, 1u);
                            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                            write.pImageInfo = &infos[p.first];
                            writes.push_back(write);
                        }
                        vkUpdateDescriptorSets(context.GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
                        context.CacheSet(key, set);
                    }
                }
                bind(BindingSets::Textures, set);
            }
        }

        // ---- Set 1 : the storage buffers ----
        {
            uint64_t key = HashCombine64(reinterpret_cast<uint64_t>(program.GetSetLayout(BindingSets::StorageBuffers)), 0x5B0);
            static thread_local std::vector<VkDescriptorBufferInfo> infos;
            infos.clear();
            static thread_local std::vector<const ReflectedBinding*> bindings;
            bindings.clear();
            infos.reserve(16);

            for (const ReflectedBinding& binding : reflection.bindings)
            {
                if (binding.set != BindingSets::StorageBuffers || binding.kind != BindingKind::StorageBuffer)
                    continue;

                VKStorageBuffer* buffer = binding.binding < m_StorageBuffers.size() ? VKStorageBuffer::Find(m_StorageBuffers[binding.binding]) : nullptr;

                VkDescriptorBufferInfo info{};
                if (buffer && buffer->GetBuffer() != VK_NULL_HANDLE)
                {
                    info.buffer = buffer->GetBuffer();
                    info.range = VK_WHOLE_SIZE;
                }
                else
                {
                    info.buffer = m_DummyBuffer.buffer;
                    info.range = VK_WHOLE_SIZE;
                }

                key = HashCombine64(key, reinterpret_cast<uint64_t>(info.buffer));
                key = HashCombine64(key, binding.binding);
                infos.push_back(info);
                bindings.push_back(&binding);
            }

            if (!bindings.empty())
            {
                VkDescriptorSet set = context.FindCachedSet(key);
                if (set == VK_NULL_HANDLE)
                {
                    set = context.AllocateSet(program.GetSetLayout(BindingSets::StorageBuffers));
                    if (set != VK_NULL_HANDLE)
                    {
                        static thread_local std::vector<VkWriteDescriptorSet> writes;
                        writes.clear();
                        for (size_t i = 0; i < bindings.size(); i++)
                        {
                            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                            write.dstSet = set;
                            write.dstBinding = bindings[i]->binding;
                            write.descriptorCount = 1;
                            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                            write.pBufferInfo = &infos[i];
                            writes.push_back(write);
                        }
                        vkUpdateDescriptorSets(context.GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
                        context.CacheSet(key, set);
                    }
                }
                bind(BindingSets::StorageBuffers, set);
            }
        }

        // ---- Set 2 : the images the shader loads from and stores to ----
        {
            uint64_t key = HashCombine64(reinterpret_cast<uint64_t>(program.GetSetLayout(BindingSets::StorageImages)), 0x1A6E);
            static thread_local std::vector<VkDescriptorImageInfo> infos;
            infos.clear();
            static thread_local std::vector<const ReflectedBinding*> bindings;
            bindings.clear();
            infos.reserve(8);
            bool complete = true;

            for (const ReflectedBinding& binding : reflection.bindings)
            {
                if (binding.set != BindingSets::StorageImages || binding.kind != BindingKind::StorageImage)
                    continue;

                const ImageUnit unit = binding.binding < m_ImageUnits.size() ? m_ImageUnits[binding.binding] : ImageUnit{};
                TextureResource* texture = TextureResource::Find(unit.texture);
                if (!texture || !texture->image.IsValid() || !(texture->image.usage & VK_IMAGE_USAGE_STORAGE_BIT))
                {
                    complete = false;
                    continue;
                }

                VkDescriptorImageInfo info{};
                info.imageView = texture->image.SubView(std::min(unit.mip, texture->image.mips - 1), 1, 0, texture->image.layers, texture->image.viewType == VK_IMAGE_VIEW_TYPE_2D ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY);
                info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                key = HashCombine64(key, reinterpret_cast<uint64_t>(info.imageView));
                key = HashCombine64(key, binding.binding);
                infos.push_back(info);
                bindings.push_back(&binding);
            }

            if (!complete && bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
                DEBUG_WARNING("A compute shader ('", program.GetName(), "') has an image unit nothing is bound to.");

            if (!bindings.empty())
            {
                VkDescriptorSet set = context.FindCachedSet(key);
                if (set == VK_NULL_HANDLE)
                {
                    set = context.AllocateSet(program.GetSetLayout(BindingSets::StorageImages));
                    if (set != VK_NULL_HANDLE)
                    {
                        static thread_local std::vector<VkWriteDescriptorSet> writes;
                        writes.clear();
                        for (size_t i = 0; i < bindings.size(); i++)
                        {
                            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                            write.dstSet = set;
                            write.dstBinding = bindings[i]->binding;
                            write.descriptorCount = 1;
                            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                            write.pImageInfo = &infos[i];
                            writes.push_back(write);
                        }
                        vkUpdateDescriptorSets(context.GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
                        context.CacheSet(key, set);
                    }
                }
                bind(BindingSets::StorageImages, set);
            }
        }

        // ---- Set 3 : the uniforms set by name ----
        uint32_t uniformOffset = 0;
        if (program.FlushUniforms(uniformOffset))
            bind(BindingSets::Uniforms, program.GetUniformSet(), 1, &uniformOffset);

        // ---- Set 4 : the bindless textures ----
        for (const ReflectedBinding& binding : reflection.bindings)
        {
            if (binding.kind == BindingKind::BindlessTextures)
            {
                bind(BindingSets::Bindless, context.GetBindlessSet());
                break;
            }
        }
    }

}
