#include "gravity.hpp"

#include <algorithm>
#include <cmath>

namespace Shard::Engine::Physics
{
    namespace
    {
        constexpr double kEpsilon = 1e-9;

        glm::vec3 Scaled(const glm::dvec3& direction, float strength)
        {
            const double length = glm::length(direction);
            if (length < kEpsilon)
                return glm::vec3(0.0f);
            return glm::vec3(direction / length * double(strength));
        }

        /// 1 deep inside, going down to 0 at the edge over `fadeDistance`. `insideDistance` : how far inside the edge the point is.
        float EdgeCoverage(double insideDistance, float fadeDistance)
        {
            if (fadeDistance <= 0.0f)
                return 1.0f;
            return float(glm::clamp(insideDistance / double(fadeDistance), 0.0, 1.0));
        }

        /// The strength of a source `distance` away from a point source / above a planet, by its falloff
        float FalloffStrength(GravityFalloff falloff, float strength, double distance, double range, double reference, double minDistance)
        {
            switch (falloff)
            {
                case GravityFalloff::Constant:
                    return strength;
                case GravityFalloff::Linear:
                    return range > 0.0 ? strength * float(glm::clamp(1.0 - distance / range, 0.0, 1.0)) : strength;
                case GravityFalloff::InverseSquare:
                {
                    const double d = std::max(distance, std::max(minDistance, 1e-4));
                    const double ratio = std::max(reference, 1e-4) / d;
                    return strength * float(ratio * ratio);
                }
            }
            return strength;
        }

        bool EvaluateUniform(const GravityFieldData& f, glm::vec3& acceleration, float& coverage)
        {
            acceleration = Scaled(f.rotation * glm::dvec3(f.direction), f.strength);
            coverage = 1.0f;
            return true;
        }

        bool EvaluatePoint(const GravityFieldData& f, const glm::dvec3& p, glm::vec3& acceleration, float& coverage)
        {
            const glm::dvec3 toSource = f.position - p;
            const double distance = glm::length(toSource);

            // A radius of 0 : reaches everywhere
            if (f.radius > 0.0f && distance >= double(f.radius))
                return false;

            coverage = f.radius > 0.0f ? EdgeCoverage(double(f.radius) - distance, f.fadeDistance) : 1.0f;

            const float strength = FalloffStrength(f.falloff, f.strength, distance, double(f.radius), double(f.referenceDistance), double(f.minDistance));
            acceleration = Scaled(toSource, strength);
            return coverage > 0.0f;
        }

        bool EvaluateRadial(const GravityFieldData& f, const glm::dvec3& p, glm::vec3& acceleration, float& coverage)
        {
            const double surface = std::max(double(f.radius), 1e-4);
            const glm::dvec3 toCenter = f.position - p;
            const double distance = glm::length(toCenter);
            const double height = distance - surface;

            if (f.reach > 0.0f && height >= double(f.reach))
                return false;

            float strength;
            if (height < 0.0)
            {
                // Inside the planet : a body of uniform density pulls in proportion to the distance from its center
                strength = f.solidInterior ? f.strength * float(distance / surface) : f.strength;
                coverage = 1.0f;
            }
            else
            {
                const double reference = surface;
                strength = FalloffStrength(f.falloff, f.strength, f.falloff == GravityFalloff::InverseSquare ? distance : height,
                                           double(f.reach), reference, 0.0);
                coverage = f.reach > 0.0f ? EdgeCoverage(double(f.reach) - height, f.fadeDistance) : 1.0f;
            }

            acceleration = Scaled(toCenter, strength);
            return coverage > 0.0f;
        }

        bool EvaluateVolume(const GravityFieldData& f, const glm::dvec3& p, glm::vec3& acceleration, float& coverage)
        {
            const glm::dvec3 local = glm::inverse(f.rotation) * (p - f.position);
            const glm::dvec3 extent = glm::max(glm::dvec3(f.halfExtent) * glm::abs(glm::dvec3(f.scale)), glm::dvec3(1e-6));

            // How far inside the edge of the region the point is
            double depth;
            switch (f.shape)
            {
                case GravityVolumeShape::Sphere:
                    depth = (1.0 - glm::length(local / extent)) * std::min({ extent.x, extent.y, extent.z });
                    break;
                case GravityVolumeShape::Cylinder:
                {
                    const double radial = glm::length(glm::dvec2(local.x / extent.x, local.z / extent.z));
                    depth = std::min((1.0 - radial) * std::min(extent.x, extent.z), extent.y - std::abs(local.y));
                    break;
                }
                case GravityVolumeShape::Box:
                default:
                {
                    const glm::dvec3 inside = extent - glm::abs(local);
                    depth = std::min({ inside.x, inside.y, inside.z });
                    break;
                }
            }

            if (depth <= 0.0)
                return false;

            coverage = EdgeCoverage(depth, f.fadeDistance);

            switch (f.volumeDirection)
            {
                case GravityVolumeDirection::ToCenter:
                    acceleration = Scaled(f.position - p, f.strength);
                    break;
                case GravityVolumeDirection::ToAxis:
                {
                    const glm::dvec3 axis = f.rotation * glm::dvec3(0.0, 1.0, 0.0);
                    const glm::dvec3 fromCenter = p - f.position;
                    const glm::dvec3 offAxis = fromCenter - axis * glm::dot(fromCenter, axis);
                    acceleration = Scaled(-offAxis, f.strength);
                    break;
                }
                case GravityVolumeDirection::Fixed:
                default:
                    acceleration = Scaled(f.rotation * glm::dvec3(f.direction), f.strength);
                    break;
            }
            return true;
        }
    }

    bool EvaluateGravityField(const GravityFieldData& field, const glm::dvec3& position, glm::vec3& acceleration, float& coverage)
    {
        acceleration = glm::vec3(0.0f);
        coverage = 0.0f;

        switch (field.kind)
        {
            case GravityFieldKind::Uniform: return EvaluateUniform(field, acceleration, coverage);
            case GravityFieldKind::Point:   return EvaluatePoint(field, position, acceleration, coverage);
            case GravityFieldKind::Radial:  return EvaluateRadial(field, position, acceleration, coverage);
            case GravityFieldKind::Volume:  return EvaluateVolume(field, position, acceleration, coverage);
        }
        return false;
    }

    void SortGravityFields(std::vector<GravityFieldData>& fields)
    {
        // A total order (the same fields always give the same list) : the result sums floats, and their order changes the last bits
        std::stable_sort(fields.begin(), fields.end(), [](const GravityFieldData& a, const GravityFieldData& b) {
            if (a.priority != b.priority)
                return a.priority > b.priority;
            return a.order < b.order;
        });
    }

    glm::vec3 SampleGravityFields(const std::vector<GravityFieldData>& fields, const glm::dvec3& position, const glm::vec3& world)
    {
        glm::dvec3 result(0.0);
        glm::dvec3 additive(0.0);
        double unclaimed = 1.0;      // the share of the gravity no field above claimed yet

        const size_t count = fields.size();
        size_t i = 0;
        while (i < count)
        {
            const int priority = fields[i].priority;

            double coverageSum = 0.0;
            glm::dvec3 weighted(0.0);

            for (; i < count && fields[i].priority == priority; ++i)
            {
                glm::vec3 acceleration;
                float coverage;
                if (!EvaluateGravityField(fields[i], position, acceleration, coverage) || coverage <= 0.0f)
                    continue;

                if (fields[i].blend == GravityBlend::Additive)
                {
                    additive += glm::dvec3(acceleration) * double(coverage);
                    continue;
                }

                coverageSum += double(coverage);
                weighted += glm::dvec3(acceleration) * double(coverage);
            }

            if (coverageSum > 0.0 && unclaimed > 0.0)
            {
                const double covered = std::min(1.0, coverageSum);
                result += (weighted / coverageSum) * (covered * unclaimed);
                unclaimed *= 1.0 - covered;
            }
        }

        result += glm::dvec3(world) * unclaimed + additive;
        return glm::vec3(result);
    }

    glm::vec3 ResolveBodyGravity(const std::vector<GravityFieldData>& fields, const BodyGravity& body, const glm::dvec3& position, const glm::vec3& world)
    {
        switch (body.mode)
        {
            case GravityMode::World:  return world;
            case GravityMode::Custom: return body.custom;
            case GravityMode::Fields:
            default:                  return SampleGravityFields(fields, position, world);
        }
    }
}
