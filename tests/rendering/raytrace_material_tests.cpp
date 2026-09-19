#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "engine/rendering/material/material.hpp"
#include "engine/rendering/raytracing/raytrace_scene.hpp"

using namespace Shard::Engine::Rendering;
using namespace Shard::Engine::Rendering::Raytracing;

namespace {

    class FakeMaterial : public Material
    {
        public:
            void SetScalarParameter(const std::string& name, const NumericValue& value) override { scalars[name] = value; }
            void SetTextureParameter(const std::string& name, uint64_t texture) override { textures[name] = texture; }

            std::optional<NumericValue> GetScalarParameter(const std::string& name) override
            {
                auto it = scalars.find(name);
                if (it == scalars.end())
                    return std::nullopt;
                return it->second;
            }

            uint64_t GetTextureParameter(const std::string& name) const override
            {
                auto it = textures.find(name);
                return it == textures.end() ? 0 : it->second;
            }

            uint32_t GetTexturesCount() const override { return (uint32_t)textures.size(); }

            std::unordered_map<std::string, NumericValue> scalars;
            std::unordered_map<std::string, uint64_t> textures;
    };

}

TEST(RaytraceMaterial, StrideIsMultipleOfStd430ArrayAlignment)
{
    EXPECT_EQ(sizeof(GPUMaterial) % 16, 0u);
}

TEST(RaytraceMaterial, NullMaterialGivesDefaults)
{
    GPUMaterial gm = ExtractMaterial(nullptr);

    EXPECT_EQ(gm.textureFlags, 0u);
    EXPECT_EQ(gm.emissive, glm::vec4(0.0f));
    EXPECT_EQ(gm.emissiveTex, glm::uvec2(0));
}

TEST(RaytraceMaterial, EmissiveScalarVec3IsCopiedWithoutAnyTextureFlag)
{
    auto mat = std::make_shared<FakeMaterial>();
    mat->SetScalarParameter("emissive", glm::vec3(2.0f, 0.5f, 0.0f));

    GPUMaterial gm = ExtractMaterial(mat);

    EXPECT_EQ(gm.emissive, glm::vec4(2.0f, 0.5f, 0.0f, 1.0f));
    EXPECT_EQ(gm.textureFlags & GPUMaterialTexEmissive, 0u);
    EXPECT_EQ(gm.emissiveTex, glm::uvec2(0));
}

TEST(RaytraceMaterial, EmissiveMapSetsBitAndPacksHandle)
{
    auto mat = std::make_shared<FakeMaterial>();
    mat->SetTextureParameter("emissiveMap", 0xAABBCCDD11223344ull);

    GPUMaterial gm = ExtractMaterial(mat);

    EXPECT_NE(gm.textureFlags & GPUMaterialTexEmissive, 0u);
    EXPECT_EQ(gm.emissiveTex.x, 0x11223344u); // low 32 bits first, matching sampler2D(uvec2) on the GPU
    EXPECT_EQ(gm.emissiveTex.y, 0xAABBCCDDu);
}

TEST(RaytraceMaterial, EmissiveMapWithoutFactorStaysDark)
{
    auto mat = std::make_shared<FakeMaterial>();
    mat->SetTextureParameter("emissiveMap", 42);

    GPUMaterial gm = ExtractMaterial(mat);

    EXPECT_EQ(glm::vec3(gm.emissive), glm::vec3(0.0f));
}

TEST(RaytraceMaterial, EmissiveBitIsIndependentOfTheOtherTextureSlots)
{
    auto mat = std::make_shared<FakeMaterial>();
    mat->SetTextureParameter("albedo", 1);
    mat->SetTextureParameter("metallicMap", 2);
    mat->SetTextureParameter("roughnessMap", 3);
    mat->SetTextureParameter("normalMap", 4);

    GPUMaterial withoutEmissive = ExtractMaterial(mat);
    EXPECT_EQ(withoutEmissive.textureFlags,
        (uint32_t)(GPUMaterialTexAlbedo | GPUMaterialTexMetallic | GPUMaterialTexRoughness | GPUMaterialTexNormal));

    mat->SetTextureParameter("emissiveMap", 5);

    GPUMaterial withEmissive = ExtractMaterial(mat);
    EXPECT_EQ(withEmissive.textureFlags, withoutEmissive.textureFlags | GPUMaterialTexEmissive);
    EXPECT_EQ(withEmissive.emissiveTex, glm::uvec2(5, 0));
    // The other slots' handles must not have been disturbed by the new field.
    EXPECT_EQ(withEmissive.normalTex, glm::uvec2(4, 0));
}
