#include "engine/physics/collision/collision_system.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include <Jolt/Physics/Body/BodyInterface.h>

#include "engine/core/diagnostics/logger.hpp"
#include "engine/physics/physics_body.hpp"
#include "engine/physics/physics_manager.hpp"
#include "engine/world/actor.hpp"
#include "engine/world/engine.hpp"

namespace Shard::Engine::Physics {

    using Collision::ClassId;
    using Collision::EndCause;
    using Collision::EventKind;
    using Collision::kInvalidClass;
    using Collision::Mode;
    using Collision::Response;

    namespace {
        constexpr uint32_t kQueryStaticBit = 1u;
        constexpr uint32_t kQueryMovingBit = 2u;

        Events::EventDispatcher* Dispatcher()
        {
            return Core::gSharedEnginePtr ? Core::gSharedEnginePtr->GetEventDispatcher() : nullptr;
        }

        Collision::Combine PickCombine(uint8_t a, uint8_t b)
        {
            const Collision::Combine first = Collision::Combine(a ? a - 1 : 0);
            const Collision::Combine second = Collision::Combine(b ? b - 1 : 0);
            if (!a) return second;
            if (!b) return first;
            return first == Collision::Combine::Average ? second : first;
        }

        float DefaultFriction(const JPH::Body& a, const JPH::Body& b)
        {
            return std::sqrt(a.GetFriction() * b.GetFriction());
        }

        float DefaultRestitution(const JPH::Body& a, const JPH::Body& b)
        {
            return std::max(a.GetRestitution(), b.GetRestitution());
        }
    }

    void PairIgnoreFilter::RemoveGroup(uint32_t group)
    {
        for (auto it = m_Pairs.begin(); it != m_Pairs.end();) {
            const uint32_t low = uint32_t(*it >> 32);
            const uint32_t high = uint32_t(*it & 0xFFFFFFFFu);
            if (low == group || high == group)
                it = m_Pairs.erase(it);
            else
                ++it;
        }
    }

    std::string Explanation::Text() const
    {
        std::string out;
        for (const std::string& line : lines) {
            out += line;
            out += '\n';
        }
        return out;
    }

    bool CollisionSystem::RowFilter::ShouldCollide(JPH::ObjectLayer layer) const
    {
        if (layer >= m_Classes.Slots() || !m_Classes.IsAlive(layer))
            return false;
        if (!QueryVisible(m_Classes.Get(layer)))
            return false;
        return m_Classes.SideResponse(m_As, layer) >= m_Minimum && m_Classes.SideResponse(m_As, layer) != Response::Ignore;
    }

    bool CollisionSystem::MaskFilter::ShouldCollide(JPH::ObjectLayer layer) const
    {
        if (layer >= m_Classes.Slots() || !m_Classes.IsAlive(layer))
            return false;
        const Collision::ClassInfo& info = m_Classes.Get(layer);
        if (!QueryVisible(info))
            return false;
        return ((info.moving ? kQueryMovingBit : kQueryStaticBit) & m_Mask) != 0;
    }

    bool CollisionSystem::QueryVisible(const Collision::ClassInfo& info)
    {
        return info.mode != Mode::Disabled;
    }

    CollisionSystem*& CollisionSystem::Active()
    {
        static CollisionSystem* active = nullptr;
        return active;
    }

    CollisionSystem::CollisionSystem()
        : m_BroadPhase(m_Classes), m_ObjectVsBroad(m_Classes), m_PairFilter(m_Classes)
    {
        m_StateAt.assign(kMaxTrackedBodies, nullptr);
        m_BodyListens.assign(kMaxTrackedBodies, 0);
        m_BodyIgnores.assign(kMaxTrackedBodies, 0);
        m_BodyCombine.assign(kMaxTrackedBodies, 0);
        m_Recorder.SetEnabled(true);
    }

    CollisionSystem::~CollisionSystem()
    {
        if (Active() == this)
            Active() = nullptr;
    }

    void CollisionSystem::Init(PhysicsManager* manager)
    {
        m_Manager = manager;
        m_GroupFilter = new PairIgnoreFilter();
        m_GroupFilterRef = m_GroupFilter;
        Active() = this;
        m_RegistryVersionSeen = Collision::ProfileRegistry::Get().GetVersion();
        RegisterBuiltinActions();

        Collision::ResolvedProfile fixed;
        Collision::ResolvedProfile moving;
        const Collision::ProfileRegistry& registry = Collision::ProfileRegistry::Get();
        registry.Resolve(Collision::kDefaultStaticProfile, fixed);
        registry.Resolve(Collision::kDefaultMovingProfile, moving);
        m_Classes.Acquire(fixed, false);
        m_Classes.Acquire(moving, true);
    }

    void CollisionSystem::Shutdown()
    {
        m_States.clear();
        std::fill(m_StateAt.begin(), m_StateAt.end(), nullptr);
        std::fill(m_BodyListens.begin(), m_BodyListens.end(), 0);
        std::fill(m_BodyIgnores.begin(), m_BodyIgnores.end(), 0);
        std::fill(m_BodyCombine.begin(), m_BodyCombine.end(), 0);
        std::fill(m_ClassListens.begin(), m_ClassListens.end(), 0);
        m_Classes.Clear();
        m_Tracker.Clear();
        m_Carry.clear();
        m_Batch.clear();
        for (Shard_& shard : m_Shards)
            shard.contacts.clear();
        m_Subscriptions.clear();
        m_ActorListens.clear();
        m_Recorder.Clear();
        m_GroupFilterRef = nullptr;
        m_GroupFilter = nullptr;
        m_Manager = nullptr;
        if (Active() == this)
            Active() = nullptr;
    }

    Objects::Components::PhysicsBody* CollisionSystem::ComponentOf(uint32_t bodyValue) const
    {
        return m_Manager ? m_Manager->ComponentOf(JPH::BodyID(bodyValue)) : nullptr;
    }

    ColliderState* CollisionSystem::MutableState(JPH::BodyID body)
    {
        auto it = m_States.find(body.GetIndexAndSequenceNumber());
        return it == m_States.end() ? nullptr : &it->second;
    }

    const ColliderState* CollisionSystem::StateOf(JPH::BodyID body) const
    {
        auto it = m_States.find(body.GetIndexAndSequenceNumber());
        return it == m_States.end() ? nullptr : &it->second;
    }

    ClassId CollisionSystem::ClassOf(JPH::BodyID body) const
    {
        const ColliderState* state = StateOf(body);
        return state ? state->cls : kInvalidClass;
    }

    bool CollisionSystem::MakeState(const std::string& profile, bool moving, const Collision::Patch* inspector, ColliderState& out, std::string* error)
    {
        const Collision::ProfileRegistry& registry = Collision::ProfileRegistry::Get();
        out = ColliderState();
        out.profile = profile.empty() ? (moving ? Collision::kDefaultMovingProfile : Collision::kDefaultStaticProfile) : profile;
        out.moving = moving;

        std::string failure;
        if (!registry.Resolve(out.profile, out.base, &failure)) {
            if (error) *error = failure;
            if (!profile.empty() && m_Warned.insert(failure).second)
                DEBUG_ERROR("Collision : " + failure + ", using the default profile");
            out.profile = moving ? Collision::kDefaultMovingProfile : Collision::kDefaultStaticProfile;
            if (!registry.Resolve(out.profile, out.base, &failure))
                return false;
        }

        if (inspector)
            out.overrides.Push("inspector", -1000, *inspector);

        const Collision::ResolvedProfile folded = out.overrides.Fold(out.base);
        out.cls = m_Classes.Acquire(folded, moving, error);
        if (out.cls == kInvalidClass)
            return false;
        out.mode = folded.mode;
        out.events = folded.events;
        if (const Collision::PhysicsMaterial* material = registry.FindMaterial(folded.material)) {
            out.material = *material;
            out.hasMaterial = true;
            out.surface = material->surface.empty() ? Core::Tag() : Core::Tag::Request(material->surface);
        }
        out.group = m_NextGroup++;
        return true;
    }

    void CollisionSystem::Discard(ColliderState& state)
    {
        if (state.cls != kInvalidClass)
            m_Classes.Release(state.cls);
        state.cls = kInvalidClass;
    }

    void CollisionSystem::Configure(const ColliderState& state, JPH::BodyCreationSettings& settings, JPH::EMotionType motion)
    {
        settings.mObjectLayer = JPH::ObjectLayer(state.cls);
        settings.mIsSensor = state.mode == Mode::Sensor;
        if (state.mode == Mode::Sensor && motion == JPH::EMotionType::Kinematic) {
            std::vector<ClassId> alive;
            m_Classes.CollectAlive(alive);
            for (ClassId other : alive)
                if (!m_Classes.Get(other).moving && m_Classes.PairResponse(state.cls, other) == Response::Overlap)
                    settings.mCollideKinematicVsNonDynamic = true;
        }
        if (m_GroupFilter)
            settings.mCollisionGroup = JPH::CollisionGroup(m_GroupFilter, 0, state.group);
        if (state.hasMaterial) {
            settings.mFriction = state.material.friction;
            settings.mRestitution = state.material.restitution;
            if (motion == JPH::EMotionType::Dynamic && settings.mOverrideMassProperties == JPH::EOverrideMassProperties::CalculateMassAndInertia && settings.GetShape()) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = std::max(1e-3f, settings.GetShape()->GetVolume() * state.material.density);
            }
        }
    }

    void CollisionSystem::RefreshFlags(ColliderState& state)
    {
        const uint32_t index = state.body.GetIndex();
        if (index >= kMaxTrackedBodies)
            return;
        m_BodyListens[index] = state.listeners > 0 ? 1 : 0;
        m_BodyCombine[index] = state.hasMaterial ? uint8_t(1 + uint8_t(state.material.combine)) : 0;
    }

    void CollisionSystem::Attach(JPH::BodyID body, ColliderState&& state, Objects::Components::PhysicsBody* component)
    {
        const uint32_t index = body.GetIndex();
        if (index >= kMaxTrackedBodies) {
            Discard(state);
            return;
        }
        state.body = body;
        state.listeners = 0;
        if (component && component->parent) {
            auto it = m_ActorListens.find(component->parent->GetID().GetAsInt());
            if (it != m_ActorListens.end())
                state.listeners = it->second;
            state.liveTags = m_Classes.Get(state.cls).identity;
            for (Core::Tag tag : component->parent->GetTags())
                state.liveTags.Add(tag);
        } else {
            state.liveTags = m_Classes.Get(state.cls).identity;
        }
        auto [it, inserted] = m_States.insert_or_assign(body.GetIndexAndSequenceNumber(), std::move(state));
        ColliderState& stored = it->second;
        m_StateAt[index] = &stored;
        RefreshFlags(stored);
        if (m_Classes.Get(stored.cls).hasDynamicRules)
            m_AnyDynamic = true;
    }

    void CollisionSystem::FlushPairs(uint64_t body, EndCause cause)
    {
        const size_t first = m_Scratch.size();
        m_Tracker.RemoveCollider(body, cause, m_Scratch);
        for (size_t i = first; i < m_Scratch.size(); ++i)
            Emit(m_Scratch[i], nullptr);
        m_Scratch.resize(first);
    }

    void CollisionSystem::Detach(JPH::BodyID body, EndCause cause)
    {
        auto it = m_States.find(body.GetIndexAndSequenceNumber());
        if (it == m_States.end())
            return;
        ColliderState& state = it->second;
        FlushPairs(body.GetIndexAndSequenceNumber(), cause);
        if (state.cls != kInvalidClass)
            m_Classes.Release(state.cls);
        if (m_GroupFilter)
            m_GroupFilter->RemoveGroup(state.group);
        const uint32_t index = body.GetIndex();
        if (index < kMaxTrackedBodies) {
            m_StateAt[index] = nullptr;
            m_BodyListens[index] = 0;
            m_BodyIgnores[index] = 0;
            m_BodyCombine[index] = 0;
        }
        m_States.erase(it);
    }

    void CollisionSystem::Reclass(ColliderState& state, EndCause cause)
    {
        const Collision::ResolvedProfile folded = state.overrides.Fold(state.base);
        std::string error;
        const ClassId next = m_Classes.Acquire(folded, state.moving, &error);
        if (next == kInvalidClass) {
            if (m_Warned.insert(error).second)
                DEBUG_ERROR("Collision : " + error);
            return;
        }
        state.mode = folded.mode;
        state.events = folded.events;
        if (const Collision::PhysicsMaterial* material = Collision::ProfileRegistry::Get().FindMaterial(folded.material)) {
            state.material = *material;
            state.hasMaterial = true;
            state.surface = material->surface.empty() ? Core::Tag() : Core::Tag::Request(material->surface);
        } else {
            state.hasMaterial = false;
            state.surface = Core::Tag();
        }
        RefreshFlags(state);

        const ClassId old = state.cls;
        if (next == old) {
            m_Classes.Release(next);
            return;
        }
        state.cls = next;
        m_Classes.Release(old);
        if (m_Classes.Get(next).hasDynamicRules)
            m_AnyDynamic = true;

        if (m_Manager && m_Manager->IsInitialized()) {
            JPH::BodyInterface& bodies = m_Manager->GetBodyInterface();
            bodies.SetObjectLayer(state.body, JPH::ObjectLayer(next));
            bodies.SetIsSensor(state.body, state.mode == Mode::Sensor);
            if (bodies.GetMotionType(state.body) != JPH::EMotionType::Static)
                bodies.ActivateBody(state.body);
        }

        const uint64_t self = state.body.GetIndexAndSequenceNumber();
        const EndCause why = state.mode == Mode::Disabled ? EndCause::Disabled : cause;
        for (uint64_t other : m_Tracker.Overlapping(self)) {
            const ColliderState* partner = StateOf(JPH::BodyID(uint32_t(other)));
            const bool keeps = partner && m_Classes.PairResponse(next, partner->cls) == Response::Overlap;
            if (keeps)
                continue;
            const size_t first = m_Scratch.size();
            m_Tracker.RemovePair(self, other, why, m_Scratch);
            for (size_t i = first; i < m_Scratch.size(); ++i)
                Emit(m_Scratch[i], nullptr);
            m_Scratch.resize(first);
        }

        if (m_Recorder.Enabled()) {
            Collision::Record record;
            record.step = m_Step;
            record.kind = Collision::RecordKind::Override;
            record.a = self;
            record.detail = uint8_t(next);
            m_Recorder.Push(record);
        }
    }

    uint64_t CollisionSystem::PushOverride(JPH::BodyID body, const std::string& owner, int priority, Collision::Patch patch)
    {
        ColliderState* state = MutableState(body);
        if (!state)
            return 0;
        const uint64_t id = state->overrides.Push(owner, priority, std::move(patch));
        Reclass(*state, EndCause::ProfileChanged);
        return id;
    }

    bool CollisionSystem::PopOverride(JPH::BodyID body, uint64_t id)
    {
        ColliderState* state = MutableState(body);
        if (!state || !state->overrides.Pop(id))
            return false;
        Reclass(*state, EndCause::ProfileChanged);
        return true;
    }

    size_t CollisionSystem::RemoveOverrides(JPH::BodyID body, const std::string& owner)
    {
        ColliderState* state = MutableState(body);
        if (!state)
            return 0;
        const size_t removed = state->overrides.RemoveOwner(owner);
        if (removed)
            Reclass(*state, EndCause::ProfileChanged);
        return removed;
    }

    bool CollisionSystem::SetProfile(JPH::BodyID body, const std::string& profile, std::string* error)
    {
        ColliderState* state = MutableState(body);
        if (!state)
            return false;
        Collision::ResolvedProfile resolved;
        if (!Collision::ProfileRegistry::Get().Resolve(profile, resolved, error))
            return false;
        state->profile = profile;
        state->base = std::move(resolved);
        Reclass(*state, EndCause::ProfileChanged);
        return true;
    }

    void CollisionSystem::RefreshProfiles()
    {
        Collision::ProfileRegistry& registry = Collision::ProfileRegistry::Get();
        m_RegistryVersionSeen = registry.GetVersion();
        m_Classes.RebuildAll();
        for (auto& [key, state] : m_States) {
            Collision::ResolvedProfile resolved;
            if (!registry.Resolve(state.profile, resolved))
                continue;
            state.base = std::move(resolved);
            Reclass(state, EndCause::ProfileChanged);
        }
        m_AnyDynamic = m_Classes.AnyDynamic();
    }

    void CollisionSystem::IgnoreCollision(JPH::BodyID a, JPH::BodyID b, bool ignore)
    {
        ColliderState* sa = MutableState(a);
        ColliderState* sb = MutableState(b);
        if (!sa || !sb || !m_GroupFilter)
            return;
        if (ignore) {
            m_GroupFilter->Add(sa->group, sb->group);
            const size_t first = m_Scratch.size();
            m_Tracker.RemovePair(a.GetIndexAndSequenceNumber(), b.GetIndexAndSequenceNumber(), EndCause::ProfileChanged, m_Scratch);
            for (size_t i = first; i < m_Scratch.size(); ++i)
                Emit(m_Scratch[i], nullptr);
            m_Scratch.resize(first);
        } else {
            m_GroupFilter->Remove(sa->group, sb->group);
        }
        if (m_Manager && m_Manager->IsInitialized()) {
            JPH::BodyInterface& bodies = m_Manager->GetBodyInterface();
            if (bodies.GetMotionType(a) != JPH::EMotionType::Static) bodies.ActivateBody(a);
            if (bodies.GetMotionType(b) != JPH::EMotionType::Static) bodies.ActivateBody(b);
        }
    }

    bool CollisionSystem::IsIgnoring(JPH::BodyID a, JPH::BodyID b) const
    {
        const ColliderState* sa = StateOf(a);
        const ColliderState* sb = StateOf(b);
        return sa && sb && m_GroupFilter && m_GroupFilter->Contains(sa->group, sb->group);
    }

    void CollisionSystem::NotifyTeleport(JPH::BodyID body)
    {
        if (!StateOf(body))
            return;
        FlushPairs(body.GetIndexAndSequenceNumber(), EndCause::Teleported);
    }

    std::vector<JPH::BodyID> CollisionSystem::Overlapping(JPH::BodyID body) const
    {
        std::vector<JPH::BodyID> out;
        for (uint64_t other : m_Tracker.Overlapping(body.GetIndexAndSequenceNumber()))
            out.push_back(JPH::BodyID(uint32_t(other)));
        return out;
    }

    Explanation CollisionSystem::Explain(JPH::BodyID a, JPH::BodyID b) const
    {
        Explanation out;
        const ColliderState* sa = StateOf(a);
        const ColliderState* sb = StateOf(b);
        auto fill = [&](ExplainedSide& side, const ColliderState* s, JPH::BodyID id) {
            if (!s)
                return;
            const Objects::Components::PhysicsBody* component = ComponentOf(id.GetIndexAndSequenceNumber());
            side.hasProfile = true;
            side.actor = component && component->parent ? component->parent->GetName() : std::string("?");
            side.profile = s->profile;
            side.material = s->hasMaterial ? s->material.name : std::string();
            side.mode = s->mode;
            side.moving = s->moving;
            for (const Collision::OverrideEntry& e : s->overrides.Entries())
                side.overrides.push_back(e.owner + " (priority " + std::to_string(e.priority) + ")");
        };
        fill(out.sides[0], sa, a);
        fill(out.sides[1], sb, b);
        auto describe = [&](const char* name, const ColliderState* s, JPH::BodyID id) {
            if (!s) {
                out.lines.push_back(std::string(name) + " : no collision profile (not a body of an actor)");
                return;
            }
            const Objects::Components::PhysicsBody* component = ComponentOf(id.GetIndexAndSequenceNumber());
            const std::string actor = component && component->parent ? component->parent->GetName() : std::string("?");
            out.lines.push_back(std::string(name) + " : '" + actor + "', profile '" + s->profile + "', " + Collision::ToString(s->mode) +
                (s->moving ? ", moving" : ", static"));
            for (const Collision::OverrideEntry& e : s->overrides.Entries())
                out.lines.push_back(std::string("  override '") + e.owner + "' (priority " + std::to_string(e.priority) + ")");
        };
        describe("first", sa, a);
        describe("second", sb, b);
        if (!sa || !sb) {
            out.response = Response::Ignore;
            out.lines.push_back("result : Ignore");
            return out;
        }
        if (m_GroupFilter && m_GroupFilter->Contains(sa->group, sb->group)) {
            out.lines.push_back("IgnoreCollisionWith : this pair is ignored");
            out.groupIgnored = true;
            out.response = Response::Ignore;
            out.lines.push_back("result : Ignore");
            return out;
        }
        const Collision::Decision d = m_Classes.Decide(sa->cls, sb->cls, &sa->liveTags, &sb->liveTags, true);
        for (const std::string& line : d.reasons)
            out.lines.push_back(line);
        out.reasons = d.reasons;
        out.response = d.response;
        out.lines.push_back(std::string("result : ") + Collision::ToString(d.response));
        return out;
    }

    uint64_t CollisionSystem::Subscribe(const std::shared_ptr<Objects::Actor>& actor, uint32_t kinds, SubscribeOptions options, Events::CollisionHandler handler, uint64_t ownerToken)
    {
        if (!actor || !handler || !(kinds & kAllCollisionKinds))
            return 0;
        Subscription sub;
        sub.id = m_NextSubscription++;
        sub.owner = ownerToken;
        sub.actorId = actor->GetID().GetAsInt();
        sub.kinds = kinds & kAllCollisionKinds;
        sub.options = std::move(options);
        sub.handler = std::move(handler);
        m_Subscriptions.push_back(std::move(sub));
        ++m_ActorListens[actor->GetID().GetAsInt()];
        for (const auto& body : actor->GetComponents<Objects::Components::PhysicsBody>()) {
            if (ColliderState* state = MutableState(body->GetBodyID())) {
                ++state->listeners;
                RefreshFlags(*state);
            }
        }
        m_CheckListeners = true;
        return m_Subscriptions.back().id;
    }

    bool CollisionSystem::Unsubscribe(uint64_t id)
    {
        auto it = std::find_if(m_Subscriptions.begin(), m_Subscriptions.end(), [&](const Subscription& s) { return s.id == id && !s.dead; });
        if (it == m_Subscriptions.end())
            return false;
        it->dead = true;
        it->handler = nullptr;
        const int actorId = it->actorId;
        auto listen = m_ActorListens.find(actorId);
        if (listen != m_ActorListens.end() && --listen->second == 0)
            m_ActorListens.erase(listen);
        for (auto& [key, state] : m_States) {
            const Objects::Components::PhysicsBody* component = ComponentOf(key);
            if (component && component->parent && component->parent->GetID().GetAsInt() == actorId && state.listeners > 0) {
                --state.listeners;
                RefreshFlags(state);
            }
        }
        m_Subscriptions.erase(std::remove_if(m_Subscriptions.begin(), m_Subscriptions.end(), [&](const Subscription& s) { return s.dead; }), m_Subscriptions.end());
        return true;
    }

    size_t CollisionSystem::UnsubscribeOwner(uint64_t ownerToken)
    {
        if (!ownerToken)
            return 0;
        std::vector<uint64_t> ids;
        for (const Subscription& s : m_Subscriptions)
            if (s.owner == ownerToken && !s.dead)
                ids.push_back(s.id);
        for (uint64_t id : ids)
            Unsubscribe(id);
        return ids.size();
    }

    void CollisionSystem::RegisterAction(const std::string& name, ActionHandler handler)
    {
        m_Actions[name] = std::move(handler);
    }

    std::vector<Collision::Diagnostic> CollisionSystem::Diagnose() const
    {
        std::vector<Collision::ListenerSpec> listeners;
        for (const Subscription& s : m_Subscriptions) {
            if (s.dead)
                continue;
            for (const auto& [key, state] : m_States) {
                const Objects::Components::PhysicsBody* component = ComponentOf(key);
                if (!component || !component->parent || component->parent->GetID().GetAsInt() != s.actorId)
                    continue;
                for (EventKind kind : { EventKind::OverlapBegin, EventKind::Hit }) {
                    if (!(s.kinds & KindBit(kind)))
                        continue;
                    Collision::ListenerSpec spec;
                    spec.owner = s.options.label.empty() ? component->parent->GetName() : s.options.label;
                    spec.profile = state.profile;
                    spec.kind = kind;
                    spec.other = s.options.other;
                    listeners.push_back(std::move(spec));
                }
            }
        }
        return Collision::Analyze(Collision::ProfileRegistry::Get(), listeners);
    }

    void CollisionSystem::SetRecording(bool on)
    {
        m_Recording = on;
    }

    void CollisionSystem::SetDebugTap(bool on)
    {
        m_DebugTap = on;
        if (!on)
            ClearDebug();
    }

    void CollisionSystem::ClearDebug()
    {
        m_DebugContacts.clear();
        m_DebugEvents.clear();
    }

    std::shared_ptr<Objects::Actor> CollisionSystem::ActorOf(JPH::BodyID body) const
    {
        const Objects::Components::PhysicsBody* component = ComponentOf(body.GetIndexAndSequenceNumber());
        return component ? component->parent : nullptr;
    }

    void CollisionSystem::RefreshClassListens()
    {
        m_ClassListens.assign(m_Classes.Slots(), 0);
        if (m_Interactions.Empty())
            return;
        for (ClassId id = 0; id < m_Classes.Slots(); ++id) {
            if (!m_Classes.IsAlive(id))
                continue;
            const Collision::ClassInfo& info = m_Classes.Get(id);
            if (m_Interactions.MightApply(info, EventKind::Hit) || m_Interactions.MightApply(info, EventKind::OverlapBegin) || m_Interactions.MightApply(info, EventKind::OverlapEnd))
                m_ClassListens[id] = 1;
        }
    }

    void CollisionSystem::RefreshLiveTags()
    {
        for (auto& [key, state] : m_States) {
            const Objects::Components::PhysicsBody* component = ComponentOf(key);
            state.liveTags = m_Classes.Get(state.cls).identity;
            if (component && component->parent)
                for (Core::Tag tag : component->parent->GetTags())
                    state.liveTags.Add(tag);
        }
    }

    void CollisionSystem::CheckListeners()
    {
        m_CheckListeners = false;
        for (Subscription& s : m_Subscriptions) {
            if (s.checked || s.dead)
                continue;
            for (const auto& [key, state] : m_States) {
                const Objects::Components::PhysicsBody* component = ComponentOf(key);
                if (!component || !component->parent || component->parent->GetID().GetAsInt() != s.actorId)
                    continue;
                s.checked = true;
                for (EventKind kind : { EventKind::OverlapBegin, EventKind::Hit }) {
                    if (!(s.kinds & KindBit(kind)))
                        continue;
                    Collision::ListenerSpec spec;
                    spec.owner = s.options.label.empty() ? component->parent->GetName() : s.options.label;
                    spec.profile = state.profile;
                    spec.kind = kind;
                    spec.other = s.options.other;
                    for (const Collision::Diagnostic& d : Collision::AnalyzeListener(Collision::ProfileRegistry::Get(), spec))
                        if (m_Warned.insert(d.message).second)
                            DEBUG_WARNING("Collision : " + d.message);
                }
            }
        }
    }

    void CollisionSystem::BeginStep()
    {
        Collision::ProfileRegistry& registry = Collision::ProfileRegistry::Get();
        if (registry.GetVersion() != m_RegistryVersionSeen)
            RefreshProfiles();
        if (m_ClassRevisionSeen != m_Classes.Revision() || m_TableRevisionSeen != m_Interactions.Revision()) {
            RefreshClassListens();
            m_ClassRevisionSeen = m_Classes.Revision();
            m_TableRevisionSeen = m_Interactions.Revision();
            m_AnyDynamic = m_Classes.AnyDynamic();
        }
        if (m_AnyDynamic)
            RefreshLiveTags();
        if (m_CheckListeners)
            CheckListeners();
    }

    bool CollisionSystem::Listens(uint32_t indexA, uint32_t indexB, ClassId a, ClassId b) const
    {
        if (m_Recording || m_DebugTap)
            return true;
        if (indexA < kMaxTrackedBodies && m_BodyListens[indexA])
            return true;
        if (indexB < kMaxTrackedBodies && m_BodyListens[indexB])
            return true;
        if (a < m_ClassListens.size() && m_ClassListens[a])
            return true;
        if (b < m_ClassListens.size() && m_ClassListens[b])
            return true;
        return false;
    }

    JPH::ValidateResult CollisionSystem::OnValidate(const JPH::Body& a, const JPH::Body& b)
    {
        const ClassId ca = a.GetObjectLayer();
        const ClassId cb = b.GetObjectLayer();
        if (ca >= m_Classes.Slots() || cb >= m_Classes.Slots())
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        if (!m_Classes.PairNeedsContact(ca, cb))
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        const uint32_t ia = a.GetID().GetIndex();
        const uint32_t ib = b.GetID().GetIndex();
        const ColliderState* sa = ia < kMaxTrackedBodies ? m_StateAt[ia] : nullptr;
        const ColliderState* sb = ib < kMaxTrackedBodies ? m_StateAt[ib] : nullptr;
        const Core::TagContainer& tagsA = sa ? sa->liveTags : m_Classes.Get(ca).identity;
        const Core::TagContainer& tagsB = sb ? sb->liveTags : m_Classes.Get(cb).identity;
        const Collision::Decision d = m_Classes.Decide(ca, cb, &tagsA, &tagsB, false);
        return d.response == Response::Ignore ? JPH::ValidateResult::RejectAllContactsForThisBodyPair : JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void CollisionSystem::OnContact(bool added, const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings)
    {
        const ClassId ca = a.GetObjectLayer();
        const ClassId cb = b.GetObjectLayer();
        if (ca >= m_Classes.Slots() || cb >= m_Classes.Slots())
            return;
        Response response = m_Classes.PairResponse(ca, cb);
        if (m_Classes.PairNeedsContact(ca, cb)) {
            const uint32_t ia = a.GetID().GetIndex();
            const uint32_t ib = b.GetID().GetIndex();
            const ColliderState* sa = ia < kMaxTrackedBodies ? m_StateAt[ia] : nullptr;
            const ColliderState* sb = ib < kMaxTrackedBodies ? m_StateAt[ib] : nullptr;
            const Core::TagContainer& tagsA = sa ? sa->liveTags : m_Classes.Get(ca).identity;
            const Core::TagContainer& tagsB = sb ? sb->liveTags : m_Classes.Get(cb).identity;
            response = m_Classes.Decide(ca, cb, &tagsA, &tagsB, false).response;
        }
        if (response == Response::Overlap)
            settings.mIsSensor = true;
        if (response == Response::Ignore)
            return;
        if (response == Response::Block && !added)
            return;

        if (!Listens(a.GetID().GetIndex(), b.GetID().GetIndex(), ca, cb))
            return;

        RawContact contact;
        contact.kind = added ? 0 : 1;
        contact.response = response;
        contact.body1 = a.GetID().GetIndexAndSequenceNumber();
        contact.body2 = b.GetID().GetIndexAndSequenceNumber();
        contact.sub1 = manifold.mSubShapeID1.GetValue();
        contact.sub2 = manifold.mSubShapeID2.GetValue();
        contact.normal[0] = manifold.mWorldSpaceNormal.GetX();
        contact.normal[1] = manifold.mWorldSpaceNormal.GetY();
        contact.normal[2] = manifold.mWorldSpaceNormal.GetZ();
        JPH::RVec3 point = manifold.mBaseOffset;
        if (!manifold.mRelativeContactPointsOn1.empty())
            point = manifold.GetWorldSpaceContactPointOn1(0);
        contact.point[0] = double(point.GetX());
        contact.point[1] = double(point.GetY());
        contact.point[2] = double(point.GetZ());
        if (response == Response::Block) {
            const JPH::Vec3 relative = a.GetPointVelocity(point) - b.GetPointVelocity(point);
            contact.impactSpeed = std::max(0.0f, relative.Dot(manifold.mWorldSpaceNormal));
        }

        Shard_& shard = m_Shards[(a.GetID().GetIndex() ^ b.GetID().GetIndex()) % kShards];
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.contacts.push_back(contact);
    }

    void CollisionSystem::OnContactRemoved(const JPH::SubShapeIDPair& pair)
    {
        const uint32_t ia = pair.GetBody1ID().GetIndex();
        const uint32_t ib = pair.GetBody2ID().GetIndex();
        if (ia >= kMaxTrackedBodies || ib >= kMaxTrackedBodies)
            return;
        const ColliderState* sa = m_StateAt[ia];
        const ColliderState* sb = m_StateAt[ib];
        if (!Listens(ia, ib, sa ? sa->cls : kInvalidClass, sb ? sb->cls : kInvalidClass))
            return;
        RawContact contact;
        contact.kind = 2;
        contact.body1 = pair.GetBody1ID().GetIndexAndSequenceNumber();
        contact.body2 = pair.GetBody2ID().GetIndexAndSequenceNumber();
        contact.sub1 = pair.GetSubShapeID1().GetValue();
        contact.sub2 = pair.GetSubShapeID2().GetValue();
        Shard_& shard = m_Shards[(ia ^ ib) % kShards];
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.contacts.push_back(contact);
    }

    float CollisionSystem::CombineFriction(const JPH::Body& a, const JPH::Body& b) const
    {
        const uint32_t ia = a.GetID().GetIndex();
        const uint32_t ib = b.GetID().GetIndex();
        const uint8_t ma = ia < kMaxTrackedBodies ? m_BodyCombine[ia] : 0;
        const uint8_t mb = ib < kMaxTrackedBodies ? m_BodyCombine[ib] : 0;
        if (!ma && !mb)
            return DefaultFriction(a, b);
        return Collision::CombineValues(PickCombine(ma, mb), a.GetFriction(), b.GetFriction());
    }

    float CollisionSystem::CombineRestitution(const JPH::Body& a, const JPH::Body& b) const
    {
        const uint32_t ia = a.GetID().GetIndex();
        const uint32_t ib = b.GetID().GetIndex();
        const uint8_t ma = ia < kMaxTrackedBodies ? m_BodyCombine[ia] : 0;
        const uint8_t mb = ib < kMaxTrackedBodies ? m_BodyCombine[ib] : 0;
        if (!ma && !mb)
            return DefaultRestitution(a, b);
        return Collision::CombineValues(PickCombine(ma, mb), a.GetRestitution(), b.GetRestitution());
    }

    void CollisionSystem::Emit(const Collision::TrackerEvent& event, const RawContact* contact)
    {
        Events::CollisionEvent out;
        out.kind = event.kind;
        out.step = m_Step;
        out.cause = event.cause;
        out.selfCollider = event.a;
        out.otherCollider = event.b;
        if (const auto* component = ComponentOf(uint32_t(event.a)))
            out.self = component->parent;
        if (const auto* component = ComponentOf(uint32_t(event.b)))
            out.other = component->parent;
        if (contact) {
            out.point = glm::dvec3(contact->point[0], contact->point[1], contact->point[2]);
            const glm::vec3 n(contact->normal[0], contact->normal[1], contact->normal[2]);
            out.normal = uint32_t(event.a) == contact->body1 ? -n : n;
            out.impactSpeed = contact->impactSpeed;
        }
        m_Carry.push_back(std::move(out));
    }

    void CollisionSystem::EndStep(uint64_t step)
    {
        m_Step = step;
        m_Batch.clear();
        for (Shard_& shard : m_Shards) {
            std::lock_guard<std::mutex> lock(shard.mutex);
            m_Batch.insert(m_Batch.end(), shard.contacts.begin(), shard.contacts.end());
            shard.contacts.clear();
        }

        std::sort(m_Batch.begin(), m_Batch.end(), [](const RawContact& l, const RawContact& r) {
            return std::tie(l.kind, l.body1, l.body2, l.sub1, l.sub2) < std::tie(r.kind, r.body1, r.body2, r.sub1, r.sub2);
        });

        std::vector<Events::CollisionEvent> ready;
        ready.swap(m_Carry);

        for (const RawContact& c : m_Batch) {
            if (m_Recorder.Enabled() && m_Recording) {
                Collision::Record record;
                record.step = step;
                record.kind = Collision::RecordKind::Contact;
                record.detail = uint8_t(c.kind) | uint8_t(uint8_t(c.response) << 4);
                record.a = c.body1;
                record.b = c.body2;
                record.value = c.impactSpeed;
                record.position[0] = c.point[0];
                record.position[1] = c.point[1];
                record.position[2] = c.point[2];
                m_Recorder.Push(record);
            }

            if (m_DebugTap && c.kind != 2) {
                DebugContact contact;
                contact.step = step;
                contact.point = glm::dvec3(c.point[0], c.point[1], c.point[2]);
                contact.normal = glm::vec3(c.normal[0], c.normal[1], c.normal[2]);
                contact.impactSpeed = c.impactSpeed;
                contact.response = c.response;
                contact.persisted = c.kind == 1;
                m_DebugContacts.push_back(contact);
                while (m_DebugContacts.size() > kDebugHistory)
                    m_DebugContacts.pop_front();
            }

            m_Scratch.clear();
            if (c.kind == 2) {
                m_Tracker.Remove(c.body1, c.body2, c.sub1, c.sub2, m_Scratch);
                for (const Collision::TrackerEvent& e : m_Scratch)
                    Emit(e, nullptr);
            } else if (c.response == Response::Overlap) {
                m_Tracker.Add(c.body1, c.body2, c.sub1, c.sub2, m_Scratch);
                for (const Collision::TrackerEvent& e : m_Scratch)
                    Emit(e, &c);
            } else if (c.response == Response::Block && c.kind == 0) {
                Emit({ EventKind::Hit, c.body1, c.body2, EndCause::None }, &c);
            }
            m_Scratch.clear();
        }
        m_Batch.clear();

        ready.insert(ready.end(), std::make_move_iterator(m_Carry.begin()), std::make_move_iterator(m_Carry.end()));
        m_Carry.clear();

        for (Events::CollisionEvent& event : ready)
            Deliver(event);
    }

    void CollisionSystem::Deliver(Events::CollisionEvent& event)
    {
        const ColliderState* first = StateOf(JPH::BodyID(uint32_t(event.selfCollider)));
        const ColliderState* second = StateOf(JPH::BodyID(uint32_t(event.otherCollider)));

        if (m_Recorder.Enabled() && m_Recording) {
            Collision::Record record;
            record.step = event.step;
            record.kind = Collision::RecordKind::Event;
            record.detail = uint8_t(event.kind) | uint8_t(uint8_t(event.cause) << 4);
            record.a = event.selfCollider;
            record.b = event.otherCollider;
            record.value = event.impactSpeed;
            record.position[0] = event.point.x;
            record.position[1] = event.point.y;
            record.position[2] = event.point.z;
            m_Recorder.Push(record);
        }

        if (m_DebugTap) {
            DebugEvent entry;
            entry.step = event.step;
            entry.kind = event.kind;
            entry.cause = event.cause;
            entry.point = event.point;
            entry.hasPoint = event.point != glm::dvec3(0.0);
            entry.impactSpeed = event.impactSpeed;
            entry.self = event.self ? event.self->GetName() : std::string();
            entry.other = event.other ? event.other->GetName() : std::string();
            m_DebugEvents.push_back(std::move(entry));
            while (m_DebugEvents.size() > kDebugHistory)
                m_DebugEvents.pop_front();
        }

        for (int direction = 0; direction < 2; ++direction) {
            Events::CollisionEvent e = event;
            const ColliderState* self = first;
            const ColliderState* other = second;
            if (direction == 1) {
                std::swap(e.self, e.other);
                std::swap(e.selfCollider, e.otherCollider);
                e.normal = -e.normal;
                std::swap(self, other);
            }
            e.surface = other ? other->surface : Core::Tag();
            if (!e.self)
                continue;
            if (self) {
                if (e.kind == EventKind::Hit && (!self->events.hits || e.impactSpeed < self->events.minImpactSpeed))
                    continue;
                if (e.kind != EventKind::Hit && !self->events.overlaps)
                    continue;
            }
            e.sourceObjectID = e.self->GetID();

            const int actorId = e.self->GetID().GetAsInt();
            if (!m_Subscriptions.empty() && m_ActorListens.count(actorId)) {
                std::vector<uint64_t> matches;
                for (const Subscription& s : m_Subscriptions) {
                    if (s.dead || s.actorId != actorId || !(s.kinds & KindBit(e.kind)))
                        continue;
                    if (e.kind == EventKind::Hit && e.impactSpeed < s.options.minImpactSpeed)
                        continue;
                    if (!s.options.other.IsEmpty()) {
                        Core::TagContainer tags;
                        if (other)
                            tags = other->liveTags;
                        if (e.other)
                            for (Core::Tag tag : e.other->GetTags())
                                tags.Add(tag);
                        if (!s.options.other.Matches(tags))
                            continue;
                    }
                    matches.push_back(s.id);
                }
                for (uint64_t id : matches) {
                    Events::CollisionHandler handler;
                    bool once = false;
                    for (const Subscription& s : m_Subscriptions)
                        if (s.id == id && !s.dead) { handler = s.handler; once = s.options.once; break; }
                    if (!handler)
                        continue;
                    handler(e);
                    if (once)
                        Unsubscribe(id);
                }
            }

            if (Events::EventDispatcher* dispatcher = Dispatcher())
                dispatcher->emitToActor(e.self->GetID(), e);
        }

        if (!m_Interactions.Empty())
            RunInteractions(event);
    }

    void CollisionSystem::RunInteractions(const Events::CollisionEvent& event)
    {
        auto tagsOf = [&](const std::shared_ptr<Objects::Actor>& actor, const ColliderState* state) {
            Core::TagContainer tags;
            if (state) {
                tags = m_Classes.IsAlive(state->cls) ? m_Classes.Get(state->cls).identity : Core::TagContainer();
                if (state->surface.IsValid())
                    tags.Add(state->surface);
            }
            if (actor)
                for (Core::Tag tag : actor->GetTags())
                    tags.Add(tag);
            return tags;
        };

        const ColliderState* first = StateOf(JPH::BodyID(uint32_t(event.selfCollider)));
        const ColliderState* second = StateOf(JPH::BodyID(uint32_t(event.otherCollider)));
        const Core::TagContainer tagsA = tagsOf(event.self, first);
        const Core::TagContainer tagsB = tagsOf(event.other, second);
        const Collision::InteractionMatch match = m_Interactions.Find(tagsA, tagsB, event.kind, event.impactSpeed);
        if (!match.row)
            return;

        for (const Collision::Action& action : match.row->actions) {
            auto it = m_Actions.find(action.type);
            if (it == m_Actions.end()) {
                if (m_Warned.insert("action:" + action.type).second)
                    DEBUG_WARNING("Collision : the interaction '" + match.row->name + "' uses the unknown action '" + action.type + "'");
                continue;
            }
            ActionContext context{ action, match.swapped ? event.other : event.self, match.swapped ? event.self : event.other,
                                   event.point, match.swapped ? -event.normal : event.normal, event.impactSpeed };
            it->second(*this, context);
        }
    }

    void CollisionSystem::RegisterBuiltinActions()
    {
        auto targets = [](const ActionContext& c, const char* fallback) {
            std::vector<std::shared_ptr<Objects::Actor>> out;
            const std::string* target = c.action.Param("target");
            const std::string which = target ? *target : fallback;
            if (which == "self" || which == "both") out.push_back(c.first);
            if (which == "other" || which == "both") out.push_back(c.second);
            return out;
        };

        RegisterAction("Destroy", [targets](CollisionSystem&, const ActionContext& c) {
            for (auto& actor : targets(c, "self"))
                if (actor) actor->Destroy();
        });

        RegisterAction("ApplyImpulse", [targets](CollisionSystem&, const ActionContext& c) {
            const float strength = c.action.ParamFloat("strength", 1.0f);
            for (auto& actor : targets(c, "other")) {
                if (!actor) continue;
                const float sign = actor == c.first ? 1.0f : -1.0f;
                for (const auto& body : actor->GetComponents<Objects::Components::PhysicsBody>())
                    body->AddImpulse(c.normal * strength * sign);
            }
        });

        RegisterAction("SendEvent", [](CollisionSystem&, const ActionContext& c) {
            const std::string* name = c.action.Param("name");
            if (!name || name->empty()) return;
            if (Events::EventDispatcher* dispatcher = Dispatcher())
                dispatcher->emitNamed(*name, nullptr, 0);
        });

        RegisterAction("AddTag", [targets](CollisionSystem&, const ActionContext& c) {
            const std::string* name = c.action.Param("tag");
            if (!name) return;
            const Core::Tag tag = Core::Tag::Request(*name);
            if (!tag.IsValid()) return;
            for (auto& actor : targets(c, "self"))
                if (actor) actor->GetTags().Add(tag);
        });

        RegisterAction("RemoveTag", [targets](CollisionSystem&, const ActionContext& c) {
            const std::string* name = c.action.Param("tag");
            if (!name) return;
            const Core::Tag tag = Core::Tag::Request(*name);
            if (!tag.IsValid()) return;
            for (auto& actor : targets(c, "self"))
                if (actor) actor->GetTags().Remove(tag);
        });
    }
}
