#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <vector>

#include "engine/core/determinism/determinism.hpp"
#include "engine/core/determinism/state_hash.hpp"
#include "engine/core/jobs/job_system.hpp"
#include "engine/core/math/guid.hpp"
#include "engine/physics/physics_manager.hpp"
#include "support/test_engine_context.hpp"

using namespace Shard::Engine::Core;
using Shard::Engine::Core::Determinism::StateHasher;

namespace {
    // Every test starts and ends with the global seed and mode forgotten : they are process-wide
    struct DeterminismReset : ::testing::Test {
        void SetUp() override { Determinism::Reset(); }
        void TearDown() override { Determinism::Reset(); }
    };
}

// ---- StateHasher ------------------------------------------------------------------------------------------------------

TEST(StateHasher, SameValuesSameHashAndOrderMatters) {
    const auto hashOf = [](float a, float b) { return StateHasher().Float(a).Float(b).Digest(); };
    EXPECT_EQ(hashOf(1.0f, 2.0f), hashOf(1.0f, 2.0f));
    EXPECT_NE(hashOf(1.0f, 2.0f), hashOf(2.0f, 1.0f));
    EXPECT_NE(StateHasher().Digest(), StateHasher().Byte(0).Digest()) << "an empty state and a state of one zero byte are not the same";
}

TEST(StateHasher, OneFlippedBitChangesTheHash) {
    const float a = 1.0f;
    uint32_t bits;
    std::memcpy(&bits, &a, sizeof(bits));
    bits ^= 1u;
    float b;
    std::memcpy(&b, &bits, sizeof(b));
    EXPECT_NE(StateHasher().Float(a).Digest(), StateHasher().Float(b).Digest()) << "the last bit of a float counts : that is what the hash is for";
}

TEST(StateHasher, EqualValuesWithDifferentBitsHashTheSame) {
    EXPECT_EQ(StateHasher().Float(0.0f).Digest(), StateHasher().Float(-0.0f).Digest());
    const float nan1 = std::numeric_limits<float>::quiet_NaN();
    uint32_t bits;
    std::memcpy(&bits, &nan1, sizeof(bits));
    bits |= 0x1234u;
    float nan2;
    std::memcpy(&nan2, &bits, sizeof(nan2));
    EXPECT_EQ(StateHasher().Float(nan1).Digest(), StateHasher().Float(nan2).Digest());

    // q and -q are the same rotation
    const glm::quat q = glm::normalize(glm::quat(0.7f, 0.1f, -0.3f, 0.2f));
    EXPECT_EQ(StateHasher().Quat(q).Digest(), StateHasher().Quat(-q).Digest());
}

TEST(StateHasher, StringsAreLengthPrefixed) {
    EXPECT_NE(StateHasher().String("ab").String("c").Digest(), StateHasher().String("a").String("bc").Digest());
}

TEST(StateHasher, ValuesArePinned) {
    // The same on every platform and compiler, forever : replay files hold hashes made with it. If this test fails the
    // hash changed, and every recorded replay has to be recorded again (ShardHeadless --write-replay).
    const uint64_t digest = StateHasher().U32(1).Float(1.5f).Bool(true).String("shard").Vec3(glm::vec3(1.0f, -2.0f, 0.25f)).Digest();
    EXPECT_EQ(Determinism::HashToString(digest), "d1d81970faaa1079");
}

TEST(StateHasher, HashTextRoundTrips) {
    for (uint64_t value : {0ull, 1ull, 0xdeadbeefcafef00dull, ~0ull}) {
        uint64_t parsed = 123;
        ASSERT_TRUE(Determinism::HashFromString(Determinism::HashToString(value), parsed));
        EXPECT_EQ(parsed, value);
    }
    uint64_t parsed = 0;
    EXPECT_TRUE(Determinism::HashFromString("0xFF", parsed));
    EXPECT_EQ(parsed, 255u);
    EXPECT_FALSE(Determinism::HashFromString("", parsed));
    EXPECT_FALSE(Determinism::HashFromString("xyz", parsed));
    EXPECT_FALSE(Determinism::HashFromString("00000000000000001", parsed)) << "17 digits do not fit";
}

// ---- seed and streams -------------------------------------------------------------------------------------------------

TEST_F(DeterminismReset, AStreamIsAFunctionOfTheSeedAndTheName) {
    Determinism::Configure(42, true);
    Random a = Determinism::MakeStream("physics.spawner");
    Random b = Determinism::MakeStream("physics.spawner");
    for (int i = 0; i < 100; ++i) EXPECT_EQ(a.Next(), b.Next());

    Random other = Determinism::MakeStream("audio.jitter");
    Random again = Determinism::MakeStream("physics.spawner");
    int same = 0;
    for (int i = 0; i < 100; ++i) same += other.Next() == again.Next();
    EXPECT_LT(same, 3) << "two names give two different sequences";

    Determinism::Configure(43, true);
    Random reseeded = Determinism::MakeStream("physics.spawner");
    Random original = Random(0);
    Determinism::Configure(42, true);
    original = Determinism::MakeStream("physics.spawner");
    same = 0;
    for (int i = 0; i < 100; ++i) same += reseeded.Next() == original.Next();
    EXPECT_LT(same, 3) << "two seeds give two different sequences";
}

TEST_F(DeterminismReset, StreamsDoNotDependOnTheOrderTheyAreAskedIn) {
    Determinism::Configure(7, true);
    Random first = Determinism::MakeStream("a");
    Random second = Determinism::MakeStream("b");
    const uint32_t a0 = first.Next(), b0 = second.Next();

    Determinism::Configure(7, true);
    Random secondAgain = Determinism::MakeStream("b");
    Random firstAgain = Determinism::MakeStream("a");
    EXPECT_EQ(firstAgain.Next(), a0);
    EXPECT_EQ(secondAgain.Next(), b0);
}

TEST_F(DeterminismReset, StreamValuesArePinned) {
    // PCG32 and the mixers only use integer arithmetic : the same numbers on every platform. Pinned so that a change of
    // the derivation (which would silently change every recorded replay) is noticed.
    Determinism::Configure(1, true);
    Random r = Determinism::MakeStream("pinned");
    EXPECT_EQ(r.Next(), 2949991208u);
    EXPECT_EQ(r.Next(), 2090037578u);
}

TEST_F(DeterminismReset, AnUnconfiguredRunDrawsASeedAndKeepsIt) {
    const uint64_t first = Determinism::Seed();
    EXPECT_EQ(Determinism::Seed(), first);
    EXPECT_FALSE(Determinism::Enabled());
}

TEST_F(DeterminismReset, GuidsAreCountedInADeterministicRun) {
    Determinism::Configure(5, true);
    const Guid a = Guid::Generate();
    const Guid b = Guid::Generate();
    EXPECT_NE(a, b);
    EXPECT_EQ(a.Version(), 4);

    Determinism::Configure(5, true);
    EXPECT_EQ(Guid::Generate(), a);
    EXPECT_EQ(Guid::Generate(), b);

    Determinism::Configure(6, true);
    EXPECT_NE(Guid::Generate(), a);
}

TEST_F(DeterminismReset, GuidsAreRandomOtherwise) {
    Determinism::Configure(5, false);
    std::set<std::pair<uint64_t, uint64_t>> seen;
    for (int i = 0; i < 100; ++i) {
        const Guid g = Guid::Generate();
        seen.insert({g.high, g.low});
    }
    EXPECT_EQ(seen.size(), 100u);
}

// ---- the random helpers are libm free ----------------------------------------------------------------------------------

TEST(Random, UnitVectorsAreMadeOfExactOperationsOnly) {
    // Pinned bit patterns : sin / cos differ in the last bit between C libraries, + - * / and sqrt do not
    Random r(99);
    const glm::vec3 v = r.OnUnitSphere();
    const glm::vec2 c = r.OnUnitCircle();
    uint32_t bits[5];
    std::memcpy(&bits[0], &v.x, 4);
    std::memcpy(&bits[1], &v.y, 4);
    std::memcpy(&bits[2], &v.z, 4);
    std::memcpy(&bits[3], &c.x, 4);
    std::memcpy(&bits[4], &c.y, 4);
    const uint32_t expected[5] = {1053852318u, 3202649067u, 3209427785u, 3208312491u, 1060036489u};
    for (int i = 0; i < 5; ++i) EXPECT_EQ(bits[i], expected[i]) << i;
}

// ---- physics in deterministic mode ------------------------------------------------------------------------------------

namespace {
    // Jolt calls back into the engine's physics manager when two bodies touch
    struct PhysicsContext : Shard::Tests::TestEngineContext {
        Shard::Engine::Physics::PhysicsManager* GetPhysicsManager() const override { return physics; }
        Shard::Engine::Physics::PhysicsManager* physics = nullptr;
    };

    // Drops a handful of bodies onto a floor, steps, returns what Jolt says about every one of them
    std::vector<float> DropBodies(bool deterministic, Shard::Engine::Core::JobSystem* jobs) {
        Shard::Engine::Physics::PhysicsManager physics;
        PhysicsContext context;
        context.jobSystem = jobs;
        context.physics = &physics;
        SetEngine(&context);

        physics.Init(glm::vec3(0.0f, -9.81f, 0.0f), deterministic);

        JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(20.0f, 0.5f, 20.0f)), JPH::RVec3(0.0f, -0.5f, 0.0f), JPH::Quat::sIdentity(),
                                        JPH::EMotionType::Static, Shard::Engine::Physics::Layers::NON_MOVING);
        physics.CreateBody(floor, nullptr, JPH::EActivation::DontActivate);

        std::vector<JPH::BodyID> ids;
        for (int i = 0; i < 40; ++i) {
            const JPH::RVec3 position(float(i % 5) * 0.9f - 2.0f, 1.0f + float(i) * 0.7f, float((i / 5) % 4) * 0.8f);
            JPH::ShapeRefC shape = (i % 2) ? JPH::ShapeRefC(new JPH::SphereShape(0.5f)) : JPH::ShapeRefC(new JPH::BoxShape(JPH::Vec3(0.4f, 0.4f, 0.4f)));
            JPH::BodyCreationSettings settings(shape, position, JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Shard::Engine::Physics::Layers::MOVING);
            settings.mRestitution = 0.3f;
            ids.push_back(physics.CreateBody(settings, nullptr));
        }

        for (int s = 0; s < 240; ++s) physics.StepSimulation(1.0f / 60.0f, false);

        std::vector<float> state;
        for (const JPH::BodyID& id : ids) {
            const JPH::RVec3 p = physics.GetBodyInterface().GetPosition(id);
            const JPH::Quat q = physics.GetBodyInterface().GetRotation(id);
            const JPH::Vec3 v = physics.GetBodyInterface().GetLinearVelocity(id);
            for (float f : {float(p.GetX()), float(p.GetY()), float(p.GetZ()), q.GetX(), q.GetY(), q.GetZ(), q.GetW(), v.GetX(), v.GetY(), v.GetZ()})
                state.push_back(f);
        }

        physics.Shutdown();
        SetEngine(nullptr);
        return state;
    }

    bool SameBits(const std::vector<float>& a, const std::vector<float>& b) {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    }
}

TEST(DeterministicPhysics, TwoRunsGiveTheSameBits) {
    const std::vector<float> first = DropBodies(true, nullptr);
    const std::vector<float> second = DropBodies(true, nullptr);
    ASSERT_FALSE(first.empty());
    EXPECT_TRUE(SameBits(first, second));
}

TEST(DeterministicPhysics, TheNumberOfThreadsDoesNotChangeTheResult) {
    // Jolt's own guarantee, which the deterministic mode builds on : one thread or several, same bits
    Shard::Engine::Core::JobSystemDesc desc;
    desc.workerCount = 3;
    Shard::Engine::Core::JobSystem jobs(desc);

    const std::vector<float> serial = DropBodies(true, nullptr);
    const std::vector<float> parallel = DropBodies(false, &jobs);
    EXPECT_TRUE(SameBits(serial, parallel));
}
