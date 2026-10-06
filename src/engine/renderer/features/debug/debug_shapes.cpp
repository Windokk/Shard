#include "debug_shapes.hpp"

#include <glm/gtc/constants.hpp>

#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"

namespace Shard::Engine::Rendering {

    struct Vertex {
        glm::vec3 position;
        glm::vec2 texCoord;
        glm::vec3 normal;
        glm::vec4 color;
    };

    void DebugBox::GenerateBox(glm::vec3 halfExtent, COL_RGBA color)
    {
        std::vector<glm::vec3> localCorners = {
            {-halfExtent.x, -halfExtent.y, -halfExtent.z},
            { halfExtent.x, -halfExtent.y, -halfExtent.z},
            { halfExtent.x,  halfExtent.y, -halfExtent.z},
            {-halfExtent.x,  halfExtent.y, -halfExtent.z},
            {-halfExtent.x, -halfExtent.y,  halfExtent.z},
            { halfExtent.x, -halfExtent.y,  halfExtent.z},
            { halfExtent.x,  halfExtent.y,  halfExtent.z},
            {-halfExtent.x,  halfExtent.y,  halfExtent.z}
        };

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        vertices.resize(8);
        for (int i = 0; i < 8; ++i) {
            glm::vec3 pos = localCorners[i];
            vertices[i].position = pos;
            vertices[i].normal = glm::normalize(pos);
            vertices[i].color = color;
            vertices[i].texCoord = glm::vec2(0,0);
        }

        indices = {
            0,1, 1,2, 2,3, 3,0, // bottom square edges
            4,5, 5,6, 6,7, 7,4, // top square edges
            0,4, 1,5, 2,6, 3,7  // vertical edgess
        };

        std::vector<uint8_t> vertexBuffer;
        vertexBuffer.resize(vertices.size() * sizeof(Vertex));
        memcpy(vertexBuffer.data(), vertices.data(), vertexBuffer.size());

        m_Mesh = Mesh::Create();
        m_Mesh->Create(vertexBuffer, indices, {{
            {"aPos",     ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
            {"aTexCoord",ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
            {"aNormal",  ShaderDataType::Vec3, 2, offsetof(Vertex, normal)},
            {"aColor",   ShaderDataType::Vec4, 3, offsetof(Vertex, color)},
        }, sizeof(Vertex)});
    }

    void DebugSphere::GenerateSphere(float radius, COL_RGBA color)
    {
        int segments = 32;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        for (int i = 0; i < segments; i++)
        {
            float angle = glm::two_pi<float>() * i / segments;
            float c = cos(angle);
            float s = sin(angle);

            // XY ring
            vertices.push_back({ glm::vec3(radius * c, radius * s, 0), glm::vec2(0), glm::vec3(0), color });

            // XZ ring
            vertices.push_back({ glm::vec3(radius * c, 0, radius * s), glm::vec2(0), glm::vec3(0), color });

            // YZ ring
            vertices.push_back({ glm::vec3(0, radius * c, radius * s), glm::vec2(0), glm::vec3(0), color });
        }

        for (int i = 0; i < segments; i++)
        {
            int next = (i + 1) % segments;

            int xy0 = i * 3;
            int xy1 = next * 3;

            int xz0 = i * 3 + 1;
            int xz1 = next * 3 + 1;

            int yz0 = i * 3 + 2;
            int yz1 = next * 3 + 2;

            // XY
            indices.push_back(xy0);
            indices.push_back(xy1);

            // XZ
            indices.push_back(xz0);
            indices.push_back(xz1);

            // YZ
            indices.push_back(yz0);
            indices.push_back(yz1);
        }

        std::vector<uint8_t> vertexBuffer;
        vertexBuffer.resize(vertices.size() * sizeof(Vertex));
        memcpy(vertexBuffer.data(), vertices.data(), vertexBuffer.size());

        m_Mesh = Mesh::Create();
        m_Mesh->Create(vertexBuffer, indices, {{
            {"aPos",     ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
            {"aTexCoord",ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
            {"aNormal",  ShaderDataType::Vec3, 2, offsetof(Vertex, normal)},
            {"aColor",   ShaderDataType::Vec4, 3, offsetof(Vertex, color)},
        }, sizeof(Vertex)});
    }

    void DebugCapsule::GenerateCapsule(float radius, float halfHeight, COL_RGBA color)
    {
        int segments = 24;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        // --- circles (top & bottom) ---
        for (int i = 0; i < segments; i++)
        {
            float angle = glm::two_pi<float>() * i / segments;
            float c = cos(angle);
            float s = sin(angle);

            glm::vec3 dir(c * radius, 0, s * radius);

            // bottom ring
            vertices.push_back({ dir + glm::vec3(0,-halfHeight,0), glm::vec2(0), glm::vec3(0), color });

            // top ring
            vertices.push_back({ dir + glm::vec3(0, halfHeight,0), glm::vec2(0), glm::vec3(0), color });
        }

        for (int i = 0; i < segments; i++)
        {
            int next = (i + 1) % segments;

            // bottom circle
            indices.push_back(i*2);
            indices.push_back(next*2);

            // top circle
            indices.push_back(i*2+1);
            indices.push_back(next*2+1);
        }

        // --- vertical lines ---
        vertices.push_back({ glm::vec3(radius,-halfHeight,0), glm::vec2(0), glm::vec3(0), color });
        vertices.push_back({ glm::vec3(radius, halfHeight,0), glm::vec2(0), glm::vec3(0), color });

        vertices.push_back({ glm::vec3(-radius,-halfHeight,0), glm::vec2(0), glm::vec3(0), color });
        vertices.push_back({ glm::vec3(-radius, halfHeight,0), glm::vec2(0), glm::vec3(0), color });

        vertices.push_back({ glm::vec3(0,-halfHeight,radius), glm::vec2(0), glm::vec3(0), color });
        vertices.push_back({ glm::vec3(0, halfHeight,radius), glm::vec2(0), glm::vec3(0), color });

        vertices.push_back({ glm::vec3(0,-halfHeight,-radius), glm::vec2(0), glm::vec3(0), color });
        vertices.push_back({ glm::vec3(0, halfHeight,-radius), glm::vec2(0), glm::vec3(0), color });

        int base = vertices.size() - 8;

        for(int i = 0; i < 8; i += 2)
        {
            indices.push_back(base + i);
            indices.push_back(base + i + 1);
        }

        // half circles (caps)

        int arc_segments = segments / 2;

        auto addHalfCircle = [&](glm::vec3 center, bool topCap, bool xAxis)
        {
            int start = vertices.size();

            for(int i = 0; i <= arc_segments; i++)
            {
                float angle = glm::pi<float>() * i / arc_segments;

                float x = cos(angle) * radius;
                float y = sin(angle) * radius;

                if(!topCap) y = -y;

                glm::vec3 p;

                if(xAxis)
                    p = center + glm::vec3(x, y, 0);
                else
                    p = center + glm::vec3(0, y, x);

                vertices.push_back({ p, glm::vec2(0), glm::vec3(0), color });

                if(i > 0)
                {
                    indices.push_back(start + i - 1);
                    indices.push_back(start + i);
                }
            }
        };

        // top hemisphere arcs
        addHalfCircle(glm::vec3(0, halfHeight, 0), true, true);
        addHalfCircle(glm::vec3(0, halfHeight, 0), true, false);

        // bottom hemisphere arcs
        addHalfCircle(glm::vec3(0,-halfHeight, 0), false, true);
        addHalfCircle(glm::vec3(0,-halfHeight, 0), false, false);

        std::vector<uint8_t> vertexBuffer;
        vertexBuffer.resize(vertices.size() * sizeof(Vertex));
        memcpy(vertexBuffer.data(), vertices.data(), vertexBuffer.size());

        m_Mesh = Mesh::Create();
        m_Mesh->Create(vertexBuffer, indices, {{
            {"aPos",     ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
            {"aTexCoord",ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
            {"aNormal",  ShaderDataType::Vec3, 2, offsetof(Vertex, normal)},
            {"aColor",   ShaderDataType::Vec4, 3, offsetof(Vertex, color)},
        }, sizeof(Vertex)});
    }

    void DebugCylinder::GenerateCylinder(float radius, float halfHeight, COL_RGBA color)
    {
        int segments = 18; // minimal segment count for wireframe

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        // Vertex generation (top and bottom circles)
        for (int i = 0; i < segments; ++i) {
            float angle = glm::two_pi<float>() * i / segments;
            float x = cos(angle), z = sin(angle);
            glm::vec3 dir = glm::vec3(x, 0, z);

            // bottom ring
            vertices.push_back({ dir * radius + glm::vec3(0, -halfHeight, 0), glm::vec2(0,0), dir, color });
            // top ring
            vertices.push_back({ dir * radius + glm::vec3(0, +halfHeight, 0), glm::vec2(0,0), dir, color });
        }

        // Index generation
        for (int i = 0; i < segments; ++i) {
            int next = (i + 1) % segments;

            int b0 = i * 2;       // bottom current
            int t0 = b0 + 1;      // top current
            int b1 = next * 2;    // bottom next
            int t1 = b1 + 1;      // top next

            // Bottom circle
            indices.push_back(b0);
            indices.push_back(b1);

            // Top circle
            indices.push_back(t0);
            indices.push_back(t1);

            // Vertical lines
            indices.push_back(b0);
            indices.push_back(t0);
        }
    
        std::vector<uint8_t> vertexBuffer;
        vertexBuffer.resize(vertices.size() * sizeof(Vertex));
        memcpy(vertexBuffer.data(), vertices.data(), vertexBuffer.size());

        m_Mesh = Mesh::Create();
        m_Mesh->Create(vertexBuffer, indices, {{
            {"aPos",     ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
            {"aTexCoord",ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
            {"aNormal",  ShaderDataType::Vec3, 2, offsetof(Vertex, normal)},
            {"aColor",   ShaderDataType::Vec4, 3, offsetof(Vertex, color)},
        }, sizeof(Vertex)});
    }

    void DebugMultiSphere::GenerateMultiSphere(const std::vector<glm::vec3>& centers, float radius, COL_RGBA color)
    {
        // Fewer segments than DebugSphere's 32 - these are meant to be small, cheap markers, and there
        // can be hundreds of them in a single merged mesh (one per probe in a GI probe grid).
        int segments = 8;

        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(centers.size() * segments * 3);
        indices.reserve(centers.size() * segments * 6);

        for (const glm::vec3& center : centers)
        {
            uint32_t base = (uint32_t)vertices.size();

            for (int i = 0; i < segments; i++)
            {
                float angle = glm::two_pi<float>() * i / segments;
                float c = cos(angle);
                float s = sin(angle);

                // XY ring
                vertices.push_back({ center + glm::vec3(radius * c, radius * s, 0), glm::vec2(0), glm::vec3(0), color });
                // XZ ring
                vertices.push_back({ center + glm::vec3(radius * c, 0, radius * s), glm::vec2(0), glm::vec3(0), color });
                // YZ ring
                vertices.push_back({ center + glm::vec3(0, radius * c, radius * s), glm::vec2(0), glm::vec3(0), color });
            }

            for (int i = 0; i < segments; i++)
            {
                int next = (i + 1) % segments;

                uint32_t xy0 = base + i * 3, xy1 = base + next * 3;
                uint32_t xz0 = base + i * 3 + 1, xz1 = base + next * 3 + 1;
                uint32_t yz0 = base + i * 3 + 2, yz1 = base + next * 3 + 2;

                indices.push_back(xy0);
                indices.push_back(xy1);

                indices.push_back(xz0);
                indices.push_back(xz1);

                indices.push_back(yz0);
                indices.push_back(yz1);
            }
        }

        std::vector<uint8_t> vertexBuffer;
        vertexBuffer.resize(vertices.size() * sizeof(Vertex));
        if (!vertices.empty())
            memcpy(vertexBuffer.data(), vertices.data(), vertexBuffer.size());

        m_Mesh = Mesh::Create();
        m_Mesh->Create(vertexBuffer, indices, {{
            {"aPos",     ShaderDataType::Vec3, 0, offsetof(Vertex, position)},
            {"aTexCoord",ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
            {"aNormal",  ShaderDataType::Vec3, 2, offsetof(Vertex, normal)},
            {"aColor",   ShaderDataType::Vec4, 3, offsetof(Vertex, color)},
        }, sizeof(Vertex)});
    }

    DebugShape::~DebugShape()
    {

    }

}