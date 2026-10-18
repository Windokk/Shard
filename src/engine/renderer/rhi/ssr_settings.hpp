#pragma once

#include <algorithm>
#include <string>

namespace Shard::Engine::Rendering {

    /// The preset a world's screen-space reflections start from. A preset only sets the cost knobs (steps, resolution, temporal) : the look
    /// (intensity, distance, thickness, roughness...) is the world's. Touching a cost knob by hand makes the quality Custom.
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

    /// @brief How a world's screen-space reflections (see SSRManager) are traced and used. Stored with the world, handed to the renderer by the
    /// snapshot. Off by default : a world that never asked for it is drawn as before.
    struct SSRSettings
    {
        bool enabled = false;
        SSRQuality quality = SSRQuality::Medium;

        // ---- Look ----

        /// How much of the screen-space reflection replaces the probe / sky reflection where the ray found something (0 : none, 1 : all of it)
        float intensity = 1.0f;
        /// How far a reflected ray travels, in world units
        float maxDistance = 50.0f;
        /// How far behind the depth buffer's surface a ray may pass and still count as having hit it (the depth buffer has no back of things)
        float thickness = 0.3f;
        /// Beyond this roughness the reflection is left to the probes and the sky. The fade starts at 60% of it.
        float maxRoughness = 0.6f;
        /// Scale of how much the reflection is blurred by roughness (1 : the width of the specular lobe)
        float roughnessBlur = 1.0f;
        /// Fraction of the screen, from its edges, over which a reflection fades out : what is reflected is gone beyond them
        float edgeFade = 0.1f;

        // ---- Cost ----

        /// Iterations of the hierarchical depth march, at most
        int maxSteps = 48;
        /// The trace runs at 1 / resolutionDivisor of the screen, 1, 2 or 4 (the result is upsampled, edge aware)
        int resolutionDivisor = 2;
        /// The hit of a pixel is blended with the one of the frame before (reprojected), which steadies it on edges and when the trace is small
        bool temporal = true;
        /// How much of the history stays (0 : none, 0.95 : a lot). Reduced by itself while the camera moves.
        float temporalBlend = 0.85f;

        /// Sets the cost knobs of a preset (Custom changes nothing)
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

        /// What the renderer can run : a divisor other than 1, 2, 4 is not a size the passes were made for
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
