#version 430 core
// Engine-owned textures that change per frame or per volume are bindless handles, so they cost no sampler units (32 on some drivers).
#extension GL_ARB_bindless_texture : enable

#include "../common/light.glsl"
#include "../common/cluster.glsl"
#include "../common/normal_map.glsl"
#include "../common/tonemap.glsl"

layout(std430, binding = 0) buffer LightBuffer {
    Light lights[];
};

layout(std430, binding = 2) readonly buffer LightGrid {
    uvec2 clusters[];
};
layout(std430, binding = 3) readonly buffer LightIndexList {
    uint lightIndices[];
};

uniform uvec2 clusterGridSizeXY;
uniform float clusterScaleZ;
uniform float clusterBiasZ;

// Three programs from one file (Shader::GetVariant): forward, SHARD_GBUFFER (material half, writes the G-buffer), SHARD_DEFERRED_LIGHTING (lighting half, same ShadeSurface).
#ifdef SHARD_GBUFFER
layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gNormals;
layout(location = 2) out vec4 gMaterial;
layout(location = 3) out vec4 gBaked;
#else
out vec4 fragColor;
#endif

#ifdef SHARD_DEFERRED_LIGHTING
vec3 worldPos;
uniform mat4 uView;
uniform mat4 uInvViewProjection;
#define viewMatrix uView
#else
in vec2 texCoord;
in vec2 lightmapUV;
in vec4 color;
in vec3 worldPos;
in mat4 viewMatrix;
in vec3 T;
in vec3 B;
in vec3 N_in;
#endif

vec3 N;

uniform int lightNB;

uniform vec3 camPos;

uniform bool masked;

uniform bool depthPrepass;

uniform int viewMode;
uniform bool showLighting;
uniform bool showShadows;
uniform int excludedLightingTerms;

uniform bool shadowsEnabled;
uniform int dirShadowFilterRadius;
uniform float dirShadowFilterSpread;
uniform float dirShadowNormalBias;
uniform float dirShadowDepthBias;
uniform float dirShadowSlopeBias;
uniform int spotShadowFilterRadius;
uniform float spotShadowFilterSpread;
uniform float spotShadowDepthBias;
uniform float spotShadowMinDepthBias;
uniform int pointShadowFilterSamples;
uniform float pointShadowFilterRadius;
uniform float pointShadowDepthBias;

#define MAX_DIRECTIONAL_LIGHTS 3
#define MAX_CASCADES_PER_LIGHT 6
#define NUM_CASCADES (MAX_DIRECTIONAL_LIGHTS * 3)

uniform int dirCascadeCount;
uniform vec3 dirCascadeColors[MAX_CASCADES_PER_LIGHT];

uniform mat4 dirLightSpaceMatrices[NUM_CASCADES];
uniform float dirCascadeSplits[NUM_CASCADES];

uniform mat4 spotLightSpaceMatrices[10];

uniform float pointLightFarPlanes[10];

#ifdef SHARD_DEFERRED_LIGHTING
#ifdef SHARD_GBUFFER_MS
#define GBUFFER_SAMPLER sampler2DMS
#define GBUFFER_FETCH(tex, pixel) texelFetch(tex, pixel, gl_SampleID)
#define GBUFFER_SIZE(tex) textureSize(tex)
#else
#define GBUFFER_SAMPLER sampler2D
#define GBUFFER_FETCH(tex, pixel) texelFetch(tex, pixel, 0)
#define GBUFFER_SIZE(tex) textureSize(tex, 0)
#endif
layout(binding = 0) uniform GBUFFER_SAMPLER gAlbedoTex;
layout(binding = 1) uniform GBUFFER_SAMPLER gNormalsTex;
layout(binding = 2) uniform GBUFFER_SAMPLER gMaterialTex;
layout(binding = 3) uniform GBUFFER_SAMPLER gBakedTex;
#else
layout(binding = 0) uniform sampler2D albedo;
layout(binding = 1) uniform sampler2D metallicMap;
layout(binding = 2) uniform sampler2D roughnessMap;
layout(binding = 3) uniform sampler2D normalMap;
#endif

layout(binding = 4) uniform samplerCube ibl_irradianceMap;
layout(binding = 5) uniform samplerCube ibl_prefilteredEnvMap;

// MUST equal ProbeManager::kMaxProbeVolumes. DDGI_PickVolume takes the smallest volume containing the point, -1 falls back to IBL diffuse.
#define MAX_PROBE_VOLUMES 2

#define DDGI_MIN_TAP_WEIGHT 0.004

uniform uvec2 ddgi_irradianceAtlasHandle[MAX_PROBE_VOLUMES];
uniform uvec2 ddgi_distanceAtlasHandle[MAX_PROBE_VOLUMES];

// ---- Sampler units : what still takes one (11 of the 32 some drivers allow a fragment stage) ----
//   0-3   the material's textures (forward) or the G-buffer (deferred lighting)
//   4-5   the sky's irradiance and prefiltered environment
//   6     the reflection probe
//   7-9   the shadow maps : one array per kind of light, a light's map is a layer of it
//   10    the emission map (forward) or the G-buffer's depth (deferred lighting)

layout(binding = 6) uniform samplerCube rp_prefilter;
uniform bool rp_enabled;
uniform vec3 rp_center;
uniform vec3 rp_halfExtent;
uniform int rp_shape;
uniform vec3 rp_params;

uniform bool ddgi_enabled;
uniform int  ddgi_volumeCount;
uniform vec3 ddgi_gridOrigin[MAX_PROBE_VOLUMES];
uniform vec3 ddgi_gridSpacing[MAX_PROBE_VOLUMES];
uniform vec3 ddgi_probeCounts[MAX_PROBE_VOLUMES];
uniform int  ddgi_tileSize[MAX_PROBE_VOLUMES];
uniform int  ddgi_atlasProbesPerRow[MAX_PROBE_VOLUMES];
uniform int  ddgi_atlasSize[MAX_PROBE_VOLUMES];

// One named SSBO block per volume (14, 15) because GLSL 4.3 cannot portably put an unsized array in an arrayed block. .w = active flag, .xyz = relocation offset.
layout(std430, binding = 14) readonly buffer DDGIProbeState0 { vec4 ddgi_probeState0[]; };
layout(std430, binding = 15) readonly buffer DDGIProbeState1 { vec4 ddgi_probeState1[]; };

vec4 DDGI_ProbeState(int v, int probeIndex) {
    if (v == 0) return ddgi_probeState0[probeIndex];
    return ddgi_probeState1[probeIndex];
}

// Layers: sun k cascade c is layer k * dirCascadeCount + c of the first array, spot s is layer s of the second, point p is cube p of the third.
layout(binding = 7) uniform sampler2DArrayShadow dirShadowMapArray;
layout(binding = 8) uniform sampler2DArray spotShadowMapArray;
layout(binding = 9) uniform samplerCubeArray pointShadowMapArray;

#ifdef SHARD_DEFERRED_LIGHTING
layout(binding = 10) uniform GBUFFER_SAMPLER gDepthTex;
#else
layout(binding = 10) uniform sampler2D emissiveMap;
#endif

const float PI = 3.141592653589793;
uniform float metallic;
uniform float roughness;
uniform vec3 ambientColor;

// glTF-style emissiveMap.rgb * emissive. Default map is white, so a map alone shows nothing. Reset to 0 before every draw by GLRendererAPI::BindLevelState or it leaks between materials.
uniform vec3 emissive;

uniform bool ssaoEnabled;
uniform float ssaoIntensity;
uniform uvec2 ssaoTextureHandle;

vec3 SampleSSAO(vec3 ambient) {
    if (!ssaoEnabled || ssaoTextureHandle == uvec2(0))
        return ambient;

    // Build sampler2D(handle) at the read, never store it in a variable. The Vulkan build (shaderc) turns it into a bindless array read.
    float ao = texture(sampler2D(ssaoTextureHandle), gl_FragCoord.xy / vec2(textureSize(sampler2D(ssaoTextureHandle), 0))).r;
    return ambient * clamp(mix(1.0, ao, ssaoIntensity), 0.0, 1.0);
}

uniform bool ssrEnabled;
uniform float ssrIntensity;
uniform float ssrMaxRoughness;
uniform float ssrRoughnessBlur;
uniform float ssrMaxLod;
uniform uvec2 ssrHitHandle;
uniform uvec2 ssrColorHandle;

vec4 SampleSSR(float roughnessValue)
{
    if (!ssrEnabled || ssrHitHandle == uvec2(0) || ssrColorHandle == uvec2(0))
        return vec4(0.0);

    float roughnessFade = 1.0 - smoothstep(0.6 * ssrMaxRoughness, ssrMaxRoughness, roughnessValue);
    if (roughnessFade <= 0.0)
        return vec4(0.0);

    vec4 hit = textureLod(sampler2D(ssrHitHandle), gl_FragCoord.xy / vec2(textureSize(sampler2D(ssrHitHandle), 0)), 0.0);
    if (hit.w <= 0.0)
        return vec4(0.0);

    float lobePixels = 2.0 * hit.z * roughnessValue * roughnessValue * ssrRoughnessBlur;
    float lod = clamp(log2(max(lobePixels, 1.0)), 0.0, ssrMaxLod);

    vec3 color = textureLod(sampler2D(ssrColorHandle), hit.xy, lod).rgb;
    return vec4(color, hit.w * roughnessFade * clamp(ssrIntensity, 0.0, 1.0));
}

vec3 sampleOffsetDirections[20] = vec3[]
(
   vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1), 
   vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
   vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
   vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
   vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
);

#ifdef SHARD_DEFERRED_LIGHTING
bool useEnvReflections;
#else
uniform bool useEnvReflections;
#endif

uniform bool outputDisplayColors;

uniform bool lightmapEnabled;
uniform uvec2 lightmapHandle;
uniform vec4 lightmapScaleOffset;

int selectCascade(int lightIndex){
    vec4 viewPos = viewMatrix * vec4(worldPos, 1.0);
    float depth = abs(viewPos.z);

    int base = lightIndex * dirCascadeCount;

    for (int i = 0; i < dirCascadeCount; ++i) {
        if (depth < dirCascadeSplits[base + i])
            return base + i;
    }

    return base + dirCascadeCount - 1;
}

// worldNormal must be the geometric normal. A normal-mapped one makes the bias flicker per texel of the cloth.
float ShadowCalculationDir(int cascade, mat4 lightSpaceMatrix, vec3 lightDir, vec3 worldPos, vec3 worldNormal){
    mat3 M = mat3(lightSpaceMatrix);
    float scaleX = length(vec3(M[0][0], M[1][0], M[2][0]));
    float scaleZ = length(vec3(M[0][2], M[1][2], M[2][2]));
    vec2 mapSize = vec2(textureSize(dirShadowMapArray, 0).xy);
    float texelWorld = 2.0 / (scaleX * mapSize.x);
    float zPerWorld = 0.5 * scaleZ;

    vec3 n = normalize(worldNormal);
    float NdotL = dot(n, lightDir);
    if (NdotL < 0.0) { n = -n; NdotL = -NdotL; }
    float sinTheta = sqrt(max(1.0 - NdotL * NdotL, 0.0));

    vec3 biasedPos = worldPos + n * (texelWorld * dirShadowNormalBias * sinTheta);

    vec4 fragLS = lightSpaceMatrix * vec4(biasedPos, 1.0);
    vec3 proj = fragLS.xyz / fragLS.w;

    proj = proj * 0.5 + 0.5;

    if (proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0 ||
        proj.z > 1.0)
        return 1.0;

    // Receiver-plane bias: each PCF tap is compared with the surface depth at that tap. A constant bias gives acne or detached shadows on slants. Slope comes from M^-T n.
    vec3 nLS = transpose(inverse(M)) * n;
    vec2 depthSlope = vec2(0.0);
    if (abs(nLS.z) > 1e-4)
        depthSlope = clamp(-nLS.xy / nLS.z, vec2(-8.0), vec2(8.0)) * dirShadowSlopeBias;

    float constBias = texelWorld * dirShadowDepthBias * zPerWorld;

    vec2 texelSize = 1.0 / mapSize;

    float visibility = 0.0;

    int radius = clamp(dirShadowFilterRadius, 0, 4);
    for (int x = -radius; x <= radius; ++x) {
        for (int y = -radius; y <= radius; ++y) {
            vec2 offset = vec2(x, y) * texelSize * dirShadowFilterSpread;
            float planeDepth = dot(depthSlope, offset);
            visibility += texture(dirShadowMapArray, vec4(proj.xy + offset, float(cascade), proj.z + planeDepth - constBias));
        }
    }

    float taps = float(2 * radius + 1);
    return visibility / (taps * taps);
}

float ShadowCalculationSpot(int slot, vec3 lightDir, mat4 lightSpaceMatrix, vec3 worldNormal) {
    vec4 fragPosLightSpace = lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0 || projCoords.z < 0.0)
        return 1.0;

    float bias = max(spotShadowDepthBias * (1.0 - dot(normalize(worldNormal), lightDir)), spotShadowMinDepthBias);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(spotShadowMapArray, 0).xy);

    int radius = clamp(spotShadowFilterRadius, 0, 4);
    for (int x = -radius; x <= radius; ++x) {
        for (int y = -radius; y <= radius; ++y) {
            float closestDepth = texture(spotShadowMapArray, vec3(projCoords.xy + vec2(x, y) * texelSize * spotShadowFilterSpread, float(slot))).r;
            shadow += (projCoords.z - bias) < closestDepth ? 1.0 : 0.0;
        }
    }

    float taps = float(2 * radius + 1);
    return shadow / (taps * taps);
}

// Must match the flattening in cluster_build.comp and cluster_light_cull.comp: x + y*gridWidth + z*gridWidth*gridHeight.
uint GetClusterIndex(vec3 fragWorldPos){
    vec4 viewPosForCluster = viewMatrix * vec4(fragWorldPos, 1.0);
    float viewZ = max(-viewPosForCluster.z, 1e-4);

    uint zSlice = uint(clamp(log2(viewZ) * clusterScaleZ + clusterBiasZ, 0.0, float(CLUSTER_Z_SLICES - 1)));

    uvec2 tileXY = uvec2(gl_FragCoord.xy) / uint(CLUSTER_TILE_PX);
    tileXY = min(tileXY, clusterGridSizeXY - uvec2(1, 1));

    return tileXY.x + tileXY.y * clusterGridSizeXY.x + zSlice * (clusterGridSizeXY.x * clusterGridSizeXY.y);
}

float ShadowCalculationPoint(int index,vec3 lightPos,vec3 fragPos,float farPlane,vec3 worldNormal){
    vec3 fragToLight = fragPos - lightPos;
    float currentDepth = length(fragToLight);

    float shadow = 0.0;
    float bias   = pointShadowDepthBias;
    int samples  = clamp(pointShadowFilterSamples, 1, 20);
    float viewDistance = length(camPos - fragPos);
    float diskRadius = pointShadowFilterRadius;
    for(int i = 0; i < samples; ++i)
    {
        vec3 tapOffset = samples > 1 ? sampleOffsetDirections[i] * diskRadius : vec3(0.0);
        float closestDepth = texture(pointShadowMapArray, vec4(fragToLight + tapOffset, index)).r;
        closestDepth *= farPlane;
        if(currentDepth - bias > closestDepth)
            shadow += 1.0;
    }
    shadow /= float(samples);

    return 1.0 - shadow;
}

vec3 IBL_Diffuse(vec3 N, vec3 albedo, float metallic) {
    vec3 kS = mix(vec3(0.04), albedo, metallic);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = texture(ibl_irradianceMap, N).rgb;
    return irradiance * albedo * kD;
}

#include "../common/octahedral.glsl"
#include "../common/ddgi_atlas.glsl"
#include "../common/ddgi_visibility.glsl"

// Constant volume index, so every handle is uniform. textureLod 0 because the atlases have no mips and the select is non-uniform control flow.
vec3 DDGI_SampleIrradianceAtlas(int v, vec2 uv) {
    vec3 value = vec3(0.0);
    for (int i = 0; i < MAX_PROBE_VOLUMES; i++)
        if (i == v)
            value = textureLod(sampler2D(ddgi_irradianceAtlasHandle[i]), uv, 0.0).rgb;
    return value;
}
vec2 DDGI_SampleDistanceAtlas(int v, vec2 uv) {
    vec2 value = vec2(0.0);
    for (int i = 0; i < MAX_PROBE_VOLUMES; i++)
        if (i == v)
            value = textureLod(sampler2D(ddgi_distanceAtlasHandle[i]), uv, 0.0).rg;
    return value;
}

vec3 DDGI_SampleProbe(int v, int probeIndex, vec3 N) {
    vec2 uv = DDGI_AtlasUV(probeIndex, N, ddgi_tileSize[v], ddgi_atlasProbesPerRow[v], ddgi_atlasSize[v]);
    return DDGI_SampleIrradianceAtlas(v, uv);
}

vec2 DDGI_SampleDistance(int v, int probeIndex, vec3 dir) {
    vec2 uv = DDGI_AtlasUV(probeIndex, dir, ddgi_tileSize[v], ddgi_atlasProbesPerRow[v], ddgi_atlasSize[v]);
    return DDGI_SampleDistanceAtlas(v, uv);
}

// Box is the volume's full halfExtent, not the probe lattice AABB. The lattice is inset by half a cell, so testing it rejected walls, floor and ceiling (no GI there).
int DDGI_PickVolume(vec3 p) {
    int best = -1;
    float bestCell = 1e30;
    for (int v = 0; v < ddgi_volumeCount; v++) {
        vec3 gmin = ddgi_gridOrigin[v] - ddgi_gridSpacing[v] * 0.5;
        vec3 gmax = ddgi_gridOrigin[v] + ddgi_gridSpacing[v] * (max(ddgi_probeCounts[v] - 1.0, vec3(0.0)) + 0.5);
        if (all(greaterThanEqual(p, gmin)) && all(lessThanEqual(p, gmax))) {
            float cell = ddgi_gridSpacing[v].x * ddgi_gridSpacing[v].y * ddgi_gridSpacing[v].z;
            if (cell < bestCell) { bestCell = cell; best = v; }
        }
    }
    return best;
}

void DDGI_SampleVolume(int v, vec3 worldPos, vec3 N, vec3 specDir, out vec3 outDiffuse, out vec3 outSpecular) {
    vec3 biasedPos = worldPos + N * (0.25 * min(min(ddgi_gridSpacing[v].x, ddgi_gridSpacing[v].y), ddgi_gridSpacing[v].z));

    vec3 gridPos = (biasedPos - ddgi_gridOrigin[v]) / max(ddgi_gridSpacing[v], vec3(1e-4));

    // Quadratic B-spline over 3x3x3 probes around the nearest node. Trilinear is only C0 (creases), smoothstep easing gives plateaus. Weights (0.5(.5-d)^2, .75-d^2, 0.5(.5+d)^2).
    vec3 nearest = floor(gridPos + 0.5);
    vec3 d = gridPos - nearest;
    vec3 tapWeights[3] = vec3[](0.5 * (0.5 - d) * (0.5 - d), 0.75 - d * d, 0.5 * (0.5 + d) * (0.5 + d));

    vec3 maxCoord = max(ddgi_probeCounts[v] - 1.0, 0.0);
    int rowStride = int(ddgi_probeCounts[v].x);
    int layerStride = int(ddgi_probeCounts[v].x) * int(ddgi_probeCounts[v].y);

    vec3 irradiance = vec3(0.0);
    vec3 specRadiance = vec3(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < 27; i++) {
        ivec3 tap = ivec3(i % 3, (i / 3) % 3, i / 9);
        float kernelWeight = tapWeights[tap.x].x * tapWeights[tap.y].y * tapWeights[tap.z].z;
        // Outermost taps weigh a fraction of a percent. Skipping them (rest renormalized) is under one 8-bit level (PSNR 67 dB on Sponza) and about 20% faster.
        if (kernelWeight < DDGI_MIN_TAP_WEIGHT)
            continue;

        vec3 probeCoord = clamp(nearest + vec3(tap - 1), vec3(0.0), maxCoord);
        int probeIndex = int(probeCoord.x) + int(probeCoord.y) * rowStride + int(probeCoord.z) * layerStride;

        vec4 state = DDGI_ProbeState(v, probeIndex);
        if (state.w <= 0.0)
            continue;

        vec3 probeWorldPos = ddgi_gridOrigin[v] + probeCoord * ddgi_gridSpacing[v] + state.xyz;
        vec3 toPoint = biasedPos - probeWorldPos;
        float distToPoint = length(toPoint);
        vec3 dirToPoint = toPoint / max(distToPoint, 1e-5);

        float visWeight = DDGI_VisibilityWeight(DDGI_SampleDistance(v, probeIndex, dirToPoint), distToPoint);

        // Smooth back-face weight, measured from the unbiased point. Do not fade taps behind an embedded probe instead: that flips as the point crosses the probe's plane and shows a hard seam. Size the volume to fit the room.
        vec3 toProbe = probeWorldPos - worldPos;
        float facing = dot(toProbe, N) / max(length(toProbe), 1e-5);
        float backfaceWeight = (facing * 0.5 + 0.5) * (facing * 0.5 + 0.5);

        float weight = kernelWeight * visWeight * backfaceWeight;
        if (weight <= 0.0)
            continue;

        irradiance   += DDGI_SampleProbe(v, probeIndex, N) * weight;
        specRadiance += DDGI_SampleProbe(v, probeIndex, specDir) * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0) {
        irradiance /= totalWeight;
        specRadiance /= totalWeight;
    }

    outDiffuse = irradiance;
    outSpecular = specRadiance;
}

vec3 DDGI_Diffuse(vec3 irradiance, vec3 albedo, float metallic) {
    vec3 kS = mix(vec3(0.04), albedo, metallic);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    return irradiance * albedo * kD;
}

vec3 EnvBRDFApprox(vec3 F0, float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}


vec3 DDGI_SpecularDirection(vec3 N, vec3 V, float roughness) {
    vec3 R = reflect(-V, N);
    vec3 dir = mix(R, N, roughness * roughness);
    // R and N antiparallel gives a zero-length blend, normalizing it is NaN. Fall back to N.
    float len = length(dir);
    return len > 1e-4 ? dir / len : N;
}

vec3 DDGI_Specular(vec3 radiance, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    return radiance * EnvBRDFApprox(F0, roughness, max(dot(N, V), 0.0));
}

vec3 LightmapSpecular(vec3 irradiance, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    return irradiance * EnvBRDFApprox(F0, roughness, max(dot(N, V), 0.0));
}

float RP_Weight(vec3 P) {
    vec3 d = P - rp_center;
    vec3 inside = rp_halfExtent - abs(d);
    float depth = min(inside.x, min(inside.y, inside.z));
    float minXZ = min(rp_halfExtent.x, rp_halfExtent.z);

    if (rp_shape == 1) {
        depth = (1.0 - length(d / rp_halfExtent)) * min(minXZ, rp_halfExtent.y);
    } else if (rp_shape == 2) {
        depth = min((1.0 - length(d.xz / rp_halfExtent.xz)) * minXZ, inside.y);
    } else if (rp_shape >= 3) {
        float narrow = 1.0 - (d.y + rp_halfExtent.y) / (2.0 * rp_halfExtent.y);
        float radial = rp_shape == 3 ? length(d.xz / rp_halfExtent.xz) : max(abs(d.x) / rp_halfExtent.x, abs(d.z) / rp_halfExtent.z);
        depth = min((narrow - radial) * minXZ, inside.y);
    }
    return clamp(depth / max(rp_params.y, 1e-3), 0.0, 1.0);
}

vec3 RP_BoxProject(vec3 P, vec3 R) {
    if (rp_shape == 1) {
        vec3 o = (P - rp_center) / rp_halfExtent;
        vec3 r = R / rp_halfExtent;
        float a = dot(r, r);
        float b = dot(o, r);
        float disc = max(b * b - a * (dot(o, o) - 1.0), 0.0);
        float t = (-b + sqrt(disc)) / max(a, 1e-8);
        return normalize(P + R * max(t, 0.0) - rp_center);
    }
    vec3 safeR = mix(R, vec3(1e-5), lessThan(abs(R), vec3(1e-5)));
    vec3 toMax = (rp_center + rp_halfExtent - P) / safeR;
    vec3 toMin = (rp_center - rp_halfExtent - P) / safeR;
    float t = min(min(max(toMax.x, toMin.x), max(toMax.y, toMin.y)), max(toMax.z, toMin.z));
    return normalize(P + R * max(t, 0.0) - rp_center);
}

vec3 RP_Specular(vec3 P, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, out float weight) {
    weight = RP_Weight(P);
    if (weight <= 0.0)
        return vec3(0.0);

    vec3 R = reflect(-V, N);
    vec3 dir = rp_params.z > 0.5 ? RP_BoxProject(P, R) : R;

    const float MAX_REFLECTION_LOD = 5.0;
    vec3 prefiltered = textureLod(rp_prefilter, dir, roughness * MAX_REFLECTION_LOD).rgb * rp_params.x;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    return prefiltered * EnvBRDFApprox(F0, roughness, max(dot(N, V), 0.0));
}

vec3 IBL_Specular(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness){
    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NdotV = max(dot(N, V), 0.0);

    const float MAX_REFLECTION_LOD = 5.0;
    vec3 prefilteredColor = textureLod(ibl_prefilteredEnvMap, R, roughness * MAX_REFLECTION_LOD).rgb;

    return prefilteredColor * EnvBRDFApprox(F0, roughness, NdotV);
}

vec3 ComputeLightDisney(Light light, vec3 L, vec3 V, vec3 N, vec3 baseColor, float roughness, float metallic, float visibility, float attenuation) {
    vec3 Nn = normalize(N);
    vec3 Ln = normalize(L);
    vec3 Vn = normalize(V);
    vec3 H = normalize(Vn + Ln);

    float NdotL = max(dot(Nn, Ln), 0.0);
    float NdotV = max(dot(Nn, Vn), 0.0);
    float NdotH = max(dot(Nn, H), 0.0);
    float VdotH = max(dot(Vn, H), 0.0);

    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    // Roughness 0 makes GGX 0/0 at the highlight (NaN, once hidden by an 8-bit clamp). Clamp to the lowest finite value.
    roughness = max(roughness, 0.045);
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom);

    float k = (roughness + 1.0);
    k = (k * k) / 8.0;
    float G_V = NdotV / (NdotV * (1.0 - k) + k);
    float G_L = NdotL / (NdotL * (1.0 - k) + k);
    float G = G_V * G_L;

    vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    vec3 diffuse = kD * baseColor / 3.14159265359;

    vec3 Lo = (diffuse + specular) * NdotL * light.color.xyz * light.intensity * visibility * attenuation;

    return Lo;
}

// Narrow on purpose. At 0..0.3 it darkened every grazed surface, about a third of Sponza's direct light against the path tracer (scripts/compare_reference.py).
float TerminatorSoften(vec3 geoNormal, vec3 L){
    return smoothstep(0.0, 0.05, dot(normalize(geoNormal), L));
}

vec3 TerminatorNormal(vec3 geoNormal, vec3 mappedNormal, vec3 L){
    float t = smoothstep(0.0, 0.5, dot(normalize(geoNormal), L));
    return normalize(mix(normalize(geoNormal), mappedNormal, t));
}

vec4 ShadeSurface(vec4 baseColor, float metallicValue, float roughnessValue, vec3 worldNormal, bool hasLightmap, vec3 bakedIrradiance, vec3 emission, vec3 V)
{
    vec3 result = vec3(0.0);

    if (viewMode == 10) {
        for (int i = 0; i < lightNB; ++i) {
            Light l = lights[i];
            if (l.type != 0 || l.castShadow != 1 || l.shadowIndex < 0)
                continue;

            int cascadeIdx = selectCascade(l.shadowIndex);
            vec3 L = -normalize(l.direction.xyz);
            float visibility = ShadowCalculationDir(cascadeIdx, dirLightSpaceMatrices[cascadeIdx], L, worldPos, normalize(N));

            vec4 ls = dirLightSpaceMatrices[cascadeIdx] * vec4(worldPos, 1.0);
            vec3 proj = ls.xyz / ls.w * 0.5 + 0.5;
            if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0)
                return vec4(1.0);

            return vec4(dirCascadeColors[cascadeIdx - l.shadowIndex * dirCascadeCount] * (0.15 + 0.85 * visibility), 1.0);
        }
        return vec4(1.0, 0.0, 1.0, 1.0);
    }

    if (!showLighting) {
        float facing = 0.25 + 0.75 * max(dot(worldNormal, V), 0.0);
        return vec4(baseColor.rgb * facing + emission, baseColor.a);
    }

    for (int i = 0; i < lightNB; ++i) {
        Light l = lights[i];
        if (l.type != 0)
            continue;

        // Static lights are already in the lightmap, lighting them again would count them twice.
        if (hasLightmap && l.bakeMode == LIGHT_BAKE_STATIC)
            continue;

        vec3 L = -normalize(l.direction.xyz);
        float visibility = 1.0;

        if (showShadows && shadowsEnabled && l.castShadow == 1 && l.shadowIndex >= 0) {
            int cascadeIdx = selectCascade(l.shadowIndex);
            visibility = ShadowCalculationDir(cascadeIdx, dirLightSpaceMatrices[cascadeIdx], L, worldPos, normalize(N));
        }

        result += ComputeLightDisney(l, L, V, TerminatorNormal(N, worldNormal, L), baseColor.rgb, roughnessValue, metallicValue, visibility * TerminatorSoften(N, L), 1.0);
    }

    uint clusterIndex = GetClusterIndex(worldPos);
    uvec2 clusterEntry = clusters[clusterIndex];
    uint clusterLightOffset = clusterEntry.x;
    uint clusterLightCount = min(clusterEntry.y, uint(MAX_LIGHTS_PER_CLUSTER));

    for (uint k = 0; k < clusterLightCount; ++k) {
        Light l = lights[lightIndices[clusterLightOffset + k]];

        if (hasLightmap && l.bakeMode == LIGHT_BAKE_STATIC)
            continue;

        float visibility = 1.0;
        float attenuation = 1.0;
        vec3 L = vec3(0.0);

        if (l.type == 1) {
            vec3 toLight = l.position.xyz - worldPos;

            float dist = length(toLight);

            L = normalize(toLight);

            attenuation = 1.0 / (dist * dist);

            float falloff = clamp(1.0 - dist / l.radius, 0.0, 1.0);
            attenuation *= falloff * falloff;

            if (showShadows && shadowsEnabled && l.castShadow == 1 && l.shadowIndex >= 0){
                visibility = ShadowCalculationPoint(l.shadowIndex, l.position.xyz, worldPos, pointLightFarPlanes[l.shadowIndex], worldNormal);
            }
        }
        else if (l.type == 2) {
            vec3 toFrag = worldPos - l.position.xyz;

            float dist = length(toFrag);

            L = normalize(l.position.xyz - worldPos);

            attenuation = 1.0 / (dist * dist);

            float falloff = clamp(1.0 - dist / l.radius, 0.0, 1.0);
            attenuation *= falloff * falloff;

            float cosTheta = dot(L, normalize(-l.direction.xyz));

            float epsilon = l.innerCutoff - l.outerCutoff;
            float spotIntensity = clamp((cosTheta - l.outerCutoff) / epsilon, 0.0, 1.0);

            attenuation *= spotIntensity;

            if (showShadows && shadowsEnabled && l.castShadow == 1 && l.shadowIndex >= 0) {
                visibility = ShadowCalculationSpot(l.shadowIndex, L, spotLightSpaceMatrices[l.shadowIndex], worldNormal);
            }
        }

        result += ComputeLightDisney(l, L, V, TerminatorNormal(N, worldNormal, L), baseColor.rgb, roughnessValue, metallicValue, visibility * TerminatorSoften(N, L), attenuation);
    }

    // GI lookups use a mostly geometric normal (30% normal map). Probes hold no detail at weave scale, the full normal map only makes GI flicker per texel.
    vec3 giNormal = normalize(mix(normalize(N), worldNormal, 0.3));

    vec3 ambientDiffuse = vec3(0.0);
    vec3 specularIBL = vec3(0.0);

    float skySpecularOcclusion = 1.0;
    if (hasLightmap && useEnvReflections) {
        float bakedLum = dot(bakedIrradiance, vec3(0.2126, 0.7152, 0.0722));
        float skyLum = dot(texture(ibl_irradianceMap, giNormal).rgb, vec3(0.2126, 0.7152, 0.0722));
        skySpecularOcclusion = clamp(bakedLum / max(skyLum, 1e-3), 0.0, 1.0);
        skySpecularOcclusion *= skySpecularOcclusion;
    }

    int ddgiVolume = ddgi_enabled ? DDGI_PickVolume(worldPos) : -1;

    if (ddgiVolume >= 0) {
        vec3 probeIrradiance;
        vec3 probeRadiance;
        DDGI_SampleVolume(ddgiVolume, worldPos, giNormal,
                          DDGI_SpecularDirection(giNormal, V, roughnessValue),
                          probeIrradiance, probeRadiance);

        ambientDiffuse = DDGI_Diffuse(hasLightmap ? bakedIrradiance : probeIrradiance, baseColor.rgb, metallicValue);

        vec3 probeSpecular = DDGI_Specular(probeRadiance, giNormal, V, baseColor.rgb, metallicValue, roughnessValue);
        if (useEnvReflections)
            specularIBL = mix(IBL_Specular(giNormal, V, baseColor.rgb, metallicValue, roughnessValue) * skySpecularOcclusion, probeSpecular, roughnessValue);
        else
            specularIBL = probeSpecular;
    }
    else if (useEnvReflections) {
        ambientDiffuse = hasLightmap ? DDGI_Diffuse(bakedIrradiance, baseColor.rgb, metallicValue)
                                     : IBL_Diffuse(giNormal, baseColor.rgb, metallicValue);
        specularIBL = IBL_Specular(giNormal, V, baseColor.rgb, metallicValue, roughnessValue) * skySpecularOcclusion;
    }
    else if (hasLightmap) {
        ambientDiffuse = DDGI_Diffuse(bakedIrradiance, baseColor.rgb, metallicValue);
        specularIBL = LightmapSpecular(bakedIrradiance, giNormal, V, baseColor.rgb, metallicValue, roughnessValue);
    }

    if (useEnvReflections && rp_enabled) {
        float rpWeight;
        vec3 rpSpecular = RP_Specular(worldPos, giNormal, V, baseColor.rgb, metallicValue, roughnessValue, rpWeight);
        specularIBL = mix(specularIBL, rpSpecular, rpWeight);
    }

    if (useEnvReflections && ssrEnabled) {
        vec4 ssr = SampleSSR(roughnessValue);
        if (ssr.a > 0.0) {
            vec3 ssrF0 = mix(vec3(0.04), baseColor.rgb, metallicValue);
            vec3 ssrSpecular = ssr.rgb * EnvBRDFApprox(ssrF0, roughnessValue, max(dot(giNormal, V), 0.0));
            specularIBL = mix(specularIBL, ssrSpecular, ssr.a);
        }
    }

    ambientDiffuse += baseColor.rgb * ambientColor;

    if (viewMode == 7) {
        return vec4(ambientDiffuse + specularIBL, 1.0);
    }
    if (viewMode == 8) {
        float ao = 1.0;
        if (ssaoEnabled && ssaoTextureHandle != uvec2(0)) {
            ao = texture(sampler2D(ssaoTextureHandle), gl_FragCoord.xy / vec2(textureSize(sampler2D(ssaoTextureHandle), 0))).r;
            ao = clamp(mix(1.0, ao, ssaoIntensity), 0.0, 1.0);
        }
        return vec4(vec3(ao), 1.0);
    }
    if (!hasLightmap)
        ambientDiffuse = SampleSSAO(ambientDiffuse);

    if ((excludedLightingTerms & LIGHTING_TERM_DIRECT) != 0)
        result = vec3(0.0);
    if ((excludedLightingTerms & LIGHTING_TERM_INDIRECT_DIFFUSE) != 0)
        ambientDiffuse = vec3(0.0);
    if ((excludedLightingTerms & LIGHTING_TERM_INDIRECT_SPECULAR) != 0)
        specularIBL = vec3(0.0);
    if ((excludedLightingTerms & LIGHTING_TERM_EMISSION) != 0)
        emission = vec3(0.0);

    vec3 lighting = result + ambientDiffuse + specularIBL + emission;
    // Drop non-finite or huge samples here, while the pixel is a single sample, so the multisample resolve does not spread them.
    lighting = clamp(lighting, vec3(0.0), vec3(60000.0));
    if (any(isnan(lighting)))
        lighting = vec3(0.0);
    if (outputDisplayColors)
        lighting = DisplayTransform(lighting, TONEMAP_ACES, 1.0);
    return vec4(lighting, baseColor.a);
}

bool IsVertexDebugView() {
    return viewMode == 2 || viewMode == 3 || viewMode == 6 || viewMode == 9;
}

bool SurfaceDebugColor(vec4 baseColor, vec3 emission, vec3 worldNormal, out vec4 color) {
    color = vec4(0.0);
    if (viewMode == 1) { color = vec4(baseColor.rgb + emission, baseColor.a); return true; }
    if (viewMode == 4) { color = vec4(worldNormal * 0.5 + 0.5, 1.0); return true; }
    if (viewMode == 5) {
        float d = length(camPos - worldPos);
        color = vec4(vec3(1.0 - d / (d + 20.0)), 1.0);
        return true;
    }
    return false;
}

#ifndef SHARD_DEFERRED_LIGHTING
vec3 VertexDebugColor(bool hasLightmap) {
    if (viewMode == 2) return vec3(0.85, 0.9, 1.0);
    if (viewMode == 3) return vec3(0.02, 0.02, 0.03);
    if (viewMode == 6) return vec3(fract(texCoord), 0.0);

    vec2 lmUV = hasLightmap ? lightmapUV * lightmapScaleOffset.xy + lightmapScaleOffset.zw : lightmapUV;
    vec2 cell = floor(lmUV * 64.0);
    float checker = mod(cell.x + cell.y, 2.0);
    vec3 col = vec3(fract(lmUV), 0.35) * mix(0.55, 1.0, checker);
    return hasLightmap ? col : col * 0.4;
}
#endif

#ifdef SHARD_DEFERRED_LIGHTING

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);

    float depth = GBUFFER_FETCH(gDepthTex, pixel).r;
    if (depth >= 1.0)
        discard;

    vec4 clip = vec4(gl_FragCoord.xy / vec2(GBUFFER_SIZE(gDepthTex)) * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProjection * clip;
    worldPos = world.xyz / world.w;

    vec4 albedoRaw = GBUFFER_FETCH(gAlbedoTex, pixel);
    vec4 normals = GBUFFER_FETCH(gNormalsTex, pixel);
    vec4 material = GBUFFER_FETCH(gMaterialTex, pixel);
    vec4 baked = GBUFFER_FETCH(gBakedTex, pixel);

    int flags = int(albedoRaw.a * 3.0 + 0.5);
    useEnvReflections = (flags & 2) != 0;

    vec3 worldNormal = OctDecode(normals.xy);
    N = OctDecode(normals.zw);
    vec3 V = normalize(camPos - worldPos);
    gl_FragDepth = depth;

    if (IsVertexDebugView()) {
        fragColor = vec4(albedoRaw.rgb, 1.0);
        return;
    }
    vec4 debugColor;
    if (SurfaceDebugColor(vec4(albedoRaw.rgb, 1.0), material.rgb, worldNormal, debugColor)) {
        fragColor = debugColor;
        return;
    }

    fragColor = ShadeSurface(vec4(albedoRaw.rgb, 1.0), material.a, baked.a, worldNormal, (flags & 1) != 0, baked.rgb, material.rgb, V);
}

#else

void main() {
    N = gl_FrontFacing ? N_in : -N_in;

    if (depthPrepass) {
#ifndef SHARD_GBUFFER
        fragColor = vec4(0.0);
#endif
        return;
    }

    vec4 baseColor = texture(albedo, texCoord);

    if (masked && baseColor.a < 0.5)
        discard;

    float metallicValue = texture(metallicMap, texCoord).r * metallic;
    float roughnessValue = texture(roughnessMap, texCoord).r * roughness;

    vec3 normalFromMap = DecodeTangentNormal(texture(normalMap, texCoord));

    mat3 TBN = mat3(T, B, N);
    vec3 worldNormal = normalize(TBN * normalFromMap);

    vec3 V = normalize(camPos - worldPos);

    bool hasLightmap = lightmapEnabled && lightmapHandle != uvec2(0);
    vec3 bakedIrradiance = vec3(0.0);
    if (hasLightmap)
    {
        bakedIrradiance = textureLod(sampler2D(lightmapHandle), lightmapUV * lightmapScaleOffset.xy + lightmapScaleOffset.zw, 0.0).rgb;
    }

    vec3 emission = texture(emissiveMap, texCoord).rgb * emissive;

#ifdef SHARD_GBUFFER
    gAlbedo = vec4(baseColor.rgb, ((hasLightmap ? 1.0 : 0.0) + (useEnvReflections ? 2.0 : 0.0)) / 3.0);
    gNormals = vec4(OctEncode(worldNormal), OctEncode(N));
    gMaterial = vec4(emission, metallicValue);
    gBaked = vec4(bakedIrradiance, roughnessValue);
    if (IsVertexDebugView())
        gAlbedo.rgb = VertexDebugColor(hasLightmap);
    return;
#else
    if (IsVertexDebugView()) {
        fragColor = vec4(VertexDebugColor(hasLightmap), 1.0);
        return;
    }
    vec4 debugColor;
    if (SurfaceDebugColor(baseColor, emission, worldNormal, debugColor)) {
        fragColor = debugColor;
        return;
    }

    fragColor = ShadeSurface(baseColor, metallicValue, roughnessValue, worldNormal, hasLightmap, bakedIrradiance, emission, V);
#endif
}

#endif
