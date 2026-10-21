#include <gtest/gtest.h>

#include <cstring>

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

// ---- Named events and their layouts ----

TEST(EventDispatcher, ANamedEventLayoutBelongsToWhoDescribedIt) {
    EventDispatcher dispatcher;
    NamedEventLayout layout;
    layout.size = 8;
    layout.fields = { { "hits", NamedEventLayout::FieldType::I32, 0 }, { "alive", NamedEventLayout::FieldType::Bool, 4 } };
    EXPECT_TRUE(dispatcher.describeNamed("game.Hit", layout, 7));
    EXPECT_TRUE(dispatcher.describeNamed("game.Hit", layout, 7));        // its owner may describe it again
    EXPECT_FALSE(dispatcher.describeNamed("game.Hit", layout, 8));       // not somebody else
    ASSERT_NE(dispatcher.namedLayout("game.Hit"), nullptr);
    EXPECT_EQ(dispatcher.describedNames(), std::vector<std::string>{ "game.Hit" });

    dispatcher.UnsubscribeOwner(7);
    EXPECT_EQ(dispatcher.namedLayout("game.Hit"), nullptr);
}

TEST(EventDispatcher, ANamedEventIsFormattedFromItsLayout) {
    NamedEventLayout layout;
    layout.size = 20;
    layout.fields = { { "hits", NamedEventLayout::FieldType::I32, 0 }, { "where", NamedEventLayout::FieldType::Vec3, 4 }, { "alive", NamedEventLayout::FieldType::Bool, 16 } };
    unsigned char payload[20] = {};
    const int32_t hits = 3;
    const float where[3] = { 1.f, 2.5f, -1.f };
    std::memcpy(payload, &hits, 4);
    std::memcpy(payload + 4, where, 12);
    payload[16] = 1;
    EXPECT_EQ(FormatNamedEvent(layout, payload, sizeof(payload)), "{ hits: 3, where: (1, 2.5, -1), alive: true }");
    EXPECT_EQ(FormatNamedEvent(layout, payload, 6), "(6 bytes)");      // not the size it was described with
}
