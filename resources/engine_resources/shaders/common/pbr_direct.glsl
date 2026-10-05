// Requires `const float PI` to already be declared by the including file (both current consumers
// already declare their own before using these).
//
// Cook-Torrance GGX terms for evaluating (not importance-sampling) a direct-light specular response,
// epsilon-clamped throughout so a grazing/degenerate NdotV or NdotL never divides by ~0. Shared by
// compute/path_trace.comp's NEE term and compute/probes/probe_trace.comp's direct-lighting term - the
// only two places this exact (clamped) variant is used. NOT shared with mesh/lit.frag's IBL specular
// path, which uses a different k (roughness^2/2, unclamped) appropriate for a prefiltered environment
// map rather than a single light sample - do not unify the two, they're intentionally different.
float DistributionGGX(vec3 n, vec3 h, float roughness)
{
    float a = max(roughness * roughness, 1e-4);
    float a2 = a * a;
    float NdotH = max(dot(n, h), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-6);
}

float GeometrySmith(vec3 n, vec3 v, vec3 l, float roughness)
{
    return GeometrySchlickGGX(max(dot(n, v), 0.0), roughness) * GeometrySchlickGGX(max(dot(n, l), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
