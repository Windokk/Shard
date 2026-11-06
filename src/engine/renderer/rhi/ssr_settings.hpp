#pragma once

#include <algorithm>
#include <string>

namespace Shard::Engine::Rendering {

    enum class SSRQuality
    {
        Low,
        Medium,
        High,
        Ultra,
        Custom
    };

    inline const char* SSRQualityName(SSRQuality quality)
    {
        switch (quality)
        {
            case SSRQuality::Low:    return "low";
            case SSRQuality::Medium: return "medium";
            case SSRQuality::High:   return "high";
            case SSRQuality::Ultra:  return "ultra";
            default:                 return "custom";
        }
    }

    inline bool ParseSSRQuality(const std::string& name, SSRQuality& out)
    {
        for (SSRQuality candidate : { SSRQuality::Low, SSRQuality::Medium, SSRQuality::High, SSRQuality::Ultra, SSRQuality::Custom })
        {
            if (name == SSRQualityName(candidate))
            {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    struct SSRSettings
    {
        bool enabled = false;
        SSRQuality quality = SSRQuality::Medium;

        float intensity = 1.0f;
        float maxDistance = 50.0f;
        float thickness = 0.3f;
        float maxRoughness = 0.6f;
        float roughnessBlur = 1.0f;
        float edgeFade = 0.1f;

        int maxSteps = 48;
        int resolutionDivisor = 2;
        bool temporal = true;
        float temporalBlend = 0.85f;

        void ApplyQuality(SSRQuality preset)
        {
            quality = preset;
            switch (preset)
            {
                case SSRQuality::Low:    maxSteps = 32; resolutionDivisor = 4; temporal = true; break;
                case SSRQuality::Medium: maxSteps = 48; resolutionDivisor = 2; temporal = true; break;
                case SSRQuality::High:   maxSteps = 64; resolutionDivisor = 2; temporal = true; break;
                case SSRQuality::Ultra:  maxSteps = 96; resolutionDivisor = 1; temporal = true; break;
                case SSRQuality::Custom: break;
            }
        }

        int ClampedDivisor() const { return resolutionDivisor >= 4 ? 4 : (resolutionDivisor >= 2 ? 2 : 1); }

        bool operator==(const SSRSettings& o) const
        {
            return enabled == o.enabled && quality == o.quality && intensity == o.intensity && maxDistance == o.maxDistance &&
                   thickness == o.thickness && maxRoughness == o.maxRoughness && roughnessBlur == o.roughnessBlur && edgeFade == o.edgeFade &&
                   maxSteps == o.maxSteps && resolutionDivisor == o.resolutionDivisor && temporal == o.temporal && temporalBlend == o.temporalBlend;
        }
        bool operator!=(const SSRSettings& o) const { return !(*this == o); }
    };

}
