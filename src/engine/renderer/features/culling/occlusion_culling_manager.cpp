#include "occlusion_culling_manager.hpp"

#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/shader/compute_shader.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"

#include "engine/core/diagnostics/logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Shard::Engine::Rendering {

    static_assert(sizeof(glm::mat4) == 64 && sizeof(glm::vec4) == 16 && sizeof(glm::uvec4) == 16,
        "InstanceGPU must stay tightly packed to match occlusion_cull.comp's std430 Instance");

    namespace {

        std::shared_ptr<ComputePipeline> MakePipeline(IRenderContext* renderer, const std::shared_ptr<ComputeShader>& shader, const char* debugName)
        {
            ComputePipelineSpecifications specs;
            specs.shader = shader;
            specs.debugName = debugName;
            return renderer->GetOrAddComputePipeline(specs);
        }

        uint32_t NextPowerOfTwo(uint32_t value)
        {
            uint32_t result = 1;
            while (result < value)
                result <<= 1;
            return result;
        }
    }

    void OcclusionCullingManager::Init(IRenderContext* renderer)
    {
        m_Renderer = renderer;
        m_Ready = false;

        Filesystem::Path resRoot = Core::GetEngine().GetFileManager()->GetEngineResRoot();

        m_HiZInitShader = ComputeShader::Create(resRoot / "shaders/compute/culling/hiz_init.comp");
        m_HiZInitMSShader = ComputeShader::Create(resRoot / "shaders/compute/culling/hiz_init_ms.comp");
        m_HiZReduceShader = ComputeShader::Create(resRoot / "shaders/compute/culling/hiz_reduce.comp");
        m_CullShader = ComputeShader::Create(resRoot / "shaders/compute/culling/occlusion_cull.comp");

        // ComputeShader::Create hands back an object even when the file is missing (it then has no name and no program),
        // and dispatching that is a silent no-op - every indirect command would stay zero and nothing would be drawn.
        const auto loaded = [](const std::shared_ptr<ComputeShader>& shader) { return shader && !shader->GetShaderName().empty(); };

        if (!loaded(m_HiZInitShader) || !loaded(m_HiZInitMSShader) || !loaded(m_HiZReduceShader) || !loaded(m_CullShader))
        {
            DEBUG_ERROR("OcclusionCullingManager : failed to load the shaders in shaders/compute/culling - GPU occlusion culling is disabled "
                "(is engine_resources up to date next to the executable?)");
            return;
        }

        m_HiZInitPipeline = MakePipeline(renderer, m_HiZInitShader, "HiZInit");
        m_HiZInitMSPipeline = MakePipeline(renderer, m_HiZInitMSShader, "HiZInitMS");
        m_HiZReducePipeline = MakePipeline(renderer, m_HiZReduceShader, "HiZReduce");
        m_CullPipeline = MakePipeline(renderer, m_CullShader, "OcclusionCull");

        m_Ready = m_HiZInitPipeline && m_HiZInitMSPipeline && m_HiZReducePipeline && m_CullPipeline;
    }

    bool OcclusionCullingManager::IsCullable(const DrawCommand& command)
    {
        return !command.fullscreenTri
            && command.mesh
            && command.material
            && command.indexCount > 0
            && command.material->GetOpacity() != Opacity::Translucent;
    }

    void OcclusionCullingManager::EnsureCapacity(PassState& pass, uint32_t slots, bool& instancesDirty)
    {
        if (slots <= pass.capacity)
            return;

        const uint32_t capacity = std::max(64u, NextPowerOfTwo(slots));

        const size_t previousSize = pass.cpuInstances.size();
        pass.cpuInstances.resize(capacity);
        std::memset(pass.cpuInstances.data() + previousSize, 0, (capacity - previousSize) * sizeof(InstanceGPU));

        // The visibility history starts empty with the new buffer : it is only a hint for phase 1, so losing it
        // costs one frame of everything being drawn in phase 2 instead, never a wrong image.
        std::vector<uint32_t> zeroState(capacity, 0u);
        std::vector<uint32_t> zeroCommands((size_t)capacity * 5, 0u);

        pass.instances = StorageBuffer::Create(capacity * (uint32_t)sizeof(InstanceGPU));
        pass.state = StorageBuffer::Create(capacity * (uint32_t)sizeof(uint32_t));
        pass.commands = StorageBuffer::Create(capacity * 5 * (uint32_t)sizeof(uint32_t));

        pass.state->SetData(zeroState.data(), capacity * (uint32_t)sizeof(uint32_t));
        pass.commands->SetData(zeroCommands.data(), capacity * 5 * (uint32_t)sizeof(uint32_t));

        pass.capacity = capacity;
        instancesDirty = true;
    }

    void OcclusionCullingManager::EnsureHiZ(PassState& pass, uint32_t width, uint32_t height)
    {
        if (pass.hiz && pass.hizWidth == width && pass.hizHeight == height)
            return;

        TextureSpecifications specs;
        specs.width = width;
        specs.height = height;
        specs.format = TextureFormat::RED;
        specs.internalFormat = TextureInternalFormat::R32F;
        // texelFetch only, but a mip-filtered min filter keeps every level of the immutable chain "complete"
        specs.minFilter = TextureFilter::NearestMipmapNearest;
        specs.magFilter = TextureFilter::Nearest;
        specs.wrapS = TextureWrap::ClampEdge;
        specs.wrapT = TextureWrap::ClampEdge;
        specs.generateMips = true;       // allocates the whole chain
        specs.immutableStorage = true;   // required to bind levels as images

        pass.hiz = Texture2D::Create(specs, static_cast<const void*>(nullptr));
        pass.hizWidth = width;
        pass.hizHeight = height;
        pass.hizLevels = (uint32_t)std::floor(std::log2((double)std::max(width, height))) + 1;
    }

    void OcclusionCullingManager::DispatchCull(const std::string& passKey, PassState& pass, const glm::mat4& viewProj, int phase)
    {
        m_CullPipeline->Bind();

        pass.instances->Bind(kInstanceBinding);
        pass.state->Bind(kStateBinding);
        pass.commands->Bind(kCommandBinding);
        pass.hiz->Bind(kHiZUnit);

        m_CullShader->SetMat4("viewProj", viewProj);
        m_CullShader->SetInt("instanceCount", (int)pass.slotCount);
        m_CullShader->SetInt("phase", phase);
        m_CullShader->SetInt("hizLevels", (int)pass.hizLevels);
        m_CullShader->SetInt("hizSizeX", (int)pass.hizWidth);
        m_CullShader->SetInt("hizSizeY", (int)pass.hizHeight);

        // ShaderStorage : phase 2 reads the state phase 1 wrote. Command : the draws that follow read the commands.
        MemoryBarrierBit barriers = MemoryBarrierBit::ShaderStorage | MemoryBarrierBit::Command;
        if (m_CollectStats)
            barriers = barriers | MemoryBarrierBit::BufferUpdate;

        m_Renderer->DispatchCompute(m_CullPipeline, (pass.slotCount + 63) / 64, 1, 1, barriers);

        if (m_CollectStats)
        {
            std::vector<uint32_t> commands((size_t)pass.slotCount * 5);
            pass.commands->GetData(commands.data(), (uint32_t)(commands.size() * sizeof(uint32_t)));

            PassStats& stats = m_Stats[passKey];
            uint32_t drawn = 0;
            uint64_t triangles = 0;
            for (uint32_t slot = 0; slot < pass.slotCount; slot++)
            {
                if (commands[slot * 5 + 1] != 0)
                {
                    drawn++;
                    triangles += commands[slot * 5] / 3;
                }
            }

            if (phase == 1)
            {
                stats = PassStats{};
                stats.cullableCommands = (uint32_t)pass.slotOf.size();
                for (const InstanceGPU& instance : pass.cpuInstances)
                    if (instance.boundsMin.w > 0.5f)
                        stats.trianglesCullable += instance.draw.x / 3;
                stats.drawnInPhase1 = drawn;
                stats.trianglesDrawn = triangles;
            }
            else
            {
                stats.drawnInPhase2 = drawn;
                stats.trianglesDrawn += triangles;
            }
        }
    }

    void OcclusionCullingManager::BuildHiZ(PassState& pass, const Target& target)
    {
        RendererAPI* api = m_Renderer->GetRendererAPI();

        // ---- Level 0 from the pass's depth ----
        auto& initPipeline = target.multisampled ? m_HiZInitMSPipeline : m_HiZInitPipeline;
        auto& initShader = target.multisampled ? m_HiZInitMSShader : m_HiZInitShader;

        initPipeline->Bind();
        api->BindTextureUnit(kDepthUnit, target.depthTexture);
        pass.hiz->BindImage(0, TextureAccess::WriteOnly, 0);

        initShader->SetInt("sizeX", (int)pass.hizWidth);
        initShader->SetInt("sizeY", (int)pass.hizHeight);
        if (target.multisampled)
            initShader->SetInt("sampleCount", (int)target.samples);

        m_Renderer->DispatchCompute(initPipeline, (pass.hizWidth + 7) / 8, (pass.hizHeight + 7) / 8, 1,
            MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);

        // ---- Each further level is the farthest of the 2x2 (or 3x2 / 2x3 / 3x3 at an odd edge) texels above it ----
        uint32_t srcWidth = pass.hizWidth;
        uint32_t srcHeight = pass.hizHeight;

        for (uint32_t level = 1; level < pass.hizLevels; level++)
        {
            const uint32_t dstWidth = std::max(srcWidth / 2, 1u);
            const uint32_t dstHeight = std::max(srcHeight / 2, 1u);

            m_HiZReducePipeline->Bind();
            pass.hiz->BindImage(0, TextureAccess::ReadOnly, level - 1);
            pass.hiz->BindImage(1, TextureAccess::WriteOnly, level);

            m_HiZReduceShader->SetInt("srcSizeX", (int)srcWidth);
            m_HiZReduceShader->SetInt("srcSizeY", (int)srcHeight);
            m_HiZReduceShader->SetInt("dstSizeX", (int)dstWidth);
            m_HiZReduceShader->SetInt("dstSizeY", (int)dstHeight);

            m_Renderer->DispatchCompute(m_HiZReducePipeline, (dstWidth + 7) / 8, (dstHeight + 7) / 8, 1,
                MemoryBarrierBit::ImageAccess | MemoryBarrierBit::TextureFetch);

            srcWidth = dstWidth;
            srcHeight = dstHeight;
        }
    }

    bool OcclusionCullingManager::PrepareFirstPhase(const std::string& passKey, std::vector<DrawCommand>& drawList, const glm::mat4& viewProj, const Target& target)
    {
        if (!m_Ready || target.width == 0 || target.height == 0 || target.depthTexture == 0)
            return false;

        PassState& pass = m_Passes[passKey];
        pass.frame++;

        bool instancesDirty = false;
        size_t found = 0;

        for (DrawCommand& command : drawList)
        {
            if (!IsCullable(command))
            {
                command.cullSlot = -1;
                continue;
            }

            uint32_t slot;
            auto it = pass.slotOf.find(command.commandID);
            if (it != pass.slotOf.end())
            {
                slot = it->second;
            }
            else
            {
                if (!pass.freeSlots.empty())
                {
                    slot = pass.freeSlots.back();
                    pass.freeSlots.pop_back();
                }
                else
                {
                    slot = pass.slotCount++;
                }
                pass.slotOf.emplace(command.commandID, slot);
            }

            if (slot >= pass.cpuInstances.size())
            {
                const size_t previousSize = pass.cpuInstances.size();
                pass.cpuInstances.resize((size_t)slot + 1);
                std::memset(pass.cpuInstances.data() + previousSize, 0, (pass.cpuInstances.size() - previousSize) * sizeof(InstanceGPU));
            }
            if (slot >= pass.lastSeen.size())
                pass.lastSeen.resize((size_t)slot + 1, 0u);
            pass.lastSeen[slot] = pass.frame;

            InstanceGPU instance;
            std::memset(&instance, 0, sizeof(instance)); // padding included, so the comparison below is exact
            instance.model = command.modelMatrix;
            instance.boundsMin = glm::vec4(command.boundsMin, 1.0f);
            instance.boundsMax = glm::vec4(command.boundsMax, 0.0f);
            instance.draw = glm::uvec4(command.indexCount, command.indexOffset, 0u, 0u);

            if (std::memcmp(&pass.cpuInstances[slot], &instance, sizeof(InstanceGPU)) != 0)
            {
                pass.cpuInstances[slot] = instance;
                instancesDirty = true;
            }

            command.cullSlot = (int32_t)slot;
            found++;
        }

        // Commands that left the list : free their slots (and mark them dead for the compute pass)
        if (pass.slotOf.size() != found)
        {
            for (auto it = pass.slotOf.begin(); it != pass.slotOf.end();)
            {
                if (pass.lastSeen[it->second] == pass.frame)
                {
                    ++it;
                    continue;
                }

                std::memset(&pass.cpuInstances[it->second], 0, sizeof(InstanceGPU));
                pass.freeSlots.push_back(it->second);
                instancesDirty = true;
                it = pass.slotOf.erase(it);
            }
        }

        if (found == 0)
            return false;

        EnsureCapacity(pass, pass.slotCount, instancesDirty);
        EnsureHiZ(pass, target.width, target.height);

        if (instancesDirty)
            pass.instances->SetData(pass.cpuInstances.data(), pass.capacity * (uint32_t)sizeof(InstanceGPU));

        DispatchCull(passKey, pass, viewProj, 1);
        return true;
    }

    void OcclusionCullingManager::PrepareSecondPhase(const std::string& passKey, const glm::mat4& viewProj, const Target& target)
    {
        auto it = m_Passes.find(passKey);
        if (it == m_Passes.end())
            return;

        PassState& pass = it->second;

        BuildHiZ(pass, target);
        DispatchCull(passKey, pass, viewProj, 2);
    }

    const OcclusionCullingManager::PassStats* OcclusionCullingManager::GetStats(const std::string& passKey) const
    {
        auto it = m_Stats.find(passKey);
        return it == m_Stats.end() ? nullptr : &it->second;
    }

    uint32_t OcclusionCullingManager::GetIndirectBuffer(const std::string& passKey) const
    {
        auto it = m_Passes.find(passKey);
        if (it == m_Passes.end() || !it->second.commands)
            return 0;

        return it->second.commands->GetHandle();
    }

    void OcclusionCullingManager::Forget(const std::string& passKey)
    {
        m_Passes.erase(passKey);
        m_Stats.erase(passKey);
    }
}
