#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

// The maths behind the spatial effects of the audio (reverb zones, doppler), kept free of miniaudio and of the engine so they are
// plain functions : the audio manager feeds them, the tests call them directly.

namespace Shard::Engine::Audio
{
    /// The shape of the region a reverb zone covers
    enum class ReverbShape : int {
        Box = 0,    // the box of the actor (half extent, turned and scaled with it)
        Sphere      // a sphere around the actor (radius, scaled with its largest scale)
    };

    /// Ready-made rooms. Picking one fills the settings of the zone with its values, Custom leaves them as they are.
    enum class ReverbPreset : int {
        Custom = 0,
        Generic,
        Room,
        Bathroom,
        LivingRoom,
        StoneRoom,
        Auditorium,
        ConcertHall,
        Cave,
        Arena,
        Hangar,
        Corridor,
        Sewer,
        Underwater
    };

    /// What a reverb is made of. Read by the reverb bus of the audio manager (see ReverbBusNode).
    struct ReverbParams
    {
        float roomSize = 0.5f;      // 0 to 1 : the length of the tail
        float damping = 0.25f;      // 0 to 1 : how fast the high frequencies die in the tail
        float width = 1.0f;         // 0 to 1 : stereo spread of the tail
        float wetLevel = 0.5f;      // level of the reverberated sound, 0 to 2
        float dryLevel = 1.0f;      // level of the direct sound while in the zone, 0 to 1
        float preDelayMs = 10.0f;   // delay before the tail starts, in milliseconds
        float lowCutHz = 20.0f;     // the reverberated sound has nothing below this frequency
        float highCutHz = 12000.0f; // ... and nothing above this one
    };

    /// The settings a preset stands for
    inline ReverbParams GetReverbPreset(ReverbPreset preset)
    {
        //                         room   damp   width  wet    dry    preDelay lowCut  highCut
        switch (preset)
        {
            case ReverbPreset::Generic:     return { 0.50f, 0.30f, 1.00f, 0.45f, 0.90f, 10.0f,   20.0f, 12000.0f };
            case ReverbPreset::Room:        return { 0.35f, 0.45f, 0.80f, 0.40f, 0.90f,  5.0f,   40.0f,  9000.0f };
            case ReverbPreset::Bathroom:    return { 0.45f, 0.10f, 0.70f, 0.60f, 0.80f,  3.0f,   80.0f, 14000.0f };
            case ReverbPreset::LivingRoom:  return { 0.25f, 0.65f, 0.80f, 0.30f, 1.00f,  4.0f,   40.0f,  6000.0f };
            case ReverbPreset::StoneRoom:   return { 0.60f, 0.20f, 0.90f, 0.55f, 0.85f,  8.0f,   60.0f, 11000.0f };
            case ReverbPreset::Auditorium:  return { 0.75f, 0.40f, 1.00f, 0.55f, 0.80f, 22.0f,   60.0f,  9000.0f };
            case ReverbPreset::ConcertHall: return { 0.85f, 0.35f, 1.00f, 0.65f, 0.75f, 35.0f,   60.0f,  9500.0f };
            case ReverbPreset::Cave:        return { 0.90f, 0.25f, 1.00f, 0.75f, 0.70f, 45.0f,   80.0f,  7000.0f };
            case ReverbPreset::Arena:       return { 0.95f, 0.45f, 1.00f, 0.60f, 0.75f, 80.0f,   80.0f,  8000.0f };
            case ReverbPreset::Hangar:      return { 0.92f, 0.30f, 1.00f, 0.70f, 0.70f, 55.0f,  100.0f,  8500.0f };
            case ReverbPreset::Corridor:    return { 0.40f, 0.30f, 0.50f, 0.55f, 0.85f,  6.0f,   80.0f, 10000.0f };
            case ReverbPreset::Sewer:       return { 0.70f, 0.15f, 0.80f, 0.80f, 0.70f, 20.0f,  100.0f,  5000.0f };
            case ReverbPreset::Underwater:  return { 0.60f, 0.90f, 0.90f, 0.85f, 0.50f, 15.0f,  120.0f,  1800.0f };
            case ReverbPreset::Custom:
            default:                        return ReverbParams();
        }
    }

    inline float SmoothStep01(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    inline ReverbParams LerpReverb(const ReverbParams& a, const ReverbParams& b, float t)
    {
        auto mix = [t](float x, float y){ return x + (y - x) * t; };
        ReverbParams r;
        r.roomSize = mix(a.roomSize, b.roomSize);
        r.damping = mix(a.damping, b.damping);
        r.width = mix(a.width, b.width);
        r.wetLevel = mix(a.wetLevel, b.wetLevel);
        r.dryLevel = mix(a.dryLevel, b.dryLevel);
        r.preDelayMs = mix(a.preDelayMs, b.preDelayMs);
        r.lowCutHz = mix(a.lowCutHz, b.lowCutHz);
        r.highCutHz = mix(a.highCutHz, b.highCutHz);
        return r;
    }

    /// How much a zone covers a point : 1 inside, falling to 0 over `blendDistance` outside its edge (0 : a hard edge).
    /// @param localPosition  the point, relative to the center of the zone and in its axes (the rotation undone)
    /// @param halfExtent     Box only : half of the size along each axis, scale included
    /// @param radius         Sphere only : scale included
    inline float ReverbZoneWeight(ReverbShape shape, const glm::vec3& localPosition, const glm::vec3& halfExtent, float radius, float blendDistance)
    {
        float outside = 0.0f;
        if (shape == ReverbShape::Sphere)
        {
            outside = std::max(0.0f, glm::length(localPosition) - radius);
        }
        else
        {
            const glm::vec3 excess = glm::max(glm::abs(localPosition) - halfExtent, glm::vec3(0.0f));
            outside = glm::length(excess);
        }

        if (outside <= 0.0f)
            return 1.0f;
        if (blendDistance <= 0.0f)
            return 0.0f;

        return SmoothStep01(1.0f - outside / blendDistance);
    }

    /// One zone as the listener sees it
    struct ReverbZoneSample
    {
        float weight = 0.0f;    // 0 to 1, see ReverbZoneWeight
        int priority = 0;
        ReverbParams params;
    };

    /// What the listener hears of all the zones around it
    struct ReverbMix
    {
        ReverbParams params;    // the zones blended (wetLevel and dryLevel are the ones at full coverage)
        float coverage = 0.0f;  // 0 : in no zone, 1 : fully inside zones
    };

    /// Combines the zones the listener is in or next to. Zones of a higher priority cover the lower ones (they only show through where the
    /// higher ones do not cover), zones of the same priority blend by their weight.
    inline ReverbMix MixReverbZones(std::vector<ReverbZoneSample> zones)
    {
        ReverbMix result;

        std::stable_sort(zones.begin(), zones.end(), [](const ReverbZoneSample& a, const ReverbZoneSample& b){ return a.priority > b.priority; });

        float remaining = 1.0f;
        ReverbParams accumulated;
        accumulated.roomSize = accumulated.damping = accumulated.width = accumulated.wetLevel = 0.0f;
        accumulated.dryLevel = accumulated.preDelayMs = accumulated.lowCutHz = accumulated.highCutHz = 0.0f;

        size_t i = 0;
        while (i < zones.size() && remaining > 1e-5f)
        {
            // The zones of this priority : one blended zone, as heavy as the zones together cover
            size_t end = i;
            float totalWeight = 0.0f;
            float uncovered = 1.0f;
            while (end < zones.size() && zones[end].priority == zones[i].priority)
            {
                const float w = std::clamp(zones[end].weight, 0.0f, 1.0f);
                totalWeight += w;
                uncovered *= 1.0f - w;
                ++end;
            }

            const float groupWeight = 1.0f - uncovered;
            if (groupWeight > 1e-5f && totalWeight > 1e-5f)
            {
                ReverbParams group;
                group.roomSize = group.damping = group.width = group.wetLevel = 0.0f;
                group.dryLevel = group.preDelayMs = group.lowCutHz = group.highCutHz = 0.0f;
                for (size_t k = i; k < end; ++k)
                {
                    const float share = std::clamp(zones[k].weight, 0.0f, 1.0f) / totalWeight;
                    const ReverbParams& p = zones[k].params;
                    group.roomSize += p.roomSize * share;
                    group.damping += p.damping * share;
                    group.width += p.width * share;
                    group.wetLevel += p.wetLevel * share;
                    group.dryLevel += p.dryLevel * share;
                    group.preDelayMs += p.preDelayMs * share;
                    group.lowCutHz += p.lowCutHz * share;
                    group.highCutHz += p.highCutHz * share;
                }

                const float contribution = groupWeight * remaining;
                accumulated.roomSize += group.roomSize * contribution;
                accumulated.damping += group.damping * contribution;
                accumulated.width += group.width * contribution;
                accumulated.wetLevel += group.wetLevel * contribution;
                accumulated.dryLevel += group.dryLevel * contribution;
                accumulated.preDelayMs += group.preDelayMs * contribution;
                accumulated.lowCutHz += group.lowCutHz * contribution;
                accumulated.highCutHz += group.highCutHz * contribution;
                remaining *= 1.0f - groupWeight;
            }

            i = end;
        }

        result.coverage = 1.0f - remaining;
        if (result.coverage > 1e-5f)
        {
            const float inv = 1.0f / result.coverage;
            result.params.roomSize = accumulated.roomSize * inv;
            result.params.damping = accumulated.damping * inv;
            result.params.width = accumulated.width * inv;
            result.params.wetLevel = accumulated.wetLevel * inv;
            result.params.dryLevel = accumulated.dryLevel * inv;
            result.params.preDelayMs = accumulated.preDelayMs * inv;
            result.params.lowCutHz = accumulated.lowCutHz * inv;
            result.params.highCutHz = accumulated.highCutHz * inv;
        }
        else
        {
            result.coverage = 0.0f;
        }

        return result;
    }

    /// The factor the pitch of a source is multiplied by because it and the listener move relative to each other.
    /// > 1 when they come closer, < 1 when they move apart, 1 when they are still or `dopplerScale` is 0.
    /// @param dopplerScale  multiplies both velocities (0 turns the effect off, 1 is physical, more exaggerates it)
    inline float ComputeDopplerPitch(const glm::dvec3& listenerPosition, const glm::dvec3& listenerVelocity,
                                     const glm::dvec3& sourcePosition, const glm::dvec3& sourceVelocity,
                                     float speedOfSound, float dopplerScale)
    {
        if (dopplerScale <= 0.0f || speedOfSound <= 0.0f)
            return 1.0f;

        const glm::dvec3 toSource = sourcePosition - listenerPosition;
        const double distance = glm::length(toSource);
        if (distance < 1e-4)
            return 1.0f;

        const glm::dvec3 axis = toSource / distance;
        const double c = speedOfSound;

        // The speeds along the line between the two, an object can't catch up with its own sound
        const double limit = c * 0.9;
        const double listenerAlong = std::clamp(glm::dot(listenerVelocity, axis) * dopplerScale, -limit, limit); // + : toward the source
        const double sourceAlong = std::clamp(glm::dot(sourceVelocity, axis) * dopplerScale, -limit, limit);     // + : away from the listener

        const double pitch = (c + listenerAlong) / (c + sourceAlong);
        return static_cast<float>(std::clamp(pitch, 0.25, 4.0));
    }
}
