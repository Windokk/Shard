#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/containers/result.hpp"
#include "engine/core/containers/slot_map.hpp"
#include "engine/core/containers/span.hpp"
#include "engine/core/containers/string_id.hpp"

using namespace Shard::Engine::Core;

namespace {
    struct Widget {
        explicit Widget(int v, int* destroyed = nullptr) : value(v), destroyed(destroyed) {}
        ~Widget() { if (destroyed) ++*destroyed; }
        int value;
        int* destroyed;
    };
}

TEST(SlotMap, InsertGetRemove) {
    SlotMap<std::string> map;
    auto a = map.Insert("alpha");
    auto b = map.Insert("beta");
    EXPECT_EQ(map.Size(), 2u);
    EXPECT_EQ(*map.Get(a), "alpha");
    EXPECT_EQ(*map.Get(b), "beta");
    EXPECT_TRUE(map.Remove(a));
    EXPECT_EQ(map.Get(a), nullptr);
    EXPECT_FALSE(map.Remove(a));                     // already removed
    EXPECT_EQ(map.Size(), 1u);
}

TEST(SlotMap, StaleHandleIsDetectedAfterSlotReuse) {
    SlotMap<int> map;
    auto first = map.Insert(1);
    map.Remove(first);
    auto second = map.Insert(2);                     // reuses the slot
    EXPECT_EQ(first.index, second.index);
    EXPECT_NE(first.generation, second.generation);
    EXPECT_EQ(map.Get(first), nullptr);              // the old handle does not see the new element
    EXPECT_EQ(*map.Get(second), 2);
}

TEST(SlotMap, DefaultAndOutOfRangeHandlesAreInvalid) {
    SlotMap<int> map;
    Handle<int> none;
    EXPECT_FALSE(none.IsValid());
    EXPECT_EQ(map.Get(none), nullptr);
    EXPECT_FALSE(map.Remove(none));
    map.Insert(5);
    EXPECT_EQ(map.Get(Handle<int>{99, 1}), nullptr);
}

TEST(SlotMap, ElementsNeverMove) {
    SlotMap<int> map;
    auto first = map.Insert(123);
    int* address = map.Get(first);
    for (int i = 0; i < 5000; ++i) map.Insert(i);     // many chunks later
    EXPECT_EQ(map.Get(first), address);
    EXPECT_EQ(*address, 123);
}

TEST(SlotMap, RunsDestructors) {
    int destroyed = 0;
    {
        SlotMap<Widget> map;
        auto a = map.Emplace(1, &destroyed);
        map.Emplace(2, &destroyed);
        map.Remove(a);
        EXPECT_EQ(destroyed, 1);
    }
    EXPECT_EQ(destroyed, 2);                          // the survivor goes with the map
}

TEST(SlotMap, IterationVisitsLiveElementsOnly) {
    SlotMap<int> map;
    std::vector<Handle<int>> handles;
    for (int i = 0; i < 10; ++i) handles.push_back(map.Insert(i));
    map.Remove(handles[3]);
    map.Remove(handles[7]);

    std::set<int> seen;
    for (auto [handle, value] : map) {
        EXPECT_EQ(*map.Get(handle), value);
        seen.insert(value);
    }
    EXPECT_EQ(seen, (std::set<int>{0, 1, 2, 4, 5, 6, 8, 9}));

    int sum = 0;
    map.ForEach([&](Handle<int>, int& v) { sum += v; });
    EXPECT_EQ(sum, 0 + 1 + 2 + 4 + 5 + 6 + 8 + 9);
}

TEST(SlotMap, ClearInvalidatesEveryHandle) {
    SlotMap<int> map;
    auto a = map.Insert(1);
    auto b = map.Insert(2);
    map.Clear();
    EXPECT_EQ(map.Size(), 0u);
    EXPECT_EQ(map.Get(a), nullptr);
    EXPECT_EQ(map.Get(b), nullptr);
    auto c = map.Insert(3);                           // slots are reusable
    EXPECT_EQ(*map.Get(c), 3);
    EXPECT_EQ(map.Get(a), nullptr);
}

TEST(SlotMap, MoveTransfersOwnership) {
    SlotMap<int> a;
    auto h = a.Insert(9);
    SlotMap<int> b = std::move(a);
    EXPECT_EQ(*b.Get(h), 9);
    EXPECT_EQ(a.Size(), 0u);
}

TEST(Handle, PackUnpackAndHash) {
    Handle<int> h{12345, 678};
    EXPECT_EQ(Handle<int>::Unpack(h.Pack()), h);
    std::unordered_map<Handle<int>, int> map;
    map[h] = 1;
    EXPECT_EQ(map.at(h), 1);
    static_assert(sizeof(Handle<int>) == 8, "handles are 8 bytes");
}

TEST(Span, ViewsContainers) {
    std::vector<int> v = {1, 2, 3, 4, 5};
    Span<int> s(v);
    EXPECT_EQ(s.size(), 5u);
    s[0] = 10;
    EXPECT_EQ(v[0], 10);                              // a view, not a copy

    std::array<int, 3> a = {7, 8, 9};
    Span<const int> sa(a);
    EXPECT_EQ(sa.back(), 9);

    int raw[4] = {1, 2, 3, 4};
    Span<int> sr(raw);
    EXPECT_EQ(sr.size(), 4u);

    Span<const int> converted = s;                    // Span<T> -> Span<const T>
    EXPECT_EQ(converted.size(), 5u);
}

TEST(Span, Subranges) {
    std::vector<int> v = {0, 1, 2, 3, 4, 5};
    Span<int> s(v);
    EXPECT_EQ(s.first(2).back(), 1);
    EXPECT_EQ(s.last(2).front(), 4);
    EXPECT_EQ(s.subspan(2, 3).size(), 3u);
    EXPECT_EQ(s.subspan(4).size(), 2u);
    EXPECT_EQ(s.subspan(4, 100).size(), 2u);          // clamped
    EXPECT_TRUE(s.subspan(6).empty());
    int sum = 0;
    for (int x : s.subspan(1, 3)) sum += x;
    EXPECT_EQ(sum, 6);
}

TEST(Span, BytesAndEmpty) {
    Span<int> none;
    EXPECT_TRUE(none.empty());
    int values[2] = {1, 2};
    Span<int> s(values);
    EXPECT_EQ(s.size_bytes(), 2 * sizeof(int));
    EXPECT_EQ(AsBytes(s).size(), 2 * sizeof(int));
}

namespace {
    Result<int> ParsePositive(int v) {
        if (v < 0) return Err("negative");
        return Ok(v);
    }
    Result<void> MightFail(bool fail) {
        if (fail) return Err(42, "failed");
        return Ok();
    }
}

TEST(Result, SuccessAndFailure) {
    auto good = ParsePositive(5);
    ASSERT_TRUE(good);
    EXPECT_EQ(good.Value(), 5);
    EXPECT_TRUE(good.IsOk());

    auto bad = ParsePositive(-1);
    EXPECT_FALSE(bad);
    EXPECT_TRUE(bad.IsErr());
    EXPECT_EQ(bad.GetError().message, "negative");
}

TEST(Result, ValueOrMapAndThen) {
    EXPECT_EQ(ParsePositive(-1).ValueOr(9), 9);
    EXPECT_EQ(ParsePositive(3).ValueOr(9), 3);

    auto doubled = ParsePositive(4).Map([](int v) { return v * 2; });
    EXPECT_EQ(doubled.Value(), 8);
    EXPECT_FALSE(ParsePositive(-4).Map([](int v) { return v * 2; }));

    auto chained = ParsePositive(4).AndThen([](int v) { return ParsePositive(v - 10); });
    EXPECT_FALSE(chained);
    EXPECT_EQ(chained.GetError().message, "negative");
}

TEST(Result, VoidAndErrorCodes) {
    EXPECT_TRUE(MightFail(false));
    auto r = MightFail(true);
    EXPECT_FALSE(r);
    EXPECT_EQ(r.GetError().code, 42);
    EXPECT_EQ(r.GetError().message, "failed");
}

TEST(Result, MoveOnlyValue) {
    Result<std::unique_ptr<int>> r = Ok(std::make_unique<int>(5));
    ASSERT_TRUE(r);
    std::unique_ptr<int> p = std::move(r).Value();
    EXPECT_EQ(*p, 5);
}

TEST(Result, CustomErrorType) {
    enum class Code { NotFound };
    Result<int, Code> r = Err(Code::NotFound);
    EXPECT_TRUE(r.IsErr());
    EXPECT_EQ(r.GetError(), Code::NotFound);
}

TEST(Optional, SomeAndNone) {
    Optional<int> a = Some(3);
    Optional<int> b = None;
    EXPECT_TRUE(a.has_value());
    EXPECT_FALSE(b.has_value());
    EXPECT_EQ(*a, 3);
}

TEST(StringId, EqualNamesGiveEqualIds) {
    constexpr StringId a("albedo");
    constexpr StringId b("albedo");
    constexpr StringId c("normal");
    static_assert(a == b, "same text, same id, at compile time");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_TRUE(a.IsValid());
    EXPECT_FALSE(StringId().IsValid());
}

TEST(StringId, UsableInSwitch) {
    using namespace Literals;
    constexpr StringId id = "roughness"_sid;
    int result = 0;
    switch (id.Value()) {
        case "metallic"_sid .Value(): result = 1; break;
        case "roughness"_sid .Value(): result = 2; break;
        default: result = 3;
    }
    EXPECT_EQ(result, 2);
}

TEST(StringId, InternKeepsTheText) {
    StringId id = StringId::Intern("interned_name_for_test");
    EXPECT_EQ(id, StringId("interned_name_for_test"));
    EXPECT_EQ(id.ToString(), "interned_name_for_test");
    EXPECT_EQ(StringId("never_interned_name").ToString().front(), '#');   // falls back to the hash
    EXPECT_FALSE(StringId::HadCollision());
}

TEST(StringId, WorksAsHashKey) {
    std::unordered_map<StringId, int> map;
    map[StringId("a")] = 1;
    map[StringId("b")] = 2;
    EXPECT_EQ(map[StringId("a")], 1);
    EXPECT_EQ(map.size(), 2u);
}
