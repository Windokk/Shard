#pragma once

#include <glm/glm.hpp>

#include "engine/physics/gravity.hpp"

#include "engine/world/components/component.hpp"

#include "engine/assets/reflection/reflection_fields.hpp"
#include "engine/assets/reflection/attributes.hpp"

namespace Shard::Engine::Objects::Components
{
    // Something that decides which way, and how hard, bodies fall in a region of the world, in place of (or on top of) the project's gravity.
    // Not usable on its own : the four kinds below are what an actor gets. The physics reads the fields of the loaded worlds at every step
    // (see PhysicsManager::CollectGravityFields) and gives each dynamic body the gravity the fields make where its center of mass is, by
    // its PhysicsBody::gravityMode.
    //
    // Fields that overlap are combined by priority : the highest one that covers a body wins, the lower ones and the project's gravity only
    // show through where it does not cover (a field fades out toward its edge, so the gravity changes smoothly from a field to the next).
    // Fields of the same priority that overlap blend. An additive field is summed on top of the result whatever its priority.
    class CLASS() GravityField : public Component {
        public:
            GravityField(std::shared_ptr<Actor> parent, uint32_t local_id);

            /// The numbers the simulation solves this field with, from the pose the actor has now (root frame, in double)
            Physics::GravityFieldData BuildData();

            /// What this field alone gives at `position` (root frame) : false when it does not reach it, else its acceleration at full
            /// strength and how much of the position it covers, 0 to 1 (less than 1 near its edge)
            bool Sample(const glm::dvec3& position, glm::vec3& acceleration, float& coverage);

            FIELD(Editable, range=-1000.0f|1000.0f, tooltip=Acceleration in world units per second squared (9.81 for the Earth, in a project in metres). Negative pushes away)
            float strength = 9.81f;

            FIELD(Editable, range=-100|100, tooltip=The field of the highest priority that covers a body wins over the lower ones and over the project gravity)
            int priority = 0;

            FIELD(Editable, tooltip=Override replaces the gravity below it, Additive is added on top of it)
            Physics::GravityBlend blend = Physics::GravityBlend::Override;

        protected:
            /// Fills what is specific to the kind of field, `data` already holding the common part
            virtual void Fill(Physics::GravityFieldData& data) = 0;
    };

    // The same gravity everywhere : a direction and a strength. Replaces the project's gravity for the whole world, and is turned by the
    // rotation of its actor (tilt the actor to tilt gravity). The way to give a level its own gravity, or a lower priority base for volumes.
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

    // Pulls toward the position of its actor, as a mass would : strongest next to it, weaker with the distance. Beyond its radius it does nothing.
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

    // The gravity of a planet : it pulls toward the center of its actor, with the strength of the surface at `radius`, which falls with the
    // height above it (inverse square by default) and, inside the planet, with the distance to the center. Bodies stand on the surface
    // whatever side of the planet they are on : read PhysicsBody::GetGravity to orient a character.
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

    // A region (the box of its actor, or the sphere / cylinder inscribed in it) where gravity is something else : a corridor where it points
    // sideways, a zone without any, the inside of a rotating station. Moves, turns and scales with its actor. Fades out over `fadeDistance`
    // inside its edge, so a body crossing it feels the change gradually.
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
