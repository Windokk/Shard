// The path tracing core shared by compute/path_trace.comp (the offline render) and compute/lightmap_bake.comp (the
// lightmap baker) : the RNG, the sampling routines, the light sampling and TracePath(), which follows one ray through
// the scene and returns the radiance that reaches its origin.
//
// Like raytracing_trace.glsl this works on global SSBO arrays by name, so the including shader has to declare, in this
// order, BEFORE including this file :
//
//     #include "../common/light.glsl"
//     #include "../common/raytracing_types.glsl"
//     #include "../common/normal_map.glsl"
//     layout(std430, binding = N)   readonly buffer BVHNodes          { BVHNode bvhNodes[]; };
//     layout(std430, binding = N+1) readonly buffer TrianglePositions { TrianglePos triPos[]; };
//     layout(std430, binding = N+2) readonly buffer TriangleAttribs   { TriangleAttrib triAttrib[]; };
//     layout(std430, binding = N+3) readonly buffer Materials         { GPUMaterial materials[]; };
//     layout(std430, binding = N+4) readonly buffer Lights            { Light lights[]; };
//     #include "../common/raytracing_trace.glsl"
//     #include "../common/path_tracing.glsl"
//
// The uniforms below are set by the host (Raytracer / LightmapManager).

const float PI = 3.14159265359;

uniform int   uLightCount;
uniform int   uMaxBounces;

// What a ray that escapes the scene sees : the level's skybox (the very same cubemap the sky is drawn from) when it has one,
// else a flat colour
uniform bool  uHasSky;
uniform vec3  uSkyColor;
// A texture unit well above the ones the forward pass uses (0-31) and the probe tracer's (40-46) : the host binds it with a
// raw Bind() that GLStateCache does not know about
layout(binding = 45) uniform samplerCube uSkyCube;

// lit.frag adds `albedo * ambientIntensity` to every surface, light or not (the world's flat ambient floor) : the offline
// render applies the same to the surface the camera sees. Left at 0 when baking, the forward pass adds it at run time.
uniform float uAmbientIntensity;

// A sample of indirect light brighter than this (luminance) is scaled down to it : one lucky path through a small, bright
// feature would otherwise be a white speck that no amount of averaging gets rid of. 0 : no limit.
uniform float uFireflyClamp;

// Direct sampling of the emissive surfaces (next event estimation), for the includer that defines SHARD_EMISSIVE_NEE and declares
//     layout(std430, binding = N) readonly buffer EmissiveTriangles { EmissiveTriangle emissiveTris[]; };
// before this file. A small bright emitter (a ceiling panel) is found by a path that bounces off a surface only by chance, which is
// a lot of noise ; sampling a point of it from every surface the path touches, with a shadow ray, makes that light as smooth as
// a Static light's. A path that finds the emitter by chance anyway is weighted against that sampling (multiple importance
// sampling, balance heuristic), so the light is not counted twice.
#ifdef SHARD_EMISSIVE_NEE
uniform int   uEmissiveCount;   // triangles on the list (0 : nothing is sampled, every emitter is left to the paths)
uniform float uEmissivePower;   // sum over the list of (luminance of the emission * area)
#endif

// ---------------------------------------------------------------------------
// RNG - PCG hash, seeded per-pixel per-sample so every dispatch draws an
// independent stream of pseudo-random numbers.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Sampling helpers
// ---------------------------------------------------------------------------

// Builds an orthonormal basis (t, b) around unit vector n (Duff et al., "Building an Orthonormal
// Basis, Revisited" - avoids the singularity/branch cost of a naive cross-product approach).
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

// Importance-samples the GGX half-vector distribution around n.
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

// A direction uniformly distributed in the cone of half angle `halfAngle` (radians) around `axis`
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

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

// One sample of a light as seen from `hitPos` : the direction toward it, the distance to it (1e30 for a directional light)
// and the radiance it sends along that direction - range window, spot cone and inverse-square law applied, shadows not (that
// is what the shadow ray the caller traces is for). `u` positions the sample on the light's emitter, so the penumbra comes
// out of the average of many samples : a directional light is a disc of angular radius sourceSize (degrees), a point or spot
// light a disc of radius sourceSize turned toward the shaded point.
vec3 SampleLight(Light light, vec3 hitPos, vec2 u, out vec3 toLight, out float dist)
{
    if (light.type == 0) // Directional
    {
        vec3 dir = normalize(-light.direction.xyz);
        if (light.sourceSize > 0.0)
            dir = SampleCone(dir, radians(light.sourceSize), u);

        toLight = dir;
        dist = 1e30;
        return light.color.rgb * light.intensity;
    }

    // Point / Spot
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

    if (light.type == 2) // Spot cone falloff
    {
        float cosAngle = dot(-toLight, normalize(light.direction.xyz));
        float t = clamp((cosAngle - light.outerCutoff) / max(light.innerCutoff - light.outerCutoff, 1e-4), 0.0, 1.0);
        incoming *= t;
    }

    return incoming;
}

// Whether something stands between `pos` and the light along `toLight` (always false for a light that casts no shadow, like
// the forward pass, which never samples a shadow map for it)
bool LightOccluded(Light light, vec3 pos, vec3 ng, vec3 toLight, float dist)
{
    if (light.castShadow == 0)
        return false;

    return TraceRayAny(pos + ng * 1e-3, toLight, dist - 2e-3);
}

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------

vec3 GetSkyColor(vec3 rd)
{
    // LOD 0 : implicit derivatives do not exist in a compute shader, and a cubemap with mips would pick an undefined level
    return uHasSky ? textureLod(uSkyCube, rd, 0.0).rgb : uSkyColor;
}

// ---------------------------------------------------------------------------
// The path
// ---------------------------------------------------------------------------

#ifdef SHARD_EMISSIVE_NEE
// Whether a triangle of this material is on the CPU's list of emitters (RaytraceScene::emissiveTriangles) : the two have to agree
bool EmissiveIsSampled(GPUMaterial mat, float area)
{
    return (mat.textureFlags & TEX_EMISSIVE_BIT) == 0u && dot(mat.emissive.rgb, vec3(0.2126, 0.7152, 0.0722)) > 0.0 && area > 1e-10;
}

// One sample of the emitters as seen from `pos` : the radiance that arrives from a random point of a random emitter, times the cosine
// at `pos` and the MIS weight, over the pdf of the sample - what the caller multiplies by the BRDF. `n` is the shading normal,
// `lobeProb` the probability with which the path's own sampling would have drawn a direction in the diffuse cosine lobe (the
// other strategy of the MIS). Zero when the emitter is behind the surface or something is in the way.
vec3 EmissiveDirect(vec3 pos, vec3 ng, vec3 n, float lobeProb)
{
    if (uEmissiveCount <= 0 || uEmissivePower <= 0.0)
        return vec3(0.0);

    // The emitter, by binary search on the CDF of the power
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

    // A point of it, uniformly by area
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
    float cosEmitter = abs(dot(emitterNormal, wi));      // an emissive surface sends its light from both sides, like the paths see it
    if (NdotL <= 0.0 || dot(ng, wi) <= 0.0 || cosEmitter < 1e-4)
        return vec3(0.0);

    vec3 Le = materials[uint(triAttrib[triIndex].uvMatID.w)].emissive.rgb;
    float lum = dot(Le, vec3(0.2126, 0.7152, 0.0722));

    // pdf, per solid angle : (power / total) of picking the triangle, 1 / area of the point on it, then the change of measure
    float pdfLight = lum * dist2 / (uEmissivePower * cosEmitter);
    float pdfBsdf = lobeProb * NdotL / PI;

    if (TraceRayAny(pos + ng * 1e-3, wi, dist * 0.999 - 2e-3))
        return vec3(0.0);

    return Le * NdotL / (pdfLight + pdfBsdf);       // = Le * NdotL / pdfLight * (pdfLight / (pdfLight + pdfBsdf))
}
#endif

// Radiance that reaches `ro` along the ray `rd` (pointing away from `ro`), after at most uMaxBounces bounces.
//
// With SHARD_EMISSIVE_NEE, `firstLobe` and `firstNormal` say how the ray was drawn : with probability `firstLobe` per unit of
// cosine weighted solid angle around `firstNormal` (the lightmap baker draws its first ray so : 1.0), which the emission it finds
// is weighted against. 0 : it is not weighted, the ray was not drawn from a diffuse lobe that the caller samples the emitters for.
vec3 TracePathMIS(vec3 ro, vec3 rd, vec3 firstNormal, float firstLobe)
{
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 prevNormal = firstNormal;
    float prevLobe = firstLobe;

    for (int bounce = 0; bounce < uMaxBounces; bounce++)
    {
        HitInfo hit;
        if (!TraceRay(ro, rd, 1e30, hit))
        {
            radiance += throughput * GetSkyColor(rd);
            break;
        }

        TriangleAttrib attrib = triAttrib[hit.triIndex];
        float w0 = 1.0 - hit.bary.x - hit.bary.y;
        vec3 n = normalize(attrib.n0.xyz * w0 + attrib.n1.xyz * hit.bary.x + attrib.n2.xyz * hit.bary.y);
        uint matID = uint(attrib.uvMatID.w);
        GPUMaterial mat = materials[matID];

        vec3 hitPos = ro + rd * hit.t;

        // The geometric normal of the triangle : what the rays that leave the surface are offset along. The shading normal
        // (interpolated, then perturbed by a normal map) can lean far enough from the flat triangle for such a ray to start
        // below it.
        TrianglePos tri = triPos[hit.triIndex];
        vec3 ngRaw = cross(tri.v1.xyz - tri.v0.xyz, tri.v2.xyz - tri.v0.xyz);
        float hitArea = 0.5 * length(ngRaw);
        vec3 ng = normalize(ngRaw);

        // Shade whichever side the ray actually came from (double-sided geometry)
        if (dot(ng, rd) > 0.0)
            ng = -ng;
        if (dot(n, rd) > 0.0)
            n = -n;

        // ---- Texture sampling : overrides (or, for metallic/roughness, modulates) the material's
        // scalar values wherever a bit is set - see GPUMaterial's comment. ----
        vec3 albedo = mat.albedo.rgb;
        float roughness = mat.roughness;
        float metallic = mat.metallic;
        vec3 emission = mat.emissive.rgb;

        if (mat.textureFlags != 0u)
        {
            vec2 uv = vec2(attrib.n0.w, attrib.uvMatID.x) * w0
                    + vec2(attrib.n1.w, attrib.uvMatID.y) * hit.bary.x
                    + vec2(attrib.n2.w, attrib.uvMatID.z) * hit.bary.y;

            // LOD 0 everywhere : see GetSkyColor. The paths of a pixel average the texture over its footprint themselves.
            if ((mat.textureFlags & TEX_ALBEDO_BIT) != 0u)
                albedo = textureLod(sampler2D(mat.albedoTex), uv, 0.0).rgb;

            if ((mat.textureFlags & TEX_METALLIC_BIT) != 0u)
                metallic *= textureLod(sampler2D(mat.metallicTex), uv, 0.0).r;

            if ((mat.textureFlags & TEX_ROUGHNESS_BIT) != 0u)
                roughness *= textureLod(sampler2D(mat.roughnessTex), uv, 0.0).r;

            // Modulates the scalar factor (same as lit.frag's emissiveMap * emissive), not overrides it.
            if ((mat.textureFlags & TEX_EMISSIVE_BIT) != 0u)
                emission *= textureLod(sampler2D(mat.emissiveTex), uv, 0.0).rgb;

            if ((mat.textureFlags & TEX_NORMAL_BIT) != 0u)
            {
                vec3 tangent = normalize(attrib.t0.xyz * w0 + attrib.t1.xyz * hit.bary.x + attrib.t2.xyz * hit.bary.y);
                tangent = normalize(tangent - n * dot(tangent, n)); // re-orthogonalize against the interpolated normal
                vec3 bitangent = cross(n, tangent);
                mat3 TBN = mat3(tangent, bitangent, n);

                vec3 sampledNormal = DecodeTangentNormal(textureLod(sampler2D(mat.normalTex), uv, 0.0));
                n = normalize(TBN * sampledNormal);
            }
        }

#ifdef SHARD_EMISSIVE_NEE
        if (prevLobe > 0.0 && EmissiveIsSampled(mat, hitArea))
        {
            // An emitter the previous surface sampled directly : this path found it too, the two estimates share the light
            float lum = dot(emission, vec3(0.2126, 0.7152, 0.0722));
            float cosEmitter = max(abs(dot(ng, rd)), 1e-4);
            float pdfLight = lum * hit.t * hit.t / (uEmissivePower * cosEmitter);
            float pdfBsdf = prevLobe * max(dot(rd, prevNormal), 0.0) / PI;
            radiance += throughput * emission * (pdfBsdf / max(pdfBsdf + pdfLight, 1e-12));
        }
        else
#endif
        radiance += throughput * emission;

        // The world's flat ambient floor, on the surface the ray starts from only (see uAmbientIntensity)
        if (bounce == 0)
            radiance += throughput * albedo * uAmbientIntensity;

        vec3 viewDir = normalize(-rd);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        float specProb = clamp(mix(0.04, 1.0, metallic), 0.04, 0.95);

#ifdef SHARD_EMISSIVE_NEE
        // The emitters, sampled directly for the diffuse lobe (the specular lobe finds them by its own sampling, unweighted)
        {
            vec3 kdDirect = (vec3(1.0) - F0) * (1.0 - metallic);
            radiance += throughput * (kdDirect * albedo / PI) * EmissiveDirect(hitPos, ng, n, 1.0 - specProb);
        }
#endif

        // ---- Next event estimation : sample one light directly, with a shadow ray ----
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

                    // uLightCount factor : we sampled one of uLightCount lights uniformly (pdf = 1 / uLightCount)
                    radiance += throughput * (diffuse + specular) * incomingRadiance * NdotL * float(uLightCount);
                }
            }
        }

        // ---- Continue the path : stochastically pick a lobe and sample a bounce direction ----
        vec3 newDir;
        vec3 lobeWeight;

        if (RandomFloat() < specProb)
        {
            prevLobe = 0.0;     // not a diffuse draw : an emitter it finds counts whole
            vec3 h = SampleGGX(n, roughness);
            newDir = reflect(rd, h);
            if (dot(newDir, n) <= 0.0 || dot(newDir, ng) <= 0.0)
                break; // sampled a direction below the surface - dead end for this path

            vec3 F = FresnelSchlick(max(dot(h, viewDir), 0.0), F0);
            float G = GeometrySmith(n, viewDir, newDir, roughness);
            float NdotV = max(dot(n, viewDir), 0.0);
            float NdotH = max(dot(n, h), 0.0);
            // Standard GGX-half-vector-importance-sampling weight : BRDF * NdotL / pdf, simplified
            // using dot(newDir,h) == dot(viewDir,h) for a mirror reflection about h.
            lobeWeight = (F * G * max(dot(newDir, h), 0.0)) / max(NdotV * NdotH * specProb, 1e-4);
        }
        else
        {
            newDir = CosineSampleHemisphere(n);
            // Around a shading normal that leans away from the geometry the sample can fall under the triangle : lift it
            // back above rather than lose the path
            float aboveGeometry = dot(newDir, ng);
            if (aboveGeometry < 0.05)
                newDir = normalize(newDir + ng * (0.05 - aboveGeometry));

            vec3 kd = (vec3(1.0) - F0) * (1.0 - metallic);
            // Cosine-weighted hemisphere pdf (cosTheta / PI) cancels the BRDF's NdotL / PI exactly,
            // leaving just the diffuse albedo term, divided by the probability of picking this lobe.
            lobeWeight = kd * albedo / max(1.0 - specProb, 1e-4);
            prevLobe = 1.0 - specProb;
            prevNormal = n;
        }

        throughput *= lobeWeight;

        // ---- Russian roulette : bound path length without introducing bias ----
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

// Scales a sample of light down so its luminance does not exceed uFireflyClamp (see there)
vec3 ClampFirefly(vec3 radiance)
{
    if (uFireflyClamp <= 0.0)
        return radiance;

    float lum = dot(radiance, vec3(0.2126, 0.7152, 0.0722));
    return lum > uFireflyClamp ? radiance * (uFireflyClamp / lum) : radiance;
}
