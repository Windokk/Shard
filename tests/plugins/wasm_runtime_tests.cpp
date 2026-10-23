#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <random>
#include <string>

#include "engine/plugins/wasm/wasm_runtime.hpp"
#include "plugins/wasm_test_module.hpp"

using namespace Shard::Engine::Plugins::Wasm;

// The WebAssembly runtime of sandboxed plugins (docs/design-notes/wasm-sandbox.fr.md, W1), on modules written byte by byte (wasm_test_module.hpp).

using namespace WasmTest;

TEST(WasmRuntime, AddsTwoNumbers)
{
    Builder b;
    const uint32_t t = b.Type({ I32, I32 }, { I32 });
    b.Function(t, Get(0) + Get(1) + Op({ 0x6A, 0x0B }), {}, "add");
    auto in = Make(b);
    ASSERT_TRUE(in);
    EXPECT_EQ(One(*in, "add", { 2, 3 }), 5u);
    EXPECT_EQ(uint32_t(One(*in, "add", { 0xFFFFFFFFu, 2 })), 1u);          // wraps
}

TEST(WasmRuntime, RecursesWithIfElse)
{
    // fact(n) = n == 0 ? 1 : n * fact(n - 1), in i64
    Builder b;
    const uint32_t t = b.Type({ I64 }, { I64 });
    b.Function(t, Get(0) + Op({ 0x50, 0x04, I64 }) + I64Const(1) + Op({ 0x05 }) + Get(0) + Get(0) + I64Const(1) + Op({ 0x7D }) + Call(0) +
                  Op({ 0x7E, 0x0B, 0x0B }), {}, "fact");
    auto in = Make(b);
    ASSERT_TRUE(in);
    EXPECT_EQ(One(*in, "fact", { 20 }), 2432902008176640000ull);
}

TEST(WasmRuntime, LoopsAndBranches)
{
    // sum = 0 ; i = n ; loop { sum += i ; i -= 1 ; br_if i != 0 } ; sum
    Builder b;
    const uint32_t t = b.Type({ I32 }, { I32 });
    b.Function(t, Op({ 0x03, 0x40 }) + Get(1) + Get(0) + Op({ 0x6A }) + Set(1) + Get(0) + I32Const(1) + Op({ 0x6B, 0x22, 0 }) +
                  Op({ 0x0D, 0, 0x0B }) + Get(1) + Op({ 0x0B }), { { 1, I32 } }, "sum");
    // br_table : 0 -> 10, 1 -> 20, anything else -> 30
    b.Function(t, Op({ 0x02, 0x40, 0x02, 0x40, 0x02, 0x40 }) + Get(0) + Op({ 0x0E, 2, 0, 1, 2, 0x0B }) + I32Const(10) + Op({ 0x0F, 0x0B }) +
                  I32Const(20) + Op({ 0x0F, 0x0B }) + I32Const(30) + Op({ 0x0B }), {}, "pick");
    auto in = Make(b);
    ASSERT_TRUE(in);
    EXPECT_EQ(One(*in, "sum", { 100 }), 5050u);
    EXPECT_EQ(One(*in, "pick", { 0 }), 10u);
    EXPECT_EQ(One(*in, "pick", { 1 }), 20u);
    EXPECT_EQ(One(*in, "pick", { 7 }), 30u);
}

TEST(WasmRuntime, MemoryIsBoundedAndSegmentsAreLaidOut)
{
    Builder b;
    b.memoryMin = 1; b.memoryMax = 4;
    b.data.push_back({ 16, "hello" });
    const uint32_t load = b.Type({ I32 }, { I32 });
    const uint32_t store = b.Type({ I32, I32 }, {});
    const uint32_t grow = b.Type({ I32 }, { I32 });
    b.Function(load, Get(0) + Op({ 0x2D, 0, 0, 0x0B }), {}, "load8");
    b.Function(store, Get(0) + Get(1) + Op({ 0x36, 2, 0, 0x0B }), {}, "store");
    b.Function(grow, Get(0) + Op({ 0x40, 0, 0x0B }), {}, "grow");
    auto in = Make(b);
    ASSERT_TRUE(in);
    EXPECT_EQ(One(*in, "load8", { 16 }), uint64_t('h'));
    EXPECT_TRUE(in->Call("store", { 100, 0xDEADBEEF }));
    uint32_t v = 0;
    ASSERT_TRUE(in->Read(100, &v, 4));
    EXPECT_EQ(v, 0xDEADBEEFu);

    auto out = in->Call("load8", { 65536 });
    ASSERT_FALSE(out);
    EXPECT_EQ(in->LastTrap().kind, TrapKind::MemoryOutOfBounds);
    EXPECT_FALSE(in->Call("store", { 65534, 1 }));                          // straddles the end

    EXPECT_EQ(One(*in, "grow", { 2 }), 1u);                                 // old size in pages
    EXPECT_EQ(in->MemorySize(), 3u * 65536);
    EXPECT_EQ(uint32_t(One(*in, "grow", { 2 })), 0xFFFFFFFFu);             // past the module's maximum
    EXPECT_FALSE(in->Read(3u * 65536 - 2, &v, 4));                          // the host's view is checked too
}

TEST(WasmRuntime, TheHostLimitOnMemoryWinsOverTheModules)
{
    Builder b;
    b.memoryMin = 1;
    const uint32_t grow = b.Type({ I32 }, { I32 });
    b.Function(grow, Get(0) + Op({ 0x40, 0, 0x0B }), {}, "grow");
    Limits limits;
    limits.maxMemoryPages = 2;
    auto in = Make(b, {}, limits);
    ASSERT_TRUE(in);
    EXPECT_EQ(One(*in, "grow", { 1 }), 1u);
    EXPECT_EQ(uint32_t(One(*in, "grow", { 1 })), 0xFFFFFFFFu);

    b.memoryMin = 3;
    const std::vector<uint8_t> bytes = b.Build();
    auto module = Module::Decode(bytes.data(), bytes.size(), limits);
    ASSERT_TRUE(module);
    EXPECT_FALSE(Instance::Create(module.Value(), {}));                     // needs more at start than allowed
}

TEST(WasmRuntime, AnEndlessLoopRunsOutOfFuelAndTheInstanceStaysUsable)
{
    Builder b;
    const uint32_t t = b.Type({}, {});
    const uint32_t t2 = b.Type({}, { I32 });
    b.Function(t, Op({ 0x03, 0x40, 0x0C, 0, 0x0B, 0x0B }), {}, "spin");
    b.Function(t2, I32Const(7) + Op({ 0x0B }), {}, "seven");
    auto in = Make(b);
    ASSERT_TRUE(in);
    in->SetFuel(10000);
    auto r = in->Call("spin", {});
    ASSERT_FALSE(r);
    EXPECT_EQ(in->LastTrap().kind, TrapKind::OutOfFuel);
    EXPECT_EQ(in->Fuel(), 0u);
    in->SetFuel(100);
    EXPECT_EQ(One(*in, "seven", {}), 7u);
    EXPECT_LT(in->Fuel(), 100u);
}

TEST(WasmRuntime, IntegerTraps)
{
    Builder b;
    const uint32_t t = b.Type({ I32, I32 }, { I32 });
    b.Function(t, Get(0) + Get(1) + Op({ 0x6D, 0x0B }), {}, "div_s");
    b.Function(t, Get(0) + Get(1) + Op({ 0x6F, 0x0B }), {}, "rem_s");
    const uint32_t u = b.Type({ F32 }, { I32 });
    b.Function(u, Get(0) + Op({ 0xA8, 0x0B }), {}, "trunc");
    b.Function(u, Get(0) + Op({ 0xFC, 0x00, 0x0B }), {}, "trunc_sat");
    auto in = Make(b);
    ASSERT_TRUE(in);
    EXPECT_EQ(int32_t(One(*in, "div_s", { uint32_t(-7), 2 })), -3);
    EXPECT_FALSE(in->Call("div_s", { 1, 0 }));
    EXPECT_EQ(in->LastTrap().kind, TrapKind::DivideByZero);
    EXPECT_FALSE(in->Call("div_s", { 0x80000000u, 0xFFFFFFFFu }));
    EXPECT_EQ(in->LastTrap().kind, TrapKind::IntegerOverflow);
    EXPECT_EQ(One(*in, "rem_s", { 0x80000000u, 0xFFFFFFFFu }), 0u);
    EXPECT_EQ(int32_t(One(*in, "trunc", { Bits(-3.9f) })), -3);
    EXPECT_FALSE(in->Call("trunc", { Bits(3e9f) }));
    EXPECT_EQ(in->LastTrap().kind, TrapKind::IntegerOverflow);              // what the spec calls a value out of the integer's range
    EXPECT_FALSE(in->Call("trunc", { Bits(std::nanf("")) }));
    EXPECT_EQ(in->LastTrap().kind, TrapKind::InvalidConversion);
    EXPECT_EQ(uint32_t(One(*in, "trunc_sat", { Bits(3e9f) })), 0x7FFFFFFFu);
    EXPECT_EQ(One(*in, "trunc_sat", { Bits(std::nanf("")) }), 0u);
}

TEST(WasmRuntime, FloatsAreIeeeAndNaNsAreCanonical)
{
    Builder b;
    const uint32_t t = b.Type({ F32, F32 }, { F32 });
    b.Function(t, Get(0) + Get(1) + Op({ 0x95, 0x0B }), {}, "div");
    b.Function(t, Get(0) + Get(1) + Op({ 0x96, 0x0B }), {}, "min");
    const uint32_t d = b.Type({ F64 }, { F64 });
    b.Function(d, Get(0) + Op({ 0x9F, 0x0B }), {}, "sqrt");
    b.Function(d, Get(0) + Op({ 0x9E, 0x0B }), {}, "nearest");
    auto in = Make(b);
    ASSERT_TRUE(in);
    EXPECT_EQ(AsF32(One(*in, "div", { Bits(1.f), Bits(4.f) })), 0.25f);
    EXPECT_EQ(uint32_t(One(*in, "div", { Bits(0.f), Bits(0.f) })), 0x7FC00000u);
    EXPECT_EQ(uint32_t(One(*in, "div", { Bits(-0.f), Bits(0.f) })), 0x7FC00000u);     // positive, whatever the hardware gave
    EXPECT_EQ(uint32_t(One(*in, "min", { Bits(0.f), Bits(-0.f) })), Bits(-0.f));
    EXPECT_EQ(uint32_t(One(*in, "min", { Bits(1.f), Bits(std::nanf("")) })), 0x7FC00000u);
    double m1 = -1.0; uint64_t bits; std::memcpy(&bits, &m1, 8);
    EXPECT_EQ(One(*in, "sqrt", { bits }), 0x7FF8000000000000ull);
    double h = 2.5; std::memcpy(&bits, &h, 8);
    EXPECT_EQ(AsF64(One(*in, "nearest", { bits })), 2.0);                // ties to even
}

TEST(WasmRuntime, HostFunctionsReadTheSandboxMemoryAndCanTrap)
{
    Builder b;
    b.memoryMin = 1;
    b.data.push_back({ 8, "hello" });
    const uint32_t log = b.Type({ I32, I32 }, {});
    const uint32_t twice = b.Type({ I32 }, { I32 });
    const uint32_t run = b.Type({}, { I32 });
    b.Import("shard", "log", log);
    b.Import("shard", "twice", twice);
    b.Function(run, I32Const(8) + I32Const(5) + Call(0) + I32Const(21) + Call(1) + Op({ 0x0B }), {}, "run");
    b.Function(run, I32Const(65530) + I32Const(100) + Call(0) + I32Const(0) + Op({ 0x0B }), {}, "bad");

    std::string logged;
    std::vector<HostImport> imports = {
        { "shard", "log", { { ValType::I32, ValType::I32 }, {} }, [&](Instance& in, const uint64_t* a, uint64_t*) {
            const uint8_t* p = in.MemoryAt(uint32_t(a[0]), uint32_t(a[1]));
            if (!p) { in.HostTrap("message out of the memory"); return false; }
            logged.assign(reinterpret_cast<const char*>(p), uint32_t(a[1]));
            return true;
        } },
        { "shard", "twice", { { ValType::I32 }, { ValType::I32 } }, [](Instance&, const uint64_t* a, uint64_t* r) { r[0] = uint32_t(a[0]) * 2; return true; } },
    };
    auto in = Make(b, imports);
    ASSERT_TRUE(in);
    EXPECT_EQ(One(*in, "run", {}), 42u);
    EXPECT_EQ(logged, "hello");
    auto r = in->Call("bad", {});
    ASSERT_FALSE(r);
    EXPECT_EQ(in->LastTrap().kind, TrapKind::Host);
    EXPECT_NE(r.GetError().message.find("message out of the memory"), std::string::npos);
}

TEST(WasmRuntime, ImportsAreMatchedByNameAndType)
{
    Builder b;
    const uint32_t t = b.Type({ I32 }, {});
    b.Import("shard", "log", t);
    const std::vector<uint8_t> bytes = b.Build();
    auto module = Module::Decode(bytes.data(), bytes.size());
    ASSERT_TRUE(module);
    EXPECT_FALSE(Instance::Create(module.Value(), {}));
    std::vector<HostImport> wrongType = { { "shard", "log", { { ValType::I64 }, {} }, [](Instance&, const uint64_t*, uint64_t*) { return true; } } };
    EXPECT_FALSE(Instance::Create(module.Value(), wrongType));
    std::vector<HostImport> right = { { "shard", "log", { { ValType::I32 }, {} }, [](Instance&, const uint64_t*, uint64_t*) { return true; } } };
    EXPECT_TRUE(Instance::Create(module.Value(), right));
}

TEST(WasmRuntime, IndirectCallsCheckTheTableAndTheType)
{
    Builder b;
    b.tableSize = 3;
    const uint32_t ii = b.Type({ I32 }, { I32 });
    const uint32_t v = b.Type({}, {});
    const uint32_t inc = b.Function(ii, Get(0) + I32Const(1) + Op({ 0x6A, 0x0B }));
    const uint32_t nothing = b.Function(v, Op({ 0x0B }));
    // dispatch(slot, x) = table[slot](x)
    const uint32_t dispatch = b.Type({ I32, I32 }, { I32 });
    b.Function(dispatch, Get(1) + Get(0) + Op({ 0x11, uint8_t(ii), 0, 0x0B }), {}, "dispatch");
    b.elements.push_back({ 0, { inc, nothing } });
    auto in = Make(b);
    ASSERT_TRUE(in);
    EXPECT_EQ(One(*in, "dispatch", { 0, 41 }), 42u);
    EXPECT_FALSE(in->Call("dispatch", { 1, 41 }));
    EXPECT_EQ(in->LastTrap().kind, TrapKind::IndirectCallType);
    EXPECT_FALSE(in->Call("dispatch", { 2, 41 }));
    EXPECT_EQ(in->LastTrap().kind, TrapKind::IndirectCallNull);
    EXPECT_FALSE(in->Call("dispatch", { 9, 41 }));
    EXPECT_EQ(in->LastTrap().kind, TrapKind::TableOutOfBounds);

    // What the host does with a callback : the plugin hands over a table slot
    FuncType type{ { ValType::I32 }, { ValType::I32 } };
    auto r = in->CallIndirect(0, type, { 1 });
    ASSERT_TRUE(r) << r.GetError().message;
    EXPECT_EQ(r.Value()[0], 2u);
    EXPECT_FALSE(in->CallIndirect(1, type, { 1 }));
}

TEST(WasmRuntime, DeepRecursionStopsTheCallNotTheHost)
{
    Builder b;
    const uint32_t t = b.Type({}, {});
    b.Function(t, Call(0) + Op({ 0x0B }), {}, "forever");
    auto in = Make(b);
    ASSERT_TRUE(in);
    EXPECT_FALSE(in->Call("forever", {}));
    EXPECT_EQ(in->LastTrap().kind, TrapKind::CallStackExhausted);
}

TEST(WasmRuntime, TheStartFunctionRunsAtInstantiation)
{
    Builder b;
    b.memoryMin = 1;
    const uint32_t t = b.Type({}, {});
    b.Function(t, I32Const(0) + I32Const(99) + Op({ 0x36, 2, 0, 0x0B }));
    b.start = 0;
    auto in = Make(b);
    ASSERT_TRUE(in);
    uint32_t v = 0;
    ASSERT_TRUE(in->Read(0, &v, 4));
    EXPECT_EQ(v, 99u);

    Builder loop;
    const uint32_t lt = loop.Type({}, {});
    loop.Function(lt, Op({ 0x03, 0x40, 0x0C, 0, 0x0B, 0x0B }));
    loop.start = 0;
    const std::vector<uint8_t> bytes = loop.Build();
    auto module = Module::Decode(bytes.data(), bytes.size());
    ASSERT_TRUE(module);
    EXPECT_FALSE(Instance::Create(module.Value(), {}, 1000));              // a start that never ends is refused, not waited for
}

TEST(WasmRuntime, MalformedModulesAreRefused)
{
    auto refused = [](std::vector<uint8_t> bytes) { return !Module::Decode(bytes.data(), bytes.size()); };
    EXPECT_TRUE(refused({}));
    EXPECT_TRUE(refused({ 0, 'a', 's', 'n', 1, 0, 0, 0 }));
    EXPECT_TRUE(refused({ 0, 'a', 's', 'm', 2, 0, 0, 0 }));
    EXPECT_FALSE(refused({ 0, 'a', 's', 'm', 1, 0, 0, 0 }));               // empty is a valid module

    Builder b;
    const uint32_t t = b.Type({}, {});
    b.Function(t, Op({ 0xFD, 0x00, 0x0B }));                                 // SIMD
    EXPECT_TRUE(refused(b.Build()));
    Builder unclosed;
    unclosed.Function(unclosed.Type({}, {}), Op({ 0x02, 0x40, 0x0B }));      // the block's end, not the function's
    EXPECT_TRUE(refused(unclosed.Build()));
    Builder badLocal;
    badLocal.Function(badLocal.Type({}, {}), Get(3) + Op({ 0x1A, 0x0B }));
    EXPECT_TRUE(refused(badLocal.Build()));
    Builder badBranch;
    badBranch.Function(badBranch.Type({}, {}), Op({ 0x0C, 5, 0x0B }));
    EXPECT_TRUE(refused(badBranch.Build()));
    Builder noMemory;
    noMemory.Function(noMemory.Type({}, { I32 }), I32Const(0) + Op({ 0x28, 2, 0, 0x0B }));
    EXPECT_TRUE(refused(noMemory.Build()));

    // An imported memory : the sandbox owns its memory, it never gets the host's
    std::vector<uint8_t> importMemory = { 0, 'a', 's', 'm', 1, 0, 0, 0, 2, 12, 1, 3, 'e', 'n', 'v', 3, 'm', 'e', 'm', 2, 0, 1 };
    EXPECT_TRUE(refused(importMemory));
}

TEST(WasmRuntime, BodiesAreTypeCheckedWhenDecoded)
{
    auto refused = [](const Builder& b) { const std::vector<uint8_t> bytes = b.Build(); return !Module::Decode(bytes.data(), bytes.size()); };
    Builder underflow;
    underflow.Function(underflow.Type({}, { I32 }), Op({ 0x6A, 0x0B }));                         // i32.add on an empty stack
    EXPECT_TRUE(refused(underflow));
    Builder wrongType;
    wrongType.Function(wrongType.Type({}, { I32 }), I64Const(1) + I32Const(1) + Op({ 0x6A, 0x0B }));     // i32.add of an i64
    EXPECT_TRUE(refused(wrongType));
    Builder wrongResult;
    wrongResult.Function(wrongResult.Type({}, { I32 }), I64Const(1) + Op({ 0x0B }));
    EXPECT_TRUE(refused(wrongResult));
    Builder leftover;
    leftover.Function(leftover.Type({}, {}), I32Const(1) + Op({ 0x0B }));                       // a value left on the stack
    EXPECT_TRUE(refused(leftover));
    Builder ifWithoutElse;
    ifWithoutElse.Function(ifWithoutElse.Type({}, { I32 }), I32Const(1) + Op({ 0x04, I32 }) + I32Const(2) + Op({ 0x0B, 0x0B }));
    EXPECT_TRUE(refused(ifWithoutElse));
    Builder badBranch;
    badBranch.Function(badBranch.Type({}, { I32 }), Op({ 0x02, I32 }) + I64Const(1) + Op({ 0x0C, 0, 0x0B, 0x0B }));   // br with an i64 to an i32 block
    EXPECT_TRUE(refused(badBranch));

    // After an unconditional branch anything goes : the rest of the block is never run
    Builder afterBranch;
    afterBranch.Function(afterBranch.Type({}, { I32 }), Op({ 0x02, I32 }) + I32Const(1) + Op({ 0x0C, 0, 0x6A, 0x0B, 0x0B }), {}, "f");
    EXPECT_FALSE(refused(afterBranch));
    Builder trap;
    trap.Function(trap.Type({}, { I64 }), Op({ 0x00, 0x0B }), {}, "f");
    EXPECT_FALSE(refused(trap));
}

TEST(WasmRuntime, FlippedBytesNeverCrashTheHost)
{
    Builder b;
    b.memoryMin = 1; b.memoryMax = 2;
    b.tableSize = 2;
    b.data.push_back({ 0, "abcdefgh" });
    const uint32_t ii = b.Type({ I32 }, { I32 });
    const uint32_t fact = b.Type({ I64 }, { I64 });
    const uint32_t f0 = b.Function(ii, Get(0) + Op({ 0x2D, 0, 0 }) + Get(0) + Op({ 0x6A, 0x0B }), {}, "f");
    b.Function(fact, Get(0) + Op({ 0x50, 0x04, I64 }) + I64Const(1) + Op({ 0x05 }) + Get(0) + Get(0) + I64Const(1) + Op({ 0x7D }) + Call(1) +
                     Op({ 0x7E, 0x0B, 0x0B }), {}, "g");
    b.Function(ii, Op({ 0x03, 0x40 }) + Get(0) + I32Const(1) + Op({ 0x6B, 0x22, 0, 0x0D, 0, 0x0B }) + Get(0) + Op({ 0x0B }), {}, "h");
    b.elements.push_back({ 0, { f0 } });
    const std::vector<uint8_t> good = b.Build();

    std::mt19937 rng(1234);
    int decoded = 0;
    for (int i = 0; i < 1000; ++i) {
        std::vector<uint8_t> bytes = good;
        const int flips = 1 + int(rng() % 4);
        for (int k = 0; k < flips; ++k) bytes[8 + rng() % (bytes.size() - 8)] = uint8_t(rng());
        if (rng() % 8 == 0) bytes.resize(8 + rng() % (bytes.size() - 8));
        auto module = Module::Decode(bytes.data(), bytes.size());
        if (!module) continue;
        ++decoded;
        auto in = Instance::Create(module.Value(), {}, 10000);
        if (!in) continue;
        in.Value()->SetFuel(20000);
        for (const char* name : { "f", "g", "h" }) {
            for (const auto& e : module.Value()->Exports()) {
                if (e.kind != 0 || e.name != name) continue;
                std::vector<uint64_t> args(e.type.params.size(), uint64_t(rng() % 70000));
                (void)in.Value()->Call(name, args);
            }
        }
    }
    EXPECT_GT(decoded, 0);
}
