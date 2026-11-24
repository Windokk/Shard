#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Shard::Engine::Projects{

    enum class QualityLevel : uint8_t { UltraLow = 0, Low, Medium, High, UltraHigh };

    inline constexpr size_t kQualityLevelCount = 5;

    // "ultra_low", "low", "medium", "high", "ultra_high" : the spelling of the project file and of `quality.preset`
    const char* QualityLevelName(QualityLevel level);
    // "Ultra Low", ... : for the editor
    const char* QualityLevelLabel(QualityLevel level);
    // Accepts the names above, and "ultralow", "ultra low", "ultra-high"... in any case
    bool ParseQualityLevel(std::string_view text, QualityLevel& out);

    // Custom : no preset, the project's own values are used (the default, and what a project had before presets existed).
    // Auto   : the preset recommended for this machine (RecommendQuality).
    // Fixed  : `level`, whatever the machine.
    enum class QualityMode : uint8_t { Custom = 0, Auto, Fixed };

    struct QualitySelection{
        QualityMode mode = QualityMode::Custom;
        QualityLevel level = QualityLevel::High;

        bool operator==(const QualitySelection& other) const { return mode == other.mode && (mode != QualityMode::Fixed || level == other.level); }
        bool operator!=(const QualitySelection& other) const { return !(*this == other); }
    };

    // "custom", "auto" or a level name
    std::string FormatQualitySelection(const QualitySelection& selection);
    bool ParseQualitySelection(std::string_view text, QualitySelection& out);

    struct QualitySettings{
        QualitySelection preset;
    };

    // What the engine knows of the machine, as far as the quality presets are concerned
    struct HardwareProfile{
        std::string gpuName;        // GL_RENDERER, or the Vulkan device name
        std::string gpuVendor;
        std::string api;            // "opengl" or "vulkan" : informative, it does not change the choice
        uint64_t vramMB = 0;        // dedicated video memory, 0 when unknown (or shared with the system, as on an integrated GPU)
        uint32_t cpuLogicalCores = 0;   // 0 : unknown
        uint64_t ramMB = 0;             // 0 : unknown
    };
}
