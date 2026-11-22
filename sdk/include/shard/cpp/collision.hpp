// Shard C++ SDK : collision profiles at run time, overlap and hit events, temporary changes (shard_collision.h).
//
//   class Door : public Shard::Script {
//   public:
//       static constexpr uint32_t kCollisionEvents = Shard::CollisionKinds::OverlapBegin | Shard::CollisionKinds::OverlapEnd;
//       void OnOverlapBegin(const Shard::CollisionEvent& e) override { if (e.other.HasTag("Player")) Open(); }
//       void OnOverlapEnd(const Shard::CollisionEvent& e) override { if (e.other.HasTag("Player")) Close(); }
//   };
//
//   auto scope = actor.CollisionOverride({ .ignore = "Enemy" });     // ignores enemies until `scope` is destroyed
//   actor.IgnoreCollisionWith(shooter);
//   std::string why = Shard::Physics::Explain(a, b);
//
// A script gets the events it names in kCollisionEvents, nothing is produced for the others. Outside a script, a Shard::CollisionListener
// collects the events of an actor and the plugin reads them with Poll() after the physics step.
#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <shard/shard_collision.h>
#include <shard/cpp/actor.hpp>
#include <shard/cpp/detail.hpp>

namespace Shard {

    struct CollisionKinds {
        static constexpr uint32_t OverlapBegin = SHARD_COLLISION_OVERLAP_BEGIN;
        static constexpr uint32_t OverlapEnd = SHARD_COLLISION_OVERLAP_END;
        static constexpr uint32_t Hit = SHARD_COLLISION_HIT;
        static constexpr uint32_t All = SHARD_COLLISION_ALL;
    };

    enum class EndCause : uint32_t {
        None = SHARD_COLLISION_CAUSE_NONE,
        Separated = SHARD_COLLISION_CAUSE_SEPARATED,
        Destroyed = SHARD_COLLISION_CAUSE_DESTROYED,
        Disabled = SHARD_COLLISION_CAUSE_DISABLED,
        ProfileChanged = SHARD_COLLISION_CAUSE_PROFILE_CHANGED,
        Teleported = SHARD_COLLISION_CAUSE_TELEPORTED,
    };

    /// An overlap that began or ended, or a hit between two colliders that block each other
    struct CollisionEvent {
        uint32_t kind = 0;          ///< one of CollisionKinds
        uint64_t step = 0;          ///< the fixed step it happened in
        Actor self;                 ///< the actor that listens
        Actor other;                ///< what it touched
        double point[3] = {};       ///< where, in the world
        Vec3 normal;                ///< the direction pointing out of `other`
        float impactSpeed = 0.f;    ///< speed of approach along the normal, before the contact is resolved
        float impulse = 0.f;        ///< estimated from the impact speed and the masses
        EndCause cause = EndCause::None;    ///< for an overlap end
        std::string surface;        ///< the surface tag of `other`, or empty

        static CollisionEvent From(const ShardCollisionEvent& raw)
        {
            CollisionEvent e;
            e.kind = raw.kind;
            e.step = raw.step;
            e.self = Actor(raw.self);
            e.other = Actor(raw.other);
            for (int i = 0; i < 3; ++i) e.point[i] = raw.point[i];
            e.normal = detail::FromArray(raw.normal);
            e.impactSpeed = raw.impactSpeed;
            e.impulse = raw.impulse;
            e.cause = EndCause(raw.cause);
            const char* text = reinterpret_cast<const char*>(raw.surface);
            e.surface.assign(text, strnlen(text, sizeof(raw.surface)));
            return e;
        }
    };

    /// A temporary change of a collider
    struct CollisionChange {
        std::string ignore;             ///< a tag query : what it matches is ignored ("Enemy & !Status.Dead")
        int priority = 0;               ///< higher wins when several changes touch the same thing
        std::string name;               ///< who asks, shown by Explain
        uint32_t mode = 0;              ///< 0 unchanged, 1 Solid, 2 Sensor, 3 QueryOnly, 4 Disabled
        uint32_t defaultResponse = 0;   ///< 0 unchanged, 1 Ignore, 2 Overlap, 3 Block
    };

    namespace detail {
        inline const ShardCollisionApi& CollisionApi() { return Require<ShardCollisionApi>(SHARD_IFACE_COLLISION); }
    }

    /// Gives the change back when destroyed (or Release()), and with the actor
    class CollisionScope {
    public:
        CollisionScope() = default;
        CollisionScope(ShardActor actor, uint64_t id) : m_Actor(actor), m_Id(id) {}
        CollisionScope(CollisionScope&& o) noexcept : m_Actor(o.m_Actor), m_Id(o.m_Id) { o.m_Id = 0; }
        CollisionScope& operator=(CollisionScope&& o) noexcept { if (this != &o) { Release(); m_Actor = o.m_Actor; m_Id = o.m_Id; o.m_Id = 0; } return *this; }
        CollisionScope(const CollisionScope&) = delete;
        CollisionScope& operator=(const CollisionScope&) = delete;
        ~CollisionScope() { Release(); }
        explicit operator bool() const { return m_Id != 0; }
        void Release()
        {
            if (m_Id && detail::Host()) {
                if (const ShardCollisionApi* api = detail::Interface<ShardCollisionApi>(SHARD_IFACE_COLLISION)) api->pop_override(detail::PluginHandle(), m_Actor, m_Id);
            }
            m_Id = 0;
        }

    private:
        ShardActor m_Actor = 0;
        uint64_t m_Id = 0;
    };

    /// The events of one actor, read after the physics step. Nothing is produced for a pair until a listener asks for it.
    class CollisionListener {
    public:
        CollisionListener() = default;
        CollisionListener(Actor actor, uint32_t kinds = CollisionKinds::All, std::string_view other = {}, float minImpactSpeed = 0.f, bool once = false)
        {
            detail::Check(detail::CollisionApi().subscribe(detail::PluginHandle(), actor.Handle(), kinds, Str(other), minImpactSpeed, once ? SHARD_COLLISION_ONCE : 0u, &m_Sub), "a collision listener");
        }
        CollisionListener(CollisionListener&& o) noexcept : m_Sub(o.m_Sub) { o.m_Sub = 0; }
        CollisionListener& operator=(CollisionListener&& o) noexcept { if (this != &o) { Reset(); m_Sub = o.m_Sub; o.m_Sub = 0; } return *this; }
        CollisionListener(const CollisionListener&) = delete;
        CollisionListener& operator=(const CollisionListener&) = delete;
        ~CollisionListener() { Reset(); }
        explicit operator bool() const { return m_Sub != 0; }

        /// What arrived since the last call, oldest first
        std::vector<CollisionEvent> Poll(uint32_t maxEvents = 256) const
        {
            std::vector<CollisionEvent> out;
            if (!m_Sub) return out;
            std::vector<ShardCollisionEvent> raw(maxEvents);
            for (ShardCollisionEvent& r : raw) r.structSize = sizeof(ShardCollisionEvent);
            uint32_t count = 0;
            if (detail::CollisionApi().poll_events(detail::PluginHandle(), m_Sub, raw.data(), maxEvents, &count) != SHARD_OK) return out;
            for (uint32_t i = 0; i < count && i < raw.size(); ++i) out.push_back(CollisionEvent::From(raw[i]));
            return out;
        }
        void Reset()
        {
            if (m_Sub && detail::Host()) {
                if (const ShardCollisionApi* api = detail::Interface<ShardCollisionApi>(SHARD_IFACE_COLLISION)) api->unsubscribe(detail::PluginHandle(), m_Sub);
            }
            m_Sub = 0;
        }

    private:
        ShardCollisionSub m_Sub = 0;
    };

    inline std::vector<Actor> Actor::Overlapping() const
    {
        std::vector<Actor> out;
        uint32_t count = 0;
        if (detail::CollisionApi().get_overlaps(detail::PluginHandle(), m_Handle, nullptr, 0, &count) != SHARD_OK || count == 0) return out;
        std::vector<ShardActor> raw(count);
        if (detail::CollisionApi().get_overlaps(detail::PluginHandle(), m_Handle, raw.data(), count, &count) != SHARD_OK) return out;
        for (uint32_t i = 0; i < count && i < raw.size(); ++i) out.push_back(Actor(raw[i]));
        return out;
    }

    inline bool Actor::IgnoreCollisionWith(Actor other, bool ignore) const
    {
        return detail::CollisionApi().ignore_collision(detail::PluginHandle(), m_Handle, other.Handle(), ignore ? 1 : 0) == SHARD_OK;
    }

    inline bool Actor::SetCollisionProfile(std::string_view profile) const
    {
        return detail::CollisionApi().set_profile(detail::PluginHandle(), m_Handle, Str(profile)) == SHARD_OK;
    }

    inline CollisionScope Actor::CollisionOverride(const CollisionChange& change) const
    {
        uint64_t id = 0;
        if (detail::CollisionApi().push_override(detail::PluginHandle(), m_Handle, Str(change.name), change.priority, Str(change.ignore), change.mode, change.defaultResponse, &id) != SHARD_OK) return CollisionScope();
        return CollisionScope(m_Handle, id);
    }
}
