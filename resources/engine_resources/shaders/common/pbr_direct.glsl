// Needs `const float PI` declared by the includer. Epsilon-clamped GGX for single light samples. Do not unify with lit.frag's IBL specular, which uses a different k (roughness^2/2).
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
