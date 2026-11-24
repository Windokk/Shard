#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/assets/project/project.hpp"
#include "engine/assets/project/quality_types.hpp"

namespace Shard::Engine::Projects{

    // Quality presets : a named set of values for settings the project already has (MSAA, shadows, physics solver...), and the choice of one
    // for the machine. Nothing here touches the GPU : the hardware is described by a HardwareProfile, so the choice can be tested with made-up machines.

    // What the project file says, unless the `quality.preset` variable was given a value ("project" : defer to the file). The variable
    // is what a command line (+quality.preset=low), a user.cfg or the console set, so a player can choose without editing the project.
    QualitySelection ResolveQualitySelection(const QualitySelection& fromProject, std::string_view cvarValue);


    // ---- Hardware ----------------------------------------------------------------------------------------------------------------------------


    enum class GpuClass : uint8_t {
        Unknown,        // no name, no memory : treated as a mid-range card
        Software,       // llvmpipe, SwiftShader, Microsoft Basic Render Driver, headless
        Integrated,     // shares the system memory
        Legacy,         // a discrete card old enough that its memory size flatters it (GTX 10 series and before, RX 400/500...)
        Discrete
    };

    const char* GpuClassName(GpuClass cls);

    GpuClass ClassifyGpu(const HardwareProfile& hardware);

    struct QualityRecommendation{
        QualityLevel level = QualityLevel::Medium;
        GpuClass gpuClass = GpuClass::Unknown;
        // One line per step of the decision, in order : what the GPU gives, then what RAM and CPU cap
        std::vector<std::string> reasons;
    };

    // The heuristic, in short :
    //  1. a software renderer gets UltraLow ; an integrated GPU Low ;
    //  2. a discrete GPU is placed by its video memory : < 1.5 GB UltraLow, < 3 GB Low, < 7 GB Medium, < 15 GB High, more UltraHigh
    //     (memory unknown : Medium) ; a legacy card is capped at Medium ;
    //  3. then the system caps it : RAM < 3.5 GB UltraLow, < 7 GB at most Low, < 11 GB at most Medium ; 2 logical cores or fewer at most Low, 4 or fewer at most Medium.
    // The thresholds sit a little under the round sizes because the figures a system reports do (an 8 GB card shows 8151 MB).
    QualityRecommendation RecommendQuality(const HardwareProfile& hardware);

    // ---- Presets -----------------------------------------------------------------------------------------------------------------------------

    struct QualityPreset{
        QualityLevel level = QualityLevel::High;

        // Rendering
        uint32_t msaaSamples = 4;               // 1 : off
        bool occlusionCulling = true;
        uint32_t maxAnisotropy = 16;
        bool ssao = true;                       // allowed : a world still switches it off for itself
        bool ssr = true;

        bool shadowsEnabled = true;
        uint32_t directionalResolution = 2048;
        uint32_t cascades = 3;                  // 1 to 4 (the project may have up to ShadowSettings::MaxCascades)
        uint32_t directionalFilterRadius = 2;
        bool directionalDoubleSided = true;
        bool directionalConservativeRaster = false;
        uint32_t spotResolution = 1024;
        uint32_t spotFilterRadius = 2;
        uint32_t pointResolution = 1024;
        uint32_t pointFilterSamples = 20;

        // Physics : the fixed time step is the project's, times this (2 : half as many steps per second)
        float fixedTimeStepScale = 1.0f;
        uint32_t velocitySteps = 10;
        uint32_t positionSteps = 2;
        uint32_t collisionSteps = 1;
    };

    const QualityPreset& GetQualityPreset(QualityLevel level);

    // Cascade splits (see ShadowSettings::cascadeSplits) of 1 to 4 cascades. 3 is the project default.
    std::vector<float> CascadeSplitsFor(uint32_t cascades);

    // Writes the preset's values over the settings it covers (the others, the biases and the distance among them, keep the project's).
    // `includePhysics` false : the simulation is left as the project has it.
    void ApplyQualityPreset(const QualityPreset& preset, RenderingSettings& rendering, PhysicsSettings& physics, bool includePhysics = true);

    // What the engine runs with, and why
    struct EffectiveQuality{
        QualitySelection selection;
        bool presetActive = false;
        QualityLevel level = QualityLevel::High;    // the preset in use, when presetActive
        QualityRecommendation recommendation;       // always filled : what Auto would pick
        RenderingSettings rendering;
        PhysicsSettings physics;
        bool physicsOverridden = false;             // false in a deterministic run : the simulation must not depend on the machine
    };

    // `baseRendering` and `basePhysics` are the project's own values : they are copied, never changed.
    EffectiveQuality ResolveQuality(const RenderingSettings& baseRendering, const PhysicsSettings& basePhysics, const QualitySelection& selection,
                                    const HardwareProfile& hardware, bool deterministic);

    // ---- What is running ---------------------------------------------------------------------------------------------------------------------

    // Published by the engine each time it applies the settings, read by the editor and by the code that gates a feature.
    void SetActiveQuality(const EffectiveQuality& quality, const HardwareProfile& hardware);
    EffectiveQuality GetActiveQuality();
    HardwareProfile GetDetectedHardware();

    // The project's switches for the features a world turns on itself. Thread safe, cheap.
    bool QualityAllowsSSAO();
    bool QualityAllowsSSR();
    uint32_t QualityMaxAnisotropy();

    // One line per setting the preset sets, "name : value", for the log, the console and the editor
    std::vector<std::string> DescribeQuality(const EffectiveQuality& quality);
}
