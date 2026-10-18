#include <gtest/gtest.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/renderer/rhi/backends/null/null_rhi.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/pipelines/compute_pipeline.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"
#include "engine/renderer/rhi/resources/buffer/storage_buffer.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/cubemap.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"
#include "engine/renderer/rhi/shader/compute_shader.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"

using namespace Shard::Engine::Rendering;

namespace {
    // The null backend as the API the rhi factories pick from, for the length of a test
    class NullBackend : public ::testing::Test {
        protected:
            void SetUp() override {
                api = RendererAPI::Create(RendererAPI::API::Null);
                previous = RendererAPI::Current();
                RendererAPI::SetCurrent(api.get());
            }
            void TearDown() override { RendererAPI::SetCurrent(previous); }

            std::shared_ptr<RendererAPI> api;
            RendererAPI* previous = nullptr;
    };

    VertexLayout PositionLayout() {
        VertexElement position;
        position.name = "aPos";
        position.type = ShaderDataType::Vec3;
        position.location = 0;
        position.offset = 0;
        return VertexLayout({position}, sizeof(float) * 3);
    }
}

TEST_F(NullBackend, TheApiIsTheNullOne) {
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->GetAPI(), RendererAPI::API::Null);
    EXPECT_GT(api->GetComputeLimits().maxWorkGroupInvocations, 0u);
    EXPECT_FALSE(api->SupportsBlockCompression());
    EXPECT_EQ(api->GetGpuProfiler(), nullptr);

    api->SetClearColor(0.1f, 0.2f, 0.3f, 0.4f);
    EXPECT_EQ(api->GetClearColor(), glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
}

TEST_F(NullBackend, AMeshKeepsItsGeometryAndBounds) {
    const float vertices[] = {-1.0f, 0.0f, 2.0f,  3.0f, 4.0f, -5.0f,  0.0f, 1.0f, 0.0f};
    std::vector<uint8_t> bytes(sizeof(vertices));
    std::memcpy(bytes.data(), vertices, sizeof(vertices));

    std::shared_ptr<Mesh> mesh = Mesh::Create();
    ASSERT_NE(mesh, nullptr);
    mesh->Create(bytes, {0, 1, 2}, PositionLayout());

    EXPECT_EQ(mesh->GetVertexCount(), 3u);
    EXPECT_EQ(mesh->GetIndexCount(), 3u);
    EXPECT_EQ(mesh->SubMeshesCount(), 1);
    EXPECT_EQ(mesh->GetBoundsMin(), glm::vec3(-1.0f, 0.0f, -5.0f));
    EXPECT_EQ(mesh->GetBoundsMax(), glm::vec3(3.0f, 4.0f, 2.0f));

    // Nothing to read back from : the geometry never left the CPU, so it is simply there
    std::shared_ptr<const MeshCPUCopy> copy = mesh->AcquireCPUData();
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->vertices, bytes);
    EXPECT_EQ(copy->indices, (std::vector<uint32_t>{0, 1, 2}));

    // The draw commands of a mesh work as they do on a GPU
    std::shared_ptr<Shader> shader = Shader::Create(Shard::Engine::Filesystem::Path("a.vert"), Shard::Engine::Filesystem::Path("a.frag"));
    PipelineSpecifications specs;
    specs.shader = shader;
    std::shared_ptr<Material> material = Material::Create(shader, std::make_shared<Null::NullPipeline>(specs));
    EXPECT_EQ(mesh->CreateDrawCommands(glm::mat4(1.0f), 1, 1, {material}).size(), 1u);

    // An unresolved material slot draws nothing
    EXPECT_TRUE(mesh->CreateDrawCommands(glm::mat4(1.0f), 1, 1, {nullptr}).empty());
}

TEST_F(NullBackend, TexturesFramebuffersAndBuffersExist) {
    TextureSpecifications specs;
    specs.width = 4;
    specs.height = 2;
    specs.internalFormat = TextureInternalFormat::RGBA8;
    const uint8_t pixels[4 * 2 * 4] = {};
    std::shared_ptr<Texture2D> texture = Texture2D::Create(specs, pixels);
    ASSERT_NE(texture, nullptr);
    EXPECT_NE(texture->GetHandle(), 0u) << "0 means \"no texture\" to the code above";
    EXPECT_EQ(texture->GetWidth(), 4u);
    EXPECT_TRUE(texture->IsValid());
    std::vector<uint8_t> back(32, 0xAA);
    texture->ReadPixels(back.data(), back.size());
    EXPECT_EQ(back, std::vector<uint8_t>(32, 0));

    FramebufferSpecifications fbSpecs;
    fbSpecs.width = 64;
    fbSpecs.height = 32;
    std::shared_ptr<Framebuffer> fb = Framebuffer::Create(fbSpecs);
    ASSERT_NE(fb, nullptr);
    EXPECT_TRUE(fb->IsValid());
    EXPECT_NE(fb->GetColorAttachment(), 0u);
    EXPECT_NE(fb->GetColorAttachment(), fb->GetDepthAttachment());
    fb->Resize(128, 16);
    EXPECT_EQ(fb->GetWidth(), 128u);
    std::vector<uint8_t> picture;
    ASSERT_TRUE(fb->ReadPixelsRGBA8(picture));
    EXPECT_EQ(picture.size(), 128u * 16u * 4u);

    std::shared_ptr<StorageBuffer> buffer = StorageBuffer::Create(16);
    ASSERT_NE(buffer, nullptr);
    const uint32_t data[2] = {7, 9};
    buffer->SetData(data, sizeof(data));
    uint32_t out[2] = {};
    buffer->GetData(out, sizeof(out));
    EXPECT_EQ(out[0], 7u);
    EXPECT_EQ(out[1], 9u);
    EXPECT_EQ(buffer->GetSize(), 8u);

    TextureSpecifications faceSpecs;
    faceSpecs.width = faceSpecs.height = 8;
    unsigned char face[8 * 8 * 4] = {};
    std::shared_ptr<Cubemap> cubemap = Cubemap::Create(faceSpecs, {face, face, face, face, face, face});
    ASSERT_NE(cubemap, nullptr);
    EXPECT_EQ(cubemap->GetWidth(), 8u);
}

TEST_F(NullBackend, AMaterialKeepsEveryParameterItIsGiven) {
    std::shared_ptr<Shader> shader = Shader::Create(Shard::Engine::Filesystem::Path("a.vert"), Shard::Engine::Filesystem::Path("a.frag"));
    ASSERT_NE(shader, nullptr);
    shader->SetAssetID(Shard::Engine::Filesystem::AssetID(5));
    EXPECT_EQ(shader->GetVertexShaderName(), "a.vert");
    EXPECT_FALSE(shader->SupportsDepthPrepass());

    PipelineSpecifications specs;
    specs.shader = shader;
    std::shared_ptr<Pipeline> pipeline = std::make_shared<Null::NullPipeline>(specs);

    std::shared_ptr<Material> material = Material::Create(shader, pipeline, true, Opacity::Translucent);
    ASSERT_NE(material, nullptr);
    EXPECT_TRUE(pipeline->GetSpecifications().blending) << "a translucent material blends, on every backend";

    material->SetScalarParameter("roughness", 0.25f);
    material->SetTextureParameter("albedo", 12);
    ASSERT_TRUE(material->GetScalarParameter("roughness").has_value());
    EXPECT_EQ(std::get<float>(*material->GetScalarParameter("roughness")), 0.25f);
    EXPECT_FALSE(material->GetScalarParameter("metallic").has_value());
    EXPECT_EQ(material->GetTexturesCount(), 1u);
    EXPECT_EQ(material->GetTextureParameter("albedo"), 12u);
}

TEST_F(NullBackend, PipelineCacheKeysOnTheShaderObjectNotItsAssetID) {
    // Shaders made from files directly (the deferred lighting, the tonemap, every compute shader) all have the AssetID 0
    std::shared_ptr<Shader> a = Shader::Create(Shard::Engine::Filesystem::Path("a.vert"), Shard::Engine::Filesystem::Path("a.frag"));
    std::shared_ptr<Shader> b = Shader::Create(Shard::Engine::Filesystem::Path("b.vert"), Shard::Engine::Filesystem::Path("b.frag"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(a->GetAssetID().GetAsInt(), b->GetAssetID().GetAsInt());

    PipelineSpecifications specsA;
    specsA.shader = a;
    PipelineSpecifications specsB = specsA;
    specsB.shader = b;
    PipelineSpecifications specsA2 = specsA;

    EXPECT_FALSE(specsA == specsB) << "two shaders must not share a cached pipeline";
    EXPECT_TRUE(specsA == specsA2);
    EXPECT_EQ(PipelineSpecsHash{}(specsA), PipelineSpecsHash{}(specsA2));

    std::unordered_map<PipelineSpecifications, std::shared_ptr<Pipeline>, PipelineSpecsHash> cache;
    cache.emplace(specsA, std::make_shared<Null::NullPipeline>(specsA));
    cache.emplace(specsB, std::make_shared<Null::NullPipeline>(specsB));
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(cache.at(specsB)->GetSpecifications().shader, b);

    std::shared_ptr<ComputeShader> c = ComputeShader::Create(Shard::Engine::Filesystem::Path("c.comp"));
    std::shared_ptr<ComputeShader> d = ComputeShader::Create(Shard::Engine::Filesystem::Path("d.comp"));
    ASSERT_NE(c, nullptr);
    ASSERT_NE(d, nullptr);
    ComputePipelineSpecifications computeC{c};
    ComputePipelineSpecifications computeC2{c};
    ComputePipelineSpecifications computeD{d};
    EXPECT_TRUE(computeC == computeC2) << "the same compute shader asked twice must hit the cache";
    EXPECT_EQ(ComputePipelineSpecsHash{}(computeC), ComputePipelineSpecsHash{}(computeC2));
    EXPECT_FALSE(computeC == computeD);
}

TEST_F(NullBackend, PipelineCachePruneDropsOnlyWhatNothingElseHolds) {
    std::shared_ptr<Shader> kept = Shader::Create(Shard::Engine::Filesystem::Path("a.vert"), Shard::Engine::Filesystem::Path("a.frag"));
    std::shared_ptr<Shader> dropped = Shader::Create(Shard::Engine::Filesystem::Path("b.vert"), Shard::Engine::Filesystem::Path("b.frag"));
    std::weak_ptr<Shader> droppedWatch = dropped;

    PipelineSpecifications keptSpecs;
    keptSpecs.shader = kept;
    PipelineSpecifications droppedSpecs;
    droppedSpecs.shader = dropped;

    std::unordered_map<PipelineSpecifications, std::shared_ptr<Pipeline>, PipelineSpecsHash> cache;
    std::shared_ptr<Pipeline> inUse = std::make_shared<Null::NullPipeline>(keptSpecs);
    cache.emplace(keptSpecs, inUse);
    cache.emplace(droppedSpecs, std::make_shared<Null::NullPipeline>(droppedSpecs));
    cache.emplace(PipelineSpecifications{}, nullptr); // an entry being filled in
    droppedSpecs = {};
    dropped.reset();

    PruneUnusedPipelines(cache);

    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(cache.count(keptSpecs), 1u);
    EXPECT_TRUE(droppedWatch.expired()) << "the pruned pipeline released its shader";
}

TEST_F(NullBackend, HandlesAreNeverZeroAndNeverRepeat) {
    const uint32_t a = Null::NextHandle();
    const uint32_t b = Null::NextHandle();
    EXPECT_NE(a, 0u);
    EXPECT_NE(a, b);
}
