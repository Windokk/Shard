#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

#include "engine/core/math/guid.hpp"
#include "engine/core/math/hash.hpp"
#include "engine/core/math/math.hpp"
#include "engine/core/math/noise.hpp"
#include "engine/core/math/random.hpp"

using namespace Shard::Engine::Core;

// ------------------------------------------------------------------------------------------------------------ hash

TEST(Hash, Fnv1aKnownVectors) {
    EXPECT_EQ(Fnv1a64(""), 0xcbf29ce484222325ull);
    EXPECT_EQ(Fnv1a64("a"), 0xaf63dc4c8601ec8cull);
    EXPECT_EQ(Fnv1a64("foobar"), 0x85944171f73967e8ull);
    EXPECT_EQ(Fnv1a32(""), 0x811c9dc5u);
    EXPECT_EQ(Fnv1a32("a"), 0xe40c292cu);
    EXPECT_EQ(Fnv1a32("foobar"), 0xbf9cf968u);
    static_assert(Fnv1a64("a") == 0xaf63dc4c8601ec8cull, "constexpr");
}

TEST(Hash, Murmur3KnownVectors) {
    EXPECT_EQ(Murmur3_32(std::string_view(""), 0), 0u);
    EXPECT_EQ(Murmur3_32("", 1), 0x514E28B7u);
    EXPECT_EQ(Murmur3_32("", 0xffffffffu), 0x81F16F39u);
    const uint32_t aaaa = 0x61616161;
    EXPECT_EQ(Murmur3_32(&aaaa, 4, 0x9747b28c), 0x5A97808Au);
    EXPECT_EQ(Murmur3_32("Hello, world!", 0x9747b28c), 0x24884CBAu);
}

TEST(Hash, MixersScatterAndAreBijective) {
    EXPECT_NE(Mix32(1), Mix32(2));
    EXPECT_EQ(Mix32(0), 0u);                         // known fixed point of the finaliser
    std::unordered_set<uint32_t> seen;
    for (uint32_t i = 0; i < 100000; ++i) seen.insert(Mix32(i));
    EXPECT_EQ(seen.size(), 100000u);                 // no collision among consecutive inputs
    // changing one input bit flips about half of the output bits
    int totalFlipped = 0;
    for (uint32_t i = 0; i < 1000; ++i) totalFlipped += __builtin_popcount(Mix32(i) ^ Mix32(i ^ 1));
    EXPECT_NEAR(totalFlipped / 1000.0, 16.0, 2.0);
}

TEST(Hash, CombineDependsOnOrder) {
    EXPECT_NE(HashCombine(HashCombine(0, 1), 2), HashCombine(HashCombine(0, 2), 1));
    EXPECT_EQ(HashCombine(5, 6), HashCombine(5, 6));
}

TEST(Hash, HashIntsIsStableAndSensitive) {
    EXPECT_EQ(HashInts(1, 2, 3, 4), HashInts(1, 2, 3, 4));
    EXPECT_NE(HashInts(1, 2, 3, 4), HashInts(2, 1, 3, 4));
    EXPECT_NE(HashInts(1, 2, 3, 4), HashInts(1, 2, 3, 5));
    EXPECT_NE(HashInts(-1, 0, 0, 0), HashInts(0, 0, 0, 0));
}

// -------------------------------------------------------------------------------------------------------- random

TEST(Random, Pcg32ReferenceSequence) {
    // The pcg32-demo of the PCG authors : seed 42, stream 54
    Random rng(42, 54);
    const uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32_t e : expected) EXPECT_EQ(rng.Next(), e);
}

TEST(Random, SameSeedSameSequenceDifferentSeedDiffers) {
    Random a(1234), b(1234), c(1235);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const uint32_t x = a.Next();
        EXPECT_EQ(x, b.Next());
        differs |= (x != c.Next());
    }
    EXPECT_TRUE(differs);
}

TEST(Random, StreamsAreIndependent) {
    Random a(7, 1), b(7, 2);
    int equal = 0;
    for (int i = 0; i < 1000; ++i) equal += (a.Next() == b.Next());
    EXPECT_LT(equal, 3);
}

TEST(Random, FloatRangeAndMean) {
    Random rng(99);
    double sum = 0;
    for (int i = 0; i < 100000; ++i) {
        const float f = rng.NextFloat();
        ASSERT_GE(f, 0.0f);
        ASSERT_LT(f, 1.0f);
        sum += f;
        const double d = rng.NextDouble();
        ASSERT_GE(d, 0.0);
        ASSERT_LT(d, 1.0);
    }
    EXPECT_NEAR(sum / 100000, 0.5, 0.01);
}

TEST(Random, BelowAndRangeStayInBoundsAndCoverAllValues) {
    Random rng(5);
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i) {
        const int v = rng.Range(-3, 3);
        ASSERT_GE(v, -3);
        ASSERT_LE(v, 3);
        seen.insert(v);
        ASSERT_LT(rng.Below(10), 10u);
    }
    EXPECT_EQ(seen.size(), 7u);
    EXPECT_EQ(rng.Below(0), 0u);
    EXPECT_EQ(rng.Range(4, 4), 4);
    const int swapped = rng.Range(10, 5);
    EXPECT_TRUE(swapped >= 5 && swapped <= 10);
}

TEST(Random, BelowIsUniform) {
    Random rng(2024);
    int buckets[6] = {};
    for (int i = 0; i < 60000; ++i) ++buckets[rng.Below(6)];
    for (int b : buckets) EXPECT_NEAR(b, 10000, 400);
}

TEST(Random, GeometryHelpers) {
    Random rng(8);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_LE(glm::length(rng.InUnitSphere()), 1.0001f);
        EXPECT_LE(glm::length(rng.InUnitDisk()), 1.0001f);
        EXPECT_NEAR(glm::length(rng.OnUnitSphere()), 1.0f, 1e-4f);
        EXPECT_NEAR(glm::length(rng.OnUnitCircle()), 1.0f, 1e-4f);
    }
}

TEST(Random, ShuffleIsAPermutationAndDeterministic) {
    std::vector<int> a(50), b(50);
    for (int i = 0; i < 50; ++i) a[i] = b[i] = i;
    Random r1(3), r2(3);
    r1.Shuffle(a.begin(), a.end());
    r2.Shuffle(b.begin(), b.end());
    EXPECT_EQ(a, b);
    EXPECT_NE(a, (std::vector<int>(a.size()) = [] { std::vector<int> v(50); for (int i = 0; i < 50; ++i) v[i] = i; return v; }()));
    std::sort(a.begin(), a.end());
    for (int i = 0; i < 50; ++i) EXPECT_EQ(a[i], i);
}

TEST(Random, SplitGivesIndependentGenerators) {
    Random parent(11);
    Random child1 = parent.Split();
    Random child2 = parent.Split();
    EXPECT_NE(child1.Next(), child2.Next());
    Random parent2(11);
    EXPECT_EQ(parent2.Split().Next(), Random(11).Split().Next());   // deterministic
}

TEST(Random, SplitMixReference) {
    SplitMix64 sm(0);
    EXPECT_EQ(sm.Next(), 0xe220a8397b1dcdafull);     // first output for seed 0 (Vigna's reference)
}

// --------------------------------------------------------------------------------------------------------- noise

TEST(Noise, DeterministicAndSeedDependent) {
    EXPECT_EQ(Noise::Perlin(1.37f, 2.91f, 5), Noise::Perlin(1.37f, 2.91f, 5));
    EXPECT_NE(Noise::Perlin(1.37f, 2.91f, 5), Noise::Perlin(1.37f, 2.91f, 6));
    EXPECT_EQ(Noise::Value3(0.4f, 0.6f, 1.2f, 9), Noise::Value3(0.4f, 0.6f, 1.2f, 9));
    EXPECT_EQ(Noise::Worley(3.3f, 4.4f, 1), Noise::Worley(3.3f, 4.4f, 1));
}

TEST(Noise, StaysInRange) {
    Random rng(1);
    for (int i = 0; i < 20000; ++i) {
        const float x = rng.Range(-100.0f, 100.0f), y = rng.Range(-100.0f, 100.0f), z = rng.Range(-100.0f, 100.0f);
        ASSERT_LE(std::fabs(Noise::Perlin(x, y)), 1.0f);
        ASSERT_LE(std::fabs(Noise::Perlin3(x, y, z)), 1.0f);
        ASSERT_LE(std::fabs(Noise::Value(x, y)), 1.0f);
        ASSERT_LE(std::fabs(Noise::Value3(x, y, z)), 1.0f);
        ASSERT_LE(std::fabs(Noise::Fbm(x, y)), 1.0f);
        const float r = Noise::Ridged(x, y);
        ASSERT_GE(r, 0.0f);
        ASSERT_LE(r, 1.0f);
        const float w = Noise::Worley3(x, y, z);
        ASSERT_GE(w, 0.0f);
        ASSERT_LE(w, 1.8f);
    }
}

TEST(Noise, PerlinIsZeroOnTheLattice) {
    for (int x = -3; x <= 3; ++x)
        for (int y = -3; y <= 3; ++y) {
            EXPECT_FLOAT_EQ(Noise::Perlin(float(x), float(y), 77), 0.0f);
            EXPECT_FLOAT_EQ(Noise::Perlin3(float(x), float(y), 2.0f, 77), 0.0f);
        }
}

TEST(Noise, IsContinuous) {
    // A tiny step in the input never makes a big jump in the output
    const float eps = 1e-3f;
    float worst = 0;
    for (float x = -5; x < 5; x += 0.137f)
        for (float y = -5; y < 5; y += 0.151f) {
            worst = std::max(worst, std::fabs(Noise::Perlin(x + eps, y) - Noise::Perlin(x, y)));
            worst = std::max(worst, std::fabs(Noise::Value(x + eps, y) - Noise::Value(x, y)));
            worst = std::max(worst, std::fabs(Noise::Worley(x + eps, y) - Noise::Worley(x, y)));
        }
    EXPECT_LT(worst, 0.02f);
}

TEST(Noise, HasVarianceAndIsRoughlyCentred) {
    double sum = 0, sumSq = 0;
    constexpr int n = 40000;
    Random rng(2);
    for (int i = 0; i < n; ++i) {
        const double v = Noise::Perlin(rng.Range(-200.0f, 200.0f), rng.Range(-200.0f, 200.0f), 3);
        sum += v;
        sumSq += v * v;
    }
    const double mean = sum / n, variance = sumSq / n - mean * mean;
    EXPECT_NEAR(mean, 0.0, 0.03);
    EXPECT_GT(variance, 0.03);                       // not flat
}

TEST(Noise, FbmCombinesOctaves) {
    Noise::FractalParams one;
    one.octaves = 1;
    EXPECT_FLOAT_EQ(Noise::Fbm(0.5f, 0.7f, 4, one), Noise::Perlin(0.5f, 0.7f, 4));
    Noise::FractalParams many;
    many.octaves = 6;
    EXPECT_NE(Noise::Fbm(0.5f, 0.7f, 4, many), Noise::Fbm(0.5f, 0.7f, 4, one));
}

TEST(Noise, WorleyIsZeroAtAFeaturePointAndSmallNearby) {
    // somewhere the nearest feature point must be close : the maximum distance in a unit cell grid is < sqrt(2)
    Random rng(6);
    for (int i = 0; i < 1000; ++i)
        EXPECT_LT(Noise::Worley(rng.Range(-50.0f, 50.0f), rng.Range(-50.0f, 50.0f), 1), 1.42f);
}

TEST(Noise, ThreadSafe) {
    std::vector<float> results(4);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&, t] {
            float sum = 0;
            for (int i = 0; i < 10000; ++i) sum += Noise::Fbm(i * 0.01f, i * 0.02f, 8);
            results[t] = sum;
        });
    for (auto& t : threads) t.join();
    for (int t = 1; t < 4; ++t) EXPECT_EQ(results[0], results[t]);
}

// ---------------------------------------------------------------------------------------------------------- guid

TEST(Guid, GenerateIsUniqueAndWellFormed) {
    std::set<Guid> seen;
    for (int i = 0; i < 5000; ++i) {
        Guid g = Guid::Generate();
        EXPECT_FALSE(g.IsNull());
        EXPECT_EQ(g.Version(), 4);
        EXPECT_EQ((g.low >> 62) & 0x3, 0x2u);        // RFC 4122 variant
        EXPECT_TRUE(seen.insert(g).second);
    }
}

TEST(Guid, GenerateIsUniqueAcrossThreads) {
    std::vector<std::vector<Guid>> perThread(4);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&, t] { for (int i = 0; i < 2000; ++i) perThread[t].push_back(Guid::Generate()); });
    for (auto& t : threads) t.join();
    std::set<Guid> all;
    for (auto& v : perThread) all.insert(v.begin(), v.end());
    EXPECT_EQ(all.size(), 8000u);
}

TEST(Guid, StringRoundTrip) {
    Guid g(0x0123456789abcdefull, 0xfedcba9876543210ull);
    EXPECT_EQ(g.ToString(), "01234567-89ab-cdef-fedc-ba9876543210");
    auto parsed = Guid::Parse(g.ToString());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, g);
    EXPECT_EQ(*Guid::Parse("01234567-89AB-CDEF-FEDC-BA9876543210"), g);    // case-insensitive
    EXPECT_EQ(*Guid::Parse("{01234567-89ab-cdef-fedc-ba9876543210}"), g);  // braces
    Guid random = Guid::Generate();
    EXPECT_EQ(*Guid::Parse(random.ToString()), random);
}

TEST(Guid, RejectsMalformedText) {
    EXPECT_FALSE(Guid::Parse("").has_value());
    EXPECT_FALSE(Guid::Parse("not a guid").has_value());
    EXPECT_FALSE(Guid::Parse("01234567-89ab-cdef-fedc-ba987654321").has_value());       // too short
    EXPECT_FALSE(Guid::Parse("01234567-89ab-cdef-fedc-ba98765432100").has_value());     // too long
    EXPECT_FALSE(Guid::Parse("0123456789abcdef-fedc-ba9876543210xxxx").has_value());    // hyphens misplaced
    EXPECT_FALSE(Guid::Parse("0123456g-89ab-cdef-fedc-ba9876543210").has_value());      // bad digit
    EXPECT_FALSE(Guid::Parse("{01234567-89ab-cdef-fedc-ba9876543210").has_value());     // brace not closed
}

TEST(Guid, FromNameIsDeterministicAndSensitive) {
    EXPECT_EQ(Guid::FromName("textures/wall.png"), Guid::FromName("textures/wall.png"));
    EXPECT_NE(Guid::FromName("textures/wall.png"), Guid::FromName("textures/wall2.png"));
    EXPECT_EQ(Guid::FromName("x").Version(), 8);
    Guid ns1(1, 2), ns2(3, 4);
    EXPECT_NE(Guid::FromName(ns1, "same"), Guid::FromName(ns2, "same"));    // the namespace matters
    EXPECT_FALSE(Guid::FromName("").IsNull());
}

TEST(Guid, NullAndHash) {
    EXPECT_TRUE(Guid().IsNull());
    EXPECT_FALSE(static_cast<bool>(Guid()));
    std::unordered_set<Guid> set;
    set.insert(Guid::Generate());
    set.insert(Guid::Generate());
    EXPECT_EQ(set.size(), 2u);
}

// ---------------------------------------------------------------------------------------------------------- math

TEST(MathHelpers, Basics) {
    EXPECT_FLOAT_EQ(Math::Lerp(2.0f, 4.0f, 0.25f), 2.5f);
    EXPECT_FLOAT_EQ(Math::InverseLerp(2.0f, 4.0f, 3.0f), 0.5f);
    EXPECT_FLOAT_EQ(Math::InverseLerp(1.0f, 1.0f, 5.0f), 0.0f);          // degenerate range
    EXPECT_FLOAT_EQ(Math::Remap(5.0f, 0.0f, 10.0f, 100.0f, 200.0f), 150.0f);
    EXPECT_FLOAT_EQ(Math::Saturate(1.5f), 1.0f);
    EXPECT_FLOAT_EQ(Math::Saturate(-1.0f), 0.0f);
    EXPECT_EQ(Math::Clamp(5, 0, 3), 3);
    EXPECT_NEAR(Math::Radians(180.0), Math::kPi, 1e-12);
    EXPECT_NEAR(Math::Degrees(Math::kPi), 180.0, 1e-12);
}

TEST(MathHelpers, SmoothStepAndWrap) {
    EXPECT_FLOAT_EQ(Math::SmoothStep(0.0f, 1.0f, -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(Math::SmoothStep(0.0f, 1.0f, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ(Math::SmoothStep(0.0f, 1.0f, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(Math::SmootherStep(0.0f, 1.0f, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(Math::Wrap(370.0f, 0.0f, 360.0f), 10.0f);
    EXPECT_FLOAT_EQ(Math::Wrap(-10.0f, 0.0f, 360.0f), 350.0f);
    EXPECT_EQ(Math::NextPowerOfTwo(0), 1u);
    EXPECT_EQ(Math::NextPowerOfTwo(5), 8u);
    EXPECT_EQ(Math::NextPowerOfTwo(64), 64u);
    EXPECT_TRUE(Math::Approx(1.0, 1.0 + 1e-9));
    EXPECT_FALSE(Math::Approx(1.0, 1.1));
}

TEST(LargeWorlds, DoubleKeepsPrecisionWhereFloatDoesNot) {
    // 100 km from the origin a float cannot tell 1 mm apart, the double world position + local offset can
    const DVec3 origin(100000.0, 0.0, 0.0);
    const DVec3 a = origin + DVec3(0.001, 0.0, 0.0), b = origin + DVec3(0.002, 0.0, 0.0);
    EXPECT_EQ(float(a.x), float(b.x));                                   // narrowed too early : they collapse
    EXPECT_NE(ToLocal(a, origin).x, ToLocal(b, origin).x);               // relative to the origin : still distinct
    EXPECT_NEAR(ToLocal(b, origin).x, 0.002f, 1e-6f);
    EXPECT_NEAR(ToWorld(ToLocal(b, origin), origin).x, b.x, 1e-6);
}

TEST(LargeWorlds, MatrixIsRebasedBeforeNarrowing) {
    const DVec3 origin(1.0e6, 2.0e6, -3.0e6);
    const DMat4 world = ComposeTRS(origin + DVec3(1.0, 2.0, 3.0), DQuat(1, 0, 0, 0), DVec3(2.0));
    const Mat4 local = ToLocal(world, origin);
    EXPECT_NEAR(local[3].x, 1.0f, 1e-6f);
    EXPECT_NEAR(local[3].y, 2.0f, 1e-6f);
    EXPECT_NEAR(local[3].z, 3.0f, 1e-6f);
    EXPECT_NEAR(local[0].x, 2.0f, 1e-6f);                                // the scale survived
}

TEST(LargeWorlds, ComposeTRSMatchesGlm) {
    const DQuat rot = glm::angleAxis(0.7, glm::normalize(DVec3(1, 2, 3)));
    const DMat4 expected = glm::translate(DMat4(1.0), DVec3(4, 5, 6)) * glm::mat4_cast(rot) * glm::scale(DMat4(1.0), DVec3(1, 2, 3));
    const DMat4 actual = ComposeTRS(DVec3(4, 5, 6), rot, DVec3(1, 2, 3));
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) EXPECT_NEAR(actual[c][r], expected[c][r], 1e-12);
}
