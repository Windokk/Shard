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

    dispatcher.emitGlobal(LevelStructureChangedEvent(1, LevelChangeType::CREATED, "Actor", ObjectID(1)));

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

TEST(EventDispatcher, LevelScopedEventOnlyReachesMatchingLevel) {
    EventDispatcher dispatcher;
    bool level1Called = false;
    bool level2Called = false;

    dispatcher.subscribeToLevel<KeyPressedEvent>(1, [&](const KeyPressedEvent&) { level1Called = true; });
    dispatcher.subscribeToLevel<KeyPressedEvent>(2, [&](const KeyPressedEvent&) { level2Called = true; });

    dispatcher.emitToLevel(1, KeyPressedEvent(1, false, ObjectID(1)));

    EXPECT_TRUE(level1Called);
    EXPECT_FALSE(level2Called);
}

TEST(EventDispatcher, EmitToLevelWithNoSubscribersDoesNotThrow) {
    EventDispatcher dispatcher;

    EXPECT_NO_THROW(dispatcher.emitToLevel(99, KeyPressedEvent(1, false, ObjectID(1))));
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
