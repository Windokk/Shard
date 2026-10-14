#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "engine/renderer/rhi/shader_compiler/shader_compiler.hpp"

using namespace Shard::Engine::Rendering;
using namespace Shard::Engine::Rendering::ShaderCompiler;

namespace {

    const char* kVertex = R"(#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;
uniform mat4 model;
uniform mat4 uViewProjection;
out vec2 texCoord;
void main()
{
    texCoord = aTexCoord;
    gl_Position = uViewProjection * model * vec4(aPos, 1.0);
}
)";

    const char* kFragment = R"(#version 430 core
in vec2 texCoord;
out vec4 fragColor;
uniform sampler2D albedo;
uniform vec3 tint;
uniform float scale[3];
uniform bool useTint;
uniform int mode;
void main()
{
    vec3 c = texture(albedo, texCoord).rgb * scale[1];
    if (useTint)
        c *= tint;
    fragColor = vec4(c, float(mode));
}
)";

    Result Compile(const char* vertex, const char* fragment)
    {
        return CompileProgram({{ShaderStageKind::Vertex, vertex, "test.vert"}, {ShaderStageKind::Fragment, fragment, "test.frag"}});
    }
}

TEST(ShaderCompiler, CompilesAGlProgramForVulkan)
{
    Result result = Compile(kVertex, kFragment);
    ASSERT_TRUE(result.ok) << result.log;

    EXPECT_TRUE(result.binary.HasStage(ShaderStageKind::Vertex));
    EXPECT_TRUE(result.binary.HasStage(ShaderStageKind::Fragment));
    EXPECT_FALSE(result.binary.HasStage(ShaderStageKind::Geometry));
    EXPECT_FALSE(result.binary.IsCompute());

    // SPIR-V starts with its magic number
    EXPECT_EQ(result.binary.Stage(ShaderStageKind::Vertex)[0], 0x07230203u);
    EXPECT_EQ(result.binary.Stage(ShaderStageKind::Fragment)[0], 0x07230203u);
}

TEST(ShaderCompiler, TheLooseUniformsOfAllTheStagesShareOneBlock)
{
    Result result = Compile(kVertex, kFragment);
    ASSERT_TRUE(result.ok) << result.log;

    const ShaderReflection& reflection = result.binary.reflection;
    EXPECT_GT(reflection.uniformBlockSize, 0u);

    // Every uniform of the vertex stage and of the fragment stage is in the one block, at one offset
    for (const char* name : {"model", "uViewProjection", "tint", "scale[0]", "scale[2]", "useTint", "mode"})
        EXPECT_NE(reflection.FindUniform(name), nullptr) << name;

    const ReflectedUniform* model = reflection.FindUniform("model");
    const ReflectedUniform* viewProjection = reflection.FindUniform("uViewProjection");
    ASSERT_TRUE(model && viewProjection);
    EXPECT_EQ(model->type, ShaderDataType::Mat4);
    EXPECT_EQ(model->size, 64u);
    EXPECT_NE(model->offset, viewProjection->offset);

    // std140 : a float array has the stride of a vec4
    const ReflectedUniform* scale0 = reflection.FindUniform("scale[0]");
    const ReflectedUniform* scale1 = reflection.FindUniform("scale[1]");
    ASSERT_TRUE(scale0 && scale1);
    EXPECT_EQ(scale1->offset - scale0->offset, 16u);
    EXPECT_EQ(scale0->type, ShaderDataType::Float);

    EXPECT_EQ(reflection.FindUniform("tint")->type, ShaderDataType::Vec3);
    EXPECT_EQ(reflection.FindUniform("tint")->size, 12u);
    EXPECT_EQ(reflection.FindUniform("mode")->type, ShaderDataType::Int);

    // A bool stays a bool for whoever lists the uniforms, though the block stores it as an integer
    EXPECT_EQ(reflection.FindUniform("useTint")->type, ShaderDataType::Bool);

    // Every offset is inside the block
    for (const ReflectedUniform& uniform : reflection.uniforms)
        EXPECT_LE(uniform.offset + uniform.size, reflection.uniformBlockSize) << uniform.name;
}

TEST(ShaderCompiler, ASamplerGetsItsTextureUnitInTheTextureSet)
{
    Result result = Compile(kVertex, kFragment);
    ASSERT_TRUE(result.ok) << result.log;

    const ReflectedBinding* albedo = result.binary.reflection.FindBinding("albedo");
    ASSERT_NE(albedo, nullptr);
    EXPECT_EQ(albedo->kind, BindingKind::SampledImage);
    EXPECT_EQ(albedo->set, BindingSets::Textures);
    EXPECT_EQ(albedo->samplerType, ShaderSamplerType::Texture2D);
    EXPECT_EQ(albedo->stages, ShaderStageBit(ShaderStageKind::Fragment));

    // The uniform block is the binding 0 of its set, used by both stages
    bool foundBlock = false;
    for (const ReflectedBinding& binding : result.binary.reflection.bindings)
    {
        if (binding.kind == BindingKind::UniformBuffer)
        {
            foundBlock = true;
            EXPECT_EQ(binding.set, BindingSets::Uniforms);
            EXPECT_EQ(binding.binding, 0u);
            EXPECT_EQ(binding.stages, ShaderStageBit(ShaderStageKind::Vertex) | ShaderStageBit(ShaderStageKind::Fragment));
        }
    }
    EXPECT_TRUE(foundBlock);
}

TEST(ShaderCompiler, TheVertexInputsKeepTheirLocations)
{
    Result result = Compile(kVertex, kFragment);
    ASSERT_TRUE(result.ok) << result.log;

    const auto& inputs = result.binary.reflection.vertexInputs;
    ASSERT_EQ(inputs.size(), 2u);

    bool position = false, uv = false;
    for (const ReflectedVertexInput& input : inputs)
    {
        if (input.name == "aPos")      { position = true; EXPECT_EQ(input.location, 0u); EXPECT_EQ(input.type, ShaderDataType::Vec3); }
        if (input.name == "aTexCoord") { uv = true;       EXPECT_EQ(input.location, 1u); EXPECT_EQ(input.type, ShaderDataType::Vec2); }
    }
    EXPECT_TRUE(position && uv);
}

TEST(ShaderCompiler, EachKindOfResourceLivesInTheSetOfItsKind)
{
    const char* compute = R"(#version 430 core
layout(local_size_x = 8, local_size_y = 4, local_size_z = 1) in;
layout(binding = 0) uniform sampler2D uSource;
layout(rgba16f, binding = 0) writeonly uniform image2D uOut;
layout(std430, binding = 0) readonly buffer Input { vec4 values[]; };
layout(std430, binding = 3) buffer Counter { uint count; };
uniform int offsetX;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    imageStore(uOut, p, texelFetch(uSource, p, 0) + values[p.x + offsetX]);
    atomicAdd(count, 1u);
}
)";

    // The same binding number in three kinds : what a GL driver keeps apart by kind
    Result result = CompileProgram({{ShaderStageKind::Compute, compute, "test.comp"}});
    ASSERT_TRUE(result.ok) << result.log;

    const ShaderReflection& reflection = result.binary.reflection;
    EXPECT_TRUE(result.binary.IsCompute());
    EXPECT_EQ(reflection.localSize[0], 8u);
    EXPECT_EQ(reflection.localSize[1], 4u);
    EXPECT_EQ(reflection.localSize[2], 1u);

    const ReflectedBinding* sampled = reflection.FindBinding("uSource");
    const ReflectedBinding* image = reflection.FindBinding("uOut");
    const ReflectedBinding* input = reflection.FindBinding("Input");
    const ReflectedBinding* counter = reflection.FindBinding("Counter");
    ASSERT_TRUE(sampled && image && input && counter);

    EXPECT_EQ(sampled->set, BindingSets::Textures);
    EXPECT_EQ(sampled->binding, 0u);
    EXPECT_EQ(image->set, BindingSets::StorageImages);
    EXPECT_EQ(image->binding, 0u);
    EXPECT_TRUE(image->writeOnly);
    EXPECT_EQ(input->set, BindingSets::StorageBuffers);
    EXPECT_EQ(input->binding, 0u);
    EXPECT_TRUE(input->readOnly);
    EXPECT_EQ(counter->set, BindingSets::StorageBuffers);
    EXPECT_EQ(counter->binding, 3u);
    EXPECT_FALSE(counter->readOnly);

    EXPECT_NE(reflection.FindUniform("offsetX"), nullptr);
}

TEST(ShaderCompiler, ABindlessHandleReadsTheGlobalTextureArray)
{
    const char* fragment = R"(#version 430 core
#extension GL_ARB_bindless_texture : enable
in vec2 texCoord;
out vec4 fragColor;
uniform uvec2 lightmapHandle;
void main()
{
    fragColor = texture(sampler2D(lightmapHandle), texCoord);
}
)";

    Result result = Compile(kVertex, fragment);
    ASSERT_TRUE(result.ok) << result.log;

    const ReflectedBinding* bindless = nullptr;
    for (const ReflectedBinding& binding : result.binary.reflection.bindings)
    {
        if (binding.kind == BindingKind::BindlessTextures)
            bindless = &binding;
    }
    ASSERT_NE(bindless, nullptr);
    EXPECT_EQ(bindless->set, BindingSets::Bindless);
    EXPECT_EQ(bindless->binding, 0u);
    EXPECT_EQ(bindless->count, 0u);

    EXPECT_EQ(result.binary.reflection.FindUniform("lightmapHandle")->type, ShaderDataType::UVec2);
}

TEST(ShaderCompiler, DefinesMakeAPermutation)
{
    const char* fragment = R"(#version 430 core
in vec2 texCoord;
out vec4 fragColor;
void main()
{
#ifdef FANCY
    uniform_removed_by_the_preprocessor_when_not_defined
#endif
    fragColor = vec4(texCoord, 0.0, 1.0);
}
)";

    // Without the define the line is skipped and the program is fine ; with it, it is an error
    EXPECT_TRUE(Compile(kVertex, fragment).ok);

    Options options;
    options.defines.push_back({"FANCY", "1"});
    Result with = CompileProgram({{ShaderStageKind::Vertex, kVertex, "test.vert"}, {ShaderStageKind::Fragment, fragment, "test.frag"}}, options);
    EXPECT_FALSE(with.ok);

    // Another set of defines is another binary
    Options other;
    other.defines.push_back({"OTHER", "2"});
    const std::vector<StageSource> stages = {{ShaderStageKind::Vertex, kVertex, "test.vert"}, {ShaderStageKind::Fragment, kFragment, "test.frag"}};
    EXPECT_NE(HashSources(stages, Options{}), HashSources(stages, other));
    EXPECT_EQ(HashSources(stages, Options{}), HashSources(stages, Options{}));
}

TEST(ShaderCompiler, ASyntaxErrorIsReportedWithItsFile)
{
    Result result = Compile(kVertex, "#version 430 core\nvoid main() { this is not glsl }\n");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.log.find("test.frag"), std::string::npos) << result.log;
}

TEST(ShaderCompiler, ACompiledProgramSurvivesItsFile)
{
    Result result = Compile(kVertex, kFragment);
    ASSERT_TRUE(result.ok) << result.log;

    const std::vector<uint8_t> bytes = SerializeShaderBinary(result.binary);

    ShaderBinary loaded;
    std::string error;
    ASSERT_TRUE(DeserializeShaderBinary(bytes.data(), bytes.size(), loaded, &error)) << error;

    EXPECT_EQ(loaded.sourceHash, result.binary.sourceHash);
    EXPECT_EQ(loaded.Stage(ShaderStageKind::Vertex), result.binary.Stage(ShaderStageKind::Vertex));
    EXPECT_EQ(loaded.Stage(ShaderStageKind::Fragment), result.binary.Stage(ShaderStageKind::Fragment));
    EXPECT_EQ(loaded.reflection.uniformBlockSize, result.binary.reflection.uniformBlockSize);
    EXPECT_EQ(loaded.reflection.uniforms.size(), result.binary.reflection.uniforms.size());
    EXPECT_EQ(loaded.reflection.bindings.size(), result.binary.reflection.bindings.size());
    ASSERT_NE(loaded.reflection.FindUniform("useTint"), nullptr);
    EXPECT_EQ(loaded.reflection.FindUniform("useTint")->type, ShaderDataType::Bool);

    // Garbage is refused, not read
    ShaderBinary refused;
    EXPECT_FALSE(DeserializeShaderBinary(bytes.data(), 3, refused));
    std::vector<uint8_t> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
    EXPECT_FALSE(DeserializeShaderBinary(truncated.data(), truncated.size(), refused));
}

TEST(ShaderCompiler, EveryShaderOfTheEngineCompiles)
{
    const std::filesystem::path root = std::filesystem::path(SHARD_SOURCE_DIR) / "resources" / "engine_resources" / "shaders";
    ASSERT_TRUE(std::filesystem::exists(root)) << root;

    const std::vector<ProgramFiles> programs = FindPrograms(root);
    ASSERT_GT(programs.size(), 30u);

    for (const ProgramFiles& program : programs)
    {
        // A Slang program needs slangc (the Vulkan SDK has it)
        const ShaderStageKind first = program.Has(ShaderStageKind::Compute) ? ShaderStageKind::Compute : ShaderStageKind::Vertex;
        if (LanguageOfFile(program.stages[static_cast<size_t>(first)]) == SourceLanguage::Slang && FindSlangCompiler().empty())
            continue;

        Result result = CompileProgramFiles(program);
        EXPECT_TRUE(result.ok) << program.name << "\n" << result.log;
    }
}

// ---- Slang ------------------------------------------------------------------------------------------------------------------------

namespace {

    // The uniforms the engine sets by name are the members of the constant buffer at set 3, binding 0 ; a texture is one descriptor with
    // its sampler (Sampler2D)
    const char* kSlang = R"(
struct VSInput  { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };

[[vk::binding(0, 3)]] cbuffer Uniforms
{
    float4x4 model;
    float4x4 uViewProjection;
    float3 tint;
    float scale;
};

[[vk::binding(2, 0)]] Sampler2D albedo;

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    output.position = mul(uViewProjection, mul(model, float4(input.pos, 1.0)));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(albedo.Sample(input.uv).rgb * tint * scale, 1.0);
}
)";

    const char* kSlangCompute = R"(
[[vk::binding(0, 1)]] RWStructuredBuffer<float> values;

[shader("compute")]
[numthreads(8, 4, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    values[id.x] = values[id.x] * 2.0;
}
)";

    void ExpectTheUniformsAndTheTexture(const ShaderReflection& reflection)
    {
        for (const char* name : {"model", "uViewProjection", "tint", "scale"})
            EXPECT_NE(reflection.FindUniform(name), nullptr) << name;

        const ReflectedUniform* tint = reflection.FindUniform("tint");
        ASSERT_NE(tint, nullptr);
        EXPECT_EQ(tint->type, ShaderDataType::Vec3);

        const ReflectedUniform* model = reflection.FindUniform("model");
        ASSERT_NE(model, nullptr);
        EXPECT_EQ(model->type, ShaderDataType::Mat4);

        const ReflectedBinding* texture = reflection.FindBinding("albedo");
        ASSERT_NE(texture, nullptr);
        EXPECT_EQ(texture->kind, BindingKind::SampledImage);
        EXPECT_EQ(texture->set, BindingSets::Textures);
        EXPECT_EQ(texture->binding, 2u);

        EXPECT_FALSE(reflection.vertexInputs.empty());
    }

    /// A Slang stage as the Vulkan backend gives it : the one file is each of the stages
    StageSource SlangStage(ShaderStageKind kind, const std::filesystem::path& file)
    {
        StageSource stage{kind, kSlang, file.generic_string()};
        stage.language = SourceLanguage::Slang;
        stage.file = file;
        return stage;
    }
}

TEST(ShaderCompiler, ATextureWithoutItsSamplerInTheSameDescriptorIsRefused)
{
    // A texture that is not paired with a sampler is a descriptor the engine has no way to bind
    const std::string fragment = R"(#version 450
#extension GL_EXT_samplerless_texture_functions : require
layout(set = 0, binding = 0) uniform texture2D albedo;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() { color = texelFetch(albedo, ivec2(uv), 0); }
)";

    Result result = Compile(kVertex, fragment.c_str());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.log.find("one descriptor"), std::string::npos) << result.log;
}

TEST(ShaderCompiler, TheLanguageIsTheExtensionOfTheFile)
{
    EXPECT_EQ(LanguageOfFile("a/b.vert"), SourceLanguage::GLSL);
    EXPECT_EQ(LanguageOfFile("a/b.comp"), SourceLanguage::GLSL);
    EXPECT_EQ(LanguageOfFile("a/b.slang"), SourceLanguage::Slang);
    EXPECT_STREQ(DefaultEntryPoint(SourceLanguage::Slang, ShaderStageKind::Fragment), "fragmentMain");
    EXPECT_STREQ(DefaultEntryPoint(SourceLanguage::GLSL, ShaderStageKind::Fragment), "main");
}

TEST(ShaderCompiler, ASlangFileIsOneProgramWithTheStagesItHasEntryPointsFor)
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "shard_shader_language_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sub");

    auto write = [](const std::filesystem::path& path, const char* text) { std::ofstream(path, std::ios::binary) << text; };
    write(root / "sub" / "tint.slang", kSlang);
    write(root / "scale.slang", kSlangCompute);

    const std::vector<ProgramFiles> programs = FindPrograms(root);
    ASSERT_EQ(programs.size(), 2u);

    for (const ProgramFiles& program : programs)
    {
        if (program.name == "sub/tint")
        {
            // The one file is the vertex stage and the fragment stage
            EXPECT_TRUE(program.Has(ShaderStageKind::Vertex) && program.Has(ShaderStageKind::Fragment));
            EXPECT_FALSE(program.Has(ShaderStageKind::Compute));
            EXPECT_EQ(program.stages[static_cast<size_t>(ShaderStageKind::Vertex)], root / "sub" / "tint.slang");
        }
        else
        {
            EXPECT_EQ(program.name, "scale");
            EXPECT_TRUE(program.Has(ShaderStageKind::Compute));
            EXPECT_FALSE(program.Has(ShaderStageKind::Vertex));
        }
    }

    std::filesystem::remove_all(root);
}

TEST(ShaderCompiler, ASlangProgramCompilesThroughSlangc)
{
    if (FindSlangCompiler().empty())
        GTEST_SKIP() << "slangc is not installed (the Vulkan SDK has it)";

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "shard_slang_test.slang";
    std::ofstream(file, std::ios::binary) << kSlang;

    Result result = CompileProgram({SlangStage(ShaderStageKind::Vertex, file), SlangStage(ShaderStageKind::Fragment, file)});
    std::filesystem::remove(file);

    ASSERT_TRUE(result.ok) << result.log;
    EXPECT_EQ(result.binary.Stage(ShaderStageKind::Vertex)[0], 0x07230203u);
    EXPECT_EQ(result.binary.Stage(ShaderStageKind::Fragment)[0], 0x07230203u);
    ExpectTheUniformsAndTheTexture(result.binary.reflection);
    EXPECT_GT(result.binary.reflection.uniformBlockSize, 0u);
}

TEST(ShaderCompiler, ASlangComputeShaderKeepsItsLocalSizeAndItsBufferSet)
{
    if (FindSlangCompiler().empty())
        GTEST_SKIP() << "slangc is not installed (the Vulkan SDK has it)";

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "shard_slang_compute_test.slang";
    std::ofstream(file, std::ios::binary) << kSlangCompute;

    StageSource stage{ShaderStageKind::Compute, kSlangCompute, file.generic_string()};
    stage.language = SourceLanguage::Slang;
    stage.file = file;

    Result result = CompileProgram({stage});
    std::filesystem::remove(file);

    ASSERT_TRUE(result.ok) << result.log;
    EXPECT_TRUE(result.binary.IsCompute());
    EXPECT_EQ(result.binary.reflection.localSize[0], 8u);
    EXPECT_EQ(result.binary.reflection.localSize[1], 4u);

    const ReflectedBinding* values = result.binary.reflection.FindBinding("values");
    ASSERT_NE(values, nullptr);
    EXPECT_EQ(values->kind, BindingKind::StorageBuffer);
    EXPECT_EQ(values->set, BindingSets::StorageBuffers);
}

TEST(ShaderCompiler, ASlangErrorIsReportedWithItsFile)
{
    if (FindSlangCompiler().empty())
        GTEST_SKIP() << "slangc is not installed (the Vulkan SDK has it)";

    const std::filesystem::path file = std::filesystem::temp_directory_path() / "shard_slang_error_test.slang";
    const char* broken = "[shader(\"vertex\")] float4 vertexMain() : SV_Position { return notDeclared; }";
    std::ofstream(file, std::ios::binary) << broken;

    StageSource stage{ShaderStageKind::Vertex, broken, file.generic_string()};
    stage.language = SourceLanguage::Slang;
    stage.file = file;

    Result result = CompileProgram({stage});
    std::filesystem::remove(file);

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.log.find("notDeclared"), std::string::npos) << result.log;
}
