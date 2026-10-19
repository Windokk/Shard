#include <gtest/gtest.h>

#include <cstring>
#include <thread>
#include <vector>

#include "engine/plugins/context.hpp"
#include "engine/plugins/manager.hpp"
#include "plugins/plugin_test_utils.hpp"

using namespace Shard::Engine;
using namespace Shard::Engine::Plugins;
using PluginTest::TempDir;
using nlohmann::json;

namespace {
    // A host that counts what goes through its allocator
    struct CountingHost {
        std::atomic<int> live{ 0 };
        std::atomic<size_t> bytes{ 0 };
        HostBindings Bindings()
        {
            HostBindings b;
            b.allocate = [this](size_t size, size_t align) -> void* { ++live; bytes += size; return ::operator new(size, std::align_val_t(align)); };
            b.deallocate = [this](void* p, size_t size, size_t align) { --live; bytes -= size; ::operator delete(p, std::align_val_t(align)); };
            return b;
        }
    };
}

TEST(PluginMemory, CountsWhatIsAllocatedAndFreed)
{
    CountingHost host;
    PluginMemory memory(host.Bindings(), 0, false);
    void* a = memory.Allocate(100);
    void* b = memory.Allocate(1000, 64);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(a) % 16, 0u);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(b) % 64, 0u);
    EXPECT_EQ(memory.Used(), 1100u);
    EXPECT_EQ(memory.Allocations(), 2u);
    EXPECT_EQ(host.live.load(), 2);
    std::memset(a, 0xAB, 100);          // the whole block is ours
    std::memset(b, 0xCD, 1000);
    memory.Free(a);
    EXPECT_EQ(memory.Used(), 1000u);
    EXPECT_EQ(memory.Peak(), 1100u);
    memory.Free(b);
    EXPECT_EQ(memory.Used(), 0u);
    EXPECT_EQ(host.live.load(), 0);
    memory.Free(nullptr);          // harmless
    EXPECT_EQ(memory.Allocate(8, 3), nullptr);          // not a power of two
}

TEST(PluginMemory, ABudgetIsAWarningUnlessEnforced)
{
    CountingHost host;
    PluginMemory soft(host.Bindings(), 1000, false);
    void* a = soft.Allocate(800);
    void* b = soft.Allocate(800);
    EXPECT_NE(b, nullptr);                  // over, and allowed
    EXPECT_TRUE(soft.WentOver());
    EXPECT_EQ(soft.Failures(), 0u);
    soft.Free(a); soft.Free(b);

    PluginMemory hard(host.Bindings(), 1000, true);
    void* c = hard.Allocate(800);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(hard.Allocate(800), nullptr);
    EXPECT_EQ(hard.Failures(), 1u);
    EXPECT_EQ(hard.Used(), 800u);
    EXPECT_NE(hard.Allocate(100), nullptr);          // what fits still fits
    hard.Free(c);
    EXPECT_NE(hard.Allocate(900), nullptr);          // room again
}

TEST(PluginMemory, NewAndDeleteRunConstructorsAndDestructors)
{
    CountingHost host;
    PluginMemory memory(host.Bindings(), 0, false);
    struct Probe { int* destroyed; int value; Probe(int* d, int v) : destroyed(d), value(v) {} ~Probe() { ++*destroyed; } };
    int destroyed = 0;
    Probe* p = memory.New<Probe>(&destroyed, 5);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->value, 5);
    EXPECT_EQ(memory.Used(), sizeof(Probe));
    memory.Delete(p);
    EXPECT_EQ(destroyed, 1);
    EXPECT_EQ(memory.Used(), 0u);
}

TEST(PluginMemory, ContainersCanUseTheAllocator)
{
    CountingHost host;
    PluginMemory memory(host.Bindings(), 0, false);
    {
        std::vector<int, PluginAllocator<int>> numbers{ PluginAllocator<int>(memory) };
        for (int i = 0; i < 1000; ++i) numbers.push_back(i);
        EXPECT_GE(memory.Used(), 1000 * sizeof(int));
        EXPECT_EQ(numbers[999], 999);
    }
    EXPECT_EQ(memory.Used(), 0u);
    EXPECT_EQ(host.live.load(), 0);

    PluginMemory small(host.Bindings(), 64, true);
    std::vector<int, PluginAllocator<int>> limited{ PluginAllocator<int>(small) };
    EXPECT_THROW(limited.resize(1000), std::bad_alloc);
}

TEST(PluginMemory, IsSafeFromSeveralThreads)
{
    CountingHost host;
    PluginMemory memory(host.Bindings(), 0, false);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 2000; ++i) { void* p = memory.Allocate(64 + size_t(i % 7)); memory.Free(p); }
        });
    for (auto& t : threads) t.join();
    EXPECT_EQ(memory.Used(), 0u);
    EXPECT_EQ(host.live.load(), 0);
    EXPECT_EQ(memory.Allocations(), 8000u);
}

TEST(PluginMemory, TheBudgetComesFromTheManifestAndShowsInTheReports)
{
    PluginManager& pm = PluginManager::Get();
    pm.ResetForTests();
    class Hog final : public IModule {
    public:
        void OnLoad(PluginContext& ctx) override { block = ctx.Memory().Allocate(3 * 1024 * 1024); refused = ctx.Memory().Allocate(3 * 1024 * 1024); m = &ctx.Memory(); }
        void OnUnload(PluginContext&) override { m->Free(block); }
        void* block = nullptr;
        void* refused = reinterpret_cast<void*>(1);
        PluginMemory* m = nullptr;
    };
    struct Factory { static IPlugin* Make() { return new Hog(); } };

    json manifest = PluginTest::Manifest("hog");
    manifest["kind"] = "module";
    manifest["moduleKind"] = "code";
    manifest["budgets"] = { { "memoryMB", 4.0 }, { "enforce", true } };
    pm.AddStaticPlugin(manifest.dump(), &Factory::Make);
    PluginManagerConfig config;
    config.host = Host::Headless;
    config.registerConsoleCommands = false;
    ASSERT_TRUE(pm.Startup(config));
    PluginRecord* r = pm.Find("hog");
    ASSERT_NE(r, nullptr);
    auto* hog = static_cast<Hog*>(r->instance);
    EXPECT_NE(hog->block, nullptr);
    EXPECT_EQ(hog->refused, nullptr);                              // 3 + 3 MB passes the enforced 4 MB
    EXPECT_EQ(r->context->Memory().Failures(), 1u);
    pm.OnEngineSystemsReady(nullptr, nullptr, nullptr);
    pm.OnEngineInitialized();
    pm.UpdateBudgets();
    EXPECT_NEAR(r->memoryMB, 3.0, 0.01);
    EXPECT_TRUE(r->overMemoryBudget);                              // it went over once, even if refused
    EXPECT_NE(pm.DescribePlugin("hog").find("counted memory"), std::string::npos);
    pm.OnEngineDestroying();
    pm.ResetForTests();
}
