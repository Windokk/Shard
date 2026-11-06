#pragma once

#include <glm/glm.hpp>

#include "engine/physics/gravity.hpp"

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    class CLASS() GravityField : public Component {
        public:
            GravityField(std::shared_ptr<Actor> parent, uint32_t local_id);

            Physics::GravityFieldData BuildData();

            bool Sample(const glm::dvec3& position, glm::vec3& acceleration, float& coverage);

            FIELD(Editable, range=-1000.0f|1000.0f, tooltip=Acceleration in world units per second squared (9.81 for the Earth, in a project in metres). Negative pushes away)
            float strength = 9.81f;

            FIELD(Editable, range=-100|100, tooltip=The field of the highest priority that covers a body wins over the lower ones and over the project gravity)
            int priority = 0;

            FIELD(Editable, tooltip=Override replaces the gravity below it, Additive is added on top of it)
            Physics::GravityBlend blend = Physics::GravityBlend::Override;

        protected:
            virtual void Fill(Physics::GravityFieldData& data) = 0;
    };

    class CLASS() UniformGravityField : public GravityField {
        public:
            UniformGravityField(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            FIELD(Editable, tooltip=Direction gravity points to, in the space of the actor. Down by default)
            glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);

            DECLARE_DESCRIPTOR(UniformGravityField)

        protected:
            void Fill(Physics::GravityFieldData& data) override;
    };

    class CLASS() PointGravityField : public GravityField {
        public:
            PointGravityField(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            FIELD(Editable, range=0.0f|1000000.0f, tooltip=Distance from the actor beyond which the field does nothing. 0 reaches everywhere)
            float radius = 20.0f;

            FIELD(Editable, tooltip=How the strength drops with the distance : Constant, Linear (to nothing at the radius) or InverseSquare)
            Physics::GravityFalloff falloff = Physics::GravityFalloff::Linear;

            FIELD(Editable, range=0.001f|10000.0f, tooltip=InverseSquare only : the distance at which the strength is reached)
            float referenceDistance = 1.0f;

            FIELD(Editable, range=0.0f|1000.0f, tooltip=InverseSquare only : closer than this the strength stops growing, so the pull does not become infinite at the center)
            float minDistance = 0.5f;

            FIELD(Editable, range=0.0f|10000.0f, tooltip=The gravity fades to nothing over this distance inside the radius, so it does not stop sharply)
            float fadeDistance = 0.0f;

            DECLARE_DESCRIPTOR(PointGravityField)

        protected:
            void Fill(Physics::GravityFieldData& data) override;
    };

    class CLASS() RadialGravityField : public GravityField {
        public:
            RadialGravityField(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            FIELD(Editable, range=0.01f|10000000.0f, tooltip=Radius of the planet. The strength is the one of the surface)
            float radius = 10.0f;

            FIELD(Editable, range=0.0f|10000000.0f, tooltip=Height above the surface to which the gravity reaches. 0 reaches everywhere)
            float reach = 0.0f;

            FIELD(Editable, tooltip=How the strength drops with the height above the surface : Constant, Linear (to nothing at the reach) or InverseSquare)
            Physics::GravityFalloff falloff = Physics::GravityFalloff::InverseSquare;

            FIELD(Editable, tooltip=Inside the planet the gravity decreases to nothing at the center, as in a ball of uniform density. Off keeps the surface strength)
            bool solidInterior = true;

            FIELD(Editable, range=0.0f|1000000.0f, tooltip=The gravity fades to nothing over this height at the end of the reach)
            float fadeDistance = 0.0f;

            DECLARE_DESCRIPTOR(RadialGravityField)

        protected:
            void Fill(Physics::GravityFieldData& data) override;
    };

    class CLASS() GravityVolume : public GravityField {
        public:
            GravityVolume(std::shared_ptr<Actor> parent, uint32_t local_id);

            ordered_json Serialize() override;
            std::shared_ptr<Component> Clone() const override;

            FIELD(Editable, tooltip=Half of the size of the box of the volume along each axis, before the scale of the actor)
            glm::vec3 halfExtent = glm::vec3(5.0f);

            FIELD(Editable, tooltip=Shape of the region the volume covers, inscribed in its box. Cylinder stands along Y)
            Physics::GravityVolumeShape shape = Physics::GravityVolumeShape::Box;

            FIELD(Editable, tooltip=Where gravity points : Fixed (the direction below), ToCenter, or ToAxis (the Y axis of the volume, away from it with a negative strength))
            Physics::GravityVolumeDirection directionMode = Physics::GravityVolumeDirection::Fixed;

            FIELD(Editable, tooltip=Fixed only : direction gravity points to, in the space of the actor. Down by default)
            glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);

            FIELD(Editable, range=0.0f|1000.0f, tooltip=The gravity fades in over this distance inside the edge of the volume. 0 changes sharply at the edge)
            float fadeDistance = 1.0f;

            DECLARE_DESCRIPTOR(GravityVolume)

        protected:
            void Fill(Physics::GravityFieldData& data) override;
    };
}

using namespace Shard::Engine;
