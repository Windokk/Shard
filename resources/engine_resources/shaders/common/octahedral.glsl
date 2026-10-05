// Octahedral direction<->UV mapping, shared by every pass that stores or reads per-probe data in an
// octahedral atlas tile : compute/probes/probe_trace.comp (picks each ray's direction from its texel),
// compute/probes/probe_relocate.comp (reconstructs a texel's ray direction), and mesh/lit.frag (samples
// a probe's tile toward the shading normal). OctDecode/OctEncode are exact inverses of each other - keep
// them in sync if either ever changes.
vec3 OctDecode(vec2 f)
{
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

vec2 OctEncode(vec3 n)
{
    vec2 p = n.xy * (1.0 / (abs(n.x) + abs(n.y) + abs(n.z)));
    if (n.z <= 0.0)
        p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    return p;
}
