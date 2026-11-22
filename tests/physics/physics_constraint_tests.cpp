#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "engine/core/tags/tag_registry.hpp"
#include "engine/physics/constraints/physics_constraint.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/physics/physics_world_data.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"
#include "support/test_engine_context.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Physics;
using Shard::Engine::Core::Object;
using Shard::Engine::Objects::Actor;
using namespace Shard::Engine::Objects::Components;

namespace {

    constexpr float kStep = 1.0f / 60.0f;

    class ConstraintComponents : public ::testing::Test {
        protected:
            void SetUp() override
            {
                Core::SetEngine(&context);
                physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
                context.physicsManager = &physics;
                world = std::make_unique<Worlds::World>("constraints", Filesystem::Path());
            }

            void TearDown() override
            {
                actors.clear();
                world.reset();
                physics.Shutdown();
                context.physicsManager = nullptr;
                Core::SetEngine(nullptr);
            }

            std::shared_ptr<Actor> MakeActor(const char* name, glm::vec3 position, JPH::EMotionType motion)
            {
                auto actor = Object::CreateWithContext<Actor>(&context, std::string(name), &context);
                actor->SetWorld(world.get());
                actor->transform->SetPosition(position);
                auto body = actor->AddComponent<PhysicsBody>();
                body->CreateBody(motion);
                actors.push_back(actor);
                return actor;
            }

            void Run(int steps, PhysicsConstraint* constraint = nullptr)
            {
                for (int i = 0; i < steps; ++i)
                {
                    if (constraint)
                        constraint->Sync(physics);
                    physics.StepSimulation(kStep, false);
                }
            }

            static glm::dvec3 Where(const std::shared_ptr<Actor>& actor)
            {
                return actor->GetComponent<PhysicsBody>()->GetPositionD();
            }

            Shard::Tests::TestEngineContext context;
            PhysicsManager physics;
            std::unique_ptr<Worlds::World> world;
            std::vector<std::shared_ptr<Actor>> actors;
    };
}

TEST_F(ConstraintComponents, HingeToTheWorldKeepsThePivot)
{
    auto swing = MakeActor("Swing", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = swing->AddComponent<HingeConstraint>();
    hinge->axis = glm::vec3(0.0f, 0.0f, 1.0f);
    hinge->anchor = glm::vec3(1.0f, 0.0f, 0.0f);

    ASSERT_TRUE(hinge->Sync(physics));
    EXPECT_TRUE(hinge->IsBuilt());
    EXPECT_EQ(physics.GetConstraintCount(), 1u);

    Run(90, hinge.get());
    const glm::dvec3 at = Where(swing);
    EXPECT_LT(at.y, 4.9);
    EXPECT_NEAR(glm::length(at - glm::dvec3(1.0, 5.0, 0.0)), 1.0, 0.05);
    EXPECT_NEAR(at.z, 0.0, 0.01);
}

TEST_F(ConstraintComponents, HingeLimitsStopTheSwing)
{
    auto swing = MakeActor("Swing", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = swing->AddComponent<HingeConstraint>();
    hinge->axis = glm::vec3(0.0f, 0.0f, 1.0f);
    hinge->anchor = glm::vec3(1.0f, 0.0f, 0.0f);
    hinge->limited = true;
    hinge->minAngle = -10.0f;
    hinge->maxAngle = 10.0f;

    ASSERT_TRUE(hinge->Sync(physics));
    Run(180, hinge.get());
    EXPECT_LE(std::abs(hinge->GetAngle()), 11.0f);
    EXPECT_GT(std::abs(hinge->GetAngle()), 8.0f);
}

TEST_F(ConstraintComponents, FixedHoldsTheRelativePose)
{
    auto anchor = MakeActor("Anchor", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Static);
    auto hanging = MakeActor("Hanging", glm::vec3(2.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto fixed = hanging->AddComponent<FixedConstraint>();
    fixed->ConnectTo(anchor);

    ASSERT_TRUE(fixed->Sync(physics));
    Run(120, fixed.get());
    const glm::dvec3 at = Where(hanging);
    EXPECT_NEAR(at.x, 2.0, 0.05);
    EXPECT_NEAR(at.y, 5.0, 0.05);
}

TEST_F(ConstraintComponents, TargetIsFoundByTag)
{
    const Core::Tag tag = Core::TagRegistry::Get().Resolve("Test.ConstraintFrame");
    auto frame = MakeActor("Frame", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Static);
    frame->GetTags().Add(tag);
    auto door = MakeActor("Door", glm::vec3(1.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = door->AddComponent<HingeConstraint>();
    hinge->target = Core::TagQuery::Parse("Test.ConstraintFrame");

    ASSERT_TRUE(hinge->Sync(physics));
    EXPECT_EQ(hinge->GetTarget(), frame);
}

TEST_F(ConstraintComponents, WaitsForATargetThatDoesNotExistYet)
{
    auto door = MakeActor("Door", glm::vec3(1.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = door->AddComponent<HingeConstraint>();
    hinge->target = Core::TagQuery::Parse("Test.ConstraintMissing");

    EXPECT_FALSE(hinge->Sync(physics));
    EXPECT_FALSE(hinge->IsBuilt());
    EXPECT_EQ(physics.GetConstraintCount(), 0u);
}

TEST_F(ConstraintComponents, DistanceActsAsARope)
{
    auto ball = MakeActor("Ball", glm::vec3(0.0f, 10.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto rope = ball->AddComponent<DistanceConstraint>();
    rope->targetAnchor = glm::vec3(0.0f, 10.0f, 0.0f);
    rope->minDistance = 0.0f;
    rope->maxDistance = 3.0f;

    ASSERT_TRUE(rope->Sync(physics));
    Run(240, rope.get());
    EXPECT_NEAR(Where(ball).y, 7.0, 0.15);
}

TEST_F(ConstraintComponents, SliderLimitsAndMotor)
{
    auto cart = MakeActor("Cart", glm::vec3(0.0f, 2.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto slider = cart->AddComponent<SliderConstraint>();
    slider->axis = glm::vec3(1.0f, 0.0f, 0.0f);
    slider->limited = true;
    slider->minLimit = 0.0f;
    slider->maxLimit = 2.0f;
    slider->motorMaxForce = 1.0e7f;

    ASSERT_TRUE(slider->Sync(physics));
    slider->SetMotor(ConstraintMotor::Velocity, 2.0f);
    Run(30, slider.get());
    EXPECT_GT(slider->GetPosition(), 0.5f);
    EXPECT_NEAR(Where(cart).y, 2.0, 0.05);

    Run(180, slider.get());
    EXPECT_NEAR(slider->GetPosition(), 2.0f, 0.1f);
}

TEST_F(ConstraintComponents, PointConeAndSwingTwistBuild)
{
    auto a = MakeActor("A", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto b = MakeActor("B", glm::vec3(3.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto c = MakeActor("C", glm::vec3(6.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);

    auto point = a->AddComponent<PointConstraint>();
    point->anchor = glm::vec3(1.0f, 0.0f, 0.0f);
    auto cone = b->AddComponent<ConeConstraint>();
    auto swingTwist = c->AddComponent<SwingTwistConstraint>();

    EXPECT_TRUE(point->Sync(physics));
    EXPECT_TRUE(cone->Sync(physics));
    EXPECT_TRUE(swingTwist->Sync(physics));
    EXPECT_EQ(physics.GetConstraintCount(), 3u);

    for (int i = 0; i < 120; ++i)
    {
        point->Sync(physics);
        cone->Sync(physics);
        swingTwist->Sync(physics);
        physics.StepSimulation(kStep, false);
    }
    EXPECT_LT(Where(a).y, 5.0);
    EXPECT_GT(Where(b).y, 3.0);
    EXPECT_GT(Where(c).y, 3.0);
}

TEST_F(ConstraintComponents, BreaksAboveTheForce)
{
    auto anchor = MakeActor("Anchor", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Static);
    auto load = MakeActor("Load", glm::vec3(0.0f, 4.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto fixed = load->AddComponent<FixedConstraint>();
    fixed->ConnectTo(anchor);
    fixed->breakForce = 100.0f;

    int events = 0;
    context.GetEventDispatcher()->subscribeNamed("constraint.broken", [&events](const Events::NamedEvent&) { ++events; });

    ASSERT_TRUE(fixed->Sync(physics));
    Run(60, fixed.get());

    EXPECT_TRUE(fixed->IsBroken());
    EXPECT_FALSE(fixed->IsBuilt());
    EXPECT_EQ(physics.GetConstraintCount(), 0u);
    EXPECT_EQ(events, 1);
    EXPECT_FALSE(fixed->Sync(physics));

    fixed->Repair();
    EXPECT_FALSE(fixed->IsBroken());
}

TEST_F(ConstraintComponents, StaysBelowTheBreakForce)
{
    auto anchor = MakeActor("Anchor", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Static);
    auto load = MakeActor("Load", glm::vec3(0.0f, 4.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto fixed = load->AddComponent<FixedConstraint>();
    fixed->ConnectTo(anchor);
    fixed->breakForce = 1.0e9f;

    ASSERT_TRUE(fixed->Sync(physics));
    Run(60, fixed.get());
    EXPECT_FALSE(fixed->IsBroken());
    EXPECT_TRUE(fixed->IsBuilt());
}

TEST_F(ConstraintComponents, RemovingABodyDropsItsConstraints)
{
    auto anchor = MakeActor("Anchor", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Static);
    auto load = MakeActor("Load", glm::vec3(0.0f, 4.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto fixed = load->AddComponent<FixedConstraint>();
    fixed->ConnectTo(anchor);
    ASSERT_TRUE(fixed->Sync(physics));

    anchor->GetComponent<PhysicsBody>()->RemoveBody();
    EXPECT_FALSE(fixed->IsBuilt());
    EXPECT_EQ(physics.GetConstraintCount(), 0u);
    Run(10);

    EXPECT_FALSE(fixed->Sync(physics));
}

TEST_F(ConstraintComponents, RebuildsWhenTheTargetBodyIsRecreated)
{
    auto anchor = MakeActor("Anchor", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Static);
    auto load = MakeActor("Load", glm::vec3(0.0f, 4.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto fixed = load->AddComponent<FixedConstraint>();
    fixed->ConnectTo(anchor);
    ASSERT_TRUE(fixed->Sync(physics));

    anchor->GetComponent<PhysicsBody>()->CreateBody(JPH::EMotionType::Static);
    EXPECT_FALSE(fixed->IsBuilt());
    EXPECT_TRUE(fixed->Sync(physics));
    EXPECT_EQ(physics.GetConstraintCount(), 1u);
}

TEST_F(ConstraintComponents, ConnectedBodiesDoNotCollideUnlessAsked)
{
    auto anchor = MakeActor("Anchor", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Static);
    auto load = MakeActor("Load", glm::vec3(0.0f, 4.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto point = load->AddComponent<PointConstraint>();
    point->ConnectTo(anchor);
    const JPH::BodyID a = anchor->GetComponent<PhysicsBody>()->GetBodyID();
    const JPH::BodyID b = load->GetComponent<PhysicsBody>()->GetBodyID();

    ASSERT_TRUE(point->Sync(physics));
    EXPECT_TRUE(physics.Collision().IsIgnoring(a, b));

    point->Release();
    EXPECT_FALSE(physics.Collision().IsIgnoring(a, b));

    point->collideConnected = true;
    ASSERT_TRUE(point->Sync(physics));
    EXPECT_FALSE(physics.Collision().IsIgnoring(a, b));
}

TEST_F(ConstraintComponents, EditingAFieldRebuilds)
{
    auto swing = MakeActor("Swing", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = swing->AddComponent<HingeConstraint>();
    ASSERT_TRUE(hinge->Sync(physics));
    ASSERT_TRUE(hinge->IsBuilt());

    EXPECT_TRUE(hinge->ApplyField("limited", true));
    EXPECT_TRUE(hinge->Sync(physics));
    EXPECT_TRUE(hinge->IsBuilt());
    EXPECT_EQ(physics.GetConstraintCount(), 1u);
}

TEST_F(ConstraintComponents, DeactivatingReleases)
{
    auto swing = MakeActor("Swing", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = swing->AddComponent<HingeConstraint>();
    ASSERT_TRUE(hinge->Sync(physics));

    hinge->DeActivate();
    EXPECT_EQ(physics.GetConstraintCount(), 0u);
    EXPECT_FALSE(hinge->Sync(physics));

    hinge->Activate();
    EXPECT_TRUE(hinge->Sync(physics));
}

TEST_F(ConstraintComponents, EditModeBuildsNothing)
{
    auto swing = MakeActor("Swing", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = swing->AddComponent<HingeConstraint>();
    ASSERT_TRUE(hinge->Sync(physics));

    physics.TickConstraints(false);
    EXPECT_FALSE(hinge->IsBuilt());
    EXPECT_EQ(physics.GetConstraintCount(), 0u);
}

TEST_F(ConstraintComponents, DestroyingTheComponentReleases)
{
    auto swing = MakeActor("Swing", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = swing->AddComponent<HingeConstraint>();
    ASSERT_TRUE(hinge->Sync(physics));

    swing->RemoveComponent(hinge);
    hinge.reset();
    EXPECT_EQ(physics.GetConstraintCount(), 0u);
}

TEST_F(ConstraintComponents, SaveAndLoadKeepsTheFields)
{
    auto actor = MakeActor("Door", glm::vec3(0.0f, 1.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = actor->AddComponent<HingeConstraint>();
    hinge->axis = glm::vec3(0.0f, 0.0f, 1.0f);
    hinge->limited = true;
    hinge->minAngle = -30.0f;
    hinge->maxAngle = 120.0f;
    hinge->motor = ConstraintMotor::Position;
    hinge->motorTarget = 45.0f;
    hinge->breakForce = 500.0f;
    hinge->target = Core::TagQuery::Parse("Door.Frame");
    const ordered_json saved = hinge->Serialize();
    EXPECT_EQ(saved["type"], "constraintHinge");

    auto other = MakeActor("Other", glm::vec3(0.0f), JPH::EMotionType::Dynamic);
    auto loaded = other->AddComponent<HingeConstraint>();
    loaded->Deserialize(saved);
    EXPECT_EQ(loaded->axis, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(loaded->limited);
    EXPECT_FLOAT_EQ(loaded->minAngle, -30.0f);
    EXPECT_FLOAT_EQ(loaded->maxAngle, 120.0f);
    EXPECT_EQ(loaded->motor, ConstraintMotor::Position);
    EXPECT_FLOAT_EQ(loaded->motorTarget, 45.0f);
    EXPECT_FLOAT_EQ(loaded->breakForce, 500.0f);
    EXPECT_EQ(loaded->target.ToString(), "Door.Frame");
}

TEST_F(ConstraintComponents, CloneCopiesTheFieldsButNotTheConstraint)
{
    auto swing = MakeActor("Swing", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto rope = swing->AddComponent<DistanceConstraint>();
    rope->maxDistance = 4.0f;
    ASSERT_TRUE(rope->Sync(physics));

    auto copy = std::static_pointer_cast<DistanceConstraint>(rope->Clone());
    EXPECT_FLOAT_EQ(copy->maxDistance, 4.0f);
    EXPECT_FALSE(copy->IsBuilt());
    EXPECT_EQ(physics.GetConstraintCount(), 1u);
}

TEST_F(ConstraintComponents, TheWorldKnowsItsConstraints)
{
    auto& data = world->Ext<PhysicsWorldData>();
    auto swing = MakeActor("Swing", glm::vec3(0.0f, 5.0f, 0.0f), JPH::EMotionType::Dynamic);
    auto hinge = swing->AddComponent<HingeConstraint>();
    auto point = swing->AddComponent<PointConstraint>();
    EXPECT_EQ(data.constraints.size(), 2u);

    swing->RemoveComponent(point);
    EXPECT_EQ(data.constraints.size(), 1u);
}
