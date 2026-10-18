#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "engine/core/jobs/job_system.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"
#include "support/test_engine_context.hpp"

using namespace Shard::Engine;
using Shard::Engine::Objects::Actor;
using Shard::Engine::Core::Object;
using Shard::Engine::Objects::Components::PhysicsBody;
using Shard::Engine::Physics::PhysicsManager;

namespace {

    // Jolt in double precision + a world with reference frames : physics runs in the root frame (double), the transforms stay
    // small numbers in their own frame
    class LargeWorldPhysics : public ::testing::Test {
        protected:
            void SetUp() override {
                Core::SetEngine(&context);
                physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
                context.physicsManager = &physics;
                world = std::make_unique<Worlds::World>("large", Filesystem::Path());
            }

            void TearDown() override {
                actors.clear();
                world.reset();
                physics.Shutdown();
                context.physicsManager = nullptr;
                Core::SetEngine(nullptr);
            }

            std::shared_ptr<Actor> MakeBox(const char* name, glm::vec3 position, glm::vec3 scale, JPH::EMotionType motion, Core::FrameId frame) {
                auto actor = Object::CreateWithContext<Actor>(&context, std::string(name), &context);
                actor->SetWorld(world.get());
                actor->transform->frame = frame;
                actor->transform->SetPosition(position);
                actor->transform->SetScale(scale);
                actor->AddComponent<PhysicsBody>()->CreateBody(motion);
                actors.push_back(actor);
                return actor;
            }

            Shard::Tests::TestEngineContext context;
            PhysicsManager physics;
            std::unique_ptr<Worlds::World> world;
            std::vector<std::shared_ptr<Actor>> actors;
    };
}

TEST_F(LargeWorldPhysics, JoltKeepsDoublePrecisionPositions) {
    // The build option reaches the engine : a RVec3 holds doubles
#ifdef JPH_DOUBLE_PRECISION
    JPH::RVec3 p(1.0e12 + 0.001, 0.0, 0.0);
    EXPECT_NEAR(p.GetX(), 1.0e12 + 0.001, 1e-3);        // a float would be off by 65 km here
    SUCCEED();
#else
    GTEST_SKIP() << "Jolt was built in single precision (SHARD_DOUBLE_PRECISION_PHYSICS=OFF)";
#endif
}

TEST_F(LargeWorldPhysics, ATransformReportsItsPositionInTheRootFrame) {
    // 10^9 cells * 1024 m = 10^12 m away from the root
    const Core::CellCoord far{1000000000ll, 0, 0};
    const Core::FrameId planet = world->GetFrames().Create("Planet", Core::kRootFrame, Core::WorldPosition(far, glm::dvec3(0.0)));

    auto actor = MakeBox("A", glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(1.0f), JPH::EMotionType::Static, planet);

    const Core::WorldPosition coords = actor->transform->GetWorldCoords();
    EXPECT_EQ(coords.cell.x, far.x + 0);
    EXPECT_NEAR(coords.local.x, 1.0, 1e-9);
    EXPECT_NEAR(coords.local.y, 2.0, 1e-9);
    EXPECT_NEAR(coords.local.z, 3.0, 1e-9);

    // And back : moving it to a root position lands on the same frame-local value
    actor->transform->SetWorldCoords(world->GetFrames().ToRoot(planet, glm::dvec3(10.0, 0.0, -4.0)));
    EXPECT_NEAR(actor->transform->GetPosition().x, 10.0f, 1e-4f);
    EXPECT_NEAR(actor->transform->GetPosition().z, -4.0f, 1e-4f);
}

TEST_F(LargeWorldPhysics, ABodyFallsAndRestsOnAGroundFarFromTheOrigin) {
    const Core::CellCoord far{1000000000ll, 0, -1000000000ll};
    const Core::FrameId planet = world->GetFrames().Create("Planet", Core::kRootFrame, Core::WorldPosition(far, glm::dvec3(0.0)));

    auto ground = MakeBox("Ground", glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f), JPH::EMotionType::Static, planet);
    auto box = MakeBox("Box", glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic, planet);

    const glm::dvec3 start = box->transform->GetWorldPositionD();
    EXPECT_GT(start.x, 1.0e11);                         // really far : a float could not even say where in the metre it is

    for (int step = 0; step < 180; ++step) {
        physics.StepSimulation(1.0f / 60.0f, false);
        box->GetComponent<PhysicsBody>()->SyncTransformFromPhysics();
    }

    // Landed on the ground's top face, in the frame's own small numbers
    EXPECT_NEAR(box->transform->GetPosition().y, 0.5f, 0.05f);
    EXPECT_NEAR(box->transform->GetPosition().x, 0.0f, 1e-3f);
    EXPECT_NEAR(box->transform->GetPosition().z, 0.0f, 1e-3f);

    // The body itself is in the root frame, in double, where the frame put it
    const glm::dvec3 bodyPosition = box->GetComponent<PhysicsBody>()->GetPositionD();
    EXPECT_NEAR(bodyPosition.x, start.x, 1e-3);
    EXPECT_NEAR(bodyPosition.y, start.y - 1.5, 0.05);
}
