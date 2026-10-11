#include "render_extractor.hpp"

#include <algorithm>
#include <atomic>

#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/parallel_for.hpp"
#include "engine/core/diagnostics/profiler.hpp"

#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/renderer/components/skybox.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/resources/framebuffer/framebuffer.hpp"
#include "engine/renderer/rhi/resources/texture/cubemap/envmap.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"
#include "engine/world/components/transform.hpp"

namespace Shard::Engine::Rendering {

    using namespace Objects::Components;

    namespace {

        /// Proxies of different worlds must not collide : the world's index in the high bits, the id in the world below
        uint64_t MakeKey(int worldIndex, int idInWorld)
        {
            return (uint64_t(uint32_t(worldIndex)) << 48) | uint64_t(uint32_t(idInWorld));
        }

        template <typename Proxy>
        void SortByKey(std::vector<Proxy>& proxies)
        {
            std::sort(proxies.begin(), proxies.end(), [](const Proxy& a, const Proxy& b) { return a.key < b.key; });
        }

        LightProxy MakeLightProxy(uint64_t key, Light& light)
        {
            const LightData data = light.GetData();

            LightProxy proxy;
            proxy.key = key;
            proxy.objectId = light.parent ? light.parent->GetID().GetAsInt() : -1;
            proxy.type = static_cast<LightProxyType>(data.type);
            // A light follows its actor : where it stands and where it points come from the transform
            if (light.parent && light.parent->transform) {
                proxy.position = light.parent->transform->GetWorldPosition();
                proxy.direction = glm::normalize(light.parent->transform->GetWorldForward());
            } else {
                proxy.position = glm::vec3(data.position);
                proxy.direction = glm::vec3(data.direction);
            }
            proxy.color = glm::vec3(data.color);
            proxy.intensity = data.intensity;
            proxy.radius = data.radius;
            proxy.innerCutoff = data.innerCutoff;
            proxy.outerCutoff = data.outerCutoff;
            proxy.castShadows = data.castShadow != 0;
            return proxy;
        }

        ProbeVolumeProxy MakeProbeVolumeProxy(uint64_t key, ProbeVolume& volume)
        {
            ProbeVolumeProxy proxy;
            proxy.volume.key = key;
            proxy.volume.objectId = volume.parent ? volume.parent->GetID().GetAsInt() : -1;
            proxy.volume.center = volume.parent && volume.parent->transform ? volume.parent->transform->GetWorldPosition() : glm::vec3(0.0f);
            proxy.volume.halfExtent = volume.halfExtent;
            proxy.volume.active = volume.Active();
            proxy.probeCounts = volume.probeCounts;
            proxy.raysPerProbe = volume.raysPerProbe;
            proxy.probeUpdateStride = volume.probeUpdateStride;
            proxy.indirectIntensity = volume.indirectIntensity;
            proxy.enableRelocation = volume.enableRelocation;
            proxy.bakedData = volume.bakedData;
            return proxy;
        }

        EnvironmentProxy MakeEnvironmentProxy(Worlds::World& world)
        {
            EnvironmentProxy env;
            env.ambientIntensity = world.ambientIntensity;
            env.ssaoEnabled = world.ssaoEnabled;
            env.ssaoRadius = world.ssaoRadius;
            env.ssaoBias = world.ssaoBias;
            env.ssaoIntensity = world.ssaoIntensity;
            env.ssaoPower = world.ssaoPower;

            const auto& skybox = world.Ext<RenderWorldData>().skybox;
            if (skybox) {
                env.skybox = skybox->GetEnvMap();
                env.skyboxMaterial = skybox->GetMaterial();
                env.skyboxObjectId = skybox->GetID().GetAsInt();
            }
            return env;
        }

        void ExtractLightsAndVolumes(Worlds::World& world, int worldIndex, RenderSnapshot& next)
        {
            RenderWorldData& data = world.Ext<RenderWorldData>();

            for (auto& [id, light] : data.lightComps) {
                if (light && light->Active())
                    next.lights.push_back(MakeLightProxy(MakeKey(worldIndex, id), *light));
            }

            for (auto& [id, volume] : data.probeVolumes) {
                if (volume && volume->Active())
                    next.probeVolumes.push_back(MakeProbeVolumeProxy(MakeKey(worldIndex, id), *volume));
            }
        }

        CameraProxy MakeCameraProxy(Camera& camera)
        {
            CameraProxy proxy;
            if (!camera.parent || !camera.parent->transform)
                return proxy;

            auto transform = camera.parent->transform;

            proxy.valid = true;
            proxy.key = uint64_t(uint32_t(camera.parent->GetID().GetAsInt()));
            proxy.objectId = camera.parent->GetID().GetAsInt();
            proxy.view = camera.GetView();
            proxy.projection = camera.GetProjection();
            proxy.viewProjection = camera.GetMatrix();
            proxy.position = transform->GetWorldPosition();
            proxy.forward = transform->GetWorldForward();
            proxy.right = transform->GetWorldRight();
            proxy.up = transform->GetWorldUp();
            proxy.nearPlane = camera.nearPlane;
            proxy.farPlane = camera.farPlane;
            proxy.fovDegrees = camera.fov;
            proxy.orthoSize = camera.orthoSize;
            proxy.width = int(camera.GetSize().x);
            proxy.height = int(camera.GetSize().y);
            proxy.orthographic = camera.orthographic;
            proxy.frustumCulling = camera.frustumCulling;
            return proxy;
        }

        bool Differs(const CameraProxy& a, const CameraProxy& b)
        {
            return a.valid != b.valid || a.key != b.key || a.viewProjection != b.viewProjection || a.view != b.view ||
                   a.projection != b.projection || a.position != b.position || a.nearPlane != b.nearPlane ||
                   a.farPlane != b.farPlane || a.fovDegrees != b.fovDegrees || a.orthoSize != b.orthoSize ||
                   a.width != b.width || a.height != b.height || a.orthographic != b.orthographic ||
                   a.frustumCulling != b.frustumCulling;
        }

        bool Differs(const LightProxy& a, const LightProxy& b)
        {
            return a.type != b.type || a.position != b.position || a.direction != b.direction || a.color != b.color ||
                   a.intensity != b.intensity || a.radius != b.radius || a.innerCutoff != b.innerCutoff ||
                   a.outerCutoff != b.outerCutoff || a.castShadows != b.castShadows || a.objectId != b.objectId;
        }

        bool Differs(const ProbeVolumeProxy& a, const ProbeVolumeProxy& b)
        {
            return a.volume.center != b.volume.center || a.volume.halfExtent != b.volume.halfExtent ||
                   a.volume.active != b.volume.active || a.volume.objectId != b.volume.objectId ||
                   a.probeCounts != b.probeCounts || a.raysPerProbe != b.raysPerProbe ||
                   a.probeUpdateStride != b.probeUpdateStride || a.indirectIntensity != b.indirectIntensity ||
                   a.enableRelocation != b.enableRelocation || a.bakedData != b.bakedData;
        }
    }

    void RenderExtractor::ExtractMeshes(Worlds::WorldManager& worlds, const RenderSnapshot& previous, RenderSnapshot& next, Core::JobSystem* jobs)
    {
        m_Sources.clear();

        for (int w = 0; w < worlds.GetLoadedWorldCount(); w++) {
            Worlds::World* world = worlds.GetWorldAt(w);
            if (!world)
                continue;

            for (auto& [id, model] : world->Ext<RenderWorldData>().models) {
                if (model && model->Active() && model->MeshRef() && !model->MaterialList().empty() && model->parent && model->parent->transform)
                    m_Sources.push_back({MakeKey(w, id), id, model.get()});
            }
        }

        // The same order every frame (the maps iterate in whatever order their buckets are in)
        std::sort(m_Sources.begin(), m_Sources.end(), [](const MeshSource& a, const MeshSource& b) { return a.key < b.key; });

        const size_t count = m_Sources.size();
        next.meshes.resize(count);

        // Matches each source with the proxy it had last frame : both lists are sorted, so one walk does it
        m_PreviousIndex.assign(count, -1);
        {
            size_t p = 0;
            for (size_t i = 0; i < count; i++) {
                while (p < previous.meshes.size() && previous.meshes[p].key < m_Sources[i].key)
                    p++;
                if (p < previous.meshes.size() && previous.meshes[p].key == m_Sources[i].key)
                    m_PreviousIndex[i] = int(p);
            }
        }

        // World matrices are cached lazily by the transforms, and a parent's is shared by its children : settle them
        // here, on this thread, so that the parallel part below only reads.
        for (size_t i = 0; i < count; i++)
            next.meshes[i].worldMatrix = m_Sources[i].model->parent->transform->GetWorldMatrix();

        std::atomic<uint32_t> changedCount{0};

        auto fill = [&](size_t begin, size_t end) {
            uint32_t changedHere = 0;
            for (size_t i = begin; i < end; i++) {
                const MeshSource& source = m_Sources[i];
                MeshProxy& proxy = next.meshes[i];

                proxy.key = source.key;
                proxy.objectId = source.model->parent->GetID().GetAsInt();
                proxy.componentId = source.componentId;
                proxy.mesh = source.model->MeshRef();
                proxy.materials = source.model->MaterialList();
                proxy.extraPasses = source.model->ExtraPasses();

                const int prev = m_PreviousIndex[i];
                if (prev < 0) {
                    proxy.changed = true;
                } else {
                    const MeshProxy& old = previous.meshes[prev];
                    proxy.changed = old.worldMatrix != proxy.worldMatrix || old.mesh != proxy.mesh ||
                                    old.materials != proxy.materials || old.objectId != proxy.objectId ||
                                    old.extraPasses != proxy.extraPasses;
                }
                if (proxy.changed)
                    changedHere++;
            }
            changedCount.fetch_add(changedHere, std::memory_order_relaxed);
        };

        m_Stats.parallel = jobs && count >= kParallelThreshold;
        if (m_Stats.parallel)
            Core::ParallelForRange(*jobs, 0, count, 0, fill);
        else
            fill(0, count);

        m_Stats.meshes = uint32_t(count);
        m_Stats.meshesChanged = changedCount.load(std::memory_order_relaxed);
    }

    void RenderExtractor::Extract(IRenderContext& renderer, Worlds::WorldManager& worlds, CameraManager& cameras, Core::JobSystem* jobs, uint64_t frame)
    {
        RenderSnapshotBuffer& buffer = renderer.GetSnapshotBuffer();
        const RenderSnapshot& previous = buffer.Latest();
        RenderSnapshot& next = buffer.BeginWrite();

        next.Reset();
        next.frame = frame;

        if (auto viewport = renderer.GetViewportFramebuffer()) {
            next.viewportWidth = int(viewport->GetWidth());
            next.viewportHeight = int(viewport->GetHeight());
        }

        // The camera : its matrices are brought up to date, then copied
        {
            SHARD_PROFILE_RENDER_SUB_SCOPE(Debugging::RenderSubSample::CameraUpdate);
            cameras.Tick();
        }

        if (auto camera = cameras.GetActiveCamera()) {
            next.camera = MakeCameraProxy(*camera);
            next.camera.changed = Differs(next.camera, previous.camera);
        }

        ExtractMeshes(worlds, previous, next, jobs);

        for (int w = 0; w < worlds.GetLoadedWorldCount(); w++) {
            if (Worlds::World* world = worlds.GetWorldAt(w))
                ExtractLightsAndVolumes(*world, w, next);
        }
        SortByKey(next.lights);
        std::sort(next.probeVolumes.begin(), next.probeVolumes.end(),
                  [](const ProbeVolumeProxy& a, const ProbeVolumeProxy& b) { return a.volume.key < b.volume.key; });

        if (worlds.GetLoadedWorldCount() > 0) {
            if (Worlds::World* world = worlds.GetWorldAt(0))
                next.environment = MakeEnvironmentProxy(*world);
        }

        for (LightProxy& light : next.lights) {
            const LightProxy* old = RenderSnapshot::Find(previous.lights, light.key);
            light.changed = !old || Differs(light, *old);
        }
        for (ProbeVolumeProxy& volume : next.probeVolumes) {
            const ProbeVolumeProxy* old = nullptr;
            for (const ProbeVolumeProxy& candidate : previous.probeVolumes)
                if (candidate.volume.key == volume.volume.key)
                    old = &candidate;
            volume.volume.changed = !old || Differs(volume, *old);
        }

        m_Stats.lights = uint32_t(next.lights.size());
        m_Stats.probeVolumes = uint32_t(next.probeVolumes.size());

        buffer.Publish();
    }

    uint64_t RenderKeyOf(Worlds::WorldManager& worlds, const Worlds::World* world, int idInWorld)
    {
        for (int w = 0; w < worlds.GetLoadedWorldCount(); w++) {
            if (worlds.GetWorldAt(w) == world)
                return MakeKey(w, idInWorld);
        }
        return MakeKey(0, idInWorld);
    }

    Objects::Components::ProbeVolume* FindProbeVolume(Worlds::WorldManager& worlds, uint64_t key)
    {
        const int worldIndex = int(key >> 48);
        const int idInWorld = int(uint32_t(key & 0xFFFFFFFFull));

        if (worldIndex >= worlds.GetLoadedWorldCount())
            return nullptr;

        Worlds::World* world = worlds.GetWorldAt(worldIndex);
        if (!world)
            return nullptr;

        auto& volumes = world->Ext<RenderWorldData>().probeVolumes;
        auto it = volumes.find(idInWorld);
        return it != volumes.end() ? it->second.get() : nullptr;
    }
}
