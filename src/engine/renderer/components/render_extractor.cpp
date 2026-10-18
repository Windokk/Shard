#include "render_extractor.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

#include "engine/core/jobs/job_system.hpp"
#include "engine/core/jobs/parallel_for.hpp"
#include "engine/core/diagnostics/profiler.hpp"

#include "engine/renderer/components/billboard_component.hpp"
#include "engine/renderer/components/camera.hpp"
#include "engine/renderer/components/camera_manager.hpp"
#include "engine/renderer/components/light_component.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/reflection_probe.hpp"
#include "engine/renderer/components/render_space.hpp"
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

        /// How the frame `actor` lives in is brought into the snapshot's render space (the identity for a scene that stays near the
        /// origin, without touching the frame tree at all)
        FrameToRender SpaceOf(const RenderSnapshot& snapshot, Objects::Actor* actor)
        {
            const Core::ReferenceFrameTree* tree = actor && actor->transform ? actor->transform->GetFrameTree() : nullptr;
            if (!tree)
                return FrameToRender();
            if (tree->Count() == 1 && snapshot.renderOrigin == Core::WorldPosition())
                return FrameToRender();
            return MakeFrameToRender(*tree, actor->transform->GetFrame(), snapshot.renderOrigin);
        }

        LightProxy MakeLightProxy(uint64_t key, Light& light, const glm::mat4* poseOffset, const FrameToRender& space)
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
            proxy.position = space.ApplyToPoint(proxy.position);
            proxy.direction = space.ApplyToDirection(proxy.direction);
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

        ProbeVolumeProxy MakeProbeVolumeProxy(uint64_t key, ProbeVolume& volume, const FrameToRender& space)
        {
            ProbeVolumeProxy proxy;
            proxy.volume.key = key;
            proxy.volume.objectId = volume.parent ? volume.parent->GetID().GetAsInt() : -1;
            proxy.volume.center = space.ApplyToPoint(volume.parent && volume.parent->transform ? volume.parent->transform->GetWorldPosition() : glm::vec3(0.0f));
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

        ReflectionProbeProxy MakeReflectionProbeProxy(uint64_t key, ReflectionProbe& probe, const FrameToRender& space)
        {
            ReflectionProbeProxy proxy;
            proxy.volume.key = key;
            proxy.volume.objectId = probe.parent ? probe.parent->GetID().GetAsInt() : -1;
            proxy.volume.center = space.ApplyToPoint(probe.parent && probe.parent->transform ? probe.parent->transform->GetWorldPosition() : glm::vec3(0.0f));
            proxy.volume.halfExtent = probe.halfExtent;
            proxy.volume.active = probe.Active();
            proxy.shape = probe.shape;
            proxy.intensity = probe.intensity;
            proxy.blendDistance = probe.blendDistance;
            proxy.boxProjection = probe.boxProjection;
            proxy.environment = probe.GetEnvironment();     // loaded by ReflectionProbe::TickAll, never here
            return proxy;
        }

        // ---- Billboards : the built-in providers. One per kind of component, each only reads its own components.

        constexpr float kIconSize = 40.0f;      // pixels
        constexpr uint8_t kIconFlags = BillboardFlag::ScreenSpaceSize | BillboardFlag::DepthTest | BillboardFlag::EditorOnly | BillboardFlag::Pickable;

        BillboardProxy MakeIcon(const RenderSnapshot& snapshot, uint64_t key, Objects::Actor* actor, BuiltinSprite sprite, const glm::vec4& tint)
        {
            BillboardProxy proxy;
            proxy.key = key;
            proxy.objectId = actor ? actor->GetID().GetAsInt() : -1;
            if (actor && actor->transform)
                proxy.position = glm::dvec3(SpaceOf(snapshot, actor).ApplyToPoint(actor->transform->GetWorldPosition()));
            proxy.size = kIconSize;
            proxy.sprite = BillboardAtlas::Get().GetBuiltin(sprite);
            proxy.tint = tint;
            proxy.facing = BillboardProxyFacing::Camera;
            proxy.flags = kIconFlags;
            return proxy;
        }

        void LightBillboards(Worlds::World& world, int worldIndex, const RenderSnapshot& snapshot, std::vector<BillboardProxy>& out)
        {
            for (auto& [id, light] : world.Ext<RenderWorldData>().lightComps) {
                if (!light || !light->Active() || !light->parent)
                    continue;
                // The icon takes the light colour, kept bright enough to read on any background
                const glm::vec3 color = glm::mix(glm::vec3(light->color), glm::vec3(1.0f), 0.5f);
                out.push_back(MakeIcon(snapshot, MakeKey(worldIndex, id), light->parent.get(), BuiltinSprite::Light, glm::vec4(color, 1.0f)));
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
                out.push_back(MakeIcon(snapshot, MakeKey(worldIndex, id), camera->parent.get(), BuiltinSprite::Camera, glm::vec4(1.0f)));
            }
        }

        void ReflectionProbeBillboards(Worlds::World& world, int worldIndex, const RenderSnapshot& snapshot, std::vector<BillboardProxy>& out)
        {
            for (auto& [id, probe] : world.Ext<RenderWorldData>().reflectionProbes) {
                if (!probe || !probe->Active() || !probe->parent)
                    continue;
                // The probe icon in a cooler colour than the GI one, dimmed while the probe has no picture to reflect
                const glm::vec4 tint = probe->GetEnvironment() ? glm::vec4(0.55f, 0.80f, 1.0f, 1.0f) : glm::vec4(0.55f, 0.65f, 0.80f, 0.55f);
                out.push_back(MakeIcon(snapshot, MakeKey(worldIndex, id), probe->parent.get(), BuiltinSprite::ProbeVolume, tint));
            }
        }

        void ProbeVolumeBillboards(Worlds::World& world, int worldIndex, const RenderSnapshot& snapshot, std::vector<BillboardProxy>& out)
        {
            for (auto& [id, volume] : world.Ext<RenderWorldData>().probeVolumes) {
                if (!volume || !volume->Active() || !volume->parent)
                    continue;
                out.push_back(MakeIcon(snapshot, MakeKey(worldIndex, id), volume->parent.get(), BuiltinSprite::ProbeVolume, glm::vec4(1.0f)));
            }
        }

        void ComponentBillboards(Worlds::World& world, int worldIndex, const RenderSnapshot& snapshot, std::vector<BillboardProxy>& out)
        {
            for (auto& [id, billboard] : world.Ext<RenderWorldData>().billboards) {
                if (!billboard || !billboard->Active() || !billboard->parent)
                    continue;

                BillboardProxy proxy;
                proxy.key = MakeKey(worldIndex, id);
                proxy.objectId = billboard->parent->GetID().GetAsInt();
                proxy.position = glm::dvec3(SpaceOf(snapshot, billboard->parent.get()).ApplyToPoint(glm::vec3(billboard->GetPosition())));
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
            env.ssr = world.Ext<RenderWorldData>().ssr;

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
                    next.lights.push_back(MakeLightProxy(MakeKey(worldIndex, id), *light, FindPoseOffset(poseOffsets, light->parent.get()), SpaceOf(next, light->parent.get())));
            }

            for (auto& [id, volume] : data.probeVolumes) {
                if (volume && volume->Active())
                    next.probeVolumes.push_back(MakeProbeVolumeProxy(MakeKey(worldIndex, id), *volume, SpaceOf(next, volume->parent.get())));
            }

            for (auto& [id, probe] : data.reflectionProbes) {
                if (probe && probe->Active())
                    next.reflectionProbes.push_back(MakeReflectionProbeProxy(MakeKey(worldIndex, id), *probe, SpaceOf(next, probe->parent.get())));
            }
        }

        CameraProxy MakeCameraProxy(Camera& camera, const RenderExtractor::PoseOffsets* poseOffsets, const FrameToRender& space)
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

            // Into render space : the view is composed with the inverse of the frame's placement (inverted in double)
            if (!space.identity) {
                const glm::dmat4 frameToRender = Core::ComposeTRS(space.translation, space.rotation, glm::dvec3(1.0));
                proxy.view = proxy.view * glm::mat4(glm::inverse(frameToRender));
                proxy.viewProjection = proxy.projection * proxy.view;
                proxy.position = space.ApplyToPoint(proxy.position);
                proxy.forward = space.ApplyToDirection(proxy.forward);
                proxy.right = space.ApplyToDirection(proxy.right);
                proxy.up = space.ApplyToDirection(proxy.up);
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
        m_BillboardProviders.push_back(ReflectionProbeBillboards);
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
            // Large worlds : into render space, relative to the render origin (a no-op near the origin)
            next.meshes[i].worldMatrix = SpaceOfCached(next, m_Sources[i].model->parent.get()).ApplyToMatrix(world);
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

    const FrameToRender& RenderExtractor::SpaceOfCached(const RenderSnapshot& snapshot, Objects::Actor* actor)
    {
        static const FrameToRender kIdentity;
        const Core::ReferenceFrameTree* tree = actor && actor->transform ? actor->transform->GetFrameTree() : nullptr;
        if (!tree)
            return kIdentity;
        if (tree->Count() == 1 && snapshot.renderOrigin == Core::WorldPosition())
            return kIdentity;

        const Core::FrameId frame = actor->transform->GetFrame();
        const auto key = std::make_pair(static_cast<const void*>(tree), frame);
        auto it = m_SpaceCache.find(key);
        if (it == m_SpaceCache.end())
            it = m_SpaceCache.emplace(key, MakeFrameToRender(*tree, frame, snapshot.renderOrigin)).first;
        return it->second;
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

        auto camera = cameras.GetActiveCamera();

        // The render origin follows the camera, in steps : positions are drawn relative to it, so the floats the renderer gets stay
        // small wherever in the universe the camera is. While the camera stays within the rebase distance of it, it does not move
        // (render space then is the root frame, and nothing is different from a world without reference frames).
        {
            const Core::WorldPosition previousOrigin = previous.frame != 0 ? previous.renderOrigin : m_RenderOrigin;
            if (camera && camera->parent && camera->parent->transform) {
                const Core::WorldPosition cameraPosition = camera->parent->transform->GetWorldCoords();
                const double distance = glm::length(Core::WorldPosition::Offset(cameraPosition, m_RenderOrigin));
                // A camera whose position is not a usable number (NaN, infinity) must not become the origin, and an origin that
                // went bad is dropped : the next sane camera position brings it back
                if (!std::isfinite(distance)) {
                    if (!std::isfinite(glm::length(Core::WorldPosition::Offset(m_RenderOrigin, Core::WorldPosition()))))
                        m_RenderOrigin = Core::WorldPosition();
                }
                else if (m_RebaseDistance <= 0.0 || distance > m_RebaseDistance) {
                    if (cameraPosition.InCellRange())
                        // a cell corner : exact (or the camera itself, for strict camera-relative rendering)
                        m_RenderOrigin = m_RebaseDistance <= 0.0 ? cameraPosition : Core::WorldPosition(cameraPosition.cell, glm::dvec3(0.0));
                    else {
                        // Past the reach of the cells the camera's root position is a rounded double : its small offset inside its frame is
                        // gone. The frame's origin is exact, and the camera is near it : that is the origin, so that what is near the camera
                        // keeps small numbers.
                        const Core::ReferenceFrameTree* tree = camera->parent->transform->GetFrameTree();
                        m_RenderOrigin = tree ? tree->GetOriginInRoot(camera->parent->transform->GetFrame()) : cameraPosition;
                    }
                }
            }
            next.renderOrigin = m_RenderOrigin;
            next.originShift = Core::WorldPosition::Offset(previousOrigin, m_RenderOrigin);
        }
        m_SpaceCache.clear();

        if (camera) {
            next.camera = MakeCameraProxy(*camera, m_PoseOffsets, SpaceOf(next, camera->parent.get()));
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
        std::sort(next.reflectionProbes.begin(), next.reflectionProbes.end(),
                  [](const ReflectionProbeProxy& a, const ReflectionProbeProxy& b) { return a.volume.key < b.volume.key; });

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

    Objects::Components::ReflectionProbe* FindReflectionProbe(Worlds::WorldManager& worlds, uint64_t key)
    {
        const int worldIndex = int(key >> 48);
        const int idInWorld = int(uint32_t(key & 0xFFFFFFFFull));

        if (worldIndex >= worlds.GetLoadedWorldCount())
            return nullptr;

        Worlds::World* world = worlds.GetWorldAt(worldIndex);
        if (!world)
            return nullptr;

        auto& probes = world->Ext<RenderWorldData>().reflectionProbes;
        auto it = probes.find(idInWorld);
        return it != probes.end() ? it->second.get() : nullptr;
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
