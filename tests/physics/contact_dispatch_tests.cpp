#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <vector>

#include "engine/core/jobs/job_system.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_events.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/script.hpp"
#include "support/test_engine_context.hpp"

using namespace Shard::Engine;
using Shard::Engine::Objects::Actor;
using Shard::Engine::Core::Object;
using Shard::Engine::Objects::Components::PhysicsBody;
using Shard::Engine::Objects::Components::Script;
using Shard::Engine::Physics::PhysicsManager;

namespace {

    /// Destroys its actor the first time it touches something. Reads its own body first : with the bodies still locked by Jolt
    /// (the callback running inside the step, on one of its jobs) that read would deadlock or assert.
    class DestroyOnContact : public Script {
        public:
            using Script::Script;

            void OnContactAdded(const Events::ContactAddedEvent& event) override {
                ++calls;
                onCallerThread = std::this_thread::get_id() == caller;
                otherBodyAlive = !event.otherBody.GetBodyID().IsInvalid();
                std::shared_ptr<PhysicsBody> body = parent->GetComponent<PhysicsBody>();
                heightAtContact = body->GetPosition().y;
                parent->Destroy();
            }

            std::thread::id caller;
            int calls = 0;
            bool onCallerThread = false;
            bool otherBodyAlive = false;
            float heightAtContact = -1.0f;
    };

    /// On the ground : must never be told about a body that a script destroyed earlier in the same dispatch
    class ContactWitness : public Script {
        public:
            using Script::Script;

            void OnContactAdded(const Events::ContactAddedEvent& event) override { Check(event.otherBody); }
            void OnContactPersisted(const Events::ContactPersistedEvent& event) override { Check(event.otherBody); }
            void OnContactEnded(const Events::ContactRemovedEvent& event) override { Check(event.otherBody); }

            void Check(const PhysicsBody& other) {
                ++calls;
                if (other.GetBodyID().IsInvalid()) ++deadBodies;
                if (std::this_thread::get_id() != caller) ++offThread;
            }

            std::thread::id caller;
            int calls = 0;
            int deadBodies = 0;
            int offThread = 0;
    };

    // Physics on the engine's job system (several workers, the contact callbacks then come from several threads at once), with
    // actors that carry a PhysicsBody and scripts
    class ContactDispatch : public ::testing::Test {
        protected:
            void SetUp() override {
                Core::JobSystemDesc desc;
                desc.workerCount = 3;
                jobs = std::make_unique<Core::JobSystem>(desc);
                context.jobSystem = jobs.get();
                Core::SetEngine(&context);
                physics.Init(glm::vec3(0.0f, -9.81f, 0.0f));
                context.physicsManager = &physics;
            }

            void TearDown() override {
                actors.clear();
                physics.Shutdown();
                context.physicsManager = nullptr;
                Core::SetEngine(nullptr);
                jobs.reset();
            }

            std::shared_ptr<Actor> MakeBox(const char* name, glm::vec3 position, glm::vec3 scale, JPH::EMotionType motion) {
                auto actor = Object::CreateWithContext<Actor>(&context, std::string(name), &context);
                actor->transform->SetPosition(position);
                actor->transform->SetScale(scale);
                actor->AddComponent<PhysicsBody>()->CreateBody(motion);                // a 1x1x1 box, scaled
                actors.push_back(actor);
                return actor;
            }

            Shard::Tests::TestEngineContext context;
            std::unique_ptr<Core::JobSystem> jobs;
            PhysicsManager physics;
            std::vector<std::shared_ptr<Actor>> actors;
    };
}

TEST_F(ContactDispatch, AScriptCanDestroyItsActorWhenItLands) {
    const std::thread::id caller = std::this_thread::get_id();

    auto ground = MakeBox("Ground", glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f), JPH::EMotionType::Static);
    auto witness = ground->AddComponent<ContactWitness>();
    witness->caller = caller;

    // A grid of boxes landing in the same step : their contacts are found by different jobs
    std::vector<std::shared_ptr<DestroyOnContact>> scripts;
    for (int x = 0; x < 6; ++x) {
        for (int z = 0; z < 6; ++z) {
            auto box = MakeBox("Box", glm::vec3(x * 2.0f - 5.0f, 1.0f, z * 2.0f - 5.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);
            auto script = box->AddComponent<DestroyOnContact>();
            script->caller = caller;
            scripts.push_back(script);
        }
    }
    ASSERT_EQ(physics.GetPhysicsSystem().GetNumBodies(), 37u);

    for (int step = 0; step < 120; ++step)
        physics.StepSimulation(1.0f / 60.0f, false);

    for (const auto& script : scripts) {
        EXPECT_EQ(script->calls, 1);                                   // destroyed by the first contact : no other one followed
        EXPECT_TRUE(script->onCallerThread);
        EXPECT_TRUE(script->otherBodyAlive);
        EXPECT_NEAR(script->heightAtContact, 0.5f, 0.1f);              // resting on the ground's top face
    }
    EXPECT_EQ(physics.GetPhysicsSystem().GetNumBodies(), 1u);          // only the ground is left
    EXPECT_EQ(witness->deadBodies, 0);
    EXPECT_EQ(witness->offThread, 0);
}

TEST_F(ContactDispatch, ContactsReachTheScriptsOnTheSteppingThread) {
    const std::thread::id caller = std::this_thread::get_id();

    auto ground = MakeBox("Ground", glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 1.0f, 40.0f), JPH::EMotionType::Static);
    auto witness = ground->AddComponent<ContactWitness>();
    witness->caller = caller;
    for (int i = 0; i < 16; ++i)
        MakeBox("Box", glm::vec3((i % 4) * 2.0f - 3.0f, 1.0f + (i / 4) * 0.25f, (i / 4) * 2.0f - 3.0f), glm::vec3(1.0f), JPH::EMotionType::Dynamic);

    for (int step = 0; step < 90; ++step)
        physics.StepSimulation(1.0f / 60.0f, false);

    EXPECT_GE(witness->calls, 16);                                     // every box landed (and kept touching)
    EXPECT_EQ(witness->offThread, 0);
    EXPECT_EQ(witness->deadBodies, 0);
}
