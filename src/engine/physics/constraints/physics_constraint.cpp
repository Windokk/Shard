#include "physics_constraint.hpp"

#include <algorithm>
#include <cmath>

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/physics/physics_world_data.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/event_system.hpp"
#include "engine/world/world.hpp"

#include "physics_constraint.reflection.hpp"

namespace Shard::Engine::Objects::Components{

    namespace {

        constexpr int kRetryTicks = 30;
        const char* kBrokenEvent = "constraint.broken";

        float Radians(float degrees)
        {
            return glm::radians(degrees);
        }

        JPH::EMotorState MotorState(Physics::ConstraintMotor mode)
        {
            switch(mode){
                case Physics::ConstraintMotor::Velocity: return JPH::EMotorState::Velocity;
                case Physics::ConstraintMotor::Position: return JPH::EMotorState::Position;
                default:                                 return JPH::EMotorState::Off;
            }
        }

        JPH::Vec3 PerpendicularTo(JPH::Vec3Arg axis)
        {
            return axis.GetNormalizedPerpendicular();
        }

        JPH::SpringSettings Spring(float frequency, float damping)
        {
            return JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, std::max(frequency, 0.0f), std::max(damping, 0.0f));
        }

        std::shared_ptr<PhysicsBody> BodyOf(const std::shared_ptr<Actor>& actor)
        {
            return actor ? actor->GetComponent<PhysicsBody>() : nullptr;
        }
    }

    PhysicsConstraint::PhysicsConstraint(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {
    }

    PhysicsConstraint::~PhysicsConstraint()
    {
        Release();
    }

    void PhysicsConstraint::Activate()
    {
        Component::Activate();
        m_Runtime.dirty = true;
    }

    void PhysicsConstraint::DeActivate()
    {
        Component::DeActivate();
        Release();
    }

    void PhysicsConstraint::Destroy()
    {
        Release();
        Component::Destroy();
    }

    void PhysicsConstraint::OnFieldChanged(const FieldChangedEvent& event)
    {
        m_Runtime.dirty = true;
        m_Runtime.cooldown = 0;
    }

    void PhysicsConstraint::Reconfigure()
    {
        if (m_Runtime.live)
            Configure(*m_Runtime.live);
    }

    void PhysicsConstraint::Release()
    {
        if (m_Runtime.manager && m_Runtime.live)
            m_Runtime.manager->UnregisterConstraint(this);
        OnConstraintDropped();
    }

    void PhysicsConstraint::OnConstraintDropped()
    {
        if (m_Runtime.manager && m_Runtime.ignored)
            m_Runtime.manager->Collision().IgnoreCollision(m_Runtime.a, m_Runtime.b, false);
        m_Runtime.ignored = false;
        m_Runtime.live = nullptr;
        m_Runtime.a = JPH::BodyID();
        m_Runtime.b = JPH::BodyID();
        m_Runtime.dirty = true;
    }

    void PhysicsConstraint::Break()
    {
        if (m_Runtime.broken)
            return;

        Release();
        m_Runtime.broken = true;

        if (Core::IEngineContext* engine = GetEngineContext())
            if (Events::EventDispatcher* events = engine->GetEventDispatcher())
            {
                const int actorId = parent ? parent->GetID().GetAsInt() : 0;
                events->emitNamed(kBrokenEvent, &actorId, sizeof(actorId));
            }
    }

    void PhysicsConstraint::Repair()
    {
        m_Runtime.broken = false;
        m_Runtime.dirty = true;
    }

    void PhysicsConstraint::CheckBreak(float deltaTime)
    {
        if (!m_Runtime.live || deltaTime <= 0.0f || (breakForce <= 0.0f && breakTorque <= 0.0f))
            return;

        float linear = 0.0f;
        float angular = 0.0f;
        Measure(*m_Runtime.live, linear, angular);

        if ((breakForce > 0.0f && linear / deltaTime > breakForce) || (breakTorque > 0.0f && angular / deltaTime > breakTorque))
            Break();
    }

    void PhysicsConstraint::ConnectTo(const std::shared_ptr<Actor>& actor)
    {
        m_Runtime.explicitTarget = actor;
        m_Runtime.explicitWorld = false;
        m_Runtime.dirty = true;
    }

    void PhysicsConstraint::ConnectToWorld()
    {
        m_Runtime.explicitTarget.reset();
        m_Runtime.explicitWorld = true;
        m_Runtime.dirty = true;
    }

    void PhysicsConstraint::ConnectByTag()
    {
        m_Runtime.explicitTarget.reset();
        m_Runtime.explicitWorld = false;
        m_Runtime.found.reset();
        m_Runtime.cooldown = 0;
        m_Runtime.dirty = true;
    }

    std::shared_ptr<Actor> PhysicsConstraint::GetTarget() const
    {
        if (std::shared_ptr<Actor> chosen = m_Runtime.explicitTarget.lock())
            return chosen;
        return m_Runtime.found.lock();
    }

    bool PhysicsConstraint::ResolveTarget(JPH::BodyID& body)
    {
        body = JPH::BodyID();

        if (m_Runtime.explicitWorld)
            return true;

        if (std::shared_ptr<Actor> chosen = m_Runtime.explicitTarget.lock())
        {
            const auto targetBody = BodyOf(chosen);
            body = targetBody ? targetBody->GetBodyID() : JPH::BodyID();
            return !body.IsInvalid();
        }

        if (target.IsEmpty())
            return true;

        std::shared_ptr<Actor> cached = m_Runtime.found.lock();
        if (cached)
        {
            const auto cachedBody = BodyOf(cached);
            if (cachedBody && !cachedBody->GetBodyID().IsInvalid() && target.Matches(cached->GetTags()))
            {
                body = cachedBody->GetBodyID();
                return true;
            }
            m_Runtime.found.reset();
        }

        if (m_Runtime.cooldown > 0)
        {
            m_Runtime.cooldown--;
            return false;
        }
        m_Runtime.cooldown = kRetryTicks;

        if (!parent || !parent->world)
            return false;

        std::shared_ptr<Actor> best;
        for (const std::shared_ptr<Actor>& candidate : parent->world->FindActors(target))
        {
            if (!candidate || candidate == parent || !BodyOf(candidate))
                continue;
            if (!best || candidate->GetID().GetAsInt() < best->GetID().GetAsInt())
                best = candidate;
        }
        if (!best)
            return false;

        m_Runtime.found = best;
        m_Runtime.cooldown = 0;
        const auto bestBody = BodyOf(best);
        body = bestBody->GetBodyID();
        return !body.IsInvalid();
    }

    bool PhysicsConstraint::Sync(Physics::PhysicsManager& physics)
    {
        if (!activated || !parent || m_Runtime.broken)
        {
            Release();
            return false;
        }

        const auto ownBody = BodyOf(parent);
        const JPH::BodyID a = ownBody ? ownBody->GetBodyID() : JPH::BodyID();
        if (a.IsInvalid())
        {
            Release();
            return false;
        }

        JPH::BodyID b;
        if (!ResolveTarget(b))
        {
            Release();
            return false;
        }

        if (m_Runtime.live && !m_Runtime.dirty && m_Runtime.a == a && m_Runtime.b == b)
            return true;

        Release();
        m_Runtime.dirty = false;
        return Build(physics, a, b);
    }

    bool PhysicsConstraint::Build(Physics::PhysicsManager& physics, JPH::BodyID a, JPH::BodyID b)
    {
        JPH::PhysicsSystem& system = physics.GetPhysicsSystem();
        JPH::TwoBodyConstraint* made = nullptr;

        const auto frameOf = [this](const JPH::Body& body) {
            Frame frame;
            frame.rotation = body.GetRotation();
            frame.scale = JPH::Vec3::sReplicate(1.0f);
            if (parent && parent->transform)
                frame.scale = ToJolt(parent->transform->GetWorldScale());
            frame.pivot = body.GetPosition() + frame.rotation * (ToJolt(anchor) * frame.scale);
            return frame;
        };

        if (b.IsInvalid())
        {
            JPH::BodyLockWrite lock(system.GetBodyLockInterface(), a);
            if (!lock.Succeeded() || lock.GetBody().IsStatic())
                return false;
            made = Create(frameOf(lock.GetBody()), lock.GetBody(), JPH::Body::sFixedToWorld);
        }
        else
        {
            const JPH::BodyID ids[2] = { a, b };
            JPH::BodyLockMultiWrite lock(system.GetBodyLockInterface(), ids, 2);
            JPH::Body* first = lock.GetBody(0);
            JPH::Body* second = lock.GetBody(1);
            if (!first || !second || (first->IsStatic() && second->IsStatic()))
                return false;
            made = Create(frameOf(*first), *first, *second);
        }

        if (!made)
            return false;

        Configure(*made);
        m_Runtime.manager = &physics;
        m_Runtime.live = made;
        m_Runtime.a = a;
        m_Runtime.b = b;
        physics.RegisterConstraint(this, a, b, made);

        if (!collideConnected && !b.IsInvalid())
        {
            physics.Collision().IgnoreCollision(a, b, true);
            m_Runtime.ignored = true;
        }
        return true;
    }

    FixedConstraint::FixedConstraint(std::shared_ptr<Actor> parent, uint32_t local_id) : PhysicsConstraint(parent, local_id)
    {
    }

    ordered_json FixedConstraint::Serialize()
    {
        return SerializeReflected("constraintFixed");
    }

    std::shared_ptr<Component> FixedConstraint::Clone() const
    {
        return Object::Create<FixedConstraint>(*this);
    }

    JPH::TwoBodyConstraint* FixedConstraint::Create(const Frame&, JPH::Body& a, JPH::Body& b)
    {
        JPH::FixedConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mAutoDetectPoint = true;
        return settings.Create(a, b);
    }

    void FixedConstraint::Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const
    {
        const auto& fixed = static_cast<const JPH::FixedConstraint&>(constraint);
        linear = fixed.GetTotalLambdaPosition().Length();
        angular = fixed.GetTotalLambdaRotation().Length();
    }

    PointConstraint::PointConstraint(std::shared_ptr<Actor> parent, uint32_t local_id) : PhysicsConstraint(parent, local_id)
    {
    }

    ordered_json PointConstraint::Serialize()
    {
        return SerializeReflected("constraintPoint");
    }

    std::shared_ptr<Component> PointConstraint::Clone() const
    {
        return Object::Create<PointConstraint>(*this);
    }

    JPH::TwoBodyConstraint* PointConstraint::Create(const Frame& frame, JPH::Body& a, JPH::Body& b)
    {
        JPH::PointConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = frame.pivot;
        settings.mPoint2 = frame.pivot;
        return settings.Create(a, b);
    }

    void PointConstraint::Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float&) const
    {
        linear = static_cast<const JPH::PointConstraint&>(constraint).GetTotalLambdaPosition().Length();
    }

    HingeConstraint::HingeConstraint(std::shared_ptr<Actor> parent, uint32_t local_id) : PhysicsConstraint(parent, local_id)
    {
    }

    ordered_json HingeConstraint::Serialize()
    {
        return SerializeReflected("constraintHinge");
    }

    std::shared_ptr<Component> HingeConstraint::Clone() const
    {
        return Object::Create<HingeConstraint>(*this);
    }

    JPH::TwoBodyConstraint* HingeConstraint::Create(const Frame& frame, JPH::Body& a, JPH::Body& b)
    {
        const JPH::Vec3 hinge = (frame.rotation * ToJolt(axis)).NormalizedOr(JPH::Vec3::sAxisY());
        const JPH::Vec3 normal = PerpendicularTo(hinge);

        JPH::HingeConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = frame.pivot;
        settings.mHingeAxis1 = settings.mHingeAxis2 = hinge;
        settings.mNormalAxis1 = settings.mNormalAxis2 = normal;
        if (limited)
        {
            settings.mLimitsMin = Radians(std::clamp(minAngle, -180.0f, 0.0f));
            settings.mLimitsMax = Radians(std::clamp(maxAngle, 0.0f, 180.0f));
            settings.mLimitsSpringSettings = Spring(limitFrequency, limitDamping);
        }
        settings.mMaxFrictionTorque = std::max(friction, 0.0f);
        settings.mMotorSettings = JPH::MotorSettings(std::max(motorFrequency, 0.001f), std::max(motorDamping, 0.0f), FLT_MAX, std::max(motorMaxTorque, 0.0f));
        return settings.Create(a, b);
    }

    void HingeConstraint::Configure(JPH::TwoBodyConstraint& constraint)
    {
        auto& hinge = static_cast<JPH::HingeConstraint&>(constraint);
        hinge.SetMotorState(MotorState(motor));
        hinge.SetTargetAngularVelocity(Radians(motorTarget));
        hinge.SetTargetAngle(Radians(motorTarget));
    }

    void HingeConstraint::Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const
    {
        const auto& hinge = static_cast<const JPH::HingeConstraint&>(constraint);
        linear = hinge.GetTotalLambdaPosition().Length();
        const JPH::Vector<2> rotation = hinge.GetTotalLambdaRotation();
        angular = std::sqrt(rotation[0] * rotation[0] + rotation[1] * rotation[1]);
    }

    void HingeConstraint::SetMotor(Physics::ConstraintMotor mode, float targetValue)
    {
        motor = mode;
        motorTarget = targetValue;
        Reconfigure();
        if (Live() && mode != Physics::ConstraintMotor::Off && m_Runtime.manager)
            m_Runtime.manager->WakeConstraintBodies(m_Runtime.a, m_Runtime.b);
    }

    float HingeConstraint::GetAngle() const
    {
        return Live() ? glm::degrees(static_cast<const JPH::HingeConstraint*>(Live())->GetCurrentAngle()) : 0.0f;
    }

    SliderConstraint::SliderConstraint(std::shared_ptr<Actor> parent, uint32_t local_id) : PhysicsConstraint(parent, local_id)
    {
    }

    ordered_json SliderConstraint::Serialize()
    {
        return SerializeReflected("constraintSlider");
    }

    std::shared_ptr<Component> SliderConstraint::Clone() const
    {
        return Object::Create<SliderConstraint>(*this);
    }

    JPH::TwoBodyConstraint* SliderConstraint::Create(const Frame& frame, JPH::Body& a, JPH::Body& b)
    {
        const JPH::Vec3 slide = (frame.rotation * ToJolt(axis)).NormalizedOr(JPH::Vec3::sAxisX());
        const JPH::Vec3 normal = PerpendicularTo(slide);

        JPH::SliderConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = frame.pivot;
        settings.mSliderAxis1 = settings.mSliderAxis2 = slide;
        settings.mNormalAxis1 = settings.mNormalAxis2 = normal;
        if (limited)
        {
            settings.mLimitsMin = std::min(minLimit, 0.0f);
            settings.mLimitsMax = std::max(maxLimit, 0.0f);
            settings.mLimitsSpringSettings = Spring(limitFrequency, limitDamping);
        }
        settings.mMaxFrictionForce = std::max(friction, 0.0f);
        settings.mMotorSettings = JPH::MotorSettings(std::max(motorFrequency, 0.001f), std::max(motorDamping, 0.0f), std::max(motorMaxForce, 0.0f), FLT_MAX);
        return settings.Create(a, b);
    }

    void SliderConstraint::Configure(JPH::TwoBodyConstraint& constraint)
    {
        auto& slider = static_cast<JPH::SliderConstraint&>(constraint);
        slider.SetMotorState(MotorState(motor));
        slider.SetTargetVelocity(motorTarget);
        slider.SetTargetPosition(motorTarget);
    }

    void SliderConstraint::Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const
    {
        const auto& slider = static_cast<const JPH::SliderConstraint&>(constraint);
        const JPH::Vector<2> position = slider.GetTotalLambdaPosition();
        linear = std::sqrt(position[0] * position[0] + position[1] * position[1]);
        angular = slider.GetTotalLambdaRotation().Length();
    }

    void SliderConstraint::SetMotor(Physics::ConstraintMotor mode, float targetValue)
    {
        motor = mode;
        motorTarget = targetValue;
        Reconfigure();
        if (Live() && mode != Physics::ConstraintMotor::Off && m_Runtime.manager)
            m_Runtime.manager->WakeConstraintBodies(m_Runtime.a, m_Runtime.b);
    }

    float SliderConstraint::GetPosition() const
    {
        return Live() ? static_cast<const JPH::SliderConstraint*>(Live())->GetCurrentPosition() : 0.0f;
    }

    DistanceConstraint::DistanceConstraint(std::shared_ptr<Actor> parent, uint32_t local_id) : PhysicsConstraint(parent, local_id)
    {
    }

    ordered_json DistanceConstraint::Serialize()
    {
        return SerializeReflected("constraintDistance");
    }

    std::shared_ptr<Component> DistanceConstraint::Clone() const
    {
        return Object::Create<DistanceConstraint>(*this);
    }

    JPH::TwoBodyConstraint* DistanceConstraint::Create(const Frame& frame, JPH::Body& a, JPH::Body& b)
    {
        JPH::Vec3 scale = JPH::Vec3::sReplicate(1.0f);
        if (const std::shared_ptr<Actor> other = GetTarget())
            if (other->transform)
                scale = ToJolt(other->transform->GetWorldScale());

        JPH::DistanceConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = frame.pivot;
        settings.mPoint2 = b.GetPosition() + b.GetRotation() * (ToJolt(targetAnchor) * scale);
        settings.mMinDistance = minDistance;
        settings.mMaxDistance = maxDistance;
        if (minDistance >= 0.0f && maxDistance >= 0.0f && maxDistance < minDistance)
            settings.mMaxDistance = minDistance;
        settings.mLimitsSpringSettings = Spring(springFrequency, springDamping);
        return settings.Create(a, b);
    }

    void DistanceConstraint::Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float&) const
    {
        linear = std::abs(static_cast<const JPH::DistanceConstraint&>(constraint).GetTotalLambdaPosition());
    }

    ConeConstraint::ConeConstraint(std::shared_ptr<Actor> parent, uint32_t local_id) : PhysicsConstraint(parent, local_id)
    {
    }

    ordered_json ConeConstraint::Serialize()
    {
        return SerializeReflected("constraintCone");
    }

    std::shared_ptr<Component> ConeConstraint::Clone() const
    {
        return Object::Create<ConeConstraint>(*this);
    }

    JPH::TwoBodyConstraint* ConeConstraint::Create(const Frame& frame, JPH::Body& a, JPH::Body& b)
    {
        const JPH::Vec3 twist = (frame.rotation * ToJolt(axis)).NormalizedOr(JPH::Vec3::sAxisY());

        JPH::ConeConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPoint1 = settings.mPoint2 = frame.pivot;
        settings.mTwistAxis1 = settings.mTwistAxis2 = twist;
        settings.mHalfConeAngle = Radians(std::clamp(coneAngle, 0.0f, 180.0f));
        return settings.Create(a, b);
    }

    void ConeConstraint::Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const
    {
        const auto& cone = static_cast<const JPH::ConeConstraint&>(constraint);
        linear = cone.GetTotalLambdaPosition().Length();
        angular = std::abs(cone.GetTotalLambdaRotation());
    }

    SwingTwistConstraint::SwingTwistConstraint(std::shared_ptr<Actor> parent, uint32_t local_id) : PhysicsConstraint(parent, local_id)
    {
    }

    ordered_json SwingTwistConstraint::Serialize()
    {
        return SerializeReflected("constraintSwingTwist");
    }

    std::shared_ptr<Component> SwingTwistConstraint::Clone() const
    {
        return Object::Create<SwingTwistConstraint>(*this);
    }

    JPH::TwoBodyConstraint* SwingTwistConstraint::Create(const Frame& frame, JPH::Body& a, JPH::Body& b)
    {
        const JPH::Vec3 twist = (frame.rotation * ToJolt(twistAxis)).NormalizedOr(JPH::Vec3::sAxisX());
        const JPH::Vec3 plane = PerpendicularTo(twist);

        const float low = std::clamp(std::min(twistMin, twistMax), -180.0f, 180.0f);
        const float high = std::clamp(std::max(twistMin, twistMax), -180.0f, 180.0f);

        JPH::SwingTwistConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;
        settings.mPosition1 = settings.mPosition2 = frame.pivot;
        settings.mTwistAxis1 = settings.mTwistAxis2 = twist;
        settings.mPlaneAxis1 = settings.mPlaneAxis2 = plane;
        settings.mSwingType = swingShape == Physics::SwingShape::Pyramid ? JPH::ESwingType::Pyramid : JPH::ESwingType::Cone;
        settings.mNormalHalfConeAngle = Radians(std::clamp(swingAngle, 0.0f, 180.0f));
        settings.mPlaneHalfConeAngle = Radians(std::clamp(planeAngle, 0.0f, 180.0f));
        settings.mTwistMinAngle = Radians(low);
        settings.mTwistMaxAngle = Radians(high);
        settings.mMaxFrictionTorque = std::max(friction, 0.0f);
        return settings.Create(a, b);
    }

    void SwingTwistConstraint::Measure(const JPH::TwoBodyConstraint& constraint, float& linear, float& angular) const
    {
        const auto& joint = static_cast<const JPH::SwingTwistConstraint&>(constraint);
        linear = joint.GetTotalLambdaPosition().Length();
        const float twist = joint.GetTotalLambdaTwist();
        const float swingY = joint.GetTotalLambdaSwingY();
        const float swingZ = joint.GetTotalLambdaSwingZ();
        angular = std::sqrt(twist * twist + swingY * swingY + swingZ * swingZ);
    }
}
