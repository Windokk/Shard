#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <unordered_map>

#include <shard/shard_jobs.h>

#include "engine/plugins/abi/c_internal.hpp"
#include "engine/plugins/wasm/wasm_imports.hpp"
#include "engine/plugins/wasm/wasm_marshal.hpp"

// shard.jobs for a sandboxed plugin. An instance is one thread's, so the engine's workers never enter a module : its jobs run one after
// the other on the main thread, in submission order, inside the frame's fuel. A job runs when the module waits for it, else at the end of the
// frame or before the plugin is disabled. worker_count is 1, and parallel_for calls its slices in order. Nothing depends on the machine, so replays hold.

namespace Shard::Engine::Plugins::Wasm {

    using namespace Marshal;

    namespace {
        constexpr ValType I = ValType::I32;
        constexpr size_t kMaxJobs = 1u << 16;
        constexpr size_t kMaxPosts = 1u << 16;
        constexpr uint32_t kMaxSlices = 1u << 16;

        struct Job {
            enum class State : uint8_t { Queued, Running, Done };
            uint32_t fn = 0, user = 0;
            uint32_t count = 0, grain = 0;
            bool range = false;
            bool released = false;
            State state = State::Queued;
        };

        using JobMap = std::map<uint64_t, Job>;

        struct PostNode;

        struct WasmJobs final : Registration {
            JobMap jobs;
            std::deque<uint64_t> queue;
            uint64_t next = 1;
            std::unordered_map<PostNode*, std::unique_ptr<PostNode>> posts;
            const ShardJobsApi* api = nullptr;
        };

        struct PostNode {
            WasmJobs* owner = nullptr;
            Sandbox* sandbox = nullptr;
            uint32_t fn = 0, user = 0;
        };

        WasmJobs* FindJobs(Sandbox& s)
        {
            for (auto& r : s.registrations)
                if (auto* found = dynamic_cast<WasmJobs*>(r.get())) return found;
            return nullptr;
        }
        WasmJobs& JobsOf(Sandbox& s, const ShardJobsApi* api)
        {
            if (WasmJobs* found = FindJobs(s)) return *found;
            auto* created = s.Register<WasmJobs>();
            created->api = api;
            return *created;
        }

        uint32_t GrainFor(uint32_t count, uint32_t grain)
        {
            const uint32_t floor = (count + kMaxSlices - 1) / kMaxSlices;
            if (grain == 0) return count;
            return std::max(grain, floor);
        }

        bool RunSlices(Sandbox& s, uint32_t fn, uint32_t user, uint32_t count, uint32_t grain)
        {
            static const FuncType type{ { I, I, I, I }, {} };
            const uint32_t step = GrainFor(count, grain);
            std::vector<uint64_t> out;
            for (uint64_t begin = 0; begin < count && !s.dead; begin += step)
                if (!s.CallTable(fn, type, { user, begin, std::min<uint64_t>(count, begin + step), 0 }, Budget::Frame, out)) return false;
            return !s.dead;
        }

        void RunJob(Sandbox& s, WasmJobs& jobs, uint64_t handle)
        {
            auto it = jobs.jobs.find(handle);
            if (it == jobs.jobs.end() || it->second.state != Job::State::Queued) return;
            const Job job = it->second;
            it->second.state = Job::State::Running;
            if (job.range) {
                RunSlices(s, job.fn, job.user, job.count, job.grain);
            } else {
                static const FuncType type{ { I, I }, {} };
                std::vector<uint64_t> out;
                s.CallTable(job.fn, type, { job.user, 0 }, Budget::Frame, out);
            }
            it = jobs.jobs.find(handle);
            if (it == jobs.jobs.end()) return;
            if (it->second.released) jobs.jobs.erase(it);
            else it->second.state = Job::State::Done;
        }

        void RunThrough(Sandbox& s, WasmJobs& jobs, uint64_t handle)
        {
            while (!jobs.queue.empty() && !s.dead) {
                const uint64_t next = jobs.queue.front();
                jobs.queue.pop_front();
                RunJob(s, jobs, next);
                if (next == handle) return;
            }
        }

        void Drop(WasmJobs& jobs, PostNode* node) { jobs.posts.erase(node); }

        void SHARD_CALL PostTrampoline(void* user)
        {
            auto* node = static_cast<PostNode*>(user);
            Sandbox& s = *node->sandbox;
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            const uint32_t fn = node->fn, arg = node->user;
            Drop(*node->owner, node);
            if (s.dead) return;
            static const FuncType type{ { I }, {} };
            std::vector<uint64_t> out;
            s.CallTable(fn, type, { arg }, Budget::Frame, out);
        }
    }

    void RunPendingJobs(Sandbox& s)
    {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        WasmJobs* jobs = FindJobs(s);
        if (!jobs) return;
        // The frame's fuel is shared with the module's systems : once spent, the rest waits for the next frame.
        while (!jobs->queue.empty() && !s.dead && !(s.frameOpen.load() && s.frameLeft == 0)) {
            const uint64_t handle = jobs->queue.front();
            jobs->queue.pop_front();
            RunJob(s, *jobs, handle);
        }
    }

    void AddJobsImports(Sandbox& s, ImportList& imports, const HostApis& apis)
    {
        const ShardJobsApi* api = apis.jobs;
        auto unsupported = [](Instance&, const uint64_t*, uint64_t* r) { r[0] = uint32_t(SHARD_E_UNSUPPORTED); return true; };

        imports.AddManual("jobs.worker_count", [](Instance&, const uint64_t*, uint64_t* r) { r[0] = 1; return true; });

        auto submit = [&s, api](Instance& in, uint64_t outAt, bool needOut, Job job, uint64_t* r) {
            if (job.fn == 0 || (needOut && outAt == 0)) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            if (outAt != 0 && !Span(in, outAt, 8, "job")) return false;
            WasmJobs& jobs = JobsOf(s, api);
            if (jobs.jobs.size() >= kMaxJobs) { r[0] = uint32_t(SHARD_E_OUT_OF_MEMORY); return true; }
            if (job.range && job.count == 0) {
                const uint64_t none = 0;
                if (outAt != 0) in.Write(uint32_t(outAt), &none, 8);
                r[0] = uint32_t(SHARD_OK);
                return true;
            }
            const uint64_t handle = jobs.next++;
            job.released = outAt == 0;
            jobs.jobs.emplace(handle, job);
            jobs.queue.push_back(handle);
            if (outAt != 0) in.Write(uint32_t(outAt), &handle, 8);
            r[0] = uint32_t(SHARD_OK);
            return true;
        };

        imports.AddManual("jobs.run", [submit](Instance& in, const uint64_t* a, uint64_t* r) {
            Job job;
            job.fn = uint32_t(a[1]);
            job.user = uint32_t(a[2]);
            return submit(in, a[3], false, job, r);
        });
        imports.AddManual("jobs.parallel_for_async", [submit](Instance& in, const uint64_t* a, uint64_t* r) {
            Job job;
            job.range = true;
            job.count = uint32_t(a[1]);
            job.grain = uint32_t(a[2]);
            job.fn = uint32_t(a[3]);
            job.user = uint32_t(a[4]);
            return submit(in, a[5], true, job, r);
        });

        imports.AddManual("jobs.parallel_for", [&s](Instance&, const uint64_t* a, uint64_t* r) {
            if (!CAbi::OnScriptThread()) { r[0] = uint32_t(SHARD_E_WRONG_THREAD); return true; }
            const uint32_t count = uint32_t(a[1]), grain = uint32_t(a[2]), fn = uint32_t(a[3]), user = uint32_t(a[4]);
            if (fn == 0) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            if (count > 0) RunSlices(s, fn, user, count, grain);
            r[0] = uint32_t(SHARD_OK);
            return true;
        });

        imports.AddManual("jobs.is_done", [&s](Instance&, const uint64_t* a, uint64_t* r) {
            WasmJobs* jobs = FindJobs(s);
            const uint64_t handle = a[1];
            auto it = jobs && handle ? jobs->jobs.find(handle) : JobMap::iterator{};
            if (!jobs || !handle || it == jobs->jobs.end()) { r[0] = uint32_t(SHARD_TRUE); return true; }
            if (it->second.state == Job::State::Queued) RunThrough(s, *jobs, handle);
            it = jobs->jobs.find(handle);
            r[0] = uint32_t(it == jobs->jobs.end() || it->second.state == Job::State::Done ? SHARD_TRUE : SHARD_FALSE);
            return true;
        });

        imports.AddManual("jobs.wait", [&s](Instance&, const uint64_t* a, uint64_t* r) {
            if (!CAbi::OnScriptThread()) { r[0] = uint32_t(SHARD_E_WRONG_THREAD); return true; }
            const uint64_t handle = a[1];
            if (!handle) { r[0] = uint32_t(SHARD_OK); return true; }
            WasmJobs* jobs = FindJobs(s);
            auto it = jobs ? jobs->jobs.find(handle) : JobMap::iterator{};
            if (!jobs || it == jobs->jobs.end() || it->second.released) { r[0] = uint32_t(SHARD_E_STALE_HANDLE); return true; }
            // A job that is running is one of the callers up the stack, or the job itself : it can not end while it waits
            if (it->second.state == Job::State::Running) { r[0] = uint32_t(SHARD_E_INVALID_ARG); return true; }
            if (it->second.state == Job::State::Queued) RunThrough(s, *jobs, handle);
            it = jobs->jobs.find(handle);
            if (it != jobs->jobs.end() && it->second.state == Job::State::Done) jobs->jobs.erase(it);
            r[0] = uint32_t(SHARD_OK);
            return true;
        });

        imports.AddManual("jobs.release", [&s](Instance&, const uint64_t* a, uint64_t* r) {
            const uint64_t handle = a[1];
            if (!handle) { r[0] = uint32_t(SHARD_OK); return true; }
            WasmJobs* jobs = FindJobs(s);
            auto it = jobs ? jobs->jobs.find(handle) : JobMap::iterator{};
            if (!jobs || it == jobs->jobs.end() || it->second.released) { r[0] = uint32_t(SHARD_E_STALE_HANDLE); return true; }
            if (it->second.state == Job::State::Done) jobs->jobs.erase(it);
            else it->second.released = true;
            r[0] = uint32_t(SHARD_OK);
            return true;
        });

        if (!api) {
            imports.AddManual("jobs.post_main", unsupported);
            return;
        }
        imports.AddManual("jobs.post_main", [&s, api](Instance&, const uint64_t* a, uint64_t* r) {
            if (a[1] == 0 || !SHARD_HAS(api, post_main)) { r[0] = uint32_t(a[1] == 0 ? SHARD_E_INVALID_ARG : SHARD_E_UNSUPPORTED); return true; }
            WasmJobs& jobs = JobsOf(s, api);
            if (jobs.posts.size() >= kMaxPosts) { r[0] = uint32_t(SHARD_E_OUT_OF_MEMORY); return true; }
            auto node = std::make_unique<PostNode>();
            node->owner = &jobs;
            node->sandbox = &s;
            node->fn = uint32_t(a[1]);
            node->user = uint32_t(a[2]);
            PostNode* raw = node.get();
            jobs.posts[raw] = std::move(node);
            const ShardResult result = api->post_main(s.ctx, &PostTrampoline, raw);
            if (result != SHARD_OK) Drop(jobs, raw);
            r[0] = uint32_t(result);
            return true;
        });
    }
}
