const float PI = 3.14159265359;

uniform int   uLightCount;
uniform int   uMaxBounces;

uniform bool  uHasSky;
uniform vec3  uSkyColor;
// Texture unit above the forward pass (0-31) and probe tracer (40-46) ranges. Bound with a raw Bind() that GLStateCache does not know about.
layout(binding = 45) uniform samplerCube uSkyCube;

uniform vec3 uAmbientColor;

// Clamps the luminance of indirect samples so one lucky path does not leave a permanent white speck. 0 = no limit.
uniform float uFireflyClamp;

// Next event estimation on emissive triangles, MIS-weighted (balance heuristic) against paths that hit the emitter by chance.
#ifdef SHARD_EMISSIVE_NEE
uniform int   uEmissiveCount;
uniform float uEmissivePower;
#endif

uint rngState;

uint PcgHash(uint x)
{
    uint state = x * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RandomFloat()
{
    rngState = PcgHash(rngState);
    return float(rngState) / 4294967296.0;
}

vec2 RandomVec2()
{
    return vec2(RandomFloat(), RandomFloat());
}

// Duff et al., "Building an Orthonormal Basis, Revisited".
void BuildONB(vec3 n, out vec3 t, out vec3 b)
{
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float bb = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * bb, -s * n.x);
    b = vec3(bb, s + n.y * n.y * a, -n.y);
}

vec3 CosineSampleHemisphere(vec3 n, vec2 u)
{
    float r = sqrt(u.x);
    float phi = 2.0 * PI * u.y;
    vec3 t, b;
    BuildONB(n, t, b);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u.x)));
}

vec3 CosineSampleHemisphere(vec3 n)
{
    return CosineSampleHemisphere(n, RandomVec2());
}

vec3 SampleGGX(vec3 n, float roughness)
{
    vec2 u = RandomVec2();
    float a = max(roughness * roughness, 1e-4);
    float phi = 2.0 * PI * u.x;
    float cosTheta = sqrt((1.0 - u.y) / (1.0 + (a * a - 1.0) * u.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    vec3 hLocal = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    vec3 t, b;
    BuildONB(n, t, b);
    return normalize(t * hLocal.x + b * hLocal.y + n * hLocal.z);
}

vec3 SampleCone(vec3 axis, float halfAngle, vec2 u)
{
    float cosTheta = mix(1.0, cos(halfAngle), u.x);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = 2.0 * PI * u.y;
    vec3 t, b;
    BuildONB(axis, t, b);
    return normalize(t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + axis * cosTheta);
}

#include "pbr_direct.glsl"

vec3 SampleLight(Light light, vec3 hitPos, vec2 u, out vec3 toLight, out float dist)
{
    if (light.type == 0)
    {
        vec3 dir = normalize(-light.direction.xyz);
        if (light.sourceSize > 0.0)
            dir = SampleCone(dir, radians(light.sourceSize), u);

        toLight = dir;
        dist = 1e30;
        return light.color.rgb * light.intensity;
    }

    vec3 center = light.position.xyz;
    vec3 toCenter = center - hitPos;
    float centerDist = length(toCenter);

    vec3 samplePos = center;
    if (light.sourceSize > 0.0 && centerDist > 1e-4)
    {
        vec3 t, b;
        BuildONB(toCenter / centerDist, t, b);
        float r = light.sourceSize * sqrt(u.x);
        float phi = 2.0 * PI * u.y;
        samplePos += (t * cos(phi) + b * sin(phi)) * r;
    }

    vec3 d = samplePos - hitPos;
    float dist2 = max(dot(d, d), 1e-4);
    dist = sqrt(dist2);
    toLight = d / dist;

    float atten = LightRangeFalloff(light.radius, centerDist) / dist2;
    vec3 incoming = light.color.rgb * light.intensity * atten;

    if (light.type == 2)
    {
        float cosAngle = dot(-toLight, normalize(light.direction.xyz));
        float t = clamp((cosAngle - light.outerCutoff) / max(light.innerCutoff - light.outerCutoff, 1e-4), 0.0, 1.0);
        incoming *= t;
    }

    return incoming;
}

bool LightOccluded(Light light, vec3 pos, vec3 ng, vec3 toLight, float dist)
{
    if (light.castShadow == 0)
        return false;

    return TraceRayAny(pos + ng * 1e-3, toLight, dist - 2e-3);
}

vec3 GetSkyColor(vec3 rd)
{
    // LOD 0: no implicit derivatives in compute, a mipped cubemap would pick an undefined level.
    return uHasSky ? textureLod(uSkyCube, rd, 0.0).rgb : uSkyColor;
}

// Karis' split-sum fit, same as lit.frag's EnvBRDFApprox.
vec3 SpecularAlbedoApprox(vec3 F0, float roughness, float NdotV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

vec3 gPathTerms[4];

void AddPathLight(inout vec3 radiance, vec3 light, int term)
{
    radiance += light;
    gPathTerms[term] += light;
}

#ifdef SHARD_EMISSIVE_NEE
bool EmissiveIsSampled(GPUMaterial mat, float area)
{
    return (mat.textureFlags & TEX_EMISSIVE_BIT) == 0u && dot(mat.emissive.rgb, vec3(0.2126, 0.7152, 0.0722)) > 0.0 && area > 1e-10;
}

vec3 EmissiveDirect(vec3 pos, vec3 ng, vec3 n, float lobeProb)
{
    if (uEmissiveCount <= 0 || uEmissivePower <= 0.0)
        return vec3(0.0);

    float pick = RandomFloat();
    int lo = 0;
    int hi = uEmissiveCount - 1;
    while (lo < hi)
    {
        int mid = (lo + hi) >> 1;
        if (emissiveTris[mid].cdf >= pick)
            hi = mid;
        else
            lo = mid + 1;
    }

    uint triIndex = emissiveTris[lo].tri;
    TrianglePos t = triPos[triIndex];

    vec2 r = RandomVec2();
    float s = sqrt(r.x);
    float b1 = 1.0 - s;
    float b2 = r.y * s;
    vec3 p = t.v0.xyz * (1.0 - b1 - b2) + t.v1.xyz * b1 + t.v2.xyz * b2;

    vec3 emitterNormal = cross(t.v1.xyz - t.v0.xyz, t.v2.xyz - t.v0.xyz);
    float doubleArea = length(emitterNormal);
    if (doubleArea < 2e-10)
        return vec3(0.0);
    emitterNormal /= doubleArea;

    vec3 d = p - pos;
    float dist2 = dot(d, d);
    if (dist2 < 1e-8)
        return vec3(0.0);
    float dist = sqrt(dist2);
    vec3 wi = d / dist;

    float NdotL = dot(n, wi);
    float cosEmitter = abs(dot(emitterNormal, wi));
    if (NdotL <= 0.0 || dot(ng, wi) <= 0.0 || cosEmitter < 1e-4)
        return vec3(0.0);

    vec3 Le = materials[uint(triAttrib[triIndex].uvMatID.w)].emissive.rgb;
    float lum = dot(Le, vec3(0.2126, 0.7152, 0.0722));

    float pdfLight = lum * dist2 / (uEmissivePower * cosEmitter);
    float pdfBsdf = lobeProb * NdotL / PI;

    if (TraceRayAny(pos + ng * 1e-3, wi, dist * 0.999 - 2e-3))
        return vec3(0.0);

    return Le * NdotL / (pdfLight + pdfBsdf);
}
#endif

vec3 TracePathMIS(vec3 ro, vec3 rd, vec3 firstNormal, float firstLobe)
{
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 prevNormal = firstNormal;
    float prevLobe = firstLobe;

    for (int t = 0; t < 4; t++)
        gPathTerms[t] = vec3(0.0);
    int pathTerm = 3;

    for (int bounce = 0; bounce < uMaxBounces; bounce++)
    {
        HitInfo hit;
        if (!TraceRay(ro, rd, 1e30, hit))
        {
            AddPathLight(radiance, throughput * GetSkyColor(rd), pathTerm);
            break;
        }

        TriangleAttrib attrib = triAttrib[hit.triIndex];
        float w0 = 1.0 - hit.bary.x - hit.bary.y;
        vec3 n = normalize(attrib.n0.xyz * w0 + attrib.n1.xyz * hit.bary.x + attrib.n2.xyz * hit.bary.y);
        uint matID = uint(attrib.uvMatID.w);
        GPUMaterial mat = materials[matID];

        vec3 hitPos = ro + rd * hit.t;

        TrianglePos tri = triPos[hit.triIndex];
        vec3 ngRaw = cross(tri.v1.xyz - tri.v0.xyz, tri.v2.xyz - tri.v0.xyz);
        float hitArea = 0.5 * length(ngRaw);
        vec3 ng = normalize(ngRaw);

        if (dot(ng, rd) > 0.0)
            ng = -ng;
        if (dot(n, rd) > 0.0)
            n = -n;

        vec3 albedo = mat.albedo.rgb;
        float roughness = mat.roughness;
        float metallic = mat.metallic;
        vec3 emission = mat.emissive.rgb;

        if (mat.textureFlags != 0u)
        {
            vec2 uv = vec2(attrib.n0.w, attrib.uvMatID.x) * w0
                    + vec2(attrib.n1.w, attrib.uvMatID.y) * hit.bary.x
                    + vec2(attrib.n2.w, attrib.uvMatID.z) * hit.bary.y;

            if ((mat.textureFlags & TEX_ALBEDO_BIT) != 0u)
                albedo = textureLod(sampler2D(mat.albedoTex), uv, 0.0).rgb;

            if ((mat.textureFlags & TEX_METALLIC_BIT) != 0u)
                metallic *= textureLod(sampler2D(mat.metallicTex), uv, 0.0).r;

            if ((mat.textureFlags & TEX_ROUGHNESS_BIT) != 0u)
                roughness *= textureLod(sampler2D(mat.roughnessTex), uv, 0.0).r;

            if ((mat.textureFlags & TEX_EMISSIVE_BIT) != 0u)
                emission *= textureLod(sampler2D(mat.emissiveTex), uv, 0.0).rgb;

            if ((mat.textureFlags & TEX_NORMAL_BIT) != 0u)
            {
                vec3 tangent = normalize(attrib.t0.xyz * w0 + attrib.t1.xyz * hit.bary.x + attrib.t2.xyz * hit.bary.y);
                tangent = normalize(tangent - n * dot(tangent, n));
                vec3 bitangent = cross(n, tangent);
                mat3 TBN = mat3(tangent, bitangent, n);

                vec3 sampledNormal = DecodeTangentNormal(textureLod(sampler2D(mat.normalTex), uv, 0.0));
                n = normalize(TBN * sampledNormal);
            }
        }

#ifdef SHARD_EMISSIVE_NEE
        if (prevLobe > 0.0 && EmissiveIsSampled(mat, hitArea))
        {
            float lum = dot(emission, vec3(0.2126, 0.7152, 0.0722));
            float cosEmitter = max(abs(dot(ng, rd)), 1e-4);
            float pdfLight = lum * hit.t * hit.t / (uEmissivePower * cosEmitter);
            float pdfBsdf = prevLobe * max(dot(rd, prevNormal), 0.0) / PI;
            AddPathLight(radiance, throughput * emission * (pdfBsdf / max(pdfBsdf + pdfLight, 1e-12)), pathTerm);
        }
        else
#endif
        AddPathLight(radiance, throughput * emission, pathTerm);

        if (bounce == 0)
            AddPathLight(radiance, throughput * albedo * uAmbientColor, 1);

        vec3 viewDir = normalize(-rd);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);

        // Lobe chosen in proportion to what each reflects (specular directional albedo vs diffuse colour). Same mean for any probability, this one is least noisy. F0 alone gave fireflies on rough dielectrics.
        float specWeight = dot(SpecularAlbedoApprox(F0, roughness, max(dot(n, viewDir), 0.0)), vec3(0.2126, 0.7152, 0.0722));
        float diffuseWeight = dot((vec3(1.0) - F0) * (1.0 - metallic) * albedo, vec3(0.2126, 0.7152, 0.0722));
        float specProb = clamp(specWeight / max(specWeight + diffuseWeight, 1e-4), 0.04, 0.95);

#ifdef SHARD_EMISSIVE_NEE
        {
            vec3 kdDirect = (vec3(1.0) - F0) * (1.0 - metallic);
            AddPathLight(radiance, throughput * (kdDirect * albedo / PI) * EmissiveDirect(hitPos, ng, n, 1.0 - specProb), bounce == 0 ? 1 : pathTerm);
        }
#endif

        if (uLightCount > 0)
        {
            int lightIdx = min(int(RandomFloat() * float(uLightCount)), uLightCount - 1);
            Light light = lights[lightIdx];

            vec3 toLight;
            float dist;
            vec3 incomingRadiance = SampleLight(light, hitPos, RandomVec2(), toLight, dist);

            float NdotL = dot(n, toLight);
            if (NdotL > 0.0 && dot(ng, toLight) > 0.0 && dot(incomingRadiance, vec3(1.0)) > 0.0)
            {
                if (!LightOccluded(light, hitPos, ng, toLight, dist))
                {
                    vec3 h = normalize(viewDir + toLight);
                    vec3 F = FresnelSchlick(max(dot(h, viewDir), 0.0), F0);
                    float D = DistributionGGX(n, h, roughness);
                    float G = GeometrySmith(n, viewDir, toLight, roughness);
                    vec3 specular = (D * G * F) / max(4.0 * max(dot(n, viewDir), 0.0) * NdotL, 1e-4);
                    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
                    vec3 diffuse = kd * albedo / PI;

                    AddPathLight(radiance, throughput * (diffuse + specular) * incomingRadiance * NdotL * float(uLightCount), bounce == 0 ? 0 : pathTerm);
                }
            }
        }

        vec3 newDir;
        vec3 lobeWeight;

        bool specularDraw = RandomFloat() < specProb;
        if (bounce == 0)
            pathTerm = specularDraw ? 2 : 1;

        if (specularDraw)
        {
            prevLobe = 0.0;
            vec3 h = SampleGGX(n, roughness);
            newDir = reflect(rd, h);
            if (dot(newDir, n) <= 0.0 || dot(newDir, ng) <= 0.0)
                break;

            vec3 F = FresnelSchlick(max(dot(h, viewDir), 0.0), F0);
            float G = GeometrySmith(n, viewDir, newDir, roughness);
            float NdotV = max(dot(n, viewDir), 0.0);
            float NdotH = max(dot(n, h), 0.0);
            lobeWeight = (F * G * max(dot(newDir, h), 0.0)) / max(NdotV * NdotH * specProb, 1e-4);
        }
        else
        {
            newDir = CosineSampleHemisphere(n);
            float aboveGeometry = dot(newDir, ng);
            if (aboveGeometry < 0.05)
                newDir = normalize(newDir + ng * (0.05 - aboveGeometry));

            vec3 kd = (vec3(1.0) - F0) * (1.0 - metallic);
            lobeWeight = kd * albedo / max(1.0 - specProb, 1e-4);
            prevLobe = 1.0 - specProb;
            prevNormal = n;
        }

        throughput *= lobeWeight;

        if (bounce > 3)
        {
            float surviveProb = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
            if (RandomFloat() > surviveProb)
                break;
            throughput /= surviveProb;
        }

        ro = hitPos + ng * 1e-3;
        rd = newDir;
    }

    return radiance;
}

vec3 TracePath(vec3 ro, vec3 rd)
{
    return TracePathMIS(ro, rd, vec3(0.0, 1.0, 0.0), 0.0);
}

vec3 ClampFirefly(vec3 radiance)
{
    if (uFireflyClamp <= 0.0)
        return radiance;

    float lum = dot(radiance, vec3(0.2126, 0.7152, 0.0722));
    return lum > uFireflyClamp ? radiance * (uFireflyClamp / lum) : radiance;
}
