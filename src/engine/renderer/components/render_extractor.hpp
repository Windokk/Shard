#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
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

        using BillboardProvider = std::function<void(Worlds::World& world, int worldIndex, const RenderSnapshot& snapshot, std::vector<BillboardProxy>& out)>;

        using PoseOffsets = std::unordered_map<int, glm::mat4>;

        RenderExtractor();

        void SetPoseOffsets(const PoseOffsets* offsets) { m_PoseOffsets = offsets; }

        void SetRebaseDistance(double meters) { m_RebaseDistance = meters; }
        double GetRebaseDistance() const { return m_RebaseDistance; }

        const Core::WorldPosition& GetRenderOrigin() const { return m_RenderOrigin; }

        struct CameraOverride {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 target = glm::vec3(0.0f, 0.0f, -1.0f);
            float fovDegrees = 0.0f;
        };
        void SetCameraOverride(const std::optional<CameraOverride>& view) { std::lock_guard<std::mutex> lock(m_CameraOverrideMutex); m_CameraOverride = view; }
        std::optional<CameraOverride> GetCameraOverride() const { std::lock_guard<std::mutex> lock(m_CameraOverrideMutex); return m_CameraOverride; }

        void AddBillboardProvider(BillboardProvider provider) { m_BillboardProviders.push_back(std::move(provider)); }

        using SnapshotProvider = std::function<void(const RenderSnapshot& previous, RenderSnapshot& next)>;
        void AddSnapshotProvider(SnapshotProvider provider) { m_SnapshotProviders.push_back(std::move(provider)); }

        void Extract(IRenderContext& renderer, Worlds::WorldManager& worlds, CameraManager& cameras, Core::JobSystem* jobs, uint64_t frame);

        const Stats& GetLastStats() const { return m_Stats; }

        static constexpr size_t kParallelThreshold = 512;

    private:
        struct MeshSource {
            uint64_t key;
            int componentId;
            Objects::Components::Model* model;
        };

        const FrameToRender& SpaceOfCached(const RenderSnapshot& snapshot, Objects::Actor* actor);

        void ExtractMeshes(Worlds::WorldManager& worlds, const RenderSnapshot& previous, RenderSnapshot& next, Core::JobSystem* jobs);

        std::vector<MeshSource> m_Sources;
        std::vector<int> m_PreviousIndex;
        std::vector<BillboardProvider> m_BillboardProviders;
        std::vector<SnapshotProvider> m_SnapshotProviders;
        const PoseOffsets* m_PoseOffsets = nullptr;
        std::optional<CameraOverride> m_CameraOverride;
        mutable std::mutex m_CameraOverrideMutex;
        Core::WorldPosition m_RenderOrigin;
        double m_RebaseDistance = 4096.0;
        std::map<std::pair<const void*, Core::FrameId>, FrameToRender> m_SpaceCache;
        Stats m_Stats;
    };

    uint64_t RenderKeyOf(Worlds::WorldManager& worlds, const Worlds::World* world, int idInWorld);

    Objects::Components::ProbeVolume* FindProbeVolume(Worlds::WorldManager& worlds, uint64_t key);

    Objects::Components::ReflectionProbe* FindReflectionProbe(Worlds::WorldManager& worlds, uint64_t key);
}
