#include "volume.hpp"

#include "engine/world/actor.hpp"
#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/world/engine.hpp"

#include "engine/renderer/rhi/render_context.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/features/debug/debug_shapes.hpp"

#include "engine/assets/assetID.hpp"

namespace Shard::Engine::Objects::Components{

    Volume::Volume(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

    glm::mat4 Volume::GetDebugModelMatrix() const
    {
        return parent->transform->GetWorldMatrix();
    }

    void Volume::RebuildDebugShape()
    {
        if (!activated || !parent)
            return;

        Filesystem::AssetID previousDebugMeshID;
        if (m_DebugShape && m_DebugShape->m_Mesh)
            previousDebugMeshID = m_DebugShape->m_Mesh->GetAssetID();

        delete m_DebugShape;
        m_DebugShape = new Rendering::DebugBox(halfExtent, GetWireframeColor());

        if (previousDebugMeshID.GetAsInt() != 0)
            m_DebugShape->m_Mesh->SetAssetID(previousDebugMeshID);
        else
            m_DebugShape->m_Mesh->SetAssetID(GetEngineContext()->GetAssetIDManager()->GenerateNewID());

        RefreshDebugDrawCommands();
    }

    void Volume::OnTransformChanged(uint8_t)
    {
        RefreshDebugDrawCommands();
    }

    void Volume::RefreshDebugDrawCommands()
    {
        if (!m_DebugShape || !m_DebugShape->m_Mesh || !parent || !parent->world || !parent->world->IsLoaded())
            return;

        Rendering::DrawCommand cmd = {};

        cmd.boundsMax = m_DebugShape->m_Mesh->GetBoundsMax();
        cmd.boundsMin = m_DebugShape->m_Mesh->GetBoundsMin();
        cmd.indexCount = m_DebugShape->m_Mesh->GetIndexCount();
        cmd.indexOffset = 0;
        cmd.material = GetEngineContext()->GetRenderContext()->GetDebugMaterial();
        cmd.mesh = m_DebugShape->m_Mesh;
        cmd.modelID = parent->GetComponentIDInWorld(local_id);
        cmd.modelMatrix = GetDebugModelMatrix();
        cmd.objectID = parent->GetID().GetAsInt();
        cmd.vertexCount = m_DebugShape->m_Mesh->GetVertexCount();

        GetEngineContext()->GetRenderContext()->AddOrUpdateCommands({cmd}, {"ForwardPass"}, false);
    }

    void Volume::RemoveDebugShape()
    {
        if (m_DebugShape && m_DebugShape->m_Mesh && parent)
        {
            uint64_t cmdID = Rendering::MakeCommandID(m_DebugShape->m_Mesh->GetAssetID().GetAsInt(), parent->GetComponentIDInWorld(local_id), 0);
            GetEngineContext()->GetRenderContext()->RemoveCommands({cmdID}, {"ForwardPass"}, false);
        }

        delete m_DebugShape;
        m_DebugShape = nullptr;
    }

    void Volume::Activate()
    {
        Component::Activate();

        RebuildDebugShape();
    }

    void Volume::DeActivate()
    {
        Component::DeActivate();
    }

    void Volume::Destroy()
    {
        RemoveDebugShape();
    }

    void Volume::OnFieldChanged(const FieldChangedEvent &event)
    {
        if (std::string(event.field->name) == "halfExtent")
            RebuildDebugShape();
    }

}
