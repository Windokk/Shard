// Struct/constant layer of the shared BVH ray-tracing code, split from raytracing_trace.glsl (the
// actual traversal functions) because of a genuine ordering constraint : the traversal functions
// reference global SSBO arrays (bvhNodes[], triPos[], triAttrib[]) by name rather than taking them as
// parameters, so each including file must declare its own `layout(std430, binding = N) buffer ...`
// blocks - using THESE struct types, and THESE exact array names - in between the two includes :
//
//     #include ".../common/raytracing_types.glsl"
//     layout(std430, binding = N) readonly buffer BVHNodes { BVHNode bvhNodes[]; };
//     layout(std430, binding = N+1) readonly buffer TrianglePositions { TrianglePos triPos[]; };
//     layout(std430, binding = N+2) readonly buffer TriangleAttribs { TriangleAttrib triAttrib[]; };
//     #include ".../common/raytracing_trace.glsl"
//
// Binding indices legitimately differ per consumer (compute/path_trace.comp uses 0-2, compute/probes/
// probe_trace.comp uses 8-10, to stay clear of mesh/lit.frag's own SSBO bindings - see the comment in
// probe_trace.comp) which is exactly why the SSBO declarations themselves aren't part of this shared
// header.
struct BVHNode
{
    vec3 boundsMin;
    uint leftFirst; // internal node : left child index (right = leftFirst + 1) - leaf : first triangle index
    vec3 boundsMax;
    uint triCount;  // 0 = internal node, > 0 = leaf
};

struct TrianglePos
{
    vec4 v0, v1, v2; // xyz = world-space position
};

struct TriangleAttrib
{
    vec4 n0, n1, n2;  // xyz = world-space normal, w = u (texcoord) for that vertex
    vec4 uvMatID;     // x/y/z = v (texcoord) for vertices 0/1/2, w = material index
    vec4 t0, t1, t2;  // xyz = world-space tangent, w unused - only read when TEX_NORMAL_BIT is set
};

// Texture handles are ARB_bindless_texture handles packed as uvec2 (see GLTexture2D::GetBindlessHandle /
// raytrace_scene.hpp) - sampler2D(uvec2) reconstructs a usable sampler from one of these directly, no
// GL_ARB_gpu_shader_int64 needed. A bit is only set in textureFlags when the material actually has that
// texture assigned (see ExtractMaterial), so materials without one just keep using the scalar value.
struct GPUMaterial
{
    vec4 albedo;
    vec4 emissive;
    float roughness;
    float metallic;
    float ior;
    uint textureFlags;
    uvec2 albedoTex;
    uvec2 metallicTex;
    uvec2 roughnessTex;
    uvec2 normalTex;
    uvec2 emissiveTex;
    uvec2 padding; // rounds the struct to 96 bytes, the std430 array stride - keeps this in step with the
                   // C++ GPUMaterial (raytrace_scene.hpp), which spells the same padding out
};

const uint TEX_ALBEDO_BIT    = 1u;
const uint TEX_METALLIC_BIT  = 2u;
const uint TEX_ROUGHNESS_BIT = 4u;
const uint TEX_NORMAL_BIT    = 8u;
const uint TEX_EMISSIVE_BIT  = 16u;

struct HitInfo
{
    float t;
    uint triIndex;
    vec2 bary; // barycentric (u, v) - w = 1 - u - v
};
