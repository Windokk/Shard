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
#include <glm/gtc/type_precision.hpp>
#include <string>

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

    // The bone matrices of a skinned object, shared by every draw command of it. The renderer packs the palettes of the frame into one GPU buffer
    // (`gpuOffset` : the first matrix of this one, set when it does) and the shaders skin the vertices from it. `revision` changes with the bones, so a
    // pass that caches its output (shadow maps) draws again.
    struct SkinPalette
    {
        std::vector<glm::mat4> bones;
        uint32_t gpuOffset = 0;
        uint64_t revision = 0;
    };

    struct DrawCommand
    {
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t vertexCount;

        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        glm::mat4 modelMatrix;

        // Set for a skinned mesh that is posed : the draw uses the "SKINNED" variant of its shader and reads its bones from the palette
        std::shared_ptr<SkinPalette> skin;

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

    // A node of the model's hierarchy (bones and what they hang from). The local rest transform is in meters, so that a pose built
    // from these nodes lands in the mesh's own space.
    struct MeshSkinNode
    {
        std::string name;
        int32_t parent = -1;
        glm::vec3 translation = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
    };

    // What a skinned FBX adds to its mesh. joints/weights are per vertex of the mesh, in the order of its vertex buffer (4 influences,
    // weights summing to 1). A joint is a node (jointNodes) with its inverse bind matrix, in the mesh's space.
    struct MeshSkin
    {
        std::vector<glm::u16vec4> joints;
        std::vector<glm::vec4> weights;
        std::vector<MeshSkinNode> nodes;
        std::vector<int32_t> jointNodes;
        std::vector<glm::mat4> inverseBind;
    };

    struct MeshCPUData
    {
        bool success = false;
        std::vector<uint8_t> vertices;
        std::vector<uint32_t> indices;
        std::vector<SubMesh> submeshes;
        VertexLayout layout;
        glm::vec3 boundsMin = glm::vec3(std::numeric_limits<float>::max());
        glm::vec3 boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        std::shared_ptr<MeshSkin> skin;     // null for a mesh that is not skinned
    };

    MeshCPUData BuildMeshCPUDataFromFBX(const ufbx_mesh *ufbx_mesh, double scene_unit_meters,
        ufbx_material_list& ufbx_mats, ufbx_node* mesh_node, COL_RGBA vertexColor = COL_RGBA(0.99f,0.06f,0.75f,1.0f));

    // The vertices of a skinned mesh as the GPU skins them : each vertex gets its 4 joints and 4 weights after its own bytes (as floats, so every backend
    // reads them the same way), at the attribute locations kSkinJointsLocation and kSkinWeightsLocation. A mesh that has no skin, or whose skin does not
    // cover its vertices, comes back as it was.
    constexpr uint32_t kSkinJointsLocation = 6;
    constexpr uint32_t kSkinWeightsLocation = 7;
    MeshCPUData AddGpuSkin(MeshCPUData data, const std::vector<glm::u16vec4>& joints, const std::vector<glm::vec4>& weights);
    inline MeshCPUData AddGpuSkin(MeshCPUData data)
    {
        if (!data.skin) return data;
        const std::shared_ptr<MeshSkin> skin = data.skin;
        return AddGpuSkin(std::move(data), skin->joints, skin->weights);
    }

    // Every mesh of the scene in one mesh : the material slots follow one another (the slots of the first mesh, then those of the second...),
    // and a skinned scene brings its skeleton and skin weights along.
    MeshCPUData BuildMeshCPUDataFromScene(const ufbx_scene* scene, COL_RGBA vertexColor = COL_RGBA(0.99f,0.06f,0.75f,1.0f));

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

            // `skin` : the bones of a posed skinned mesh (see SkinPalette). Its draws get the bounds of the mesh grown, since the pose moves the vertices.
            std::vector<DrawCommand> CreateDrawCommands(const glm::mat4& worldMatrix, int objectID, int modelID, std::vector<std::shared_ptr<Material>> mats,
                uint64_t lightmapHandle = 0, const glm::vec4& lightmapScaleOffset = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f),
                const std::shared_ptr<SkinPalette>& skin = nullptr);

            // The vertices carry joints and weights (AddGpuSkin) : the mesh can be drawn skinned
            bool IsSkinnable() const
            {
                for (const VertexElement& element : m_VertexLayout.GetElements())
                    if (element.location == kSkinJointsLocation) return true;
                return false;
            }

            void SetAssetID(Filesystem::AssetID assetID) {
                m_AssetID = assetID;
            }

            Filesystem::AssetID GetAssetID() {
                return m_AssetID;
            }

            const VertexLayout& GetVertexLayout() const { return m_VertexLayout; }

            // The skeleton and weights an FBX brought, null when the mesh is not skinned. The mesh is drawn in its bind pose either way.
            void SetSkin(std::shared_ptr<const MeshSkin> skin) { m_Skin = std::move(skin); }
            const std::shared_ptr<const MeshSkin>& GetSkin() const { return m_Skin; }

        protected:

            std::shared_ptr<const MeshSkin> m_Skin;

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
