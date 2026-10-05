#include <gtest/gtest.h>

#include "engine/physics/physics_manager.hpp"
#include "engine/world/time_manager.hpp"

using Shard::Engine::Physics::PhysicsManager;
using Shard::Engine::Time::TimeManager;

namespace {

    // Drives PhysicsManager the way EngineInstance::Run() does - bank each frame's real time, then
    // run however many fixed steps it paid for - so these tests exercise the actual loop policy and
    // not just Jolt.
    class FallingBodyFixture : public ::testing::Test {
        protected:

            static constexpr float kFixedStep = 1.0f / 60.0f;

            void SetUpWorld(glm::vec3 gravity)
            {
                // A fresh manager per world: JPH::PhysicsSystem is held by value and asserts if it is
                // initialized twice, so a shut-down PhysicsManager can't be reused.
                TearDownWorld();
                physics = std::make_unique<PhysicsManager>();
                physics->Init(gravity);

                JPH::BodyCreationSettings settings(
                    new JPH::SphereShape(0.5f),
                    JPH::RVec3(0.0, 0.0, 0.0),
                    JPH::Quat::sIdentity(),
                    JPH::EMotionType::Dynamic,
                    Shard::Engine::Physics::Layers::MOVING
                );

                // Free fall in a vacuum: damping would make the comparison against 1/2*g*t^2 meaningless.
                settings.mLinearDamping = 0.0f;
                settings.mAngularDamping = 0.0f;

                bodyID = physics->CreateBody(settings, nullptr);
            }

            void TearDownWorld()
            {
                if (physics)
                {
                    physics->Shutdown();
                    physics.reset();
                }
            }

            void TearDown() override
            {
                TearDownWorld();
            }

            /// Runs `realSeconds` of wall clock at `fps` and returns how far the body fell.
            float FallOver(float realSeconds, float fps)
            {
                TimeManager time;
                time.Init(kFixedStep);

                const float startY = physics->GetBodyInterface().GetPosition(bodyID).GetY();

                const int frames = static_cast<int>(realSeconds * fps);
                for (int i = 0; i < frames; i++)
                {
                    time.Advance(1.0f / fps);

                    const int steps = time.ConsumeFixedSteps();
                    for (int s = 0; s < steps; s++)
                        physics->StepSimulation(kFixedStep, false);
                }

                return startY - physics->GetBodyInterface().GetPosition(bodyID).GetY();
            }

            std::unique_ptr<PhysicsManager> physics;
            JPH::BodyID bodyID;
    };
}

TEST_F(FallingBodyFixture, FallsTheSameDistanceWhateverTheFramerate)
{
    const float gravity = 9.81f;
    const float seconds = 1.0f;

    const float framerates[] = { 500.0f, 144.0f, 60.0f, 20.0f };

    // Sampled at an arbitrary instant a body can be one step behind: whether the last step of the
    // second has been paid out yet depends on where the frame boundaries fall, and a frame delta that
    // isn't exactly representable (1/20 s, say) can leave the accumulator a hair under the threshold.
    // That step is carried into the next frame rather than dropped, so the bound is one step of
    // travel at the speed reached - not a drift that grows with time.
    const float oneStepOfTravel = (gravity * seconds) * kFixedStep;

    float reference = -1.0f;

    for (float fps : framerates)
    {
        SetUpWorld(glm::vec3(0.0f, -gravity, 0.0f));
        const float fallen = FallOver(seconds, fps);

        if (reference < 0.0f)
            reference = fallen;
        else
            EXPECT_NEAR(fallen, reference, oneStepOfTravel * 1.1f) << "at " << fps << " fps";
    }
}

TEST_F(FallingBodyFixture, DoesNotDriftFurtherBehindOverTime)
{
    const float gravity = 9.81f;

    // The one-step sampling gap must stay a fixed offset, not accumulate: over 8 seconds a loop that
    // leaked time would be far more than one step apart, which is exactly the framerate dependence
    // this whole fixed-step scheme exists to prevent.
    const float seconds = 8.0f;
    const float oneStepOfTravel = (gravity * seconds) * kFixedStep;

    SetUpWorld(glm::vec3(0.0f, -gravity, 0.0f));
    const float fast = FallOver(seconds, 500.0f);

    SetUpWorld(glm::vec3(0.0f, -gravity, 0.0f));
    const float slow = FallOver(seconds, 20.0f);

    EXPECT_NEAR(fast, slow, oneStepOfTravel * 1.1f);
}

TEST_F(FallingBodyFixture, FallsTheTextbookDistanceInRealTime)
{
    const float gravity = 9.81f;

    SetUpWorld(glm::vec3(0.0f, -gravity, 0.0f));

    const float fallen = FallOver(1.0f, 60.0f);

    // 1/2*g*t^2 after one real second. The discrete integrator overshoots the closed form slightly
    // (by half a step of velocity), hence the few percent of slack rather than an exact match.
    const float expected = 0.5f * gravity * 1.0f * 1.0f;
    EXPECT_NEAR(fallen, expected, expected * 0.03f);
}

TEST_F(FallingBodyFixture, GravityScalesTheFallForProjectsThatArentMetric)
{
    // A project whose unit isn't a metre scales gravity to match - twice the gravity has to cover
    // twice the distance in the same time, which is what makes a 2x-scaled world read correctly.
    SetUpWorld(glm::vec3(0.0f, -9.81f, 0.0f));
    const float metric = FallOver(1.0f, 60.0f);

    SetUpWorld(glm::vec3(0.0f, -19.62f, 0.0f));
    const float doubled = FallOver(1.0f, 60.0f);

    EXPECT_NEAR(doubled, metric * 2.0f, metric * 0.02f);
}
