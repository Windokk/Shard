#include "lightmap_layout.hpp"

#include <algorithm>
#include <cmath>

#include "engine/core/math/rect_packer.hpp"

namespace Shard::Engine::Rendering {

    namespace {

        // Space kept around a lightmap in the atlas, on each side
        constexpr uint32_t kGap = 1;

        uint32_t NextPowerOfTwo(uint32_t value)
        {
            uint32_t power = 1;
            while (power < value)
                power <<= 1;
            return power;
        }
    }

    uint32_t LightmapResolutionFor(const LightmapInstanceInput& instance, const LightmapSettings& settings)
    {
        const uint32_t largest = std::max(settings.minResolution, settings.maxResolution);
        const uint32_t limit = std::min(largest, std::max(settings.maxPageSize, 2u * kGap + 1u) - 2u * kGap);

        if (instance.resolutionOverride > 0)
            return std::clamp(instance.resolutionOverride, 1u, limit);

        float wanted = float(settings.minResolution);
        if (instance.surfaceArea > 0.0f && instance.uvArea > 1e-6f)
            wanted = std::sqrt(instance.surfaceArea / instance.uvArea) * settings.texelsPerMeter;

        return std::clamp(uint32_t(std::lround(std::max(wanted, 0.0f))), std::min(settings.minResolution, limit), limit);
    }

    LightmapLayout PlanLightmapLayout(const std::vector<LightmapInstanceInput>& instances, const LightmapSettings& settings)
    {
        LightmapLayout layout;
        if (instances.empty())
            return layout;

        const uint32_t maxPage = std::max(NextPowerOfTwo(std::max(settings.maxPageSize, 16u)), 16u);

        std::vector<uint32_t> resolutions(instances.size());
        std::vector<Core::PackRect> rects(instances.size());
        double totalArea = 0.0;
        for (size_t i = 0; i < instances.size(); i++)
        {
            resolutions[i] = LightmapResolutionFor(instances[i], settings);
            const float side = float(resolutions[i] + 2 * kGap);
            rects[i].w = side;
            rects[i].h = side;
            totalArea += double(side) * side;
        }

        // The smallest page that holds everything : a first guess from the total area (the shelves waste a little of it), then
        // doubled until it works, up to the largest page there is
        uint32_t pageSize = std::min(maxPage, std::max(16u, NextPowerOfTwo(uint32_t(std::ceil(std::sqrt(totalArea * 1.1))))));
        int bins = -1;
        for (;;)
        {
            bins = Core::PackRectsShelf(rects, float(pageSize), float(pageSize), 1);
            if (bins == 1 || pageSize >= maxPage)
                break;
            pageSize *= 2;
        }

        if (bins != 1)
        {
            // More than one page of the largest size : every lightmap is smaller than a page (see LightmapResolutionFor)
            pageSize = maxPage;
            bins = Core::PackRectsShelf(rects, float(pageSize), float(pageSize));
        }

        layout.pageSize = pageSize;
        layout.pageCount = uint32_t(std::max(bins, 0));
        layout.placements.resize(instances.size());
        for (size_t i = 0; i < instances.size(); i++)
        {
            LightmapPlacement& placement = layout.placements[i];
            placement.key = instances[i].key;
            placement.page = uint32_t(std::max(rects[i].bin, 0));
            placement.x = uint32_t(rects[i].x) + kGap;
            placement.y = uint32_t(rects[i].y) + kGap;
            placement.resolution = resolutions[i];
        }

        return layout;
    }
}
