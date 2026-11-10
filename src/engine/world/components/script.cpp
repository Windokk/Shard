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

    void Script::Destroy()
    {
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
