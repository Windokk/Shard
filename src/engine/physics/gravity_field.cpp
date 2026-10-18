#include "gravity_field.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/components/transform.hpp"

#include "gravity_field.reflection.hpp"

namespace Shard::Engine::Objects::Components{

    GravityField::GravityField(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

    Physics::GravityFieldData GravityField::BuildData()
    {
        Physics::GravityFieldData data;
        data.blend = blend;
        data.priority = priority;
        data.strength = strength;

        // Jolt, and so the simulation, is in the root frame : the pose of the actor is taken there, in double
        if (parent && parent->transform)
        {
            data.position = parent->transform->GetWorldPositionD();
            data.rotation = parent->transform->GetWorldRotationD();
            data.scale = parent->transform->GetWorldScale();
        }

        Fill(data);
        return data;
    }

    bool GravityField::Sample(const glm::dvec3& position, glm::vec3& acceleration, float& coverage)
    {
        return Physics::EvaluateGravityField(BuildData(), position, acceleration, coverage);
    }

    // ---------------- Uniform ----------------

    UniformGravityField::UniformGravityField(std::shared_ptr<Actor> parent, uint32_t local_id) : GravityField(parent, local_id)
    {
    }

    ordered_json UniformGravityField::Serialize()
    {
        return SerializeReflected("gravityUniform");
    }

    std::shared_ptr<Component> UniformGravityField::Clone() const
    {
        return Object::Create<UniformGravityField>(*this);
    }

    void UniformGravityField::Fill(Physics::GravityFieldData& data)
    {
        data.kind = Physics::GravityFieldKind::Uniform;
        data.direction = direction;
    }

    // ---------------- Point ----------------

    PointGravityField::PointGravityField(std::shared_ptr<Actor> parent, uint32_t local_id) : GravityField(parent, local_id)
    {
    }

    ordered_json PointGravityField::Serialize()
    {
        return SerializeReflected("gravityPoint");
    }

    std::shared_ptr<Component> PointGravityField::Clone() const
    {
        return Object::Create<PointGravityField>(*this);
    }

    void PointGravityField::Fill(Physics::GravityFieldData& data)
    {
        data.kind = Physics::GravityFieldKind::Point;
        data.radius = radius;
        data.falloff = falloff;
        data.referenceDistance = referenceDistance;
        data.minDistance = minDistance;
        data.fadeDistance = fadeDistance;
    }

    // ---------------- Radial ----------------

    RadialGravityField::RadialGravityField(std::shared_ptr<Actor> parent, uint32_t local_id) : GravityField(parent, local_id)
    {
    }

    ordered_json RadialGravityField::Serialize()
    {
        return SerializeReflected("gravityRadial");
    }

    std::shared_ptr<Component> RadialGravityField::Clone() const
    {
        return Object::Create<RadialGravityField>(*this);
    }

    void RadialGravityField::Fill(Physics::GravityFieldData& data)
    {
        data.kind = Physics::GravityFieldKind::Radial;
        data.radius = radius;
        data.reach = reach;
        data.falloff = falloff;
        data.solidInterior = solidInterior;
        data.fadeDistance = fadeDistance;
    }

    // ---------------- Volume ----------------

    GravityVolume::GravityVolume(std::shared_ptr<Actor> parent, uint32_t local_id) : GravityField(parent, local_id)
    {
    }

    ordered_json GravityVolume::Serialize()
    {
        return SerializeReflected("gravityVolume");
    }

    std::shared_ptr<Component> GravityVolume::Clone() const
    {
        return Object::Create<GravityVolume>(*this);
    }

    void GravityVolume::Fill(Physics::GravityFieldData& data)
    {
        data.kind = Physics::GravityFieldKind::Volume;
        data.halfExtent = halfExtent;
        data.shape = shape;
        data.volumeDirection = directionMode;
        data.direction = direction;
        data.fadeDistance = fadeDistance;
    }
}
