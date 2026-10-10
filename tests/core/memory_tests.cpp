#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <thread>
#include <vector>

#include "engine/core/memory/frame_allocator.hpp"
#include "engine/core/memory/linear_allocator.hpp"
#include "engine/core/memory/memory_tracker.hpp"
#include "engine/core/memory/pool_allocator.hpp"
#include "engine/core/memory/stack_allocator.hpp"

using namespace Shard::Engine::Core;

TEST(Alignment, Helpers) {
    EXPECT_TRUE(IsPowerOfTwo(1));
    EXPECT_TRUE(IsPowerOfTwo(64));
    EXPECT_FALSE(IsPowerOfTwo(0));
    EXPECT_FALSE(IsPowerOfTwo(48));
    EXPECT_EQ(AlignUp(size_t(0), 16), 0u);
    EXPECT_EQ(AlignUp(size_t(1), 16), 16u);
    EXPECT_EQ(AlignUp(size_t(16), 16), 16u);
    EXPECT_EQ(AlignUp(size_t(17), 16), 32u);
    void* p = AlignedAlloc(100, 128);
    EXPECT_TRUE(IsAligned(p, 128));
    AlignedFree(p, 128);
}

TEST(LinearAllocator, AllocatesAlignedAndInOrder) {
    LinearAllocator arena(1024);
    char* a = static_cast<char*>(arena.Allocate(10, 1));
    void* b = arena.Allocate(8, 16);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(IsAligned(b, 16));
    EXPECT_GT(static_cast<char*>(b), a + 9);
    EXPECT_TRUE(arena.Owns(a));
}

TEST(LinearAllocator, ReturnsNullWhenFullAndResetFreesEverything) {
    LinearAllocator arena(256);
    EXPECT_NE(arena.Allocate(200, 1), nullptr);
    EXPECT_EQ(arena.Allocate(100, 1), nullptr);          // does not fit : no growth
    EXPECT_EQ(arena.PeakUsed(), 200u);
    arena.Reset();
    EXPECT_EQ(arena.Used(), 0u);
    EXPECT_NE(arena.Allocate(256, 1), nullptr);
}

TEST(LinearAllocator, NewAndNewArray) {
    struct Point { int x = 1, y = 2; };
    LinearAllocator arena(1024);
    Point* p = arena.New<Point>();
    EXPECT_EQ(p->x, 1);
    int* values = arena.NewArray<int>(10);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(values[i], 0);   // value-initialised
}

TEST(LinearAllocator, WrapsAnExternalBuffer) {
    alignas(16) unsigned char buffer[128];
    LinearAllocator arena(buffer, sizeof(buffer));
    void* p = arena.Allocate(32, 16);
    EXPECT_GE(p, static_cast<void*>(buffer));
    EXPECT_LT(p, static_cast<void*>(buffer + sizeof(buffer)));
}

TEST(StackAllocator, FreeGivesBackOnlyTheLatestAllocation) {
    StackAllocator stack(1024);
    void* a = stack.Allocate(32);
    void* b = stack.Allocate(32);
    const size_t usedWithBoth = stack.Used();
    EXPECT_FALSE(stack.Free(a));                     // not the latest
    EXPECT_EQ(stack.Used(), usedWithBoth);
    EXPECT_TRUE(stack.Free(b));
    EXPECT_LT(stack.Used(), usedWithBoth);
    EXPECT_TRUE(stack.Free(a));
    EXPECT_EQ(stack.Used(), 0u);
    EXPECT_FALSE(stack.Free(a));                     // already gone
}

TEST(StackAllocator, MarkerRewindsEverythingSince) {
    StackAllocator stack(1024);
    void* keep = stack.Allocate(16);
    const auto marker = stack.GetMarker();
    stack.Allocate(100);
    stack.Allocate(100);
    stack.RewindTo(marker);
    EXPECT_EQ(stack.GetMarker(), marker);
    EXPECT_TRUE(stack.Free(keep));                   // the surviving allocation is the latest again
}

TEST(StackAllocator, ScopedStackRewindsOnExit) {
    StackAllocator stack(1024);
    stack.Allocate(16);
    const size_t before = stack.Used();
    {
        ScopedStack scope(stack);
        stack.Allocate(200);
        {
            ScopedStack inner(stack);
            stack.Allocate(200);
        }
        EXPECT_GT(stack.Used(), before);
    }
    EXPECT_EQ(stack.Used(), before);
}

TEST(StackAllocator, AlignmentAndFull) {
    StackAllocator stack(128);
    void* p = stack.Allocate(8, 32);
    EXPECT_TRUE(IsAligned(p, 32));
    EXPECT_EQ(stack.Allocate(1000), nullptr);
}

TEST(PoolAllocator, AllocatesDistinctBlocksUntilFull) {
    PoolAllocator pool(24, 10);
    std::set<void*> blocks;
    for (int i = 0; i < 10; ++i) {
        void* b = pool.Allocate();
        ASSERT_NE(b, nullptr);
        EXPECT_TRUE(pool.Owns(b));
        EXPECT_TRUE(blocks.insert(b).second);
    }
    EXPECT_TRUE(pool.Full());
    EXPECT_EQ(pool.Allocate(), nullptr);
}

TEST(PoolAllocator, FreedBlocksAreReused) {
    PoolAllocator pool(16, 4);
    void* a = pool.Allocate();
    pool.Allocate();
    EXPECT_TRUE(pool.Free(a));
    EXPECT_EQ(pool.Allocate(), a);                   // LIFO free list
    EXPECT_EQ(pool.InUse(), 2u);
    EXPECT_EQ(pool.PeakInUse(), 2u);
}

TEST(PoolAllocator, RejectsDoubleFreeAndForeignPointers) {
    PoolAllocator pool(16, 4);
    void* a = pool.Allocate();
    EXPECT_TRUE(pool.Free(a));
    EXPECT_FALSE(pool.Free(a));                      // double free
    int local;
    EXPECT_FALSE(pool.Free(&local));                 // not ours
    char* inside = static_cast<char*>(pool.Allocate()) + 1;
    EXPECT_FALSE(pool.Free(inside));                 // inside a block, not at its start
    EXPECT_FALSE(pool.Free(nullptr));
}

TEST(PoolAllocator, BlocksAreAligned) {
    PoolAllocator pool(10, 8, 32);
    EXPECT_EQ(pool.BlockSize() % 32, 0u);
    for (int i = 0; i < 8; ++i) EXPECT_TRUE(IsAligned(pool.Allocate(), 32));
}

namespace {
    struct Counted {
        explicit Counted(int* alive, int id = 0) : alive(alive), id(id) { ++*alive; }
        ~Counted() { --*alive; }
        int* alive;
        int id;
    };
}

TEST(ObjectPool, ConstructsAndDestroysObjects) {
    int alive = 0;
    ObjectPool<Counted> pool(3);
    Counted* a = pool.Create(&alive, 7);
    Counted* b = pool.Create(&alive, 8);
    EXPECT_EQ(alive, 2);
    EXPECT_EQ(a->id, 7);
    EXPECT_EQ(pool.Size(), 2u);
    EXPECT_TRUE(pool.Destroy(a));
    EXPECT_EQ(alive, 1);
    EXPECT_FALSE(pool.Destroy(a));                   // already destroyed
    (void)b;
}

TEST(ObjectPool, DestroysSurvivorsWithThePool) {
    int alive = 0;
    {
        ObjectPool<Counted> pool(5);
        pool.Create(&alive);
        pool.Create(&alive);
        pool.Create(&alive);
        EXPECT_EQ(alive, 3);
    }
    EXPECT_EQ(alive, 0);
}

TEST(ObjectPool, FullReturnsNull) {
    int alive = 0;
    ObjectPool<Counted> pool(1);
    EXPECT_NE(pool.Create(&alive), nullptr);
    EXPECT_EQ(pool.Create(&alive), nullptr);
}

TEST(FrameAllocator, AllocationsAreDisjointAcrossThreads) {
    FrameAllocator frame(1 << 20);
    constexpr int kThreads = 4, kPerThread = 2000;
    std::vector<std::vector<uint32_t*>> results(kThreads);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                uint32_t* p = frame.New<uint32_t>(uint32_t(t * kPerThread + i));
                results[t].push_back(p);
            }
        });
    for (auto& t : threads) t.join();

    std::set<uint32_t*> unique;
    for (int t = 0; t < kThreads; ++t)
        for (int i = 0; i < kPerThread; ++i) {
            EXPECT_EQ(*results[t][i], uint32_t(t * kPerThread + i));   // nobody overwrote anybody
            unique.insert(results[t][i]);
        }
    EXPECT_EQ(unique.size(), size_t(kThreads * kPerThread));
    EXPECT_EQ(frame.OverflowCount(), 0u);
}

TEST(FrameAllocator, DataLivesForTheConfiguredNumberOfFrames) {
    FrameAllocator frame(1024, 2);
    int* mine = frame.New<int>(42);
    frame.NextFrame();
    EXPECT_EQ(*mine, 42);                            // still valid one frame later (2 buffers)
    int* next = frame.New<int>(7);
    EXPECT_NE(next, mine);
    EXPECT_EQ(*mine, 42);                            // the new frame allocates elsewhere
    frame.NextFrame();                               // back to the first buffer : cleared
    EXPECT_EQ(frame.UsedThisFrame(), 0u);
    EXPECT_EQ(frame.FrameNumber(), 2u);
}

TEST(FrameAllocator, OverflowFallsBackToTheHeap) {
    FrameAllocator frame(128, 1);
    EXPECT_NE(frame.Allocate(100, 1), nullptr);
    void* big = frame.Allocate(1000, 8);             // does not fit
    ASSERT_NE(big, nullptr);
    std::memset(big, 0xAB, 1000);                    // really usable memory
    EXPECT_EQ(frame.OverflowCount(), 1u);
    frame.NextFrame();                               // frees the overflow
    EXPECT_EQ(frame.UsedThisFrame(), 0u);
}

TEST(FrameAllocator, TracksThePeakBudget) {
    FrameAllocator frame(4096, 2);
    frame.Allocate(300, 1);
    frame.NextFrame();
    frame.Allocate(100, 1);
    frame.NextFrame();
    EXPECT_GE(frame.PeakPerFrame(), 300u);
}

TEST(MemoryTracker, RecordsLeaksAndStatsByTag) {
    MemoryTracker& tracker = MemoryTracker::Get();
    const bool wasEnabled = tracker.IsEnabled();
    tracker.Reset();
    tracker.SetEnabled(true);

    int a, b, c;
    tracker.RecordAlloc(&a, 100, "Alpha", "file.cpp", 10);
    tracker.RecordAlloc(&b, 50, "Alpha");
    tracker.RecordAlloc(&c, 8, "Beta");
    EXPECT_EQ(tracker.LiveCount(), 3u);
    EXPECT_EQ(tracker.LiveBytes(), 158u);

    tracker.RecordFree(&b);
    auto stats = tracker.GetStats();
    ASSERT_EQ(stats.size(), 2u);
    EXPECT_EQ(stats[0].tag, "Alpha");
    EXPECT_EQ(stats[0].liveBytes, 100u);
    EXPECT_EQ(stats[0].peakBytes, 150u);
    EXPECT_EQ(stats[0].totalAllocations, 2u);

    const std::string report = tracker.Report();
    EXPECT_NE(report.find("[Alpha]"), std::string::npos);
    EXPECT_NE(report.find("file.cpp:10"), std::string::npos);

    tracker.RecordFree(&a);
    tracker.RecordFree(&c);
    EXPECT_EQ(tracker.LiveCount(), 0u);
    EXPECT_TRUE(tracker.Report().empty());

    tracker.Reset();
    tracker.SetEnabled(wasEnabled);
}

TEST(MemoryTracker, AllocatorsRegisterTheirBuffers) {
    MemoryTracker& tracker = MemoryTracker::Get();
    const bool wasEnabled = tracker.IsEnabled();
    tracker.Reset();
    tracker.SetEnabled(true);
    {
        LinearAllocator arena(4096, "TestArena");
        EXPECT_EQ(tracker.LiveCount(), 1u);
        EXPECT_EQ(tracker.GetStats()[0].tag, "TestArena");
        EXPECT_EQ(tracker.LiveBytes(), 4096u);
    }
    EXPECT_EQ(tracker.LiveCount(), 0u);              // the destructor freed it : no leak
    tracker.Reset();
    tracker.SetEnabled(wasEnabled);
}

TEST(MemoryTracker, DisabledTrackerRecordsNothing) {
    MemoryTracker& tracker = MemoryTracker::Get();
    const bool wasEnabled = tracker.IsEnabled();
    tracker.Reset();
    tracker.SetEnabled(false);
    int x;
    tracker.RecordAlloc(&x, 10, "Off");
    EXPECT_EQ(tracker.LiveCount(), 0u);
    tracker.SetEnabled(wasEnabled);
}
