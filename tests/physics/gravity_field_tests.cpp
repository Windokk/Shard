#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "engine/physics/gravity.hpp"
#include "engine/physics/gravity_field.hpp"
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
using Shard::Engine::Objects::Components::GravityVolume;
using Shard::Engine::Objects::Components::PhysicsBody;
using Shard::Engine::Objects::Components::PointGravityField;
using Shard::Engine::Objects::Components::RadialGravityField;
using Shard::Engine::Objects::Components::UniformGravityField;

namespace {

    constexpr float kStep = 1.0f / 60.0f;
    const glm::vec3 kEarth(0.0f, -9.81f, 0.0f);

    GravityFieldData Uniform(glm::vec3 direction, float strength, int priority = 0)
    {
        GravityFieldData f;
        f.kind = GravityFieldKind::Uniform;
        f.direction = direction;
        f.strength = strength;
        f.priority = priority;
        return f;
    }

    GravityFieldData Point(glm::dvec3 at, float strength, float radius, GravityFalloff falloff)
    {
        GravityFieldData f;
        f.kind = GravityFieldKind::Point;
        f.position = at;
        f.strength = strength;
        f.radius = radius;
        f.falloff = falloff;
        return f;
    }

    GravityFieldData Radial(glm::dvec3 at, float strength, float radius, float reach, GravityFalloff falloff)
    {
        GravityFieldData f;
        f.kind = GravityFieldKind::Radial;
        f.position = at;
        f.strength = strength;
        f.radius = radius;
        f.reach = reach;
        f.falloff = falloff;
        return f;
    }

    GravityFieldData Volume(glm::dvec3 at, glm::vec3 halfExtent, GravityVolumeShape shape, glm::vec3 direction, float strength, float fade = 0.0f)
    {
        GravityFieldData f;
        f.kind = GravityFieldKind::Volume;
        f.position = at;
        f.halfExtent = halfExtent;
        f.shape = shape;
        f.direction = direction;
        f.strength = strength;
        f.fadeDistance = fade;
        return f;
    }

    std::vector<GravityFieldData> Sorted(std::vector<GravityFieldData> fields)
    {
        for (size_t i = 0; i < fields.size(); ++i)
            fields[i].order = (uint32_t)i;
        SortGravityFields(fields);
        return fields;
    }

    void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
    {
        EXPECT_NEAR(a.x, b.x, tolerance);
        EXPECT_NEAR(a.y, b.y, tolerance);
        EXPECT_NEAR(a.z, b.z, tolerance);
    }
}

// ---------------------------------------------------------------- the fields

TEST(GravityFields, UniformPointsWhereItIsToldAndFollowsItsRotation)
{
    glm::vec3 a;
    float coverage;

    ASSERT_TRUE(EvaluateGravityField(Uniform(glm::vec3(0.0f, -2.0f, 0.0f), 4.0f), glm::dvec3(100.0, 5.0, 3.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f, -4.0f, 0.0f));
    EXPECT_FLOAT_EQ(coverage, 1.0f);

    // Turned a quarter around Z : down becomes +X
    GravityFieldData turned = Uniform(glm::vec3(0.0f, -1.0f, 0.0f), 3.0f);
    turned.rotation = glm::angleAxis(glm::radians(90.0), glm::dvec3(0.0, 0.0, 1.0));
    ASSERT_TRUE(EvaluateGravityField(turned, glm::dvec3(0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(3.0f, 0.0f, 0.0f), 1e-5f);

    // A degenerate direction is no gravity, not a NaN
    ASSERT_TRUE(EvaluateGravityField(Uniform(glm::vec3(0.0f), 9.81f), glm::dvec3(0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f));
}

TEST(GravityFields, PointPullsTowardItsPositionAndStopsAtItsRadius)
{
    glm::vec3 a;
    float coverage;

    GravityFieldData constant = Point(glm::dvec3(10.0, 0.0, 0.0), 6.0f, 20.0f, GravityFalloff::Constant);
    ASSERT_TRUE(EvaluateGravityField(constant, glm::dvec3(0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(6.0f, 0.0f, 0.0f));

    // Past the radius it does not reach
    EXPECT_FALSE(EvaluateGravityField(constant, glm::dvec3(-10.0, 0.0, 0.0), a, coverage));

    // Linear : half way, half the strength
    GravityFieldData linear = Point(glm::dvec3(0.0), 8.0f, 10.0f, GravityFalloff::Linear);
    ASSERT_TRUE(EvaluateGravityField(linear, glm::dvec3(0.0, 5.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f, -4.0f, 0.0f));

    // Inverse square : twice as far, a quarter of the pull. 0 radius : no limit
    GravityFieldData inverse = Point(glm::dvec3(0.0), 8.0f, 0.0f, GravityFalloff::InverseSquare);
    inverse.referenceDistance = 2.0f;
    ASSERT_TRUE(EvaluateGravityField(inverse, glm::dvec3(2.0, 0.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(-8.0f, 0.0f, 0.0f));
    ASSERT_TRUE(EvaluateGravityField(inverse, glm::dvec3(4.0, 0.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(-2.0f, 0.0f, 0.0f));
    ASSERT_TRUE(EvaluateGravityField(inverse, glm::dvec3(1.0e6, 0.0, 0.0), a, coverage));      // far away, still defined
    EXPECT_LT(std::abs(a.x), 1e-6f);

    // A negative strength pushes away
    GravityFieldData repel = Point(glm::dvec3(0.0), -5.0f, 10.0f, GravityFalloff::Constant);
    ASSERT_TRUE(EvaluateGravityField(repel, glm::dvec3(3.0, 0.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(5.0f, 0.0f, 0.0f));
}

TEST(GravityFields, PointDoesNotBlowUpAtItsCenter)
{
    glm::vec3 a;
    float coverage;

    GravityFieldData f = Point(glm::dvec3(0.0), 9.81f, 10.0f, GravityFalloff::InverseSquare);
    f.minDistance = 0.5f;

    // Right on it : no direction, so no pull. Next to it : capped by minDistance
    ASSERT_TRUE(EvaluateGravityField(f, glm::dvec3(0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f));

    ASSERT_TRUE(EvaluateGravityField(f, glm::dvec3(0.001, 0.0, 0.0), a, coverage));
    EXPECT_TRUE(std::isfinite(a.x));
    EXPECT_NEAR(a.x, -9.81f * 4.0f, 1e-3f);      // (1 / 0.5)^2
}

TEST(GravityFields, PointFadesOutInsideItsRadius)
{
    glm::vec3 a;
    float coverage;

    GravityFieldData f = Point(glm::dvec3(0.0), 5.0f, 10.0f, GravityFalloff::Constant);
    f.fadeDistance = 4.0f;

    ASSERT_TRUE(EvaluateGravityField(f, glm::dvec3(5.0, 0.0, 0.0), a, coverage));
    EXPECT_FLOAT_EQ(coverage, 1.0f);

    ASSERT_TRUE(EvaluateGravityField(f, glm::dvec3(8.0, 0.0, 0.0), a, coverage));
    EXPECT_NEAR(coverage, 0.5f, 1e-5f);

    EXPECT_FALSE(EvaluateGravityField(f, glm::dvec3(10.0, 0.0, 0.0), a, coverage));
}

TEST(GravityFields, RadialIsTheSurfaceGravityOfAPlanet)
{
    glm::vec3 a;
    float coverage;

    GravityFieldData planet = Radial(glm::dvec3(0.0), 10.0f, 100.0f, 0.0f, GravityFalloff::InverseSquare);

    // On the surface : the strength, toward the center, whatever the side
    ASSERT_TRUE(EvaluateGravityField(planet, glm::dvec3(100.0, 0.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(-10.0f, 0.0f, 0.0f), 1e-4f);
    ASSERT_TRUE(EvaluateGravityField(planet, glm::dvec3(0.0, -100.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f, 10.0f, 0.0f), 1e-4f);

    // Twice the radius away : a quarter
    ASSERT_TRUE(EvaluateGravityField(planet, glm::dvec3(0.0, 0.0, 200.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f, 0.0f, -2.5f), 1e-4f);

    // Inside, as a ball of uniform density : in proportion to the distance to the center
    ASSERT_TRUE(EvaluateGravityField(planet, glm::dvec3(50.0, 0.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(-5.0f, 0.0f, 0.0f), 1e-4f);

    // ... or the surface strength all the way down
    planet.solidInterior = false;
    ASSERT_TRUE(EvaluateGravityField(planet, glm::dvec3(50.0, 0.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(-10.0f, 0.0f, 0.0f), 1e-4f);
}

TEST(GravityFields, RadialStopsAtItsReachAndFadesBeforeIt)
{
    glm::vec3 a;
    float coverage;

    GravityFieldData planet = Radial(glm::dvec3(0.0), 10.0f, 100.0f, 50.0f, GravityFalloff::Constant);
    planet.fadeDistance = 20.0f;

    ASSERT_TRUE(EvaluateGravityField(planet, glm::dvec3(110.0, 0.0, 0.0), a, coverage));
    EXPECT_FLOAT_EQ(coverage, 1.0f);

    ASSERT_TRUE(EvaluateGravityField(planet, glm::dvec3(140.0, 0.0, 0.0), a, coverage));
    EXPECT_NEAR(coverage, 0.5f, 1e-5f);

    EXPECT_FALSE(EvaluateGravityField(planet, glm::dvec3(150.0, 0.0, 0.0), a, coverage));

    // Linear toward the end of the reach
    planet.falloff = GravityFalloff::Linear;
    planet.fadeDistance = 0.0f;
    ASSERT_TRUE(EvaluateGravityField(planet, glm::dvec3(125.0, 0.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(-5.0f, 0.0f, 0.0f), 1e-4f);
}

TEST(GravityFields, RadialAtItsCenterIsFiniteAndZero)
{
    glm::vec3 a;
    float coverage;

    ASSERT_TRUE(EvaluateGravityField(Radial(glm::dvec3(5.0), 10.0f, 10.0f, 0.0f, GravityFalloff::InverseSquare), glm::dvec3(5.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f));
}

TEST(GravityFields, VolumeCoversItsShape)
{
    glm::vec3 a;
    float coverage;
    const glm::vec3 half(4.0f, 2.0f, 4.0f);
    const glm::vec3 down(0.0f, -1.0f, 0.0f);

    GravityFieldData box = Volume(glm::dvec3(10.0, 0.0, 0.0), half, GravityVolumeShape::Box, down, 3.0f);
    EXPECT_TRUE(EvaluateGravityField(box, glm::dvec3(13.5, 1.5, -3.5), a, coverage));     // a corner region
    EXPECT_FALSE(EvaluateGravityField(box, glm::dvec3(14.5, 0.0, 0.0), a, coverage));
    EXPECT_FALSE(EvaluateGravityField(box, glm::dvec3(10.0, 2.5, 0.0), a, coverage));

    // The sphere is inscribed in the box : the corner of the box is outside it
    GravityFieldData sphere = Volume(glm::dvec3(0.0), glm::vec3(4.0f), GravityVolumeShape::Sphere, down, 3.0f);
    EXPECT_TRUE(EvaluateGravityField(sphere, glm::dvec3(3.0, 0.0, 0.0), a, coverage));
    EXPECT_FALSE(EvaluateGravityField(sphere, glm::dvec3(3.0, 3.0, 3.0), a, coverage));

    // The cylinder stands along Y
    GravityFieldData cylinder = Volume(glm::dvec3(0.0), half, GravityVolumeShape::Cylinder, down, 3.0f);
    EXPECT_TRUE(EvaluateGravityField(cylinder, glm::dvec3(3.5, 1.5, 0.0), a, coverage));
    EXPECT_FALSE(EvaluateGravityField(cylinder, glm::dvec3(3.5, 2.5, 0.0), a, coverage));
    EXPECT_FALSE(EvaluateGravityField(cylinder, glm::dvec3(3.5, 0.0, 3.5), a, coverage));      // inside the box, outside the circle
}

TEST(GravityFields, VolumeFollowsItsRotationAndScale)
{
    glm::vec3 a;
    float coverage;

    // 10 long along its own X, turned a quarter around Y : that length is now along Z
    GravityFieldData f = Volume(glm::dvec3(0.0), glm::vec3(5.0f, 1.0f, 1.0f), GravityVolumeShape::Box, glm::vec3(0.0f, -1.0f, 0.0f), 2.0f);
    f.rotation = glm::angleAxis(glm::radians(90.0), glm::dvec3(0.0, 1.0, 0.0));
    EXPECT_TRUE(EvaluateGravityField(f, glm::dvec3(0.0, 0.0, 4.0), a, coverage));
    EXPECT_FALSE(EvaluateGravityField(f, glm::dvec3(4.0, 0.0, 0.0), a, coverage));

    // Doubled by the scale of the actor
    f.rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);
    EXPECT_FALSE(EvaluateGravityField(f, glm::dvec3(8.0, 0.0, 0.0), a, coverage));
    f.scale = glm::vec3(2.0f, 1.0f, 1.0f);
    EXPECT_TRUE(EvaluateGravityField(f, glm::dvec3(8.0, 0.0, 0.0), a, coverage));

    // A negative scale (a mirrored actor) covers the same region
    f.scale = glm::vec3(-2.0f, 1.0f, 1.0f);
    EXPECT_TRUE(EvaluateGravityField(f, glm::dvec3(8.0, 0.0, 0.0), a, coverage));
}

TEST(GravityFields, VolumeFadesInFromItsEdge)
{
    glm::vec3 a;
    float coverage;

    GravityFieldData f = Volume(glm::dvec3(0.0), glm::vec3(10.0f), GravityVolumeShape::Box, glm::vec3(0.0f, -1.0f, 0.0f), 2.0f, 4.0f);

    ASSERT_TRUE(EvaluateGravityField(f, glm::dvec3(0.0), a, coverage));
    EXPECT_FLOAT_EQ(coverage, 1.0f);

    ASSERT_TRUE(EvaluateGravityField(f, glm::dvec3(8.0, 0.0, 0.0), a, coverage));      // 2 inside the edge, over 4
    EXPECT_NEAR(coverage, 0.5f, 1e-5f);

    EXPECT_FALSE(EvaluateGravityField(f, glm::dvec3(10.0, 0.0, 0.0), a, coverage));
}

TEST(GravityFields, VolumeDirections)
{
    glm::vec3 a;
    float coverage;

    GravityFieldData toCenter = Volume(glm::dvec3(0.0), glm::vec3(10.0f), GravityVolumeShape::Box, glm::vec3(0.0f, -1.0f, 0.0f), 4.0f);
    toCenter.volumeDirection = GravityVolumeDirection::ToCenter;
    ASSERT_TRUE(EvaluateGravityField(toCenter, glm::dvec3(0.0, 5.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f, -4.0f, 0.0f));

    // To the Y axis : the height does not matter, the gravity is horizontal. Negative : away from it (a rotating station)
    GravityFieldData toAxis = toCenter;
    toAxis.volumeDirection = GravityVolumeDirection::ToAxis;
    ASSERT_TRUE(EvaluateGravityField(toAxis, glm::dvec3(5.0, 7.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(-4.0f, 0.0f, 0.0f));

    toAxis.strength = -4.0f;
    ASSERT_TRUE(EvaluateGravityField(toAxis, glm::dvec3(0.0, 7.0, 5.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f, 0.0f, 4.0f));

    // On the axis itself there is no direction
    ASSERT_TRUE(EvaluateGravityField(toAxis, glm::dvec3(0.0, 3.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(0.0f));
}

// ---------------------------------------------------------------- combining

TEST(GravitySampling, NoFieldIsTheWorldGravity)
{
    ExpectVec3Near(SampleGravityFields({}, glm::dvec3(1.0, 2.0, 3.0), kEarth), kEarth);
}

TEST(GravitySampling, AFieldThatDoesNotReachLeavesTheWorldGravity)
{
    const auto fields = Sorted({ Point(glm::dvec3(100.0), 5.0f, 10.0f, GravityFalloff::Constant) });
    EXPECT_EQ(SampleGravityFields(fields, glm::dvec3(0.0), kEarth), kEarth);
}

TEST(GravitySampling, AnOverridingFieldReplacesTheWorldGravity)
{
    const auto fields = Sorted({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 3.0f) });
    ExpectVec3Near(SampleGravityFields(fields, glm::dvec3(0.0), kEarth), glm::vec3(3.0f, 0.0f, 0.0f));
}

TEST(GravitySampling, TheHighestPriorityWins)
{
    const auto fields = Sorted({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 1.0f, 0), Uniform(glm::vec3(0.0f, 0.0f, 1.0f), 2.0f, 5), Uniform(glm::vec3(0.0f, 1.0f, 0.0f), 3.0f, 2) });
    ExpectVec3Near(SampleGravityFields(fields, glm::dvec3(0.0), kEarth), glm::vec3(0.0f, 0.0f, 2.0f));
}

TEST(GravitySampling, ALowerFieldShowsThroughWhereTheHigherOneFades)
{
    // A world-wide field below, a volume above it fading in over 4 : half way in, an even mix of the two
    GravityFieldData volume = Volume(glm::dvec3(0.0), glm::vec3(10.0f), GravityVolumeShape::Box, glm::vec3(1.0f, 0.0f, 0.0f), 10.0f, 4.0f);
    volume.priority = 1;
    const auto fields = Sorted({ Uniform(glm::vec3(0.0f, -1.0f, 0.0f), 6.0f, 0), volume });

    ExpectVec3Near(SampleGravityFields(fields, glm::dvec3(0.0), kEarth), glm::vec3(10.0f, 0.0f, 0.0f));                          // deep inside
    ExpectVec3Near(SampleGravityFields(fields, glm::dvec3(8.0, 0.0, 0.0), kEarth), glm::vec3(5.0f, -3.0f, 0.0f));                // half faded
    ExpectVec3Near(SampleGravityFields(fields, glm::dvec3(50.0, 0.0, 0.0), kEarth), glm::vec3(0.0f, -6.0f, 0.0f));               // outside

    // With nothing below, it fades into the world's gravity
    const auto alone = Sorted({ volume });
    ExpectVec3Near(SampleGravityFields(alone, glm::dvec3(8.0, 0.0, 0.0), kEarth), glm::vec3(5.0f, -4.905f, 0.0f));
}

TEST(GravitySampling, FieldsOfTheSamePriorityBlend)
{
    const auto fields = Sorted({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 4.0f), Uniform(glm::vec3(0.0f, 1.0f, 0.0f), 4.0f) });
    ExpectVec3Near(SampleGravityFields(fields, glm::dvec3(0.0), kEarth), glm::vec3(2.0f, 2.0f, 0.0f));
}

TEST(GravitySampling, AdditiveFieldsAreSummedOnTop)
{
    GravityFieldData moon = Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 2.0f, -50);
    moon.blend = GravityBlend::Additive;

    // Whatever its priority, and over any override
    const auto fields = Sorted({ Uniform(glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, 10), moon });
    ExpectVec3Near(SampleGravityFields(fields, glm::dvec3(0.0), kEarth), glm::vec3(2.0f, -5.0f, 0.0f));

    // Alone it adds to the world's gravity
    ExpectVec3Near(SampleGravityFields(Sorted({ moon }), glm::dvec3(0.0), kEarth), glm::vec3(2.0f, -9.81f, 0.0f));
}

TEST(GravitySampling, TheResultDoesNotDependOnTheOrderTheFieldsWereGivenIn)
{
    std::vector<GravityFieldData> a = { Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 4.0f), Point(glm::dvec3(0.0), 3.0f, 100.0f, GravityFalloff::Constant) };
    std::vector<GravityFieldData> b = { a[1], a[0] };

    for (auto* list : { &a, &b })
        for (size_t i = 0; i < list->size(); ++i)
            (*list)[i].order = (uint32_t)((*list)[i].kind == GravityFieldKind::Uniform ? 0 : 1);   // the order is a property of the field, not of the list
    SortGravityFields(a);
    SortGravityFields(b);

    const glm::dvec3 p(7.0, 1.0, 2.0);
    EXPECT_EQ(SampleGravityFields(a, p, kEarth), SampleGravityFields(b, p, kEarth));
}

TEST(GravitySampling, BodiesTakeTheirGravityFromTheirMode)
{
    const auto fields = Sorted({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 3.0f) });
    const glm::dvec3 p(0.0);

    BodyGravity body;
    body.custom = glm::vec3(0.0f, 7.0f, 0.0f);

    body.mode = GravityMode::Fields;
    ExpectVec3Near(ResolveBodyGravity(fields, body, p, kEarth), glm::vec3(3.0f, 0.0f, 0.0f));
    body.mode = GravityMode::World;
    ExpectVec3Near(ResolveBodyGravity(fields, body, p, kEarth), kEarth);
    body.mode = GravityMode::Custom;
    ExpectVec3Near(ResolveBodyGravity(fields, body, p, kEarth), glm::vec3(0.0f, 7.0f, 0.0f));
}

// ---------------------------------------------------------------- in the simulation

namespace {

    class GravitySimulation : public ::testing::Test {
        protected:
            void SetUp() override
            {
                physics.Init(kEarth);
            }

            void TearDown() override
            {
                physics.Shutdown();
            }

            JPH::BodyID AddBall(JPH::RVec3 at, float gravityFactor = 1.0f)
            {
                JPH::BodyCreationSettings settings(new JPH::SphereShape(0.5f), at, JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
                settings.mLinearDamping = 0.0f;
                settings.mAngularDamping = 0.0f;
                settings.mGravityFactor = gravityFactor;
                return physics.CreateBody(settings, nullptr);
            }

            void Run(int steps)
            {
                for (int i = 0; i < steps; ++i)
                    physics.StepSimulation(kStep, false);
            }

            glm::dvec3 Where(JPH::BodyID id)
            {
                return ToGLMD(physics.GetBodyInterface().GetPosition(id));
            }

            PhysicsManager physics;
    };
}

TEST_F(GravitySimulation, WithoutFieldsTheWorldGravityIsUntouched)
{
    const JPH::BodyID ball = AddBall(JPH::RVec3(0, 0, 0));
    Run(60);

    // 1/2 g t^2, plus half a step of velocity for the integrator
    EXPECT_NEAR(Where(ball).y, -(0.5 * 9.81 + 0.5 * 9.81 * kStep), 0.02);
    EXPECT_NEAR(Where(ball).x, 0.0, 1e-9);
}

TEST_F(GravitySimulation, AUniformFieldRedirectsTheFall)
{
    const JPH::BodyID ball = AddBall(JPH::RVec3(0, 0, 0));
    physics.SetGravityFields({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f) });
    Run(60);

    // The world's pull down is gone, replaced by 5 along X
    EXPECT_NEAR(Where(ball).x, 0.5 * 5.0 + 0.5 * 5.0 * kStep, 0.02);
    EXPECT_NEAR(Where(ball).y, 0.0, 1e-3);
    EXPECT_NEAR(Where(ball).z, 0.0, 1e-9);
}

TEST_F(GravitySimulation, AFieldMakesTheSameGravityForAnyMassOfBody)
{
    const JPH::BodyID light = AddBall(JPH::RVec3(0, 0, 0));
    JPH::BodyCreationSettings settings(new JPH::SphereShape(0.5f), JPH::RVec3(0, 10, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
    settings.mLinearDamping = 0.0f;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = 5000.0f;
    const JPH::BodyID heavy = physics.CreateBody(settings, nullptr);

    physics.SetGravityFields({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f) });
    Run(60);

    EXPECT_NEAR(Where(light).x, Where(heavy).x, 1e-3);
    EXPECT_NEAR(Where(heavy).y, 10.0, 1e-2);
}

TEST_F(GravitySimulation, TheGravityFactorScalesTheGravityOfTheFields)
{
    const JPH::BodyID normal = AddBall(JPH::RVec3(0, 0, 0), 1.0f);
    const JPH::BodyID twice = AddBall(JPH::RVec3(0, 20, 0), 2.0f);
    const JPH::BodyID none = AddBall(JPH::RVec3(0, 40, 0), 0.0f);
    const JPH::BodyID upwards = AddBall(JPH::RVec3(0, 60, 0), -1.0f);

    physics.SetGravityFields({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 4.0f) });
    Run(60);

    const double travel = 0.5 * 4.0 + 0.5 * 4.0 * kStep;
    EXPECT_NEAR(Where(normal).x, travel, 0.02);
    EXPECT_NEAR(Where(twice).x, 2.0 * travel, 0.04);
    EXPECT_NEAR(Where(none).x, 0.0, 1e-6);
    EXPECT_NEAR(Where(upwards).x, -travel, 0.02);
    EXPECT_NEAR(Where(none).y, 40.0, 1e-3);
}

TEST_F(GravitySimulation, ARadialFieldPullsToItsCenterFromAnySide)
{
    const JPH::BodyID right = AddBall(JPH::RVec3(30, 0, 0));
    const JPH::BodyID above = AddBall(JPH::RVec3(0, 30, 0));
    const JPH::BodyID behind = AddBall(JPH::RVec3(0, 0, -30));

    physics.SetGravityFields({ Radial(glm::dvec3(0.0), 9.81f, 10.0f, 0.0f, GravityFalloff::Constant) });
    Run(30);

    EXPECT_LT(Where(right).x, 30.0);
    EXPECT_NEAR(Where(right).y, 0.0, 1e-3);
    EXPECT_LT(Where(above).y, 30.0);
    EXPECT_NEAR(Where(above).x, 0.0, 1e-3);
    EXPECT_GT(Where(behind).z, -30.0);
    EXPECT_NEAR(Where(behind).y, 0.0, 1e-3);
}

TEST_F(GravitySimulation, ABodyOnTheFarSideOfAPlanetStandsOnIt)
{
#ifndef JPH_DOUBLE_PRECISION
    GTEST_SKIP() << "Jolt was built in single precision (SHARD_DOUBLE_PRECISION_PHYSICS=OFF)";
#endif
    // A planet of radius 10 centered 1e9 m from the origin, a floor on its +X side, a ball above : the ball lands, pulled along -X
    const double far = 1.0e9;
    JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(0.5f, 20.0f, 20.0f)), JPH::RVec3(far + 10.5, 0, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
    physics.CreateBody(floor, nullptr);
    const JPH::BodyID ball = AddBall(JPH::RVec3(far + 14.0, 0, 0));

    physics.SetGravityFields({ Radial(glm::dvec3(far, 0.0, 0.0), 9.81f, 10.0f, 0.0f, GravityFalloff::InverseSquare) });
    Run(240);

    // Resting on the floor's face (at far + 11), not drifted sideways
    EXPECT_NEAR(Where(ball).x - far, 11.5, 0.05);
    EXPECT_NEAR(Where(ball).y, 0.0, 0.05);
}

TEST_F(GravitySimulation, ABodyCanHaveAGravityOfItsOwn)
{
    const JPH::BodyID custom = AddBall(JPH::RVec3(0, 0, 0));
    const JPH::BodyID normal = AddBall(JPH::RVec3(20, 0, 0));
    physics.SetBodyGravity(custom, GravityMode::Custom, glm::vec3(0.0f, 4.0f, 0.0f));
    Run(60);

    // No field and no force on the other : the custom gravity works without any field in the world
    EXPECT_NEAR(Where(custom).y, 0.5 * 4.0 + 0.5 * 4.0 * kStep, 0.02);
    EXPECT_NEAR(Where(normal).y, -(0.5 * 9.81 + 0.5 * 9.81 * kStep), 0.02);
}

TEST_F(GravitySimulation, TheWorldModeIgnoresTheFields)
{
    const JPH::BodyID world = AddBall(JPH::RVec3(0, 0, 0));
    const JPH::BodyID fields = AddBall(JPH::RVec3(20, 0, 0));
    physics.SetBodyGravity(world, GravityMode::World, glm::vec3(0.0f));
    physics.SetGravityFields({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f) });
    Run(60);

    EXPECT_NEAR(Where(world).x, 0.0, 1e-6);
    EXPECT_NEAR(Where(world).y, -(0.5 * 9.81 + 0.5 * 9.81 * kStep), 0.02);
    EXPECT_GT(Where(fields).x - 20.0, 2.0);
}

TEST_F(GravitySimulation, CustomGravityFollowsTheGravityFactorToo)
{
    const JPH::BodyID ball = AddBall(JPH::RVec3(0, 0, 0), 0.5f);
    physics.SetBodyGravity(ball, GravityMode::Custom, glm::vec3(0.0f, 0.0f, 8.0f));
    Run(60);

    EXPECT_NEAR(Where(ball).z, 0.5 * 4.0 + 0.5 * 4.0 * kStep, 0.02);
    EXPECT_NEAR(Where(ball).y, 0.0, 1e-3);
}

TEST_F(GravitySimulation, ReturningToTheFieldsModeGivesTheFieldGravityBack)
{
    const JPH::BodyID ball = AddBall(JPH::RVec3(0, 0, 0));
    physics.SetBodyGravity(ball, GravityMode::Custom, glm::vec3(0.0f, 4.0f, 0.0f));
    physics.SetBodyGravity(ball, GravityMode::Fields, glm::vec3(0.0f));
    physics.SetGravityFields({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f) });
    Run(60);

    EXPECT_NEAR(Where(ball).x, 0.5 * 5.0 + 0.5 * 5.0 * kStep, 0.02);
    EXPECT_NEAR(Where(ball).y, 0.0, 1e-3);
}

TEST_F(GravitySimulation, AQueryAnswersLikeTheSimulation)
{
    physics.SetGravityFields(Sorted({ Point(glm::dvec3(0.0), 6.0f, 50.0f, GravityFalloff::Constant) }));

    ExpectVec3Near(physics.GetGravityAt(glm::dvec3(10.0, 0.0, 0.0)), glm::vec3(-6.0f, 0.0f, 0.0f));
    ExpectVec3Near(physics.GetGravityAt(glm::dvec3(100.0, 0.0, 0.0)), kEarth);

    const JPH::BodyID ball = AddBall(JPH::RVec3(0, 10, 0));
    ExpectVec3Near(physics.GetBodyGravity(ball), glm::vec3(0.0f, -6.0f, 0.0f));
    physics.SetBodyGravity(ball, GravityMode::Custom, glm::vec3(1.0f, 2.0f, 3.0f));
    ExpectVec3Near(physics.GetBodyGravity(ball), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_F(GravitySimulation, SettingTheSameFieldsAgainChangesNothing)
{
    const JPH::BodyID ball = AddBall(JPH::RVec3(0, 0, 0));
    const auto fields = Sorted({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f) });
    physics.SetGravityFields(fields);
    Run(30);
    physics.SetGravityFields(fields);
    Run(30);

    EXPECT_NEAR(Where(ball).x, 0.5 * 5.0 + 0.5 * 5.0 * kStep, 0.02);
}

// Its own managers (one at a time : Jolt keeps its type factory in a global), so not on the fixture
TEST(GravityDeterminism, TheSameFieldsGiveTheSameSimulation)
{
    auto simulate = []() {
        PhysicsManager physics;
        physics.Init(kEarth, true);
        std::vector<JPH::BodyID> balls;
        for (int i = 0; i < 20; ++i)
        {
            JPH::BodyCreationSettings settings(new JPH::SphereShape(0.5f), JPH::RVec3(double(i) * 1.1 - 10.0, 5.0 + i, double(i % 3)), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Layers::MOVING);
            balls.push_back(physics.CreateBody(settings, nullptr));
        }
        physics.SetGravityFields(Sorted({ Radial(glm::dvec3(0.0), 9.81f, 4.0f, 0.0f, GravityFalloff::InverseSquare), Uniform(glm::vec3(0.0f, 0.0f, 1.0f), 1.0f, -1) }));
        for (int s = 0; s < 120; ++s)
            physics.StepSimulation(kStep, false);
        std::vector<glm::dvec3> result;
        for (JPH::BodyID id : balls)
            result.push_back(ToGLMD(physics.GetBodyInterface().GetPosition(id)));
        physics.Shutdown();
        return result;
    };

    EXPECT_EQ(simulate(), simulate());
}

TEST_F(GravitySimulation, ASleepingBodyIsWokenWhenTheFieldsChangeItsGravity)
{
    JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(50.0f, 0.5f, 50.0f)), JPH::RVec3(0, -0.5, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
    physics.CreateBody(floor, nullptr);
    const JPH::BodyID ball = AddBall(JPH::RVec3(0, 0.6, 0));

    Run(240);
    ASSERT_FALSE(physics.GetBodyInterface().IsActive(ball)) << "the ball should be asleep on the floor";

    // A field that does not reach it leaves it asleep
    physics.SetGravityFields(Sorted({ Point(glm::dvec3(500.0, 0.0, 0.0), 5.0f, 10.0f, GravityFalloff::Constant) }));
    Run(2);
    EXPECT_FALSE(physics.GetBodyInterface().IsActive(ball));

    // One that does wakes it up
    physics.SetGravityFields(Sorted({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 10.0f) }));
    Run(2);
    EXPECT_TRUE(physics.GetBodyInterface().IsActive(ball));
    Run(60);
    EXPECT_GT(Where(ball).x, 0.5);
}

TEST_F(GravitySimulation, ASleepingBodyIsWokenWhenTheFieldsGoAway)
{
    JPH::BodyCreationSettings wall(new JPH::BoxShape(JPH::Vec3(0.5f, 50.0f, 50.0f)), JPH::RVec3(-0.5, 0, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
    physics.CreateBody(wall, nullptr);
    JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(50.0f, 0.5f, 50.0f)), JPH::RVec3(0, -50.5, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Layers::NON_MOVING);
    physics.CreateBody(floor, nullptr);
    const JPH::BodyID ball = AddBall(JPH::RVec3(0.6, 0, 0));

    // Pressed against the wall by a sideways gravity, until it sleeps
    physics.SetGravityFields(Sorted({ Uniform(glm::vec3(-1.0f, 0.0f, 0.0f), 9.81f) }));
    Run(240);
    ASSERT_FALSE(physics.GetBodyInterface().IsActive(ball));

    // The field is gone : the world's gravity takes it off the wall
    physics.SetGravityFields({});
    Run(2);
    EXPECT_TRUE(physics.GetBodyInterface().IsActive(ball));
    Run(30);
    EXPECT_LT(Where(ball).y, -0.5);
}

TEST_F(GravitySimulation, AKinematicBodyIsLeftAlone)
{
    JPH::BodyCreationSettings settings(new JPH::SphereShape(0.5f), JPH::RVec3(0, 0, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Layers::MOVING);
    const JPH::BodyID platform = physics.CreateBody(settings, nullptr);
    physics.SetGravityFields({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f) });
    physics.SetBodyGravity(platform, GravityMode::Custom, glm::vec3(0.0f, 1.0f, 0.0f));
    Run(30);

    EXPECT_EQ(Where(platform), glm::dvec3(0.0));
}

// ---------------------------------------------------------------- the components

namespace {

    class GravityComponents : public ::testing::Test {
        protected:
            void SetUp() override
            {
                Core::SetEngine(&context);
                physics.Init(kEarth);
                context.physicsManager = &physics;
                world = std::make_unique<Worlds::World>("gravity", Filesystem::Path());
            }

            void TearDown() override
            {
                actors.clear();
                world.reset();
                physics.Shutdown();
                context.physicsManager = nullptr;
                Core::SetEngine(nullptr);
            }

            std::shared_ptr<Actor> MakeActor(const char* name, glm::vec3 position = glm::vec3(0.0f))
            {
                auto actor = Object::CreateWithContext<Actor>(&context, std::string(name), &context);
                actor->SetWorld(world.get());
                actor->transform->SetPosition(position);
                actors.push_back(actor);
                return actor;
            }

            Shard::Tests::TestEngineContext context;
            PhysicsManager physics;
            std::unique_ptr<Worlds::World> world;
            std::vector<std::shared_ptr<Actor>> actors;
    };
}

TEST_F(GravityComponents, AFieldReadsTheTransformOfItsActor)
{
    auto actor = MakeActor("Planet", glm::vec3(100.0f, 50.0f, -20.0f));
    auto field = actor->AddComponent<RadialGravityField>();
    field->radius = 30.0f;
    field->strength = 12.0f;
    field->falloff = GravityFalloff::Constant;

    const GravityFieldData data = field->BuildData();
    EXPECT_EQ(data.kind, GravityFieldKind::Radial);
    EXPECT_EQ(data.position, glm::dvec3(100.0, 50.0, -20.0));
    EXPECT_FLOAT_EQ(data.radius, 30.0f);

    // And the actor moves, the field follows
    actor->transform->SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    glm::vec3 a;
    float coverage;
    ASSERT_TRUE(field->Sample(glm::dvec3(40.0, 0.0, 0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(-12.0f, 0.0f, 0.0f));
}

TEST_F(GravityComponents, TurningTheActorTurnsAUniformField)
{
    auto actor = MakeActor("Level");
    auto field = actor->AddComponent<UniformGravityField>();
    field->strength = 10.0f;
    actor->transform->SetRotation(glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)));

    glm::vec3 a;
    float coverage;
    ASSERT_TRUE(field->Sample(glm::dvec3(0.0), a, coverage));
    ExpectVec3Near(a, glm::vec3(10.0f, 0.0f, 0.0f), 1e-4f);
}

TEST_F(GravityComponents, AVolumeFollowsTheScaleOfItsActor)
{
    auto actor = MakeActor("Zone");
    auto volume = actor->AddComponent<GravityVolume>();
    volume->halfExtent = glm::vec3(2.0f);

    glm::vec3 a;
    float coverage;
    EXPECT_FALSE(volume->Sample(glm::dvec3(3.0, 0.0, 0.0), a, coverage));
    actor->transform->SetScale(glm::vec3(2.0f));
    EXPECT_TRUE(volume->Sample(glm::dvec3(3.0, 0.0, 0.0), a, coverage));
}

TEST_F(GravityComponents, TheWorldKnowsItsFields)
{
    auto& data = world->Ext<PhysicsWorldData>();
    auto actor = MakeActor("Fields");

    auto uniform = actor->AddComponent<UniformGravityField>();
    auto point = actor->AddComponent<PointGravityField>();
    actor->AddComponent<PhysicsBody>();
    EXPECT_EQ(data.gravityFields.size(), 2u);
    EXPECT_EQ(data.bodies.size(), 1u);

    actor->RemoveComponent(uniform);
    EXPECT_EQ(data.gravityFields.size(), 1u);
    actor->RemoveComponent(point);
    EXPECT_TRUE(data.gravityFields.empty());
    EXPECT_EQ(data.bodies.size(), 1u);
}

TEST_F(GravityComponents, EveryFieldSavesAndLoadsAgain)
{
    auto actor = MakeActor("Saved");
    auto other = MakeActor("Loaded");

    auto uniform = actor->AddComponent<UniformGravityField>();
    uniform->strength = 3.5f;
    uniform->priority = 4;
    uniform->blend = GravityBlend::Additive;
    uniform->direction = glm::vec3(1.0f, 0.0f, 0.0f);
    ordered_json saved = uniform->Serialize();
    EXPECT_EQ(saved["type"], "gravityUniform");
    auto loadedUniform = other->AddComponent<UniformGravityField>();
    loadedUniform->Deserialize(saved);
    EXPECT_FLOAT_EQ(loadedUniform->strength, 3.5f);
    EXPECT_EQ(loadedUniform->priority, 4);
    EXPECT_EQ(loadedUniform->blend, GravityBlend::Additive);
    EXPECT_EQ(loadedUniform->direction, glm::vec3(1.0f, 0.0f, 0.0f));

    auto point = actor->AddComponent<PointGravityField>();
    point->radius = 77.0f;
    point->falloff = GravityFalloff::InverseSquare;
    point->referenceDistance = 3.0f;
    point->minDistance = 1.5f;
    point->fadeDistance = 6.0f;
    saved = point->Serialize();
    EXPECT_EQ(saved["type"], "gravityPoint");
    auto loadedPoint = other->AddComponent<PointGravityField>();
    loadedPoint->Deserialize(saved);
    EXPECT_FLOAT_EQ(loadedPoint->radius, 77.0f);
    EXPECT_EQ(loadedPoint->falloff, GravityFalloff::InverseSquare);
    EXPECT_FLOAT_EQ(loadedPoint->referenceDistance, 3.0f);
    EXPECT_FLOAT_EQ(loadedPoint->minDistance, 1.5f);
    EXPECT_FLOAT_EQ(loadedPoint->fadeDistance, 6.0f);

    auto radial = actor->AddComponent<RadialGravityField>();
    radial->radius = 640.0f;
    radial->reach = 300.0f;
    radial->solidInterior = false;
    saved = radial->Serialize();
    EXPECT_EQ(saved["type"], "gravityRadial");
    auto loadedRadial = other->AddComponent<RadialGravityField>();
    loadedRadial->Deserialize(saved);
    EXPECT_FLOAT_EQ(loadedRadial->radius, 640.0f);
    EXPECT_FLOAT_EQ(loadedRadial->reach, 300.0f);
    EXPECT_FALSE(loadedRadial->solidInterior);

    auto volume = actor->AddComponent<GravityVolume>();
    volume->halfExtent = glm::vec3(1.0f, 2.0f, 3.0f);
    volume->shape = GravityVolumeShape::Cylinder;
    volume->directionMode = GravityVolumeDirection::ToAxis;
    volume->strength = -8.0f;
    saved = volume->Serialize();
    EXPECT_EQ(saved["type"], "gravityVolume");
    auto loadedVolume = other->AddComponent<GravityVolume>();
    loadedVolume->Deserialize(saved);
    EXPECT_EQ(loadedVolume->halfExtent, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(loadedVolume->shape, GravityVolumeShape::Cylinder);
    EXPECT_EQ(loadedVolume->directionMode, GravityVolumeDirection::ToAxis);
    EXPECT_FLOAT_EQ(loadedVolume->strength, -8.0f);
}

TEST_F(GravityComponents, AFieldFromAFileThatDoesNotSayEverythingKeepsItsDefaults)
{
    auto actor = MakeActor("Sparse");
    auto field = actor->AddComponent<PointGravityField>();
    field->Deserialize(json{ { "type", "gravityPoint" }, { "strength", 2.0f } });

    EXPECT_FLOAT_EQ(field->strength, 2.0f);
    EXPECT_FLOAT_EQ(field->radius, 20.0f);
    EXPECT_EQ(field->falloff, GravityFalloff::Linear);
    EXPECT_TRUE(field->Active());
}

TEST_F(GravityComponents, CloningCopiesTheFieldAndNothingElse)
{
    auto actor = MakeActor("Original");
    auto volume = actor->AddComponent<GravityVolume>();
    volume->halfExtent = glm::vec3(9.0f);
    volume->priority = 3;

    auto copy = std::dynamic_pointer_cast<GravityVolume>(volume->Clone());
    ASSERT_TRUE(copy);
    EXPECT_NE(copy.get(), volume.get());
    EXPECT_EQ(copy->halfExtent, glm::vec3(9.0f));
    EXPECT_EQ(copy->priority, 3);
}

TEST_F(GravityComponents, ABodyWithItsOwnGravityFallsWithIt)
{
    auto actor = MakeActor("Ball");
    auto body = actor->AddComponent<PhysicsBody>();
    body->CreateBody(JPH::EMotionType::Dynamic);

    // The default : the world's gravity
    ExpectVec3Near(body->GetGravity(), kEarth);

    body->SetCustomGravity(glm::vec3(0.0f, 0.0f, 3.0f));
    EXPECT_EQ(body->GetGravityMode(), GravityMode::Custom);
    ExpectVec3Near(body->GetGravity(), glm::vec3(0.0f, 0.0f, 3.0f));

    // The gravity factor is part of what the body falls with
    body->SetGravityFactor(2.0f);
    ExpectVec3Near(body->GetGravity(), glm::vec3(0.0f, 0.0f, 6.0f));

    for (int i = 0; i < 60; ++i)
        physics.StepSimulation(kStep, false);
    EXPECT_GT(body->GetPositionD().z, 2.0);
}

TEST_F(GravityComponents, ABodyReportsTheGravityOfTheFieldItIsIn)
{
    auto actor = MakeActor("Ball", glm::vec3(10.0f, 0.0f, 0.0f));
    auto body = actor->AddComponent<PhysicsBody>();
    body->CreateBody(JPH::EMotionType::Dynamic);

    physics.SetGravityFields(Sorted({ Radial(glm::dvec3(0.0), 8.0f, 2.0f, 0.0f, GravityFalloff::Constant) }));
    ExpectVec3Near(body->GetGravity(), glm::vec3(-8.0f, 0.0f, 0.0f));

    body->SetGravityMode(GravityMode::World);
    ExpectVec3Near(body->GetGravity(), kEarth);
}

TEST_F(GravityComponents, TheGravityOfABodyIsSavedAndLoaded)
{
    auto actor = MakeActor("Saved");
    auto body = actor->AddComponent<PhysicsBody>();
    body->CreateBody(JPH::EMotionType::Dynamic);
    body->SetCustomGravity(glm::vec3(1.0f, 2.0f, 3.0f));

    const ordered_json saved = body->Serialize();
    EXPECT_EQ(saved["gravityMode"], "Custom");

    auto other = MakeActor("Loaded");
    auto loaded = other->AddComponent<PhysicsBody>();
    loaded->Deserialize(saved);
    EXPECT_EQ(loaded->GetGravityMode(), GravityMode::Custom);
    EXPECT_EQ(loaded->GetCustomGravity(), glm::vec3(1.0f, 2.0f, 3.0f));
    ExpectVec3Near(loaded->GetGravity(), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_F(GravityComponents, ABodyFromAnOldFileFollowsTheFields)
{
    auto actor = MakeActor("Old");
    auto body = actor->AddComponent<PhysicsBody>();
    body->Deserialize(json::parse(R"({"type":"physics_body","motion_type":"dynamic","gravity_factor":1.0,
        "shapes":[{"shape":"sphere","params":{"radius":0.5}}]})"));

    EXPECT_EQ(body->GetGravityMode(), GravityMode::Fields);
}

TEST_F(GravityComponents, ADestroyedBodyForgetsItsGravity)
{
    auto actor = MakeActor("Gone");
    auto body = actor->AddComponent<PhysicsBody>();
    body->CreateBody(JPH::EMotionType::Dynamic);
    body->SetCustomGravity(glm::vec3(0.0f, 1.0f, 0.0f));
    body->RemoveBody();

    // Stepping with the body gone and a field in the world touches nothing it should not
    physics.SetGravityFields({ Uniform(glm::vec3(1.0f, 0.0f, 0.0f), 5.0f) });
    physics.StepSimulation(kStep, false);
    SUCCEED();
}

TEST_F(GravityComponents, EveryFieldIsFoundAgainByTheTypeItSaves)
{
    // What loading a world, undoing the removal of a component or cloning an actor goes through : the type a component saved is the one
    // the registry builds it from
    RegisterPhysicsModule();
    auto& data = world->Ext<PhysicsWorldData>();      // an extension only hears of the components added after it exists

    auto source = MakeActor("Source");
    auto target = MakeActor("Target");

    std::vector<std::shared_ptr<Objects::Components::Component>> fields = {
        source->AddComponent<UniformGravityField>(), source->AddComponent<PointGravityField>(),
        source->AddComponent<RadialGravityField>(), source->AddComponent<GravityVolume>()
    };
    std::dynamic_pointer_cast<PointGravityField>(fields[1])->radius = 123.0f;

    for (auto& field : fields)
    {
        const ordered_json saved = field->Serialize();
        auto created = target->AddComponentByName(saved["type"].get<std::string>());
        ASSERT_TRUE(created) << saved["type"];
        EXPECT_EQ(typeid(*created), typeid(*field)) << saved["type"];
        created->Deserialize(saved);
        EXPECT_EQ(created->Serialize(), saved) << saved["type"];
    }

    EXPECT_EQ(data.gravityFields.size(), 8u);
}
