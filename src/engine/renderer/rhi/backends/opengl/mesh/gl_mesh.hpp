#pragma once

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/pipelines/pipeline.hpp"

namespace Shard::Engine::Rendering{

    class VertexLayout;

    class GLMesh : public Mesh{
        public:

            GLMesh() {};

            void Create(std::vector<uint8_t> vertices, std::vector<uint32_t> indices, const VertexLayout& layout) override;

            void CreateFromFBX(const ufbx_mesh *ufbx_mesh, double scene_unit_meters,
                ufbx_material_list& ufbx_mats, ufbx_node* mesh_node,
                COL_RGBA vertexColor = COL_RGBA(0.99f,0.06f,0.75f,1.0f)) override;

            void CreateFromData(const MeshCPUData& data) override;

            ~GLMesh() override;


            uint32_t GetVAO() const { return m_VAO; }

        private:

            void GenerateGLBuffers();

            uint32_t m_VAO = 0, m_VBO = 0, m_EBO = 0;

    };
}