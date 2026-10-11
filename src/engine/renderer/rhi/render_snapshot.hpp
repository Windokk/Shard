#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/renderer/rhi/render_view.hpp"

namespace Shard::Engine::Rendering {

    class Mesh;
    class Material;
    class EnvironmentMap;

    // Render proxies : what the renderer knows of the simulation. Plain data, no pointer back to an actor, a component
    // or a world, so a frame can be rendered (later : on its own thread) while the simulation already works on the next.
    // The only shared things are the GPU resources (mesh, materials, environment map), which are owned by the resources
    // manager and immutable while a frame uses them.
    //
    // `key` identifies the source object from one snapshot to the next (an actor id for a camera, the component id
    // in its world for the rest), `changed` says whether the proxy differs from the one with the same key in the
    // previous snapshot, so a consumer can update only what moved.

    struct MeshProxy {
        uint64_t key = 0;
        int objectId = -1;                              // the actor, for picking / outlines
        int componentId = -1;                           // the model component, in its world
        std::shared_ptr<Mesh> mesh;
        std::vector<std::shared_ptr<Material>> materials;
        glm::mat4 worldMatrix = glm::mat4(1.0f);
        /// Passes the mesh is drawn into besides the standing ones (ForwardPass, the SSAO prepass and the shadow list) :
        /// the editor adds the selected actor to its outline mask this way.
        std::vector<std::string> extraPasses;
        bool changed = true;
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
        float innerCutoff = 0.0f;                       // cosine of the inner cone's half angle (what the shader compares against)
        float outerCutoff = 0.0f;                       // cosine of the outer cone's half angle
        bool castShadows = false;
        bool changed = true;
    };

    struct CameraProxy {
        bool valid = false;                             // false : the world has no usable camera this frame
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

    /// A region of the world with a box shape (a GI probe volume today).
    struct VolumeProxy {
        uint64_t key = 0;
        int objectId = -1;
        glm::vec3 center = glm::vec3(0.0f);
        glm::vec3 halfExtent = glm::vec3(1.0f);
        bool active = true;
        bool changed = true;
    };

    /// World-space distance between adjacent probes of a grid that fills a box of `halfExtent` with `probeCounts` probes
    inline glm::vec3 ProbeGridSpacing(const glm::vec3& halfExtent, const glm::ivec3& probeCounts)
    {
        return (halfExtent * 2.0f) / glm::vec3(glm::max(probeCounts, glm::ivec3(1)));
    }

    /// World-space position of probe (0,0,0). Inset by half a cell so the outermost layer of probes sits inside the box
    /// instead of exactly on its surface : a volume is typically sized flush against a room's walls, and a probe placed on a
    /// wall sees it fill almost its whole hemisphere, which shows as a faceted pattern on that wall.
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
        std::string bakedData;                          // path in the project of the baked probes, empty if never baked

        glm::vec3 GridSpacing() const { return ProbeGridSpacing(volume.halfExtent, probeCounts); }
        glm::vec3 GridOrigin() const { return ProbeGridOrigin(volume.center, volume.halfExtent, probeCounts); }
    };

    /// Everything that is not an object of the scene : the world's ambient, SSAO and sky settings.
    struct EnvironmentProxy {
        float ambientIntensity = 0.3f;
        bool ssaoEnabled = true;
        float ssaoRadius = 0.5f;
        float ssaoBias = 0.025f;
        float ssaoIntensity = 1.0f;
        float ssaoPower = 2.0f;
        std::shared_ptr<EnvironmentMap> skybox;         // null : no sky
        std::shared_ptr<Material> skyboxMaterial;       // what draws it : a fullscreen triangle, set up by the world's skybox
        int skyboxObjectId = -1;
    };

    /// @brief One frame of the simulation, as the renderer sees it. Built by the Extract stage, then only read.
    struct RenderSnapshot {
        uint64_t frame = 0;                             // 0 : nothing was ever extracted
        int viewportWidth = 0;
        int viewportHeight = 0;

        CameraProxy camera;                             // the main camera
        EnvironmentProxy environment;
        std::vector<MeshProxy> meshes;
        std::vector<LightProxy> lights;
        std::vector<ProbeVolumeProxy> probeVolumes;

        /// Empties the lists but keeps their memory : the next extraction refills the same buffers.
        void Reset()
        {
            frame = 0;
            camera = CameraProxy();
            environment = EnvironmentProxy();
            meshes.clear();
            lights.clear();
            probeVolumes.clear();
        }

        /// The proxy of the previous snapshot with the same key, or null
        template <typename Proxy>
        static const Proxy* Find(const std::vector<Proxy>& proxies, uint64_t key)
        {
            for (const Proxy& proxy : proxies)
                if (proxy.key == key)
                    return &proxy;
            return nullptr;
        }
    };

    /// @brief The view the main camera sees the snapshot from. Returns false when the snapshot has no camera.
    bool BuildMainView(const RenderSnapshot& snapshot, RenderView& out);

    /// @brief A view from a camera proxy (main, portal, probe capture, render-to-texture : any camera of the scene).
    RenderView BuildView(const CameraProxy& camera);

    /// @brief Frustum culling happens against this matrix : the camera's view-projection.
    inline const glm::mat4& CullMatrix(const CameraProxy& camera) { return camera.viewProjection; }

    /// @brief The two snapshots the frame is double buffered with : the Extract stage fills one while the renderer reads the other.
    ///
    /// Write side (simulation) :   RenderSnapshot& next = buffer.BeginWrite();  ... fill ...  buffer.Publish();
    /// Read side (renderer)    :   { auto frame = buffer.Acquire(); use(frame.Get()); }
    ///
    /// Publish() makes the written snapshot the current one. BeginWrite() waits only if the renderer still reads the
    /// buffer it is about to reuse, which cannot happen while both run on the same thread. A reader only ever gets a
    /// const reference : a published snapshot is immutable.
    class RenderSnapshotBuffer {
    public:
        /// Keeps the snapshot it was acquired from alive and unmodified until it goes out of scope
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

        /// The buffer to fill. Its content is the snapshot from two publishes ago : the previous one (Latest()) is the
        /// one to compare with for the `changed` flags.
        RenderSnapshot& BeginWrite();

        /// Makes the buffer returned by BeginWrite() the current snapshot
        void Publish();

        /// The current snapshot, held until the lease is dropped
        ReadLease Acquire();

        /// The current snapshot without a lease : only valid on the thread that publishes (the simulation, e.g. to diff
        /// against), or while nothing publishes.
        const RenderSnapshot& Latest() const { return m_Buffers[m_Current]; }

        /// How many snapshots were published
        uint64_t PublishedCount() const { return m_Published; }

    private:
        void ReleaseRead(int index);

        RenderSnapshot m_Buffers[2];
        int m_Current = 0;                              // the published one ; the other is the write buffer
        int m_Readers[2] = {0, 0};
        bool m_Writing = false;
        uint64_t m_Published = 0;
        std::mutex m_Mutex;
        std::condition_variable m_Released;
    };
}
