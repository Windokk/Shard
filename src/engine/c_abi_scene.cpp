#include <algorithm>
#include <limits>

#include <glm/glm.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/renderer/components/model_component.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

// What the C ABI asks of the scene that the plugin layer cannot see : the renderer's meshes and the physics bodies of an actor are above it.

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        struct Box {
            glm::vec3 lo = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 hi = glm::vec3(std::numeric_limits<float>::lowest());
            bool any = false;

            void Add(const glm::vec3& p)
            {
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
                any = true;
            }
        };

        void Collect(const std::shared_ptr<Objects::Actor>& actor, Box& box, int depth)
        {
            if (!actor || depth > 64) return;

            if (actor->transform) {
                for (const auto& model : actor->GetComponents<Objects::Components::Model>()) {
                    const std::shared_ptr<Rendering::Mesh>& mesh = model->MeshRef();
                    if (!mesh) continue;
                    const glm::vec3 lo = mesh->GetBoundsMin(), hi = mesh->GetBoundsMax();
                    const glm::mat4 world = actor->transform->GetWorldMatrix();
                    for (int corner = 0; corner < 8; ++corner) {
                        const glm::vec3 p((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y, (corner & 4) ? hi.z : lo.z);
                        box.Add(glm::vec3(world * glm::vec4(p, 1.0f)));
                    }
                }
            }

            Core::IEngineContext* engine = actor->GetEngineContext();
            Physics::PhysicsManager* physics = engine ? engine->GetPhysicsManager() : nullptr;
            if (physics) {
                for (const auto& body : actor->GetComponents<Objects::Components::PhysicsBody>()) {
                    if (body->GetBodyID().IsInvalid()) continue;
                    const JPH::AABox bounds = physics->GetPhysicsSystem().GetBodyInterface().GetTransformedShape(body->GetBodyID()).GetWorldSpaceBounds();
                    box.Add(ToGLM(bounds.mMin));
                    box.Add(ToGLM(bounds.mMax));
                }
            }

            Core::ObjectIDManager* objects = engine ? engine->GetObjectIDManager() : nullptr;
            if (!objects) return;
            for (const Core::ObjectID& id : actor->GetChildrenID())
                Collect(std::dynamic_pointer_cast<Objects::Actor>(objects->GetObjectFromID(id)), box, depth + 1);
        }
    }

    bool ActorBounds(const std::shared_ptr<Objects::Actor>& actor, float outMin[3], float outMax[3])
    {
        Box box;
        Collect(actor, box, 0);
        if (!box.any) return false;
        for (int i = 0; i < 3; ++i) { outMin[i] = box.lo[i]; outMax[i] = box.hi[i]; }
        return true;
    }
}
