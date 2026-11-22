#include <Jolt/Jolt.h>

#include <gtest/gtest.h>

#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "engine/physics/mesh/mesh_collider.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
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

    void AddQuad(TriangleSoup& soup, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, uint32_t submesh)
    {
        const uint32_t base = uint32_t(soup.positions.size());
        soup.positions.insert(soup.positions.end(), { a, b, c, d });
        for (uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u })
            soup.indices.push_back(base + i);
        soup.triangleSubmesh.push_back(submesh);
        soup.triangleSubmesh.push_back(submesh);
        soup.submeshCount = std::max(soup.submeshCount, submesh + 1);
    }

    void AddBox(TriangleSoup& soup, glm::vec3 lo, glm::vec3 hi, uint32_t submesh)
    {
        const glm::vec3 p[8] = {
            { lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, hi.y, lo.z }, { lo.x, hi.y, lo.z },
            { lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z } };
        AddQuad(soup, p[0], p[3], p[2], p[1], submesh);
        AddQuad(soup, p[4], p[5], p[6], p[7], submesh);
        AddQuad(soup, p[0], p[1], p[5], p[4], submesh);
        AddQuad(soup, p[2], p[3], p[7], p[6], submesh);
        AddQuad(soup, p[1], p[2], p[6], p[5], submesh);
        AddQuad(soup, p[3], p[0], p[4], p[7], submesh);
    }

    TriangleSoup Floor()
    {
        TriangleSoup soup;
        AddQuad(soup, { -10, 0, -10 }, { -10, 0, 10 }, { 0, 0, 10 }, { 0, 0, -10 }, 0);
        AddQuad(soup, { 0, 0, -10 }, { 0, 0, 10 }, { 10, 0, 10 }, { 10, 0, -10 }, 1);
        return soup;
    }

    TriangleSoup LShape()
    {
        TriangleSoup soup;
        const float poly[6][2] = { { 0, 0 }, { 3, 0 }, { 3, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
        for (int z = 0; z < 2; ++z)
            for (const auto& point : poly)
                soup.positions.emplace_back(point[0], point[1], float(z));
        auto add = [&soup](uint32_t a, uint32_t b, uint32_t c) {
            soup.indices.insert(soup.indices.end(), { a, b, c });
            soup.triangleSubmesh.push_back(0);
        };
        const uint32_t caps[4][3] = { { 0, 1, 2 }, { 0, 2, 3 }, { 0, 3, 5 }, { 3, 4, 5 } };
        for (const auto& t : caps)
        {
            add(t[0], t[2], t[1]);
            add(6 + t[0], 6 + t[1], 6 + t[2]);
        }
        for (uint32_t i = 0; i < 6; ++i)
        {
            const uint32_t j = (i + 1) % 6;
            add(i, j, 6 + j);
            add(i, 6 + j, 6 + i);
        }
        soup.submeshCount = 1;
        return soup;
    }

    class MeshColliders : public ::testing::Test {
        protected:
            void SetUp() override
            {
                Core::SetEngine(&context);
                physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
                context.physicsManager = &physics;
                world = std::make_unique<Worlds::World>("meshes", Filesystem::Path());

                MeshSource source;
                source.load = [this](Filesystem::AssetID id, TriangleSoup& soup) {
                    ++loads;
                    const auto it = soups.find(id.GetAsInt());
                    if (it == soups.end()) return false;
                    soup = it->second;
                    return true;
                };
                source.stamp = [this](Filesystem::AssetID) { return stamp; };
                source.path = [this](Filesystem::AssetID id) { return id.GetAsInt() == 1 ? cooked : std::string(); };
                source.modelMesh = [](Actor&) { return Filesystem::AssetID(1); };
                physics.MeshColliders().SetSource(std::move(source));

                soups[1] = Floor();
                soups[2] = LShape();
                cooked = (std::filesystem::temp_directory_path() / "shard_mesh_collider_test.fbx").string();
                std::filesystem::remove(CookedCollision::PathFor(cooked));
            }

            void TearDown() override
            {
                std::filesystem::remove(CookedCollision::PathFor(cooked));
                actors.clear();
                world.reset();
                physics.Shutdown();
                context.physicsManager = nullptr;
                Core::SetEngine(nullptr);
            }

            std::shared_ptr<Actor> MakeActor(const char* name, glm::vec3 position)
            {
                auto actor = Object::CreateWithContext<Actor>(&context, std::string(name), &context);
                actor->SetWorld(world.get());
                actor->transform->SetPosition(position);
                actors.push_back(actor);
                return actor;
            }

            std::shared_ptr<PhysicsBody> MeshBody(const std::shared_ptr<Actor>& actor, MeshApproximation approximation, JPH::EMotionType motion)
            {
                auto body = actor->AddComponent<PhysicsBody>();
                body->SetShapeType(0, PhysicsShape::MESH);
                body->GetShapeParams<MeshParams>(0).approximation = approximation;
                body->CreateBody(motion);
                return body;
            }

            void Run(int steps)
            {
                for (int i = 0; i < steps; ++i)
                {
                    physics.MeshColliders().Poll();
                    physics.StepSimulation(kStep, false);
                }
            }

            Shard::Tests::TestEngineContext context;
            PhysicsManager physics;
            std::unique_ptr<Worlds::World> world;
            std::vector<std::shared_ptr<Actor>> actors;
            std::unordered_map<int, TriangleSoup> soups;
            std::string cooked;
            uint64_t stamp = 100;
            int loads = 0;
    };

    MeshColliderSettings SettingsFor(int asset, MeshApproximation approximation)
    {
        MeshColliderSettings settings;
        settings.mesh = Filesystem::AssetID(asset);
        settings.approximation = approximation;
        return settings;
    }
}

TEST(MeshColliderPure, AutoPicksByMotion)
{
    EXPECT_EQ(ResolveApproximation(MeshApproximation::Auto, false), MeshApproximation::Exact);
    EXPECT_EQ(ResolveApproximation(MeshApproximation::Auto, true), MeshApproximation::Hulls);
    EXPECT_EQ(ResolveApproximation(MeshApproximation::Exact, true), MeshApproximation::Hull);
    EXPECT_EQ(ResolveApproximation(MeshApproximation::Exact, false), MeshApproximation::Exact);
    EXPECT_EQ(ResolveApproximation(MeshApproximation::Bounds, true), MeshApproximation::Bounds);
}

TEST(MeshColliderPure, SubmeshList)
{
    EXPECT_EQ(ParseSubmeshMask(""), ~0ull);
    EXPECT_EQ(ParseSubmeshMask("0"), 1ull);
    EXPECT_EQ(ParseSubmeshMask("0,2"), 5ull);
    EXPECT_EQ(ParseSubmeshMask("1-3"), 14ull);
    EXPECT_EQ(ParseSubmeshMask(" 1 , 4 "), 18ull);
    EXPECT_EQ(ParseSubmeshMask("nothing"), ~0ull);
}

TEST(MeshColliderPure, KeyIgnoresWhatTheApproximationDoesNotUse)
{
    MeshColliderSettings a, b;
    b.concavity = 0.5f;
    b.maxHulls = 3;
    EXPECT_EQ(MeshColliderKeyHash(a, MeshApproximation::Exact), MeshColliderKeyHash(b, MeshApproximation::Exact));
    EXPECT_EQ(MeshColliderKeyHash(a, MeshApproximation::Hull), MeshColliderKeyHash(b, MeshApproximation::Hull));
    EXPECT_NE(MeshColliderKeyHash(a, MeshApproximation::Hulls), MeshColliderKeyHash(b, MeshApproximation::Hulls));
    EXPECT_NE(MeshColliderKeyHash(a, MeshApproximation::Exact), MeshColliderKeyHash(a, MeshApproximation::Hull));
}

TEST_F(MeshColliders, ExactBuildsATriangleMesh)
{
    std::string error;
    JPH::ShapeRefC shape = BuildMeshShape(soups[1], SettingsFor(1, MeshApproximation::Exact), MeshApproximation::Exact, &error);
    ASSERT_TRUE(shape) << error;
    EXPECT_EQ(shape->GetSubType(), JPH::EShapeSubType::Mesh);
    EXPECT_EQ(static_cast<const JPH::MeshShape*>(shape.GetPtr())->GetStats().mNumTriangles, 4u);
}

TEST_F(MeshColliders, SubmeshMaskKeepsOnlyThoseTriangles)
{
    MeshColliderSettings settings = SettingsFor(1, MeshApproximation::Exact);
    settings.submeshMask = ParseSubmeshMask("1");
    JPH::ShapeRefC shape = BuildMeshShape(soups[1], settings, MeshApproximation::Exact, nullptr);
    ASSERT_TRUE(shape);
    EXPECT_EQ(static_cast<const JPH::MeshShape*>(shape.GetPtr())->GetStats().mNumTriangles, 2u);
    EXPECT_GT(shape->GetLocalBounds().GetCenter().GetX(), 4.0f);

    settings.submeshMask = ParseSubmeshMask("7");
    std::string error;
    EXPECT_FALSE(BuildMeshShape(soups[1], settings, MeshApproximation::Exact, &error));
    EXPECT_FALSE(error.empty());
}

TEST_F(MeshColliders, HullAndBoundsBuild)
{
    JPH::ShapeRefC hull = BuildMeshShape(soups[2], SettingsFor(2, MeshApproximation::Hull), MeshApproximation::Hull, nullptr);
    ASSERT_TRUE(hull);
    EXPECT_EQ(hull->GetSubType(), JPH::EShapeSubType::ConvexHull);

    JPH::ShapeRefC bounds = BuildMeshShape(soups[2], SettingsFor(2, MeshApproximation::Bounds), MeshApproximation::Bounds, nullptr);
    ASSERT_TRUE(bounds);
    const JPH::AABox box = bounds->GetLocalBounds();
    EXPECT_NEAR(box.GetExtent().GetX(), 1.5f, 1e-3f);
    EXPECT_NEAR(bounds->GetCenterOfMass().GetX(), 1.5f, 1e-3f);
    EXPECT_NEAR(box.GetExtent().GetY(), 1.5f, 1e-3f);
}

TEST_F(MeshColliders, HullKeepsItsVertexBudget)
{
    TriangleSoup sphere;
    const int rings = 40, segments = 60;
    for (int r = 0; r <= rings; ++r)
        for (int s = 0; s < segments; ++s)
        {
            const float v = 3.14159265f * float(r) / float(rings), u = 6.2831853f * float(s) / float(segments);
            sphere.positions.emplace_back(std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u));
        }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s)
        {
            const uint32_t a = uint32_t(r * segments + s), b = uint32_t(r * segments + (s + 1) % segments);
            const uint32_t c = a + segments, d = b + segments;
            for (uint32_t i : { a, c, b, b, c, d }) sphere.indices.push_back(i);
            sphere.triangleSubmesh.push_back(0);
            sphere.triangleSubmesh.push_back(0);
        }
    sphere.submeshCount = 1;
    MeshColliderSettings settings;
    settings.maxHullVertices = 48;
    JPH::ShapeRefC hull = BuildMeshShape(sphere, settings, MeshApproximation::Hull, nullptr);
    ASSERT_TRUE(hull);
    EXPECT_LE(static_cast<const JPH::ConvexHullShape*>(hull.GetPtr())->GetNumPoints(), 48u);
    EXPECT_GT(hull->GetLocalBounds().GetExtent().GetX(), 0.85f);
}

TEST_F(MeshColliders, DecompositionBuildsAConcaveBody)
{
    MeshColliderSettings settings;
    settings.concavity = 0.05f;
    settings.maxHulls = 4;
    std::string error;
    JPH::ShapeRefC shape = BuildMeshShape(soups[2], settings, MeshApproximation::Hulls, &error);
    ASSERT_TRUE(shape) << error;
    const JPH::AABox box = shape->GetLocalBounds();
    EXPECT_NEAR(box.GetExtent().GetX(), 1.5f, 0.1f);
    EXPECT_NEAR(box.GetExtent().GetY(), 1.5f, 0.1f);
    ASSERT_EQ(shape->GetSubType(), JPH::EShapeSubType::StaticCompound);
    EXPECT_GE(static_cast<const JPH::CompoundShape*>(shape.GetPtr())->GetNumSubShapes(), 2u);
}

TEST_F(MeshColliders, TrianglesComeBackInTheSpaceOfTheMesh)
{
    for (MeshApproximation approximation : { MeshApproximation::Exact, MeshApproximation::Hull, MeshApproximation::Bounds, MeshApproximation::Hulls })
    {
        JPH::ShapeRefC shape = BuildMeshShape(soups[2], SettingsFor(2, approximation), approximation, nullptr);
        ASSERT_TRUE(shape);
        std::vector<glm::vec3> vertices;
        ASSERT_TRUE(ExtractShapeTriangles(*shape, vertices, 100000));
        ASSERT_FALSE(vertices.empty()) << ApproximationName(approximation);
        EXPECT_EQ(vertices.size() % 3, 0u);
        glm::vec3 low(1e9f), high(-1e9f);
        for (const glm::vec3& v : vertices) { low = glm::min(low, v); high = glm::max(high, v); }
        EXPECT_NEAR(low.x, 0.0f, 0.02f) << ApproximationName(approximation);
        EXPECT_NEAR(high.x, 3.0f, 0.02f) << ApproximationName(approximation);
        EXPECT_NEAR(high.y, 3.0f, 0.02f) << ApproximationName(approximation);
        EXPECT_NEAR(low.z, 0.0f, 0.02f) << ApproximationName(approximation);
    }
    JPH::ShapeRefC shape = BuildMeshShape(soups[2], SettingsFor(2, MeshApproximation::Exact), MeshApproximation::Exact, nullptr);
    std::vector<glm::vec3> few;
    EXPECT_FALSE(ExtractShapeTriangles(*shape, few, 3));
}

TEST_F(MeshColliders, ShapesSurviveSerialization)
{
    for (MeshApproximation approximation : { MeshApproximation::Exact, MeshApproximation::Hull, MeshApproximation::Bounds })
    {
        JPH::ShapeRefC shape = BuildMeshShape(soups[1], SettingsFor(1, approximation), approximation, nullptr);
        ASSERT_TRUE(shape);
        const std::vector<uint8_t> bytes = SerializeMeshShape(*shape);
        ASSERT_FALSE(bytes.empty());
        JPH::ShapeRefC restored = DeserializeMeshShape(bytes.data(), bytes.size());
        ASSERT_TRUE(restored) << ApproximationName(approximation);
        EXPECT_EQ(restored->GetSubType(), shape->GetSubType());
        EXPECT_NEAR(restored->GetLocalBounds().GetExtent().GetX(), shape->GetLocalBounds().GetExtent().GetX(), 1e-4f);
    }
    EXPECT_FALSE(DeserializeMeshShape(nullptr, 0));
    const uint8_t junk[5] = { 1, 2, 3, 4, 5 };
    EXPECT_FALSE(DeserializeMeshShape(junk, 5));
}

TEST_F(MeshColliders, TheCacheSharesOneShapePerMesh)
{
    MeshColliderCache& cache = physics.MeshColliders();
    MeshColliderResult first = cache.Acquire(SettingsFor(1, MeshApproximation::Auto), false);
    MeshColliderResult second = cache.Acquire(SettingsFor(1, MeshApproximation::Auto), false);
    ASSERT_TRUE(first.shape);
    EXPECT_EQ(first.shape.GetPtr(), second.shape.GetPtr());
    EXPECT_EQ(loads, 1);
    EXPECT_FALSE(first.refining);

    MeshColliderSettings other = SettingsFor(1, MeshApproximation::Auto);
    other.edgeAngle = 20.0f;
    EXPECT_NE(cache.Acquire(other, false).shape.GetPtr(), first.shape.GetPtr());
    EXPECT_EQ(loads, 1);
}

TEST_F(MeshColliders, AMissingMeshIsReportedAndNotRetriedBlindly)
{
    MeshColliderCache& cache = physics.MeshColliders();
    EXPECT_FALSE(cache.Acquire(SettingsFor(0, MeshApproximation::Exact), false).error.empty());
    MeshColliderResult result = cache.Acquire(SettingsFor(99, MeshApproximation::Exact), false);
    EXPECT_FALSE(result.shape);
    EXPECT_FALSE(result.error.empty());
    const int before = loads;
    cache.Acquire(SettingsFor(99, MeshApproximation::Exact), false);
    EXPECT_EQ(loads, before);
}

TEST_F(MeshColliders, DynamicBodiesGetAHullThenTheDecomposition)
{
    MeshColliderCache& cache = physics.MeshColliders();
    MeshColliderResult first = cache.Acquire(SettingsFor(2, MeshApproximation::Auto), true);
    ASSERT_TRUE(first.shape);
    EXPECT_TRUE(first.refining);
    EXPECT_EQ(first.shape->GetSubType(), JPH::EShapeSubType::ConvexHull);
    EXPECT_EQ(cache.PendingJobs(), 1u);

    const uint64_t generation = cache.Generation();
    cache.WaitIdle();
    EXPECT_GT(cache.Generation(), generation);
    EXPECT_EQ(cache.PendingJobs(), 0u);

    MeshColliderResult second = cache.Acquire(SettingsFor(2, MeshApproximation::Auto), true);
    ASSERT_TRUE(second.shape);
    EXPECT_FALSE(second.refining);
    EXPECT_EQ(second.used, MeshApproximation::Hulls);
}

TEST_F(MeshColliders, CookedFilesAreWrittenAndReused)
{
    physics.MeshColliders().SetWriteCooked(true);
    ASSERT_TRUE(physics.MeshColliders().Acquire(SettingsFor(1, MeshApproximation::Exact), false).shape);
    ASSERT_TRUE(physics.MeshColliders().Acquire(SettingsFor(1, MeshApproximation::Hull), false).shape);
    ASSERT_TRUE(std::filesystem::exists(CookedCollision::PathFor(cooked)));

    CookedCollision::File file;
    ASSERT_TRUE(CookedCollision::Read(CookedCollision::PathFor(cooked), file));
    EXPECT_EQ(file.entries.size(), 2u);
    EXPECT_EQ(file.stamp, stamp);

    physics.MeshColliders().Clear();
    loads = 0;
    MeshColliderResult again = physics.MeshColliders().Acquire(SettingsFor(1, MeshApproximation::Exact), false);
    ASSERT_TRUE(again.shape);
    EXPECT_EQ(again.shape->GetSubType(), JPH::EShapeSubType::Mesh);
    EXPECT_EQ(loads, 0);
}

TEST_F(MeshColliders, ACookedFileOfAnOlderSourceIsIgnored)
{
    physics.MeshColliders().SetWriteCooked(true);
    ASSERT_TRUE(physics.MeshColliders().Acquire(SettingsFor(1, MeshApproximation::Exact), false).shape);
    physics.MeshColliders().Clear();
    stamp = 200;
    loads = 0;
    ASSERT_TRUE(physics.MeshColliders().Acquire(SettingsFor(1, MeshApproximation::Exact), false).shape);
    EXPECT_EQ(loads, 1);

    CookedCollision::File file;
    ASSERT_TRUE(CookedCollision::Read(CookedCollision::PathFor(cooked), file));
    EXPECT_EQ(file.stamp, 200u);
    EXPECT_EQ(file.entries.size(), 1u);
}

TEST_F(MeshColliders, ACorruptCookedFileFallsBackToTheMesh)
{
    {
        std::ofstream out(CookedCollision::PathFor(cooked), std::ios::binary);
        out << "this is not a cooked collision file";
    }
    loads = 0;
    ASSERT_TRUE(physics.MeshColliders().Acquire(SettingsFor(1, MeshApproximation::Exact), false).shape);
    EXPECT_EQ(loads, 1);
}

TEST_F(MeshColliders, InvalidateRebuildsFromTheMesh)
{
    MeshColliderCache& cache = physics.MeshColliders();
    JPH::ShapeRefC before = cache.Acquire(SettingsFor(1, MeshApproximation::Exact), false).shape;
    ASSERT_TRUE(before);
    cache.Invalidate(Filesystem::AssetID(1));
    JPH::ShapeRefC after = cache.Acquire(SettingsFor(1, MeshApproximation::Exact), false).shape;
    ASSERT_TRUE(after);
    EXPECT_NE(before.GetPtr(), after.GetPtr());
    EXPECT_EQ(loads, 2);
}

TEST_F(MeshColliders, AStaticFloorHoldsADynamicBall)
{
    auto floor = MakeActor("Floor", glm::vec3(0.0f));
    auto floorBody = MeshBody(floor, MeshApproximation::Auto, JPH::EMotionType::Static);
    ASSERT_FALSE(floorBody->GetBodyID().IsInvalid());

    auto ball = MakeActor("Ball", glm::vec3(1.0f, 3.0f, 1.0f));
    auto ballBody = ball->AddComponent<PhysicsBody>();
    ballBody->SetShapeType(0, PhysicsShape::SPHERE);
    ballBody->CreateBody(JPH::EMotionType::Dynamic);

    Run(240);
    const glm::dvec3 at = ballBody->GetPositionD();
    EXPECT_NEAR(at.y, 0.5, 0.08);
    EXPECT_GT(at.y, 0.3);
}

TEST_F(MeshColliders, ADynamicMeshBodyRestsOnAFloorAndKeepsItsPieces)
{
    auto floor = MakeActor("Floor", glm::vec3(0.0f));
    ASSERT_FALSE(MeshBody(floor, MeshApproximation::Exact, JPH::EMotionType::Static)->GetBodyID().IsInvalid());

    auto chair = MakeActor("Chair", glm::vec3(2.0f, 2.0f, 0.0f));
    soups[1] = LShape();
    physics.MeshColliders().Clear();
    auto chairBody = MeshBody(chair, MeshApproximation::Auto, JPH::EMotionType::Dynamic);
    ASSERT_FALSE(chairBody->GetBodyID().IsInvalid());

    Run(30);
    physics.MeshColliders().WaitIdle();
    Run(300);
    EXPECT_GE(chairBody->GetPositionD().y, -0.1);
    EXPECT_LT(chairBody->GetPositionD().y, 2.0);
}

TEST_F(MeshColliders, ABodyBuiltOnAHullUpgradesWhenTheDecompositionIsReady)
{
    soups[1] = LShape();
    auto piece = MakeActor("Piece", glm::vec3(0.0f, 5.0f, 0.0f));
    auto body = MeshBody(piece, MeshApproximation::Auto, JPH::EMotionType::Dynamic);
    ASSERT_FALSE(body->GetBodyID().IsInvalid());
    const JPH::Shape* first = nullptr;
    {
        JPH::BodyLockRead lock(physics.GetPhysicsSystem().GetBodyLockInterface(), body->GetBodyID());
        ASSERT_TRUE(lock.Succeeded());
        first = lock.GetBody().GetShape();
        EXPECT_EQ(first->GetSubType(), JPH::EShapeSubType::ConvexHull);
    }
    physics.MeshColliders().WaitIdle();
    body->Tick(kStep);
    ASSERT_FALSE(body->GetBodyID().IsInvalid());
    JPH::BodyLockRead lock(physics.GetPhysicsSystem().GetBodyLockInterface(), body->GetBodyID());
    ASSERT_TRUE(lock.Succeeded());
    EXPECT_NE(lock.GetBody().GetShape(), first);
}

TEST_F(MeshColliders, TheActorScaleScalesTheMesh)
{
    auto floor = MakeActor("Floor", glm::vec3(0.0f));
    floor->transform->SetScale(glm::vec3(2.0f));
    auto body = MeshBody(floor, MeshApproximation::Exact, JPH::EMotionType::Static);
    ASSERT_FALSE(body->GetBodyID().IsInvalid());
    JPH::BodyLockRead lock(physics.GetPhysicsSystem().GetBodyLockInterface(), body->GetBodyID());
    ASSERT_TRUE(lock.Succeeded());
    EXPECT_EQ(lock.GetBody().GetShape()->GetSubType(), JPH::EShapeSubType::Scaled);
    EXPECT_NEAR(lock.GetBody().GetShape()->GetLocalBounds().GetExtent().GetX(), 20.0f, 1e-3f);
}

TEST_F(MeshColliders, ARayReportsTheSubmeshItHit)
{
    auto floor = MakeActor("Floor", glm::vec3(0.0f));
    ASSERT_FALSE(MeshBody(floor, MeshApproximation::Exact, JPH::EMotionType::Static)->GetBodyID().IsInvalid());
    physics.StepSimulation(kStep, false);

    for (const auto& [x, submesh] : { std::pair<float, uint32_t>{ -4.0f, 0u }, std::pair<float, uint32_t>{ 4.0f, 1u } })
    {
        RaycastRequest request;
        request.origin = glm::vec3(x, 5.0f, 1.0f);
        request.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        request.maxDistance = 20.0f;
        const RaycastResult hit = physics.RayCast(request);
        ASSERT_TRUE(hit.hit);
        EXPECT_TRUE(hit.onMesh);
        EXPECT_EQ(hit.submesh, submesh);
        EXPECT_NEAR(hit.hitDistance, 5.0f, 1e-3f);
    }
}

TEST_F(MeshColliders, AMeshShapeWithoutAModelWaitsForOne)
{
    MeshSource source = physics.MeshColliders().Source();
    int mesh = 0;
    source.modelMesh = [&mesh](Actor&) { return Filesystem::AssetID(mesh); };
    physics.MeshColliders().SetSource(std::move(source));

    auto floor = MakeActor("Floor", glm::vec3(0.0f));
    auto body = MeshBody(floor, MeshApproximation::Exact, JPH::EMotionType::Static);
    EXPECT_TRUE(body->GetBodyID().IsInvalid());

    mesh = 1;
    body->Tick(kStep);
    EXPECT_FALSE(body->GetBodyID().IsInvalid());
}

TEST_F(MeshColliders, MeshParametersAreSavedAndRestored)
{
    Filesystem::AssetIDManager assets;
    context.assetIDManager = &assets;
    auto actor = MakeActor("Rock", glm::vec3(0.0f));
    auto body = actor->AddComponent<PhysicsBody>();
    body->SetShapeType(0, PhysicsShape::MESH);
    MeshParams& params = body->GetShapeParams<MeshParams>(0);
    params.approximation = MeshApproximation::Hulls;
    params.submeshes = "0,2";
    params.concavity = 0.2f;
    params.maxHulls = 7;
    params.maxHullVertices = 40;
    params.convexRadius = 0.05f;
    params.edgeAngle = 12.0f;

    const auto saved = body->Serialize();
    ASSERT_EQ(saved["shapes"].size(), 1u);
    EXPECT_EQ(saved["shapes"][0]["shape"], "mesh");

    auto copy = actor->AddComponent<PhysicsBody>();
    copy->Deserialize(saved);
    ASSERT_EQ(copy->GetShapeCount(), 1u);
    ASSERT_EQ(copy->GetShapeType(0), PhysicsShape::MESH);
    const MeshParams& restored = copy->GetShapeParams<MeshParams>(0);
    EXPECT_EQ(restored.approximation, MeshApproximation::Hulls);
    EXPECT_EQ(restored.submeshes, "0,2");
    EXPECT_FLOAT_EQ(restored.concavity, 0.2f);
    EXPECT_EQ(restored.maxHulls, 7);
    EXPECT_EQ(restored.maxHullVertices, 40);
    EXPECT_FLOAT_EQ(restored.convexRadius, 0.05f);
    EXPECT_FLOAT_EQ(restored.edgeAngle, 12.0f);
    context.assetIDManager = nullptr;
}
