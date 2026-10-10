#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "engine/core/jobs/job_system.hpp"
#include "engine/physics/physics_manager.hpp"
#include "support/test_engine_context.hpp"

using Shard::Engine::Physics::PhysicsManager;

namespace {

    // With an engine context that owns a JobSystem, PhysicsManager runs Jolt on it (JoltJobSystem) instead of on a
    // thread pool of Jolt's own. Same physics, so the same answers as without.
    class PhysicsOnJobSystem : public ::testing::Test {
        protected:
            void SetUp() override {
                Shard::Engine::Core::JobSystemDesc desc;
                desc.workerCount = 3;
                jobs = std::make_unique<Shard::Engine::Core::JobSystem>(desc);
                context.jobSystem = jobs.get();
                Shard::Engine::Core::SetEngine(&context);
            }

            void TearDown() override {
                Shard::Engine::Core::SetEngine(nullptr);
                jobs.reset();
            }

            Shard::Tests::TestEngineContext context;
            std::unique_ptr<Shard::Engine::Core::JobSystem> jobs;
    };
}

TEST_F(PhysicsOnJobSystem, ManyBodiesFallLikeTheTextbook) {
    constexpr float kGravity = 9.81f, kStep = 1.0f / 60.0f;
    constexpr int kBodies = 300, kSteps = 60;

    PhysicsManager physics;
    physics.Init(glm::vec3(0.0f, -kGravity, 0.0f));

    std::vector<JPH::BodyID> ids;
    for (int i = 0; i < kBodies; ++i) {
        JPH::BodyCreationSettings settings(new JPH::SphereShape(0.5f),
                                           JPH::RVec3(float(i % 20) * 3.0f, 0.0f, float(i / 20) * 3.0f),
                                           JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic,
                                           Shard::Engine::Physics::Layers::MOVING);
        settings.mLinearDamping = 0.0f;
        settings.mAngularDamping = 0.0f;
        ids.push_back(physics.CreateBody(settings, nullptr));
    }

    for (int s = 0; s < kSteps; ++s) physics.StepSimulation(kStep, false);

    const float expected = 0.5f * kGravity * (kSteps * kStep) * (kSteps * kStep);
    for (const JPH::BodyID& id : ids)
        EXPECT_NEAR(-physics.GetBodyInterface().GetPosition(id).GetY(), expected, expected * 0.03f);

    physics.Shutdown();
}

TEST_F(PhysicsOnJobSystem, ShutsDownCleanlyAndCanBeRestarted) {
    for (int round = 0; round < 3; ++round) {
        PhysicsManager physics;
        physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
        physics.StepSimulation(1.0f / 60.0f, false);
        physics.Shutdown();
    }
}
