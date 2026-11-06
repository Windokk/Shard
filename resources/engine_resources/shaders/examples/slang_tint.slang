// A shader written in Slang (https://shader-slang.org) : a textured surface mixed with a tint, drawn the way mesh/unlit is.
//
// Slang is for the Vulkan backend only (the OpenGL one reads GLSL). shaderc compiles this file with slangc (the Vulkan SDK has it) into
// shaders_spv/examples/slang_tint.spvpack ; Shader::Create(path, path) on the Vulkan backend loads that, or has slangc make it. The conventions
// that the GLSL gets from the engine's shader compiler are written by hand here :
//
//   - the values the engine sets by name (Shader::SetFloat, a material's uniforms, the camera : model, uView, uProjection) are the members of the
//     constant buffer at set 3, binding 0 ; it is column major, like glm's matrices
//   - a texture is a Sampler2D : the texture and its sampler are one descriptor. Material textures are found by name, at any binding of set 0
//   - the entry points are vertexMain and fragmentMain (and computeMain for a compute shader), the vertex inputs have the locations
//     the engine's meshes use : 0 position, 1 texture coordinates, 2 normal, 3 colour

struct VSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
    float3 normal   : NORMAL;
    float4 color    : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 color    : COLOR;
};

[[vk::binding(0, 3)]] cbuffer Uniforms
{
    float4x4 model;
    float4x4 uView;
    float4x4 uProjection;
    float4 tint;            // what the surface is mixed with
    float tintAmount;       // 0 : the texture alone, 1 : the tint alone
};

[[vk::binding(0, 0)]] Sampler2D diffuse;

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    output.position = mul(uProjection, mul(uView, mul(model, float4(input.position, 1.0))));
    output.texCoord = input.texCoord;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float4 surface = diffuse.Sample(input.texCoord);
    return float4(lerp(surface.rgb, tint.rgb, tintAmount), surface.a);
}
