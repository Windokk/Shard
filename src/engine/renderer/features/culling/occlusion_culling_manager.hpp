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

    // GPU occlusion culling for the main camera's geometry passes (ForwardPass, the SSAO depth prepass). Whether a
    // draw command is issued is decided by a compute shader and consumed through indirect draws : each culled
    // command owns a slot in a buffer of DrawElementsIndirectCommands, the compute pass writes that slot's
    // instanceCount (1 = draw, 0 = nothing), and the CPU keeps walking the draw list exactly as before, just issuing
    // glDrawElementsIndirect for those commands. No readback, no latency, no CPU-side visibility state.
    //
    // The occluders are the pass's own depth, so there is no separate occluder pass : each frame is two phases
    // (Wihlidal / Aaltonen "two-phase occlusion culling"),
    //
    //   1. PrepareFirstPhase()  frustum-cull, keep what was visible LAST frame -> the caller draws those
    //   2. PrepareSecondPhase() build a Hi-Z (farthest-depth) pyramid from the depth phase 1 just produced, then test
    //                           EVERY command against it -> the caller draws the ones phase 1 didn't
    //
    // Last frame's visibility is only a guess at what to draw first : phase 2 re-tests everything against this frame's
    // own depth, so a newly revealed object is drawn the same frame it appears (no popping), and the final image is
    // the one a draw-everything pass would produce. A wrong guess costs speed, never correctness.
    //
    // Each pass (keyed by name) owns independent GPU state : its slot assignment, visibility history and pyramid.
    class OcclusionCullingManager
    {
        public:
            /// What the culling needs to know about the pass's render target
            struct Target
            {
                uint32_t depthTexture = 0;   ///< the API's name for the target's depth texture
                bool multisampled = false;
                uint32_t samples = 1;
                uint32_t width = 0;
                uint32_t height = 0;
            };

            /// Loads the shaders. When that fails IsReady() stays false and the renderer draws without culling.
            void Init(IRenderContext* renderer);

            bool IsReady() const { return m_Ready; }

            /// Whether a draw command is eligible for GPU culling : an indexed, opaque or masked mesh draw. Translucent
            /// geometry (it has to be blended after everything behind it, so it can't be reordered into a phase) and
            /// fullscreen/immediate commands are always drawn.
            static bool IsCullable(const DrawCommand& command);

            /// Phase 1. Assigns every cullable command in `drawList` a slot (DrawCommand::cullSlot, -1 for the rest),
            /// uploads whatever changed, and records which of them to draw first. `viewProj` is the camera's.
            /// Returns false when nothing in the list is cullable (or the pass can't be culled), in which case the
            /// list is left drawable the plain way.
            bool PrepareFirstPhase(const std::string& passKey, std::vector<DrawCommand>& drawList, const glm::mat4& viewProj, const Target& target);

            /// Phase 2 : call once the phase-1 draws are done and the pass's depth holds their result.
            void PrepareSecondPhase(const std::string& passKey, const glm::mat4& viewProj, const Target& target);

            /// Buffer holding the pass's indirect draw commands, for RendererAPI::SetIndirectDrawBuffer
            uint32_t GetIndirectBuffer(const std::string& passKey) const;

            /// Drops a pass's GPU state (it was removed, or the world changed under it)
            void Forget(const std::string& passKey);

            /// What the last frame of a pass drew. Only filled while stats collection is on.
            struct PassStats
            {
                uint32_t cullableCommands = 0;    ///< commands the GPU decides about
                uint32_t drawnInPhase1 = 0;       ///< of those, drawn from last frame's visibility
                uint32_t drawnInPhase2 = 0;       ///< of those, drawn after the Hi-Z test on top of phase 1
                uint64_t trianglesCullable = 0;   ///< triangles of all the cullable commands (what drawing them all costs)
                uint64_t trianglesDrawn = 0;      ///< triangles actually submitted by both phases
            };

            /// Debug aid : reads the indirect commands back after every cull dispatch to fill PassStats. That is a
            /// blocking GPU round-trip per dispatch - leave it off outside of measuring.
            void SetCollectStats(bool collect) { m_CollectStats = collect; }
            bool IsCollectingStats() const { return m_CollectStats; }
            const PassStats* GetStats(const std::string& passKey) const;

        private:
            // Mirrors occlusion_cull.comp's Instance - keep in sync. 112 bytes, a multiple of 16 as std430 requires.
            struct InstanceGPU
            {
                glm::mat4 model;
                glm::vec4 boundsMin; // w = 1 live slot, 0 free slot
                glm::vec4 boundsMax;
                glm::uvec4 draw;     // x = index count, y = first index
            };

            struct PassState
            {
                // commandID -> slot, so a command keeps its slot (and its visibility history) across frames
                std::unordered_map<uint64_t, uint32_t> slotOf;
                std::vector<uint32_t> freeSlots;
                std::vector<uint32_t> lastSeen;
                uint32_t slotCount = 0;  // slots ever handed out; the compute dispatch covers [0, slotCount)
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

            // StorageBuffer binding points : high on purpose, 0-15 belong to lit.frag, the light culling and the probes
            static constexpr uint32_t kInstanceBinding = 20;
            static constexpr uint32_t kStateBinding = 21;
            static constexpr uint32_t kCommandBinding = 22;
            // Texture units, also above the 0-31 the forward shader samples from
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
