#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>

#include <shard/shard_collision.h>

#include <Jolt/Jolt.h>

#include "engine/core/collision/collision_profile.hpp"
#include "engine/core/tags/tag_query.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/plugins/abi/c_handles.hpp"
#include "engine/plugins/abi/c_internal.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Plugins::CAbi {

    namespace {
        using Objects::Components::PhysicsBody;

        constexpr size_t kMaxQueued = 4096;
        constexpr size_t kMaxSubscriptions = 1024;

        struct PollSub {
            std::mutex mutex;
            std::deque<ShardCollisionEvent> queue;
            uint64_t host = 0;
        };

        struct OwnerState {
            std::vector<std::pair<JPH::BodyID, uint64_t>> overrides;
            std::vector<std::pair<JPH::BodyID, JPH::BodyID>> ignores;
        };

        OwnedTable<PollSub> gSubs;
        std::mutex gMutex;
        std::unordered_map<PluginOwner, OwnerState> gOwners;

        Physics::PhysicsManager* Manager()
        {
            Core::IEngineContext* engine = Core::gSharedEnginePtr;
            return engine ? engine->GetPhysicsManager() : nullptr;
        }

        std::string OwnerName(PluginOwner owner) { return "plugin " + std::to_string(uint64_t(owner)); }

        void Release(PluginOwner owner)
        {
            Physics::PhysicsManager* physics = Manager();
            OwnerState state;
            {
                std::lock_guard<std::mutex> lock(gMutex);
                auto it = gOwners.find(owner);
                if (it != gOwners.end()) {
                    state = std::move(it->second);
                    gOwners.erase(it);
                }
            }
            const std::vector<std::shared_ptr<PollSub>> subs = gSubs.RemoveOwner(owner);
            if (!physics) return;
            for (const auto& sub : subs)
                physics->Collision().Unsubscribe(sub->host);
            for (const auto& [body, id] : state.overrides)
                physics->Collision().PopOverride(body, id);
            for (const auto& [a, b] : state.ignores)
                physics->Collision().IgnoreCollision(a, b, false);
        }

        PluginContext* Hook(ShardPluginCtx handle)
        {
            PluginContext* ctx = FindContext(handle);
            if (!ctx) return nullptr;
            const PluginOwner owner = PluginOwner(handle);
            bool hook = false;
            {
                std::lock_guard<std::mutex> lock(gMutex);
                hook = gOwners.find(owner) == gOwners.end();
                if (hook) gOwners[owner];
            }
            if (hook) ctx->OnRelease("collision", [owner] { Release(owner); });
            return ctx;
        }

        JPH::BodyID BodyIdOf(const std::shared_ptr<Objects::Actor>& actor)
        {
            for (const auto& body : actor->GetComponents<PhysicsBody>())
                if (!body->GetBodyID().IsInvalid()) return body->GetBodyID();
            return JPH::BodyID();
        }

        ShardResult Prepare(ShardPluginCtx handle, Physics::PhysicsManager*& physics)
        {
            if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
            if (!Hook(handle)) return SHARD_E_STALE_HANDLE;
            physics = Manager();
            return physics ? SHARD_OK : SHARD_E_UNSUPPORTED;
        }

        ShardResult BodyOfActor(ShardActor handle, std::shared_ptr<Objects::Actor>& actor, JPH::BodyID& body)
        {
            actor = ActorOf(handle);
            if (!actor) return SHARD_E_STALE_HANDLE;
            body = BodyIdOf(actor);
            return body.IsInvalid() ? SHARD_E_NOT_FOUND : SHARD_OK;
        }

    }

    void FillCollisionEvent(const Events::CollisionEvent& e, ShardCollisionEvent& out)
    {
        std::memset(&out, 0, sizeof(out));
        out.structSize = sizeof(out);
        out.kind = 1u << uint32_t(e.kind);
        out.step = e.step;
        out.self = e.self ? ActorHandle(e.self) : 0;
        out.other = e.other ? ActorHandle(e.other) : 0;
        out.point[0] = e.point.x; out.point[1] = e.point.y; out.point[2] = e.point.z;
        out.normal[0] = e.normal.x; out.normal[1] = e.normal.y; out.normal[2] = e.normal.z;
        out.impactSpeed = e.impactSpeed;
        out.impulse = e.impulse;
        out.cause = uint32_t(e.cause);
        if (e.surface.IsValid()) {
            const std::string name = e.surface.ToString();
            std::memcpy(out.surface, name.data(), std::min(name.size(), sizeof(out.surface) - 1));
        }
    }

    namespace {

        ShardResult SHARD_CALL Subscribe(ShardPluginCtx handle, ShardActor actorHandle, uint32_t kinds, ShardStr other, float minImpactSpeed, uint32_t flags, ShardCollisionSub* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                if (!(kinds & SHARD_COLLISION_ALL) || (kinds & ~SHARD_COLLISION_ALL)) return SHARD_E_INVALID_ARG;
                Physics::SubscribeOptions options;
                if (!std::isfinite(minImpactSpeed) || (flags & ~SHARD_COLLISION_ONCE)) return SHARD_E_INVALID_ARG;
                options.minImpactSpeed = minImpactSpeed;
                options.once = (flags & SHARD_COLLISION_ONCE) != 0;
                if (other.size) {
                    std::string error;
                    if (!Core::TagQuery::TryParse(ToString(other), options.other, &error)) return SHARD_E_INVALID_ARG;
                }
                Physics::PhysicsManager* physics = nullptr;
                if (const ShardResult r = Prepare(handle, physics); r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> actor = ActorOf(actorHandle);
                if (!actor) return SHARD_E_STALE_HANDLE;
                const PluginOwner owner = PluginOwner(handle);
                if (gSubs.CountOf(owner) >= kMaxSubscriptions) return SHARD_E_OUT_OF_MEMORY;

                auto sub = std::make_shared<PollSub>();
                std::weak_ptr<PollSub> weak = sub;
                sub->host = physics->Collision().Subscribe(actor, kinds, std::move(options), [weak](const Events::CollisionEvent& e) {
                    auto s = weak.lock();
                    if (!s) return;
                    ShardCollisionEvent raw;
                    FillCollisionEvent(e, raw);
                    std::lock_guard<std::mutex> lock(s->mutex);
                    if (s->queue.size() >= kMaxQueued) s->queue.pop_front();
                    s->queue.push_back(raw);
                });
                if (!sub->host) return SHARD_E_INVALID_ARG;
                *out = gSubs.Add(owner, sub);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Unsubscribe(ShardPluginCtx handle, ShardCollisionSub subscription)
        {
            try {
                Physics::PhysicsManager* physics = nullptr;
                if (const ShardResult r = Prepare(handle, physics); r != SHARD_OK) return r;
                std::shared_ptr<PollSub> sub;
                if (!gSubs.Remove(PluginOwner(handle), subscription, &sub)) return SHARD_E_NOT_FOUND;
                physics->Collision().Unsubscribe(sub->host);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL PollEvents(ShardPluginCtx handle, ShardCollisionSub subscription, ShardCollisionEvent* out, uint32_t capacity, uint32_t* count)
        {
            try {
                if (!count || (capacity && !out)) return SHARD_E_INVALID_ARG;
                *count = 0;
                for (uint32_t i = 0; i < capacity; ++i)
                    if (out[i].structSize < offsetof(ShardCollisionEvent, impulse) + sizeof(float)) return SHARD_E_INVALID_ARG;
                if (!OnScriptThread()) return SHARD_E_WRONG_THREAD;
                const std::shared_ptr<PollSub> sub = gSubs.Get(PluginOwner(handle), subscription);
                if (!sub) return SHARD_E_NOT_FOUND;
                std::lock_guard<std::mutex> lock(sub->mutex);
                while (*count < capacity && !sub->queue.empty()) {
                    const uint32_t asked = out[*count].structSize;
                    std::memcpy(&out[*count], &sub->queue.front(), std::min<size_t>(asked, sizeof(ShardCollisionEvent)));
                    out[*count].structSize = asked;
                    sub->queue.pop_front();
                    ++*count;
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL GetOverlaps(ShardPluginCtx handle, ShardActor actorHandle, ShardActor* out, uint32_t capacity, uint32_t* count)
        {
            try {
                if (!count || (capacity && !out)) return SHARD_E_INVALID_ARG;
                *count = 0;
                Physics::PhysicsManager* physics = nullptr;
                if (const ShardResult r = Prepare(handle, physics); r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> actor = ActorOf(actorHandle);
                if (!actor) return SHARD_E_STALE_HANDLE;
                std::vector<ShardActor> found;
                for (const auto& body : actor->GetComponents<PhysicsBody>()) {
                    if (body->GetBodyID().IsInvalid()) continue;
                    for (JPH::BodyID other : physics->Collision().Overlapping(body->GetBodyID())) {
                        PhysicsBody* component = physics->ComponentOf(other);
                        if (!component || !component->parent) continue;
                        const ShardActor otherHandle = ActorHandle(component->parent);
                        if (std::find(found.begin(), found.end(), otherHandle) == found.end()) found.push_back(otherHandle);
                    }
                }
                *count = uint32_t(found.size());
                for (uint32_t i = 0; i < capacity && i < found.size(); ++i) out[i] = found[i];
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL Explain(ShardPluginCtx handle, ShardActor a, ShardActor b, char* buffer, uint64_t capacity, uint64_t* size)
        {
            try {
                if (!size || (capacity && !buffer)) return SHARD_E_INVALID_ARG;
                Physics::PhysicsManager* physics = nullptr;
                if (const ShardResult r = Prepare(handle, physics); r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> actorA, actorB;
                JPH::BodyID bodyA, bodyB;
                if (const ShardResult r = BodyOfActor(a, actorA, bodyA); r != SHARD_OK) return r;
                if (const ShardResult r = BodyOfActor(b, actorB, bodyB); r != SHARD_OK) return r;
                return CopyOut(physics->Collision().Explain(bodyA, bodyB).Text(), buffer, capacity, size);
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL SetProfile(ShardPluginCtx handle, ShardActor actorHandle, ShardStr profile)
        {
            try {
                Physics::PhysicsManager* physics = nullptr;
                if (const ShardResult r = Prepare(handle, physics); r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> actor;
                JPH::BodyID body;
                if (const ShardResult r = BodyOfActor(actorHandle, actor, body); r != SHARD_OK) return r;
                const std::string name = ToString(profile);
                if (!name.empty() && !Collision::ProfileRegistry::Get().Exists(name)) return SHARD_E_NOT_FOUND;
                for (const auto& component : actor->GetComponents<PhysicsBody>()) {
                    component->collisionProfile = name;
                    component->RefreshCollision();
                }
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL PushOverride(ShardPluginCtx handle, ShardActor actorHandle, ShardStr changeName, int32_t priority, ShardStr ignore, uint32_t mode, uint32_t defaultResponse, uint64_t* out)
        {
            try {
                if (!out) return SHARD_E_INVALID_ARG;
                *out = 0;
                if (mode > 4 || defaultResponse > 3) return SHARD_E_INVALID_ARG;
                Collision::Patch patch;
                static constexpr Collision::Mode modes[] = { Collision::Mode::Solid, Collision::Mode::Solid, Collision::Mode::Sensor, Collision::Mode::QueryOnly, Collision::Mode::Disabled };
                static constexpr Collision::Response responses[] = { Collision::Response::Ignore, Collision::Response::Ignore, Collision::Response::Overlap, Collision::Response::Block };
                if (mode) patch.mode = modes[mode];
                if (defaultResponse) patch.defaultResponse = responses[defaultResponse];
                if (ignore.size) {
                    Core::TagQuery query;
                    std::string error;
                    if (!Core::TagQuery::TryParse(ToString(ignore), query, &error)) return SHARD_E_INVALID_ARG;
                    patch.rules.push_back({ query, Collision::Response::Ignore });
                }
                Physics::PhysicsManager* physics = nullptr;
                if (const ShardResult r = Prepare(handle, physics); r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> actor;
                JPH::BodyID body;
                if (const ShardResult r = BodyOfActor(actorHandle, actor, body); r != SHARD_OK) return r;
                const std::string name = OwnerName(PluginOwner(handle)) + (changeName.size ? " : " + ToString(changeName) : std::string());
                const uint64_t id = physics->Collision().PushOverride(body, name, priority, std::move(patch));
                if (!id) return SHARD_E_INVALID_ARG;
                {
                    std::lock_guard<std::mutex> lock(gMutex);
                    gOwners[PluginOwner(handle)].overrides.emplace_back(body, id);
                }
                *out = id;
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL PopOverride(ShardPluginCtx handle, ShardActor actorHandle, uint64_t id)
        {
            try {
                Physics::PhysicsManager* physics = nullptr;
                if (const ShardResult r = Prepare(handle, physics); r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> actor;
                JPH::BodyID body;
                if (const ShardResult r = BodyOfActor(actorHandle, actor, body); r != SHARD_OK) return r;
                {
                    std::lock_guard<std::mutex> lock(gMutex);
                    auto& list = gOwners[PluginOwner(handle)].overrides;
                    auto it = std::find(list.begin(), list.end(), std::make_pair(body, id));
                    if (it == list.end()) return SHARD_E_NOT_FOUND;
                    list.erase(it);
                }
                return physics->Collision().PopOverride(body, id) ? SHARD_OK : SHARD_E_NOT_FOUND;
            } catch (...) { return SHARD_E_INTERNAL; }
        }

        ShardResult SHARD_CALL IgnoreCollision(ShardPluginCtx handle, ShardActor a, ShardActor b, int32_t ignore)
        {
            try {
                Physics::PhysicsManager* physics = nullptr;
                if (const ShardResult r = Prepare(handle, physics); r != SHARD_OK) return r;
                std::shared_ptr<Objects::Actor> actorA, actorB;
                JPH::BodyID bodyA, bodyB;
                if (const ShardResult r = BodyOfActor(a, actorA, bodyA); r != SHARD_OK) return r;
                if (const ShardResult r = BodyOfActor(b, actorB, bodyB); r != SHARD_OK) return r;
                physics->Collision().IgnoreCollision(bodyA, bodyB, ignore != 0);
                std::lock_guard<std::mutex> lock(gMutex);
                auto& list = gOwners[PluginOwner(handle)].ignores;
                auto it = std::find(list.begin(), list.end(), std::make_pair(bodyA, bodyB));
                if (ignore && it == list.end()) list.emplace_back(bodyA, bodyB);
                if (!ignore && it != list.end()) list.erase(it);
                return SHARD_OK;
            } catch (...) { return SHARD_E_INTERNAL; }
        }
    }

    const void* CollisionApi()
    {
        static const ShardCollisionApi api = { sizeof(ShardCollisionApi), &Subscribe, &Unsubscribe, &PollEvents, &GetOverlaps, &Explain, &SetProfile, &PushOverride, &PopOverride, &IgnoreCollision };
        return &api;
    }
}
