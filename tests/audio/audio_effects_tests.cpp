#include <gtest/gtest.h>

#include "engine/audio/audio_effects.hpp"

using namespace Shard::Engine::Audio;

namespace {

    ReverbParams WithWet(float wet, float roomSize)
    {
        ReverbParams p;
        p.wetLevel = wet;
        p.roomSize = roomSize;
        return p;
    }
}

TEST(Doppler, StillSourceAndListenerKeepThePitch)
{
    EXPECT_FLOAT_EQ(ComputeDopplerPitch({0, 0, 0}, {0, 0, 0}, {10, 0, 0}, {0, 0, 0}, 343.0f, 1.0f), 1.0f);
}

TEST(Doppler, SourceComingCloserRaisesThePitch)
{
    // Source at +10 on x, moving toward the listener at the origin : -x
    const float pitch = ComputeDopplerPitch({0, 0, 0}, {0, 0, 0}, {10, 0, 0}, {-34.3, 0, 0}, 343.0f, 1.0f);
    EXPECT_NEAR(pitch, 343.0f / (343.0f - 34.3f), 1e-4f);
    EXPECT_GT(pitch, 1.0f);
}

TEST(Doppler, SourceMovingAwayLowersThePitch)
{
    const float pitch = ComputeDopplerPitch({0, 0, 0}, {0, 0, 0}, {10, 0, 0}, {34.3, 0, 0}, 343.0f, 1.0f);
    EXPECT_NEAR(pitch, 343.0f / (343.0f + 34.3f), 1e-4f);
    EXPECT_LT(pitch, 1.0f);
}

TEST(Doppler, ListenerMovingTowardTheSourceRaisesThePitch)
{
    const float pitch = ComputeDopplerPitch({0, 0, 0}, {34.3, 0, 0}, {10, 0, 0}, {0, 0, 0}, 343.0f, 1.0f);
    EXPECT_NEAR(pitch, (343.0f + 34.3f) / 343.0f, 1e-4f);
}

TEST(Doppler, OnlyTheSpeedAlongTheLineCounts)
{
    // Moving sideways around the listener changes nothing
    EXPECT_FLOAT_EQ(ComputeDopplerPitch({0, 0, 0}, {0, 0, 0}, {10, 0, 0}, {0, 0, 50}, 343.0f, 1.0f), 1.0f);
}

TEST(Doppler, ScaleZeroTurnsItOffAndLargerExaggeratesIt)
{
    const glm::dvec3 velocity(-30, 0, 0);
    EXPECT_FLOAT_EQ(ComputeDopplerPitch({0, 0, 0}, {0, 0, 0}, {10, 0, 0}, velocity, 343.0f, 0.0f), 1.0f);

    const float physical = ComputeDopplerPitch({0, 0, 0}, {0, 0, 0}, {10, 0, 0}, velocity, 343.0f, 1.0f);
    const float exaggerated = ComputeDopplerPitch({0, 0, 0}, {0, 0, 0}, {10, 0, 0}, velocity, 343.0f, 3.0f);
    EXPECT_GT(exaggerated, physical);
}

TEST(Doppler, StaysFiniteAtAndBeyondTheSpeedOfSound)
{
    const float pitch = ComputeDopplerPitch({0, 0, 0}, {0, 0, 0}, {10, 0, 0}, {-100000, 0, 0}, 343.0f, 1.0f);
    EXPECT_TRUE(std::isfinite(pitch));
    EXPECT_LE(pitch, 4.0f);
}

TEST(Doppler, SourceOnTheListenerKeepsThePitch)
{
    EXPECT_FLOAT_EQ(ComputeDopplerPitch({1, 2, 3}, {5, 0, 0}, {1, 2, 3}, {-5, 0, 0}, 343.0f, 1.0f), 1.0f);
}

TEST(ReverbZoneWeight, BoxIsFullInsideAndFadesOutside)
{
    const glm::vec3 half(5.0f);
    EXPECT_FLOAT_EQ(ReverbZoneWeight(ReverbShape::Box, {0, 0, 0}, half, 0.0f, 4.0f), 1.0f);
    EXPECT_FLOAT_EQ(ReverbZoneWeight(ReverbShape::Box, {5, 5, 5}, half, 0.0f, 4.0f), 1.0f);

    const float halfway = ReverbZoneWeight(ReverbShape::Box, {7, 0, 0}, half, 0.0f, 4.0f);
    EXPECT_NEAR(halfway, 0.5f, 1e-5f);

    EXPECT_FLOAT_EQ(ReverbZoneWeight(ReverbShape::Box, {9, 0, 0}, half, 0.0f, 4.0f), 0.0f);
    EXPECT_FLOAT_EQ(ReverbZoneWeight(ReverbShape::Box, {20, 0, 0}, half, 0.0f, 4.0f), 0.0f);
}

TEST(ReverbZoneWeight, SphereUsesTheRadius)
{
    EXPECT_FLOAT_EQ(ReverbZoneWeight(ReverbShape::Sphere, {0, 3, 0}, glm::vec3(0.0f), 4.0f, 2.0f), 1.0f);
    EXPECT_NEAR(ReverbZoneWeight(ReverbShape::Sphere, {0, 5, 0}, glm::vec3(0.0f), 4.0f, 2.0f), 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(ReverbZoneWeight(ReverbShape::Sphere, {0, 6, 0}, glm::vec3(0.0f), 4.0f, 2.0f), 0.0f);
}

TEST(ReverbZoneWeight, NoBlendDistanceIsAHardEdge)
{
    EXPECT_FLOAT_EQ(ReverbZoneWeight(ReverbShape::Box, {5.01f, 0, 0}, glm::vec3(5.0f), 0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ReverbZoneWeight(ReverbShape::Box, {4.99f, 0, 0}, glm::vec3(5.0f), 0.0f, 0.0f), 1.0f);
}

TEST(ReverbMix, NoZoneMeansNoCoverage)
{
    const ReverbMix mix = MixReverbZones({});
    EXPECT_FLOAT_EQ(mix.coverage, 0.0f);
}

TEST(ReverbMix, OneZoneInsideGivesItsSettings)
{
    const ReverbMix mix = MixReverbZones({ { 1.0f, 0, WithWet(0.7f, 0.9f) } });
    EXPECT_FLOAT_EQ(mix.coverage, 1.0f);
    EXPECT_NEAR(mix.params.wetLevel, 0.7f, 1e-5f);
    EXPECT_NEAR(mix.params.roomSize, 0.9f, 1e-5f);
}

TEST(ReverbMix, AZoneOnItsEdgeOnlyHalfCoversTheListener)
{
    const ReverbMix mix = MixReverbZones({ { 0.5f, 0, WithWet(0.7f, 0.9f) } });
    EXPECT_NEAR(mix.coverage, 0.5f, 1e-5f);
    // The settings are the zone's, the coverage says how much of it is heard
    EXPECT_NEAR(mix.params.wetLevel, 0.7f, 1e-5f);
}

TEST(ReverbMix, TheHigherPriorityCoversTheLowerOne)
{
    const ReverbMix mix = MixReverbZones({
        { 1.0f, 0, WithWet(0.2f, 0.2f) },
        { 1.0f, 5, WithWet(0.8f, 0.8f) },
    });
    EXPECT_FLOAT_EQ(mix.coverage, 1.0f);
    EXPECT_NEAR(mix.params.roomSize, 0.8f, 1e-5f);
}

TEST(ReverbMix, TheLowerPriorityShowsThroughWhereTheHigherOneDoesNotCover)
{
    const ReverbMix mix = MixReverbZones({
        { 1.0f, 0, WithWet(0.2f, 0.2f) },
        { 0.5f, 5, WithWet(0.8f, 0.8f) },
    });
    EXPECT_FLOAT_EQ(mix.coverage, 1.0f);
    EXPECT_NEAR(mix.params.roomSize, 0.5f * 0.8f + 0.5f * 0.2f, 1e-5f);
}

TEST(ReverbMix, ZonesOfTheSamePriorityBlendByWeight)
{
    const ReverbMix mix = MixReverbZones({
        { 1.0f, 0, WithWet(0.2f, 0.2f) },
        { 1.0f, 0, WithWet(0.8f, 0.8f) },
    });
    EXPECT_FLOAT_EQ(mix.coverage, 1.0f);
    EXPECT_NEAR(mix.params.roomSize, 0.5f, 1e-5f);
}

TEST(ReverbPresets, EveryPresetStaysInTheRangesOfTheFields)
{
    for (int i = static_cast<int>(ReverbPreset::Generic); i <= static_cast<int>(ReverbPreset::Underwater); ++i)
    {
        const ReverbParams p = GetReverbPreset(static_cast<ReverbPreset>(i));
        EXPECT_GE(p.roomSize, 0.0f); EXPECT_LE(p.roomSize, 1.0f);
        EXPECT_GE(p.damping, 0.0f);  EXPECT_LE(p.damping, 1.0f);
        EXPECT_GE(p.width, 0.0f);    EXPECT_LE(p.width, 1.0f);
        EXPECT_GE(p.wetLevel, 0.0f); EXPECT_LE(p.wetLevel, 2.0f);
        EXPECT_GE(p.dryLevel, 0.0f); EXPECT_LE(p.dryLevel, 1.0f);
        EXPECT_GE(p.preDelayMs, 0.0f); EXPECT_LE(p.preDelayMs, 250.0f);
        EXPECT_GE(p.lowCutHz, 10.0f);  EXPECT_LE(p.lowCutHz, 2000.0f);
        EXPECT_GE(p.highCutHz, 500.0f); EXPECT_LE(p.highCutHz, 20000.0f);
    }
}
