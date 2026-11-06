#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Shard::Engine::Physics
{
    enum class GravityMode : int {
        Fields = 0,
        World,
        Custom
    };

    enum class GravityBlend : int {
        Override = 0,
        Additive
    };

    enum class GravityFalloff : int {
        Constant = 0,
        Linear,
        InverseSquare
    };

    enum class GravityVolumeShape : int { Box = 0, Sphere, Cylinder };

    enum class GravityVolumeDirection : int {
        Fixed = 0,
        ToCenter,
        ToAxis
    };

    enum class GravityFieldKind : uint8_t { Uniform = 0, Point, Radial, Volume };

    struct GravityFieldData
    {
        GravityFieldKind kind = GravityFieldKind::Uniform;
        GravityBlend blend = GravityBlend::Override;
        int priority = 0;
        float strength = 9.81f;
        uint32_t order = 0;

        glm::dvec3 position = glm::dvec3(0.0);
        glm::dquat rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);
        glm::vec3 scale = glm::vec3(1.0f);
        glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);

        float radius = 10.0f;
        float reach = 0.0f;
        float fadeDistance = 0.0f;
        GravityFalloff falloff = GravityFalloff::InverseSquare;
        float referenceDistance = 1.0f;
        float minDistance = 0.5f;
        bool solidInterior = true;

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

    struct BodyGravity
    {
        GravityMode mode = GravityMode::Fields;
        glm::vec3 custom = glm::vec3(0.0f, -9.81f, 0.0f);
    };

    bool EvaluateGravityField(const GravityFieldData& field, const glm::dvec3& position, glm::vec3& acceleration, float& coverage);

    void SortGravityFields(std::vector<GravityFieldData>& fields);

    // Fields read from highest priority down. Overriding ones of a priority share the free part (coverage blends, fading into lower fields then the world's gravity). Additive ones sum on top.
    glm::vec3 SampleGravityFields(const std::vector<GravityFieldData>& fields, const glm::dvec3& position, const glm::vec3& world);

    glm::vec3 ResolveBodyGravity(const std::vector<GravityFieldData>& fields, const BodyGravity& body, const glm::dvec3& position, const glm::vec3& world);
}
