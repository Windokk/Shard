#include <gtest/gtest.h>

#include <filesystem>

#include "config.h"

#include "engine/assets/project/quality.hpp"
#include "engine/assets/serialization/project/project_serializer.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Projects;

namespace {
    HardwareProfile Machine(const char* gpu, uint64_t vramMB, uint32_t cores = 16, uint64_t ramMB = 32768, const char* vendor = "")
    {
        HardwareProfile hardware;
        hardware.gpuName = gpu;
        hardware.gpuVendor = vendor;
        hardware.vramMB = vramMB;
        hardware.cpuLogicalCores = cores;
        hardware.ramMB = ramMB;
        return hardware;
    }

    QualityLevel Pick(const HardwareProfile& hardware) { return RecommendQuality(hardware).level; }
}

// ---- Names and selection ---------------------------------------------------------------------------------------------------------------------

TEST(Quality, LevelNamesRoundTrip) {
    for (size_t i = 0; i < kQualityLevelCount; i++) {
        QualityLevel parsed;
        ASSERT_TRUE(ParseQualityLevel(QualityLevelName(QualityLevel(i)), parsed));
        EXPECT_EQ(parsed, QualityLevel(i));
        ASSERT_TRUE(ParseQualityLevel(QualityLevelLabel(QualityLevel(i)), parsed)) << "the label is read back too";
        EXPECT_EQ(parsed, QualityLevel(i));
    }
}

TEST(Quality, LevelParsingIsLenient) {
    QualityLevel level = QualityLevel::High;
    EXPECT_TRUE(ParseQualityLevel("ULTRALOW", level));
    EXPECT_EQ(level, QualityLevel::UltraLow);
    EXPECT_TRUE(ParseQualityLevel("ultra-high", level));
    EXPECT_EQ(level, QualityLevel::UltraHigh);
    EXPECT_TRUE(ParseQualityLevel("  Medium", level));
    EXPECT_EQ(level, QualityLevel::Medium);
    EXPECT_FALSE(ParseQualityLevel("ultra", level));
    EXPECT_FALSE(ParseQualityLevel("", level));
    EXPECT_FALSE(ParseQualityLevel("custom", level));
}

TEST(Quality, SelectionParsing) {
    QualitySelection selection;
    ASSERT_TRUE(ParseQualitySelection("auto", selection));
    EXPECT_EQ(selection.mode, QualityMode::Auto);
    ASSERT_TRUE(ParseQualitySelection("Custom", selection));
    EXPECT_EQ(selection.mode, QualityMode::Custom);
    ASSERT_TRUE(ParseQualitySelection("low", selection));
    EXPECT_EQ(selection.mode, QualityMode::Fixed);
    EXPECT_EQ(selection.level, QualityLevel::Low);
    EXPECT_EQ(FormatQualitySelection(selection), "low");
    EXPECT_FALSE(ParseQualitySelection("fast", selection));
    EXPECT_EQ(selection.level, QualityLevel::Low) << "a failed parse leaves the value alone";
}

TEST(Quality, VariableOverridesTheProject) {
    QualitySelection fromProject;
    ASSERT_TRUE(ParseQualitySelection("medium", fromProject));

    EXPECT_EQ(ResolveQualitySelection(fromProject, "project"), fromProject);
    EXPECT_EQ(ResolveQualitySelection(fromProject, ""), fromProject);
    EXPECT_EQ(ResolveQualitySelection(fromProject, "not a preset"), fromProject) << "a typo does not change the quality";

    const QualitySelection low = ResolveQualitySelection(fromProject, "low");
    EXPECT_EQ(low.mode, QualityMode::Fixed);
    EXPECT_EQ(low.level, QualityLevel::Low);
    EXPECT_EQ(ResolveQualitySelection(fromProject, "auto").mode, QualityMode::Auto);
    EXPECT_EQ(ResolveQualitySelection(fromProject, "custom").mode, QualityMode::Custom);
}

// ---- Hardware classification and the recommendation ------------------------------------------------------------------------------------------

TEST(QualityHardware, ClassifiesGpus) {
    EXPECT_EQ(ClassifyGpu(Machine("llvmpipe (LLVM 17.0.6, 256 bits)", 0)), GpuClass::Software);
    EXPECT_EQ(ClassifyGpu(Machine("Microsoft Basic Render Driver", 0)), GpuClass::Software);
    EXPECT_EQ(ClassifyGpu(Machine("none (headless)", 0, 8, 16384, "none")), GpuClass::Software);
    EXPECT_EQ(ClassifyGpu(Machine("Intel(R) UHD Graphics 630", 0)), GpuClass::Integrated);
    EXPECT_EQ(ClassifyGpu(Machine("Intel(R) Iris(R) Xe Graphics", 0)), GpuClass::Integrated);
    EXPECT_EQ(ClassifyGpu(Machine("AMD Radeon(TM) Graphics", 0)), GpuClass::Integrated);
    EXPECT_EQ(ClassifyGpu(Machine("Apple M2", 0)), GpuClass::Integrated);
    EXPECT_EQ(ClassifyGpu(Machine("NVIDIA GeForce RTX 5060/PCIe/SSE2", 8151)), GpuClass::Discrete);
    EXPECT_EQ(ClassifyGpu(Machine("NVIDIA GeForce RTX 4090 (Vulkan)", 24564)), GpuClass::Discrete);
    EXPECT_EQ(ClassifyGpu(Machine("AMD Radeon RX 7600", 8176)), GpuClass::Discrete);
    EXPECT_EQ(ClassifyGpu(Machine("Intel(R) Arc(TM) A770 Graphics", 16256)), GpuClass::Discrete) << "an Arc is discrete although it is made by Intel";
    EXPECT_EQ(ClassifyGpu(Machine("NVIDIA GeForce GTX 1060 6GB", 6144)), GpuClass::Legacy);
    EXPECT_EQ(ClassifyGpu(Machine("NVIDIA GeForce GTX 1660 SUPER", 6144)), GpuClass::Discrete) << "the 16 series is recent enough";
    EXPECT_EQ(ClassifyGpu(Machine("AMD Radeon RX 580 Series", 8192)), GpuClass::Legacy);
    EXPECT_EQ(ClassifyGpu(Machine("", 0, 0, 0)), GpuClass::Unknown);
}

TEST(QualityHardware, PicksByVideoMemory) {
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 3050", 1024)), QualityLevel::UltraLow);
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 3050", 2048)), QualityLevel::Low);
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 3050", 4096)), QualityLevel::Medium);
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 2060", 6144)), QualityLevel::Medium);
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 5060", 8151)), QualityLevel::High) << "the RTX 5060 the benchmarks run on gets High";
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 5070", 12227)), QualityLevel::High);
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 4080", 16376)), QualityLevel::UltraHigh);
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 4090", 24564)), QualityLevel::UltraHigh);
}

TEST(QualityHardware, LevelNeverFallsWhenMemoryGrows) {
    QualityLevel previous = QualityLevel::UltraLow;
    for (uint64_t vram = 256; vram <= 32768; vram += 256) {
        const QualityLevel level = Pick(Machine("NVIDIA GeForce RTX 4000", vram));
        EXPECT_GE(level, previous) << vram << " MB";
        previous = level;
    }
}

TEST(QualityHardware, SoftwareRendererIsUltraLowWhateverTheRest) {
    const QualityRecommendation recommendation = RecommendQuality(Machine("llvmpipe (LLVM 17.0.6, 256 bits)", 24000, 32, 65536));
    EXPECT_EQ(recommendation.level, QualityLevel::UltraLow);
    EXPECT_EQ(recommendation.gpuClass, GpuClass::Software);
    EXPECT_FALSE(recommendation.reasons.empty());
}

TEST(QualityHardware, IntegratedStartsAtLow) {
    EXPECT_EQ(Pick(Machine("Intel(R) UHD Graphics 630", 0)), QualityLevel::Low);
    EXPECT_EQ(Pick(Machine("AMD Radeon(TM) Graphics", 0, 16, 32768)), QualityLevel::Low);
    EXPECT_EQ(Pick(Machine("Intel(R) UHD Graphics 630", 0, 8, 3000)), QualityLevel::UltraLow) << "and very little RAM takes it lower";
}

TEST(QualityHardware, LegacyCardIsCappedAtMedium) {
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce GTX 1060 6GB", 6144)), QualityLevel::Medium);
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce GTX 1080 Ti", 11264)), QualityLevel::Medium) << "11 GB would be High on a card of today";
    EXPECT_EQ(Pick(Machine("AMD Radeon RX 580 Series", 8192)), QualityLevel::Medium);
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce GTX 750 Ti", 2048)), QualityLevel::Low) << "the cap never raises";
}

TEST(QualityHardware, UnknownMemoryIsMedium) {
    EXPECT_EQ(Pick(Machine("NVIDIA GeForce RTX 5060", 0)), QualityLevel::Medium);
    EXPECT_EQ(Pick(Machine("", 0, 0, 0)), QualityLevel::Medium) << "nothing known : the middle, not the top";
}

TEST(QualityHardware, RamCaps) {
    const char* big = "NVIDIA GeForce RTX 4090";
    EXPECT_EQ(Pick(Machine(big, 24564, 16, 3000)), QualityLevel::UltraLow);
    EXPECT_EQ(Pick(Machine(big, 24564, 16, 4096)), QualityLevel::Low);
    EXPECT_EQ(Pick(Machine(big, 24564, 16, 7900)), QualityLevel::Medium) << "8 GB of RAM as a system reports it";
    EXPECT_EQ(Pick(Machine(big, 24564, 16, 8192)), QualityLevel::Medium);
    EXPECT_EQ(Pick(Machine(big, 24564, 16, 12000)), QualityLevel::UltraHigh) << "16 GB is the usual configuration";
    EXPECT_EQ(Pick(Machine(big, 24564, 16, 0)), QualityLevel::UltraHigh) << "RAM unknown : no cap";
}

TEST(QualityHardware, CpuCaps) {
    const char* big = "NVIDIA GeForce RTX 4090";
    EXPECT_EQ(Pick(Machine(big, 24564, 2)), QualityLevel::Low);
    EXPECT_EQ(Pick(Machine(big, 24564, 4)), QualityLevel::Medium);
    EXPECT_EQ(Pick(Machine(big, 24564, 6)), QualityLevel::UltraHigh);
    EXPECT_EQ(Pick(Machine(big, 24564, 0)), QualityLevel::UltraHigh) << "cores unknown : no cap";
}

TEST(QualityHardware, ReasonsExplainTheCaps) {
    const QualityRecommendation recommendation = RecommendQuality(Machine("NVIDIA GeForce RTX 4090", 24564, 4, 32768));
    EXPECT_EQ(recommendation.level, QualityLevel::Medium);
    ASSERT_EQ(recommendation.reasons.size(), 2u);
    EXPECT_NE(recommendation.reasons[0].find("24564"), std::string::npos);
    EXPECT_NE(recommendation.reasons[1].find("4 logical cores"), std::string::npos);
}

// ---- Presets ---------------------------------------------------------------------------------------------------------------------------------

TEST(QualityPresets, HighIsWhatTheProjectHadBefore) {
    RenderingSettings rendering;
    PhysicsSettings physics;
    const RenderingSettings renderingBefore = rendering;
    const PhysicsSettings physicsBefore = physics;

    ApplyQualityPreset(GetQualityPreset(QualityLevel::High), rendering, physics);

    EXPECT_EQ(rendering.msaaSamples, renderingBefore.msaaSamples);
    EXPECT_EQ(rendering.occlusionCulling, renderingBefore.occlusionCulling);
    EXPECT_EQ(rendering.maxAnisotropy, renderingBefore.maxAnisotropy);
    EXPECT_EQ(rendering.ssao, renderingBefore.ssao);
    EXPECT_EQ(rendering.ssr, renderingBefore.ssr);
    EXPECT_EQ(rendering.shadows.enabled, renderingBefore.shadows.enabled);
    EXPECT_EQ(rendering.shadows.directionalResolution, renderingBefore.shadows.directionalResolution);
    EXPECT_EQ(rendering.shadows.cascadeSplits, renderingBefore.shadows.cascadeSplits);
    EXPECT_EQ(rendering.shadows.cascadeColors, renderingBefore.shadows.cascadeColors);
    EXPECT_EQ(rendering.shadows.directionalFilterRadius, renderingBefore.shadows.directionalFilterRadius);
    EXPECT_EQ(rendering.shadows.directionalDoubleSided, renderingBefore.shadows.directionalDoubleSided);
    EXPECT_EQ(rendering.shadows.directionalConservativeRaster, renderingBefore.shadows.directionalConservativeRaster);
    EXPECT_EQ(rendering.shadows.spotResolution, renderingBefore.shadows.spotResolution);
    EXPECT_EQ(rendering.shadows.spotFilterRadius, renderingBefore.shadows.spotFilterRadius);
    EXPECT_EQ(rendering.shadows.pointResolution, renderingBefore.shadows.pointResolution);
    EXPECT_EQ(rendering.shadows.pointFilterSamples, renderingBefore.shadows.pointFilterSamples);

    EXPECT_EQ(physics.fixedTimeStep, physicsBefore.fixedTimeStep);
    EXPECT_EQ(physics.maxAccumulatedTime, physicsBefore.maxAccumulatedTime);
    EXPECT_EQ(physics.velocitySteps, physicsBefore.velocitySteps);
    EXPECT_EQ(physics.positionSteps, physicsBefore.positionSteps);
    EXPECT_EQ(physics.collisionSteps, physicsBefore.collisionSteps);
}

TEST(QualityPresets, CostGoesUpWithTheLevel) {
    uint32_t msaa = 0, sunMap = 0, cascades = 0, filter = 0, spot = 0, point = 0;
    for (size_t i = 0; i < kQualityLevelCount; i++) {
        const QualityPreset& preset = GetQualityPreset(QualityLevel(i));
        EXPECT_EQ(preset.level, QualityLevel(i));
        EXPECT_GE(preset.msaaSamples, msaa);
        EXPECT_GE(preset.directionalResolution, sunMap);
        EXPECT_GE(preset.cascades, cascades);
        EXPECT_GE(preset.directionalFilterRadius, filter);
        EXPECT_GE(preset.spotResolution, spot);
        EXPECT_GE(preset.pointResolution, point);
        msaa = preset.msaaSamples;
        sunMap = preset.directionalResolution;
        cascades = preset.cascades;
        filter = preset.directionalFilterRadius;
        spot = preset.spotResolution;
        point = preset.pointResolution;
    }
    EXPECT_EQ(GetQualityPreset(QualityLevel::UltraLow).msaaSamples, 1u);
    EXPECT_EQ(GetQualityPreset(QualityLevel::UltraHigh).msaaSamples, 8u);
    // the simulation gets finer from Ultra Low to Ultra High
    EXPECT_GT(GetQualityPreset(QualityLevel::UltraLow).fixedTimeStepScale, GetQualityPreset(QualityLevel::UltraHigh).fixedTimeStepScale);
    EXPECT_LT(GetQualityPreset(QualityLevel::UltraLow).velocitySteps, GetQualityPreset(QualityLevel::UltraHigh).velocitySteps);
}

TEST(QualityPresets, EveryPresetFitsTheLimitsOfTheProjectFile) {
    for (size_t i = 0; i < kQualityLevelCount; i++) {
        RenderingSettings rendering;
        PhysicsSettings physics;
        ApplyQualityPreset(GetQualityPreset(QualityLevel(i)), rendering, physics);

        EXPECT_GE(rendering.msaaSamples, 1u);
        EXPECT_LE(rendering.msaaSamples, 16u);
        EXPECT_GE(rendering.shadows.directionalResolution, 256u);
        EXPECT_LE(rendering.shadows.directionalResolution, 8192u);
        EXPECT_GE(rendering.shadows.spotResolution, 256u);
        EXPECT_GE(rendering.shadows.pointResolution, 256u);
        EXPECT_LE(rendering.shadows.pointResolution, 4096u);
        EXPECT_LE(rendering.shadows.directionalFilterRadius, 4u);
        EXPECT_LE(rendering.shadows.spotFilterRadius, 4u);
        EXPECT_GE(rendering.shadows.pointFilterSamples, 1u);
        EXPECT_LE(rendering.shadows.pointFilterSamples, 20u);
        EXPECT_GE(rendering.maxAnisotropy, 1u);
        EXPECT_LE(rendering.maxAnisotropy, 16u);
        EXPECT_EQ(rendering.shadows.cascadeColors.size(), rendering.shadows.CascadeCount());
        EXPECT_LE(rendering.shadows.CascadeCount(), ShadowSettings::MaxCascades);

        EXPECT_GT(physics.fixedTimeStep, 0.0f);
        EXPECT_GE(physics.maxAccumulatedTime, physics.fixedTimeStep);
        EXPECT_GE(physics.velocitySteps, 1u);
        EXPECT_GE(physics.positionSteps, 1u);
        EXPECT_GE(physics.collisionSteps, 1u);
    }
}

TEST(QualityPresets, CascadeCountFollowsThePreset) {
    for (size_t i = 0; i < kQualityLevelCount; i++) {
        RenderingSettings rendering;
        PhysicsSettings physics;
        const QualityPreset& preset = GetQualityPreset(QualityLevel(i));
        ApplyQualityPreset(preset, rendering, physics);
        EXPECT_EQ(rendering.shadows.CascadeCount(), preset.cascades);
        for (size_t split = 1; split < rendering.shadows.cascadeSplits.size(); split++)
            EXPECT_GT(rendering.shadows.cascadeSplits[split], rendering.shadows.cascadeSplits[split - 1]);
    }
}

TEST(QualityPresets, KeepsTheProjectsOwnCascadeSplits) {
    RenderingSettings rendering;
    rendering.shadows.cascadeSplits = { 0.1f, 0.3f };    // as many cascades as High asks for, laid out differently
    PhysicsSettings physics;
    ApplyQualityPreset(GetQualityPreset(QualityLevel::High), rendering, physics);
    EXPECT_EQ(rendering.shadows.cascadeSplits, (std::vector<float>{ 0.1f, 0.3f }));
}

TEST(QualityPresets, LeavesWhatItDoesNotCover) {
    RenderingSettings rendering;
    rendering.renderPipeline = "deferred";
    rendering.shadows.directionalNormalBias = 3.5f;
    rendering.shadows.casterRange = 123.0f;
    PhysicsSettings physics;
    physics.gravity = glm::vec3(0.0f, -1.62f, 0.0f);

    ApplyQualityPreset(GetQualityPreset(QualityLevel::UltraLow), rendering, physics);

    EXPECT_EQ(rendering.renderPipeline, "deferred");
    EXPECT_EQ(rendering.shadows.directionalNormalBias, 3.5f);
    EXPECT_EQ(rendering.shadows.casterRange, 123.0f);
    EXPECT_EQ(physics.gravity, glm::vec3(0.0f, -1.62f, 0.0f));
}

TEST(QualityPresets, TimeStepScalesTheProjectsStep) {
    RenderingSettings rendering;
    PhysicsSettings physics;
    physics.fixedTimeStep = 0.01f;      // the example project runs at 100 Hz
    ApplyQualityPreset(GetQualityPreset(QualityLevel::UltraLow), rendering, physics);
    EXPECT_FLOAT_EQ(physics.fixedTimeStep, 0.02f);
    EXPECT_GE(physics.maxAccumulatedTime, physics.fixedTimeStep);
}

// ---- Resolving -------------------------------------------------------------------------------------------------------------------------------

TEST(QualityResolve, CustomChangesNothing) {
    RenderingSettings rendering;
    rendering.msaaSamples = 8;
    PhysicsSettings physics;
    physics.fixedTimeStep = 1.0f / 90.0f;

    const EffectiveQuality quality = ResolveQuality(rendering, physics, QualitySelection{}, Machine("llvmpipe", 0), false);
    EXPECT_FALSE(quality.presetActive);
    EXPECT_EQ(quality.rendering.msaaSamples, 8u);
    EXPECT_EQ(quality.physics.fixedTimeStep, physics.fixedTimeStep);
    EXPECT_EQ(quality.recommendation.level, QualityLevel::UltraLow) << "the recommendation is there to show even when it is not used";
}

TEST(QualityResolve, FixedIgnoresTheMachine) {
    QualitySelection selection;
    selection.mode = QualityMode::Fixed;
    selection.level = QualityLevel::UltraHigh;

    const EffectiveQuality quality = ResolveQuality(RenderingSettings{}, PhysicsSettings{}, selection, Machine("llvmpipe", 0), false);
    EXPECT_TRUE(quality.presetActive);
    EXPECT_EQ(quality.level, QualityLevel::UltraHigh);
    EXPECT_EQ(quality.rendering.msaaSamples, 8u);
}

TEST(QualityResolve, AutoFollowsTheMachine) {
    QualitySelection selection;
    selection.mode = QualityMode::Auto;

    const EffectiveQuality weak = ResolveQuality(RenderingSettings{}, PhysicsSettings{}, selection, Machine("Intel(R) UHD Graphics 630", 0), false);
    EXPECT_EQ(weak.level, QualityLevel::Low);
    EXPECT_EQ(weak.rendering.msaaSamples, GetQualityPreset(QualityLevel::Low).msaaSamples);

    const EffectiveQuality strong = ResolveQuality(RenderingSettings{}, PhysicsSettings{}, selection, Machine("NVIDIA GeForce RTX 5060", 8151), false);
    EXPECT_EQ(strong.level, QualityLevel::High);
    EXPECT_EQ(strong.rendering.msaaSamples, 4u);
}

TEST(QualityResolve, DoesNotChangeTheProjectsValues) {
    RenderingSettings rendering;
    PhysicsSettings physics;
    QualitySelection selection;
    selection.mode = QualityMode::Fixed;
    selection.level = QualityLevel::UltraLow;

    ResolveQuality(rendering, physics, selection, Machine("x", 0), false);
    EXPECT_EQ(rendering.msaaSamples, RenderingSettings{}.msaaSamples);
    EXPECT_EQ(physics.fixedTimeStep, PhysicsSettings{}.fixedTimeStep);
}

TEST(QualityResolve, DeterministicRunKeepsTheSimulation) {
    PhysicsSettings physics;
    QualitySelection selection;
    selection.mode = QualityMode::Fixed;
    selection.level = QualityLevel::UltraLow;

    const EffectiveQuality quality = ResolveQuality(RenderingSettings{}, physics, selection, Machine("x", 0), true);
    EXPECT_TRUE(quality.presetActive);
    EXPECT_FALSE(quality.physicsOverridden);
    EXPECT_EQ(quality.physics.fixedTimeStep, physics.fixedTimeStep);
    EXPECT_EQ(quality.physics.velocitySteps, physics.velocitySteps);
    EXPECT_EQ(quality.rendering.msaaSamples, 1u) << "the image still follows the preset";
}

TEST(QualityResolve, PublishesTheGates) {
    QualitySelection selection;
    selection.mode = QualityMode::Fixed;
    selection.level = QualityLevel::Low;
    const HardwareProfile hardware = Machine("NVIDIA GeForce RTX 5060", 8151);

    SetActiveQuality(ResolveQuality(RenderingSettings{}, PhysicsSettings{}, selection, hardware, false), hardware);
    EXPECT_FALSE(QualityAllowsSSAO());
    EXPECT_FALSE(QualityAllowsSSR());
    EXPECT_EQ(QualityMaxAnisotropy(), 4u);
    EXPECT_EQ(GetDetectedHardware().gpuName, "NVIDIA GeForce RTX 5060");

    SetActiveQuality(ResolveQuality(RenderingSettings{}, PhysicsSettings{}, QualitySelection{}, hardware, false), hardware);
    EXPECT_TRUE(QualityAllowsSSAO());
    EXPECT_TRUE(QualityAllowsSSR());
    EXPECT_EQ(QualityMaxAnisotropy(), 16u);
}

TEST(QualityResolve, DescribesWhatItSets) {
    QualitySelection selection;
    selection.mode = QualityMode::Fixed;
    selection.level = QualityLevel::Medium;
    const EffectiveQuality quality = ResolveQuality(RenderingSettings{}, PhysicsSettings{}, selection, Machine("x", 0), false);
    const std::vector<std::string> lines = DescribeQuality(quality);
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines[0], "MSAA : 2x");
}

// ---- Project file ----------------------------------------------------------------------------------------------------------------------------

namespace {
    std::shared_ptr<Project> MakeProject(const std::filesystem::path& dir)
    {
        const std::string root = dir.generic_string();
        auto project = std::make_shared<Project>("quality_test", Shard::Engine::Filesystem::Path(root), Shard::Engine::Filesystem::Path(root), Shard::Engine::Filesystem::Path(root),
                                                 BuildSettings(), Shard::Engine::Filesystem::Path((dir / "db.json").generic_string()));
        project->versionMajor = VERSION_MAJOR;
        project->versionMinor = VERSION_MINOR;
        project->versionPatch = VERSION_PATCH;
        return project;
    }
}

TEST(QualityProjectFile, AProjectWithoutAQualityBlockIsCustom) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "shard_quality_default";
    std::filesystem::create_directories(dir);
    auto project = MakeProject(dir);
    EXPECT_EQ(project->GetQualitySettings()->preset.mode, QualityMode::Custom);

    nlohmann::json data = Serialization::ProjectToJson(project.get());
    data.erase("quality");
    for (const char* key : { "ssao", "ssr", "maxAnisotropy" })
        data["rendering"].erase(key);
    for (const char* key : { "velocitySteps", "positionSteps", "collisionSteps" })
        data["physics"].erase(key);
    const std::string file = (dir / "project.json").generic_string();
    ASSERT_TRUE(Shard::Engine::Filesystem::Path(file).WriteFile(data.dump()));

    auto loaded = Serialization::DeserializeProject(Shard::Engine::Filesystem::Path(file));
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->GetQualitySettings()->preset.mode, QualityMode::Custom);
    EXPECT_EQ(loaded->GetRenderingSettings()->msaaSamples, RenderingSettings{}.msaaSamples);
    EXPECT_EQ(loaded->GetRenderingSettings()->maxAnisotropy, 16u);
    EXPECT_TRUE(loaded->GetRenderingSettings()->ssao);
    EXPECT_EQ(loaded->GetPhysicsSettings()->velocitySteps, PhysicsSettings{}.velocitySteps);
    std::filesystem::remove_all(dir);
}

TEST(QualityProjectFile, SelectionAndNewSettingsRoundTrip) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "shard_quality_roundtrip";
    std::filesystem::create_directories(dir);
    auto project = MakeProject(dir);

    ASSERT_TRUE(ParseQualitySelection("ultra_low", project->GetQualitySettings()->preset));
    project->GetRenderingSettings()->msaaSamples = 1;
    project->GetRenderingSettings()->ssr = false;
    project->GetRenderingSettings()->maxAnisotropy = 8;
    project->GetPhysicsSettings()->velocitySteps = 6;
    project->GetPhysicsSettings()->collisionSteps = 2;

    const std::string file = (dir / "project.json").generic_string();
    ASSERT_TRUE(Shard::Engine::Filesystem::Path(file).WriteFile(Serialization::ProjectToJson(project.get()).dump()));

    auto loaded = Serialization::DeserializeProject(Shard::Engine::Filesystem::Path(file));
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->GetQualitySettings()->preset.mode, QualityMode::Fixed);
    EXPECT_EQ(loaded->GetQualitySettings()->preset.level, QualityLevel::UltraLow);
    EXPECT_EQ(loaded->GetRenderingSettings()->msaaSamples, 1u) << "MSAA off is a value the file keeps";
    EXPECT_FALSE(loaded->GetRenderingSettings()->ssr);
    EXPECT_EQ(loaded->GetRenderingSettings()->maxAnisotropy, 8u);
    EXPECT_EQ(loaded->GetPhysicsSettings()->velocitySteps, 6u);
    EXPECT_EQ(loaded->GetPhysicsSettings()->collisionSteps, 2u);
    std::filesystem::remove_all(dir);
}

TEST(QualityProjectFile, ABadPresetNameFallsBackToCustom) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "shard_quality_bad";
    std::filesystem::create_directories(dir);
    auto project = MakeProject(dir);

    nlohmann::json data = Serialization::ProjectToJson(project.get());
    data["quality"]["preset"] = "turbo";
    const std::string file = (dir / "project.json").generic_string();
    ASSERT_TRUE(Shard::Engine::Filesystem::Path(file).WriteFile(data.dump()));

    auto loaded = Serialization::DeserializeProject(Shard::Engine::Filesystem::Path(file));
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->GetQualitySettings()->preset.mode, QualityMode::Custom);
    std::filesystem::remove_all(dir);
}

TEST(QualityProjectFile, ChangingThePresetMakesTheProjectDirty) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "shard_quality_dirty";
    std::filesystem::create_directories(dir);
    auto project = MakeProject(dir);
    project->MarkSaved();
    EXPECT_FALSE(project->HasUnsavedChanges());
    ASSERT_TRUE(ParseQualitySelection("auto", project->GetQualitySettings()->preset));
    EXPECT_TRUE(project->HasUnsavedChanges());
    std::filesystem::remove_all(dir);
}
