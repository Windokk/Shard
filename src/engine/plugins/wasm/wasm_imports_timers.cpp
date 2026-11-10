#include <algorithm>
#include <memory>
#include <unordered_map>

#include <shard/shard_timers.h>

#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

// A timer of a sandboxed plugin is a native timer whose function is a trampoline into the module's table. Each one has a node holding the table
// index and the module's user pointer. A node is freed when its timer finishes, is cancelled, or is found dead by a sweep : the host drops a
// world's timers without telling anyone.

namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr ValType I = ValType::I32;

        struct TimerNode;

        struct WasmTimers final : Registration {
            std::unordered_map<TimerNode*, std::unique_ptr<TimerNode>> nodes;
            std::unordered_map<uint64_t, TimerNode*> byHandle;
            const ShardTimersApi* api = nullptr;
            size_t sweepAt = 64;
        };

        struct TimerNode {
            WasmTimers* owner = nullptr;
            Sandbox* sandbox = nullptr;
            uint32_t fn = 0, user = 0;
            uint64_t handle = 0;
            bool running = false;
        };

        // The sandbox drops its registrations when it is reset, so this is found (or made) at each call and never kept.
        WasmTimers& TimersOf(Sandbox& s, const ShardTimersApi* api)
        {
            for (auto& r : s.registrations)
                if (auto* found = dynamic_cast<WasmTimers*>(r.get())) return *found;
            auto* created = s.Register<WasmTimers>();
            created->api = api;
            return *created;
        }

        // Looked up by address only : the node may have been freed by a cancel from inside its own callback.
        void Drop(WasmTimers& timers, TimerNode* node)
        {
            auto it = timers.nodes.find(node);
            if (it == timers.nodes.end()) return;
            if (it->second->handle) timers.byHandle.erase(it->second->handle);
            timers.nodes.erase(it);
        }

        void Sweep(Sandbox& s, WasmTimers& timers)
        {
            if (timers.nodes.size() < timers.sweepAt) return;
            std::vector<TimerNode*> dead;
            for (auto& [node, owned] : timers.nodes)
                if (!owned->running && owned->handle && timers.api->is_active(s.ctx, owned->handle) != SHARD_TRUE) dead.push_back(node);
            for (TimerNode* node : dead) Drop(timers, node);
            timers.sweepAt = std::max<size_t>(64, timers.nodes.size() * 2);
        }

        void SHARD_CALL OnceTrampoline(void* user)
        {
            auto* node = static_cast<TimerNode*>(user);
            Sandbox& s = *node->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            const uint32_t fn = node->fn, arg = node->user;
            Drop(*node->owner, node);
            if (s.dead) return;
            static const FuncType type{ { I }, {} };
            std::vector<uint64_t> out;
            s.CallTable(fn, type, { arg }, Budget::Frame, out);
        }

        void SHARD_CALL EveryTrampoline(void* user)
        {
            auto* node = static_cast<TimerNode*>(user);
            Sandbox& s = *node->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            if (s.dead) return;
            node->running = true;
            const uint32_t fn = node->fn, arg = node->user;
            static const FuncType type{ { I }, {} };
            std::vector<uint64_t> out;
            s.CallTable(fn, type, { arg }, Budget::Frame, out);
            // The callback may have cancelled its own timer, which freed the node
            WasmTimers& timers = *node->owner;
            if (timers.nodes.count(node)) node->running = false;
        }

        void SHARD_CALL StepTrampoline(void* user, ShardWait* wait)
        {
            namespace Lw = Layout::ShardWait;
            auto* node = static_cast<TimerNode*>(user);
            Sandbox& s = *node->sandbox;
            WasmTimers& timers = *node->owner;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            wait->kind = SHARD_WAIT_DONE;
            if (s.dead) { Drop(timers, node); return; }
            node->running = true;
            const uint32_t fn = node->fn, arg = node->user;

            const SandboxHeap::Mark mark = s.heap.TempMark();
            const uint32_t at = s.heap.Temp(Lw::kSize, Lw::kAlign);
            bool called = false;
            if (at) {
                uint8_t zero[Lw::kSize] = {};
                s.instance->Write(at, zero, Lw::kSize);
                static const FuncType type{ { I, I }, {} };
                std::vector<uint64_t> out;
                called = s.CallTable(fn, type, { arg, at }, Budget::Frame, out);
                uint8_t raw[Lw::kSize] = {};
                if (called && s.instance->Read(at, raw, Lw::kSize)) {
                    wait->kind = Load<int32_t>(raw, Lw::o_kind);
                    wait->seconds = Load<double>(raw, Lw::o_seconds);
                    wait->latch = Load<uint64_t>(raw, Lw::o_latch);
                } else {
                    called = false;
                }
                s.heap.TempRelease(mark);
            }
            if (!called) wait->kind = SHARD_WAIT_DONE;

            if (timers.nodes.count(node)) {
                node->running = false;
                if (wait->kind != SHARD_WAIT_SECONDS && wait->kind != SHARD_WAIT_LATCH) Drop(timers, node);
            }
        }

        template <typename Make>
        bool Create(Sandbox& s, WasmTimers& timers, Instance& in, uint64_t outAt, uint32_t fn, uint32_t user, Make&& make, uint64_t* r)
        {
            Sweep(s, timers);
            OutArg<ShardTimer> out(in, outAt);
            if (!out.ok) return false;
            auto node = std::make_unique<TimerNode>();
            node->owner = &timers;
            node->sandbox = &s;
            node->fn = fn;
            node->user = user;
            TimerNode* raw = node.get();
            const ShardResult result = make(raw, out.value);
            if (result == SHARD_OK && out.value) {
                raw->handle = out.value;
                timers.byHandle[out.value] = raw;
                timers.nodes[raw] = std::move(node);
            }
            r[0] = uint32_t(result);
            return out.Commit();
        }
    }

    void AddTimersImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        const ShardTimersApi* api = apis.timers;
        auto unsupported = [](Instance&, const uint64_t*, uint64_t* r) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; };
        if (!api) {
            for (const char* name : { "timers.after", "timers.every", "timers.start", "timers.cancel", "timers.cancel_all" }) imports.AddManual(name, unsupported);
            return;
        }
        imports.AddManual("timers.after", [&s, api](Instance& in, const uint64_t* a, uint64_t* r) {
            WasmTimers* timers = &TimersOf(s, api);
            const double seconds = F64(a[1]);
            return Create(s, *timers, in, a[4], uint32_t(a[2]), uint32_t(a[3]), [&](TimerNode* node, ShardTimer& out) {
                return api->after(s.ctx, seconds, &OnceTrampoline, node, &out);
            }, r);
        });
        imports.AddManual("timers.every", [&s, api](Instance& in, const uint64_t* a, uint64_t* r) {
            WasmTimers* timers = &TimersOf(s, api);
            const double interval = F64(a[1]);
            return Create(s, *timers, in, a[4], uint32_t(a[2]), uint32_t(a[3]), [&](TimerNode* node, ShardTimer& out) {
                return api->every(s.ctx, interval, &EveryTrampoline, node, &out);
            }, r);
        });
        imports.AddManual("timers.start", [&s, api](Instance& in, const uint64_t* a, uint64_t* r) {
            WasmTimers* timers = &TimersOf(s, api);
            return Create(s, *timers, in, a[3], uint32_t(a[1]), uint32_t(a[2]), [&](TimerNode* node, ShardTimer& out) {
                return api->start(s.ctx, &StepTrampoline, node, &out);
            }, r);
        });
        imports.AddManual("timers.cancel", [&s, api](Instance&, const uint64_t* a, uint64_t* r) {
            WasmTimers* timers = &TimersOf(s, api);
            const ShardResult result = api->cancel(s.ctx, a[1]);
            // Finished and unknown handles are dropped too : their node is no use to anyone
            auto it = timers->byHandle.find(a[1]);
            if (it != timers->byHandle.end() && !it->second->running) Drop(*timers, it->second);
            r[0] = uint32_t(result);
            return true;
        });
        imports.AddManual("timers.cancel_all", [&s, api](Instance&, const uint64_t*, uint64_t* r) {
            WasmTimers* timers = &TimersOf(s, api);
            const ShardResult result = api->cancel_all(s.ctx);
            std::vector<TimerNode*> idle;
            for (auto& [node, owned] : timers->nodes)
                if (!owned->running) idle.push_back(node);
            for (TimerNode* node : idle) Drop(*timers, node);
            r[0] = uint32_t(result);
            return true;
        });
    }
}
