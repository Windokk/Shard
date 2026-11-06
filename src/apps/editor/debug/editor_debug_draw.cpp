#include "editor_debug_draw.hpp"

#define GLM_ENABLE_EXPERIMENTAL

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "engine/assets/assetID.hpp"
#include "engine/assets/resources_manager.hpp"
#include "engine/core/config/cvar.hpp"
#include "engine/physics/gravity_field.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/renderer/components/probe_volume.hpp"
#include "engine/renderer/components/reflection_probe.hpp"
#include "engine/renderer/components/volume.hpp"
#include "engine/renderer/frontend/renderer.hpp"
#include "engine/renderer/rhi/material/material.hpp"
#include "engine/renderer/rhi/render_pass.hpp"
#include "engine/renderer/rhi/resources/mesh/mesh.hpp"
#include "engine/renderer/rhi/shader/shader.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/components/transform.hpp"
#include "engine/world/engine.hpp"
#include "engine/world/time_manager.hpp"
#include "engine/world/world.hpp"
#include "engine/world/world_manager.hpp"

#include "apps/editor/debug/debug_shapes.hpp"

namespace Shard::Editor::Debug{

    namespace Rendering = Engine::Rendering;
    namespace Components = Engine::Objects::Components;

    namespace {

        constexpr int kLayerCount = 3;

        bool InterpolationEnabled()
        {
            const Engine::Core::CVarBase* cvar = Engine::Core::CVarRegistry::Global().Find("physics.interpolate");
            return !cvar || cvar->ToString() != "false";
        }

        const char* const kPassNames[kLayerCount] = { "EditorVolumeGizmoPass", "EditorProbeGizmoPass", "EditorPhysicsDebugPass" };

        constexpr uint32_t kVolumeBoxSlot = 0;
        constexpr uint32_t kVolumeProbesSlot = 1;
        constexpr uint32_t kGravityOuterSlot = 1;
        constexpr uint32_t kGravityFadeSlot = 2;
        constexpr uint64_t kSlotBits = 20;

        struct Vertex {
            glm::vec3 position;
            glm::vec2 texCoord;
            glm::vec3 normal;
            glm::vec4 color;
        };

        const COL_RGBA kWireframeColor = COL_RGBA(0.0f, 1.0f, 1.0f, 1.0f);
        const glm::vec3 kGravityArrowHalfExtent = glm::vec3(0.3f, 0.6f, 0.3f);

        glm::mat4 GravityArrowPose(const glm::vec3& position, const glm::vec3& direction)
        {
            const float length = glm::length(direction);
            const glm::vec3 dir = length > 1e-6f ? direction / length : glm::vec3(0.0f, -1.0f, 0.0f);
            return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), dir));
        }

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

            case ShapeKind::VolumeSphere:   return std::make_unique<DebugVolumeShape>(DebugVolumeShape::Kind::Sphere, spec.halfExtent, kWireframeColor);
            case ShapeKind::VolumeCylinder: return std::make_unique<DebugVolumeShape>(DebugVolumeShape::Kind::Cylinder, spec.halfExtent, kWireframeColor);
            case ShapeKind::VolumeCone:     return std::make_unique<DebugVolumeShape>(DebugVolumeShape::Kind::Cone, spec.halfExtent, kWireframeColor);
            case ShapeKind::VolumePyramid:  return std::make_unique<DebugVolumeShape>(DebugVolumeShape::Kind::Pyramid, spec.halfExtent, kWireframeColor);

            case ShapeKind::ProbeGrid:
            {
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

            // Draw commands are keyed by the mesh's asset ID.
            entry.shape->m_Mesh->SetAssetID(Engine::Core::GetEngine().GetAssetIDManager()->GenerateNewID());

            entry.spec = spec;
            entry.layer = layer;
            entry.modelID = ownerID;
            entry.commandID = Rendering::MakeCommandID(entry.shape->m_Mesh->GetAssetID().GetAsInt(), ownerID, 0);
            entry.submitted = false;
        }

        entry.seen = true;

        // A world switch wipes the draw lists, so a lost command is resubmitted. The lists belong to the render thread, never look into them from here (a lookup racing an insert crashes). The epoch tells whether a wipe happened.
        const uint64_t epoch = m_Renderer->GetContentEpoch();

        if(entry.submitted && entry.epoch == epoch && entry.model == model)
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

        // The renderer keys a command by its position in the list given, always the first here.
        m_Renderer->AddOrUpdateCommands({cmd}, {kPassNames[(int)layer]}, false);

        entry.model = model;
        entry.epoch = epoch;
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
                    glm::mat4 world = actor.transform->GetWorldMatrix();

                    // A moving body is drawn between its last two simulated poses, so its shapes follow it or they trail by up to a step.
                    Engine::Time::TimeManager* time = Engine::Core::GetEngine().GetTimeManager();
                    if(time && time->GetFixedDeltaTime() > 0.0f && Engine::Core::GetEngine().IsInPlayMode() && InterpolationEnabled())
                    {
                        glm::mat4 poseOffset;
                        if(body->GetInterpolationOffset(time->GetFixedStepAlpha(), poseOffset))
                            world = poseOffset * world;
                    }

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

                        // The mesh is built in the shape's local space. Offset and rotation go in the model matrix like the physics body does, with the actor's matrix already carrying the scale.
                        const glm::mat4 localOffset = glm::translate(glm::mat4(1.0f), body->GetShapeOffset(i)) * glm::mat4_cast(body->GetShapeRotation(i));

                        Show(DebugLayer::Physics, ownerID, (uint32_t)i, objectID, spec, world * localOffset);
                    }
                }
                else if(std::dynamic_pointer_cast<Components::GravityField>(comp))
                {
                    const glm::vec3 position = actor.transform->GetWorldPosition();
                    const glm::quat rotation = actor.transform->GetWorldRotationQuat();

                    auto sphere = [](float radius) {
                        ShapeSpec spec;
                        spec.kind = ShapeKind::Sphere;
                        spec.radius = radius;
                        return spec;
                    };
                    const glm::mat4 atActor = glm::translate(glm::mat4(1.0f), position);

                    if(auto uniform = std::dynamic_pointer_cast<Components::UniformGravityField>(comp))
                    {
                        ShapeSpec arrow;
                        arrow.kind = ShapeKind::VolumeCone;
                        arrow.halfExtent = kGravityArrowHalfExtent;
                        const glm::vec3 direction = rotation * uniform->direction * (uniform->strength < 0.0f ? -1.0f : 1.0f);
                        Show(DebugLayer::Volumes, ownerID, kVolumeBoxSlot, objectID, arrow, GravityArrowPose(position, direction));
                    }
                    else if(auto point = std::dynamic_pointer_cast<Components::PointGravityField>(comp))
                    {
                        const float radius = point->radius > 0.0f ? point->radius : 0.5f;
                        Show(DebugLayer::Volumes, ownerID, kVolumeBoxSlot, objectID, sphere(radius), atActor);

                        if(point->radius > 0.0f && point->fadeDistance > 0.0f && point->fadeDistance < point->radius)
                            Show(DebugLayer::Volumes, ownerID, kGravityFadeSlot, objectID, sphere(point->radius - point->fadeDistance), atActor);
                    }
                    else if(auto radial = std::dynamic_pointer_cast<Components::RadialGravityField>(comp))
                    {
                        Show(DebugLayer::Volumes, ownerID, kVolumeBoxSlot, objectID, sphere(radial->radius), atActor);

                        if(radial->reach > 0.0f)
                        {
                            Show(DebugLayer::Volumes, ownerID, kGravityOuterSlot, objectID, sphere(radial->radius + radial->reach), atActor);

                            if(radial->fadeDistance > 0.0f && radial->fadeDistance < radial->reach)
                                Show(DebugLayer::Volumes, ownerID, kGravityFadeSlot, objectID, sphere(radial->radius + radial->reach - radial->fadeDistance), atActor);
                        }
                    }
                    else if(auto region = std::dynamic_pointer_cast<Components::GravityVolume>(comp))
                    {
                        ShapeSpec box;
                        box.halfExtent = region->halfExtent;
                        switch(region->shape)
                        {
                            case Engine::Physics::GravityVolumeShape::Sphere:   box.kind = ShapeKind::VolumeSphere; break;
                            case Engine::Physics::GravityVolumeShape::Cylinder: box.kind = ShapeKind::VolumeCylinder; break;
                            default:                                            box.kind = ShapeKind::Box; break;
                        }

                        Show(DebugLayer::Volumes, ownerID, kVolumeBoxSlot, objectID, box, actor.transform->GetWorldMatrix());

                        if(region->directionMode == Engine::Physics::GravityVolumeDirection::Fixed)
                        {
                            ShapeSpec arrow;
                            arrow.kind = ShapeKind::VolumeCone;
                            arrow.halfExtent = kGravityArrowHalfExtent;
                            const glm::vec3 direction = rotation * region->direction * (region->strength < 0.0f ? -1.0f : 1.0f);
                            Show(DebugLayer::Volumes, ownerID, kGravityOuterSlot, objectID, arrow, GravityArrowPose(position, direction));
                        }
                    }
                }
                else if(auto volume = std::dynamic_pointer_cast<Components::Volume>(comp))
                {
                    ShapeSpec box;
                    box.kind = ShapeKind::Box;
                    box.halfExtent = volume->halfExtent;

                    if(auto probeVolume = std::dynamic_pointer_cast<Components::ProbeVolume>(comp))
                    {
                        // The probe grid is axis-aligned and centered on the actor, so it follows translation only.
                        const glm::mat4 model = glm::translate(glm::mat4(1.0f), actor.transform->GetWorldPosition());

                        ShapeSpec probes;
                        probes.kind = ShapeKind::ProbeGrid;
                        probes.halfExtent = probeVolume->halfExtent;
                        probes.probeCounts = probeVolume->probeCounts;

                        Show(DebugLayer::Volumes, ownerID, kVolumeBoxSlot, objectID, box, model);
                        Show(DebugLayer::Probes, ownerID, kVolumeProbesSlot, objectID, probes, model);
                    }
                    else if(auto reflectionProbe = std::dynamic_pointer_cast<Components::ReflectionProbe>(comp))
                    {
                        switch(reflectionProbe->shape)
                        {
                            case Engine::Rendering::ReflectionProbeShape::Sphere:   box.kind = ShapeKind::VolumeSphere; break;
                            case Engine::Rendering::ReflectionProbeShape::Cylinder: box.kind = ShapeKind::VolumeCylinder; break;
                            case Engine::Rendering::ReflectionProbeShape::Cone:     box.kind = ShapeKind::VolumeCone; break;
                            case Engine::Rendering::ReflectionProbeShape::Pyramid:  box.kind = ShapeKind::VolumePyramid; break;
                            default: break;
                        }

                        Show(DebugLayer::Volumes, ownerID, kVolumeBoxSlot, objectID, box, glm::translate(glm::mat4(1.0f), actor.transform->GetWorldPosition()));
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
