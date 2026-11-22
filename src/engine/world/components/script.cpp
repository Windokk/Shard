#include "script.hpp"

#include "script.reflection.hpp"

#include "engine/world/actor.hpp"
#include "engine/world/world.hpp"

namespace Shard::Engine::Objects::Components
{
    Script::Script(std::shared_ptr<Actor> parent, uint32_t local_id) : Component(parent, local_id)
    {    
    }

    Worlds::Scheduler* Script::GetScheduler()
    {
        if(!parent || !parent->world)
            return nullptr;
        timers = parent->world->GetSchedulerRef();
        return &parent->world->GetScheduler();
    }

    // Not shared_from_this : it throws for a script that was not made through Object::Create
    Worlds::TimerHandle Script::After(double seconds, Worlds::Scheduler::Callback fn)
    {
        Worlds::Scheduler* s = GetScheduler();
        auto self = weak_from_this().lock();
        return s && self ? s->After(seconds, std::move(fn), self) : 0;
    }

    Worlds::TimerHandle Script::Every(double interval, Worlds::Scheduler::Callback fn)
    {
        Worlds::Scheduler* s = GetScheduler();
        auto self = weak_from_this().lock();
        return s && self ? s->Every(interval, std::move(fn), self) : 0;
    }

    Worlds::TimerHandle Script::StartCoroutine(Worlds::Scheduler::Coroutine step)
    {
        Worlds::Scheduler* s = GetScheduler();
        auto self = weak_from_this().lock();
        return s && self ? s->Start(std::move(step), self) : 0;
    }

    bool Script::StopTimer(Worlds::TimerHandle handle)
    {
        auto s = timers.lock();
        return s && s->Cancel(handle, static_cast<Core::Object*>(this));
    }

    void Script::StopTimers()
    {
        if(auto s = timers.lock())
            s->CancelOwned(static_cast<Core::Object*>(this));
    }

    uint64_t Script::ListenCollisions(uint32_t kinds, Physics::SubscribeOptions options)
    {
        if(!parent || !parent->GetEngineContext())
            return 0;
        Events::ICollisionHost* host = parent->GetEngineContext()->GetCollisionHost();
        auto self = weak_from_this().lock();
        if(!host || !self)
            return 0;
        std::weak_ptr<Script> weak = std::static_pointer_cast<Script>(self);
        const uint64_t id = host->Subscribe(parent, kinds, std::move(options), [weak](const Events::CollisionEvent& event) {
            auto script = weak.lock();
            if(!script)
                return;
            switch(event.kind)
            {
                case Collision::EventKind::OverlapBegin: script->OnOverlapBegin(event); break;
                case Collision::EventKind::OverlapEnd: script->OnOverlapEnd(event); break;
                case Collision::EventKind::Hit: script->OnHit(event); break;
            }
        }, reinterpret_cast<uint64_t>(this));
        if(id)
            collisionListeners.ids.push_back(id);
        return id;
    }

    void Script::StopListeningCollisions()
    {
        if(collisionListeners.ids.empty() || !parent || !parent->GetEngineContext())
            return;
        if(Events::ICollisionHost* host = parent->GetEngineContext()->GetCollisionHost())
            host->UnsubscribeOwner(reinterpret_cast<uint64_t>(this));
        collisionListeners.ids.clear();
    }

    void Script::Destroy()
    {
        StopListeningCollisions();
        StopTimers();
        this->OnDestroyed();
    }

    void Script::OnWorldLoaded()
    {
    }

    void Script::OnWorldUnloaded()
    {
    }

    void Script::OnCreate()
    {
    }

    void Script::OnPlay()
    {
    }

    void Script::OnTick()
    {
    }

    bool Script::RunsInEditMode() const
    {
        return false;
    }

    void Script::OnEditTick()
    {
    }

    void Script::OnFixedTick()
    {
    }

    void Script::OnLateTick()
    {
    }

    void Script::OnStop()
    {
    }

    void Script::OnDestroyed()
    {
    }

    void Script::OnContactAdded(const Events::ContactAddedEvent& event)
    {
    }

    void Script::OnContactPersisted(const Events::ContactPersistedEvent& event)
    {
    }

    void Script::OnContactEnded(const Events::ContactRemovedEvent& event)
    {
    }

    void Script::OnOverlapBegin(const Events::CollisionEvent& event)
    {
    }

    void Script::OnOverlapEnd(const Events::CollisionEvent& event)
    {
    }

    void Script::OnHit(const Events::CollisionEvent& event)
    {
    }

    void Script::OnActivated()
    {
    }

    void Script::OnDeactivated()
    {

    }
    
    std::shared_ptr<Component> Script::Clone() const
    {
        auto cloned = Object::Create<Script>(*this);

        return cloned;
    }
}
