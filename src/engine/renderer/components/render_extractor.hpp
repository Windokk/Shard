#pragma once

#include <cstdint>
#include <vector>

#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Core {
    class JobSystem;
}

namespace Shard::Engine::Worlds {
    class World;
    class WorldManager;
}

namespace Shard::Engine::Objects::Components {
    class Model;
    class ProbeVolume;
}

namespace Shard::Engine::Rendering {

    class IRenderContext;
    class CameraManager;

    /// @brief The Extract stage : copies what the renderer needs out of the simulation into a RenderSnapshot.
    ///
    /// This is the one place where the renderer's data meets the world's : it reads the components of the loaded worlds
    /// (models, lights, the camera, the probe volume, the world settings) and writes plain proxies, then publishes the
    /// snapshot. Everything the renderer draws from that point comes from the snapshot, none of it reaches back into
    /// an actor or a component.
    ///
    /// Incremental : every proxy is compared with the one of the previous snapshot (same key) and flagged `changed`
    /// only if it differs, and the buffers of the snapshot are reused from one frame to the next, so a still scene costs
    /// a comparison per object and no allocation. The per-mesh work runs on the job system when there are enough meshes.
    class RenderExtractor {
    public:
        struct Stats {
            uint32_t meshes = 0;
            uint32_t meshesChanged = 0;
            uint32_t lights = 0;
            uint32_t probeVolumes = 0;
            bool parallel = false;
        };

        /// @brief Builds the next snapshot from `worlds` and publishes it to `renderer`.
        /// @param jobs may be null (serial)
        void Extract(IRenderContext& renderer, Worlds::WorldManager& worlds, CameraManager& cameras, Core::JobSystem* jobs, uint64_t frame);

        const Stats& GetLastStats() const { return m_Stats; }

        /// Meshes below this count are extracted on the calling thread : a job costs more than they do
        static constexpr size_t kParallelThreshold = 512;

    private:
        struct MeshSource {                             // a model component and the key its proxy will have
            uint64_t key;
            int componentId;
            Objects::Components::Model* model;
        };

        void ExtractMeshes(Worlds::WorldManager& worlds, const RenderSnapshot& previous, RenderSnapshot& next, Core::JobSystem* jobs);

        std::vector<MeshSource> m_Sources;              // scratch, kept between frames
        std::vector<int> m_PreviousIndex;
        Stats m_Stats;
    };

    /// @brief The key the proxy of the component `idInWorld` of `world` has in the snapshots (see ProbeVolumeProxy::volume.key)
    uint64_t RenderKeyOf(Worlds::WorldManager& worlds, const Worlds::World* world, int idInWorld);

    /// @brief The probe volume component a proxy key stands for, or null if it is gone
    Objects::Components::ProbeVolume* FindProbeVolume(Worlds::WorldManager& worlds, uint64_t key);
}
