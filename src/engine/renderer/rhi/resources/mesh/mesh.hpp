#pragma once

#include <mutex>
#include <vector>
#include <memory>
#include <limits>
#include <algorithm>

#include "engine/core/color.hpp"

#include "engine/assets/vfs/filesystem.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/renderer/rhi/pipelines/pipeline.hpp"

#include <ufbx/ufbx.h>

namespace Shard::Engine::Rendering {

    struct SubMesh {
        size_t indexOffset;
        size_t indexCount;
        size_t vertexCount;
        // Index of the source material slot this submesh belongs to (matches the
        // mesh's FBX material order, which is what world files address by slot).
        uint32_t materialIndex = 0;
        // Mesh-space bounds of just this submesh's triangles, used to frustum-cull it on its own. Left at
        // zero (min == max) a submesh is never culled - see Renderer::ExecuteRenderPass.
        glm::vec3 boundsMin = glm::vec3(0);
        glm::vec3 boundsMax = glm::vec3(0);
    };

    // A material slot's triangles are split into spatially compact submeshes (a kd-tree on the triangle
    // centroids) of at most this many triangles, so frustum culling - per submesh - can discard the parts of
    // a large mesh that are out of view instead of drawing all of it whenever any of it is visible. Meshes
    // whose slots are all smaller than this keep one submesh per slot.
    inline constexpr size_t kMaxSubMeshTriangles = 32768;

    class Material;
    class CommandBuffer;
    class Mesh;
    class Pipeline;
    class VertexLayout;

    struct DrawCommand
    {
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t vertexCount;

        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        glm::mat4 modelMatrix;

        // Local min bounds
        glm::vec3 boundsMin = glm::vec3(0);
        // Local max bounds
        glm::vec3 boundsMax = glm::vec3(0);

        uint32_t objectID;
        uint32_t modelID;

        bool fullscreenTri = false;

        /// With fullscreenTri : a draw without a mesh made of `proceduralVertexCount` vertices (the vertex shader builds them from
        /// gl_VertexID), `instanceCount` times. 0 keeps the fullscreen triangle. Billboards : 6 vertices, one instance each.
        uint32_t proceduralVertexCount = 0;
        uint32_t instanceCount = 1;

        bool bindCameraState = true;

        /// Baked lighting : the bindless handle of the lightmap page this draw reads the light it receives from (0 : the draw
        /// has no lightmap), and what maps the mesh's lightmap coordinates onto that page (uv * xy + zw)
        uint64_t lightmapHandle = 0;
        glm::vec4 lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

        /// Slot in the pass's GPU occlusion-culling buffers (see OcclusionCullingManager), or -1 when this
        /// command isn't GPU-culled. @note Automatically filled by the renderer every frame.
        int32_t cullSlot = -1;
        
        /// @note Automatically filled by the renderer. Any content will be overriden.
        uint64_t sortKey = 0;
        /// @note Automatically filled by the renderer. Any content will be overriden.
        uint64_t commandID;
    };

    /// @note Combines a mesh asset ID, a component ID and a submesh index into a collision-resistant
    /// 64-bit key. Do not pack/truncate these values manually - use this everywhere a commandID is built.
    inline uint64_t MakeCommandID(uint64_t meshAssetID, uint64_t componentID, uint64_t submeshID)
    {
        auto hashCombine = [](uint64_t seed, uint64_t v) {
            seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        };

        uint64_t h = 1469598103934665603ULL; // FNV offset basis
        h = hashCombine(h, meshAssetID);
        h = hashCombine(h, componentID);
        h = hashCombine(h, submeshID);
        return h;
    }

    // Plain CPU-side result of building mesh geometry (triangulation, vertex dedup, tangent
    // computation) from an FBX source - no GL calls involved in producing this, so it's safe to build
    // on a worker thread and hand across to the GL/main thread afterwards via Mesh::CreateFromData().
    struct MeshCPUData
    {
        bool success = false;
        std::vector<uint8_t> vertices;
        std::vector<uint32_t> indices;
        std::vector<SubMesh> submeshes;
        VertexLayout layout;
        glm::vec3 boundsMin = glm::vec3(std::numeric_limits<float>::max());
        glm::vec3 boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    };

    // Pure CPU geometry build (triangulation, vertex dedup, tangent computation) from an
    // already-loaded ufbx scene's mesh/node - no GL calls, safe to call from any thread.
    MeshCPUData BuildMeshCPUDataFromFBX(const ufbx_mesh *ufbx_mesh, double scene_unit_meters,
        ufbx_material_list& ufbx_mats, ufbx_node* mesh_node, COL_RGBA vertexColor = COL_RGBA(0.99f,0.06f,0.75f,1.0f));

    // Pure CPU: opens the FBX file, builds geometry via BuildMeshCPUDataFromFBX, and closes it again -
    // no GL calls, safe to call from any thread. Used by both the synchronous
    // ResourcesManager::LoadModel path and the async world loader's background decode workers.
    MeshCPUData DecodeMeshFile(const Filesystem::Path& path);

    /// A mesh's vertices and indices on the CPU side (see Mesh::AcquireCPUData)
    struct MeshCPUCopy
    {
        std::vector<uint8_t> vertices;
        std::vector<uint32_t> indices;
    };

    class Mesh : public std::enable_shared_from_this<Mesh>
    {
        public:

            static std::shared_ptr<Mesh> Create();

            virtual void Create(std::vector<uint8_t> vertices, std::vector<uint32_t> indices, const VertexLayout& layout) = 0;

            virtual void CreateFromFBX(const ufbx_mesh *ufbx_mesh, double scene_unit_meters,
                ufbx_material_list& ufbx_mats, ufbx_node* mesh_node, COL_RGBA vertexColor = COL_RGBA(0.99f,0.06f,0.75f,1.0f)) = 0;

            // Uploads already-built CPU geometry (e.g. produced off-thread by BuildMeshCPUDataFromFBX/
            // DecodeMeshFile) to the GPU. `data.success` must be true.
            virtual void CreateFromData(const MeshCPUData& data) = 0;

            virtual ~Mesh() = default;

            const int SubMeshesCount() const { return m_Submeshes.size(); }
            const std::vector<SubMesh>& GetSubMeshes() const { return m_Submeshes; }

            /// How many material slots the mesh addresses (what a Model's material list has to match). Not the
            /// same as SubMeshesCount() : one slot can be split into several spatial submeshes.
            size_t GetMaterialSlotCount() const
            {
                size_t slots = 1;
                for (const SubMesh& submesh : m_Submeshes)
                    slots = std::max<size_t>(slots, (size_t)submesh.materialIndex + 1);
                return slots;
            }

            const size_t GetIndexCount() const { return m_IndexCount; }
            const size_t GetVertexCount() const { return m_VertexCount; }

            /// @brief The vertices and indices on the CPU side, for the rare code that reads them (building the BVH of the
            /// ray traced GI). Once a mesh is on the GPU only the GPU keeps them - the CPU copy is as big as the buffers, and
            /// would double the memory of every mesh of the world for something almost nothing reads (`mesh.keep_cpu_data`
            /// brings that back). They are read back from the GPU buffers when nobody holds them, and go away again with the
            /// last holder. Safe from any thread (it waits for the graphics thread when it has to read back). Null if they
            /// cannot be had.
            std::shared_ptr<const MeshCPUCopy> AcquireCPUData();

            const glm::vec3 GetBoundsMax() const { return m_BoundsMax; }
            const glm::vec3 GetBoundsMin() const { return m_BoundsMin; }

            std::vector<DrawCommand> CreateDrawCommands(const glm::mat4& worldMatrix, int objectID, int modelID, std::vector<std::shared_ptr<Material>> mats,
                uint64_t lightmapHandle = 0, const glm::vec4& lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));

            void SetAssetID(Filesystem::AssetID assetID) {
                m_AssetID = assetID;
            }

            Filesystem::AssetID GetAssetID() {
                return m_AssetID;
            }

            const VertexLayout& GetVertexLayout() const { return m_VertexLayout; }

        protected:

            Filesystem::AssetID m_AssetID;

            std::vector<SubMesh> m_Submeshes;
            // Only what the creation (Create / CreateFromData) is given, until it is on the GPU : see FinishCPUData()
            std::vector<uint8_t> m_Vertices;
            std::vector<uint32_t> m_Indices;
            size_t m_VertexCount = 0;
            size_t m_IndexCount = 0;

            /// Called once the mesh is uploaded : the CPU copy is dropped (or kept, see `mesh.keep_cpu_data`)
            void FinishCPUData();

            /// Reads the vertices and indices back from the GPU (on the graphics thread). False if the backend cannot.
            virtual bool ReadBackCPUData(MeshCPUCopy& out) { (void)out; return false; }
            
            VertexLayout m_VertexLayout;

            glm::vec3 m_BoundsMin = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 m_BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());

        private:

            std::mutex m_CPUMutex;
            std::shared_ptr<MeshCPUCopy> m_KeptCPU;         // `mesh.keep_cpu_data` : held for good
            std::weak_ptr<MeshCPUCopy> m_ReadBackCPU;       // read back on demand : alive while someone holds it

        public:

            int m_MaterialsSlots = 0;
    };
}
