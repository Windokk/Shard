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
        uint32_t materialIndex = 0;
        glm::vec3 boundsMin = glm::vec3(0);
        glm::vec3 boundsMax = glm::vec3(0);
    };

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

        glm::vec3 boundsMin = glm::vec3(0);
        glm::vec3 boundsMax = glm::vec3(0);

        uint32_t objectID;
        uint32_t modelID;

        bool fullscreenTri = false;

        uint32_t proceduralVertexCount = 0;
        uint32_t instanceCount = 1;

        bool bindCameraState = true;

        uint64_t lightmapHandle = 0;
        glm::vec4 lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);

        int32_t cullSlot = -1;
        
        uint64_t sortKey = 0;
        uint64_t commandID;
    };

    // Use everywhere a commandID is built. Do not pack or truncate the values by hand.
    inline uint64_t MakeCommandID(uint64_t meshAssetID, uint64_t componentID, uint64_t submeshID)
    {
        auto hashCombine = [](uint64_t seed, uint64_t v) {
            seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        };

        uint64_t h = 1469598103934665603ULL;
        h = hashCombine(h, meshAssetID);
        h = hashCombine(h, componentID);
        h = hashCombine(h, submeshID);
        return h;
    }

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

    MeshCPUData BuildMeshCPUDataFromFBX(const ufbx_mesh *ufbx_mesh, double scene_unit_meters,
        ufbx_material_list& ufbx_mats, ufbx_node* mesh_node, COL_RGBA vertexColor = COL_RGBA(0.99f,0.06f,0.75f,1.0f));

    MeshCPUData DecodeMeshFile(const Filesystem::Path& path);

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

            virtual void CreateFromData(const MeshCPUData& data) = 0;

            virtual ~Mesh() = default;

            int SubMeshesCount() const { return static_cast<int>(m_Submeshes.size()); }
            const std::vector<SubMesh>& GetSubMeshes() const { return m_Submeshes; }

            size_t GetMaterialSlotCount() const
            {
                size_t slots = 1;
                for (const SubMesh& submesh : m_Submeshes)
                    slots = std::max<size_t>(slots, (size_t)submesh.materialIndex + 1);
                return slots;
            }

            size_t GetIndexCount() const { return m_IndexCount; }
            size_t GetVertexCount() const { return m_VertexCount; }

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
            std::vector<uint8_t> m_Vertices;
            std::vector<uint32_t> m_Indices;
            size_t m_VertexCount = 0;
            size_t m_IndexCount = 0;

            void FinishCPUData();

            virtual bool ReadBackCPUData(MeshCPUCopy& out) { (void)out; return false; }
            
            VertexLayout m_VertexLayout;

            glm::vec3 m_BoundsMin = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 m_BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());

        private:

            std::mutex m_CPUMutex;
            std::shared_ptr<MeshCPUCopy> m_KeptCPU;
            std::weak_ptr<MeshCPUCopy> m_ReadBackCPU;

        public:

            int m_MaterialsSlots = 0;
    };
}
