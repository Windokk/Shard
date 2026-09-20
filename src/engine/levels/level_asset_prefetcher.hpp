#pragma once

#include <string>
#include <vector>
#include <future>
#include <atomic>

#include "engine/filesystem/filesystem.hpp"
#include "engine/rendering/texture/texture.hpp"
#include "engine/rendering/mesh/mesh.hpp"
#include "engine/rendering/lighting/probe_bake.hpp"

namespace Shard::Engine::Levels{

    class AssetPrefetcher{

        public:

            void BeginLoad(const std::string& pathInProject);

            /// @brief Launches pending decodes (at most maxInFlight run at once) and uploads the decoded
            /// assets to the GPU/cache as they complete, spending at most `uploadBudgetMs` per call so a
            /// big level streams in over several frames instead of stalling one. A negative budget
            /// uploads everything that is ready (loading-screen mode). At least one asset is uploaded per
            /// call when any is ready, so progress is guaranteed.
            /// @return true once every asset has been applied (or nothing was in progress)
            bool Pump(float uploadBudgetMs = kDefaultUploadBudgetMs);

            static constexpr float kDefaultUploadBudgetMs = 4.0f;

            bool IsInProgress() const { return state == State::Decoding; }

            // 0-1 (meaningless when not in progress)
            float GetProgress() const;

        private:

            enum class State { Idle, Decoding };
            enum class DecodeKind { Texture, Mesh, ProbeBake, Sound };

            struct DecodeJob {
                DecodeKind kind;
                std::string pathInProject;
                Filesystem::Path path;
                bool started = false;
                bool applied = false;
                std::future<Rendering::TextureDecodeResult> textureFuture;
                std::future<Rendering::MeshCPUData> meshFuture;
                std::future<std::shared_ptr<Rendering::ProbeBakeData>> probeBakeFuture;
                std::future<std::string> soundFuture;
            };

            static bool IsDecoded(DecodeJob& job);
            static void StartDecode(DecodeJob& job);
            static void Apply(DecodeJob& job);

            State state = State::Idle;

            std::vector<DecodeJob> jobs;

            std::atomic<int> completedCount{0};
            int totalCount = 0;

            // Worker threads decoding at once. Every asset used to get its own thread, which on a
            // level with a few hundred textures oversubscribes the CPU and the memory bus.
            int maxInFlight = 2;
    };
}
