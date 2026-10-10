#pragma once

#include <string>
#include <vector>
#include <exception>
#include <memory>
#include <atomic>
#include <functional>

#include "engine/assets/resources_manager.hpp"
#include "engine/core/jobs/job_system.hpp"

namespace Shard::Engine::Worlds{

    class AssetPrefetcher{

        public:

            AssetPrefetcher() = default;
            /// Waits for the decodes still running : they reference this object's jobs.
            ~AssetPrefetcher();
            AssetPrefetcher(const AssetPrefetcher&) = delete;
            AssetPrefetcher& operator=(const AssetPrefetcher&) = delete;

            void BeginLoad(const std::string& pathInProject);

            /// @brief Launches pending decodes (at most maxInFlight run at once) and applies the decoded
            /// assets (GPU upload, insertion in the cache) as they complete, spending at most
            /// `uploadBudgetMs` per call so a big world streams in over several frames instead of stalling
            /// one. A negative budget applies everything that is ready (loading-screen mode). At least one
            /// asset is applied per call when any is ready, so progress is guaranteed.
            /// @return true once every asset has been applied (or nothing was in progress)
            bool Pump(float uploadBudgetMs = kDefaultUploadBudgetMs);

            static constexpr float kDefaultUploadBudgetMs = 4.0f;

            bool IsInProgress() const { return state == State::Decoding; }

            // 0-1 (meaningless when not in progress)
            float GetProgress() const;

        private:

            enum class State { Idle, Decoding };

            /// What to decode comes from the resources manager (the owner kinds know what they reference, the
            /// kinds that can be decoded ahead of time say how) : this class knows no asset type.
            struct DecodeJob {
                /// What the worker thread writes and the main thread reads : heap allocated so its address stays
                /// valid while the vector of jobs moves, and shared with the running job.
                struct Result {
                    std::function<void()> finish;
                    std::exception_ptr error;       // a decoder that throws must not take a worker thread down
                    std::atomic<bool> done{false};
                };

                Core::Resources::PrefetchTask task;
                bool started = false;
                bool applied = false;
                std::unique_ptr<Result> result = std::make_unique<Result>();
            };

            static bool IsDecoded(DecodeJob& job);
            void StartDecode(DecodeJob& job);
            static void Apply(DecodeJob& job);

            /// Blocks until every started decode is finished (the calling thread executes jobs meanwhile).
            void WaitForDecodes();

            /// Every decode of the current load, so one Wait covers them all.
            Core::JobGroup decodeGroup;

            State state = State::Idle;

            std::vector<DecodeJob> jobs;

            std::atomic<int> completedCount{0};
            int totalCount = 0;

            // Worker threads decoding at once. Every asset used to get its own thread, which on a
            // world with a few hundred textures oversubscribes the CPU and the memory bus.
            int maxInFlight = 2;
    };
}
