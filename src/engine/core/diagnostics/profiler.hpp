#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <atomic>
#include <functional>
#include <mutex>
#include <map>
#include <deque>

namespace Shard::Engine::Debugging{

    // Engine subsystems tracked by the per-frame performance profiler.
    // "Other" is not sampled directly: it is derived as (total frame time - sum of the rest),
    // so it captures whatever isn't wrapped in a ProfileCategory scope (OS/driver overhead, ...).
    // "Waiting" is the time the simulation thread spends blocked on the render thread (the frame pacing : it does not
    // run further ahead of the drawn frames than the limit). It is idle time, which is what a frame that is limited by
    // the display (vsync) or the GPU is made of : without its own bucket it all ended up in "Other".
    enum class ProfileCategory : uint8_t {
        Physics = 0,
        Audio,
        Scripting,
        Rendering,
        Input,
        Presentation,
        Waiting,
        Other,
        COUNT
    };

    inline const char* ProfileCategoryName(ProfileCategory category){
        switch(category){
            case ProfileCategory::Physics:      return "Physics";
            case ProfileCategory::Audio:        return "Audio";
            case ProfileCategory::Scripting:    return "Scripting";
            case ProfileCategory::Rendering:    return "Rendering";
            case ProfileCategory::Input:        return "Input";
            case ProfileCategory::Presentation: return "Presentation";
            case ProfileCategory::Waiting:      return "Waiting";
            case ProfileCategory::Other:        return "Other";
            default:                            return "Unknown";
        }
    }

    inline constexpr size_t kProfileCategoryCount = static_cast<size_t>(ProfileCategory::COUNT);

    // Fine-grained sub-measurements nested within ProfileCategory::Rendering. Unlike the top-level
    // categories, these overlap with (are part of) the Rendering time and are not summed into it :
    // they exist purely to show what inside Rendering is the heaviest.
    // Together they span the whole of Renderer::Render() (see Renderer::BeginFrame/DrawFrame/EndFrame) :
    // per-frame subsystem updates (camera, shadows, GI probes, SSAO), then per-draw-call pass execution
    // (setup, culling, state binding, submission), then the multisample resolve at the end of the frame.
    enum class RenderSubSample : uint8_t {
        CameraUpdate = 0,
        ShadowUpdate,
        GIProbeUpdate,
        SSAOUpdate,
        LightCullingUpdate,
        PassSetup,
        Culling,
        StateBinding,
        DrawElements,
        MultisampleResolve,
        COUNT
    };

    inline const char* RenderSubSampleName(RenderSubSample sample){
        switch(sample){
            case RenderSubSample::CameraUpdate:       return "Camera update";
            case RenderSubSample::ShadowUpdate:       return "Shadow maps";
            case RenderSubSample::GIProbeUpdate:      return "GI probes (DDGI)";
            case RenderSubSample::SSAOUpdate:         return "SSAO";
            case RenderSubSample::LightCullingUpdate: return "Light culling (Forward+)";
            case RenderSubSample::PassSetup:          return "Pass setup";
            case RenderSubSample::Culling:            return "Culling";
            case RenderSubSample::StateBinding:       return "State binding";
            case RenderSubSample::DrawElements:       return "glDrawElements";
            case RenderSubSample::MultisampleResolve: return "MSAA resolve";
            default:                                  return "Unknown";
        }
    }

    inline constexpr size_t kRenderSubSampleCount = static_cast<size_t>(RenderSubSample::COUNT);

    struct FrameProfile {
        std::array<float, kProfileCategoryCount> categoryMs{};
        std::array<float, kRenderSubSampleCount> renderSubMs{};
        float totalMs = 0.0f;
        // The render thread's own time for this same frame : from when it began it to the end of its swap (0 : no render thread).
        // totalMs is the simulation thread's time for the frame. The two overlap, they are not summed.
        float renderMs = 0.0f;
        // The frame from end to end : from its start on the simulation thread to the end of its swap on the render thread (both
        // threads' parts of it, overlap included). Equals totalMs when there is no render thread.
        float frameMs = 0.0f;
    };

    struct MinimalStatistics {
        //Audio
        int sounds = 0;

        /// Rendering
        float frameTimeMs = 0;
        float fps = 0;

        ////// Draw list
        int cmds = 0;
        int primitives = 0;
        int vertices = 0;

        ////// Memory usage
        float gpuMemoryMB = 0;
        float ramUsageMB = 0;

        //World
        int actors = 0;
        int lights = 0;
    };

    struct SystemCapabilities
    {
        // Feature Support
        bool supportHardwareRayTracing;
        bool supportMeshShaders;
        bool supportTaskShaders;
        bool supportBindlessTextures;
        bool supportVariableRateShading;
        bool supportSamplerAnisotropy;
        bool supportConservativeRasterization;
        bool supportMultiDrawIndirect;
        bool supportIndirectFirstInstance;
        bool supportDrawIndirectCount;
        bool supportPipelineStatisticsQuery;
        bool supportTimestampQuery;
        bool supportOcclusionQuery;
        bool supportTextureCompressionBC;
        bool supportTextureCompressionASTC;
        bool supportTextureCompressionETC2;
        bool supportCubeMapArrays;
        bool supportArrayTextures;
        bool support3DTextures;
        bool supportDepthClamp;
        bool supportDepthBiasClamp;
        bool supportWideLines;
        bool supportLargePoints;
        bool supportGeometryShaders;
        bool supportTessellationShaders;
        bool supportComputeShaders;

        // Texture Limits
        uint32_t maxTexture1DSize;
        uint32_t maxTexture2DSize;
        uint32_t maxTexture3DSize;
        uint32_t maxTextureCubeSize;
        uint32_t maxTextureArrayLayers;
        uint32_t maxTextures;
        uint32_t maxSamplers;
        uint32_t maxAnisotropy;

        // Buffer Limits
        uint32_t maxUniformBufferSize;
        uint32_t maxStorageBufferSize;
        uint32_t maxPushConstants;
        uint32_t maxVertexBufferBindings;
        uint32_t maxVertexAttributes;
        uint32_t maxVertexBufferStride;

        // Shader Limits
        uint32_t maxComputeSharedMemorySize;
        uint32_t maxComputeWorkGroupInvocations;
        uint32_t maxComputeWorkGroupSizeX;
        uint32_t maxComputeWorkGroupSizeY;
        uint32_t maxComputeWorkGroupSizeZ;

        // Rendering Limits
        uint32_t maxColorAttachments;
        uint32_t maxRenderTargets;
        uint32_t maxFramebufferWidth;
        uint32_t maxFramebufferHeight;
        uint32_t maxFramebufferLayers;
        uint32_t maxViewports;
        uint32_t maxScissors;

        // Draw / Scene Limits
        uint32_t maxDrawCallsPerFrame;
        uint32_t maxInstancesPerDraw;
        uint32_t maxVerticesPerMesh;
        uint32_t maxIndicesPerMesh;
        uint32_t maxMeshes;
        uint32_t maxMaterials;
        uint32_t maxLights;

        // Ray Tracing Limits
        uint32_t maxRayRecursionDepth;
        uint32_t maxAccelerationStructures;
        uint32_t maxShaderGroups;

        // Memory / GPU Limits
        uint64_t maxGpuMemory;
        uint64_t maxBufferAllocationSize;
        uint64_t maxTextureAllocationSize;
    };

    class Profiler {
        public:
            static constexpr size_t kHistoryLength = 200;

            // A provider fills the fields of MinimalStatistics it owns. The profiler knows no engine
            // module: each statistic is registered by the code that has access to it.
            using StatsProvider = std::function<void(MinimalStatistics&)>;

            Profiler();
            void AddStatsProvider(StatsProvider provider);
            MinimalStatistics GetStats();
            void Shutdown();

            // Profiler used by ScopedProfileSample / ScopedRenderSubSample (null when there is none:
            // the scopes then do nothing). Set by whoever owns the profiler.
            static Profiler* Active() { return s_Active; }
            static void SetActive(Profiler* profiler) { s_Active = profiler; }

            // Per-subsystem frame timing.
            // BeginSample/EndSample can be called multiple times per category within a single
            // frame (e.g. physics runs in two spots) : durations accumulate into the current frame.
            void BeginSample(ProfileCategory category);
            void EndSample(ProfileCategory category);

            // Sub-measurements nested within Rendering (e.g. glDrawElements). Same accumulation
            // semantics as BeginSample/EndSample, but scoped to RenderSubSample and excluded from
            // the top-level category sum.
            void BeginRenderSubSample(RenderSubSample sample);
            void EndRenderSubSample(RenderSubSample sample);

            // The thread that draws calls this once, when it starts (and Unmark when it ends) : the samples it takes are then
            // kept apart from the ones of the simulation (they run at the same time). The render thread calls BeginRenderFrame(n)
            // when it starts the frame the simulation numbered `n` : everything it samples from then on (the frame, its UI, its
            // swap) belongs to `n`, until it begins the next one. A simulation frame is therefore only closed once the render
            // thread has moved on to a later frame (see EndFrameSampling) : the profile of frame `n` holds both threads' time
            // for that same frame. While a render thread samples, its categories (Rendering, Presentation) are time of another
            // thread : they do not count in what the frame's own thread accounted for when "Other" is derived.
            static void MarkRenderThread();
            static void UnmarkRenderThread();
            void BeginRenderFrame(uint64_t frame);

            // Ends the simulation thread's part of frame `frame` (numbered like the one given to BeginRenderFrame). The frame
            // is kept aside until the render thread has finished its own part of it, which is one frame later or so : only then
            // are the two merged, the "Other" bucket derived and the result pushed into the history ring buffer (and
            // GetLastFrameProfile). So what is shown runs a frame behind, and is the same frame on both threads. Call once per
            // frame, after all tracked subsystems have run. frameMs is the frame's total duration on this thread.
            void EndFrameSampling(float frameMs, uint64_t frame);

            const FrameProfile& GetLastFrameProfile() const { return m_LastFrame; }
            const std::array<FrameProfile, kHistoryLength>& GetProfileHistory() const { return m_History; }
            size_t GetProfileHistoryCursor() const { return m_HistoryCursor; }
            size_t GetProfileHistoryCount() const { return m_HistoryCount; }

        private:

            using ProfileClock = std::chrono::steady_clock;

            FrameProfile m_CurrentFrame{};
            FrameProfile m_LastFrame{};
            std::array<FrameProfile, kHistoryLength> m_History{};
            size_t m_HistoryCursor = 0;
            size_t m_HistoryCount = 0;

            std::array<ProfileClock::time_point, kProfileCategoryCount> m_SampleStart{};
            std::array<ProfileClock::time_point, kRenderSubSampleCount> m_RenderSubSampleStart{};

            // What the render thread sampled : `m_RenderLocal` is its own (no other thread touches it), `m_RenderPublished`
            // is what it handed over and the simulation has not taken yet
            struct RenderSide {
                std::array<float, kProfileCategoryCount> categoryMs{};
                std::array<float, kRenderSubSampleCount> renderSubMs{};
                uint32_t subSampleCount = 0;
                ProfileClock::time_point frameStart{};   // when the render thread began the frame
                ProfileClock::time_point swapEnd{};      // when the last Presentation sample ended (the swap)
                std::array<ProfileClock::time_point, kProfileCategoryCount> sampleStart{};
                std::array<ProfileClock::time_point, kRenderSubSampleCount> subSampleStart{};
            };
            RenderSide m_RenderLocal;
            uint64_t m_RenderFrame = 0;                  // the frame m_RenderLocal belongs to (render thread only)
            bool m_RenderFrameValid = false;
            std::mutex m_RenderMutex;
            std::map<uint64_t, RenderSide> m_RenderDone; // finished frames' render side, until their simulation frame takes it
            uint64_t m_RenderStarted = 0;                // the latest frame the render thread began
            bool m_RenderStartedValid = false;

            // Simulation side of the frames that wait for the render thread's part
            struct PendingFrame {
                uint64_t frame = 0;
                FrameProfile profile;
                uint32_t subSampleCount = 0;
                ProfileClock::time_point start{};
            };
            std::deque<PendingFrame> m_Pending;

            void FinalizeFrame(PendingFrame& pending, const RenderSide* render);

            static inline thread_local bool t_RenderSide = false;
            static inline std::atomic<bool> s_RenderThreadSampling{false};

            // Cost (ms) of one Begin/End pair itself, measured at startup. Every sample includes it in
            // its own duration, and Rendering also contains the pairs of all its sub-samples, so it is
            // subtracted back out to report only the time spent in the measured code.
            float m_SampleOverheadMs = 0.0f;
            uint32_t m_RenderSubSampleCount = 0;

            void CalibrateOverhead();

            std::vector<StatsProvider> m_StatsProviders;

            static inline Profiler* s_Active = nullptr;
    };

    // RAII helper : samples a category for the lifetime of the enclosing scope.
    class ScopedProfileSample {
        public:
            explicit ScopedProfileSample(ProfileCategory category);
            ~ScopedProfileSample();

            ScopedProfileSample(const ScopedProfileSample&) = delete;
            ScopedProfileSample& operator=(const ScopedProfileSample&) = delete;
        private:
            ProfileCategory category;
    };

    // RAII helper : samples a render sub-sample for the lifetime of the enclosing scope.
    class ScopedRenderSubSample {
        public:
            explicit ScopedRenderSubSample(RenderSubSample sample);
            ~ScopedRenderSubSample();

            ScopedRenderSubSample(const ScopedRenderSubSample&) = delete;
            ScopedRenderSubSample& operator=(const ScopedRenderSubSample&) = delete;
        private:
            RenderSubSample sample;
    };
}

#define SHARD_PROFILE_CONCAT_INNER(a, b) a##b
#define SHARD_PROFILE_CONCAT(a, b) SHARD_PROFILE_CONCAT_INNER(a, b)

// Scopes a ProfileCategory for the rest of the enclosing block.
#define SHARD_PROFILE_SCOPE(category) \
    ::Shard::Engine::Debugging::ScopedProfileSample SHARD_PROFILE_CONCAT(_shardProfileScope, __LINE__)(category)

// Scopes a RenderSubSample for the rest of the enclosing block.
#define SHARD_PROFILE_RENDER_SUB_SCOPE(sample) \
    ::Shard::Engine::Debugging::ScopedRenderSubSample SHARD_PROFILE_CONCAT(_shardRenderSubScope, __LINE__)(sample)

