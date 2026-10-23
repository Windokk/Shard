#version 430 core
// For the sampler2D(uvec2) reconstructions below : the textures the engine owns and that change from frame to frame or
// from volume to volume (the AO, the reflections, the lightmap page, the GI probe atlases) are bindless handles rather than
// `layout(binding=N)` samplers, so that they cost none of the fragment stage's sampler units (32 on some drivers). See
// the sampler layout below for what still takes one.
#extension GL_ARB_bindless_texture : enable

#include "../common/light.glsl"
#include "../common/cluster.glsl"
#include "../common/normal_map.glsl"
#include "../common/tonemap.glsl"

layout(std430, binding = 0) buffer LightBuffer {
    Light lights[];
};

// Clustered light culling (point/spot lights only - see LightCullingManager). Filled every frame by
// cluster_light_cull.comp; directional lights bypass this entirely (see the dedicated loop in main()).
layout(std430, binding = 2) readonly buffer LightGrid {
    uvec2 clusters[]; // x = offset into lightIndices, y = count
};
layout(std430, binding = 3) readonly buffer LightIndexList {
    uint lightIndices[];
};

uniform uvec2 clusterGridSizeXY;
uniform float clusterScaleZ;
uniform float clusterBiasZ;

// This file is three programs (see the permutations the renderer asks for, Shader::GetVariant) :
//   (nothing)                   the forward shader : a mesh drawn with its material, lit where it is drawn
//   SHARD_GBUFFER               the material half of a deferred pipeline : the same material evaluation, written to the G-buffer instead of lit
//   SHARD_DEFERRED_LIGHTING     the lighting half : rebuilds the surface of each pixel from the G-buffer and runs the same lighting (ShadeSurface)
#ifdef SHARD_GBUFFER
layout(location = 0) out vec4 gAlbedo;          // albedo.rgb, flags (3 * a : bit 0 the surface has a lightmap, bit 1 it reflects the environment)
layout(location = 1) out vec4 gNormals;         // octahedral shading normal (xy), octahedral geometric normal (zw)
layout(location = 2) out vec4 gMaterial;        // emission.rgb, metallic
layout(location = 3) out vec4 gBaked;           // baked irradiance.rgb, roughness
#else
out vec4 fragColor;
#endif

#ifdef SHARD_DEFERRED_LIGHTING
// No vertex stage feeds the surface here : it is rebuilt from the G-buffer and the depth, with the camera's matrices
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

// The interpolated normal, turned to face the viewer : a back face (double sided materials) is lit as the surface seen from its
// other side. Written at the start of main().
vec3 N;

uniform int lightNB;

uniform vec3 camPos;

uniform bool masked;

// Set by the renderer for the depth only sweep that runs before the shaded one (see RendererAPI::SetDepthSweep) : the
// draw writes depth and nothing else, so the shader returns before it reads a texture or computes any lighting. Only
// opaque materials take part (a masked one needs its alpha, and is drawn by the shaded sweep alone).
uniform bool depthPrepass;

// Editor viewport debug views (see DebugViewState). 0 Lit, 1 Unlit, 2 Wireframe, 3 Shaded+Wireframe
// (wire sweep), 4 Normals, 5 Depth, 6 UVs. showLighting = false swaps the lighting for a flat
// camera-facing shade; showShadows = false treats every light as fully unshadowed.
uniform int viewMode;
uniform bool showLighting;
uniform bool showShadows;

// The project's shadow settings (see ShadowSettings) : set per draw by Renderer::BindWorldState
uniform bool shadowsEnabled;
uniform int dirShadowFilterRadius;          // the filter takes (2 * radius + 1)^2 taps
uniform float dirShadowFilterSpread;        // distance between two taps, in texels of the map
uniform float dirShadowNormalBias;          // in texels
uniform float dirShadowDepthBias;           // in texels
uniform float dirShadowSlopeBias;           // scale of the receiver plane bias
uniform int spotShadowFilterRadius;
uniform float spotShadowFilterSpread;
uniform float spotShadowDepthBias;
uniform float spotShadowMinDepthBias;
uniform int pointShadowFilterSamples;       // 1 to 20
uniform float pointShadowFilterRadius;
uniform float pointShadowDepthBias;

// Shadow maps

#define MAX_DIRECTIONAL_LIGHTS 3
#define MAX_CASCADES_PER_LIGHT 6
#define NUM_CASCADES (MAX_DIRECTIONAL_LIGHTS * 3)

// Cascades per sun (the project's setting) : sun k reads the slots k * dirCascadeCount ... (k + 1) * dirCascadeCount - 1
uniform int dirCascadeCount;
uniform vec3 dirCascadeColors[MAX_CASCADES_PER_LIGHT];

uniform mat4 dirLightSpaceMatrices[NUM_CASCADES];
uniform float dirCascadeSplits[NUM_CASCADES];

uniform mat4 spotLightSpaceMatrices[10];

uniform float pointLightFarPlanes[10];

#ifdef SHARD_DEFERRED_LIGHTING
// SHARD_GBUFFER_MS : the G-buffer is multisampled. Reading one of its samples with gl_SampleID makes the whole lighting run once per
// sample (sample shading), into the multisampled scene target : every sample of an edge pixel is lit with the surface it covers, and
// the resolve of the scene does the anti-aliasing, like for the forward pipelines.
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

// Real-time diffuse GI (see ProbeManager) - falls back to the static IBL diffuse term below when no
// probe volume's grid contains the shading point (DDGI_PickVolume returns -1). Up to this many
// ProbeVolumes can be active at once, each with its own grid/atlases; DDGI_PickVolume picks the
// smallest one containing the point, so a dense local volume overrides a coarse room-scale one it's
// nested in. MUST equal ProbeManager::kMaxProbeVolumes - scene_binding.cpp fills ddgi_*[0 .. volumeCount-1].
#define MAX_PROBE_VOLUMES 2

// Kernel weight under which a probe of the blend is skipped, see DDGI_SampleVolume
#define DDGI_MIN_TAP_WEIGHT 0.004

// One octahedral irradiance tile atlas + one (mean dist, mean dist^2) distance atlas per volume, as bindless handles
// (low/high 32 bits, like ssaoTextureHandle) : no sampler unit, and the volumes' atlases need not share a size. The
// distance atlas feeds DDGI_VisibilityWeight, which stops an occluded probe (wall between it and the shading point) from
// leaking light/shadow through the trilinear blend. The volume a fragment reads is per-fragment, so DDGI_Sample*Atlas
// never index these with it : they walk the volumes with a constant index, which keeps every handle uniform.
uniform uvec2 ddgi_irradianceAtlasHandle[MAX_PROBE_VOLUMES];
uniform uvec2 ddgi_distanceAtlasHandle[MAX_PROBE_VOLUMES];

// ---- Sampler units : what still takes one (11 of the 32 some drivers allow a fragment stage) ----
//   0-3   the material's textures (forward) or the G-buffer (deferred lighting)
//   4-5   the sky's irradiance and prefiltered environment
//   6     the reflection probe
//   7-9   the shadow maps : one array per kind of light, a light's map is a layer of it
//   10    the emission map (forward) or the G-buffer's depth (deferred lighting)

// Reflection probe (see ReflectionProbe) : a baked picture of the surroundings of a point, prefiltered by roughness the way the skybox's is,
// which the surfaces inside the probe's box reflect instead of the sky. ONE probe at a time (the one nearest to the camera, picked by
// scene_binding.cpp).
layout(binding = 6) uniform samplerCube rp_prefilter;
uniform bool rp_enabled;
uniform vec3 rp_center;                // where the picture was taken, and the centre of the box
uniform vec3 rp_halfExtent;
uniform int rp_shape;                  // 0 box, 1 sphere, 2 cylinder, 3 cone, 4 pyramid (see ReflectionProbeShape) - inscribed in the box
uniform vec3 rp_params;                // x : intensity, y : blend distance, z : box projection (0 or 1)

uniform bool ddgi_enabled;
uniform int  ddgi_volumeCount;                     // ready volumes actually filled in below, <= MAX_PROBE_VOLUMES
uniform vec3 ddgi_gridOrigin[MAX_PROBE_VOLUMES];
uniform vec3 ddgi_gridSpacing[MAX_PROBE_VOLUMES];
uniform vec3 ddgi_probeCounts[MAX_PROBE_VOLUMES];  // ivec3 stored as vec3
uniform int  ddgi_tileSize[MAX_PROBE_VOLUMES];
uniform int  ddgi_atlasProbesPerRow[MAX_PROBE_VOLUMES];
uniform int  ddgi_atlasSize[MAX_PROBE_VOLUMES];

// Per-probe state buffer, one per volume (SSBO 14, 15 - see gl_api.cpp / probe_manager.hpp). GLSL 4.3
// can't put an unsized array inside an arrayed interface block portably, so it's one named block per
// volume plus the DDGI_ProbeState() dispatch below. .w = classification flag (0 = probe embedded in
// geometry, contributes nothing), .xyz = relocation offset added to the probe's grid position.
layout(std430, binding = 14) readonly buffer DDGIProbeState0 { vec4 ddgi_probeState0[]; };
layout(std430, binding = 15) readonly buffer DDGIProbeState1 { vec4 ddgi_probeState1[]; };

vec4 DDGI_ProbeState(int v, int probeIndex) {
    if (v == 0) return ddgi_probeState0[probeIndex];
    return ddgi_probeState1[probeIndex];
}

// The shadow maps (see ShadowManager::SyncLayeredShadowTargets) : sun k's cascade c is layer k * dirCascadeCount + c of the
// first array, spot light s is layer s of the second, point light p is cube p of the third. The layer is a coordinate, so it
// may differ from one fragment to the next (a sampler array could only be indexed uniformly).
layout(binding = 7) uniform sampler2DArrayShadow dirShadowMapArray;
layout(binding = 8) uniform sampler2DArray spotShadowMapArray;
layout(binding = 9) uniform samplerCubeArray pointShadowMapArray;

// Emission map - a material texture slot like albedo/metallicMap/roughnessMap/normalMap, so it has to be
// a real sampler (GLMaterial::Bind binds material textures per unit, by this binding) rather than a
// bindless handle like ssaoTextureHandle.
#ifdef SHARD_DEFERRED_LIGHTING
layout(binding = 10) uniform GBUFFER_SAMPLER gDepthTex;
#else
layout(binding = 10) uniform sampler2D emissiveMap;
#endif

// PBR values
const float PI = 3.141592653589793;
uniform float metallic;
uniform float roughness;
uniform vec3 ambientColor;

// Self-illumination, glTF-style : emissiveMap.rgb * emissive, so `emissive` is the colour/intensity factor
// (values above 1 are fine - the scene is HDR) and an unset material stays non-emissive (factor 0). The
// default emissiveMap is white (see GLMaterial::GetDefaultTexture), which makes a plain `emissive` value
// with no map a flat glow, and a map alone - with no `emissive` set - deliberately show nothing, same as
// metallic/roughness need their scalar alongside their maps. Reset to 0 before every draw by
// GLRendererAPI::BindLevelState (a persistent program uniform would otherwise leak from one material into
// the next).
uniform vec3 emissive;

// Screen-space ambient occlusion (see SSAOManager) - ssaoTextureHandle is a bindless texture handle
// (low/high 32 bits of an ARB_bindless_texture handle), reconstructed into a
// sampler only at the point of use (SampleSSAO below) via GLSL's sampler2D(uvec2) constructor - NOT
// declared as `uniform sampler2D`, which would consume one of this shader's texture
// units. ssaoIntensity blends toward "no occlusion" in SampleSSAO,
// so it's a cheap post-hoc strength control that doesn't need the SSAO passes themselves rerun.
uniform bool ssaoEnabled;
uniform float ssaoIntensity;
uniform uvec2 ssaoTextureHandle;

vec3 SampleSSAO(vec3 ambient) {
    if (!ssaoEnabled || ssaoTextureHandle == uvec2(0))
        return ambient;

    // The constructor is used where the sampler is read, never stored in a variable : the Vulkan build (shaderc) turns it into a
    // read of the bindless texture array, and a sampler cannot be a local variable there
    float ao = texture(sampler2D(ssaoTextureHandle), gl_FragCoord.xy / vec2(textureSize(sampler2D(ssaoTextureHandle), 0))).r;
    // ssaoIntensity is allowed above 1.0 as an amplification knob (mix() extrapolates past `ao`
    // once the blend factor exceeds 1) - clamp so an aggressive value can't overshoot into
    // negative/relighting territory.
    return ambient * clamp(mix(1.0, ao, ssaoIntensity), 0.0, 1.0);
}

// Screen-space reflections (see SSRManager). Two bindless texture handles, for the same reason as ssaoTextureHandle's :
//   ssrHitHandle     per pixel of the screen, xy where the reflected ray landed in the last frame's picture (uv), z how many pixels wide a unit of
//                    tangent of the specular lobe is at the hit, w the confidence (0 : the ray found nothing on the screen)
//   ssrColorHandle   the last frame's HDR picture, with the mips that blur it
// The trace is the mirror ray, it does not know the roughness : it is applied here, by the level of the pyramid the colour is read at, and by
// fading the reflection out of the surfaces too rough for it.
uniform bool ssrEnabled;
uniform float ssrIntensity;
uniform float ssrMaxRoughness;
uniform float ssrRoughnessBlur;
uniform float ssrMaxLod;
uniform uvec2 ssrHitHandle;
uniform uvec2 ssrColorHandle;

// The light that comes from the reflected direction as the screen shows it (rgb), and how much of the specular light it replaces (a)
vec4 SampleSSR(float roughnessValue)
{
    if (!ssrEnabled || ssrHitHandle == uvec2(0) || ssrColorHandle == uvec2(0))
        return vec4(0.0);

    float roughnessFade = 1.0 - smoothstep(0.6 * ssrMaxRoughness, ssrMaxRoughness, roughnessValue);
    if (roughnessFade <= 0.0)
        return vec4(0.0);

    // As with the AO, the sampler is made where it is read (a Vulkan build cannot keep it in a variable)
    vec4 hit = textureLod(sampler2D(ssrHitHandle), gl_FragCoord.xy / vec2(textureSize(sampler2D(ssrHitHandle), 0)), 0.0);
    if (hit.w <= 0.0)
        return vec4(0.0);

    // The specular lobe is about roughness^2 wide (its tangent) : at the hit, that many pixels across is the blur to read the colour with
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
bool useEnvReflections;     // a property of the material, carried by the G-buffer (see the flags in gAlbedo.a)
#else
uniform bool useEnvReflections;
#endif

// The scene is normally drawn into an HDR target and mapped to the picture by a pass of its own (tonemap.frag), so this
// shader outputs linear light. A render that goes straight to an 8 bit target - the studio views of the thumbnails and the
// previews - sets this, and gets the display transform (a fixed filmic curve) here instead.
uniform bool outputDisplayColors;

// Baked lighting (see LightmapManager) : the page of the lightmap atlas this draw reads, as a bindless handle (no sampler
// unit, see the layout at the top), and what maps the mesh's lightmap coordinates onto it (uv * xy + zw). The page holds
// the irradiance the surface receives from the sky, the Static lights and the bounces of all of it, which replaces the probes
// and the skybox for the diffuse ambient term. Set per draw : lightmapEnabled is false for a mesh without a lightmap.
uniform bool lightmapEnabled;
uniform uvec2 lightmapHandle;
uniform vec4 lightmapScaleOffset;

///////// Shadow Functions /////////

// Cascade Selection
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

// worldNormal must be the GEOMETRIC (interpolated vertex) normal, not the normal-mapped one : the bias below is a statement about
// the surface the shadow map actually stored, and a weave normal map makes it flicker per texel of the cloth (the mosaic).
float ShadowCalculationDir(int cascade, mat4 lightSpaceMatrix, vec3 lightDir, vec3 worldPos, vec3 worldNormal){
    mat3 M = mat3(lightSpaceMatrix);
    // The cascade is an orthographic box : the scale of each axis of the light space gives the size of a shadow texel and the
    // world depth range of the map
    float scaleX = length(vec3(M[0][0], M[1][0], M[2][0]));
    float scaleZ = length(vec3(M[0][2], M[1][2], M[2][2]));
    vec2 mapSize = vec2(textureSize(dirShadowMapArray, 0).xy);
    float texelWorld = 2.0 / (scaleX * mapSize.x);
    float zPerWorld = 0.5 * scaleZ;                 // shadow depth (0..1) per world unit

    // A cloth is seen from both sides : the bias only cares how slanted the surface is to the light, and which way is "off" it
    vec3 n = normalize(worldNormal);
    float NdotL = dot(n, lightDir);
    if (NdotL < 0.0) { n = -n; NdotL = -NdotL; }
    float sinTheta = sqrt(max(1.0 - NdotL * NdotL, 0.0));

    // ----- Normal offset : look the surface up from slightly above it, more the more it grazes the light -----
    vec3 biasedPos = worldPos + n * (texelWorld * dirShadowNormalBias * sinTheta);

    vec4 fragLS = lightSpaceMatrix * vec4(biasedPos, 1.0);
    vec3 proj = fragLS.xyz / fragLS.w;

    // Transform to [0..1]
    proj = proj * 0.5 + 0.5;

    // Outside light frustum : not shadowed
    if (proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0 ||
        proj.z > 1.0)
        return 1.0;

    // ----- Receiver-plane depth bias : every PCF tap is compared against the depth the surface has AT that tap, not at the
    // centre. A constant bias cannot hold for a 5 x 5 kernel on a slanted surface, it either lets it self-shadow (acne) or
    // detaches the shadow. The plane's slope in shadow space comes from its normal (M^-T n, depth over u and v). -----
    vec3 nLS = transpose(inverse(M)) * n;
    vec2 depthSlope = vec2(0.0);
    if (abs(nLS.z) > 1e-4)
        depthSlope = clamp(-nLS.xy / nLS.z, vec2(-8.0), vec2(8.0)) * dirShadowSlopeBias;     // clamped : a surface edge on to the light must not explode

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

    // Add XY bounds check like directional
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

// Flattened index into LightGrid/LightIndexList for the cluster this fragment sits in - must match
// the flattening cluster_build.comp/cluster_light_cull.comp use (x + y*gridWidth + z*gridWidth*gridHeight).
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
        // One tap is the lookup direction itself
        vec3 tapOffset = samples > 1 ? sampleOffsetDirections[i] * diskRadius : vec3(0.0);
        float closestDepth = texture(pointShadowMapArray, vec4(fragToLight + tapOffset, index)).r;
        closestDepth *= farPlane;   // undo mapping [0;1]
        if(currentDepth - bias > closestDepth)
            shadow += 1.0;
    }
    shadow /= float(samples);

    return 1.0 - shadow;
}

///////// Lighting /////////

vec3 IBL_Diffuse(vec3 N, vec3 albedo, float metallic) {
    vec3 kS = mix(vec3(0.04), albedo, metallic);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    vec3 irradiance = texture(ibl_irradianceMap, N).rgb;
    return irradiance * albedo * kD;
}

#include "../common/octahedral.glsl"
#include "../common/ddgi_atlas.glsl"
#include "../common/ddgi_visibility.glsl"

// Per-fragment volume select over the atlas handles : the loop's index is a constant for every fragment, so the handle each
// texture() is made from is uniform (only whether it runs differs), which is all GL_ARB_bindless_texture and the Vulkan
// build's bindless array ask for. The sampler is made where it is read (a Vulkan build cannot keep it in a variable).
// textureLod(.., 0) not texture() : the atlases have no mips, and the select below is non-uniform
// control flow where implicit-LOD derivatives are undefined.
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

// Samples one probe's octahedral tile in volume v's irradiance atlas in direction N - see
// common/ddgi_atlas.glsl's DDGI_AtlasUV for the shared tile-addressing math this builds on.
vec3 DDGI_SampleProbe(int v, int probeIndex, vec3 N) {
    vec2 uv = DDGI_AtlasUV(probeIndex, N, ddgi_tileSize[v], ddgi_atlasProbesPerRow[v], ddgi_atlasSize[v]);
    return DDGI_SampleIrradianceAtlas(v, uv);
}

// Same tile lookup against the distance atlas - returns (mean hit distance, mean hit distance^2).
vec2 DDGI_SampleDistance(int v, int probeIndex, vec3 dir) {
    vec2 uv = DDGI_AtlasUV(probeIndex, dir, ddgi_tileSize[v], ddgi_atlasProbesPerRow[v], ddgi_atlasSize[v]);
    return DDGI_SampleDistanceAtlas(v, uv);
}

// Index of the smallest active volume whose box contains p, or -1 if none does. "Smallest" =
// smallest cell volume, so a dense local ProbeVolume nested inside a coarse room-scale one wins for
// points it covers, with no explicit priority field (see ProbeVolume's header comment).
//
// "Box" is the volume's FULL extent (halfExtent around its centre), not the AABB of the probe positions :
// ProbeVolume::GetGridOrigin() insets the lattice by half a cell so no probe sits exactly on a room's wall,
// which is exactly why a volume is meant to be sized flush against the room - so the walls, floor and
// ceiling lie in that outer half cell. Testing against the lattice AABB alone rejected every one of those
// surfaces (no GI at all - black for anything without env reflections, the sudden cut in luminosity where
// the lattice ends). DDGI_SampleVolume clamps to the outermost probe layer there, which is the intended
// behaviour for the outer half cell.
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

// Blends the 3x3x3 probes of volume `v` around worldPos with a quadratic B-spline, sampling each one
// TWICE - once toward N (`outDiffuse`, the cosine-lobe irradiance a diffuse surface receives) and once
// toward `specDir` (`outSpecular`, the same data read as incoming radiance from the reflection
// direction). Both share one loop on purpose : a probe's weight depends only on where the shading point
// is relative to that probe, never on which direction the tile is then read in, so the expensive half -
// the Chebyshev visibility lookups into the distance atlas - is paid once instead of twice.
//
// Each probe's spline weight is scaled by DDGI_VisibilityWeight (occluded probe -> ~0), by the probe's
// classification flag (probe embedded in geometry -> 0) and by a back-face weight; every probe position
// includes its relocation offset (see DDGI_ProbeState / probe_relocate.comp).
void DDGI_SampleVolume(int v, vec3 worldPos, vec3 N, vec3 specDir, out vec3 outDiffuse, out vec3 outSpecular) {
    // Bias the sampled position off the surface along its normal before gridding, same as
    // probe_trace.comp's SampleIndirect (see that function's comment) - the bias is kept in sync since
    // they're sibling lookups (one feeding the next bounce, this one shading what the player actually
    // sees). The blend kernel is NOT shared : SampleIndirect stays a cheap 8-tap trilinear blend, because
    // its result is re-averaged by the bounce loop and never seen directly, so its creases don't show;
    // only this final-pixel path needs the smoother, costlier B-spline below.
    //
    // Missing this bias here used to mean the *final* shaded pixel sampled the grid right at the surface it
    // sits on, which is far more prone to picking up the wrong side of a nearby occlusion boundary than the
    // (correctly biased) bounce-feedback path was - most visible as weak/missing colored bounce light right
    // where two differently-tinted surfaces meet (e.g. a colored curtain near the floor).
    vec3 biasedPos = worldPos + N * (0.25 * min(min(ddgi_gridSpacing[v].x, ddgi_gridSpacing[v].y), ddgi_gridSpacing[v].z));

    vec3 gridPos = (biasedPos - ddgi_gridOrigin[v]) / max(ddgi_gridSpacing[v], vec3(1e-4));

    // Quadratic B-spline blend over the 3x3x3 probes around the NEAREST lattice node, not a 2x2x2 trilinear
    // blend of the enclosing cell. A 2-tap blend can't be smooth : plain trilinear weights are C0, so the
    // light has a slope jump on every lattice plane (creases, a "cross" through any glow spanning a couple
    // of probes), and easing them (smoothstep) forces the slope to ZERO at every plane, which turns a
    // gradient into a staircase of plateaus with steep risers - visible as vertical/horizontal banding on a
    // flat wall. The quadratic B-spline is C1 with a non-zero slope through the nodes, so the field moves
    // continuously with no terraces and no creases. Cost : up to 27 probes per fragment instead of 8 (taps
    // whose weight is exactly 0 - i.e. everything but a 2x2x2 block when the point sits half-way between two
    // nodes - are skipped below). The weights are the standard (0.5(.5-d)^2, .75-d^2, 0.5(.5+d)^2) for
    // d in [-0.5, 0.5) from the nearest node, and always sum to 1 per axis.
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
        // The outermost taps (the corners of the 3x3x3 block, and the ones far from the point along an axis) weigh a
        // fraction of a percent : each costs a state read, a distance and two irradiance fetches, and leaving them out (the
        // rest is renormalized below) changes the picture by less than one level of an 8 bit channel (PSNR 67 dB on
        // Sponza) while the pass runs about a fifth faster.
        if (kernelWeight < DDGI_MIN_TAP_WEIGHT)
            continue;

        // Off the lattice edge the outermost probe layer is reused (clamp), so the field goes flat there.
        vec3 probeCoord = clamp(nearest + vec3(tap - 1), vec3(0.0), maxCoord);
        int probeIndex = int(probeCoord.x) + int(probeCoord.y) * rowStride + int(probeCoord.z) * layerStride;

        vec4 state = DDGI_ProbeState(v, probeIndex);
        if (state.w <= 0.0) // probe classified as embedded in geometry
            continue;

        // Visibility test direction is probe -> (biased) shading point, NOT N (the irradiance sample
        // direction below) - a probe can be occluded from a point regardless of that point's surface
        // normal. + state.xyz : the probe may have been relocated out of geometry.
        vec3 probeWorldPos = ddgi_gridOrigin[v] + probeCoord * ddgi_gridSpacing[v] + state.xyz;
        vec3 toPoint = biasedPos - probeWorldPos;
        float distToPoint = length(toPoint);
        vec3 dirToPoint = toPoint / max(distToPoint, 1e-5);

        float visWeight = DDGI_VisibilityWeight(DDGI_SampleDistance(v, probeIndex, dirToPoint), distToPoint);

        // Smooth back-face weight : a probe on the far side of the surface being shaded (behind the wall)
        // fades out instead of leaking dark/wrong light through it. Chebyshev alone can't do this - it is
        // fooled by rays that graze an edge or miss into the sky, and the 3x3x3 footprint reaches one probe
        // layer further than the old 2x2x2 one, so it needs the extra guard. Continuous in the probe's
        // position, so it adds no seams. Measured from the unbiased point : that's the real surface.
        //
        // Known limit : valid probes OUTSIDE a room (one layer past an embedded wall/ceiling layer) are dark
        // and Chebyshev can't see the wall between them and the room, so they darken the last ~0.5 units
        // before a wall/ceiling junction - a smooth vignette. Do NOT "fix" that by fading taps that lie behind
        // an embedded probe on the lattice : that rule flips on/off as the shading point crosses the embedded
        // probe's plane and shows up as a hard seam across every surface (tried; a wide ramp only trades the
        // seam for a corner discontinuity). The real cure is a volume that fits the room, so no probe layer
        // sits outside it.
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

    // Deliberately no epsilon floor on totalWeight : if every surrounding probe is occluded from this
    // point (e.g. a fully sealed room lit only from outside), the correct result is 0 (no bounce light),
    // not a dim leak propped up by a fallback weight.
    if (totalWeight > 0.0) {
        irradiance /= totalWeight;
        specRadiance /= totalWeight;
    }

    outDiffuse = irradiance;
    outSpecular = specRadiance;
}

// Diffuse indirect : the blended probe irradiance combined with albedo/metallic exactly the way
// IBL_Diffuse does. Multiplying by this surface's own albedo is where colour bleeding lands - light
// that already picked up a red wall's tint arrives tinted, and leaves tinted again by whatever it hits.
vec3 DDGI_Diffuse(vec3 irradiance, vec3 albedo, float metallic) {
    vec3 kS = mix(vec3(0.04), albedo, metallic);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    return irradiance * albedo * kD;
}

// Split-sum environment BRDF, Karis' analytic fit instead of a lookup into a table (there is none : a BRDF table was only
// bound when the level had a skybox, and probe-lit reflections have to work in a room that has none at all - a sealed Cornell box being the obvious case,
// where the entire point is that the reflections come from the red and green walls and not from a sky.
vec3 EnvBRDFApprox(vec3 F0, float roughness, float NdotV) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

// Specular indirect from the probe grid - the "coloured reflection" half of GI, and the only source of
// one for a surface with no skybox behind it. A probe tile is a cosine-lobe-filtered environment map,
// i.e. an extremely wide specular lobe, so this is only an honest answer for rough surfaces : the
// lookup direction is therefore pulled from the mirror direction R back toward N as roughness rises (so
// the direction sampled matches the lobe width the atlas can actually represent), and main() crossfades
// this against the sharp skybox reflection by roughness where a skybox exists.
vec3 DDGI_SpecularDirection(vec3 N, vec3 V, float roughness) {
    vec3 R = reflect(-V, N);
    vec3 dir = mix(R, N, roughness * roughness);
    // R and N are antiparallel when the (possibly normal-mapped) normal points straight away from the
    // viewer, and the blend above then passes exactly through zero at roughness^2 == 0.5 - normalizing
    // that is a NaN, which propagates through the atlas lookup into the shaded pixel. Fall back to N,
    // the direction this blend is heading toward anyway.
    float len = length(dir);
    return len > 1e-4 ? dir / len : N;
}

vec3 DDGI_Specular(vec3 radiance, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    return radiance * EnvBRDFApprox(F0, roughness, max(dot(N, V), 0.0));
}

// How much the probe reflects at P : 1 deeper inside its box than the blend distance, fading to 0 at the edge of the box, 0 outside
float RP_Weight(vec3 P) {
    vec3 d = P - rp_center;
    vec3 inside = rp_halfExtent - abs(d);                       // distance to the face along each axis, negative outside
    float depth = min(inside.x, min(inside.y, inside.z));
    float minXZ = min(rp_halfExtent.x, rp_halfExtent.z);

    // Distance to the surface of the shape, roughly (exact for the box) : how far the point is inside in units of the box, along the
    // normalised coordinates the shape is the unit one in
    if (rp_shape == 1) {
        depth = (1.0 - length(d / rp_halfExtent)) * min(minXZ, rp_halfExtent.y);
    } else if (rp_shape == 2) {
        depth = min((1.0 - length(d.xz / rp_halfExtent.xz)) * minXZ, inside.y);
    } else if (rp_shape >= 3) {
        // Narrows from the full base at the bottom to a point at the top
        float narrow = 1.0 - (d.y + rp_halfExtent.y) / (2.0 * rp_halfExtent.y);
        float radial = rp_shape == 3 ? length(d.xz / rp_halfExtent.xz) : max(abs(d.x) / rp_halfExtent.x, abs(d.z) / rp_halfExtent.z);
        depth = min((narrow - radial) * minXZ, inside.y);
    }
    return clamp(depth / max(rp_params.y, 1e-3), 0.0, 1.0);
}

// The direction to look the picture up in so that what is reflected lands where the box's walls are, not infinitely far : the ray from P
// along R is intersected with the box, and the picture (taken at the centre) is read toward the point it hits
vec3 RP_BoxProject(vec3 P, vec3 R) {
    if (rp_shape == 1) {
        // The ellipsoid : the unit sphere once the space is divided by the half extent
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

// Specular indirect from the reflection probe, with how much of it applies (0 : P is outside the probe's box, the caller keeps the sky's
// reflection). The split-sum scale is the analytic fit : the probe has no BRDF table of its own.
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

    // Sample prefiltered env map using roughness mip level
    const float MAX_REFLECTION_LOD = 5.0;
    vec3 prefilteredColor = textureLod(ibl_prefilteredEnvMap, R, roughness * MAX_REFLECTION_LOD).rgb;

    // Split-sum scale and bias, the analytic fit rather than a table : the table's sampler unit is the reflection probe's. Fresnel is part of it.
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

    // GGX Distribution. A perfectly smooth surface (roughness 0) makes the lobe a delta : 0 / 0 at the highlight, a NaN that an
    // 8 bit target used to hide by clamping it. The lowest roughness a punctual light can show is clamped to what stays finite.
    roughness = max(roughness, 0.045);
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom);

    // Geometry
    float k = (roughness + 1.0);
    k = (k * k) / 8.0;
    float G_V = NdotV / (NdotV * (1.0 - k) + k);
    float G_L = NdotL / (NdotL * (1.0 - k) + k);
    float G = G_V * G_L;

    // Fresnel
    vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);

    // Specular BRDF
    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    vec3 specular = numerator / denominator;

    // Diffuse term (energy conserving)
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    vec3 diffuse = kD * baseColor / 3.14159265359;

    // Combine with light color, intensity, NdotL, shadow, attenuation
    vec3 Lo = (diffuse + specular) * NdotL * light.color.xyz * light.intensity * visibility * attenuation;

    return Lo;
}

// A normal map perturbs N.L around the shading terminator : where the geometric surface turns from the light, the weave's
// normals face it in patches, which cuts the lit area into a ragged edge at the scale of the texture. The geometric N.L fades
// the direct light out over a short range instead, so the edge follows the folds, not the weave.
float TerminatorSoften(vec3 geoNormal, vec3 L){
    return smoothstep(0.0, 0.3, dot(normalize(geoNormal), L));
}

// Same terminator, seen from the normal : near it the shading normal is the geometric one, so the weave cannot flip N.L patch by
// patch inside the fade ; away from it the full normal-mapped detail is back.
vec3 TerminatorNormal(vec3 geoNormal, vec3 mappedNormal, vec3 L){
    float t = smoothstep(0.0, 0.5, dot(normalize(geoNormal), L));
    return normalize(mix(normalize(geoNormal), mappedNormal, t));
}

// Everything that lights a surface once its material is known : the direct lights (clustered, shadowed), the probes, the environment, the ambient
// term and the occlusion. The forward shader calls it where the mesh is drawn, the deferred lighting shader once per pixel of the G-buffer.
// `N` (the interpolated normal facing the viewer, the geometric one) and `worldPos` are set by whoever calls it.
vec4 ShadeSurface(vec4 baseColor, float metallicValue, float roughnessValue, vec3 worldNormal, bool hasLightmap, vec3 bakedIrradiance, vec3 emission, vec3 V)
{
    vec3 result = vec3(0.0);

    // Debug view 10 : the cascade each pixel reads (its color in the project's cascade settings) darkened by the sun's shadow there, white where the
    // pixel falls outside the cascade's map (which counts as lit), magenta when no directional light casts shadows
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

    // Directional lights: never clustered (at most a handful in any scene - see
    // LightManager::MAX_DIRECTIONAL_LIGHTS), so still a plain scan of the full light list.
    for (int i = 0; i < lightNB; ++i) {
        Light l = lights[i];
        if (l.type != 0)
            continue;

        // A Static light is part of the lightmap this surface reads : lighting it again here would count it twice
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

    // Point/Spot lights: only the subset the light-culling compute pass placed in this fragment's
    // cluster (see LightCullingManager / cluster_light_cull.comp) - not the full light list.
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

        if (l.type == 1) { // Point
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
        else if (l.type == 2) { // Spot
            vec3 toFrag = worldPos - l.position.xyz;

            float dist = length(toFrag);

            L = normalize(l.position.xyz - worldPos);

            // distance attenuation
            attenuation = 1.0 / (dist * dist);

            float falloff = clamp(1.0 - dist / l.radius, 0.0, 1.0);
            attenuation *= falloff * falloff;

            // cone attenuation
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

    // Real-time GI (ddgi_enabled) intentionally isn't gated behind useEnvReflections - that flag toggles
    // the (costlier) specular env reflections per material, but diffuse GI from an active probe volume
    // should show up on every material regardless, or it silently does nothing on any material that
    // doesn't happen to have useEnvReflections set. A point outside every active volume's grid
    // (ddgiVolume < 0) falls back to the same IBL / flat-ambient path as when no volume exists.
    // Indirect light is low frequency : probes and the environment map hold no detail at the scale of a weave, so looking them up
    // with the full normal map only makes the GI flicker texel by texel (the patches seen in the GI only view). The lookups use a
    // normal mostly geometric, with a little of the normal map left.
    vec3 giNormal = normalize(mix(normalize(N), worldNormal, 0.3));

    vec3 ambientDiffuse = vec3(0.0);
    vec3 specularIBL = vec3(0.0);

    // The skybox reflection knows nothing of walls : indoors it would light a lightmapped surface from a sky it cannot see, most
    // visibly as a bright rim at grazing angles (Fresnel) along edges and junctions. The lightmap says how much of the sky
    // the surface actually gets : its light against the light an unoccluded surface facing this way would receive.
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

        // A lightmapped surface takes its diffuse light from the lightmap, which has a texel per few centimetres where the
        // probes have one per metre or more ; the probes still say what the surroundings reflect (below)
        ambientDiffuse = DDGI_Diffuse(hasLightmap ? bakedIrradiance : probeIrradiance, baseColor.rgb, metallicValue);

        // Indirect specular from the probes : what the surroundings actually reflect, rather than what
        // the sky would if it could be seen from here. Crossfaded against the skybox reflection by
        // roughness where there is a skybox to crossfade with - a probe tile carries no sharp detail, so
        // a near-mirror surface is better served by the prefiltered environment map, while a rough one
        // is better served by the probes (indoors, the sky is usually not what it can see at all). Not
        // gated behind useEnvReflections, for the same reason the diffuse term above isn't : that flag
        // toggles the costlier skybox reflections per material, and gating real computed GI behind it
        // means a material that happens to have it off silently gets no indirect specular anywhere.
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
    }

    // Reflection probe : what the room around this point reflects, in place of the sky's reflection (and of the probe grid's coarse one) by the
    // probe's weight - 1 inside its box, fading to the sky's at its edge. Gated by useEnvReflections like the skybox reflection it replaces.
    if (useEnvReflections && rp_enabled) {
        float rpWeight;
        vec3 rpSpecular = RP_Specular(worldPos, giNormal, V, baseColor.rgb, metallicValue, roughnessValue, rpWeight);
        specularIBL = mix(specularIBL, rpSpecular, rpWeight);
    }

    // Screen-space reflection : what the screen shows where the mirror ray lands replaces the probe's / the sky's reflection, by how sure the
    // trace is of it. Where it found nothing the weight is 0 and the reflection above is what there is. Same gate as the reflections it replaces.
    if (useEnvReflections && ssrEnabled) {
        vec4 ssr = SampleSSR(roughnessValue);
        if (ssr.a > 0.0) {
            vec3 ssrF0 = mix(vec3(0.04), baseColor.rgb, metallicValue);
            vec3 ssrSpecular = ssr.rgb * EnvBRDFApprox(ssrF0, roughnessValue, max(dot(giNormal, V), 0.0));
            specularIBL = mix(specularIBL, ssrSpecular, ssr.a);
        }
    }

    // Flat ambient floor, ADDED on top of whatever DDGI/IBL/direct lighting already computed above
    // (not multiplied into it, and not gated behind "no DDGI and no IBL" the way this used to be) : this
    // is what makes ambientColor mean "light that's there even when the scene has no lights at all"
    // - a real light source can't do that (zero lights = zero direct contribution, always), only a flat
    // additive term can. Being additive rather than a multiplier also means it still does something in
    // a spot DDGI/IBL computes near-zero for (a probe-starved corner, geometry outside every volume's
    // grid), instead of just scaling an already-dark result to a still-dark one. Level::ambientColor
    // defaults to 0.0 - DDGI/IBL are real, computed lighting and don't need a fake floor propping them
    // up by default, so this stays a no-op unless a level explicitly wants unlit corners not to be pure
    // black, or a material overrides it for a cheap self-lit look (light_bulb.mat : 4.0, useEnvReflections
    // = false, so this term is the ENTIRE reason it's visibly bright at all - see GLRendererAPI::
    // BindLevelState for how a level default vs. a per-material override of this uniform interact).
    ambientDiffuse += baseColor.rgb * ambientColor;

    // Screen-space ambient occlusion (see SampleSSAO above / SSAOManager) - darkens ambient/indirect
    // light in creases/corners/contact points, not `result` (direct lighting) or `specularIBL` : that's
    // the standard, well-understood scope for SSAO, and it composes naturally with the additive floor
    // just above (a level with SSAO on but zero real lights/DDGI/IBL still gets its ambientColor
    // floor darkened in contact points, which is correct - occlusion blocks that flat "always there"
    // light too, physically). The 3 SSAO passes always run (see SSAOManager::Init) - ssaoEnabled is
    // purely a lit.frag-side gate, off is a no-op.
    // Debug views 7 (GI only) / 8 (SSAO only) - see ViewMode. GI shows the indirect light before SSAO
    // (diffuse + specular, no direct light/emission); SSAO shows the raw occlusion term, white = none.
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
    ambientDiffuse = SampleSSAO(ambientDiffuse);

    // Emission is light the surface itself gives off, so it goes on after everything else and is not
    // touched by SSAO, shadows or the material's albedo/metallic - it doesn't depend on any incoming light.
    vec3 lighting = result + ambientDiffuse + specularIBL + emission;
    // One non finite sample would spread over its neighbours in the multisample resolve :
    // it is dropped here, where the pixel is still a single sample
    lighting = clamp(lighting, vec3(0.0), vec3(60000.0));
    if (any(isnan(lighting)))
        lighting = vec3(0.0);
    if (outputDisplayColors)
        lighting = DisplayTransform(lighting, TONEMAP_ACES, 1.0);
    return vec4(lighting, baseColor.a);
}

// The debug views of what only the vertices know (the wireframes' line colour, the UVs, the lightmap UVs). The deferred pipeline writes this colour
// to the G-buffer's albedo, where the lighting finds it.
bool IsVertexDebugView() {
    return viewMode == 2 || viewMode == 3 || viewMode == 6 || viewMode == 9;
}

// The debug views of the surface (the albedo, the normals, the depth) : the same from the forward shader and from the G-buffer
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

    // The lightmap UVs as the bake sees them (atlas placement applied) : a checker shows texel density and
    // stretching, the colour is the UV itself. Meshes without a lightmap show their raw second UV set, dimmed.
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
        discard;        // nothing was drawn here : the background (the sky, drawn after the lighting) is not lit

    // The world position, from the depth the G-buffer pass wrote (OpenGL's window depth : 0..1, clip space z -1..1).
    // With sample shading gl_FragCoord is the position of the sample, which is where that depth was written
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

    // The debug views : the G-buffer pass wrote the colour of a view of the vertices in place of the albedo
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

    // The baked light this fragment receives, if its mesh has a lightmap (see lightmapEnabled)
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
