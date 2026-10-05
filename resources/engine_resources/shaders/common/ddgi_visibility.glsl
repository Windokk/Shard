// Chebyshev's inequality applied to a probe's stored (mean, mean^2) hit-distance distribution, to
// estimate how likely that probe can actually "see" a point `distToPoint` away without a wall between
// them - this (not just the trilinear grid weight) is what stops light/shadow from leaking through
// geometry the way a plain irradiance-only probe blend does (e.g. sun hitting a roof lighting the
// ceiling directly below it). Cubing the raw Chebyshev bound sharpens the falloff so partially-occluded
// probes fade out faster than a linear bound would - a soft bound alone still lets visible light bleed
// in near occlusion edges. Shared by compute/probes/probe_trace.comp's bounce-feedback loop (reads the
// previous bounce's not-yet-published atlas) and mesh/lit.frag's forward pass (reads the final
// published atlas) - same math either way, only which atlas is being tested differs.
float DDGI_VisibilityWeight(vec2 meanMean2, float distToPoint)
{
    float mean = meanMean2.x;
    if (distToPoint <= mean)
        return 1.0;

    float variance = abs(meanMean2.y - mean * mean);
    float d = distToPoint - mean;
    float chebyshev = variance / (variance + d * d);
    return max(chebyshev * chebyshev * chebyshev, 0.0);
}
