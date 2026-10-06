#include "debug_draw.hpp"

#include "engine/assets/assetID.hpp"
#include "engine/renderer/features/debug/debug_shapes.hpp"
#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"

namespace Shard::Engine::Rendering{

    DebugDraw::DebugDraw(IRenderContext& renderer, Filesystem::AssetIDManager& assetIDs)
        : m_Renderer(&renderer), m_AssetIDs(&assetIDs)
    {
    }

    DebugDraw::~DebugDraw() = default;

    const char* DebugDraw::PassOf(Core::DebugDrawLayer layer)
    {
        switch(layer){
            case Core::DebugDrawLayer::Physics: return "PhysicsDebugPass";
        }
        return "PhysicsDebugPass";
    }

    Core::DebugShapeHandle DebugDraw::CreateShape(const Core::DebugShapeDesc& desc)
    {
        std::unique_ptr<DebugShape> shape;

        switch(desc.kind){
            case Core::DebugShapeKind::Sphere:   shape = std::make_unique<DebugSphere>(desc.radius, desc.color); break;
            case Core::DebugShapeKind::Box:      shape = std::make_unique<DebugBox>(desc.halfExtent, desc.color); break;
            case Core::DebugShapeKind::Capsule:  shape = std::make_unique<DebugCapsule>(desc.radius, desc.halfHeight, desc.color); break;
            case Core::DebugShapeKind::Cylinder: shape = std::make_unique<DebugCylinder>(desc.radius, desc.halfHeight, desc.color); break;
        }

        if(!shape)
            return 0;

        // The draw commands are keyed by the mesh's asset ID
        if(shape->m_Mesh)
            shape->m_Mesh->SetAssetID(m_AssetIDs->GenerateNewID());

        const Core::DebugShapeHandle handle = m_NextHandle++;
        Entry entry;
        entry.shape = std::move(shape);
        m_Shapes.emplace(handle, std::move(entry));
        return handle;
    }

    void DebugDraw::DestroyShape(Core::DebugShapeHandle handle)
    {
        auto it = m_Shapes.find(handle);
        if(it == m_Shapes.end())
            return;

        Entry& entry = it->second;
        if(entry.drawn && entry.shape->m_Mesh)
        {
            uint64_t cmdID = MakeCommandID(entry.shape->m_Mesh->GetAssetID().GetAsInt(), entry.ownerID, entry.submeshID);
            m_Renderer->RemoveCommands({cmdID}, {PassOf(entry.layer)}, false);
        }

        m_Shapes.erase(it);
    }

    void DebugDraw::Draw(Core::DebugDrawLayer layer, const std::vector<Core::DebugDrawItem>& items)
    {
        std::vector<DrawCommand> cmds;
        cmds.reserve(items.size());

        for(const Core::DebugDrawItem& item : items)
        {
            auto it = m_Shapes.find(item.shape);
            if(it == m_Shapes.end() || !it->second.shape->m_Mesh)
                continue;

            Entry& entry = it->second;
            const std::shared_ptr<Mesh>& mesh = entry.shape->m_Mesh;

            DrawCommand cmd = {};
            cmd.boundsMax = mesh->GetBoundsMax();
            cmd.boundsMin = mesh->GetBoundsMin();
            cmd.indexCount = mesh->GetIndexCount();
            cmd.indexOffset = 0;
            cmd.material = m_Renderer->GetDebugMaterial();
            cmd.mesh = mesh;
            cmd.modelID = item.ownerID;
            cmd.modelMatrix = item.model;
            cmd.objectID = item.objectID;
            cmd.vertexCount = mesh->GetVertexCount();

            // The renderer keys a command by its position in the list it is given
            entry.drawn = true;
            entry.ownerID = item.ownerID;
            entry.submeshID = (int)cmds.size();
            entry.layer = layer;

            cmds.push_back(cmd);
        }

        if(!cmds.empty())
            m_Renderer->AddOrUpdateCommands(cmds, {PassOf(layer)}, false);
    }
}
