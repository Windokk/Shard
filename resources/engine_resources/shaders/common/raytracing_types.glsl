// Split from raytracing_trace.glsl: the includer declares its own SSBOs (bvhNodes, triPos, triAttrib, materials) between the two includes, with its own binding indices.
struct BVHNode
{
    vec3 boundsMin;
    uint leftFirst;
    vec3 boundsMax;
    uint triCount;
};

struct TrianglePos
{
    vec4 v0, v1, v2;
};

struct TriangleAttrib
{
    vec4 n0, n1, n2;
    vec4 uvMatID;
    vec4 t0, t1, t2;
};

// Texture handles are ARB_bindless_texture handles packed as uvec2. A bit in textureFlags is set only if the material has that texture.
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
    uint flags;
    uint padding;
};

const uint MAT_DOUBLE_SIDED_BIT = 1u;

const uint TEX_ALBEDO_BIT    = 1u;
const uint TEX_METALLIC_BIT  = 2u;
const uint TEX_ROUGHNESS_BIT = 4u;
const uint TEX_NORMAL_BIT    = 8u;
const uint TEX_EMISSIVE_BIT  = 16u;

struct EmissiveTriangle
{
    uint tri;
    float cdf;
    float padding0;
    float padding1;
};

struct HitInfo
{
    float t;
    uint triIndex;
    vec2 bary;
};
