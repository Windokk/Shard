#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "engine/renderer/rhi/lighting_terms.hpp"
#include "engine/renderer/rhi/renderer_api.hpp"

using namespace Shard::Engine::Rendering;

TEST(LightingTerms, BitsMatchTheShader) {
    // light.glsl's LIGHTING_TERM_* : the forward pass and the path tracer read these bits
    EXPECT_EQ(LightingTermDirect, 1u);
    EXPECT_EQ(LightingTermIndirectDiffuse, 2u);
    EXPECT_EQ(LightingTermIndirectSpecular, 4u);
    EXPECT_EQ(LightingTermEmission, 8u);
    EXPECT_EQ(LightingTermsAll, 15u);

    // The path tracer's term images are in the order of the bits
    for (uint32_t i = 0; i < 4; i++)
        EXPECT_EQ(kLightingTerms[i], 1u << i);
}

TEST(LightingTerms, ParsesNamesAndCombinations) {
    uint32_t terms = 0;
    ASSERT_TRUE(ParseLightingTerms("all", terms));
    EXPECT_EQ(terms, LightingTermsAll);
    ASSERT_TRUE(ParseLightingTerms("none", terms));
    EXPECT_EQ(terms, 0u);
    ASSERT_TRUE(ParseLightingTerms("direct", terms));
    EXPECT_EQ(terms, LightingTermDirect);
    ASSERT_TRUE(ParseLightingTerms("Indirect_Diffuse + emission", terms));
    EXPECT_EQ(terms, LightingTermIndirectDiffuse | LightingTermEmission);
    ASSERT_TRUE(ParseLightingTerms("indirect_specular,direct", terms));
    EXPECT_EQ(terms, LightingTermIndirectSpecular | LightingTermDirect);
}

TEST(LightingTerms, RejectsUnknownNamesWithoutTouchingTheValue) {
    uint32_t terms = LightingTermDirect;
    EXPECT_FALSE(ParseLightingTerms("", terms));
    EXPECT_FALSE(ParseLightingTerms("diffuse", terms));
    EXPECT_FALSE(ParseLightingTerms("direct+", terms));
    EXPECT_FALSE(ParseLightingTerms("direct+bloom", terms));
    EXPECT_EQ(terms, LightingTermDirect);
}

TEST(LightingTerms, FormatsBackToWhatParses) {
    EXPECT_EQ(FormatLightingTerms(LightingTermsAll), "all");
    EXPECT_EQ(FormatLightingTerms(0), "none");
    EXPECT_EQ(FormatLightingTerms(LightingTermEmission | LightingTermDirect), "direct+emission");

    for (uint32_t mask = 0; mask <= LightingTermsAll; mask++)
    {
        uint32_t parsed = 0xFFu;
        ASSERT_TRUE(ParseLightingTerms(FormatLightingTerms(mask), parsed)) << mask;
        EXPECT_EQ(parsed, mask);
    }
}

TEST(LightingTerms, DebugViewDrawsEveryTermByDefault) {
    // The passes that are not the main view (thumbnails, probes) use a default DebugViewState : they must stay fully lit
    EXPECT_EQ(DebugViewState{}.lightingTerms, LightingTermsAll);
}
