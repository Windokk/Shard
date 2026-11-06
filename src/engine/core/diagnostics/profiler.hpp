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

    // "Other" is frame total minus the tracked categories. "Waiting" is the simulation thread blocked on the render thread (frame pacing), idle time.
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
        float renderMs = 0.0f;
        float frameMs = 0.0f;
    };

    struct MinimalStatistics {
        int sounds = 0;

        float frameTimeMs = 0;
        float fps = 0;

        int cmds = 0;
        int primitives = 0;
        int vertices = 0;

        float gpuMemoryMB = 0;
        float ramUsageMB = 0;

        int actors = 0;
        int lights = 0;
    };

    struct SystemCapabilities
    {
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

        uint32_t maxTexture1DSize;
        uint32_t maxTexture2DSize;
        uint32_t maxTexture3DSize;
        uint32_t maxTextureCubeSize;
        uint32_t maxTextureArrayLayers;
        uint32_t maxTextures;
        uint32_t maxSamplers;
        uint32_t maxAnisotropy;

        uint32_t maxUniformBufferSize;
        uint32_t maxStorageBufferSize;
        uint32_t maxPushConstants;
        uint32_t maxVertexBufferBindings;
        uint32_t maxVertexAttributes;
        uint32_t maxVertexBufferStride;

        uint32_t maxComputeSharedMemorySize;
        uint32_t maxComputeWorkGroupInvocations;
        uint32_t maxComputeWorkGroupSizeX;
        uint32_t maxComputeWorkGroupSizeY;
        uint32_t maxComputeWorkGroupSizeZ;

        uint32_t maxColorAttachments;
        uint32_t maxRenderTargets;
        uint32_t maxFramebufferWidth;
        uint32_t maxFramebufferHeight;
        uint32_t maxFramebufferLayers;
        uint32_t maxViewports;
        uint32_t maxScissors;

        uint32_t maxDrawCallsPerFrame;
        uint32_t maxInstancesPerDraw;
        uint32_t maxVerticesPerMesh;
        uint32_t maxIndicesPerMesh;
        uint32_t maxMeshes;
        uint32_t maxMaterials;
        uint32_t maxLights;

        uint32_t maxRayRecursionDepth;
        uint32_t maxAccelerationStructures;
        uint32_t maxShaderGroups;

        uint64_t maxGpuMemory;
        uint64_t maxBufferAllocationSize;
        uint64_t maxTextureAllocationSize;
    };

    class Profiler {
        public:
            static constexpr size_t kHistoryLength = 200;

            using StatsProvider = std::function<void(MinimalStatistics&)>;

            Profiler();
            void AddStatsProvider(StatsProvider provider);
            MinimalStatistics GetStats();
            void Shutdown();

            static Profiler* Active() { return s_Active; }
            static void SetActive(Profiler* profiler) { s_Active = profiler; }

            void BeginSample(ProfileCategory category);
            void EndSample(ProfileCategory category);

            void BeginRenderSubSample(RenderSubSample sample);
            void EndRenderSubSample(RenderSubSample sample);

            static void MarkRenderThread();
            static void UnmarkRenderThread();
            void BeginRenderFrame(uint64_t frame);

            // Runs a frame behind: merged with the render thread's part before it is pushed to the history ring.
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

            struct RenderSide {
                std::array<float, kProfileCategoryCount> categoryMs{};
                std::array<float, kRenderSubSampleCount> renderSubMs{};
                uint32_t subSampleCount = 0;
                ProfileClock::time_point frameStart{};
                ProfileClock::time_point swapEnd{};
                std::array<ProfileClock::time_point, kProfileCategoryCount> sampleStart{};
                std::array<ProfileClock::time_point, kRenderSubSampleCount> subSampleStart{};
            };
            RenderSide m_RenderLocal;
            uint64_t m_RenderFrame = 0;
            bool m_RenderFrameValid = false;
            std::mutex m_RenderMutex;
            std::map<uint64_t, RenderSide> m_RenderDone;
            uint64_t m_RenderStarted = 0;
            bool m_RenderStartedValid = false;

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

            // Begin/End pair cost measured at startup and subtracted, so only time in the measured code is reported
            float m_SampleOverheadMs = 0.0f;
            uint32_t m_RenderSubSampleCount = 0;

            void CalibrateOverhead();

            std::vector<StatsProvider> m_StatsProviders;

            static inline Profiler* s_Active = nullptr;
    };

    class ScopedProfileSample {
        public:
            explicit ScopedProfileSample(ProfileCategory category);
            ~ScopedProfileSample();

            ScopedProfileSample(const ScopedProfileSample&) = delete;
            ScopedProfileSample& operator=(const ScopedProfileSample&) = delete;
        private:
            ProfileCategory category;
    };

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

#define SHARD_PROFILE_SCOPE(category) \
    ::Shard::Engine::Debugging::ScopedProfileSample SHARD_PROFILE_CONCAT(_shardProfileScope, __LINE__)(category)

#define SHARD_PROFILE_RENDER_SUB_SCOPE(sample) \
    ::Shard::Engine::Debugging::ScopedRenderSubSample SHARD_PROFILE_CONCAT(_shardRenderSubScope, __LINE__)(sample)

