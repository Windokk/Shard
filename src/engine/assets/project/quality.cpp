#include "quality.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <initializer_list>
#include <mutex>

namespace Shard::Engine::Projects{

    namespace {

        std::string Lower(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return out;
        }

        // "Ultra Low", "ultra-low" and "ULTRA_LOW" are the same word
        std::string Squeeze(std::string_view text)
        {
            std::string out;
            for (char c : text)
                if (c != ' ' && c != '_' && c != '-' && c != '\t')
                    out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
            return out;
        }

        bool Contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

        bool ContainsAny(const std::string& haystack, std::initializer_list<const char*> needles)
        {
            for (const char* needle : needles)
                if (Contains(haystack, needle))
                    return true;
            return false;
        }

        // Thresholds a little under the round sizes : a system reports 8151 MB for an 8 GB card and 7.8 GB of RAM for 8
        constexpr uint64_t kVram1_5GB = 1536;
        constexpr uint64_t kVram3GB = 3072;
        constexpr uint64_t kVram7GB = 7168;
        constexpr uint64_t kVram15GB = 15360;
        constexpr uint64_t kRam3_5GB = 3584;
        constexpr uint64_t kRam7GB = 7168;
        constexpr uint64_t kRam11GB = 11264;

        const std::array<QualityPreset, kQualityLevelCount>& Presets()
        {
            static const std::array<QualityPreset, kQualityLevelCount> presets = [] {
                std::array<QualityPreset, kQualityLevelCount> p{};

                // Ultra Low : the cheapest image that is still a scene. No anti-aliasing, short and coarse shadows, no AO and no reflections,
                // and a simulation at 30 Hz with a light solver.
                {
                    QualityPreset& q = p[size_t(QualityLevel::UltraLow)];
                    q.level = QualityLevel::UltraLow;
                    q.msaaSamples = 1;
                    q.occlusionCulling = true;
                    q.maxAnisotropy = 1;
                    q.ssao = false;
                    q.ssr = false;
                    q.directionalResolution = 512;
                    q.cascades = 2;
                    q.directionalFilterRadius = 0;
                    q.directionalDoubleSided = false;
                    q.spotResolution = 512;
                    q.spotFilterRadius = 0;
                    q.pointResolution = 512;
                    q.pointFilterSamples = 4;
                    q.fixedTimeStepScale = 2.0f;
                    q.velocitySteps = 4;
                    q.positionSteps = 1;
                    q.collisionSteps = 1;
                }

                // Low : no MSAA (the biggest single saving : the measured frame rate went from 80 to 97 fps on Sponza at 1080p), single-sided casters.
                {
                    QualityPreset& q = p[size_t(QualityLevel::Low)];
                    q.level = QualityLevel::Low;
                    q.msaaSamples = 1;
                    q.maxAnisotropy = 4;
                    q.ssao = false;
                    q.ssr = false;
                    q.directionalResolution = 1024;
                    q.cascades = 2;
                    q.directionalFilterRadius = 1;
                    q.directionalDoubleSided = false;
                    q.spotResolution = 512;
                    q.spotFilterRadius = 1;
                    q.pointResolution = 512;
                    q.pointFilterSamples = 8;
                    q.fixedTimeStepScale = 1.5f;
                    q.velocitySteps = 6;
                    q.positionSteps = 1;
                    q.collisionSteps = 1;
                }

                // Medium : the "combo" measured on Sponza at 1080p (single-sided casters, 1024 cascades, a 3x3 filter), 2x MSAA instead of none.
                {
                    QualityPreset& q = p[size_t(QualityLevel::Medium)];
                    q.level = QualityLevel::Medium;
                    q.msaaSamples = 2;
                    q.maxAnisotropy = 8;
                    q.ssr = false;
                    q.directionalResolution = 1024;
                    q.cascades = 3;
                    q.directionalFilterRadius = 1;
                    q.directionalDoubleSided = false;
                    q.spotResolution = 1024;
                    q.spotFilterRadius = 1;
                    q.pointResolution = 1024;
                    q.pointFilterSamples = 12;
                    q.velocitySteps = 8;
                    q.positionSteps = 2;
                }

                // High : exactly what a project had before the presets (the defaults of RenderingSettings and PhysicsSettings, which the tests check).
                {
                    QualityPreset& q = p[size_t(QualityLevel::High)];
                    q.level = QualityLevel::High;
                }

                // Ultra High : for a GPU with room to spare.
                {
                    QualityPreset& q = p[size_t(QualityLevel::UltraHigh)];
                    q.level = QualityLevel::UltraHigh;
                    q.msaaSamples = 8;
                    q.directionalResolution = 4096;
                    q.cascades = 4;
                    q.directionalFilterRadius = 3;
                    q.directionalConservativeRaster = true;     // ignored by a GPU without the extension
                    q.spotResolution = 2048;
                    q.spotFilterRadius = 3;
                    q.pointResolution = 2048;
                    q.fixedTimeStepScale = 0.5f;
                    q.velocitySteps = 14;
                    q.positionSteps = 3;
                }

                return p;
            }();
            return presets;
        }

        struct ActiveState{
            std::mutex mutex;
            EffectiveQuality quality;
            HardwareProfile hardware;
        };

        ActiveState& Active()
        {
            static ActiveState state;
            return state;
        }

        std::atomic<bool> g_AllowSSAO{true};
        std::atomic<bool> g_AllowSSR{true};
        std::atomic<uint32_t> g_MaxAnisotropy{16};
    }

    // ---- Names -------------------------------------------------------------------------------------------------------------------------------

    const char* QualityLevelName(QualityLevel level)
    {
        switch (level)
        {
            case QualityLevel::UltraLow:  return "ultra_low";
            case QualityLevel::Low:       return "low";
            case QualityLevel::Medium:    return "medium";
            case QualityLevel::High:      return "high";
            case QualityLevel::UltraHigh: return "ultra_high";
        }
        return "high";
    }

    const char* QualityLevelLabel(QualityLevel level)
    {
        switch (level)
        {
            case QualityLevel::UltraLow:  return "Ultra Low";
            case QualityLevel::Low:       return "Low";
            case QualityLevel::Medium:    return "Medium";
            case QualityLevel::High:      return "High";
            case QualityLevel::UltraHigh: return "Ultra High";
        }
        return "High";
    }

    bool ParseQualityLevel(std::string_view text, QualityLevel& out)
    {
        const std::string key = Squeeze(text);
        for (size_t i = 0; i < kQualityLevelCount; i++)
        {
            const QualityLevel level = QualityLevel(i);
            if (key == Squeeze(QualityLevelName(level)))
            {
                out = level;
                return true;
            }
        }
        return false;
    }

    std::string FormatQualitySelection(const QualitySelection& selection)
    {
        switch (selection.mode)
        {
            case QualityMode::Custom: return "custom";
            case QualityMode::Auto:   return "auto";
            case QualityMode::Fixed:  return QualityLevelName(selection.level);
        }
        return "custom";
    }

    bool ParseQualitySelection(std::string_view text, QualitySelection& out)
    {
        const std::string key = Squeeze(text);
        if (key == "custom")
        {
            out.mode = QualityMode::Custom;
            return true;
        }
        if (key == "auto")
        {
            out.mode = QualityMode::Auto;
            return true;
        }
        QualityLevel level;
        if (ParseQualityLevel(text, level))
        {
            out.mode = QualityMode::Fixed;
            out.level = level;
            return true;
        }
        return false;
    }

    QualitySelection ResolveQualitySelection(const QualitySelection& fromProject, std::string_view cvarValue)
    {
        const std::string key = Squeeze(cvarValue);
        if (key.empty() || key == "project")
            return fromProject;

        QualitySelection parsed;
        if (ParseQualitySelection(cvarValue, parsed))
            return parsed;

        return fromProject;
    }

    // ---- Hardware ----------------------------------------------------------------------------------------------------------------------------

    const char* GpuClassName(GpuClass cls)
    {
        switch (cls)
        {
            case GpuClass::Unknown:    return "unknown";
            case GpuClass::Software:   return "software renderer";
            case GpuClass::Integrated: return "integrated";
            case GpuClass::Legacy:     return "older discrete";
            case GpuClass::Discrete:   return "discrete";
        }
        return "unknown";
    }

    GpuClass ClassifyGpu(const HardwareProfile& hardware)
    {
        const std::string name = Lower(hardware.gpuName);
        const std::string vendor = Lower(hardware.gpuVendor);

        if (name.empty() && vendor.empty() && hardware.vramMB == 0)
            return GpuClass::Unknown;

        if (vendor == "none" || ContainsAny(name, { "llvmpipe", "softpipe", "swiftshader", "basic render", "gdi generic", "software", "none (headless)", "null renderer", "mesa offscreen" }))
            return GpuClass::Software;

        // A discrete card names itself with a model line that an integrated one does not have
        const bool discreteLine = ContainsAny(name, { "geforce", "rtx", "gtx", "quadro", "radeon rx", "radeon pro", "radeon r9", "radeon r7", "radeon hd", " arc ", "arc(tm)", "arc a", "arc b", "titan", "tesla" });

        if (!discreteLine)
        {
            // Intel UHD / HD / Iris / Xe, the Radeon graphics and Vega of the APUs, phones and Apple's M chips : the system memory is shared
            if (ContainsAny(name, { "intel", "uhd graphics", "hd graphics", "iris", "adreno", "mali", "powervr", "videocore", "apple" }) ||
                (Contains(name, "radeon") && ContainsAny(name, { "graphics", "vega" })))
                return GpuClass::Integrated;
        }

        // Cards whose memory size is the best of them : a 6 GB GTX 1060 is no match for an 8 GB card of today
        const bool legacyNvidia = ContainsAny(name, { "gtx 9", "gtx 10", "gtx 7", "gtx 6", "gtx 5", "gtx 4", "gt 10", "gt 7", "gt 6", "gt 5", "gt 4", "geforce 9", "geforce 8", "titan x", "titan black" });
        const bool legacyAmd = ContainsAny(name, { "radeon r9", "radeon r7", "radeon r5", "radeon hd", "rx 460", "rx 470", "rx 480", "rx 550", "rx 560", "rx 570", "rx 580", "rx 590", "rx vega 56", "rx vega 64" });
        if (legacyNvidia || legacyAmd)
            return GpuClass::Legacy;

        return GpuClass::Discrete;
    }

    namespace {
        QualityLevel LevelFromVram(uint64_t vramMB)
        {
            if (vramMB < kVram1_5GB) return QualityLevel::UltraLow;
            if (vramMB < kVram3GB)   return QualityLevel::Low;
            if (vramMB < kVram7GB)   return QualityLevel::Medium;
            if (vramMB < kVram15GB)  return QualityLevel::High;
            return QualityLevel::UltraHigh;
        }
    }

    QualityRecommendation RecommendQuality(const HardwareProfile& hardware)
    {
        QualityRecommendation result;
        result.gpuClass = ClassifyGpu(hardware);

        const std::string gpu = hardware.gpuName.empty() ? std::string("unknown GPU") : hardware.gpuName;

        switch (result.gpuClass)
        {
            case GpuClass::Software:
                result.level = QualityLevel::UltraLow;
                result.reasons.push_back(gpu + " : a software renderer or no GPU");
                return result;

            case GpuClass::Integrated:
                result.level = QualityLevel::Low;
                result.reasons.push_back(gpu + " : integrated GPU, starts at Low");
                break;

            case GpuClass::Legacy:
            case GpuClass::Discrete:
            case GpuClass::Unknown:
                if (hardware.vramMB > 0)
                {
                    result.level = LevelFromVram(hardware.vramMB);
                    result.reasons.push_back(gpu + " : " + std::to_string(hardware.vramMB) + " MB of video memory, starts at " + QualityLevelLabel(result.level));
                }
                else
                {
                    result.level = QualityLevel::Medium;
                    result.reasons.push_back(gpu + " : video memory unknown, starts at Medium");
                }
                if (result.gpuClass == GpuClass::Legacy && result.level > QualityLevel::Medium)
                {
                    result.level = QualityLevel::Medium;
                    result.reasons.push_back("older discrete GPU : capped at Medium");
                }
                break;
        }

        auto cap = [&](QualityLevel max, const std::string& why) {
            if (result.level > max)
            {
                result.level = max;
                result.reasons.push_back(why + " : capped at " + QualityLevelLabel(max));
            }
        };

        if (hardware.ramMB > 0)
        {
            const std::string ram = std::to_string(hardware.ramMB) + " MB of RAM";
            if (hardware.ramMB < kRam3_5GB)       cap(QualityLevel::UltraLow, ram);
            else if (hardware.ramMB < kRam7GB)    cap(QualityLevel::Low, ram);
            else if (hardware.ramMB < kRam11GB)   cap(QualityLevel::Medium, ram);
        }

        if (hardware.cpuLogicalCores > 0)
        {
            const std::string cpu = std::to_string(hardware.cpuLogicalCores) + " logical cores";
            if (hardware.cpuLogicalCores <= 2)      cap(QualityLevel::Low, cpu);
            else if (hardware.cpuLogicalCores <= 4) cap(QualityLevel::Medium, cpu);
        }

        return result;
    }

    // ---- Presets -----------------------------------------------------------------------------------------------------------------------------

    const QualityPreset& GetQualityPreset(QualityLevel level)
    {
        return Presets()[size_t(std::min<size_t>(size_t(level), kQualityLevelCount - 1))];
    }

    std::vector<float> CascadeSplitsFor(uint32_t cascades)
    {
        switch (std::clamp<uint32_t>(cascades, 1, 4))
        {
            case 1:  return {};
            case 2:  return { 0.4f };
            case 3:  return { 0.27f, 0.54f };
            default: return { 0.15f, 0.35f, 0.65f };
        }
    }

    void ApplyQualityPreset(const QualityPreset& preset, RenderingSettings& rendering, PhysicsSettings& physics, bool includePhysics)
    {
        rendering.msaaSamples = preset.msaaSamples;
        rendering.occlusionCulling = preset.occlusionCulling;
        rendering.maxAnisotropy = preset.maxAnisotropy;
        rendering.ssao = preset.ssao;
        rendering.ssr = preset.ssr;

        ShadowSettings& shadows = rendering.shadows;
        shadows.enabled = preset.shadowsEnabled;
        shadows.directionalResolution = preset.directionalResolution;

        // Keep the project's splits when it already has this many cascades : they are tuned to its scale
        if (shadows.CascadeCount() != preset.cascades)
            shadows.cascadeSplits = CascadeSplitsFor(preset.cascades);
        shadows.SanitizeCascades();

        shadows.directionalFilterRadius = preset.directionalFilterRadius;
        shadows.directionalDoubleSided = preset.directionalDoubleSided;
        shadows.directionalConservativeRaster = preset.directionalConservativeRaster;
        shadows.spotResolution = preset.spotResolution;
        shadows.spotFilterRadius = preset.spotFilterRadius;
        shadows.pointResolution = preset.pointResolution;
        shadows.pointFilterSamples = preset.pointFilterSamples;

        if (!includePhysics)
            return;

        physics.fixedTimeStep = physics.fixedTimeStep * preset.fixedTimeStepScale;
        physics.maxAccumulatedTime = std::max(physics.maxAccumulatedTime, physics.fixedTimeStep);
        physics.velocitySteps = preset.velocitySteps;
        physics.positionSteps = preset.positionSteps;
        physics.collisionSteps = preset.collisionSteps;
    }

    EffectiveQuality ResolveQuality(const RenderingSettings& baseRendering, const PhysicsSettings& basePhysics, const QualitySelection& selection,
                                    const HardwareProfile& hardware, bool deterministic)
    {
        EffectiveQuality out;
        out.selection = selection;
        out.rendering = baseRendering;
        out.physics = basePhysics;
        out.recommendation = RecommendQuality(hardware);

        if (selection.mode == QualityMode::Custom)
        {
            out.level = out.recommendation.level;
            return out;
        }

        out.presetActive = true;
        out.level = selection.mode == QualityMode::Auto ? out.recommendation.level : selection.level;

        // The step and the solver change what the simulation computes. A deterministic run (replays, lockstep) has to compute the same on every machine.
        out.physicsOverridden = !deterministic;
        ApplyQualityPreset(GetQualityPreset(out.level), out.rendering, out.physics, out.physicsOverridden);
        return out;
    }

    // ---- What is running ---------------------------------------------------------------------------------------------------------------------

    void SetActiveQuality(const EffectiveQuality& quality, const HardwareProfile& hardware)
    {
        {
            ActiveState& state = Active();
            std::lock_guard<std::mutex> lock(state.mutex);
            state.quality = quality;
            state.hardware = hardware;
        }
        g_AllowSSAO.store(quality.rendering.ssao, std::memory_order_relaxed);
        g_AllowSSR.store(quality.rendering.ssr, std::memory_order_relaxed);
        g_MaxAnisotropy.store(std::clamp<uint32_t>(quality.rendering.maxAnisotropy, 1, 16), std::memory_order_relaxed);
    }

    EffectiveQuality GetActiveQuality()
    {
        ActiveState& state = Active();
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.quality;
    }

    HardwareProfile GetDetectedHardware()
    {
        ActiveState& state = Active();
        std::lock_guard<std::mutex> lock(state.mutex);
        return state.hardware;
    }

    bool QualityAllowsSSAO() { return g_AllowSSAO.load(std::memory_order_relaxed); }
    bool QualityAllowsSSR() { return g_AllowSSR.load(std::memory_order_relaxed); }
    uint32_t QualityMaxAnisotropy() { return g_MaxAnisotropy.load(std::memory_order_relaxed); }

    std::vector<std::string> DescribeQuality(const EffectiveQuality& quality)
    {
        const RenderingSettings& r = quality.rendering;
        const ShadowSettings& s = r.shadows;
        const PhysicsSettings& p = quality.physics;
        auto onOff = [](bool v) { return v ? "on" : "off"; };

        std::vector<std::string> lines;
        lines.push_back(std::string("MSAA : ") + (r.msaaSamples <= 1 ? std::string("off") : std::to_string(r.msaaSamples) + "x"));
        lines.push_back("Occlusion culling : " + std::string(onOff(r.occlusionCulling)));
        lines.push_back("Anisotropy cap : " + std::to_string(r.maxAnisotropy) + "x");
        lines.push_back("SSAO : " + std::string(r.ssao ? "allowed" : "off"));
        lines.push_back("SSR : " + std::string(r.ssr ? "allowed" : "off"));
        lines.push_back("Shadows : " + std::string(onOff(s.enabled)));
        lines.push_back("Sun shadow map : " + std::to_string(s.directionalResolution) + " x " + std::to_string(s.CascadeCount()) + " cascades, filter radius " + std::to_string(s.directionalFilterRadius));
        lines.push_back("Sun casters : " + std::string(s.directionalDoubleSided ? "double-sided" : "single-sided") + ", conservative raster " + onOff(s.directionalConservativeRaster));
        lines.push_back("Spot shadow map : " + std::to_string(s.spotResolution) + ", filter radius " + std::to_string(s.spotFilterRadius));
        lines.push_back("Point shadow map : " + std::to_string(s.pointResolution) + ", " + std::to_string(s.pointFilterSamples) + " samples");
        if (quality.physicsOverridden || !quality.presetActive)
        {
            char step[48];
            std::snprintf(step, sizeof(step), "%.1f Hz", 1.0f / p.fixedTimeStep);
            lines.push_back(std::string("Physics step : ") + step);
            lines.push_back("Physics solver : " + std::to_string(p.velocitySteps) + " velocity, " + std::to_string(p.positionSteps) + " position iterations, " + std::to_string(p.collisionSteps) + " collision step(s)");
        }
        else
            lines.push_back("Physics : left as the project has it (deterministic run)");
        return lines;
    }
}
