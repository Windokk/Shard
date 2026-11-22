#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/core/collision/collision_classes.hpp"
#include "engine/core/collision/collision_diagnostics.hpp"
#include "engine/core/collision/collision_overrides.hpp"
#include "engine/core/collision/collision_recorder.hpp"
#include "engine/core/collision/collision_tracker.hpp"
#include "engine/core/collision/interaction_table.hpp"
#include "engine/world/collision_events.hpp"

namespace Shard::Engine::Objects {
    class Actor;
    namespace Components {
        class PhysicsBody;
    }
}

namespace Shard::Engine::Physics {

    class PhysicsManager;
    class CollisionSystem;

    constexpr uint32_t kMaxTrackedBodies = 65536;

    struct ColliderState {
        JPH::BodyID body;
        uint32_t group = 0;
        std::string profile;
        bool moving = false;
        Collision::ResolvedProfile base;
        Collision::OverrideStack overrides;
        Collision::ClassId cls = Collision::kInvalidClass;
        Collision::Mode mode = Collision::Mode::Solid;
        Collision::EventOptions events;
        Collision::PhysicsMaterial material;
        bool hasMaterial = false;
        Core::TagContainer liveTags;
        Core::Tag surface;
        uint32_t listeners = 0;
    };

    struct ExplainedSide {
        bool hasProfile = false;
        std::string actor;
        std::string profile;
        std::string material;
        Collision::Mode mode = Collision::Mode::Solid;
        bool moving = false;
        std::vector<std::string> overrides;
    };

    struct Explanation {
        Collision::Response response = Collision::Response::Ignore;
        ExplainedSide sides[2];
        bool groupIgnored = false;
        std::vector<std::string> reasons;
        std::vector<std::string> lines;
        std::string Text() const;
    };

    struct DebugContact {
        uint64_t step = 0;
        glm::dvec3 point = glm::dvec3(0.0);
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
        float impactSpeed = 0.0f;
        Collision::Response response = Collision::Response::Ignore;
        bool persisted = false;
    };

    struct DebugEvent {
        uint64_t step = 0;
        Collision::EventKind kind = Collision::EventKind::Hit;
        Collision::EndCause cause = Collision::EndCause::None;
        glm::dvec3 point = glm::dvec3(0.0);
        bool hasPoint = false;
        float impactSpeed = 0.0f;
        std::string self;
        std::string other;
    };

    struct ActionContext {
        const Collision::Action& action;
        std::shared_ptr<Objects::Actor> first;
        std::shared_ptr<Objects::Actor> second;
        glm::dvec3 point = glm::dvec3(0.0);
        glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
        float impactSpeed = 0.0f;
    };

    using ActionHandler = std::function<void(CollisionSystem&, const ActionContext&)>;

    class ClassBroadPhaseInterface final : public JPH::BroadPhaseLayerInterface {
    public:
        explicit ClassBroadPhaseInterface(const Collision::ClassTable& classes) : m_Classes(classes) {}
        JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
        {
            return JPH::BroadPhaseLayer(layer < m_Classes.Slots() && m_Classes.Get(layer).moving ? 1 : 0);
        }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() ? "MOVING" : "NON_MOVING"; }
#endif
    private:
        const Collision::ClassTable& m_Classes;
    };

    class ClassObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        explicit ClassObjectVsBroadPhaseFilter(const Collision::ClassTable& classes) : m_Classes(classes) {}
        bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override
        {
            if (layer < m_Classes.Slots() && m_Classes.Get(layer).moving)
                return true;
            return broad.GetValue() == 1;
        }
    private:
        const Collision::ClassTable& m_Classes;
    };

    class ClassObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
    public:
        explicit ClassObjectLayerPairFilter(const Collision::ClassTable& classes) : m_Classes(classes) {}
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
        {
            const uint32_t slots = m_Classes.Slots();
            if (a >= slots || b >= slots)
                return false;
            return m_Classes.PairResponse(a, b) != Collision::Response::Ignore;
        }
    private:
        const Collision::ClassTable& m_Classes;
    };

    class PairIgnoreFilter final : public JPH::GroupFilter {
    public:
        bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override
        {
            if (m_Pairs.empty())
                return true;
            return m_Pairs.find(Key(a.GetSubGroupID(), b.GetSubGroupID())) == m_Pairs.end();
        }
        static uint64_t Key(uint32_t a, uint32_t b)
        {
            return a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a);
        }
        void Add(uint32_t a, uint32_t b) { m_Pairs.insert(Key(a, b)); }
        void Remove(uint32_t a, uint32_t b) { m_Pairs.erase(Key(a, b)); }
        void RemoveGroup(uint32_t group);
        bool Contains(uint32_t a, uint32_t b) const { return m_Pairs.count(Key(a, b)) != 0; }
    private:
        std::unordered_set<uint64_t> m_Pairs;
    };

    class CollisionSystem final : public Events::ICollisionHost {
    public:
        CollisionSystem();
        ~CollisionSystem();

        void Init(PhysicsManager* manager);
        void Shutdown();

        JPH::BroadPhaseLayerInterface& BroadPhaseInterface() { return m_BroadPhase; }
        JPH::ObjectVsBroadPhaseLayerFilter& ObjectVsBroadPhase() { return m_ObjectVsBroad; }
        JPH::ObjectLayerPairFilter& PairFilter() { return m_PairFilter; }

        Collision::ClassTable& Classes() { return m_Classes; }
        const Collision::ClassTable& Classes() const { return m_Classes; }
        Collision::Recorder& GetRecorder() { return m_Recorder; }
        Collision::InteractionTable& Interactions() { return m_Interactions; }

        bool MakeState(const std::string& profile, bool moving, const Collision::Patch* inspector, ColliderState& out, std::string* error = nullptr);
        void Configure(const ColliderState& state, JPH::BodyCreationSettings& settings, JPH::EMotionType motion);
        void Attach(JPH::BodyID body, ColliderState&& state, Objects::Components::PhysicsBody* component);
        void Detach(JPH::BodyID body, Collision::EndCause cause);
        void Discard(ColliderState& state);

        uint64_t PushOverride(JPH::BodyID body, const std::string& owner, int priority, Collision::Patch patch);
        bool PopOverride(JPH::BodyID body, uint64_t id);
        size_t RemoveOverrides(JPH::BodyID body, const std::string& owner);
        bool SetProfile(JPH::BodyID body, const std::string& profile, std::string* error = nullptr);
        void RefreshProfiles();

        void IgnoreCollision(JPH::BodyID a, JPH::BodyID b, bool ignore);
        bool IsIgnoring(JPH::BodyID a, JPH::BodyID b) const;
        void NotifyTeleport(JPH::BodyID body);

        Explanation Explain(JPH::BodyID a, JPH::BodyID b) const;
        std::vector<JPH::BodyID> Overlapping(JPH::BodyID body) const;
        const ColliderState* StateOf(JPH::BodyID body) const;
        Collision::ClassId ClassOf(JPH::BodyID body) const;

        uint64_t Subscribe(const std::shared_ptr<Objects::Actor>& actor, uint32_t kinds, SubscribeOptions options, Events::CollisionHandler handler, uint64_t ownerToken = 0) override;
        bool Unsubscribe(uint64_t id);
        size_t UnsubscribeOwner(uint64_t ownerToken) override;
        size_t SubscriptionCount() const { return m_Subscriptions.size(); }

        void RegisterAction(const std::string& name, ActionHandler handler);
        bool HasAction(const std::string& name) const { return m_Actions.count(name) != 0; }

        std::vector<Collision::Diagnostic> Diagnose() const;
        void SetRecording(bool on);

        void SetDebugTap(bool on);
        bool DebugTap() const { return m_DebugTap; }
        void ClearDebug();
        const std::deque<DebugContact>& DebugContacts() const { return m_DebugContacts; }
        const std::deque<DebugEvent>& DebugEvents() const { return m_DebugEvents; }
        uint64_t CurrentStep() const { return m_Step; }
        std::shared_ptr<Objects::Actor> ActorOf(JPH::BodyID body) const;
        bool Recording() const { return m_Recorder.Enabled() && m_Recording; }

        void BeginStep();
        void EndStep(uint64_t step);

        JPH::ValidateResult OnValidate(const JPH::Body& a, const JPH::Body& b);
        void OnContact(bool added, const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings& settings);
        void OnContactRemoved(const JPH::SubShapeIDPair& pair);

        float CombineFriction(const JPH::Body& a, const JPH::Body& b) const;
        float CombineRestitution(const JPH::Body& a, const JPH::Body& b) const;

        class RowFilter final : public JPH::ObjectLayerFilter {
        public:
            RowFilter(const Collision::ClassTable& classes, Collision::ClassId asClass, Collision::Response minimum)
                : m_Classes(classes), m_As(asClass), m_Minimum(minimum) {}
            bool ShouldCollide(JPH::ObjectLayer layer) const override;
        private:
            const Collision::ClassTable& m_Classes;
            Collision::ClassId m_As;
            Collision::Response m_Minimum;
        };

        class MaskFilter final : public JPH::ObjectLayerFilter {
        public:
            MaskFilter(const Collision::ClassTable& classes, uint32_t mask) : m_Classes(classes), m_Mask(mask) {}
            bool ShouldCollide(JPH::ObjectLayer layer) const override;
        private:
            const Collision::ClassTable& m_Classes;
            uint32_t m_Mask;
        };

        static bool QueryVisible(const Collision::ClassInfo& info);
        static CollisionSystem*& Active();

    private:
        struct RawContact {
            uint8_t kind = 0;
            Collision::Response response = Collision::Response::Ignore;
            uint32_t body1 = 0;
            uint32_t body2 = 0;
            uint32_t sub1 = 0;
            uint32_t sub2 = 0;
            float normal[3] = { 0, 0, 0 };
            double point[3] = { 0, 0, 0 };
            float impactSpeed = 0.0f;
        };

        struct Shard_ {
            std::mutex mutex;
            std::vector<RawContact> contacts;
        };

        struct Subscription {
            uint64_t id = 0;
            uint64_t owner = 0;
            int actorId = 0;
            uint32_t kinds = 0;
            SubscribeOptions options;
            Events::CollisionHandler handler;
            bool checked = false;
            bool dead = false;
        };

        bool Listens(uint32_t indexA, uint32_t indexB, Collision::ClassId a, Collision::ClassId b) const;
        void Reclass(ColliderState& state, Collision::EndCause cause);
        void RefreshFlags(ColliderState& state);
        void RefreshListenFlags();
        void RefreshLiveTags();
        void RefreshClassListens();
        void FlushPairs(uint64_t body, Collision::EndCause cause);
        void Emit(const Collision::TrackerEvent& event, const RawContact* contact);
        void Deliver(Events::CollisionEvent& event);
        void RunInteractions(const Events::CollisionEvent& event);
        void CheckListeners();
        void RegisterBuiltinActions();
        Objects::Components::PhysicsBody* ComponentOf(uint32_t bodyValue) const;
        ColliderState* MutableState(JPH::BodyID body);
        static uint32_t IndexOf(uint32_t bodyValue) { return JPH::BodyID(bodyValue).GetIndex(); }

        PhysicsManager* m_Manager = nullptr;
        Collision::ClassTable m_Classes;
        ClassBroadPhaseInterface m_BroadPhase;
        ClassObjectVsBroadPhaseFilter m_ObjectVsBroad;
        ClassObjectLayerPairFilter m_PairFilter;
        JPH::Ref<JPH::GroupFilter> m_GroupFilterRef;
        PairIgnoreFilter* m_GroupFilter = nullptr;

        std::unordered_map<uint32_t, ColliderState> m_States;
        std::vector<ColliderState*> m_StateAt;
        std::vector<uint8_t> m_BodyListens;
        std::vector<uint8_t> m_BodyIgnores;
        std::vector<uint8_t> m_BodyCombine;
        std::vector<uint8_t> m_ClassListens;
        uint32_t m_NextGroup = 1;
        bool m_AnyDynamic = false;
        uint64_t m_ClassRevisionSeen = 0;
        uint64_t m_TableRevisionSeen = 0;
        bool m_Recording = false;
        bool m_DebugTap = false;
        std::deque<DebugContact> m_DebugContacts;
        std::deque<DebugEvent> m_DebugEvents;

        static constexpr size_t kDebugHistory = 2048;
        static constexpr size_t kShards = 16;
        Shard_ m_Shards[kShards];
        std::vector<RawContact> m_Batch;

        Collision::OverlapTracker m_Tracker;
        std::vector<Collision::TrackerEvent> m_Scratch;
        std::vector<Events::CollisionEvent> m_Carry;
        Collision::Recorder m_Recorder;
        Collision::InteractionTable m_Interactions;

        std::vector<Subscription> m_Subscriptions;
        std::unordered_map<int, uint32_t> m_ActorListens;
        uint64_t m_NextSubscription = 1;
        bool m_CheckListeners = false;
        std::unordered_map<std::string, ActionHandler> m_Actions;
        uint64_t m_Step = 0;
        std::unordered_set<std::string> m_Warned;
        uint64_t m_RegistryVersionSeen = 0;
    };
}
