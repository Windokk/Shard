#include <gtest/gtest.h>

#include "engine/world/objectID.hpp"

using namespace Shard::Engine::Core;

TEST(ObjectIDManager, GeneratesSequentialIDsStartingAtOne) {
    ObjectIDManager manager;

    ObjectID first = manager.GenerateNewID();
    ObjectID second = manager.GenerateNewID();

    EXPECT_EQ(first.GetAsInt(), 1);
    EXPECT_EQ(second.GetAsInt(), 2);
}

TEST(ObjectIDManager, RecyclesDestroyedIDs) {
    ObjectIDManager manager;

    ObjectID first = manager.GenerateNewID();
    manager.GenerateNewID();
    manager.DestroyID(first);

    ObjectID recycled = manager.GenerateNewID();

    EXPECT_EQ(recycled, first);
}

TEST(ObjectIDManager, AssignAndRetrieveObject) {
    ObjectIDManager manager;
    ObjectID id = manager.GenerateNewID();

    manager.AssignID(id, nullptr);

    EXPECT_EQ(manager.GetObjectFromID(id), nullptr);
}

TEST(ObjectIDManager, GetObjectFromUnknownIDReturnsNull) {
    ObjectIDManager manager;
    ObjectID unknown(42);

    EXPECT_EQ(manager.GetObjectFromID(unknown), nullptr);
}

TEST(ObjectIDManager, DestroyRemovesObjectMapping) {
    ObjectIDManager manager;
    ObjectID id = manager.GenerateNewID();
    manager.AssignID(id, nullptr);

    manager.DestroyID(id);

    EXPECT_EQ(manager.GetObjectFromID(id), nullptr);
}

TEST(ObjectIDManager, ResetClearsAllStateAndRestartsCounter) {
    ObjectIDManager manager;
    manager.GenerateNewID();
    manager.GenerateNewID();

    manager.Reset();
    ObjectID afterReset = manager.GenerateNewID();

    EXPECT_EQ(afterReset.GetAsInt(), 1);
}

TEST(ObjectID, EqualityComparesUnderlyingValue) {
    ObjectID a(5);
    ObjectID b(5);
    ObjectID c(6);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(ObjectID, OrderingIsByUnderlyingValue) {
    ObjectID a(1);
    ObjectID b(2);

    EXPECT_LT(a, b);
    EXPECT_FALSE(b < a);
}
