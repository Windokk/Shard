#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/core/math/large_world.hpp"
#include "engine/renderer/rhi/render_view.hpp"
#include "engine/renderer/rhi/display_transform.hpp"
#include "engine/renderer/rhi/ssr_settings.hpp"

namespace Shard::Engine::Rendering {

    class Mesh;
    class Material;
    class Texture2D;
    class Framebuffer;
    class EnvironmentMap;

    // Plain-data proxies, no pointers to actors or worlds, so a frame can render while the simulation builds the next.
    // `key` identifies the source across snapshots, `changed` says whether it differs from the previous one.

    struct MeshProxy {
        uint64_t key = 0;
        int objectId = -1;
        int componentId = -1;
        std::shared_ptr<Mesh> mesh;
        std::vector<std::shared_ptr<Material>> materials;
        glm::mat4 worldMatrix = glm::mat4(1.0f);
        std::vector<std::string> extraPasses;

        bool staticLighting = true;
        int lightmapResolution = 0;
        int lightmapPage = -1;
        glm::vec4 lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

        bool changed = true;
    };

    enum class BillboardProxyFacing : uint8_t { Camera = 0, AxisY = 1, Fixed = 2 };

    namespace BillboardFlag {
        constexpr uint8_t ScreenSpaceSize = 1 << 0;
        constexpr uint8_t DepthTest       = 1 << 1;
        constexpr uint8_t EditorOnly      = 1 << 2;
        constexpr uint8_t Pickable        = 1 << 3;
    }

    struct BillboardProxy {
        uint64_t key = 0;
        int objectId = -1;
        glm::dvec3 position = glm::dvec3(0);
        float size = 0;
        uint32_t sprite = 0;
        glm::vec4 tint = glm::vec4(1,1,1,1);
        BillboardProxyFacing facing = BillboardProxyFacing::Camera;
        uint8_t flags = BillboardFlag::ScreenSpaceSize | BillboardFlag::DepthTest;
    };

    struct SpriteInstance {
        glm::vec3 position = glm::vec3(0);
        float rotation = 0;
        glm::vec2 size = glm::vec2(1);
        uint32_t color = 0xFFFFFFFFu;
        glm::vec4 uv = glm::vec4(0, 0, 1, 1);
        glm::vec3 axis = glm::vec3(0, 1, 0);
        float custom = 0;
    };

    struct SpriteBatchProxy {
        uint64_t key = 0;
        std::shared_ptr<Texture2D> texture;
        uint8_t facing = 0;
        uint8_t blend = 2;
        uint8_t sort = 0;
        uint8_t flags = 0;
        std::vector<SpriteInstance> sprites;
    };

    struct OverlayVertex {
        float x = 0, y = 0;
        uint32_t color = 0xFFFFFFFFu;               // premultiplied alpha, r | g << 8 | b << 16 | a << 24
        float u = 0, v = 0;
    };

    struct OverlayGeometry {
        std::vector<OverlayVertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t gpu[3] = {0, 0, 0};
        ~OverlayGeometry();
    };

    struct OverlayCommand {
        enum class Kind : uint8_t { Geometry, Triangles, Scissor, Transform, ClipMask, Clear };
        Kind kind = Kind::Geometry;
        std::shared_ptr<OverlayGeometry> geometry;
        std::shared_ptr<Texture2D> texture;
        glm::vec2 translation = glm::vec2(0);
        glm::mat4 transform = glm::mat4(1.0f);
        int32_t scissor[4] = {0, 0, 0, 0};
        bool enabled = false;
        uint32_t operation = 0;
        glm::vec4 color = glm::vec4(0);
        uint32_t first = 0, count = 0;
    };

    struct OverlayProxy {
        int order = 0;
        std::shared_ptr<Framebuffer> target;
        uint32_t width = 0, height = 0;
        std::vector<OverlayCommand> commands;
        std::vector<OverlayVertex> inlineVertices;
        std::vector<uint32_t> inlineIndices;
    };

    enum class LightProxyType : uint32_t { Directional = 0, Point = 1, Spot = 2 };

    struct LightProxy {
        uint64_t key = 0;
        int objectId = -1;
        LightProxyType type = LightProxyType::Directional;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 color = glm::vec3(1.0f);
        float intensity = 1.0f;
        float radius = 0.0f;
        float innerCutoff = 0.0f;
        float outerCutoff = 0.0f;
        bool castShadows = false;
        float sourceSize = 0.0f;
        int bakeMode = 0;
        bool changed = true;
    };

    struct CameraProxy {
        bool valid = false;
        uint64_t key = 0;
        int objectId = -1;
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 projection = glm::mat4(1.0f);
        glm::mat4 viewProjection = glm::mat4(1.0f);
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
        float fovDegrees = 60.0f;
        float orthoSize = 10.0f;
        int width = 0;
        int height = 0;
        bool orthographic = false;
        bool frustumCulling = true;
        bool changed = true;

        float AspectRatio() const { return height > 0 ? float(width) / float(height) : 1.0f; }
    };

    struct VolumeProxy {
        uint64_t key = 0;
        int objectId = -1;
        glm::vec3 center = glm::vec3(0.0f);
        glm::vec3 halfExtent = glm::vec3(1.0f);
        bool active = true;
        bool changed = true;
    };

    inline glm::vec3 ProbeGridSpacing(const glm::vec3& halfExtent, const glm::ivec3& probeCounts)
    {
        return (halfExtent * 2.0f) / glm::vec3(glm::max(probeCounts, glm::ivec3(1)));
    }

    // World-space position of probe (0,0,0). Inset half a cell: probes on a wall see a full hemisphere and give a faceted pattern.
    inline glm::vec3 ProbeGridOrigin(const glm::vec3& center, const glm::vec3& halfExtent, const glm::ivec3& probeCounts)
    {
        return center - halfExtent + ProbeGridSpacing(halfExtent, probeCounts) * 0.5f;
    }

    struct ProbeVolumeProxy {
        VolumeProxy volume;
        glm::ivec3 probeCounts = glm::ivec3(8, 4, 8);
        int raysPerProbe = 64;
        int probeUpdateStride = 1;
        float indirectIntensity = 1.0f;
        bool enableRelocation = true;
        std::string bakedData;

        glm::vec3 GridSpacing() const { return ProbeGridSpacing(volume.halfExtent, probeCounts); }
        glm::vec3 GridOrigin() const { return ProbeGridOrigin(volume.center, volume.halfExtent, probeCounts); }
    };

    enum class ReflectionProbeShape : int { Box = 0, Sphere, Cylinder, Cone, Pyramid };

    struct ReflectionProbeProxy {
        VolumeProxy volume;
        ReflectionProbeShape shape = ReflectionProbeShape::Box;
        float intensity = 1.0f;
        float blendDistance = 1.0f;
        bool boxProjection = true;
        std::shared_ptr<EnvironmentMap> environment;
    };

    struct EnvironmentProxy {
        glm::vec3 ambientColor = glm::vec3(0.3f);
        bool ssaoEnabled = true;
        float ssaoRadius = 0.5f;
        float ssaoBias = 0.025f;
        float ssaoIntensity = 1.0f;
        float ssaoPower = 2.0f;
        DisplaySettings display;
        SSRSettings ssr;
        std::string lightmapData;
        std::shared_ptr<EnvironmentMap> skybox;
        std::shared_ptr<Material> skyboxMaterial;
        int skyboxObjectId = -1;
    };

    struct RenderSnapshot {
        uint64_t frame = 0;
        int viewportWidth = 0;
        int viewportHeight = 0;

        // Positions, matrices and views are float, relative to this render origin (follows the camera).
        // Temporal history or position-keyed caches must add `originShift` when the origin moves. Zero near the origin.
        Core::WorldPosition renderOrigin;
        glm::dvec3 originShift = glm::dvec3(0.0);

        CameraProxy camera;
        EnvironmentProxy environment;
        std::vector<MeshProxy> meshes;
        std::vector<LightProxy> lights;
        std::vector<ProbeVolumeProxy> probeVolumes;
        std::vector<ReflectionProbeProxy> reflectionProbes;
        std::vector<BillboardProxy> billboards;
        std::vector<SpriteBatchProxy> spriteBatches;
        std::vector<OverlayProxy> overlays;

        void Reset()
        {
            frame = 0;
            renderOrigin = Core::WorldPosition();
            originShift = glm::dvec3(0.0);
            camera = CameraProxy();
            environment = EnvironmentProxy();
            meshes.clear();
            lights.clear();
            probeVolumes.clear();
            reflectionProbes.clear();
            billboards.clear();
            spriteBatches.clear();
            overlays.clear();
        }

        template <typename Proxy>
        static const Proxy* Find(const std::vector<Proxy>& proxies, uint64_t key)
        {
            for (const Proxy& proxy : proxies)
                if (proxy.key == key)
                    return &proxy;
            return nullptr;
        }
    };

    bool BuildMainView(const RenderSnapshot& snapshot, RenderView& out);

    RenderView BuildView(const CameraProxy& camera);

    inline const glm::mat4& CullMatrix(const CameraProxy& camera) { return camera.viewProjection; }

    // Double buffer: Extract fills one while the renderer reads the other. BeginWrite() waits only if the renderer still reads the buffer being reused.
    class RenderSnapshotBuffer {
    public:
        class ReadLease {
        public:
            ReadLease() = default;
            ReadLease(const ReadLease&) = delete;
            ReadLease& operator=(const ReadLease&) = delete;
            ReadLease(ReadLease&& other) noexcept : m_Owner(other.m_Owner), m_Index(other.m_Index) { other.m_Owner = nullptr; }
            ReadLease& operator=(ReadLease&& other) noexcept
            {
                if (this != &other) {
                    Release();
                    m_Owner = other.m_Owner;
                    m_Index = other.m_Index;
                    other.m_Owner = nullptr;
                }
                return *this;
            }
            ~ReadLease() { Release(); }

            const RenderSnapshot& Get() const { return m_Owner->m_Buffers[m_Index]; }
            const RenderSnapshot* operator->() const { return &Get(); }
            const RenderSnapshot& operator*() const { return Get(); }
            explicit operator bool() const { return m_Owner != nullptr; }

        private:
            friend class RenderSnapshotBuffer;
            ReadLease(RenderSnapshotBuffer* owner, int index) : m_Owner(owner), m_Index(index) {}
            void Release()
            {
                if (m_Owner)
                    m_Owner->ReleaseRead(m_Index);
                m_Owner = nullptr;
            }

            RenderSnapshotBuffer* m_Owner = nullptr;
            int m_Index = 0;
        };

        // Holds the snapshot from two publishes ago. Diff against Latest() for the `changed` flags.
        RenderSnapshot& BeginWrite();

        void Publish();

        ReadLease Acquire();

        // Only valid on the publishing thread, or while nothing publishes.
        const RenderSnapshot& Latest() const { return m_Buffers[m_Current]; }

        uint64_t PublishedCount() const { return m_Published; }

    private:
        void ReleaseRead(int index);

        RenderSnapshot m_Buffers[2];
        int m_Current = 0;
        int m_Readers[2] = {0, 0};
        bool m_Writing = false;
        uint64_t m_Published = 0;
        std::mutex m_Mutex;
        std::condition_variable m_Released;
    };
}
