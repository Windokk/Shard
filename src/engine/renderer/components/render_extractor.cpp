#include "render_extractor.hpp"

#include <algorithm>
#include <atomic>

#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/parallel_for.hpp"
#include "engine/core/diagnostics/profiler.hpp"

#include "engine/renderer/components/billboard_component.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/render_world_data.hpp"
#include "engine/renderer/components/skybox.hpp"
#include "engine/renderer/features/billboards/billboard_atlas.hpp"
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

        /// The correction of the nearest actor, from `actor` up its parents, that has one (null : the actor is drawn where its transform says)
        const glm::mat4* FindPoseOffset(const RenderExtractor::PoseOffsets* offsets, Objects::Actor* actor)
        {
            if (!offsets || offsets->empty() || !actor)
                return nullptr;

            std::shared_ptr<Objects::WorldObject> node = actor->AsShared<Objects::WorldObject>();
            while (node) {
                if (dynamic_cast<Objects::Actor*>(node.get())) {
                    const auto it = offsets->find(node->GetID().GetAsInt());
                    if (it != offsets->end())
                        return &it->second;
                }
                node = node->GetParent();
            }
            return nullptr;
        }

        LightProxy MakeLightProxy(uint64_t key, Light& light, const glm::mat4* poseOffset)
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
            if (poseOffset) {
                proxy.position = glm::vec3(*poseOffset * glm::vec4(proxy.position, 1.0f));
                proxy.direction = glm::normalize(glm::mat3(*poseOffset) * proxy.direction);
            }
            proxy.color = glm::vec3(data.color);
            proxy.intensity = data.intensity;
            proxy.radius = data.radius;
            proxy.innerCutoff = data.innerCutoff;
            proxy.outerCutoff = data.outerCutoff;
            proxy.castShadows = data.castShadow != 0;
            proxy.sourceSize = data.sourceSize;
            proxy.bakeMode = data.bakeMode;
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

        // ---- Billboards : the built-in providers. One per kind of component, each only reads its own components.

        constexpr float kIconSize = 40.0f;      // pixels
        constexpr uint8_t kIconFlags = BillboardFlag::ScreenSpaceSize | BillboardFlag::DepthTest | BillboardFlag::EditorOnly | BillboardFlag::Pickable;

        BillboardProxy MakeIcon(uint64_t key, Objects::Actor* actor, BuiltinSprite sprite, const glm::vec4& tint)
        {
            BillboardProxy proxy;
            proxy.key = key;
            proxy.objectId = actor ? actor->GetID().GetAsInt() : -1;
            if (actor && actor->transform)
                proxy.position = glm::dvec3(actor->transform->GetWorldPosition());
            proxy.size = kIconSize;
            proxy.sprite = BillboardAtlas::Get().GetBuiltin(sprite);
            proxy.tint = tint;
            proxy.facing = BillboardProxyFacing::Camera;
            proxy.flags = kIconFlags;
            return proxy;
        }

        void LightBillboards(Worlds::World& world, int worldIndex, const RenderSnapshot&, std::vector<BillboardProxy>& out)
        {
            for (auto& [id, light] : world.Ext<RenderWorldData>().lightComps) {
                if (!light || !light->Active() || !light->parent)
                    continue;
                // The icon takes the light colour, kept bright enough to read on any background
                const glm::vec3 color = glm::mix(glm::vec3(light->color), glm::vec3(1.0f), 0.5f);
                out.push_back(MakeIcon(MakeKey(worldIndex, id), light->parent.get(), BuiltinSprite::Light, glm::vec4(color, 1.0f)));
            }
        }

        void CameraBillboards(Worlds::World& world, int worldIndex, const RenderSnapshot& snapshot, std::vector<BillboardProxy>& out)
        {
            for (auto& [id, camera] : world.Ext<RenderWorldData>().cameras) {
                if (!camera || !camera->Active() || !camera->parent)
                    continue;
                // The camera the frame is seen from has no icon to show : it would be drawn on the lens
                if (snapshot.camera.valid && snapshot.camera.objectId == camera->parent->GetID().GetAsInt())
                    continue;
                out.push_back(MakeIcon(MakeKey(worldIndex, id), camera->parent.get(), BuiltinSprite::Camera, glm::vec4(1.0f)));
            }
        }

        void ProbeVolumeBillboards(Worlds::World& world, int worldIndex, const RenderSnapshot&, std::vector<BillboardProxy>& out)
        {
            for (auto& [id, volume] : world.Ext<RenderWorldData>().probeVolumes) {
                if (!volume || !volume->Active() || !volume->parent)
                    continue;
                out.push_back(MakeIcon(MakeKey(worldIndex, id), volume->parent.get(), BuiltinSprite::ProbeVolume, glm::vec4(1.0f)));
            }
        }

        void ComponentBillboards(Worlds::World& world, int worldIndex, const RenderSnapshot&, std::vector<BillboardProxy>& out)
        {
            for (auto& [id, billboard] : world.Ext<RenderWorldData>().billboards) {
                if (!billboard || !billboard->Active() || !billboard->parent)
                    continue;

                BillboardProxy proxy;
                proxy.key = MakeKey(worldIndex, id);
                proxy.objectId = billboard->parent->GetID().GetAsInt();
                proxy.position = billboard->GetPosition();
                proxy.size = billboard->size;
                proxy.sprite = billboard->GetAtlasSprite();
                proxy.tint = glm::vec4(billboard->tint);
                proxy.facing = static_cast<BillboardProxyFacing>(billboard->facingType);
                proxy.flags = 0;
                if (billboard->ScreenSpaceSize) proxy.flags |= BillboardFlag::ScreenSpaceSize;
                if (billboard->DepthTest)       proxy.flags |= BillboardFlag::DepthTest;
                if (billboard->EditorOnly)      proxy.flags |= BillboardFlag::EditorOnly;
                if (billboard->Pickable)        proxy.flags |= BillboardFlag::Pickable;
                out.push_back(proxy);
            }
        }

        EnvironmentProxy MakeEnvironmentProxy(Worlds::World& world)
        {
            EnvironmentProxy env;
            env.ambientColor = world.ambientColor;
            env.ssaoEnabled = world.ssaoEnabled;
            env.ssaoRadius = world.ssaoRadius;
            env.ssaoBias = world.ssaoBias;
            env.ssaoIntensity = world.ssaoIntensity;
            env.ssaoPower = world.ssaoPower;

            env.lightmapData = world.Ext<RenderWorldData>().lightmapData;
            env.display = world.Ext<RenderWorldData>().display;

            const auto& skybox = world.Ext<RenderWorldData>().skybox;
            if (skybox) {
                env.skybox = skybox->GetEnvMap();
                env.skyboxMaterial = skybox->GetMaterial();
                env.skyboxObjectId = skybox->GetID().GetAsInt();
            }
            return env;
        }

        void ExtractLightsAndVolumes(Worlds::World& world, int worldIndex, RenderSnapshot& next, const RenderExtractor::PoseOffsets* poseOffsets)
        {
            RenderWorldData& data = world.Ext<RenderWorldData>();

            for (auto& [id, light] : data.lightComps) {
                if (light && light->Active())
                    next.lights.push_back(MakeLightProxy(MakeKey(worldIndex, id), *light, FindPoseOffset(poseOffsets, light->parent.get())));
            }

            for (auto& [id, volume] : data.probeVolumes) {
                if (volume && volume->Active())
                    next.probeVolumes.push_back(MakeProbeVolumeProxy(MakeKey(worldIndex, id), *volume));
            }
        }

        CameraProxy MakeCameraProxy(Camera& camera, const RenderExtractor::PoseOffsets* poseOffsets)
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

            // A camera riding on a body is drawn from where the body is drawn
            if (const glm::mat4* offset = FindPoseOffset(poseOffsets, camera.parent.get())) {
                proxy.view = proxy.view * glm::inverse(*offset);
                proxy.viewProjection = proxy.projection * proxy.view;
                proxy.position = glm::vec3(*offset * glm::vec4(proxy.position, 1.0f));
                proxy.forward = glm::normalize(glm::mat3(*offset) * proxy.forward);
                proxy.right = glm::normalize(glm::mat3(*offset) * proxy.right);
                proxy.up = glm::normalize(glm::mat3(*offset) * proxy.up);
            }
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
                   a.outerCutoff != b.outerCutoff || a.castShadows != b.castShadows || a.objectId != b.objectId ||
                   a.sourceSize != b.sourceSize || a.bakeMode != b.bakeMode;
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

    RenderExtractor::RenderExtractor()
    {
        m_BillboardProviders.push_back(LightBillboards);
        m_BillboardProviders.push_back(CameraBillboards);
        m_BillboardProviders.push_back(ProbeVolumeBillboards);
        m_BillboardProviders.push_back(ComponentBillboards);
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
        for (size_t i = 0; i < count; i++) {
            glm::mat4 world = m_Sources[i].model->parent->transform->GetWorldMatrix();
            // An actor on a moving body is drawn between the body's last two simulated poses, not at the last one
            if (const glm::mat4* offset = FindPoseOffset(m_PoseOffsets, m_Sources[i].model->parent.get()))
                world = *offset * world;
            next.meshes[i].worldMatrix = world;
        }

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
                proxy.staticLighting = source.model->staticLighting;
                proxy.lightmapResolution = source.model->lightmapResolution;
                // A model that is no longer static keeps the lightmap it was baked with in the world file, but does not use it : it moves
                proxy.lightmapPage = source.model->staticLighting ? source.model->lightmapPage : -1;
                proxy.lightmapScaleOffset = source.model->lightmapScaleOffset;

                const int prev = m_PreviousIndex[i];
                if (prev < 0) {
                    proxy.changed = true;
                } else {
                    const MeshProxy& old = previous.meshes[prev];
                    proxy.changed = old.worldMatrix != proxy.worldMatrix || old.mesh != proxy.mesh ||
                                    old.materials != proxy.materials || old.objectId != proxy.objectId ||
                                    old.extraPasses != proxy.extraPasses || old.staticLighting != proxy.staticLighting ||
                                    old.lightmapResolution != proxy.lightmapResolution || old.lightmapPage != proxy.lightmapPage ||
                                    old.lightmapScaleOffset != proxy.lightmapScaleOffset;
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
            next.camera = MakeCameraProxy(*camera, m_PoseOffsets);
            next.camera.changed = Differs(next.camera, previous.camera);
        }

        ExtractMeshes(worlds, previous, next, jobs);

        for (int w = 0; w < worlds.GetLoadedWorldCount(); w++) {
            if (Worlds::World* world = worlds.GetWorldAt(w))
                ExtractLightsAndVolumes(*world, w, next, m_PoseOffsets);
        }
        SortByKey(next.lights);

        for (int w = 0; w < worlds.GetLoadedWorldCount(); w++) {
            if (Worlds::World* world = worlds.GetWorldAt(w)) {
                for (const BillboardProvider& provider : m_BillboardProviders)
                    provider(*world, w, next, next.billboards);
            }
        }
        SortByKey(next.billboards);
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
        m_Stats.billboards = uint32_t(next.billboards.size());

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
