#pragma once

#include <vector>
#include <memory>
#include <limits>

#include "engine/core/color.hpp"

#include "engine/assets/vfs/filesystem.hpp"

#include "engine/world/components/transform.hpp"

#include "engine/renderer/rhi/pipelines/pipeline.hpp"

#include <ufbx/ufbx.h>

namespace Shard::Engine::Rendering {

    struct SubMesh {
        size_t indexOffset;
        size_t indexCount;
        size_t vertexCount;
        // Index of the source material slot this submesh belongs to (matches the
        // mesh's FBX material order, which is what level files address by slot).
        uint32_t materialIndex = 0;
    };

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

        bool bindCameraState = true;
        
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
    // ResourcesManager::LoadModel path and the async level loader's background decode workers.
    MeshCPUData DecodeMeshFile(const Filesystem::Path& path);

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

            const size_t GetIndexCount() const { return m_Indices.size(); }
            const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

            const size_t GetVertexCount() const { return m_VertexCount; }
            const std::vector<uint8_t>& GetVertices() const { return m_Vertices; }

            const glm::vec3 GetBoundsMax() const { return m_BoundsMax; }
            const glm::vec3 GetBoundsMin() const { return m_BoundsMin; }

            std::vector<DrawCommand> CreateDrawCommands(std::shared_ptr<Objects::Components::Transform> tr, int modelID, std::vector<std::shared_ptr<Material>> mats);

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
            std::vector<uint8_t> m_Vertices;
            std::vector<uint32_t> m_Indices;
            size_t m_VertexCount;
            
            VertexLayout m_VertexLayout;

            glm::vec3 m_BoundsMin = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 m_BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());

        public:

            int m_MaterialsSlots = 0;
    };
}
