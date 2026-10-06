#include <gtest/gtest.h>

#include "engine/world/event_system.hpp"

using namespace Shard::Engine::Events;
using Shard::Engine::Core::ObjectID;

TEST(EventDispatcher, GlobalSubscriberReceivesEmittedEvent) {
    EventDispatcher dispatcher;
    int receivedKey = -1;

    dispatcher.subscribeGlobal<KeyPressedEvent>([&](const KeyPressedEvent& e) {
        receivedKey = e.keyCode;
    });

    dispatcher.emitGlobal(KeyPressedEvent(42, false, ObjectID(1)));

    EXPECT_EQ(receivedKey, 42);
}

TEST(EventDispatcher, GlobalSubscriberIgnoresUnrelatedEventType) {
    EventDispatcher dispatcher;
    bool called = false;

    dispatcher.subscribeGlobal<KeyPressedEvent>([&](const KeyPressedEvent&) {
        called = true;
    });

    dispatcher.emitGlobal(WorldStructureChangedEvent(1, WorldChangeType::CREATED, "Actor", ObjectID(1)));

    EXPECT_FALSE(called);
}

TEST(EventDispatcher, MultipleGlobalSubscribersAllReceiveEvent) {
    EventDispatcher dispatcher;
    int callCount = 0;

    dispatcher.subscribeGlobal<KeyPressedEvent>([&](const KeyPressedEvent&) { callCount++; });
    dispatcher.subscribeGlobal<KeyPressedEvent>([&](const KeyPressedEvent&) { callCount++; });

    dispatcher.emitGlobal(KeyPressedEvent(1, false, ObjectID(1)));

    EXPECT_EQ(callCount, 2);
}

TEST(EventDispatcher, WorldScopedEventOnlyReachesMatchingWorld) {
    EventDispatcher dispatcher;
    bool world1Called = false;
    bool world2Called = false;

    dispatcher.subscribeToWorld<KeyPressedEvent>(1, [&](const KeyPressedEvent&) { world1Called = true; });
    dispatcher.subscribeToWorld<KeyPressedEvent>(2, [&](const KeyPressedEvent&) { world2Called = true; });

    dispatcher.emitToWorld(1, KeyPressedEvent(1, false, ObjectID(1)));

    EXPECT_TRUE(world1Called);
    EXPECT_FALSE(world2Called);
}

TEST(EventDispatcher, EmitToWorldWithNoSubscribersDoesNotThrow) {
    EventDispatcher dispatcher;

    EXPECT_NO_THROW(dispatcher.emitToWorld(99, KeyPressedEvent(1, false, ObjectID(1))));
}

TEST(EventDispatcher, ActorScopedEventOnlyReachesMatchingActor) {
    EventDispatcher dispatcher;
    ObjectID actorA(1);
    ObjectID actorB(2);
    bool aCalled = false;
    bool bCalled = false;

    dispatcher.subscribeToActor<KeyPressedEvent>(actorA, [&](const KeyPressedEvent&) { aCalled = true; });
    dispatcher.subscribeToActor<KeyPressedEvent>(actorB, [&](const KeyPressedEvent&) { bCalled = true; });

    dispatcher.emitToActor(actorA, KeyPressedEvent(1, false, actorA));

    EXPECT_TRUE(aCalled);
    EXPECT_FALSE(bCalled);
}

TEST(EventDispatcher, ComponentScopedEventOnlyReachesMatchingComponent) {
    EventDispatcher dispatcher;
    bool called = false;

    dispatcher.subscribeToComponent<KeyPressedEvent>(7, [&](const KeyPressedEvent&) { called = true; });

    dispatcher.emitToComponent(8, KeyPressedEvent(1, false, ObjectID(1)));
    EXPECT_FALSE(called);

    dispatcher.emitToComponent(7, KeyPressedEvent(1, false, ObjectID(1)));
    EXPECT_TRUE(called);
}
