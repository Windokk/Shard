#include "gl_mesh.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"

#include "engine/renderer/material/shader.hpp"

#include <algorithm>

namespace Shard::Engine::Rendering{
    
    static GLenum ShaderDataTypeToOpenGL(ShaderDataType type)
    {
        switch(type)
        {
            case ShaderDataType::Bool:  
            case ShaderDataType::Int:   return GL_INT;

            case ShaderDataType::Float:
            case ShaderDataType::Vec2:
            case ShaderDataType::Vec3:
            case ShaderDataType::Vec4:
            case ShaderDataType::Mat2:
            case ShaderDataType::Mat3:
            case ShaderDataType::Mat4:
                return GL_FLOAT;

            default:
                return GL_FLOAT;
        }
    }

    void GLMesh::Create(std::vector<uint8_t> vertices, std::vector<uint32_t> indices, const VertexLayout& layout)
    {
        m_Vertices.clear();
        m_Submeshes.clear();
        m_Indices.clear();

        m_Vertices = vertices;
        m_Indices = indices;
        m_Submeshes.push_back({
            0,
            indices.size(),
            vertices.size() / layout.GetStride()
        });

        m_VertexCount = vertices.size() / layout.GetStride();

        m_VertexLayout = layout;

        m_BoundsMin = glm::vec3(std::numeric_limits<float>::max());
        m_BoundsMax = glm::vec3(std::numeric_limits<float>::lowest());

        uint32_t stride = layout.GetStride();
        auto posIt = std::find_if(layout.GetElements().begin(), layout.GetElements().end(),
            [](const VertexElement& e) { return e.name == "aPos"; });

        if (posIt != layout.GetElements().end() && stride > 0)
        {
            uint32_t posOffset = posIt->offset;
            for (size_t i = 0; i < m_VertexCount; i++)
            {
                glm::vec3 pos;
                memcpy(&pos, vertices.data() + i * stride + posOffset, sizeof(glm::vec3));

                m_BoundsMin = glm::min(m_BoundsMin, pos);
                m_BoundsMax = glm::max(m_BoundsMax, pos);
            }
        }

        GenerateGLBuffers();
    }

    void GLMesh::CreateFromFBX(const ufbx_mesh *ufbx_mesh, double scene_unit_meters, ufbx_material_list &ufbx_mats, ufbx_node *mesh_node, COL_RGBA vertexColor)
    {
        // Pure CPU triangulation/dedup/tangent-computation lives in the backend-agnostic
        // BuildMeshCPUDataFromFBX (mesh.cpp) so it's reusable from the async level loader's
        // background decode workers - this just uploads the result to the GPU.
        CreateFromData(BuildMeshCPUDataFromFBX(ufbx_mesh, scene_unit_meters, ufbx_mats, mesh_node, vertexColor));
    }

    void GLMesh::CreateFromData(const MeshCPUData &data)
    {
        m_Vertices = data.vertices;
        m_Indices = data.indices;
        m_Submeshes = data.submeshes;
        m_VertexLayout = data.layout;
        m_VertexCount = data.vertices.size() / (data.layout.GetStride() ? data.layout.GetStride() : 1);
        m_BoundsMin = data.boundsMin;
        m_BoundsMax = data.boundsMax;

        GenerateGLBuffers();
    }

    GLMesh::~GLMesh()
    {
        glDeleteVertexArrays(1, &m_VAO);
        glDeleteBuffers(1, &m_VBO);
        glDeleteBuffers(1, &m_EBO);
    }

    void GLMesh::GenerateGLBuffers()
    {
        GLint previousVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVAO);

        if (m_VAO)
        {
            glDeleteVertexArrays(1, &m_VAO);
            glDeleteBuffers(1, &m_VBO);
            glDeleteBuffers(1, &m_EBO);

            m_VAO = 0;
            m_VBO = 0;
            m_EBO = 0;
        }

        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(1, &m_VBO);
        glGenBuffers(1, &m_EBO);

        glBindVertexArray(m_VAO);

        // Upload vertex data
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(
            GL_ARRAY_BUFFER,
            m_Vertices.size(),
            m_Vertices.data(),
            GL_STATIC_DRAW
        );

        // Upload index data
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            m_Indices.size() * sizeof(uint32_t),
            m_Indices.data(),
            GL_STATIC_DRAW
        );

        const auto& elements = m_VertexLayout.GetElements();
        uint32_t stride = m_VertexLayout.GetStride();

        for (const auto& element : elements)
        {
            glEnableVertexAttribArray(element.location);

            GLenum glType = GL_FLOAT;

            glType = ShaderDataTypeToOpenGL(element.type);

            glVertexAttribPointer(
                element.location,
                ShaderDataTypeComponentCount(element.type),
                glType,
                GL_FALSE,
                stride,
                (const void*)(uintptr_t)element.offset
            );

        }

        glBindVertexArray(previousVAO);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}