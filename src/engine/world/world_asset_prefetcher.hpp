#pragma once

#include <chrono>
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
            ~AssetPrefetcher();
            AssetPrefetcher(const AssetPrefetcher&) = delete;
            AssetPrefetcher& operator=(const AssetPrefetcher&) = delete;

            void BeginLoad(const std::string& pathInProject);

            // Spends at most uploadBudgetMs per call so a big world streams in over several frames. Negative applies everything ready (loading screen).
            // At least one asset is applied per call when any is ready, so it always progresses.
            bool Pump(float uploadBudgetMs = kDefaultUploadBudgetMs);

            static constexpr float kDefaultUploadBudgetMs = 4.0f;

            bool IsInProgress() const { return state == State::Decoding; }

            float GetProgress() const;

        private:

            enum class State { Idle, Decoding };

            struct DecodeJob {
                // Heap allocated so its address survives the jobs vector moving, and shared with the running job.
                struct Result {
                    std::function<void()> finish;
                    std::exception_ptr error;
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

            void WaitForDecodes();

            Core::JobGroup decodeGroup;

            State state = State::Idle;

            std::vector<DecodeJob> jobs;

            std::atomic<int> completedCount{0};
            std::chrono::steady_clock::time_point loadStart;
            int totalCount = 0;

            int maxInFlight = 2;
    };
}
