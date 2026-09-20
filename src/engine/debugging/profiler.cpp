#include "profiler.hpp"

#include "engine/core/engine.hpp"

#ifdef __WIN32__
#define byte cs_byte
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#ifdef byte
#undef byte
#endif


#elif defined(__unix__)

#endif

#include "engine/debugging/logger.hpp"
#include "engine/rendering/renderer/renderer.hpp"
#include "engine/rendering/lighting/light_manager.hpp"
#include "engine/audio/audio_manager.hpp"
#include "engine/levels/level_manager.hpp"
#include "engine/time/time_manager.hpp"
#include "engine/rendering/mesh/mesh.hpp"
#include "engine/core/platform/iplatform.hpp"

namespace Shard::Engine::Debugging{
   
    Profiler::Profiler(){

        CalibrateOverhead();

        #ifdef _WIN32
            // Wildcarded on the "phys_N" suffix so we pick up every physical
            // adapter (e.g. integrated + discrete) this process has memory on.
            gpuCounterPathPattern = std::string("\\GPU Process Memory(pid_") +
                                    std::to_string(getpid()) +
                                    "_*)\\Dedicated Usage";

            fmtValue = new PDH_FMT_COUNTERVALUE;

            pdhStatus = PdhOpenQuery(NULL, 0, &hQuery);
            if (pdhStatus != ERROR_SUCCESS)
            {
                DEBUG_ERROR("PdhOpenQuery failed with 0x", std::hex, pdhStatus);
            }

            // The "GPU Process Memory" instance for this pid is only created once
            // this process has actually allocated GPU memory, which hasn't happened
            // yet at engine startup. Resolving the counter is therefore deferred to
            // GetGPUMem(), which retries PdhExpandWildCardPath until the instance
            // shows up (a wildcard passed straight to PdhAddCounter only binds
            // whatever instance exists at that exact call and is never re-resolved).
        #endif
    }

    float Profiler::GetGPUMem()
    {
        #ifdef __WIN32__
            // Windows (PDH)
            if (!gpuCountersBound)
            {
                char expandedPaths[4096];
                DWORD expandedSize = sizeof(expandedPaths);

                PDH_STATUS expandStatus = PdhExpandWildCardPathA(
                    NULL,
                    gpuCounterPathPattern.c_str(),
                    expandedPaths,
                    &expandedSize,
                    0);

                if (expandStatus == ERROR_SUCCESS)
                {
                    for (const char* path = expandedPaths; *path != '\0'; path += strlen(path) + 1)
                    {
                        void* counter = nullptr;
                        if (PdhAddEnglishCounterA(hQuery, path, 0, &counter) == ERROR_SUCCESS)
                            gpuCounters.push_back(counter);
                    }

                    gpuCountersBound = !gpuCounters.empty();
                }

                // No matching instance yet (process hasn't allocated GPU memory):
                // report 0 and retry expansion on the next call.
                if (!gpuCountersBound)
                    return 0.0f;
            }

            pdhStatus = PdhCollectQueryData(hQuery);
            if (pdhStatus != ERROR_SUCCESS)
            {
                DEBUG_ERROR("PdhCollectQueryData failed with 0x", std::hex, pdhStatus);
                return 0.0f;
            }

            double totalBytes = 0.0;
            for (void* counter : gpuCounters)
            {
                pdhStatus = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, fmtValue);
                if (pdhStatus != ERROR_SUCCESS || fmtValue->CStatus != ERROR_SUCCESS)
                {
                    // The instance can disappear (e.g. the app drops all GPU
                    // allocations); rebind from scratch on the next call.
                    gpuCountersBound = false;
                    gpuCounters.clear();
                    continue;
                }

                totalBytes += fmtValue->doubleValue;
            }

            return static_cast<float>(totalBytes);

        #elif defined(__unix__)

            // detect GPU vendor via sysfs
            std::string vendorPath = "/sys/class/drm/card0/device/vendor";
            std::ifstream vendorFile(vendorPath);

            if (vendorFile.good()) {
                std::string vendorHex;
                vendorFile >> vendorHex;

                // 0x1002 = AMD
                // 0x10de = NVIDIA
                // 0x8086 = Intel
                int vendor = std::stoi(vendorHex, nullptr, 16);

                // AMD path
                if (vendor == 0x1002) {
                    long used = 0;

                    std::ifstream file("/sys/class/drm/card0/device/mem_info_vram_used");
                    if (file.good())
                        file >> used;

                    return used / 1024.0f / 1024.0f; // bytes → MB
                }

                // NVIDIA path (NVML)
                if (vendor == 0x10de) {
        #ifdef USE_NVML
                    nvmlMemory_t mem;
                    if (nvmlDeviceGetMemoryInfo(nvmlDevice, &mem) == NVML_SUCCESS)
                        return mem.used / 1024.0f / 1024.0f;
        #endif
                    return 0.0f; // NVML not enabled
                }

                // Intel (no unified per-process VRAM usage)
                if (vendor == 0x8086) {
                    return 0.0f; // unsupported
                }
            }

            return 0.0f; // fallback

        #endif
    }

    MinimalStatistics Profiler::GetStats()
    {
        MinimalStatistics ret{};

        ret.frameTimeMs = Core::GetEngine().GetTimeManager()->GetDeltaTime() * 1000;
        ret.fps = 1000 / ret.frameTimeMs;

        ret.actors = Core::GetEngine().GetLevelManager()->GetLevelAt(0)->transforms.size();

        Rendering::Renderer* renderer = Core::GetEngine().GetRenderer();

        ret.lights = renderer->GetLightManager()->GetLightsCount();
        ret.cmds = renderer->GetDrawCallsCount();
        ret.primitives = renderer->GetPrimitivesCount();
        ret.vertices = renderer->GetVerticesCount();

        ret.sounds = Core::GetEngine().GetAudioManager()->GetSoundsCount();

        Core::Platform::SystemInfos infos = Core::GetEngine().GetWindow()->GetSystemInfos();

        #if defined(_WIN64) || defined(_WIN32)
        {
            ret.gpuMemoryMB = GetGPUMem() / (1024 * 1024);
        }

        #elif defined(__unix__)
        {
            /// @todo
        }
        #endif

        return ret;
    }

    void Profiler::Shutdown()
    {
        // Close the query object
        #ifdef __WIN32__
            if (hQuery)
                PdhCloseQuery (hQuery);
        #endif
    }

    void Profiler::CalibrateOverhead()
    {
        constexpr int kIterations = 20000;

        auto start = ProfileClock::now();
        for (int i = 0; i < kIterations; i++)
        {
            BeginRenderSubSample(RenderSubSample::CameraUpdate);
            EndRenderSubSample(RenderSubSample::CameraUpdate);
        }
        auto total = ProfileClock::now() - start;

        // Raw (uncorrected) cost of a pair : the empty scopes above measure nothing but themselves.
        m_SampleOverheadMs = std::chrono::duration<float, std::milli>(total).count() / kIterations;

        m_CurrentFrame = FrameProfile{};
        m_RenderSubSampleCount = 0;
    }

    void Profiler::BeginSample(ProfileCategory category)
    {
        m_SampleStart[static_cast<size_t>(category)] = ProfileClock::now();
    }

    void Profiler::EndSample(ProfileCategory category)
    {
        auto elapsed = ProfileClock::now() - m_SampleStart[static_cast<size_t>(category)];
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();
        ms -= m_SampleOverheadMs;
        m_CurrentFrame.categoryMs[static_cast<size_t>(category)] += ms > 0.0f ? ms : 0.0f;
    }

    void Profiler::BeginRenderSubSample(RenderSubSample sample)
    {
        m_RenderSubSampleStart[static_cast<size_t>(sample)] = ProfileClock::now();
    }

    void Profiler::EndRenderSubSample(RenderSubSample sample)
    {
        auto elapsed = ProfileClock::now() - m_RenderSubSampleStart[static_cast<size_t>(sample)];
        float ms = std::chrono::duration<float, std::milli>(elapsed).count();
        ms -= m_SampleOverheadMs;
        m_CurrentFrame.renderSubMs[static_cast<size_t>(sample)] += ms > 0.0f ? ms : 0.0f;
        m_RenderSubSampleCount++;
    }

    void Profiler::EndFrameSampling()
    {
        m_CurrentFrame.totalMs = Core::GetEngine().GetTimeManager()->GetDeltaTime() * 1000.0f;

        // Rendering's wall time contains every sub-sample's own bookkeeping : remove it.
        float& renderingMs = m_CurrentFrame.categoryMs[static_cast<size_t>(ProfileCategory::Rendering)];
        renderingMs -= m_RenderSubSampleCount * m_SampleOverheadMs;
        if (renderingMs < 0.0f)
            renderingMs = 0.0f;
        m_RenderSubSampleCount = 0;

        float tracked = 0.0f;
        for (size_t i = 0; i < kProfileCategoryCount; i++)
        {
            if (static_cast<ProfileCategory>(i) == ProfileCategory::Other)
                continue;
            tracked += m_CurrentFrame.categoryMs[i];
        }

        float other = m_CurrentFrame.totalMs - tracked;
        m_CurrentFrame.categoryMs[static_cast<size_t>(ProfileCategory::Other)] = other > 0.0f ? other : 0.0f;

        m_LastFrame = m_CurrentFrame;
        m_History[m_HistoryCursor] = m_CurrentFrame;
        m_HistoryCursor = (m_HistoryCursor + 1) % kHistoryLength;
        if (m_HistoryCount < kHistoryLength)
            m_HistoryCount++;

        m_CurrentFrame = FrameProfile{};
    }

    ScopedProfileSample::ScopedProfileSample(ProfileCategory category)
        : category(category)
    {
        Core::GetEngine().GetProfiler()->BeginSample(category);
    }

    ScopedProfileSample::~ScopedProfileSample()
    {
        Core::GetEngine().GetProfiler()->EndSample(category);
    }

    ScopedRenderSubSample::ScopedRenderSubSample(RenderSubSample sample)
        : sample(sample)
    {
        Core::GetEngine().GetProfiler()->BeginRenderSubSample(sample);
    }

    ScopedRenderSubSample::~ScopedRenderSubSample()
    {
        Core::GetEngine().GetProfiler()->EndRenderSubSample(sample);
    }
}