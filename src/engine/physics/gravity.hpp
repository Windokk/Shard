#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Shard::Engine::Physics
{
    /// Where a body gets its gravity from
    enum class GravityMode : int {
        Fields = 0,     // the world's gravity, replaced or added to by the gravity fields the body is in (the default)
        World,          // the world's gravity alone : the fields do not touch this body
        Custom          // its own `customGravity` : neither the world's gravity nor the fields
    };

    /// How a gravity field combines with what is below it
    enum class GravityBlend : int {
        Override = 0,   // replaces the gravity of the fields of a lower priority and of the world, over the share of the field that covers the body
        Additive        // is added to whatever the result is (a moon pulling on top of a planet's gravity)
    };

    /// How a point / radial field weakens with the distance
    enum class GravityFalloff : int {
        Constant = 0,   // the same strength at any distance
        Linear,         // from its full strength to nothing over the range of the field (constant when the field has no range)
        InverseSquare   // 1 / distance^2, like a real mass
    };

    /// The region a gravity volume covers, inscribed in its box
    enum class GravityVolumeShape : int { Box = 0, Sphere, Cylinder };

    /// Where the gravity of a volume points
    enum class GravityVolumeDirection : int {
        Fixed = 0,      // along the `direction` of the volume (turned by the rotation of its actor)
        ToCenter,       // to the center of the volume
        ToAxis          // to the Y axis of the volume : the gravity of a rotating space station when the strength is negative (away from it)
    };

    enum class GravityFieldKind : uint8_t { Uniform = 0, Point, Radial, Volume };

    /// What the simulation needs to know of a gravity field, in plain numbers : the components turn themselves into one every step
    /// (see GravityField::BuildData), so the physics solves the fields without touching a component (or a world). Positions are the
    /// root frame's, in double, like Jolt's.
    struct GravityFieldData
    {
        GravityFieldKind kind = GravityFieldKind::Uniform;
        GravityBlend blend = GravityBlend::Override;
        int priority = 0;
        float strength = 9.81f;                         // acceleration, in world units per second squared. Negative : pushes away
        uint32_t order = 0;                             // breaks the ties between two fields of the same priority (see SetGravityFields)

        glm::dvec3 position = glm::dvec3(0.0);
        glm::dquat rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);
        glm::vec3 scale = glm::vec3(1.0f);              // volume only
        glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);     // uniform and fixed volume : in the space of the field, turned by `rotation`

        // Point (radius = distance of influence) and radial (radius = the surface of the planet, reach = how far above it gravity reaches)
        float radius = 10.0f;
        float reach = 0.0f;
        float fadeDistance = 0.0f;                      // gravity fades to nothing over this distance, inside the outer edge of the field
        GravityFalloff falloff = GravityFalloff::InverseSquare;
        float referenceDistance = 1.0f;                 // point : the distance at which `strength` is reached
        float minDistance = 0.5f;                       // point : closer than this the strength stops growing
        bool solidInterior = true;                      // radial : inside the planet gravity decreases to the center, else it stays constant

        // Volume
        GravityVolumeShape shape = GravityVolumeShape::Box;
        GravityVolumeDirection volumeDirection = GravityVolumeDirection::Fixed;
        glm::vec3 halfExtent = glm::vec3(5.0f);

        bool operator==(const GravityFieldData& o) const
        {
            return kind == o.kind && blend == o.blend && priority == o.priority && strength == o.strength && order == o.order
                && position == o.position && rotation == o.rotation && scale == o.scale && direction == o.direction
                && radius == o.radius && reach == o.reach && fadeDistance == o.fadeDistance && falloff == o.falloff
                && referenceDistance == o.referenceDistance && minDistance == o.minDistance && solidInterior == o.solidInterior
                && shape == o.shape && volumeDirection == o.volumeDirection && halfExtent == o.halfExtent;
        }
        bool operator!=(const GravityFieldData& o) const { return !(*this == o); }
    };

    /// What the gravity of one body depends on, besides where it is
    struct BodyGravity
    {
        GravityMode mode = GravityMode::Fields;
        glm::vec3 custom = glm::vec3(0.0f, -9.81f, 0.0f);
    };

    /// How much a field covers `position` (0 : not at all, 1 : fully) and the acceleration it gives there at full coverage.
    /// @return false when the field does not reach `position`
    bool EvaluateGravityField(const GravityFieldData& field, const glm::dvec3& position, glm::vec3& acceleration, float& coverage);

    /// Sorts the fields the way SampleGravityFields reads them : by priority, the highest first, then by `order`
    void SortGravityFields(std::vector<GravityFieldData>& fields);

    /// The gravity at `position`, from the fields (sorted by SortGravityFields) and the gravity of the world `world`.
    ///
    /// The fields are read from the highest priority to the lowest. The overriding ones of a priority share what is still free :
    /// together they cover `min(1, sum of their coverage)` of it, each by its coverage (so two fields that overlap blend, and one that
    /// fades out at its edge fades into the fields below it, and into the world's gravity at last). What no field covers is the world's.
    /// The additive ones are summed on top, whatever their priority.
    glm::vec3 SampleGravityFields(const std::vector<GravityFieldData>& fields, const glm::dvec3& position, const glm::vec3& world);

    /// The gravity a body feels at `position` : by its mode, the fields' / the world's / its own
    glm::vec3 ResolveBodyGravity(const std::vector<GravityFieldData>& fields, const BodyGravity& body, const glm::dvec3& position, const glm::vec3& world);
}
