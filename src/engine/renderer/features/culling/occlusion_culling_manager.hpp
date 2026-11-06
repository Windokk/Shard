#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class StorageBuffer;
    class ComputeShader;
    class ComputePipeline;
    class Texture2D;
    struct DrawCommand;

    // Two-phase GPU occlusion culling (Wihlidal/Aaltonen). Phase 1 draws last frame's visible set, phase 2 tests everything against a Hi-Z built from that depth.
    class OcclusionCullingManager
    {
        public:
            struct Target
            {
                uint32_t depthTexture = 0;
                bool multisampled = false;
                uint32_t samples = 1;
                uint32_t width = 0;
                uint32_t height = 0;
            };

            void Init(IRenderContext* renderer);

            bool IsReady() const { return m_Ready; }

            static bool IsCullable(const DrawCommand& command);

            bool PrepareFirstPhase(const std::string& passKey, std::vector<DrawCommand>& drawList, const glm::mat4& viewProj, const Target& target);

            void PrepareSecondPhase(const std::string& passKey, const glm::mat4& viewProj, const Target& target);

            uint32_t GetIndirectBuffer(const std::string& passKey) const;

            void Forget(const std::string& passKey);

            struct PassStats
            {
                uint32_t cullableCommands = 0;
                uint32_t drawnInPhase1 = 0;
                uint32_t drawnInPhase2 = 0;
                uint64_t trianglesCullable = 0;
                uint64_t trianglesDrawn = 0;
            };

            // Blocking GPU readback per dispatch. Leave off outside of measuring.
            void SetCollectStats(bool collect) { m_CollectStats = collect; }
            bool IsCollectingStats() const { return m_CollectStats; }
            const PassStats* GetStats(const std::string& passKey) const;

        private:
            struct InstanceGPU
            {
                glm::mat4 model;
                glm::vec4 boundsMin;
                glm::vec4 boundsMax;
                glm::uvec4 draw;
            };

            struct PassState
            {
                std::unordered_map<uint64_t, uint32_t> slotOf;
                std::vector<uint32_t> freeSlots;
                std::vector<uint32_t> lastSeen;
                uint32_t slotCount = 0;
                uint32_t capacity = 0;
                uint32_t frame = 0;

                std::vector<InstanceGPU> cpuInstances;

                std::shared_ptr<StorageBuffer> instances;
                std::shared_ptr<StorageBuffer> state;
                std::shared_ptr<StorageBuffer> commands;

                std::shared_ptr<Texture2D> hiz;
                uint32_t hizWidth = 0;
                uint32_t hizHeight = 0;
                uint32_t hizLevels = 0;
            };

            void EnsureCapacity(PassState& pass, uint32_t slots, bool& instancesDirty);
            void EnsureHiZ(PassState& pass, uint32_t width, uint32_t height);
            void BuildHiZ(PassState& pass, const Target& target);
            void DispatchCull(const std::string& passKey, PassState& pass, const glm::mat4& viewProj, int phase);

            // High on purpose, 0-15 belong to lit.frag, light culling and the probes.
            static constexpr uint32_t kInstanceBinding = 20;
            static constexpr uint32_t kStateBinding = 21;
            static constexpr uint32_t kCommandBinding = 22;
            static constexpr uint32_t kDepthUnit = 47;
            static constexpr uint32_t kHiZUnit = 48;

            IRenderContext* m_Renderer = nullptr;
            bool m_Ready = false;

            std::shared_ptr<ComputeShader> m_HiZInitShader;
            std::shared_ptr<ComputePipeline> m_HiZInitPipeline;
            std::shared_ptr<ComputeShader> m_HiZInitMSShader;
            std::shared_ptr<ComputePipeline> m_HiZInitMSPipeline;
            std::shared_ptr<ComputeShader> m_HiZReduceShader;
            std::shared_ptr<ComputePipeline> m_HiZReducePipeline;
            std::shared_ptr<ComputeShader> m_CullShader;
            std::shared_ptr<ComputePipeline> m_CullPipeline;

            std::unordered_map<std::string, PassState> m_Passes;

            bool m_CollectStats = false;
            std::unordered_map<std::string, PassStats> m_Stats;
    };
}
