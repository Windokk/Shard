#include "editor_debug_draw.hpp"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/assets/assetID.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/volume.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"

#include "apps/editor/debug/debug_shapes.hpp"

namespace Shard::Editor::Debug{

    namespace Rendering = Engine::Rendering;
    namespace Components = Engine::Objects::Components;

    namespace {

        constexpr int kLayerCount = 3;

        // The passes the shapes are drawn in, on top of the scene, in the viewport framebuffer
        const char* const kPassNames[kLayerCount] = { "EditorVolumeGizmoPass", "EditorProbeGizmoPass", "EditorPhysicsDebugPass" };

        // A component draws several things : a slot tells them apart (a physics body has one per shape, a probe volume
        // has its box and its probes)
        constexpr uint32_t kVolumeBoxSlot = 0;
        constexpr uint32_t kVolumeProbesSlot = 1;
        constexpr uint64_t kSlotBits = 20;

        // The layout of the vertices of the meshes DebugShape builds
        struct Vertex {
            glm::vec3 position;
            glm::vec2 texCoord;
            glm::vec3 normal;
            glm::vec4 color;
        };

        const COL_RGBA kWireframeColor = COL_RGBA(0.0f, 1.0f, 1.0f, 1.0f);
        const COL_RGBA kProbeColor = COL_RGBA(1.0f, 1.0f, 1.0f, 1.0f);
        constexpr float kProbeRadius = 0.08f;
    }

    EditorDebugDraw::EditorDebugDraw() = default;

    EditorDebugDraw::~EditorDebugDraw() = default;

    const char* EditorDebugDraw::PassOf(DebugLayer layer)
    {
        return kPassNames[(int)layer];
    }

    void EditorDebugDraw::Init()
    {
        if(m_Material)
            return;

        m_Renderer = Engine::Core::GetEngine().GetRenderer();

        // The shapes are lines : same unlit shader as a mesh, rasterized as lines
        Rendering::VertexLayout vertexLayout = {{{"aPos", Rendering::ShaderDataType::Vec3, 0, offsetof(Vertex, position)},{"aTexCoord", Rendering::ShaderDataType::Vec2, 1, offsetof(Vertex, texCoord)},
                                                {"aNormal", Rendering::ShaderDataType::Vec3, 2, offsetof(Vertex, normal)}, {"aColor", Rendering::ShaderDataType::Vec4, 3, offsetof(Vertex, color)}},
                                                    sizeof(Vertex)};

        std::shared_ptr<Rendering::Shader> shader = Engine::Core::GetEngine().GetResourcesManager()->Get<Rendering::Shader>(Engine::Core::Resources::AssetKind::Shader, "shaders/mesh/unlit");

        Rendering::PipelineSpecifications pipelineSpecs;
        pipelineSpecs.blending = false;
        pipelineSpecs.cullMode = Rendering::CullMode::None;
        pipelineSpecs.debugName = "EditorDebugDraw";
        pipelineSpecs.polygonMode = Rendering::PolygonMode::Line;
        pipelineSpecs.topology = Rendering::PrimitiveTopology::Lines;
        pipelineSpecs.shader = shader;
        pipelineSpecs.vertexLayout = vertexLayout;

        std::shared_ptr<Rendering::Pipeline> pipeline = m_Renderer->GetOrAddPipeline(pipelineSpecs);

        m_Material = Rendering::Material::Create(shader, pipeline, false, Rendering::Opacity::Opaque);
        m_Material->SetScalarParameter("useTexture", false);

        for(int i = 0; i < kLayerCount; i++)
        {
            std::shared_ptr<Rendering::RenderPass> pass = std::make_shared<Rendering::RenderPass>();
            pass->target = m_Renderer->GetViewportFramebuffer();
            pass->clearColor = false;
            pass->clearDepth = false;
            pass->overridePipeline = false;
            m_Renderer->AddRenderPass(pass, kPassNames[i], {"ForwardPass"});
            m_Passes[i] = pass;
        }
    }

    void EditorDebugDraw::SetLayerVisible(DebugLayer layer, bool visible)
    {
        if(m_Passes[(int)layer])
            m_Passes[(int)layer]->enabled = visible;
    }

    std::unique_ptr<DebugShape> EditorDebugDraw::BuildShape(const ShapeSpec& spec) const
    {
        switch(spec.kind){
            case ShapeKind::Sphere:   return std::make_unique<DebugSphere>(spec.radius, kWireframeColor);
            case ShapeKind::Box:      return std::make_unique<DebugBox>(spec.halfExtent, kWireframeColor);
            case ShapeKind::Capsule:  return std::make_unique<DebugCapsule>(spec.radius, spec.halfHeight, kWireframeColor);
            case ShapeKind::Cylinder: return std::make_unique<DebugCylinder>(spec.radius, spec.halfHeight, kWireframeColor);

            case ShapeKind::ProbeGrid:
            {
                // One small sphere per probe, in the space of the volume (see ProbeVolume::GetGridOrigin)
                const glm::ivec3 counts = glm::max(spec.probeCounts, glm::ivec3(1));
                const glm::vec3 spacing = (spec.halfExtent * 2.0f) / glm::vec3(counts);
                const glm::vec3 localOrigin = -spec.halfExtent + spacing * 0.5f;

                std::vector<glm::vec3> centers;
                centers.reserve((size_t)counts.x * (size_t)counts.y * (size_t)counts.z);
                for (int z = 0; z < counts.z; z++)
                    for (int y = 0; y < counts.y; y++)
                        for (int x = 0; x < counts.x; x++)
                            centers.push_back(localOrigin + spacing * glm::vec3((float)x, (float)y, (float)z));

                return std::make_unique<DebugMultiSphere>(centers, kProbeRadius, kProbeColor);
            }
        }

        return nullptr;
    }

    void EditorDebugDraw::Show(DebugLayer layer, uint32_t ownerID, uint32_t slot, uint32_t objectID, const ShapeSpec& spec, const glm::mat4& model)
    {
        const uint64_t key = ((uint64_t)ownerID << kSlotBits) | slot;
        Entry& entry = m_Entries[key];

        // What it shows changed : the old mesh goes (its command holds it), the new one is built below
        if(entry.shape && entry.spec != spec)
        {
            Remove(entry);
            entry.shape.reset();
        }

        if(!entry.shape)
        {
            entry.shape = BuildShape(spec);
            if(!entry.shape || !entry.shape->m_Mesh)
            {
                entry.shape.reset();
                entry.seen = true;
                return;
            }

            // The draw commands are keyed by the mesh's asset ID
            entry.shape->m_Mesh->SetAssetID(Engine::Core::GetEngine().GetAssetIDManager()->GenerateNewID());

            entry.spec = spec;
            entry.layer = layer;
            entry.modelID = ownerID;
            entry.commandID = Rendering::MakeCommandID(entry.shape->m_Mesh->GetAssetID().GetAsInt(), ownerID, 0);
            entry.submitted = false;
        }

        entry.seen = true;

        // A world switch wipes the draw lists (see Renderer::ClearPassesContent) : a command that is gone is submitted again
        const std::shared_ptr<Rendering::RenderPass>& pass = m_Passes[(int)layer];
        const bool present = pass && pass->drawCommandsLookup.find(entry.commandID) != pass->drawCommandsLookup.end();

        if(entry.submitted && present && entry.model == model)
            return;

        const std::shared_ptr<Rendering::Mesh>& mesh = entry.shape->m_Mesh;

        Rendering::DrawCommand cmd = {};
        cmd.boundsMax = mesh->GetBoundsMax();
        cmd.boundsMin = mesh->GetBoundsMin();
        cmd.indexCount = mesh->GetIndexCount();
        cmd.indexOffset = 0;
        cmd.material = m_Material;
        cmd.mesh = mesh;
        cmd.modelID = ownerID;
        cmd.modelMatrix = model;
        cmd.objectID = objectID;
        cmd.vertexCount = mesh->GetVertexCount();

        // The renderer keys a command by its position in the list it is given : always the first
        m_Renderer->AddOrUpdateCommands({cmd}, {kPassNames[(int)layer]}, false);

        entry.model = model;
        entry.submitted = true;
    }

    void EditorDebugDraw::Remove(Entry& entry)
    {
        if(entry.submitted)
            m_Renderer->RemoveCommands({entry.commandID}, {kPassNames[(int)entry.layer]}, false);

        entry.submitted = false;
    }

    void EditorDebugDraw::VisitActor(Engine::Objects::Actor& actor)
    {
        if(actor.transform)
        {
            for(const std::shared_ptr<Components::Component>& comp : actor.GetComponents())
            {
                if(!comp || !comp->Active())
                    continue;

                const uint32_t ownerID = (uint32_t)actor.GetComponentIDInWorld(comp->GetLocalId());
                const uint32_t objectID = (uint32_t)actor.GetID().GetAsInt();

                if(auto body = std::dynamic_pointer_cast<Components::PhysicsBody>(comp))
                {
                    const glm::mat4 world = actor.transform->GetWorldMatrix();

                    for(size_t i = 0; i < body->GetShapeCount(); i++)
                    {
                        ShapeSpec spec;

                        switch(body->GetShapeType(i)){
                            case Engine::Physics::PhysicsShape::SPHERE:
                                spec.kind = ShapeKind::Sphere;
                                spec.radius = body->GetShapeParams<Components::SphereParams>(i).radius;
                                break;
                            case Engine::Physics::PhysicsShape::BOX:
                                spec.kind = ShapeKind::Box;
                                spec.halfExtent = body->GetShapeParams<Components::BoxParams>(i).halfExtent;
                                break;
                            case Engine::Physics::PhysicsShape::CAPSULE:
                                spec.kind = ShapeKind::Capsule;
                                spec.radius = body->GetShapeParams<Components::CapsuleParams>(i).radius;
                                spec.halfHeight = body->GetShapeParams<Components::CapsuleParams>(i).halfHeight;
                                break;
                            case Engine::Physics::PhysicsShape::CYLINDER:
                                spec.kind = ShapeKind::Cylinder;
                                spec.radius = body->GetShapeParams<Components::CylinderParams>(i).radius;
                                spec.halfHeight = body->GetShapeParams<Components::CylinderParams>(i).halfHeight;
                                break;
                            default:
                                continue;
                        }

                        // A shape's mesh is built in its own local space (unscaled, uncentered) : its offset and rotation
                        // go in the model matrix on top of the actor's transform, like the physics places the shape in its
                        // body (where the offset is pre-scaled - here the actor's matrix already carries that scale)
                        const glm::mat4 localOffset = glm::translate(glm::mat4(1.0f), body->GetShapeOffset(i)) * glm::mat4_cast(body->GetShapeRotation(i));

                        Show(DebugLayer::Physics, ownerID, (uint32_t)i, objectID, spec, world * localOffset);
                    }
                }
                else if(auto volume = std::dynamic_pointer_cast<Components::Volume>(comp))
                {
                    ShapeSpec box;
                    box.kind = ShapeKind::Box;
                    box.halfExtent = volume->halfExtent;

                    if(auto probeVolume = std::dynamic_pointer_cast<Components::ProbeVolume>(comp))
                    {
                        // The probe grid is axis-aligned and centered on the actor : it follows its translation only, and the
                        // box and the probes preview that instead of the actor's full transform (see ProbeVolume)
                        const glm::mat4 model = glm::translate(glm::mat4(1.0f), actor.transform->GetWorldPosition());

                        ShapeSpec probes;
                        probes.kind = ShapeKind::ProbeGrid;
                        probes.halfExtent = probeVolume->halfExtent;
                        probes.probeCounts = probeVolume->probeCounts;

                        Show(DebugLayer::Volumes, ownerID, kVolumeBoxSlot, objectID, box, model);
                        Show(DebugLayer::Probes, ownerID, kVolumeProbesSlot, objectID, probes, model);
                    }
                    else
                    {
                        Show(DebugLayer::Volumes, ownerID, kVolumeBoxSlot, objectID, box, actor.transform->GetWorldMatrix());
                    }
                }
            }
        }

        for(Engine::Core::ObjectID childID : actor.GetChildrenID(false))
        {
            if(auto child = std::dynamic_pointer_cast<Engine::Objects::Actor>(actor.GetChild(childID)))
                VisitActor(*child);
        }
    }

    void EditorDebugDraw::Update()
    {
        if(!m_Material)
            return;

        for(auto& [key, entry] : m_Entries)
            entry.seen = false;

        Engine::Worlds::WorldManager* worlds = Engine::Core::GetEngine().GetWorldManager();
        for(int i = 0; i < worlds->GetLoadedWorldCount(); i++)
        {
            Engine::Worlds::World* world = worlds->GetWorldAt(i);
            if(!world || !world->IsLoaded())
                continue;

            for(auto& [id, root] : world->GetRootActors())
            {
                if(root)
                    VisitActor(*root);
            }
        }

        // What nothing shows any more : the component is gone, deactivated, or its world unloaded
        for(auto it = m_Entries.begin(); it != m_Entries.end(); )
        {
            if(it->second.seen)
            {
                ++it;
                continue;
            }

            Remove(it->second);
            it = m_Entries.erase(it);
        }
    }

    void EditorDebugDraw::Clear()
    {
        for(auto& [key, entry] : m_Entries)
            Remove(entry);

        m_Entries.clear();
    }
}
