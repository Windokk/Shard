#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/core/math/large_world.hpp"
#include "engine/renderer/components/render_space.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"

namespace Shard::Engine::Core {
    class JobSystem;
}

namespace Shard::Engine::Worlds {
    class World;
    class WorldManager;
}

namespace Shard::Engine::Objects {
    class Actor;
}

namespace Shard::Engine::Objects::Components {
    class Model;
    class ProbeVolume;
    class ReflectionProbe;
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
            uint32_t billboards = 0;
            bool parallel = false;
        };

        /// @brief Turns the components of one kind in a world into billboards (appends to `out`, with keys made like the other proxies').
        /// `snapshot` is the one being built : its camera is already set. This is where a type gets its editor icon or its handle
        /// (lights, cameras, probe volumes and Billboard components are built in ; a skeleton's bones or an IK rig add theirs).
        using BillboardProvider = std::function<void(Worlds::World& world, int worldIndex, const RenderSnapshot& snapshot, std::vector<BillboardProxy>& out)>;

        /// Where the actors that move on their own (physics bodies) are drawn this frame, as a world-space correction applied to the
        /// world matrix their transform holds : keyed by actor id. The meshes, lights and camera of such an actor, or of one of its
        /// descendants, take the nearest ancestor's correction. The renderer knows nothing of what produced them.
        using PoseOffsets = std::unordered_map<int, glm::mat4>;

        RenderExtractor();

        /// @brief The corrections to apply to the next Extract() calls (null or empty : none). Not copied : it must outlive them.
        void SetPoseOffsets(const PoseOffsets* offsets) { m_PoseOffsets = offsets; }

        /// @brief How far the camera may get from the render origin before the origin is moved to it (meters). Render space is
        /// float, relative to that origin, so this bounds the precision loss : at 4096 m a float still resolves half a millimetre.
        /// 0 : the origin is always exactly the camera (strict camera-relative rendering).
        void SetRebaseDistance(double meters) { m_RebaseDistance = meters; }
        double GetRebaseDistance() const { return m_RebaseDistance; }

        /// @brief The point of the root frame the last snapshot is relative to
        const Core::WorldPosition& GetRenderOrigin() const { return m_RenderOrigin; }

        void AddBillboardProvider(BillboardProvider provider) { m_BillboardProviders.push_back(std::move(provider)); }

        /// @brief What the plugins add to a snapshot that is not made of components : the meshes, sprites and overlays of shard.render. Called last, on
        /// the Extract thread, with the snapshot being built (its camera and render origin are set) and the previous one. The provider appends its
        /// meshes after the sorted ones (keys of the world index 0xFFFE) and the batches and overlays in the order they are drawn.
        using SnapshotProvider = std::function<void(const RenderSnapshot& previous, RenderSnapshot& next)>;
        void AddSnapshotProvider(SnapshotProvider provider) { m_SnapshotProviders.push_back(std::move(provider)); }

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

        /// The placement of the actor's frame in render space, kept for the frame being extracted (meshes share a few frames)
        const FrameToRender& SpaceOfCached(const RenderSnapshot& snapshot, Objects::Actor* actor);

        void ExtractMeshes(Worlds::WorldManager& worlds, const RenderSnapshot& previous, RenderSnapshot& next, Core::JobSystem* jobs);

        std::vector<MeshSource> m_Sources;              // scratch, kept between frames
        std::vector<int> m_PreviousIndex;
        std::vector<BillboardProvider> m_BillboardProviders;
        std::vector<SnapshotProvider> m_SnapshotProviders;
        const PoseOffsets* m_PoseOffsets = nullptr;
        Core::WorldPosition m_RenderOrigin;
        double m_RebaseDistance = 4096.0;
        std::map<std::pair<const void*, Core::FrameId>, FrameToRender> m_SpaceCache;
        Stats m_Stats;
    };

    /// @brief The key the proxy of the component `idInWorld` of `world` has in the snapshots (see ProbeVolumeProxy::volume.key)
    uint64_t RenderKeyOf(Worlds::WorldManager& worlds, const Worlds::World* world, int idInWorld);

    /// @brief The probe volume component a proxy key stands for, or null if it is gone
    Objects::Components::ProbeVolume* FindProbeVolume(Worlds::WorldManager& worlds, uint64_t key);

    /// @brief The reflection probe component a proxy key stands for, or null if it is gone
    Objects::Components::ReflectionProbe* FindReflectionProbe(Worlds::WorldManager& worlds, uint64_t key);
}
