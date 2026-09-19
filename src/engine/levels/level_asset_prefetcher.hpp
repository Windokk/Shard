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

            bool Pump();

            bool IsInProgress() const { return state == State::Decoding; }

            // 0-1 (meaningless when not in progress)
            float GetProgress() const;

        private:

            enum class State { Idle, Decoding };
            enum class DecodeKind { Texture, Mesh, ProbeBake };

            struct DecodeJob {
                DecodeKind kind;
                std::string pathInProject;
                std::future<Rendering::TextureDecodeResult> textureFuture;
                std::future<Rendering::MeshCPUData> meshFuture;
                std::future<std::shared_ptr<Rendering::ProbeBakeData>> probeBakeFuture;
            };

            void ApplyAll();

            State state = State::Idle;

            std::vector<DecodeJob> jobs;

            std::atomic<int> completedCount{0};
            int totalCount = 0;
    };
}
